#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <thread>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <iomanip>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>

#include "recomp.h"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/rsp.hpp"
#include "ultramodern/input.hpp"
#include "f3dex2.hpp"
#include "rom_loader.hpp"
#include "vi_scanout.hpp"
#include "controller_input.hpp"
#include "controller_mapper.hpp"
#include "graphics_enhancements.hpp"

// Recompiled function headers and overlay definitions
#include "funcs.h"
#include "recomp_overlays.inl"

extern "C" void recomp_syscall_handler(uint8_t* rdram, recomp_context* ctx, int32_t instruction_vram) {
    std::fprintf(stderr, "[Syscall] Syscall triggered at VRAM 0x%08X\n", instruction_vram);
}

extern uint64_t total_vis;

// Global input and window state
static std::atomic<uint16_t> g_buttons{0};
static std::atomic<float> g_stick_x{0.0f};
static std::atomic<float> g_stick_y{0.0f};
static std::atomic<bool> g_running{true};
static bool g_is_headless = false;
static bool g_presentation_check = false;
static bool g_performance_check = false;
static bool g_native_gpu = false;
static int g_hd_priority=THREAD_PRIORITY_NORMAL;
static std::atomic<uint64_t> g_native_gpu_tasks{0},g_native_gpu_fallbacks{0};
static std::atomic<uint32_t> g_render_height{0};
static bool g_resolution_check = false;
static std::atomic<bool> g_resolution_check_done{false};
static std::atomic<uint64_t> g_native_render_us{0},g_native_render_count{0},g_native_render_max_us{0};
static std::filesystem::path g_graphics_config;
static bool g_legacy_version = false;
static int g_original_resolution = -1; // -1: game, 0: off, 1: half, 2: full
static bool g_original_resolution_applied = false;

static int original_resolution_value(const std::wstring& value) {
    if(value==L"game")return -1;
    if(value==L"off")return 0;
    if(value==L"half")return 1;
    if(value==L"full")return 2;
    return -2;
}

// Called on the game thread by the explicit USA v0 generation hook, after
// defaults/save loading. No repeated writes and no video reconfiguration here.
extern "C" void tgod_apply_original_resolution(uint8_t* rdram) {
    if(g_original_resolution_applied)return;
    g_original_resolution_applied=true;
    if(g_original_resolution>=0 && g_original_resolution<=2) {
        // MEM_* expects a sign-extended KSEG base, not a positive 32-bit
        // absolute address in its offset argument.
        MEM_B(0,(gpr)(int32_t)0x80102344)=uint8_t(g_original_resolution);
        std::cout<<"[Graphics] Original startup mode: "
            <<(g_original_resolution==2 ? "Full":g_original_resolution==1 ? "Half":"Off")
            <<"; subsequent Setup choices remain available.\n";
    }
}
static HWND g_hwnd = nullptr;
static std::atomic<bool> g_gpu_window_owned{false};
static std::atomic<bool> g_filter_smooth{true};
static std::mutex g_window_output_mutex;
static std::atomic<uint64_t> g_rsp_task_types[5]{}; // 0..3 plus unclassified.

static std::mutex g_input_mutex;
static tgod::input::Snapshot g_controllers;

static void update_key_input() {
    if(g_is_headless)return;
    tgod::controls::poll();
    const auto snapshot=tgod::controls::snapshot();
    {std::lock_guard lock(g_input_mutex);g_controllers=snapshot;}
    g_buttons=snapshot[0].buttons;g_stick_x=snapshot[0].x;g_stick_y=snapshot[0].y;
}

// Native Win32 Decoupled Audio Backend
class Win32AudioBackend {
public:
    static constexpr size_t BUFFER_CHUNK_SAMPLES = 1152; // 576 stereo frames (~26ms @ 22kHz)
    static constexpr size_t NUM_BUFFERS = 16;

    struct Block {
        WAVEHDR hdr{};
        int16_t pcm[BUFFER_CHUNK_SAMPLES];
        size_t ai_frames = 0;
        bool non_silent = false;
    };

    HWAVEOUT h_wave_out = nullptr;
    HANDLE h_event = nullptr;
    std::atomic<uint32_t> current_freq{22050};
    std::atomic<bool> running{true};
    std::atomic<uint32_t> buffers_queued{0};
    std::atomic<uint64_t> total_samples_queued{0};
    std::atomic<size_t> in_flight_blocks{0};
    std::atomic<uint64_t> device_blocks_submitted{0};
    std::atomic<uint64_t> device_blocks_completed{0};
    std::atomic<uint64_t> device_starvation_transitions{0};
    std::atomic<uint64_t> non_silent_queue_accepts{0};
    std::atomic<uint64_t> non_silent_device_blocks_submitted{0};
    std::atomic<size_t> ai_frames_pending{0};

    std::mutex queue_mutex;
    std::vector<int16_t> pending_samples;
    std::thread worker_thread;
    bool enabled = true;
    std::atomic<uint64_t> audio_latency_overruns{0};
    std::atomic<uint64_t> silent_audio_inputs_discarded{0};
    std::atomic<uint64_t> pending_audio_frames_replaced{0};
    bool drop_announced = false;

    void start() {
        if (worker_thread.joinable()) return;
        running = true;
        h_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        worker_thread = std::thread(&Win32AudioBackend::audio_worker, this);
    }

    ~Win32AudioBackend() {
        shutdown();
        if (h_event) {
            CloseHandle(h_event);
            h_event = nullptr;
        }
    }

    void shutdown() {
        running = false;
        if (h_event) {
            SetEvent(h_event);
        }
        if (worker_thread.joinable()) {
            worker_thread.join();
        }
    }

    int32_t buffers_in_flight() {
        return (int32_t)in_flight_blocks.load(std::memory_order_relaxed);
    }

    void set_frequency(uint32_t freq) {
        if (freq == 0) freq = 22050;
        current_freq.store(freq);
        if (h_event) SetEvent(h_event);
    }

    void queue_samples(const int16_t* samples, size_t sample_count) {
        if (!enabled || !samples || sample_count == 0) return;
        bool has_nonzero = false;
        for (size_t s = 0; s < sample_count; ++s) {
            if (samples[s] != 0) { has_nonzero = true; break; }
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            // Match the non-blocking audio queues used by the reference ports.
            // This callback runs on a cooperative N64 thread, so waiting here
            // can stop the whole game scheduler. Bound latency by dropping only
            // stale silence when playback has fallen more than 400 ms behind.
            // This game can submit many silent AI blocks while initializing;
            // dropping all later blocks would then strand the actual music.
            const size_t queued_frames = pending_samples.size() / 2 +
                in_flight_blocks.load(std::memory_order_relaxed) * (BUFFER_CHUNK_SAMPLES / 2);
            const size_t max_queued_frames = static_cast<size_t>(current_freq.load()) * 2 / 5;
            if (queued_frames > max_queued_frames) {
                audio_latency_overruns.fetch_add(1, std::memory_order_relaxed);
                if (!has_nonzero) {
                    silent_audio_inputs_discarded.fetch_add(1, std::memory_order_relaxed);
                    if (!drop_announced) {
                        drop_announced = true;
                        std::cerr << "[Audio] Queue exceeded 400 ms; dropping silent buffers to keep the game thread running.\n";
                    }
                    return;
                }
                // Prefer new music over stale queued silence. WaveOut buffers
                // already submitted remain in order; only the software queue
                // is replaced, keeping latency bounded.
                const size_t discarded_frames = pending_samples.size() / 2;
                pending_samples.clear();
                if (discarded_frames != 0) {
                    ai_frames_pending.fetch_sub(discarded_frames, std::memory_order_relaxed);
                    pending_audio_frames_replaced.fetch_add(discarded_frames, std::memory_order_relaxed);
                }
            }
            size_t old_sz = pending_samples.size();
            pending_samples.resize(old_sz + sample_count);
            // N64 recomp stereo word swap: swap Left & Right to restore host order
            for (size_t i = 0; i + 1 < sample_count; i += 2) {
                pending_samples[old_sz + i] = samples[i + 1];
                pending_samples[old_sz + i + 1] = samples[i];
            }
            ai_frames_pending.fetch_add(sample_count / 2, std::memory_order_relaxed);
        }
        uint32_t count = buffers_queued.fetch_add(1, std::memory_order_relaxed);
        total_samples_queued.fetch_add(sample_count, std::memory_order_relaxed);
        if (has_nonzero) non_silent_queue_accepts.fetch_add(1, std::memory_order_relaxed);
        if (count < 3) {
            std::cout << "[Audio Buffer #" << count << "] Queued " << sample_count
                      << " samples at " << current_freq.load() << " Hz\n";
        }
        if (h_event) SetEvent(h_event);
    }

    size_t get_frames_remaining() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        size_t pending_frames = pending_samples.size() / 2;
        size_t in_flight_frames = in_flight_blocks.load(std::memory_order_relaxed) * (BUFFER_CHUNK_SAMPLES / 2);
        return pending_frames + in_flight_frames;
    }

    size_t get_ai_frames_remaining() const {
        return ai_frames_pending.load(std::memory_order_relaxed);
    }

    void audio_worker() {
        SetThreadDescription(GetCurrentThread(), L"TGOD Win32 Audio Worker");
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        HWAVEOUT hwo = nullptr;
        uint32_t active_freq = 0;
        Block blocks[NUM_BUFFERS]{};
        size_t block_idx = 0;

        // Headless diagnostics capture the exact host-channel PCM submitted to
        // WaveOut, including silence. Bound disk use to 60 seconds; this records
        // content and ordering, not gaps at the physical playback device.
        std::ofstream capture;
        uint32_t capture_rate = 0, capture_bytes = 0;
        bool capture_finished = false;
        uint32_t capture_segment = 0;
        double captured_seconds = 0;
        std::string capture_path;
        auto finish_capture = [&]() {
            if (!capture.is_open()) return;
            capture.seekp(0);
            auto u16 = [&](uint16_t v) { const char b[] = {char(v), char(v >> 8)}; capture.write(b, 2); };
            auto u32 = [&](uint32_t v) { const char b[] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; capture.write(b, 4); };
            capture.write("RIFF", 4); u32(36 + capture_bytes); capture.write("WAVEfmt ", 8);
            u32(16); u16(1); u16(2); u32(capture_rate); u32(capture_rate * 4);
            u16(4); u16(16); capture.write("data", 4); u32(capture_bytes);
            capture.close();
            std::cout << "[Audio Forensics] Device PCM capture: " << capture_path << " ("
                << capture_bytes / 4 << " stereo frames at " << capture_rate << " Hz)\n";
        };
        auto capture_block = [&](const int16_t* pcm, size_t sample_count) {
            if (!g_is_headless || capture_finished) return;
            if (capture_rate != active_freq && capture.is_open()) finish_capture();
            if (!capture.is_open()) {
                if (capture_segment == 8) { capture_finished = true; return; }
                capture_rate = active_freq;
                capture_bytes = 0;
                capture_path = "audio_device_capture_" + std::to_string(capture_segment++) + "_" +
                    std::to_string(capture_rate) + "Hz.wav";
                capture.open(capture_path, std::ios::binary | std::ios::trunc);
                char header[44]{};
                capture.write(header, sizeof(header));
                if (!capture) { capture_finished = true; return; }
            }
            const size_t remaining_frames = size_t(std::max(0.0, 60.0 - captured_seconds) * capture_rate);
            const uint32_t bytes = uint32_t(std::min(sample_count / 2, remaining_frames) * 4);
            capture.write(reinterpret_cast<const char*>(pcm), bytes);
            if (!capture) { capture_finished = true; return; }
            capture_bytes += bytes;
            captured_seconds += double(bytes / 4) / capture_rate;
            if (bytes < sample_count * sizeof(int16_t)) capture_finished = true;
        };

        auto close_device = [&]() {
            if (hwo) {
                waveOutReset(hwo);
                for (auto& b : blocks) {
                    if (b.hdr.dwFlags & WHDR_PREPARED) {
                        waveOutUnprepareHeader(hwo, &b.hdr, sizeof(WAVEHDR));
                    }
                    if (b.ai_frames != 0) {
                        ai_frames_pending.fetch_sub(b.ai_frames, std::memory_order_relaxed);
                        b.ai_frames = 0;
                        ultramodern::send_ai_message();
                    }
                    ZeroMemory(&b.hdr, sizeof(WAVEHDR));
                }
                waveOutClose(hwo);
                hwo = nullptr;
                active_freq = 0;
                in_flight_blocks.store(0);
            }
        };

        auto reap_completed_buffers = [&]() {
            if (!hwo) return;
            const bool was_playing = in_flight_blocks.load(std::memory_order_relaxed) != 0;
            for (auto& b : blocks) {
                if ((b.hdr.dwFlags & (WHDR_PREPARED | WHDR_DONE)) == (WHDR_PREPARED | WHDR_DONE)) {
                    if (waveOutUnprepareHeader(hwo, &b.hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                        in_flight_blocks.fetch_sub(1, std::memory_order_relaxed);
                        device_blocks_completed.fetch_add(1, std::memory_order_relaxed);
                        if (b.ai_frames != 0) {
                            ai_frames_pending.fetch_sub(b.ai_frames, std::memory_order_relaxed);
                            b.ai_frames = 0;
                            ultramodern::send_ai_message();
                        }
                        ZeroMemory(&b.hdr, sizeof(WAVEHDR));
                    }
                }
            }
            if (was_playing && in_flight_blocks.load(std::memory_order_relaxed) == 0)
                device_starvation_transitions.fetch_add(1, std::memory_order_relaxed);
        };

        while (running.load()) {
            // Count only waveOut buffers that are still playing so the backend's
            // queue-depth callback reflects consumed audio promptly.
            reap_completed_buffers();

            uint32_t req_freq = current_freq.load();
            if (req_freq != active_freq) {
                close_device();
                active_freq = req_freq;
                WAVEFORMATEX wfx{};
                wfx.wFormatTag = WAVE_FORMAT_PCM;
                wfx.nChannels = 2;
                wfx.nSamplesPerSec = active_freq;
                wfx.wBitsPerSample = 16;
                wfx.nBlockAlign = (2 * 16) / 8;
                wfx.nAvgBytesPerSec = active_freq * wfx.nBlockAlign;
                wfx.cbSize = 0;
                MMRESULT res = waveOutOpen(&hwo, WAVE_MAPPER, &wfx, (DWORD_PTR)h_event, 0, CALLBACK_EVENT);
                if (res != MMSYSERR_NOERROR) {
                    hwo = nullptr;
                } else {
                    std::cout << "[Audio Device] Successfully opened Win32 waveOut device at " << active_freq << " Hz.\n";
                }
            }

            std::vector<int16_t> chunk;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (!running.load()) break;

                if (pending_samples.size() >= BUFFER_CHUNK_SAMPLES) {
                    chunk.assign(pending_samples.begin(), pending_samples.begin() + BUFFER_CHUNK_SAMPLES);
                    pending_samples.erase(pending_samples.begin(), pending_samples.begin() + BUFFER_CHUNK_SAMPLES);
                }
            }

            if (chunk.empty()) {
                // Wake for either new PCM or WOM_DONE, so the game's AI wait
                // resumes as soon as a DMA block ends instead of polling at 20 ms.
                WaitForSingleObject(h_event, 20);
                continue;
            }

            if (!chunk.empty() && hwo) {
                Block& b = blocks[block_idx];
                block_idx = (block_idx + 1) % NUM_BUFFERS;

                if (b.hdr.dwFlags & WHDR_PREPARED) {
                    while (!(b.hdr.dwFlags & WHDR_DONE) && running.load()) {
                        WaitForSingleObject(h_event, 10);
                    }
                    waveOutUnprepareHeader(hwo, &b.hdr, sizeof(WAVEHDR));
                    if (in_flight_blocks > 0) in_flight_blocks.fetch_sub(1, std::memory_order_relaxed);
                }
                std::memcpy(b.pcm, chunk.data(), BUFFER_CHUNK_SAMPLES * sizeof(int16_t));
                b.non_silent = false;
                for (int16_t sample : b.pcm) {
                    if (sample != 0) { b.non_silent = true; break; }
                }
                ZeroMemory(&b.hdr, sizeof(WAVEHDR));
                b.hdr.lpData = reinterpret_cast<LPSTR>(b.pcm);
                b.hdr.dwBufferLength = static_cast<DWORD>(BUFFER_CHUNK_SAMPLES * sizeof(int16_t));
                b.ai_frames = BUFFER_CHUNK_SAMPLES / 2;

                bool submitted = false;
                if (waveOutPrepareHeader(hwo, &b.hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                    if (waveOutWrite(hwo, &b.hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                        in_flight_blocks.fetch_add(1, std::memory_order_relaxed);
                        device_blocks_submitted.fetch_add(1, std::memory_order_relaxed);
                        if (b.non_silent) non_silent_device_blocks_submitted.fetch_add(1, std::memory_order_relaxed);
                        capture_block(b.pcm, BUFFER_CHUNK_SAMPLES);
                        submitted = true;
                    } else {
                        waveOutUnprepareHeader(hwo, &b.hdr, sizeof(WAVEHDR));
                    }
                }
                if (submitted) {
                    // The exact AI byte count is retired by the completion path.
                    // b.ai_frames is retired when this exact waveOut block ends.
                } else {
                    ai_frames_pending.fetch_sub(b.ai_frames, std::memory_order_relaxed);
                    b.ai_frames = 0;
                    ultramodern::send_ai_message();
                }
            } else if (!chunk.empty()) {
                // The device could not be opened; drain the host queue and
                // wake any N64 thread waiting for an AI completion.
                ai_frames_pending.fetch_sub(chunk.size() / 2, std::memory_order_relaxed);
                ultramodern::send_ai_message();
            }
        }

        close_device();
        finish_capture();
    }
};

static Win32AudioBackend g_audio_backend;

static uint32_t graphics_scene(const uint8_t* ram) {
    uint32_t mode=0;std::memcpy(&mode,ram+0x117DA4,4);
    uint32_t width=0,height=0;
    std::memcpy(&width,ram+0x117D14,4);std::memcpy(&height,ram+0x11D55C,4);
    return (mode&255)|(uint32_t(ram[0xE97C2^3])<<8)|
        (mode==0 ? uint32_t(ram[0x53650^3])<<16 : 0)|
        (uint32_t(width==640)<<24)|(uint32_t(height==120)<<25)|(uint32_t(height==480)<<26);
}

static void save_fb_to_bmp(const char*,const uint8_t*,uint32_t,uint32_t,uint32_t,size_t = 8*1024*1024);

// Native RDP results remain authoritative for RSP/DP completion. HD replays
// run on separate private RDRAM and never delay those events.
class AsyncHDRenderer {
    struct Job {
        std::vector<uint8_t> ram;
        F3DEX2Interpreter interpreter;
        uint32_t dl=0,data=0,data_size=0,height=0,scene=0,offset=0;
        uint32_t scanout_width=0,scanout_height=0;
        uint64_t generation=0;
        std::chrono::steady_clock::time_point captured{};
    };
    struct Frame {
        std::vector<uint16_t> pixels;
        uint32_t width=0,height=0,scene=0,requested_height=0;
        uint64_t generation=0;
        std::chrono::steady_clock::time_point captured{};
    };
    struct Screen {
        HWND window=nullptr;
        std::vector<uint16_t> native;
        uint32_t width=1,height=1,scene=0,requested_height=0;
        bool smooth=true,visible=false;
    };
    struct Resident {
        bool valid=false;
        uint32_t address=0,width=0,height=0,offset=0,scene=0,requested_height=0;
        uint64_t generation=0;
        std::chrono::steady_clock::time_point captured{};
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::unique_ptr<Job> pending,spare;
    std::unique_ptr<Screen> screen_pending;
    Frame latest;
    std::thread worker;
    bool stopping=false;
    uint32_t requested_height=0;
    uint64_t generation=0;
    std::atomic<bool> worker_done{true};
    std::atomic<uint64_t> completed{0},replaced{0},last_ms{0},gpu_frames{0};
    std::atomic<uint64_t> replay_us{0},replays{0},display_age_ms{0},resident_frames{0};
    std::atomic<uint64_t> live_gpu_us{0};
    std::atomic<uint64_t> snapshot_us{0},snapshot_count{0},screen_bytes{0};
    uint64_t reported_gpu_samples=0,reported_gpu_us=0; // GPU worker only
    GPURaster::Statistics final_stats{};
    void start_locked() {
        if(!worker.joinable()){worker_done=false;worker=std::thread([this]{run();});}
        wake.notify_one();
    }
    void recycle(std::unique_ptr<Job> job) {
        job->interpreter.render_surfaces.clear();
        std::lock_guard lock(mutex);
        if(!stopping && !spare)spare=std::move(job);
    }
    void run() {
        if(!SetThreadPriority(GetCurrentThread(),g_hd_priority))
            std::cerr<<"[HD worker] Cannot set requested thread priority.\n";
        std::cout<<"[HD worker] Priority: "<<GetThreadPriority(GetCurrentThread())<<" (normal=0).\n";
        std::unique_ptr<GPURaster> gpu;
        bool gpu_failed=false,direct_failed=false;
        Screen screen;
        Resident resident;
        bool performance_capture=false;
        uint32_t performance_race_jobs=0;
        for (;;) {
            std::unique_ptr<Job> job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock,[&]{return stopping||pending||screen_pending;});
                if(stopping)break;
                job=std::move(pending);
                if(screen_pending){screen=std::move(*screen_pending);screen_pending.reset();}
            }
            if(job) {
                if(g_performance_check&&(job->scene&255)==2)++performance_race_jobs;
                resident.valid=false;
                const auto start=std::chrono::steady_clock::now();
                auto& r=job->interpreter;
                r.rdram=job->ram.data();r.diagnostics_enabled=false;
                r.render_surfaces.clear();r.render_surfaces.set_height(job->height);
                if(!gpu)gpu=std::make_unique<GPURaster>();
                const bool hardware=gpu->available()&&!gpu_failed;
                const bool direct=hardware&&g_hwnd&&!direct_failed;
                if(hardware) {
                    try {
                        gpu->begin_frame(direct);r.raster_backend=gpu.get();
                        r.process_display_list(job->dl,job->data,job->data_size);
                        gpu_frames.fetch_add(1);
                    } catch(const std::exception& error) {
                        std::cerr<<"[HD GPU] Replay failed: "<<error.what()<<"; software fallback.\n";
                        gpu_failed=true;r.raster_backend=nullptr;
                        gpu->release_presentation();g_gpu_window_owned=false;
                        if(g_hwnd)InvalidateRect(g_hwnd,nullptr,FALSE);
                        recycle(std::move(job));continue;
                    }
                    r.raster_backend=nullptr;
                } else r.process_display_list(job->dl,job->data,job->data_size);
                const bool matching=job->scanout_width==r.color_image_width&&job->scanout_height&&
                    job->scanout_height<=r.framebuffer_height()&&job->scanout_height+8>=r.framebuffer_height();
                Frame frame;
                frame.captured=job->captured;frame.scene=job->scene;
                frame.requested_height=job->height;frame.generation=job->generation;
                bool valid=direct;
                if(direct) {
                    resident={true,r.color_image_addr,r.color_image_width,
                        matching ? job->scanout_height:r.framebuffer_height(),matching ? job->offset:0,
                        job->scene,job->height,job->generation,job->captured};
                    resident_frames.fetch_add(1);
                } else valid=r.render_surfaces.copy_scanout(r.color_image_addr+(matching ? job->offset:0),
                    r.color_image_width,matching ? job->scanout_height:r.framebuffer_height(),
                    frame.pixels,frame.width,frame.height);
                const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
                last_ms=us/1000;replay_us.fetch_add(us);replays.fetch_add(1);
                {
                    std::lock_guard lock(mutex);
                    if(valid&&!stopping&&job->generation==generation&&job->height==requested_height){
                        std::swap(latest,frame);completed.fetch_add(1);
                    } else resident.valid=false;
                    // Use the latest VI/scene after a replay, not the screen
                    // state from before it started computing.
                    if(screen_pending){screen=std::move(*screen_pending);screen_pending.reset();}
                }
                recycle(std::move(job));
            }
            if(screen.window&&gpu&&gpu->available()&&!gpu_failed&&!direct_failed) {
                try {
                    // GDI and flip presentation never share the HWND after
                    // ownership is acquired. Native mode is presented by this
                    // same GPU worker once the swapchain has been activated.
                    {std::lock_guard lock(g_window_output_mutex);g_gpu_window_owned=true;}
                    bool compatible=false;
                    {
                        std::lock_guard lock(mutex);
                        compatible=resident.valid&&screen.visible&&resident.scene==screen.scene&&
                            resident.requested_height==screen.requested_height&&resident.generation==generation;
                    }
                    compatible=compatible&&gpu->has_target(resident.address,resident.width);
                    const auto before_present=gpu->statistics().presents;
                    if(compatible) {
                        gpu->present_target(screen.window,resident.address,resident.width,resident.height,resident.offset,screen.smooth);
                        display_age_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-resident.captured).count();
                    } else gpu->present_native(screen.window,screen.native,screen.width,screen.height,screen.smooth);
                    const bool capture_performance=g_performance_check&&!performance_capture&&
                        (screen.scene&255)==2&&compatible&&screen.visible&&performance_race_jobs>=60;
                    if((g_presentation_check||capture_performance)&&gpu->statistics().presents>before_present&&
                        (capture_performance||gpu->statistics().presents%60==1)) {
                        std::vector<uint32_t> rgba;uint32_t w=0,h=0;
                        if(gpu->capture_output(rgba,w,h)){
                            std::vector<uint16_t> pixels(rgba.size());
                            for(size_t i=0;i<rgba.size();++i){const auto p=rgba[i];
                                pixels[i]=uint16_t(((p&255)>>3)<<11|(((p>>8)&255)>>3)<<6|(((p>>16)&255)>>3)<<1|1);
                            }
                            const auto name=capture_performance ? std::string("framebuffer_performance_hd.bmp") :
                                "framebuffer_dxgi_"+std::to_string(gpu->statistics().presents)+".bmp";
                            save_fb_to_bmp(name.c_str(),reinterpret_cast<const uint8_t*>(pixels.data()),0,w,h,pixels.size()*sizeof(uint16_t));
                            if(capture_performance)performance_capture=true;
                        }
                    }
                } catch(const std::exception& error) {
                    std::cerr<<"[DXGI] Presentation failed: "<<error.what()<<"; GPU replay with CPU readback/GDI fallback.\n";
                    direct_failed=true;resident.valid=false;gpu->release_presentation();g_gpu_window_owned=false;
                    if(g_hwnd)InvalidateRect(g_hwnd,nullptr,FALSE);
                }
            }
            if(gpu && gpu->available() && !gpu_failed) {
                // Poll timestamps only on their device's worker. CPU replay
                // duration measures submission, not completed GPU execution.
                const auto timing=gpu->statistics();
                if(timing.gpu_samples>reported_gpu_samples) {
                    live_gpu_us=(timing.gpu_elapsed_us-reported_gpu_us)/(timing.gpu_samples-reported_gpu_samples);
                    reported_gpu_us=timing.gpu_elapsed_us;reported_gpu_samples=timing.gpu_samples;
                }
            }
        }
        if(gpu){final_stats=gpu->statistics();gpu.reset();}
        g_gpu_window_owned=false;worker_done=true;
    }
public:
    uint64_t gpu_raster_us() const { return live_gpu_us.load(); }
    uint64_t snapshot_age_ms() const { return display_age_ms.load(); }
    void submit(const F3DEX2Interpreter& interpreter,const OSTask* task,uint32_t height) {
        uint64_t captured_generation=0;
        std::unique_ptr<Job> job;
        {
            std::lock_guard lock(mutex);
            if(stopping)return;
            if(height!=requested_height){requested_height=height;++generation;pending.reset();latest=Frame{};}
            if(!height)return;
            captured_generation=generation;
            job=std::move(spare);
            if(!job&&pending){job=std::move(pending);replaced.fetch_add(1);}
        }
        if(!job)job=std::make_unique<Job>();
        job->captured=std::chrono::steady_clock::now();
        job->ram.assign(interpreter.rdram,interpreter.rdram+8*1024*1024);
        snapshot_us.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-job->captured).count());
        snapshot_count.fetch_add(1);
        job->interpreter=interpreter;
        job->dl=task->t.data_ptr;job->data=task->t.ucode_data;job->data_size=task->t.ucode_data_size;
        job->height=height;job->scene=graphics_scene(job->ram.data());job->offset=0;
        job->scanout_width=job->scanout_height=0;
        auto vi=ultramodern::renderer::get_vi_regs();
        if(vi){
            const auto frame=tgod::vi_frame_layout(*vi);
            if(frame.visible){job->scanout_width=frame.width;job->scanout_height=frame.height;}
            const uint32_t origin=vi->VI_ORIGIN_REG&0x1FFFFFFF;
            if(origin>=interpreter.color_image_addr&&origin-interpreter.color_image_addr<interpreter.color_image_width*8)
                job->offset=origin-interpreter.color_image_addr;
        }
        std::lock_guard lock(mutex);
        if(stopping||height!=requested_height||captured_generation!=generation)return;
        job->generation=captured_generation;
        if(pending)replaced.fetch_add(1);
        pending.swap(job);start_locked();
    }
    bool queue_screen(HWND window,const uint8_t* ram,uint32_t origin,uint32_t w,uint32_t h,bool visible) {
        if(!g_render_height.load()&&!g_gpu_window_owned.load())return false;
        auto next=std::make_unique<Screen>();next->window=window;
        next->scene=graphics_scene(ram);next->requested_height=g_render_height.load();next->smooth=g_filter_smooth.load();
        if(visible&&origin&&w&&h&&uint64_t(origin)+uint64_t(w)*h*2<=8*1024*1024){
            next->width=w;next->height=h;next->native.resize(size_t(w)*h);
            std::memcpy(next->native.data(),ram+origin,next->native.size()*2);
            screen_bytes.fetch_add(next->native.size()*2);
            next->visible=true;
        } else next->native.assign(1,0);
        {std::lock_guard lock(mutex);if(stopping)return false;screen_pending.swap(next);start_locked();}
        return g_gpu_window_owned.load();
    }
    bool copy_frame(uint32_t scene,uint32_t height,std::vector<uint16_t>& pixels,uint32_t& w,uint32_t& h) {
        std::lock_guard lock(mutex);
        if(!height||latest.pixels.empty()||latest.scene!=scene||latest.requested_height!=height||latest.generation!=generation)return false;
        pixels=latest.pixels;w=latest.width;h=latest.height;
        display_age_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-latest.captured).count();
        return true;
    }
    void shutdown() {
        {std::lock_guard lock(mutex);stopping=true;pending.reset();screen_pending.reset();spare.reset();}
        wake.notify_one();
        if(worker.joinable()){
            // DXGI can require the HWND message pump during teardown. Keep it
            // alive while the GPU owner releases the swapchain, then join.
            while(!worker_done.load()){
                MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){
                    if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessage(&msg);}
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            worker.join();
        }
    }
    bool presented()const{return final_stats.presents>0;}
    void diagnostics() const {
        std::cout<<"[HD worker] Completed: "<<completed.load()<<", pending snapshots replaced: "<<replaced.load()
            <<", last replay: "<<last_ms.load()<<" ms; native timing path retained.\n";
        std::cout<<"[HD GPU] Hardware frames: "<<gpu_frames.load()<<".\n";
        std::cout<<"[HD copies] RDRAM snapshots: "<<snapshot_count.load()<<", 8388608 bytes each, average copy: "
            <<snapshot_us.load()/double(std::max(uint64_t(1),snapshot_count.load()))/1000.0
            <<" ms; native scanout bytes copied total: "<<screen_bytes.load()<<".\n";
        std::cout<<"[HD latency] Average replay: "<<(replays.load() ? replay_us.load()/replays.load()/1000.0:0.0)
            <<" ms; last submitted snapshot age: "<<display_age_ms.load()<<" ms.\n";
        std::cout<<"[GPU resources] Surface allocations: "<<final_stats.surface_allocations<<", input allocations: "<<final_stats.input_allocations
            <<", raster dispatches: "<<final_stats.dispatches<<", readback bytes: "<<final_stats.readback_bytes<<".\n";
        std::cout<<"[DXGI] Presents: "<<final_stats.presents<<", occluded: "<<final_stats.occluded<<", busy skipped: "<<final_stats.present_skipped
            <<", resizes: "<<final_stats.resize_count<<", resident HD frames: "<<resident_frames.load()<<", final CPU readbacks: "<<final_stats.final_readbacks<<".\n";
        std::cout<<"[GPU timeline] Samples: "<<final_stats.gpu_samples<<", average replay span: "
            <<(final_stats.gpu_samples ? final_stats.gpu_elapsed_us/final_stats.gpu_samples/1000.0:0.0)
            <<" ms, maximum: "<<final_stats.gpu_max_us/1000.0<<" ms; unavailable slots skipped: "<<final_stats.timing_skipped
            <<", spans over 100 ms: "<<final_stats.gpu_long_spans<<", first long frame: "<<final_stats.gpu_first_long_frame
            <<". Resident replay CPU time does not wait for GPU completion.\n";
    }
};
static AsyncHDRenderer g_hd_renderer;

// Native implementation of __osAiDeviceBusy (func_8004A580)
// Return 0 so game audio engine never triggers assertion failure (func_80009480)
extern "C" void func_8004A580(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}

static void save_fb_to_bmp(const char* filename, const uint8_t* rdram_ptr, uint32_t fb_origin, uint32_t w, uint32_t h, size_t buffer_bytes) {
    if (!w || !h || w>4096 || h>4096 || uint64_t(fb_origin) + uint64_t(w)*h*2 > buffer_bytes) return;
    std::ofstream f(filename, std::ios::binary);
    if (!f.is_open()) return;

    uint32_t row_size = ((w * 3 + 3) / 4) * 4;
    uint32_t image_size = row_size * h;
    uint32_t file_size = 54 + image_size;
    uint8_t hdr[54] = {
        'B', 'M',
        (uint8_t)(file_size), (uint8_t)(file_size >> 8), (uint8_t)(file_size >> 16), (uint8_t)(file_size >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        (uint8_t)(w), (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24),
        (uint8_t)(h), (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24),
        1, 0, 24, 0,
        0, 0, 0, 0,
        (uint8_t)(image_size), (uint8_t)(image_size >> 8), (uint8_t)(image_size >> 16), (uint8_t)(image_size >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    f.write(reinterpret_cast<const char*>(hdr), 54);
    std::vector<uint8_t> row_buf(row_size, 0);

    const uint16_t* fb = reinterpret_cast<const uint16_t*>(rdram_ptr + fb_origin);
    for (int y = (int)h - 1; y >= 0; y--) {
        int src_y = (y >= (int)h - 1) ? std::max(0, (int)h - 2) : y;
        for (uint32_t x = 0; x < w; x++) {
            uint16_t px = fb[src_y * w + x];
            uint8_t r = ((px >> 11) & 0x1F) * 255 / 31;
            uint8_t g = ((px >> 6) & 0x1F) * 255 / 31;
            uint8_t b = ((px >> 1) & 0x1F) * 255 / 31;
            row_buf[x * 3 + 0] = b;
            row_buf[x * 3 + 1] = g;
            row_buf[x * 3 + 2] = r;
        }
        f.write(reinterpret_cast<const char*>(row_buf.data()), row_size);
    }
    std::cout << "  [Screenshot] Saved " << filename << " from origin=0x" << std::hex << fb_origin
              << std::dec << " (" << w << "x" << h << ")\n";
}

// Snapshots are copied on the renderer thread after presentation, rather than
// reading a live target from the main thread while a display list clears it.
class PresentedFrame {
    std::mutex mutex;
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0;
    float display_aspect = 4.0f/3.0f;
public:
    void update(F3DEX2Interpreter& interpreter,const uint8_t* ram, uint32_t origin, uint32_t w, uint32_t h) {
        if (!origin || origin + uint64_t(w)*h*2 > 8*1024*1024) return;
        std::lock_guard lock(mutex);
        display_aspect=(w==640 && h>=116 && h<=124) ? 8.0f/3.0f : 4.0f/3.0f;
        std::vector<uint16_t> rendered;
        if (g_hd_renderer.copy_frame(graphics_scene(ram),g_render_height.load(),rendered,width,height) ||
            interpreter.render_surfaces.copy_scanout(origin,w,h,rendered,width,height)) {
            pixels.resize(rendered.size()*2);
            std::memcpy(pixels.data(),rendered.data(),pixels.size());
        } else {
            width = w; height = h;
            pixels.assign(ram + origin, ram + origin + size_t(w)*h*2);
        }
    }
    void rgba(std::vector<uint32_t>& output,uint32_t& w,uint32_t& h,float* aspect=nullptr) {
        std::lock_guard lock(mutex);
        w=width;h=height;if(aspect)*aspect=display_aspect;output.resize(size_t(w)*h);
        const auto* source=reinterpret_cast<const uint16_t*>(pixels.data());
        for (size_t i=0;i<output.size();++i) {
            const uint16_t px=source[i];
            output[i]=(((px>>11)&31)*255/31<<16)|(((px>>6)&31)*255/31<<8)|(((px>>1)&31)*255/31);
        }
    }
    void save(const char* path) {
        std::lock_guard lock(mutex);
        if (!pixels.empty()) save_fb_to_bmp(path, pixels.data(), 0, width, height, pixels.size());
    }
    void blank() {
        std::lock_guard lock(mutex);
        std::fill(pixels.begin(), pixels.end(), 0);
    }
};
static PresentedFrame g_presented_frame;

static void process_graphics_task(F3DEX2Interpreter& interpreter,const OSTask* task) {
    if (!task || !task->t.data_ptr) return;
    uint32_t native_width=0,native_height=0;
    std::memcpy(&native_width,interpreter.rdram+0x117D14,4);
    std::memcpy(&native_height,interpreter.rdram+0x11D55C,4);
    interpreter.set_framebuffer_layout(native_width,native_height);
    interpreter.raster_backend=nullptr;
    interpreter.render_surfaces.set_height(0);
    g_hd_renderer.submit(interpreter,task,g_render_height.load());
    auto render_native=[&] {
        const auto start=std::chrono::steady_clock::now();
        // This device belongs only to the runtime graphics thread. Native
        // rendering uses private input RAM, so failure cannot leave partial
        // GPU writes in live game/audio memory. Only completed targets commit.
        thread_local std::unique_ptr<GPURaster> gpu;
        thread_local std::vector<uint8_t> private_ram;
        thread_local bool gpu_disabled=false;
        if(g_native_gpu&&!gpu_disabled&&!gpu) {
            gpu=std::make_unique<GPURaster>();
            if(!gpu->available()){gpu.reset();gpu_disabled=true;}
        }
        bool rendered=false;
        if(g_native_gpu&&!gpu_disabled&&gpu) {
            auto original=interpreter;
            auto* live_ram=interpreter.rdram;
            private_ram.assign(live_ram,live_ram+8*1024*1024);
            try {
                interpreter.rdram=private_ram.data();interpreter.raster_backend=gpu.get();
                gpu->begin_frame(false);
                interpreter.process_display_list(task->t.data_ptr,task->t.ucode_data,task->t.ucode_data_size);
                gpu->commit_native_results(interpreter,live_ram);
                interpreter.rdram=live_ram;interpreter.raster_backend=nullptr;
                rendered=true;g_native_gpu_tasks.fetch_add(1);
            } catch(const std::exception& error) {
                interpreter=std::move(original);gpu_disabled=true;gpu.reset();
                g_native_gpu_fallbacks.fetch_add(1);
                std::cerr<<"[Native GPU] Task failed: "<<error.what()<<"; retrying untouched native input on CPU.\n";
            }
        }
        if(!rendered)interpreter.process_display_list(task->t.data_ptr,task->t.ucode_data,task->t.ucode_data_size);
        const uint64_t us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        g_native_render_us.fetch_add(us);g_native_render_count.fetch_add(1);
        g_native_render_max_us.store(std::max(g_native_render_max_us.load(),us));
    };
    static uint32_t race_frames=0;
    uint32_t mode=0;std::memcpy(&mode,interpreter.rdram+0x117DA4,4);
    if(g_performance_check&&mode==2) {
        static uint32_t captured_race_tasks=0;
        const auto index=++captured_race_tasks;
        if(index==16||index==60||index==120) {
            // Private diagnostic input, tied to this executable's ABI. Never
            // serialize pointers, container ownership or include it in releases.
            const auto* tail=reinterpret_cast<const char*>(&interpreter.layout_width);
            const uint32_t tail_bytes=uint32_t(sizeof(interpreter)-(tail-reinterpret_cast<const char*>(&interpreter)));
            const uint32_t header[]={0x43504754,1,tail_bytes,uint32_t(task->t.data_ptr),uint32_t(task->t.ucode_data),uint32_t(task->t.ucode_data_size)};
            std::ofstream capture("cpu_task_"+std::to_string(index)+".bin",std::ios::binary);
            capture.write(reinterpret_cast<const char*>(header),sizeof(header));
            capture.write(reinterpret_cast<const char*>(interpreter.segments),sizeof(interpreter.segments));
            capture.write(reinterpret_cast<const char*>(interpreter.tmem),sizeof(interpreter.tmem));
            capture.write(tail,tail_bytes);
            capture.write(reinterpret_cast<const char*>(interpreter.rdram),8*1024*1024);
        }
    }
    if (g_resolution_check && !g_resolution_check_done.load() && mode==2 && ++race_frames==100) {
        // Replay one captured display list with its initial RDRAM and RSP/RDP state
        // at each height. This compares identical geometry rather than two moving races.
        const std::vector<uint8_t> initial_ram(interpreter.rdram,interpreter.rdram+8*1024*1024);
        const F3DEX2Interpreter initial_state=interpreter;
        GPURaster gpu;
        render_native();
        save_fb_to_bmp("framebuffer_compare_native.bmp",interpreter.rdram,interpreter.color_image_addr,
            interpreter.color_image_width,interpreter.framebuffer_height());
        for (const uint32_t height : {720u,1080u}) {
            std::vector<uint8_t> ram=initial_ram;
            F3DEX2Interpreter replay=initial_state;
            replay.diagnostics_enabled=false;
            replay.rdram=ram.data();replay.render_surfaces.clear();replay.render_surfaces.set_height(height);
            const auto start=std::chrono::steady_clock::now();
            replay.process_display_list(task->t.data_ptr,task->t.ucode_data,task->t.ucode_data_size);
            const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
            std::vector<uint16_t> pixels;uint32_t w=0,h=0;
            if (replay.render_surfaces.copy_scanout(replay.color_image_addr,replay.color_image_width,
                replay.framebuffer_height(),pixels,w,h)) {
                const std::string name="framebuffer_compare_"+std::to_string(height)+"p.bmp";
                save_fb_to_bmp(name.c_str(),reinterpret_cast<const uint8_t*>(pixels.data()),0,w,h,pixels.size()*sizeof(uint16_t));
                std::cout << "[Resolution comparison] Same DL rendered at native and " << w << 'x' << h
                          << "; " << height << "p replay took " << ms << " ms.\n";
                if (height==1080) g_resolution_check_done.store(true);
                if (gpu.available()) {
                    auto gpu_ram=initial_ram;
                    auto gpu_replay=initial_state;gpu_replay.diagnostics_enabled=false;
                    gpu_replay.rdram=gpu_ram.data();gpu_replay.render_surfaces.clear();
                    gpu_replay.render_surfaces.set_height(height);gpu.begin_frame();gpu_replay.raster_backend=&gpu;
                    const auto gpu_start=std::chrono::steady_clock::now();
                    gpu_replay.process_display_list(task->t.data_ptr,task->t.ucode_data,task->t.ucode_data_size);
                    const auto gpu_ms=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-gpu_start).count()/1000.0;
                    std::vector<uint16_t> gpu_pixels;uint32_t gw=0,gh=0;
                    if (gpu_replay.render_surfaces.copy_scanout(gpu_replay.color_image_addr,gpu_replay.color_image_width,
                        gpu_replay.framebuffer_height(),gpu_pixels,gw,gh)) {
                        size_t different=0;uint32_t max_error=0;
                        if (gw!=w || gh!=h || gpu_pixels.size()!=pixels.size()) throw std::runtime_error("GPU comparison dimensions differ");
                        for (size_t i=0;i<pixels.size();++i) {
                            different+=pixels[i]!=gpu_pixels[i];
                            for (int shift : {1,6,11}) max_error=std::max(max_error,uint32_t(std::abs(int((pixels[i]>>shift)&31)-int((gpu_pixels[i]>>shift)&31))));
                        }
                        const auto gpu_name="framebuffer_compare_gpu_"+std::to_string(height)+"p.bmp";
                        save_fb_to_bmp(gpu_name.c_str(),reinterpret_cast<const uint8_t*>(gpu_pixels.data()),0,gw,gh,gpu_pixels.size()*sizeof(uint16_t));
                        std::cout << "[GPU comparison] " << height << "p: " << gpu_ms << " ms; different pixels "
                            << different << '/' << pixels.size() << ", maximum 5-bit channel error " << max_error << ".\n";
                    }
                }
            }
        }
    } else render_native();
}

// Minimal Null Renderer Context for headless execution & verification
class NullRendererContext : public ultramodern::renderer::RendererContext {
public:
    uint8_t* rdram = nullptr;
    std::atomic<uint32_t> dl_count{0};
    std::atomic<uint32_t> screen_update_count{0};
    F3DEX2Interpreter interpreter;

    NullRendererContext(uint8_t* rdram_in = nullptr) : rdram(rdram_in), interpreter(rdram_in) {
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;
    }
    bool valid() override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return true; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        uint32_t count = dl_count++;
        if (task && task->t.data_ptr) {
            process_graphics_task(interpreter,task);
        }
        if (count < 3 && task) {
            std::cout << "[GFX DL #" << count << "] Type: " << task->t.type
                      << ", Flags: 0x" << std::hex << task->t.flags
                      << ", ucode: 0x" << (uintptr_t)task->t.ucode
                      << ", ucode_data: 0x" << (uintptr_t)task->t.ucode_data
                      << ", data_ptr: 0x" << (uintptr_t)task->t.data_ptr
                      << ", data_size: 0x" << task->t.data_size << std::dec << "\n";
        }
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {
        uint32_t count = screen_update_count++;
        auto vi = ultramodern::renderer::get_vi_regs();
        if (vi) {
            const auto frame = tgod::vi_frame_layout(*vi);
            if (frame.visible) g_presented_frame.update(interpreter,rdram, frame.origin, frame.width, frame.height);
            else g_presented_frame.blank();
        }
        if (count < 3) {
            if (vi) {
                std::cout << "[Screen Update #" << count << "] FB Origin: 0x" << std::hex << vi->VI_ORIGIN_REG
                          << ", Width: " << std::dec << vi->VI_WIDTH_REG
                          << ", Status: 0x" << std::hex << vi->VI_STATUS_REG << std::dec << "\n";
            }
        }
    }
    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override {
        const auto height=interpreter.render_surfaces.height();
        return height ? float(height)/interpreter.framebuffer_height() : 1.0f;
    }
};

static bool g_fullscreen = false;
static WINDOWPLACEMENT g_prev_wp = { sizeof(WINDOWPLACEMENT) };

static void render_frame_to_dc(HDC hdc, HWND hwnd, const uint32_t* rgba, const BITMAPINFO* bmi, uint32_t width, uint32_t height, float aspect=4.0f/3.0f) {
    if (!hdc || !hwnd || !rgba || !bmi) return;
    RECT client_rect;
    GetClientRect(hwnd, &client_rect);
    int client_w = client_rect.right - client_rect.left;
    int client_h = client_rect.bottom - client_rect.top;
    if (client_w <= 0 || client_h <= 0) return;

    // Preserve the original Half mode letterbox within the normal 4:3 canvas.
    int target_w = client_w;
    int target_h = std::max(1,(client_w*3)/4);
    if (target_h > client_h) {
        target_h = client_h;
        target_w = std::max(1,(client_h*4)/3);
    }
    // First fit the same 4:3 canvas as Full, then reduce its visible height.
    // Fitting 8:3 directly would incorrectly widen Half on a 16:9 monitor.
    if(aspect>2.0f)target_h=std::max(1,target_h/2);
    int off_x = (client_w - target_w) / 2;
    int off_y = (client_h - target_h) / 2;

    if (off_x > 0) {
        PatBlt(hdc, 0, 0, off_x, client_h, BLACKNESS);
        PatBlt(hdc, off_x + target_w, 0, client_w - (off_x + target_w), client_h, BLACKNESS);
    }
    if (off_y > 0) {
        PatBlt(hdc, 0, 0, client_w, off_y, BLACKNESS);
        PatBlt(hdc, 0, off_y + target_h, client_w, client_h - (off_y + target_h), BLACKNESS);
    }

    if (g_filter_smooth.load()) {
        SetStretchBltMode(hdc, HALFTONE);
        POINT pt;
        SetBrushOrgEx(hdc, 0, 0, &pt);
    } else {
        SetStretchBltMode(hdc, COLORONCOLOR);
    }

    int lines = StretchDIBits(
        hdc,
        off_x, off_y, target_w, target_h,
        0, 0, width, height,
        rgba,
        bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );
    static int dbg_count = 0;
    if (dbg_count++ < 5) {
        std::cout << "[StretchDIBits #" << dbg_count << "] lines=" << lines
                  << " target=" << target_w << "x" << target_h
                  << " at (" << off_x << "," << off_y << ") rgba[100]=" << std::hex << rgba[100]
                  << " err=" << std::dec << GetLastError() << "\n";
    }
}

static void toggle_fullscreen(HWND hwnd) {
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    if (!g_fullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(hwnd, &g_prev_wp) &&
            GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_fullscreen = true;
        }
    } else {
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &g_prev_wp);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = false;
    }
}

static void resize_window_scale(HWND hwnd, int scale) {
    if (g_fullscreen) toggle_fullscreen(hwnd);
    RECT rc = { 0, 0, 320 * scale, 240 * scale };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Interactive Win32 Renderer Context using DIB presentation
class Win32RendererContext : public ultramodern::renderer::RendererContext {
public:
    uint8_t* rdram = nullptr;
    HWND hwnd = nullptr;
    std::atomic<uint32_t> dl_count{0};
    std::atomic<uint32_t> screen_update_count{0};
    std::vector<uint32_t> rgba_pixels;
    std::mutex presentation_mutex;
    float display_aspect=4.0f/3.0f;
    BITMAPINFO bmi{};
    F3DEX2Interpreter interpreter;

    Win32RendererContext(uint8_t* rdram_in, HWND hwnd_in) : rdram(rdram_in), hwnd(hwnd_in), interpreter(rdram_in) {
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;
        rgba_pixels.resize(320 * 240, 0);

        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 320;
        bmi.bmiHeader.biHeight = -240; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
    }

    bool valid() override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return true; }
    void enable_instant_present() override {}

    void send_dl(const OSTask* task) override {
        uint32_t count = dl_count++;
        if (task && task->t.data_ptr) {
            process_graphics_task(interpreter,task);
        }
        if (count < 3 && task) {
            std::cout << "[GFX DL #" << count << "] Type: " << task->t.type
                      << ", Flags: 0x" << std::hex << task->t.flags
                      << ", ucode: 0x" << (uintptr_t)task->t.ucode
                      << ", ucode_data: 0x" << (uintptr_t)task->t.ucode_data
                      << ", data_ptr: 0x" << (uintptr_t)task->t.data_ptr
                      << ", data_size: 0x" << task->t.data_size << std::dec << "\n";
        }
    }

    void send_dummy_workload(uint32_t) override {}

    void update_screen() override {
        uint32_t count = screen_update_count++;
        if (!hwnd || !rdram) return;

        auto vi = ultramodern::renderer::get_vi_regs();
        uint32_t origin = vi ? (vi->VI_ORIGIN_REG & 0x1FFFFFFF) : 0;
        if (count < 5 || count % 60 == 0) {
            std::cout << "[update_screen #" << count << "] VI origin=0x" << std::hex << origin
                      << " width=" << std::dec << (vi ? vi->VI_WIDTH_REG : 0) << "\n";
        }
        if (!vi) return;
        const auto frame = tgod::vi_frame_layout(*vi);
        if(g_hd_renderer.queue_screen(hwnd,rdram,origin,frame.width,frame.height,frame.visible)){
            if(frame.visible)g_presented_frame.update(interpreter,rdram,origin,frame.width,frame.height);
            else g_presented_frame.blank();
            return;
        }
        std::lock_guard output_lock(g_window_output_mutex);
        if(g_gpu_window_owned.load())return;
        if (!frame.visible) {
            std::lock_guard lock(presentation_mutex);
            g_presented_frame.blank();
            std::fill(rgba_pixels.begin(), rgba_pixels.end(), 0);
            HDC hdc = GetDC(hwnd);
            if (hdc) {
                render_frame_to_dc(hdc, hwnd, rgba_pixels.data(), &bmi,
                    bmi.bmiHeader.biWidth, -bmi.bmiHeader.biHeight);
                ReleaseDC(hwnd, hdc);
            }
            return;
        }
        const uint32_t width = frame.width, height = frame.height;

        if (origin + width * height * 2 <= 8 * 1024 * 1024) {
            std::lock_guard lock(presentation_mutex);
            g_presented_frame.update(interpreter,rdram, origin, width, height);
            uint32_t rendered_width=0,rendered_height=0;
            g_presented_frame.rgba(rgba_pixels,rendered_width,rendered_height,&display_aspect);
            bmi.bmiHeader.biWidth = rendered_width;
            bmi.bmiHeader.biHeight = -int32_t(rendered_height);

            HDC hdc = GetDC(hwnd);
            if (hdc) {
                render_frame_to_dc(hdc, hwnd, rgba_pixels.data(), &bmi, rendered_width, rendered_height,display_aspect);
                ReleaseDC(hwnd, hdc);
            }
        }
    }

    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override {
        const auto height=interpreter.render_surfaces.height();
        return height ? float(height)/interpreter.framebuffer_height() : 1.0f;
    }
};

static NullRendererContext* g_renderer_null = nullptr;
static Win32RendererContext* g_renderer_win32 = nullptr;

static inline uint32_t read_rdram_u32(const uint8_t* rdram, uint32_t vram) {
    uint32_t phys = vram & 0x1FFFFFFF;
    if (phys + 4 > 8 * 1024 * 1024) return 0xFFFFFFFF;
    return *reinterpret_cast<const uint32_t*>(rdram + phys);
}

static inline uint16_t read_rdram_u16(const uint8_t* rdram, uint32_t vram) {
    uint32_t phys = (vram & 0x1FFFFFFF) ^ 2;
    if (phys + 2 > 8 * 1024 * 1024) return 0xFFFF;
    return *reinterpret_cast<const uint16_t*>(rdram + phys);
}

static inline uint8_t read_rdram_u8(const uint8_t* rdram, uint32_t vram) {
    uint32_t phys = (vram & 0x1FFFFFFF) ^ 3;
    if (phys >= 8 * 1024 * 1024) return 0xFF;
    return *(rdram + phys);
}

static inline void write_rdram_u32(uint8_t* rdram, uint32_t vram, uint32_t val) {
    uint32_t phys = vram & 0x1FFFFFFF;
    if (phys + 4 <= 8 * 1024 * 1024) {
        *reinterpret_cast<uint32_t*>(rdram + phys) = val;
    }
}

static inline void write_rdram_u16(uint8_t* rdram, uint32_t vram, uint16_t val) {
    uint32_t phys = (vram & 0x1FFFFFFF) ^ 2;
    if (phys + 2 <= 8 * 1024 * 1024) {
        *reinterpret_cast<uint16_t*>(rdram + phys) = val;
    }
}

static inline void write_rdram_u8(uint8_t* rdram, uint32_t vram, uint8_t val) {
    uint32_t phys = (vram & 0x1FFFFFFF) ^ 3;
    if (phys < 8 * 1024 * 1024) {
        *(rdram + phys) = val;
    }
}

static std::atomic<uint32_t> g_si_poll_count{0};

// Hook for Top Gear Overdrive SI polling function (0x800470A0)
extern "C" void tgod_poll_si(uint8_t* rdram, recomp_context* ctx) {
    g_si_poll_count.fetch_add(1, std::memory_order_relaxed);
    update_key_input();
    tgod::input::Snapshot snapshot;
    {
        std::lock_guard input_lock(g_input_mutex);
        snapshot=g_controllers;
    }
    if(g_is_headless) snapshot[0]={true,g_buttons.load(),g_stick_x.load(),g_stick_y.load()};
    tgod::input::write_si(rdram,snapshot);

    ultramodern::send_si_message();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1; // Prevent window background erasing flicker
        case WM_CLOSE:
            tgod::controls::close_mapper();
            g_running = false;
            PostQuitMessage(0); // HWND stays alive until the GPU worker shuts down.
            return 0;
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_running = false;
                PostQuitMessage(0);
                return 0;
            } else if (wParam == VK_F1) {
                resize_window_scale(hwnd, 1); // 320x240
            } else if (wParam == VK_F2) {
                resize_window_scale(hwnd, 2); // 640x480
            } else if (wParam == VK_F3) {
                resize_window_scale(hwnd, 3); // 960x720
            } else if (wParam == VK_F4) {
                resize_window_scale(hwnd, 4); // 1280x960
            } else if (wParam == VK_F5) {
                g_filter_smooth.store(!g_filter_smooth.load());
            } else if (wParam == VK_F6 && !(lParam&(1l<<30))) {
                if(g_legacy_version) {
                    std::cout<<"[Graphics] LegacyVersion=true: native rendering. Set false and restart to enable HD.\n";
                    return 0;
                }
                const auto current=g_render_height.load();
                const uint32_t next=current==0 ? 720 : current==720 ? 1080 : current==1080 ? 2160 : 0;
                g_render_height.store(next);
                const auto value=std::to_wstring(next);
                if (!WritePrivateProfileStringW(L"Graphics",L"RenderHeight",value.c_str(),g_graphics_config.c_str()))
                    std::cerr << "[Graphics] Could not save tgod.ini; resolution applies to this session.\n";
            } else if (wParam == VK_F7) {
                if (!(lParam & (1ll<<30))) tgod::controls::show_mapper(hwnd);
                return 0;
            } else if (wParam == VK_F11) {
                if (!(lParam & (1ll<<30))) toggle_fullscreen(hwnd);
                return 0;
            }
            update_key_input();
            break;
        case WM_SYSKEYDOWN:
            if (wParam == VK_F11 || (wParam == VK_RETURN && (lParam & (1ll << 29)))) {
                if (!(lParam & (1ll<<30))) toggle_fullscreen(hwnd);
                return 0;
            }
            break;
        case WM_KEYUP:
            update_key_input();
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            std::lock_guard output_lock(g_window_output_mutex);
            if (g_renderer_win32 && !g_gpu_window_owned.load()) {
                std::lock_guard lock(g_renderer_win32->presentation_mutex);
                render_frame_to_dc(hdc, hwnd, g_renderer_win32->rgba_pixels.data(), &g_renderer_win32->bmi,
                    g_renderer_win32->bmi.bmiHeader.biWidth, -g_renderer_win32->bmi.bmiHeader.biHeight,g_renderer_win32->display_aspect);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout.setf(std::ios::unitbuf);
    std::cout << "====================================================\n";
    std::cout << " Top Gear Overdrive (N64) - Native PC Port Runner\n";
    std::cout << " Experimental Windows port - user ROM required\n";
    std::cout << "====================================================\n\n";

    bool headless = false;
    bool graphics_check = false;
    bool intro_check = false;
    bool verify_only = false;
    bool explicit_rom = false;
    int headless_seconds = 4;
    int render_height_override = -1;
    int original_resolution_override = -2;
    int native_gpu_override=-1;
    std::filesystem::path rom_path = "top_gear_rom/normalized/Top Gear Overdrive (USA).z64";

    // The Unicode command line preserves non-ASCII ROM paths and drag-and-drop.
    int wide_argc = 0;
    wchar_t** wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    std::vector<std::wstring> arguments;
    if (!wide_argv) return 2;
    for (int i = 1; i < wide_argc; ++i) arguments.emplace_back(wide_argv[i]);
    LocalFree(wide_argv);
    for (size_t i = 0; i < arguments.size(); i++) {
        const auto& arg = arguments[i];
        if (arg == L"--headless") {
            headless = true;
            g_is_headless = true;
        } else if (arg == L"--graphics-check") {
            graphics_check = true;
        } else if (arg == L"--intro-check") {
            intro_check = true;
        } else if (arg == L"--presentation-check") {
            g_presentation_check = true;
        } else if (arg == L"--performance-check") {
            g_performance_check = true;
        } else if (arg == L"--resolution-check") {
            g_resolution_check = true;
        } else if(arg==L"--native-rasterizer"&&i+1<arguments.size()) {
            const auto& value=arguments[++i];
            if(value==L"gpu")native_gpu_override=1;
            else if(value==L"cpu")native_gpu_override=0;
            else {std::cerr<<"--native-rasterizer expects cpu or gpu.\n";return 2;}
        } else if(arg==L"--hd-priority"&&i+1<arguments.size()) {
            const auto& value=arguments[++i];
            if(value==L"normal")g_hd_priority=THREAD_PRIORITY_NORMAL;
            else if(value==L"below-normal")g_hd_priority=THREAD_PRIORITY_BELOW_NORMAL;
            else {std::cerr<<"--hd-priority expects normal or below-normal.\n";return 2;}
        } else if (arg == L"--render-height" && i+1<arguments.size()) {
            const auto& value=arguments[++i];
            if (value==L"0" || value==L"native") render_height_override=0;
            else if (value==L"720") render_height_override=720;
            else if (value==L"1080") render_height_override=1080;
            else if (value==L"2160" || value==L"4k" || value==L"4K") render_height_override=2160;
            else {std::cerr << "--render-height expects native, 720, 1080 or 2160 (4k).\n";return 2;}
        } else if (arg == L"--original-resolution" && i+1<arguments.size()) {
            original_resolution_override=original_resolution_value(arguments[++i]);
            if(original_resolution_override==-2) {
                std::cerr<<"--original-resolution expects game, off, half or full.\n";return 2;
            }
        } else if (arg == L"--verify-rom") {
            verify_only = true;
        } else if (arg == L"--seconds" && i + 1 < arguments.size()) {
            headless_seconds = std::clamp(_wtoi(arguments[++i].c_str()), 1, 3600);
        } else if (arg == L"--rom" && i + 1 < arguments.size()) {
            rom_path = arguments[++i];
            explicit_rom = true;
        } else if (arg == L"--help") {
            std::cout << "Usage: tgod_native.exe [--rom] <own ROM.z64|v64|n64> [--render-height native|720|1080|2160|4k] [--original-resolution game|off|half|full] [--native-rasterizer cpu|gpu] [--hd-priority normal|below-normal] [--headless --seconds N [--graphics-check | --intro-check | --performance-check] [--resolution-check | --presentation-check]] [--verify-rom]\n";
            return 0;
        } else if (arg.rfind(L"--", 0) != 0) {
            rom_path = arg;
            explicit_rom = true;
        } else {
            std::cerr << "Unknown or incomplete option. Use --help.\n";
            return 2;
        }
    }

    if(g_performance_check && (!headless || graphics_check || intro_check || g_presentation_check || g_resolution_check)) {
        std::cerr << "--performance-check requires --headless and no other check mode.\n";
        return 2;
    }
    if (graphics_check && !headless) {
        std::cerr << "--graphics-check requires --headless.\n";
        return 2;
    }
    if(g_presentation_check&&(!headless||g_resolution_check)){
        std::cerr<<"--presentation-check requires --headless and no --resolution-check.\n";
        return 2;
    }
    if (intro_check && (!headless || graphics_check)) {
        std::cerr << "--intro-check requires --headless and cannot be combined with --graphics-check.\n";
        return 2;
    }
    if (g_resolution_check && (!headless || intro_check || render_height_override>0)) {
        std::cerr << "--resolution-check requires --headless, native rendering and no --intro-check.\n";
        return 2;
    }
    std::error_code path_error;
    if (!std::filesystem::exists(rom_path, path_error) && !explicit_rom && !headless && !verify_only) {
        rom_path = tgod::choose_rom_file();
        if (rom_path.empty()) return 0; // Selection canceled.
    }
    rom_path = std::filesystem::absolute(rom_path, path_error);
    auto selected_rom = tgod::load_verified_rom(rom_path);
    if (!selected_rom) {
        std::cerr << "[ERROR] " << selected_rom.error << "\n";
        if (!headless && !verify_only) MessageBoxA(nullptr, selected_rom.error.c_str(), "Top Gear Overdrive - ROM", MB_OK | MB_ICONERROR);
        return 2;
    }
    auto rom_bytes = std::move(selected_rom.bytes);
    std::cout << "[Host] Verified Top Gear Overdrive USA v0 (" << rom_bytes.size() << " bytes).\n";
    if (verify_only) return 0;
    // Keep saves/diagnostics with the portable program even when launched from
    // a shortcut, a different working directory, or by dropping a ROM onto it.
    const auto program_directory = tgod::executable_directory();
    g_graphics_config=program_directory/L"tgod.ini";
    const bool use_graphics_config=!headless || g_performance_check;
    if(!headless)tgod::controls::initialize(program_directory);
    if(use_graphics_config) {
        wchar_t legacy[32]{};
        if(GetPrivateProfileStringW(L"Graphics",L"LegacyVersion",L"",legacy,32,g_graphics_config.c_str())==0) {
            WritePrivateProfileStringW(L"Graphics",L"LegacyVersion",L"false",g_graphics_config.c_str());
        } else if(_wcsicmp(legacy,L"true")==0 || wcscmp(legacy,L"1")==0)g_legacy_version=true;
        else if(_wcsicmp(legacy,L"false")!=0 && wcscmp(legacy,L"0")!=0) {
            std::cerr<<"[Graphics] Invalid LegacyVersion; using legacy mode. Expected true or false.\n";
            g_legacy_version=true;
        }
    }
    uint32_t selected_height=use_graphics_config ? GetPrivateProfileIntW(L"Graphics",L"RenderHeight",1080,g_graphics_config.c_str()) : 0;
    if (selected_height!=720 && selected_height!=1080 && selected_height!=2160) selected_height=0;
    if (render_height_override>=0) selected_height=uint32_t(render_height_override);
    wchar_t original_mode[32]{};
    GetPrivateProfileStringW(L"Graphics",L"OriginalResolution",L"full",original_mode,32,g_graphics_config.c_str());
    g_original_resolution=use_graphics_config ? original_resolution_value(original_mode) : -1;
    if(g_original_resolution==-2) {
        std::cerr<<"[Graphics] Invalid OriginalResolution; using game.\n";g_original_resolution=-1;
    }
    if(original_resolution_override!=-2)g_original_resolution=original_resolution_override;
    if(use_graphics_config) {
        wchar_t rasterizer[32]{};
        GetPrivateProfileStringW(L"Graphics",L"NativeRasterizer",L"gpu",rasterizer,32,g_graphics_config.c_str());
        if(_wcsicmp(rasterizer,L"gpu")==0)g_native_gpu=true;
        else if(_wcsicmp(rasterizer,L"cpu")!=0)
            std::cerr<<"[Graphics] Invalid NativeRasterizer; using CPU.\n";
        if(GetPrivateProfileStringW(L"Graphics",L"NativeRasterizer",L"",rasterizer,32,g_graphics_config.c_str())==0)
            WritePrivateProfileStringW(L"Graphics",L"NativeRasterizer",L"gpu",g_graphics_config.c_str());
    }
    if(native_gpu_override>=0)g_native_gpu=native_gpu_override!=0;
    if(!headless && GetPrivateProfileStringW(L"Graphics",L"RenderHeight",L"",original_mode,32,g_graphics_config.c_str())==0)
        WritePrivateProfileStringW(L"Graphics",L"RenderHeight",L"1080",g_graphics_config.c_str());
    if(!headless && GetPrivateProfileStringW(L"Graphics",L"OriginalResolution",L"",original_mode,32,g_graphics_config.c_str())==0)
        WritePrivateProfileStringW(L"Graphics",L"OriginalResolution",L"full",g_graphics_config.c_str());
    tgod::GraphicsEnhancements enhancements;
    if(use_graphics_config) {
        auto option=[&](const wchar_t* key,int initial,int low,int high) {
            wchar_t stored[32]{};
            if(GetPrivateProfileStringW(L"Graphics",key,L"",stored,32,g_graphics_config.c_str())==0) {
                const auto value=std::to_wstring(initial);
                WritePrivateProfileStringW(L"Graphics",key,value.c_str(),g_graphics_config.c_str());
            }
            return std::clamp(int(GetPrivateProfileIntW(L"Graphics",key,initial,g_graphics_config.c_str())),low,high);
        };
        enhancements.force_high_detail=option(L"ForceHighDetail",1,0,1)!=0;
        enhancements.draw_distance_percent=option(L"DrawDistancePercent",200,100,200);
        enhancements.extra_track_sections=option(L"ExtraTrackSections",2,0,2);
    }
    if(g_legacy_version) {
        selected_height=0;g_original_resolution=-1;enhancements={};g_native_gpu=false;
    }
    if(g_resolution_check)g_native_gpu=false; // Resolution comparisons retain the CPU reference.
    g_render_height.store(selected_height);
    std::cout<<"[Native renderer] "<<(g_native_gpu ? "GPU with completed native readback and CPU fallback":"CPU reference")<<".\n";
    tgod::configure_graphics_enhancements(enhancements);
    std::cout<<"[Graphics] Profile: "<<(!use_graphics_config ? "diagnostic":g_legacy_version ? "legacy":"enhanced")
        <<"; LegacyVersion takes priority over graphics overrides in legacy mode.\n";
    std::cout<<"[Graphics] High detail: "<<(enhancements.force_high_detail ? "on":"game")
        <<"; draw distance: "<<enhancements.draw_distance_percent<<"%; extra scenery sections: "
        <<enhancements.extra_track_sections<<". Experimental, restart to change.\n";
    std::cout << "[Graphics] Internal resolution: " << (selected_height ? std::to_string(selected_height)+"p (4:3)" : "native")
              << "; F6 cycles native/720p/1080p/4K. Native timing plus hardware HD.\n";
    if (!program_directory.empty()) {
        std::filesystem::current_path(program_directory, path_error);
        if (path_error) {
            std::cerr << "[ERROR] Cannot access the program directory.\n";
            return 2;
        }
    }

    g_audio_backend.start();

    // 2. Setup Win32 Window if in interactive mode
    if (!headless || g_presentation_check || g_performance_check) {
        WNDCLASSEX wc{};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = "TGOD_PORT_WINDOW";
        RegisterClassEx(&wc);

        RECT wr = { 0, 0, 640, 480 };
        AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
        g_hwnd = CreateWindowEx(
            0,
            "TGOD_PORT_WINDOW",
            "Top Gear Overdrive (N64 Native PC Port)",
            WS_OVERLAPPEDWINDOW | (g_presentation_check ? 0:WS_VISIBLE),
            CW_USEDEFAULT, CW_USEDEFAULT,
            wr.right - wr.left, wr.bottom - wr.top,
            nullptr, nullptr, GetModuleHandle(nullptr), nullptr
        );
        tgod::controls::set_game_window(g_hwnd);
        if(!g_presentation_check){ShowWindow(g_hwnd, SW_SHOW);UpdateWindow(g_hwnd);}
        std::cout << "[Host] Created native Win32 window (HWND: 0x" << std::hex << (uintptr_t)g_hwnd << std::dec << ", 640x480 client area)\n";
    } else {
        g_audio_backend.enabled = true;
        std::cout << "[Host] Running in headless test mode (audio enabled for diagnostics).\n";
    }

    // 3. Register renderer callbacks
    ultramodern::renderer::callbacks_t render_cbs{};
    render_cbs.create_render_context = [](uint8_t* rdram_in, ultramodern::renderer::WindowHandle wh, bool) -> std::unique_ptr<ultramodern::renderer::RendererContext> {
        if (wh.window != nullptr) {
            auto ctx = std::make_unique<Win32RendererContext>(rdram_in, wh.window);
            g_renderer_win32 = ctx.get();
            return ctx;
        } else {
            auto ctx = std::make_unique<NullRendererContext>(rdram_in);
            g_renderer_null = ctx.get();
            return ctx;
        }
    };
    ultramodern::renderer::set_callbacks(render_cbs);

    std::cout<<"[Sync] Graphics SP completion follows native DL consumption; HD replay stays asynchronous.\n";

    // 4. Register input callbacks
    ultramodern::input::callbacks_t input_cbs{};
    input_cbs.poll_input = []() {
        update_key_input();
    };
    input_cbs.get_input = [](int controller_num, uint16_t* buttons, float* x, float* y) -> bool {
        update_key_input();
        std::lock_guard input_lock(g_input_mutex);
        if(controller_num<0 || controller_num>=int(tgod::input::ports))return false;
        auto state=g_controllers[controller_num];
        if(g_is_headless && controller_num==0)state={true,g_buttons.load(),g_stick_x.load(),g_stick_y.load()};
        if(!state.connected)return false;
        *buttons=state.buttons;*x=state.x;*y=state.y;
        return true;
    };
    input_cbs.set_rumble = [](int, bool) {};
    input_cbs.get_connected_device_info = [](int controller_num) -> ultramodern::input::connected_device_info_t {
        update_key_input();
        std::lock_guard input_lock(g_input_mutex);
        const bool connected=controller_num>=0 && controller_num<int(tgod::input::ports) &&
            ((g_is_headless && controller_num==0) || g_controllers[controller_num].connected);
        return {connected ? ultramodern::input::Device::Controller:ultramodern::input::Device::None,
            ultramodern::input::Pak::None};
    };
    ultramodern::input::set_callbacks(input_cbs);
    std::cout << "[Host] Input callbacks registered:\n"
              << "       Enter=Start | X/Space=A | Z/Back=B | C=Z-Trig | A/Q=L | D/E=R | Arrows=D-Pad/Stick | Esc=Quit\n"
              << "       F1-F4=Window Scale | F5=Output Filter | F6=Internal Resolution | F7=Controls | F11 / Alt+Enter=Fullscreen\n";

    ultramodern::rsp::callbacks_t rsp_cbs{};
    rsp_cbs.init = []() {};
    rsp_cbs.run_task = [](uint8_t*, const OSTask* task) -> bool {
        const uint32_t type = task ? task->t.type : 4;
        g_rsp_task_types[std::min(type, 4u)].fetch_add(1, std::memory_order_relaxed);
        return true;
    };
    ultramodern::rsp::set_callbacks(rsp_cbs);

    // 5. Register audio callbacks
    ultramodern::audio_callbacks_t audio_cbs{};
    audio_cbs.set_frequency = [](uint32_t freq) {
        g_audio_backend.set_frequency(freq);
    };
    audio_cbs.queue_samples = [](int16_t* samples, size_t sample_count) {
        g_audio_backend.queue_samples(samples, sample_count);
    };
    audio_cbs.get_frames_remaining = []() -> size_t {
        return g_audio_backend.get_frames_remaining();
    };
    audio_cbs.get_ai_frames_remaining = []() -> size_t {
        return g_audio_backend.get_ai_frames_remaining();
    };
    ultramodern::set_audio_callbacks(audio_cbs);
    std::cout << "[Host] Audio callbacks registered (Win32 waveOut backend).\n";

    // 5. Register game entry in librecomp
    recomp::GameEntry game_entry{};
    game_entry.display_name = "Top Gear Overdrive";
    game_entry.internal_name = "TOP GEAR OVERDRIVE";
    game_entry.game_id = u8"tgod_usa";
    game_entry.mod_game_id = "tgod";
    game_entry.save_type = recomp::SaveType::Eep16k;
    game_entry.entrypoint_address = (gpr)(int32_t)0x80000400;
    game_entry.entrypoint = recomp_entrypoint;
    recomp::register_game(game_entry);
    recomp::start_game(u8"tgod_usa", "");
    recomp::register_config_path(".");
    recomp::set_save_type(recomp::SaveType::Eep16k);
    std::cout << "[Host] Registered game '" << game_entry.display_name << "' with librecomp.\n";

    // 6. Allocate 8 MiB RDRAM memory space
    std::cout << "[Host] Allocating 8 MiB RDRAM memory space...\n";
    std::vector<uint8_t> rdram_buffer(8 * 1024 * 1024, 0);
    uint8_t* rdram = rdram_buffer.data();

    // 7. Register overlays from recomp_overlays.inl
    std::cout << "[Host] Registering resident code sections (" << num_sections << " sections)...\n";
    recomp::overlays::register_overlays(
        { section_table, num_sections, num_sections },
        { overlay_sections_by_index, ARRLEN(overlay_sections_by_index) }
    );

    // 8. Pass ROM data to librecomp for PI DMA
    recomp::set_rom_contents(std::move(rom_bytes));

    // 9. Initialize ultramodern runtime services with WindowHandle
    std::cout << "[Host] Initializing ultramodern runtime (threads, queues, timers, window, save)...\n";
    ultramodern::renderer::WindowHandle win_handle{};
    if (g_hwnd) {
        win_handle.window = g_hwnd;
        win_handle.thread_id = GetCurrentThreadId();
    }
    ultramodern::preinit(rdram, win_handle);
    ultramodern::init_saving(rdram);
    std::cout << "[Host] Initialized EEPROM save subsystem (target: saves/tgod_usa.bin).\n";

    // 10. Initialize overlays and load resident functions into func_map
    recomp::overlays::init_overlays();
    load_overlays(0x1000, 0x80000400, 1024 * 1024);
    std::cout << "[Host] Overlays initialized and mapped.\n";

    // 11. Copy resident code & data from ROM into RDRAM via recomp::do_rom_read
    recomp::do_rom_read(rdram, (gpr)(int32_t)0x80000400, 0x10001000, 0x60B40);
    std::cout << "[Host] Transferred resident code & data into RDRAM (0x60B40 bytes).\n";

    // 12. Set up IPL3 variables in low RDRAM
    MEM_W(0, (gpr)(int32_t)0x80000300) = 1;              // osTvType = NTSC
    MEM_W(0, (gpr)(int32_t)0x80000308) = 0xB0000000u;    // osRomBase
    MEM_W(0, (gpr)(int32_t)0x8000030C) = 0;              // osResetType = cold
    MEM_W(0, (gpr)(int32_t)0x80000318) = 8 * 1024 * 1024;// osMemSize = 8 MB

    // 13. Prepare entrypoint CPU context
    recomp_context ctx{};
    ctx.f_odd = &ctx.f0.u32h;
    ctx.mips3_float_mode = false;
    ctx.r29 = (gpr)(int32_t)0x800F80A0; // Initial stack pointer

    std::cout << "[Host] Starting recomp_entrypoint at 0x80000400...\n";
    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        recomp_entrypoint(rdram, &ctx);
        std::cout << "[Host] recomp_entrypoint returned successfully!\n";
    } catch (const std::exception& ex) {
        std::cerr << "[Host Exception] " << ex.what() << "\n";
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start_time
    ).count();
    std::cout << "[Host] Entrypoint execution time: " << elapsed << " ms\n";



    if (!headless && g_hwnd) {
        std::cout << "\n====================================================\n";
        std::cout << " === Top Gear Overdrive Interactive Session ===\n";
        std::cout << "====================================================\n";
        std::cout << "Window is active at 60 FPS. Press ESC or close the window to exit.\n\n";

        MSG msg{};
        auto last_title_update = std::chrono::steady_clock::now();
        uint64_t last_vis = total_vis;
        uint64_t last_native_count=g_native_render_count.load(),last_native_us=g_native_render_us.load();

        while (g_running.load()) {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    g_running = false;
                    break;
                }
                if(!tgod::controls::handle_message(msg)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
            if (!g_running.load()) break;

            update_key_input();

            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_title_update).count();
            if (elapsed_ms >= 300) {
                last_title_update = now;
                uint64_t cur_vis = total_vis;
                double fps = (cur_vis - last_vis) * 1000.0 / (elapsed_ms > 0 ? elapsed_ms : 1);
                last_vis = cur_vis;
                const uint64_t native_count=g_native_render_count.load(),native_us=g_native_render_us.load();
                const uint64_t native_delta=native_count-last_native_count;
                const double gfx_rate=native_delta*1000.0/std::max(int64_t(1),int64_t(elapsed_ms));
                const double native_ms=native_delta ? (native_us-last_native_us)/double(native_delta)/1000.0 : 0.0;
                last_native_count=native_count;last_native_us=native_us;
                const double gpu_ms=g_render_height.load() ? g_hd_renderer.gpu_raster_us()/1000.0 : 0.0;
                char gpu_time[32];
                if(gpu_ms>0)std::snprintf(gpu_time,sizeof(gpu_time),"%.1f ms",gpu_ms);
                else std::snprintf(gpu_time,sizeof(gpu_time),"n/a");

                uint32_t mode = read_rdram_u32(rdram, 0x80117DA4);
                const char* mode_name = (mode == 0) ? "Intro" : (mode == 1) ? "Menus" : (mode == 2) ? "Race (In-Game)" : "Game";
                char title_buf[384];
                std::snprintf(title_buf, sizeof(title_buf),
                    "Top Gear Overdrive (PC) | %s | VI: %.1f Hz | GFX: %.1f/s | Native task: %.1f ms | HD GPU: %s | Audio: %u buf | Filter: %s [F5] | Render: %s [F6]",
                    mode_name, fps, gfx_rate,native_ms,gpu_time,g_audio_backend.buffers_queued.load(),
                    g_filter_smooth.load() ? "Smooth" : "Crisp",
                    g_render_height.load()==2160 ? "4K (2160p)" : g_render_height.load()==1080 ? "1080p" : g_render_height.load()==720 ? "720p" : "Native");
                SetWindowTextA(g_hwnd, title_buf);

                static auto last_console_log = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_console_log).count() >= 1000) {
                    last_console_log = now;
                    uint32_t rsp_busy = read_rdram_u32(rdram, 0x800F7B14);
                    uint32_t th7 = read_rdram_u32(rdram, 0x8006AC2C);
                    uint8_t stage = read_rdram_u8(rdram, 0x80053650);
                    uint32_t itimer = read_rdram_u32(rdram, 0x80053654);
                    std::cout << "[Live] Mode: " << mode_name
                              << " | VI: " << std::fixed << std::setprecision(1) << fps<<" Hz"
                              << " | GFX: "<<gfx_rate<<" tasks/s | Native task: "<<native_ms<<" ms | HD GPU: "<<gpu_time
                              << " | HD snapshot age: "<<(g_render_height.load() ? g_hd_renderer.snapshot_age_ms():0)<<" ms"
                              << " | DLs: " << (g_renderer_win32 ? g_renderer_win32->dl_count.load() : 0)
                              << " | Screens: " << (g_renderer_win32 ? g_renderer_win32->screen_update_count.load() : 0)
                              << " | RSP_Busy: " << rsp_busy
                              << " | Th7: " << th7
                              << " | Stage: " << (int)stage << " (T=" << itimer << ")\n";
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        std::cout << "\n[Host] Window closed. Shutting down.\n";
    } else {
        std::cout << "[Host] Waiting " << (headless_seconds * 1000) << " ms for background game threads ("
                  << (intro_check ? "uninterrupted intro capture" : "automated navigation to Mode 2") << ")...\n";
        auto perf_time=std::chrono::steady_clock::now();
        uint64_t perf_vis=total_vis,perf_count=g_native_render_count.load(),perf_us=g_native_render_us.load();
        for (int s = 0; s < headless_seconds * 10; s++) {
            if(g_performance_check) {
                MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) {
                    if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessage(&msg);}
                }
                if(s%10==0) {
                    const auto now=std::chrono::steady_clock::now();
                    const double elapsed=std::chrono::duration<double>(now-perf_time).count();
                    const uint64_t count=g_native_render_count.load(),us=g_native_render_us.load(),vis=total_vis;
                    const auto budget_bits=read_rdram_u32(rdram,0x80051EC4);
                    float vi_budget=0;std::memcpy(&vi_budget,&budget_bits,sizeof(vi_budget));
                    if(elapsed>0.5)std::cout<<"[Perf] T="<<s/10<<" mode="<<read_rdram_u32(rdram,0x80117DA4)
                        <<" VI="<<(vis-perf_vis)/elapsed<<" GFX="<<(count-perf_count)/elapsed
                        <<" Native_ms="<<(count>perf_count ? (us-perf_us)/double(count-perf_count)/1000.0 : 0.0)
                        <<" GPU_ms="<<g_hd_renderer.gpu_raster_us()/1000.0
                        <<" age_ms="<<g_hd_renderer.snapshot_age_ms()<<" VI_budget="<<vi_budget
                        <<" Original="<<unsigned(read_rdram_u8(rdram,0x80102344))<<std::endl;
                    perf_time=now;perf_vis=vis;perf_count=count;perf_us=us;
                }
            }
            if(g_presentation_check){
                MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){
                    if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessage(&msg);}
                }
                if(!intro_check){
                    if(s==80)g_render_height=720;
                    if(s==110)g_render_height=1080;
                    if(s==130)g_render_height=2160;
                    if(s==150)g_render_height=0;
                    if(s==180)g_render_height=720;
                    if(s==210)g_render_height=2160;
                    if(s==100||s==140||s==200)resize_window_scale(g_hwnd,s==100 ? 3:s==140 ? 1:2);
                    if(s==160||s==170) {
                        SendMessage(g_hwnd,WM_KEYDOWN,VK_F11,1);
                        const bool expected=s==160;
                        SendMessage(g_hwnd,WM_KEYDOWN,VK_F11,1ll|(1ll<<30));
                        if(g_fullscreen!=expected) {
                            std::cerr<<"[Fullscreen check] F11 toggle/repeat failed.\n";g_running=false;
                        } else std::cout<<"[Fullscreen check] F11 "<<(expected?"enter":"exit")<<" and repeat suppression: PASS.\n";
                    }
                    if(s==120||s==190)g_filter_smooth=!g_filter_smooth.load();
                }
            }
            // Periodic button pulses to navigate menus:
            // 2.5s-2.8s: Start + A (Intro -> Mode 1)
            // 3.8s-4.1s: Start / A (Title screen -> Main Menu)
            // 5.0s-5.3s: A (Main Menu -> Car Select)
            // 6.2s-6.5s: A (Car Select -> Track/Options)
            // 7.4s-7.7s: A (Options/Track -> Start Race)
            // 8.6s-8.9s: A (Confirm / Race start)
            // 9.8s-10.1s: A
            // 11.0s-11.3s: A
            // 12.2s-12.5s: A
            bool pulse = (s >= 25 && s <= 28) ||
                         (s >= 38 && s <= 41) ||
                         (s >= 50 && s <= 53) ||
                         (s >= 62 && s <= 65) ||
                         (s >= 74 && s <= 77) ||
                         (s >= 86 && s <= 89) ||
                         (s >= 98 && s <= 101) ||
                         (s >= 110 && s <= 113) ||
                         (s >= 122 && s <= 125);

            uint32_t cur_mode = read_rdram_u32(rdram, 0x80117DA4);
            uint8_t sub = read_rdram_u8(rdram, 0x800E97C2);

            if (intro_check) {
                g_buttons.store(0);
                g_stick_x.store(0.0f);
                if ((s+1)%5 == 0) {
                    const std::string filename = "framebuffer_intro_"+std::to_string(s+1)+".bmp";
                    g_presented_frame.save(filename.c_str());
                }
            } else if (cur_mode == 2) {
                // In Mode 2 (Race):
                static int race_ticks = 0;
                race_ticks++;
                auto cur_vi = ultramodern::renderer::get_vi_regs();
                uint32_t origin = cur_vi ? (cur_vi->VI_ORIGIN_REG & 0x1FFFFFFF) : 0;

                if (race_ticks == 10 && origin != 0) {
                    g_presented_frame.save("framebuffer_race_start.bmp");
                    const auto& interp = g_renderer_win32 ? g_renderer_win32->interpreter : g_renderer_null->interpreter;
                    save_fb_to_bmp("framebuffer_rdp_target.bmp", rdram, interp.color_image_addr, 320, 240);
                    std::cout << "[Framebuffer Pair] VI origin=0x" << std::hex << origin
                              << ", RDP target=0x" << interp.color_image_addr
                              << ", delta=" << static_cast<int64_t>(origin) - interp.color_image_addr
                              << ", RDP width=" << std::dec << interp.color_image_width << "\n";
                }

                // Preserve the game's default third-person camera. C-Down
                // switches away from it and hides the car in diagnostic shots.
                g_buttons.store(0x8000); // Accelerate

                if (graphics_check) {
                    // Exercise translucent effects without changing normal navigation.
                    if (race_ticks >= 65 && race_ticks < 85) g_buttons.store(0xC000); // A+B
                    const bool drift = race_ticks >= 90 && race_ticks < 110;
                    if (drift) g_buttons.store(0xA000); // A+Z
                    g_stick_x.store(drift ? 0.75f : 0.0f);
                    if (race_ticks == 75) g_presented_frame.save("framebuffer_graphics_brake.bmp");
                    if (race_ticks == 105) g_presented_frame.save("framebuffer_graphics_drift.bmp");
                    if (race_ticks == 150) g_presented_frame.save("framebuffer_graphics_late.bmp");
                }

                if (race_ticks == 55 && origin != 0) {
                    g_presented_frame.save("framebuffer_race_stable.bmp");
                }
            } else if (cur_mode == 1 && sub == 8) {
                // In Car Showroom (sub == 8): let mosaic transition complete and 3D car turntable render!
                static int showroom_ticks = 0;
                showroom_ticks++;
                auto cur_vi = ultramodern::renderer::get_vi_regs();
                uint32_t origin = cur_vi ? (cur_vi->VI_ORIGIN_REG & 0x1FFFFFFF) : 0;

                if (showroom_ticks == 15 && origin != 0) {
                    g_presented_frame.save("framebuffer_car_showroom.bmp");
                    const auto& interp = g_renderer_win32 ? g_renderer_win32->interpreter : g_renderer_null->interpreter;
                    save_fb_to_bmp("framebuffer_rdp_target.bmp", rdram, interp.color_image_addr, 320, 240);
                }
                if (showroom_ticks >= 25) {
                    g_buttons.store(0x8000); // Press A to confirm car selection
                } else {
                    g_buttons.store(0);
                }
            } else if (cur_mode == 1 && sub == 1) {
                // In Transmission selection: select Automatic (1) and pulse A to start the race!
                write_rdram_u8(rdram, 0x800E97C1, 1);
                static int trans_ticks = 0;
                trans_ticks++;
                if (trans_ticks >= 3) {
                    g_buttons.store(0x8000); // Press A to confirm transmission and start race!
                } else {
                    g_buttons.store(0);
                }
            } else if (s >= 25 && s <= 41) {
                g_buttons.store(pulse ? (0x1000 | 0x8000) : 0); // Start + A
            } else {
                g_buttons.store(pulse ? 0x8000 : 0);            // A button
            }


            if ((s + 1) % 5 == 0) {
                float sec = (s + 1) * 0.1f;
                uint32_t mode = read_rdram_u32(rdram, 0x80117DA4);
                uint8_t submenu = read_rdram_u8(rdram, 0x800E97C2);
                uint8_t prev_sub = read_rdram_u8(rdram, 0x800E97C3);
                uint8_t trans_flag = read_rdram_u8(rdram, 0x800E97C1);
                uint32_t init_flag = read_rdram_u32(rdram, 0x800EA074);
                uint16_t pad_btns = read_rdram_u16(rdram, 0x80077058);
                uint16_t eng_btns = read_rdram_u16(rdram, 0x80100120);
                uint16_t deb_btns = read_rdram_u16(rdram, 0x800FF61C);
                uint32_t intro_timer = read_rdram_u32(rdram, 0x80053654);
                uint8_t intro_stage = read_rdram_u8(rdram, 0x80053650);
                uint32_t dls = g_renderer_win32 ? g_renderer_win32->dl_count.load() : g_renderer_null ? g_renderer_null->dl_count.load() : 0;
                uint32_t rects = g_renderer_win32 ? g_renderer_win32->interpreter.rects_drawn : g_renderer_null ? g_renderer_null->interpreter.rects_drawn : 0;
                uint32_t tris = g_renderer_win32 ? g_renderer_win32->interpreter.triangles_drawn : g_renderer_null ? g_renderer_null->interpreter.triangles_drawn : 0;
                std::cout << "[T+" << std::fixed << std::setprecision(1) << sec << "s] "
                          << "Mode: " << mode
                          << " | Submenu: " << (int)(int8_t)submenu
                          << " (prev:" << (int)(int8_t)prev_sub << ", tf:" << (int)trans_flag << ", if:" << init_flag << ")"
                          << " | Stage: " << (int)intro_stage << " (T=" << intro_timer << ")"
                          << " | Pad0: 0x" << std::hex << pad_btns << " Eng: 0x" << eng_btns << " Deb: 0x" << deb_btns << std::dec
                          << " | DLs: " << dls
                          << " | Rects: " << rects
                          << " | Tris: " << tris << "\n";
            }
            if(g_performance_check) {
                const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
                do {
                    MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) {
                        if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessage(&msg);}
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                } while(std::chrono::steady_clock::now()<until);
            } else std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // 15. Verify group catalogues in RDRAM
    struct CatInfo {
        const char* name;
        uint32_t vram;
        uint32_t count;
        uint32_t expected_bound0;
    };
    CatInfo cats[] = {
        { "Mode 1 (Frontend/Common)", 0x800FD180, 7,  0x96078 },
        { "Mode 0 (Core/Menus)",      0x80101840, 16, 0x15528 },
        { "Mode 2 (Vehicles)",        0x80116540, 18, 0x26C00 },
        { "Mode 3 (Tracks)",          0x80129100, 12, 0x106B78 },
    };

    std::cout << "\n====================================================\n";
    std::cout << " === Group Catalogue RDRAM Verification Report ===\n";
    std::cout << "====================================================\n";
    int loaded_count = 0;
    for (const auto& c : cats) {
        uint32_t w0 = read_rdram_u32(rdram, c.vram);
        uint32_t w1 = read_rdram_u32(rdram, c.vram + 4);
        uint32_t off = read_rdram_u32(rdram, c.vram + 8);
        uint32_t bound = read_rdram_u32(rdram, c.vram + 12);

        bool bound_match = (bound == c.expected_bound0);
        if (bound_match) loaded_count++;

        std::cout << "\n[" << c.name << "] VRAM: 0x" << std::hex << c.vram << std::dec
                  << " (" << c.count << " entries, " << (c.count * 16) << " bytes)\n";
        std::cout << "  Entry 0: Word0=0x" << std::hex << w0
                  << " Word1=0x" << w1
                  << " Offset=0x" << off
                  << " Bound=0x" << bound << std::dec << " (" << bound << " bytes)\n";
        std::cout << "  Status: " << (bound_match ? "SUCCESS (Matched ROM catalog bound!)" : "NOT LOADED / MISMATCH") << "\n";
    }

    std::cout << "\n[Host] Result: " << loaded_count << " / 4 catalogues verified in RDRAM.\n";
    uint32_t dls = g_renderer_win32 ? g_renderer_win32->dl_count.load() : (g_renderer_null ? g_renderer_null->dl_count.load() : 0);
    uint32_t screens = g_renderer_win32 ? g_renderer_win32->screen_update_count.load() : (g_renderer_null ? g_renderer_null->screen_update_count.load() : 0);
    std::cout << "[Host] Graphics display lists submitted: " << dls << "\n";
    std::cout << "[Host] Screen updates processed: " << screens << "\n";
    std::cout << "[Host] Total VI frames simulated: " << total_vis << "\n";
    uint32_t rects = g_renderer_win32 ? g_renderer_win32->interpreter.rects_drawn : (g_renderer_null ? g_renderer_null->interpreter.rects_drawn : 0);
    std::cout << "[Host] 2D Textured rects rasterized: " << rects << "\n";
    const auto& interp = g_renderer_win32 ? g_renderer_win32->interpreter : g_renderer_null->interpreter;
    const float authoritative_scale=interp.render_surfaces.height() ?
        float(interp.render_surfaces.height())/interp.framebuffer_height() : 1.0f;
    std::cout << "[Host] F3DEX2 Opcodes Encountered:\n";
    for (int op = 0; op < 256; op++) {
        if (interp.opcode_counts[op] > 0) {
            std::cout << "  - 0x" << std::hex << op << ": " << std::dec << interp.opcode_counts[op] << "\n";
        }
    }
    std::cout << "[Host] 3D Triangles - Untextured: " << interp.untextured_triangles << "\n";
    std::cout << "[Host] Lighting diagnostics - vertices/light commands/texgen: " << interp.lit_vertices << "/" << interp.light_commands << "/" << interp.texgen_vertices << "\n";
    std::cout << "[Host] Triangles using independent second texture: " << interp.second_texture_triangles << "\n";
    std::cout << "[Host] L3DEX2 lines/pixels and microcode loads: " << interp.lines_drawn << "/"
              << interp.line_pixels_written << "/" << interp.microcode_loads << "\n";
    std::cout << "[Host] Unsupported microcode loads: " << interp.microcode_load_failures << "\n";
    std::cout << "[Host] Raster diagnostics - culled triangles: " << interp.triangles_culled
              << ", depth-rejected pixels: " << interp.pixels_depth_rejected
              << ", invalid-texel pixels: " << interp.pixels_texel_invalid
              << ", written pixels: " << interp.pixels_written << "\n";
    std::cout << "[Host] RDP color target: 0x" << std::hex << interp.color_image_addr
              << " (" << std::dec << interp.color_image_width << " px wide)\n";
    std::cout << "[Host] RDP modes H/L: 0x" << std::hex << interp.other_mode_h << "/0x"
              << interp.other_mode_l << ", combiner W0/W1: 0x" << interp.combine_mode_w0
              << "/0x" << interp.combine_mode_w1 << std::dec << "\n";
    std::cout << "[Host] Triangles by combiner cycle type (0/1/2/3): "
              << interp.combine_cycle_triangles[0] << "/" << interp.combine_cycle_triangles[1]
              << "/" << interp.combine_cycle_triangles[2] << "/" << interp.combine_cycle_triangles[3] << "\n";
    const char* fmt_names[] = { "RGBA", "YUV", "CI", "IA", "I", "?5", "?6", "?7" };
    const char* size_names[] = { "4b", "8b", "16b", "32b" };
    for (int f = 0; f < 8; f++) {
        for (int s = 0; s < 4; s++) {
            if (interp.fmt_counts[f][s] > 0) {
                std::cout << "  - " << fmt_names[f] << " " << size_names[s] << ": " << interp.fmt_counts[f][s] << " triangles\n";
            }
        }
    }
    std::cout << "[Host] Audio buffers queued: " << g_audio_backend.buffers_queued.load() << "\n";
    std::cout << "[Host] Audio total samples queued: " << g_audio_backend.total_samples_queued.load() << "\n";
    std::cout << "[Host] Audio queue overruns / silent inputs discarded / pending frames replaced: "
              << g_audio_backend.audio_latency_overruns.load() << "/"
              << g_audio_backend.silent_audio_inputs_discarded.load() << "/"
              << g_audio_backend.pending_audio_frames_replaced.load() << "\n";
    std::cout << "[Host] Audio device blocks submitted/completed/in-flight: "
              << g_audio_backend.device_blocks_submitted.load() << "/"
              << g_audio_backend.device_blocks_completed.load() << "/"
              << g_audio_backend.in_flight_blocks.load() << "\n";
    std::cout << "[Host] Non-silent audio callback accepts/device submissions: "
              << g_audio_backend.non_silent_queue_accepts.load() << "/"
              << g_audio_backend.non_silent_device_blocks_submitted.load() << "\n";
    std::cout << "[Host] Audio current frequency: " << g_audio_backend.current_freq.load() << " Hz\n";
    std::cout << "[Host] Audio device empty transitions: " << g_audio_backend.device_starvation_transitions.load() << "\n";
    std::cout << "[Host] Unimplemented RSP callbacks by task type (0/GFX/AUDIO/3/other): ";
    for (int i = 0; i < 5; ++i) std::cout << (i ? "/" : "") << g_rsp_task_types[i].load();
    std::cout << "\n";

    // Engine & Thread Diagnostics
    uint32_t val_8006ac24 = read_rdram_u32(rdram, 0x8006AC24);
    uint32_t val_8006ac28 = read_rdram_u32(rdram, 0x8006AC28);
    uint32_t val_8006ac2c = read_rdram_u32(rdram, 0x8006AC2C);
    uint32_t val_800f7b14 = read_rdram_u32(rdram, 0x800F7B14);
    uint32_t val_80117da4 = read_rdram_u32(rdram, 0x80117DA4);
    uint32_t val_800ea074 = read_rdram_u32(rdram, 0x800EA074);
    uint32_t val_801019e4 = read_rdram_u32(rdram, 0x801019E4);

    uint32_t val_800fd14c = read_rdram_u32(rdram, 0x800FD14C);
    uint32_t val_800770a0 = read_rdram_u32(rdram, 0x800770A0);
    uint32_t val_800f1e80 = read_rdram_u32(rdram, 0x800F1E80);

    std::cout << "\n====================================================\n";
    std::cout << " === Game Engine State & Thread Diagnostics ===\n";
    std::cout << "====================================================\n";
    std::cout << "  0x800FD14C (func_800086F8 entry): 0x" << std::hex << val_800fd14c << "\n";
    std::cout << "  0x800770A0 (osEepromProbe done):  0x" << std::hex << val_800770a0 << "\n";
    std::cout << "  0x800F1E80 (func_800400B0 entry): 0x" << std::hex << val_800f1e80 << "\n";
    std::cout << "  0x8006AC24 (VI Buffer Index):     0x" << std::hex << val_8006ac24 << "\n";
    std::cout << "  0x8006AC28 (Active GFX Task Idx):  0x" << std::hex << val_8006ac28 << "\n";
    std::cout << "  0x8006AC2C (Thread 7 Start Flag):  0x" << std::hex << val_8006ac2c << "\n";
    std::cout << "  0x800F7B14 (RSP Busy Flag):        0x" << std::hex << val_800f7b14 << "\n";
    std::cout << "  0x80117DA4 (Game Mode State):      0x" << std::hex << val_80117da4 << "\n";
    std::cout << "  0x800EA074 (State 0 Init Flag):    0x" << std::hex << val_800ea074 << "\n";
    std::cout << "  0x801019E4 (Decompressor Ptr):     0x" << std::hex << val_801019e4 << std::dec << "\n";

    // Inspect Destination Arenas for Asset Groups
    struct ArenaCheck {
        const char* name;
        uint32_t addr;
    };
    ArenaCheck arenas[] = {
        { "Group 0 (Core/Intro)",      0x80341F60 },
        { "Group 1 (Frontend/Common)", 0x80329A50 },
        { "Group 2 (Vehicle Data 1)",  0x8018CA30 },
        { "Group 2 (Vehicle Data 2)",  0x801A4F40 },
        { "Group 3 (Track Data 1)",    0x801BD450 },
        { "Group 3 (Track Data 2)",    0x80221100 },
    };

    std::cout << "\n====================================================\n";
    std::cout << " === Asset Group RDRAM Decompression Inspection ===\n";
    std::cout << "====================================================\n";
    for (const auto& a : arenas) {
        std::cout << "[" << a.name << " at 0x" << std::hex << a.addr << "]:\n ";
        for (int i = 0; i < 32; i += 4) {
            uint32_t val = read_rdram_u32(rdram, a.addr + i);
            std::cout << " 0x" << std::hex << val;
        }
        std::cout << std::dec << "\n";
    }


    auto vi = ultramodern::renderer::get_vi_regs();
    if (vi && vi->VI_ORIGIN_REG != 0) {
        uint32_t origin = vi->VI_ORIGIN_REG & 0x1FFFFFFF;
        const auto frame = tgod::vi_frame_layout(*vi);
        uint32_t width = frame.width, height = frame.height;
        std::cout << "\n[Active Framebuffer at RDRAM 0x" << std::hex << origin << " (" << std::dec << width << "x" << height << ")]:\n";
        size_t non_zero = 0;
        if (frame.visible) {
            const uint16_t* fb = reinterpret_cast<const uint16_t*>(rdram + origin);
            for (size_t i = 0; i < width * height; i++) {
                if (fb[i] != 0) non_zero++;
            }
            std::cout << "  Non-zero 16-bit pixels: " << non_zero << " / " << (width * height) << "\n";

            g_presented_frame.save("framebuffer.bmp");
            save_fb_to_bmp("framebuffer_0x1d6380.bmp", rdram, 0x1d6380, width, height);
            save_fb_to_bmp("framebuffer_0x1fbb80.bmp", rdram, 0x1fbb80, width, height);
        }
    }

    std::cout << "\n[Host] Top Gear Overdrive runner completed.\n";

    ultramodern::quit();
    tgod::controls::shutdown();
    g_hd_renderer.shutdown();
    g_hd_renderer.diagnostics();
    if(g_hwnd&&IsWindow(g_hwnd))DestroyWindow(g_hwnd);
    std::cout << "[Native render] Average: " << g_native_render_us.load()/std::max(uint64_t(1),g_native_render_count.load())
              << " us, maximum: " << g_native_render_max_us.load() << " us; scale: " << authoritative_scale << ".\n";
    std::cout<<"[Native GPU] Completed: "<<g_native_gpu_tasks.load()<<", failed tasks retried on CPU: "<<g_native_gpu_fallbacks.load()<<".\n";
    g_audio_backend.shutdown();
    ultramodern::join_saving_thread();
    if (std::filesystem::exists("saves/tgod_usa.bin")) {
        std::cout << "  [Save Status] NVRAM EEPROM save verified at saves/tgod_usa.bin ("
                  << std::filesystem::file_size("saves/tgod_usa.bin") << " bytes).\n";
    }

    std::cout << std::flush;
    std::fflush(stdout);
    if (g_resolution_check && !g_resolution_check_done.load())
        std::cerr << "[Resolution comparison] Race frame was not captured; increase --seconds.\n";
    std::quick_exit((loaded_count == 4 && (!g_resolution_check || g_resolution_check_done.load()) &&
        (!g_presentation_check || g_hd_renderer.presented())) ? 0 : 2);
}

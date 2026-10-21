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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "recomp.h"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/rsp.hpp"
#include "ultramodern/input.hpp"
#include "f3dex2.hpp"

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
static HWND g_hwnd = nullptr;

// Dynamic XInput gamepad definitions (zero extra DLL link dependencies)
typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} TGOD_XINPUT_GAMEPAD;

typedef struct {
    DWORD dwPacketNumber;
    TGOD_XINPUT_GAMEPAD Gamepad;
} TGOD_XINPUT_STATE;

typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, TGOD_XINPUT_STATE* pState);
static PFN_XInputGetState g_pfnXInputGetState = nullptr;
static bool g_xinput_initialized = false;

static void init_xinput() {
    if (g_xinput_initialized) return;
    g_xinput_initialized = true;
    HMODULE hXInput = LoadLibraryA("xinput1_4.dll");
    if (!hXInput) hXInput = LoadLibraryA("xinput9_1_0.dll");
    if (!hXInput) hXInput = LoadLibraryA("xinput1_3.dll");
    if (hXInput) {
        g_pfnXInputGetState = (PFN_XInputGetState)GetProcAddress(hXInput, "XInputGetState");
        if (g_pfnXInputGetState) {
            std::cout << "[Host] XInput gamepad support initialized successfully.\n";
        }
    }
}

static void update_key_input() {
    if (g_is_headless) return;
    init_xinput();

    uint16_t btns = 0;
    float sx = 0.0f;
    float sy = 0.0f;

    // 1. Check XInput Gamepad if connected
    if (g_pfnXInputGetState) {
        TGOD_XINPUT_STATE xstate{};
        if (g_pfnXInputGetState(0, &xstate) == 0) { // ERROR_SUCCESS == 0
            const auto& pad = xstate.Gamepad;
            if (pad.wButtons & 0x0010) btns |= 0x1000; // Start -> CONT_START
            if (pad.wButtons & 0x1000) btns |= 0x8000; // A -> CONT_A (Accelerate)
            if (pad.wButtons & 0x2000) btns |= 0x4000; // B -> CONT_B (Brake)
            if (pad.wButtons & 0x4000) btns |= 0x2000; // X -> CONT_G (Z-trigger / Handbrake)
            if (pad.wButtons & 0x8000) btns |= 0x0004; // Y -> CONT_D (C-Down: Camera)
            if (pad.wButtons & 0x0100) btns |= 0x0020; // LB -> CONT_L (Shift down)
            if (pad.wButtons & 0x0200) btns |= 0x0010; // RB -> CONT_R (Shift up)

            // Triggers
            if (pad.bRightTrigger > 30) btns |= 0x8000; // RT -> Accelerate
            if (pad.bLeftTrigger > 30)  btns |= 0x4000; // LT -> Brake

            // D-Pad
            if (pad.wButtons & 0x0001) { btns |= 0x0800; sy += 1.0f; } // Up
            if (pad.wButtons & 0x0002) { btns |= 0x0400; sy -= 1.0f; } // Down
            if (pad.wButtons & 0x0004) { btns |= 0x0200; sx -= 1.0f; } // Left
            if (pad.wButtons & 0x0008) { btns |= 0x0100; sx += 1.0f; } // Right

            // Left Thumbstick Analog Steering
            constexpr SHORT deadzone = 7849;
            if (std::abs(pad.sThumbLX) > deadzone) {
                sx = (pad.sThumbLX > 0) ? ((float)(pad.sThumbLX - deadzone) / (32767.0f - deadzone))
                                        : ((float)(pad.sThumbLX + deadzone) / (32768.0f - deadzone));
            }
            if (std::abs(pad.sThumbLY) > deadzone) {
                sy = (pad.sThumbLY > 0) ? ((float)(pad.sThumbLY - deadzone) / (32767.0f - deadzone))
                                        : ((float)(pad.sThumbLY + deadzone) / (32768.0f - deadzone));
            }

            // Right Thumbstick for C-buttons
            if (pad.sThumbRY < -12000) btns |= 0x0004; // C-Down (Camera)
            if (pad.sThumbRX < -12000) btns |= 0x0002; // C-Left (Look back)
            if (pad.sThumbRX > 12000)  btns |= 0x0001; // C-Right (Nitro)
            if (pad.sThumbRY > 12000)  btns |= 0x0008; // C-Up (Horn)
        }
    }

    // 2. Check Keyboard state
    if ((GetAsyncKeyState(VK_RETURN) & 0x8000) || (GetAsyncKeyState(VK_TAB) & 0x8000)) btns |= 0x1000; // CONT_START
    if ((GetAsyncKeyState('X') & 0x8000) || (GetAsyncKeyState(VK_SPACE) & 0x8000) || (GetAsyncKeyState('W') & 0x8000)) btns |= 0x8000; // CONT_A
    if ((GetAsyncKeyState('Z') & 0x8000) || (GetAsyncKeyState(VK_BACK) & 0x8000) || (GetAsyncKeyState('S') & 0x8000)) btns |= 0x4000; // CONT_B
    if ((GetAsyncKeyState('C') & 0x8000) || (GetAsyncKeyState(VK_LCONTROL) & 0x8000)) btns |= 0x2000; // CONT_G (Z-trigger)
    if ((GetAsyncKeyState('A') & 0x8000) || (GetAsyncKeyState('Q') & 0x8000)) btns |= 0x0020; // CONT_L
    if ((GetAsyncKeyState('D') & 0x8000) || (GetAsyncKeyState('E') & 0x8000)) btns |= 0x0010; // CONT_R

    // C-buttons for camera toggle, look back, nitro
    if ((GetAsyncKeyState('V') & 0x8000) || (GetAsyncKeyState('1') & 0x8000)) btns |= 0x0004; // CONT_D (C-Down: Camera)
    if ((GetAsyncKeyState('B') & 0x8000) || (GetAsyncKeyState('2') & 0x8000)) btns |= 0x0002; // CONT_C (C-Left: Rear view)
    if ((GetAsyncKeyState('N') & 0x8000) || (GetAsyncKeyState('3') & 0x8000)) btns |= 0x0001; // CONT_F (C-Right: Nitro)
    if ((GetAsyncKeyState('T') & 0x8000) || (GetAsyncKeyState('4') & 0x8000)) btns |= 0x0008; // CONT_E (C-Up: Horn/Map)

    // D-Pad and Analog stick
    if (GetAsyncKeyState(VK_UP) & 0x8000) {
        btns |= 0x0800; // CONT_UP
        sy = 1.0f;
    }
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
        btns |= 0x0400; // CONT_DOWN
        sy = -1.0f;
    }
    if ((GetAsyncKeyState(VK_LEFT) & 0x8000) || (GetAsyncKeyState('A') & 0x8000)) {
        btns |= 0x0200; // CONT_LEFT
        sx = -1.0f;
    }
    if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) || (GetAsyncKeyState('D') & 0x8000)) {
        btns |= 0x0100; // CONT_RIGHT
        sx = 1.0f;
    }

    uint16_t old_btns = g_buttons.exchange(btns);
    g_stick_x.store(sx);
    g_stick_y.store(sy);

    if (btns != old_btns && btns != 0) {
        std::cout << "[Input Event] Pressed: 0x" << std::hex << btns
                  << ", Stick: (" << std::dec << sx << ", " << sy << ")\n";
    }
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
    std::atomic<uint64_t> non_silent_queue_accepts{0};
    std::atomic<uint64_t> non_silent_device_blocks_submitted{0};
    std::atomic<size_t> ai_frames_pending{0};

    std::mutex queue_mutex;
    std::condition_variable cv_queue;
    std::vector<int16_t> pending_samples;
    std::thread worker_thread;
    bool enabled = true;
    std::atomic<uint64_t> audio_latency_overruns{0};
    std::atomic<uint64_t> silent_audio_inputs_discarded{0};
    std::atomic<uint64_t> pending_audio_frames_replaced{0};
    bool drop_announced = false;

    Win32AudioBackend() {
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
        cv_queue.notify_all();
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
    }

    void queue_samples(const int16_t* samples, size_t sample_count) {
        if (!enabled || !samples || sample_count == 0) return;
        bool has_nonzero = false;
        for (size_t s = 0; s < sample_count; ++s) {
            if (samples[s] != 0) { has_nonzero = true; break; }
        }

        if (g_is_headless) {
            static FILE* f_dump = nullptr;
            static int dumped_non_zero = 0;
            static int total_buffers_seen = 0;
            total_buffers_seen++;
            if (has_nonzero && dumped_non_zero == 0) {
                std::cout << "[Audio Forensics] First NON-ZERO audio buffer found at buffer #" << total_buffers_seen << "!\n";
                f_dump = fopen("raw_audio_test.raw", "wb");
            }
            if (f_dump && dumped_non_zero < 60) {
                fwrite(samples, sizeof(int16_t), sample_count, f_dump);
                dumped_non_zero++;
                if (dumped_non_zero == 60) {
                    fclose(f_dump);
                    f_dump = nullptr;
                    std::cout << "[Audio Forensics] Successfully saved 60 non-zero audio buffers to raw_audio_test.raw\n";
                }
            }
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
        cv_queue.notify_one();
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
                std::unique_lock<std::mutex> lock(queue_mutex);
                cv_queue.wait_for(lock, std::chrono::milliseconds(20), [&]() {
                    return !running.load() || pending_samples.size() >= BUFFER_CHUNK_SAMPLES;
                });
                if (!running.load()) break;

                if (pending_samples.size() >= BUFFER_CHUNK_SAMPLES) {
                    chunk.assign(pending_samples.begin(), pending_samples.begin() + BUFFER_CHUNK_SAMPLES);
                    pending_samples.erase(pending_samples.begin(), pending_samples.begin() + BUFFER_CHUNK_SAMPLES);
                }
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
    }
};

static Win32AudioBackend g_audio_backend;

// Native implementation of __osAiDeviceBusy (func_8004A580)
// Return 0 so game audio engine never triggers assertion failure (func_80009480)
extern "C" void func_8004A580(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
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
            interpreter.process_display_list(task->t.data_ptr);
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
        if (count < 3) {
            auto vi = ultramodern::renderer::get_vi_regs();
            if (vi) {
                std::cout << "[Screen Update #" << count << "] FB Origin: 0x" << std::hex << vi->VI_ORIGIN_REG
                          << ", Width: " << std::dec << vi->VI_WIDTH_REG
                          << ", Status: 0x" << std::hex << vi->VI_STATUS_REG << std::dec << "\n";
            }
        }
    }
    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
};

static std::atomic<bool> g_filter_smooth{true};
static bool g_fullscreen = false;
static WINDOWPLACEMENT g_prev_wp = { sizeof(WINDOWPLACEMENT) };

static void render_frame_to_dc(HDC hdc, HWND hwnd, const uint32_t* rgba, const BITMAPINFO* bmi, uint32_t width, uint32_t height) {
    if (!hdc || !hwnd || !rgba || !bmi) return;
    RECT client_rect;
    GetClientRect(hwnd, &client_rect);
    int client_w = client_rect.right - client_rect.left;
    int client_h = client_rect.bottom - client_rect.top;
    if (client_w <= 0 || client_h <= 0) return;

    // Maintain 4:3 aspect ratio with clean letterbox / pillarbox
    int target_w = client_w;
    int target_h = (client_w * 3) / 4;
    if (target_h > client_h) {
        target_h = client_h;
        target_w = (client_h * 4) / 3;
    }
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
            interpreter.process_display_list(task->t.data_ptr);
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
        if (origin == 0) return;
        uint32_t width = (vi->VI_WIDTH_REG != 0 && vi->VI_WIDTH_REG <= 640) ? vi->VI_WIDTH_REG : 320;
        uint32_t height = 240;

        if (origin + width * height * 2 <= 8 * 1024 * 1024) {
            const uint16_t* src = reinterpret_cast<const uint16_t*>(rdram + origin);
            for (size_t y = 0; y < height; y++) {
                size_t src_y = (y >= height - 1) ? (height - 2) : y;
                for (size_t x = 0; x < width; x++) {
                    uint16_t px = src[src_y * width + x];
                    uint8_t r = ((px >> 11) & 0x1F) * 255 / 31;
                    uint8_t g = ((px >> 6) & 0x1F) * 255 / 31;
                    uint8_t b = ((px >> 1) & 0x1F) * 255 / 31;
                    rgba_pixels[y * width + x] = (r << 16) | (g << 8) | b;
                }
            }

            HDC hdc = GetDC(hwnd);
            if (hdc) {
                render_frame_to_dc(hdc, hwnd, rgba_pixels.data(), &bmi, width, height);
                ReleaseDC(hwnd, hdc);
            }
        }
    }

    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
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
    // 1. Mark controller 0 as present in status registers
    write_rdram_u8(rdram, 0x800522D0, read_rdram_u8(rdram, 0x800522D0) | 0x01); // Controller 1 connected
    write_rdram_u8(rdram, 0x80100184, 1);    // 1 controller

    // 2. Read host buttons and analog stick
    uint16_t btns = g_buttons.load();
    float sx = g_stick_x.load();
    float sy = g_stick_y.load();

    int8_t stick_x = (int8_t)std::clamp((int)(sx * 80.0f), -128, 127);
    int8_t stick_y = (int8_t)std::clamp((int)(sy * 80.0f), -128, 127);

    // 3. Inject directly into OSContPad 0 at RDRAM 0x80077058
    write_rdram_u16(rdram, 0x80077058, btns);
    write_rdram_u8(rdram, 0x8007705A, (uint8_t)stick_x);
    write_rdram_u8(rdram, 0x8007705B, (uint8_t)stick_y);
    write_rdram_u8(rdram, 0x8007705C, 0); // errno = 0 (no error)

    // 4. Also inject into SI command/response buffer at D_800F7B30
    write_rdram_u32(rdram, 0x800F7B30, 0x01040000);
    write_rdram_u16(rdram, 0x800F7B34, btns);
    write_rdram_u8(rdram, 0x800F7B36, (uint8_t)stick_x);
    write_rdram_u8(rdram, 0x800F7B37, (uint8_t)stick_y);

    ultramodern::send_si_message();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1; // Prevent window background erasing flicker
        case WM_CLOSE:
            g_running = false;
            DestroyWindow(hwnd);
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
            } else if (wParam == VK_F11) {
                toggle_fullscreen(hwnd);
            }
            update_key_input();
            break;
        case WM_SYSKEYDOWN:
            if (wParam == VK_RETURN && (lParam & (1 << 29))) { // Alt + Enter
                toggle_fullscreen(hwnd);
                return 0;
            }
            break;
        case WM_KEYUP:
            update_key_input();
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_renderer_win32 && !g_renderer_win32->rgba_pixels.empty()) {
                render_frame_to_dc(hdc, hwnd, g_renderer_win32->rgba_pixels.data(), &g_renderer_win32->bmi, 320, 240);
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
    std::cout << " Phase 10: Visual Fidelity & Presentation Enhancements\n";
    std::cout << "====================================================\n\n";

    bool headless = false;
    int headless_seconds = 4;
    std::filesystem::path rom_path = "top_gear_rom/normalized/Top Gear Overdrive (USA).z64";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
            g_is_headless = true;
        } else if (arg == "--seconds" && i + 1 < argc) {
            headless_seconds = std::atoi(argv[++i]);
        } else if (arg.rfind("--", 0) != 0) {
            rom_path = arg;
        }
    }

    if (!std::filesystem::exists(rom_path)) {
        std::cerr << "[ERROR] ROM file not found: " << rom_path << "\n";
        return 1;
    }

    std::ifstream rom_file(rom_path, std::ios::binary | std::ios::ate);
    if (!rom_file.is_open()) {
        std::cerr << "[ERROR] Failed to open ROM: " << rom_path << "\n";
        return 1;
    }

    std::streamsize rom_size = rom_file.tellg();
    rom_file.seekg(0, std::ios::beg);
    std::vector<uint8_t> rom_bytes(rom_size);
    if (!rom_file.read(reinterpret_cast<char*>(rom_bytes.data()), rom_size)) {
        std::cerr << "[ERROR] Failed to read ROM contents.\n";
        return 1;
    }
    std::cout << "[Host] Loaded ROM: " << rom_path.string() << " (" << rom_size << " bytes)\n";

    // 2. Setup Win32 Window if in interactive mode
    if (!headless) {
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
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            wr.right - wr.left, wr.bottom - wr.top,
            nullptr, nullptr, GetModuleHandle(nullptr), nullptr
        );
        ShowWindow(g_hwnd, SW_SHOW);
        UpdateWindow(g_hwnd);
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

    // 4. Register input callbacks
    ultramodern::input::callbacks_t input_cbs{};
    input_cbs.poll_input = []() {
        update_key_input();
    };
    input_cbs.get_input = [](int controller_num, uint16_t* buttons, float* x, float* y) -> bool {
        if (controller_num == 0) {
            update_key_input();
            *buttons = g_buttons.load();
            *x = g_stick_x.load();
            *y = g_stick_y.load();
            return true;
        }
        return false;
    };
    input_cbs.set_rumble = [](int, bool) {};
    input_cbs.get_connected_device_info = [](int controller_num) -> ultramodern::input::connected_device_info_t {
        if (controller_num == 0) {
            return { ultramodern::input::Device::Controller, ultramodern::input::Pak::None };
        }
        return { ultramodern::input::Device::None, ultramodern::input::Pak::None };
    };
    ultramodern::input::set_callbacks(input_cbs);
    std::cout << "[Host] Input callbacks registered:\n"
              << "       Enter=Start | X/Space=A | Z/Back=B | C=Z-Trig | A/S=L/R | Arrows=D-Pad/Stick | Esc=Quit\n"
              << "       F1-F4=Scale (1x-4x) | F5=Toggle Filter (Crisp/Smooth) | F11 / Alt+Enter=Fullscreen\n";

    ultramodern::rsp::callbacks_t rsp_cbs{};
    rsp_cbs.init = []() {};
    rsp_cbs.run_task = [](uint8_t*, const OSTask*) -> bool { return true; };
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

    static auto save_fb_to_bmp = [](const char* filename, const uint8_t* rdram_ptr, uint32_t fb_origin, uint32_t w, uint32_t h) {
        if (fb_origin + w * h * 2 > 8 * 1024 * 1024) return;
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

        // Check if fb_origin has content; if empty, test standard double-buffer addresses
        uint32_t chosen_origin = fb_origin;
        auto count_nonzero = [&](uint32_t org) -> size_t {
            if (org + w * h * 2 > 8 * 1024 * 1024) return 0;
            const uint16_t* ptr = reinterpret_cast<const uint16_t*>(rdram_ptr + org);
            size_t c = 0;
            for (size_t i = 0; i < w * h; i++) if (ptr[i] != 0) c++;
            return c;
        };

        size_t nz = count_nonzero(chosen_origin);
        if (nz == 0) {
            size_t nz_a = count_nonzero(0x1d6380);
            size_t nz_b = count_nonzero(0x1fbb80);
            if (nz_a > 0 || nz_b > 0) {
                chosen_origin = (nz_a >= nz_b) ? 0x1d6380 : 0x1fbb80;
                nz = std::max(nz_a, nz_b);
            }
        }

        const uint16_t* fb = reinterpret_cast<const uint16_t*>(rdram_ptr + chosen_origin);
        for (int y = (int)h - 1; y >= 0; y--) {
            int src_y = (y >= (int)h - 1) ? ((int)h - 2) : y;
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
        std::cout << "  [Screenshot] Saved " << filename << " from origin=0x" << std::hex << chosen_origin
                  << std::dec << " (" << nz << " / " << (w * h) << " non-zero pixels)\n";
    };

    if (!headless && g_hwnd) {
        std::cout << "\n====================================================\n";
        std::cout << " === Top Gear Overdrive Interactive Session ===\n";
        std::cout << "====================================================\n";
        std::cout << "Window is active at 60 FPS. Press ESC or close the window to exit.\n\n";

        MSG msg{};
        auto last_title_update = std::chrono::steady_clock::now();
        uint64_t last_vis = total_vis;

        while (g_running.load()) {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    g_running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
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

                uint32_t mode = read_rdram_u32(rdram, 0x80117DA4);
                const char* mode_name = (mode == 0) ? "Intro" : (mode == 1) ? "Menus" : (mode == 2) ? "Race (In-Game)" : "Game";
                uint32_t tris = g_renderer_win32 ? g_renderer_win32->interpreter.triangles_drawn : 0;
                char title_buf[256];
                std::snprintf(title_buf, sizeof(title_buf),
                    "Top Gear Overdrive (PC) | %s | %.1f FPS | Tris: %u | Audio: %u buf | Filter: %s [F5]",
                    mode_name, fps, tris, g_audio_backend.buffers_queued.load(),
                    g_filter_smooth.load() ? "Smooth" : "Crisp");
                SetWindowTextA(g_hwnd, title_buf);

                static auto last_console_log = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_console_log).count() >= 1000) {
                    last_console_log = now;
                    uint32_t rsp_busy = read_rdram_u32(rdram, 0x800F7B14);
                    uint32_t th7 = read_rdram_u32(rdram, 0x8006AC2C);
                    uint8_t stage = read_rdram_u8(rdram, 0x80053650);
                    uint32_t itimer = read_rdram_u32(rdram, 0x80053654);
                    std::cout << "[Live] Mode: " << mode_name
                              << " | FPS: " << std::fixed << std::setprecision(1) << fps
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
        std::cout << "[Host] Waiting " << (headless_seconds * 1000) << " ms for background game threads (automated navigation to Mode 2)...\n";
        for (int s = 0; s < headless_seconds * 10; s++) {
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

            if (cur_mode == 2) {
                // In Mode 2 (Race):
                static int race_ticks = 0;
                race_ticks++;
                auto cur_vi = ultramodern::renderer::get_vi_regs();
                uint32_t origin = cur_vi ? (cur_vi->VI_ORIGIN_REG & 0x1FFFFFFF) : 0;

                if (race_ticks == 10 && origin != 0) {
                    save_fb_to_bmp("framebuffer_race_start.bmp", rdram, origin, 320, 240);
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

                if (race_ticks == 55 && origin != 0) {
                    save_fb_to_bmp("framebuffer_race_stable.bmp", rdram, origin, 320, 240);
                }
            } else if (cur_mode == 1 && sub == 8) {
                // In Car Showroom (sub == 8): let mosaic transition complete and 3D car turntable render!
                static int showroom_ticks = 0;
                showroom_ticks++;
                auto cur_vi = ultramodern::renderer::get_vi_regs();
                uint32_t origin = cur_vi ? (cur_vi->VI_ORIGIN_REG & 0x1FFFFFFF) : 0;

                if (showroom_ticks == 15 && origin != 0) {
                    save_fb_to_bmp("framebuffer_car_showroom.bmp", rdram, origin, 320, 240);
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
                uint32_t dls = g_renderer_null ? g_renderer_null->dl_count.load() : 0;
                uint32_t rects = g_renderer_null ? g_renderer_null->interpreter.rects_drawn : 0;
                uint32_t tris = g_renderer_null ? g_renderer_null->interpreter.triangles_drawn : 0;
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
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
    std::cout << "[Host] F3DEX2 Opcodes Encountered:\n";
    for (int op = 0; op < 256; op++) {
        if (interp.opcode_counts[op] > 0) {
            std::cout << "  - 0x" << std::hex << op << ": " << std::dec << interp.opcode_counts[op] << "\n";
        }
    }
    std::cout << "[Host] 3D Triangles - Untextured: " << interp.untextured_triangles << "\n";
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
        uint32_t width = (vi->VI_WIDTH_REG != 0 && vi->VI_WIDTH_REG <= 640) ? vi->VI_WIDTH_REG : 320;
        uint32_t height = 240;
        std::cout << "\n[Active Framebuffer at RDRAM 0x" << std::hex << origin << " (" << std::dec << width << "x" << height << ")]:\n";
        size_t non_zero = 0;
        if (origin + width * height * 2 <= 8 * 1024 * 1024) {
            const uint16_t* fb = reinterpret_cast<const uint16_t*>(rdram + origin);
            for (size_t i = 0; i < width * height; i++) {
                if (fb[i] != 0) non_zero++;
            }
            std::cout << "  Non-zero 16-bit pixels: " << non_zero << " / " << (width * height) << "\n";

            save_fb_to_bmp("framebuffer.bmp", rdram, origin, width, height);
            save_fb_to_bmp("framebuffer_0x1d6380.bmp", rdram, 0x1d6380, width, height);
            save_fb_to_bmp("framebuffer_0x1fbb80.bmp", rdram, 0x1fbb80, width, height);
        }
    }

    std::cout << "\n[Host] Top Gear Overdrive runner completed.\n";

    ultramodern::quit();
    ultramodern::join_saving_thread();
    if (std::filesystem::exists("saves/tgod_usa.bin")) {
        std::cout << "  [Save Status] NVRAM EEPROM save verified at saves/tgod_usa.bin ("
                  << std::filesystem::file_size("saves/tgod_usa.bin") << " bytes).\n";
    }

    std::cout << std::flush;
    std::fflush(stdout);
    std::quick_exit((loaded_count == 4) ? 0 : 2);
}

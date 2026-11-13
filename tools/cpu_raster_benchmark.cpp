// Replay a private --performance-check capture with identical initial RDRAM.
// These files contain game data: keep them out of source history and packages.
// Version 1 is local ABI only. Version 2 uses explicit renderer fields.
#include "f3dex2.hpp"
#include "renderer_capture.hpp"
#include <chrono>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: tgod_cpu_benchmark snapshot.bin [output.rdram] [--iterations N] [--height 0|720|1080|2160] [--resident] [--output-frame path] [--export-v2 path]\n";return 2;}
    const char* output_path=nullptr;
    const char* frame_path=nullptr;
    const char* export_path=nullptr;
    unsigned iterations=10;
    unsigned height=0;
    bool resident=false;
    for(int i=2;i<argc;++i) {
        if(std::strcmp(argv[i],"--iterations")==0) {
            if(++i==argc){std::cerr<<"Missing iteration count.\n";return 2;}
            const char* end=argv[i]+std::strlen(argv[i]);
            const auto parsed=std::from_chars(argv[i],end,iterations);
            if(parsed.ec!=std::errc{}||parsed.ptr!=end||iterations<1||iterations>10000) {
                std::cerr<<"Iterations must be between 1 and 10000.\n";return 2;
            }
        } else if(std::strcmp(argv[i],"--height")==0) {
            if(++i==argc){std::cerr<<"Missing render height.\n";return 2;}
            const char* end=argv[i]+std::strlen(argv[i]);
            const auto parsed=std::from_chars(argv[i],end,height);
            if(parsed.ec!=std::errc{}||parsed.ptr!=end||(height!=0&&height!=720&&height!=1080&&height!=2160)) {
                std::cerr<<"Invalid render height.\n";return 2;
            }
        } else if(std::strcmp(argv[i],"--output-frame")==0) {
            if(++i==argc){std::cerr<<"Missing output frame path.\n";return 2;}
            frame_path=argv[i];
        } else if(std::strcmp(argv[i],"--export-v2")==0) {
            if(++i==argc){std::cerr<<"Missing capture output path.\n";return 2;}
            export_path=argv[i];
        } else if(std::strcmp(argv[i],"--resident")==0) resident=true;
        else if(!output_path) output_path=argv[i];
        else {std::cerr<<"Unexpected argument.\n";return 2;}
    }
#ifndef TGOD_BENCH_GPU
    if(resident){std::cerr<<"Resident mode requires the GPU benchmark.\n";return 2;}
#endif
    if(resident&&!height){std::cerr<<"Resident mode requires an HD height.\n";return 2;}
    if(frame_path&&!height){std::cerr<<"Frame output requires an HD height.\n";return 2;}
    std::vector<uint8_t> input(8*1024*1024),ram(input.size()),expected;
    F3DEX2Interpreter initial(ram.data());
    auto* tail=reinterpret_cast<char*>(&initial.layout_width);
    const auto tail_bytes=sizeof(initial)-(tail-reinterpret_cast<char*>(&initial));
    uint32_t header[6]{};
    std::ifstream capture(argv[1],std::ios::binary);
    tgod::capture::Archive<true> header_reader(capture);
    header_reader(header[0],header[1],header[2],header[3],header[4],header[5]);
    if(!header_reader.ok()||header[0]!=tgod::capture::magic ||
       (header[1]!=1&&header[1]!=tgod::capture::version)) {
        std::cerr<<"Invalid capture or different renderer ABI.\n";return 2;
    }
    if(header[1]==1) {
        if(header[2]!=tail_bytes){std::cerr<<"Incompatible legacy renderer ABI.\n";return 2;}
        capture.read(reinterpret_cast<char*>(initial.segments),sizeof(initial.segments));
        capture.read(reinterpret_cast<char*>(initial.tmem),sizeof(initial.tmem));
        capture.read(tail,tail_bytes);
    } else if(!tgod::capture::read_v2(capture,initial,header[2])) {
        std::cerr<<"Invalid version 2 renderer state.\n";return 2;
    }
    capture.read(reinterpret_cast<char*>(input.data()),input.size());
    if(!capture||capture.peek()!=std::char_traits<char>::eof()) {
        std::cerr<<"Truncated or oversized capture.\n";return 2;
    }
    if(export_path) {
        initial.rdram=input.data();std::ofstream converted(export_path,std::ios::binary);
        if(!tgod::capture::write(converted,initial,header[3],header[4],header[5])) {
            std::cerr<<"Cannot write converted capture.\n";return 2;
        }
        initial.rdram=ram.data();
    }
    initial.diagnostics_enabled=false;initial.raster_backend=nullptr;
#ifdef TGOD_BENCH_GPU
    GPURaster gpu;
    if(!gpu.available())return 77;
    GPURaster::Statistics warmup_stats{};
    GPURaster::Statistics previous_stats{};
    std::vector<uint64_t> gpu_intervals;
#endif
    double total_ms=0;
    std::vector<double> samples;samples.reserve(iterations);
    std::vector<uint16_t> expected_frame;
    uint32_t expected_width=0,expected_height=0;
    for(unsigned iteration=0;iteration<=iterations;++iteration) {
        ram=input;
        auto renderer=initial;renderer.rdram=ram.data();
        renderer.render_surfaces.clear();renderer.render_surfaces.set_height(height);
        const auto start=std::chrono::steady_clock::now();
#ifdef TGOD_BENCH_GPU
        gpu.begin_frame(resident);renderer.raster_backend=&gpu;
#endif
        renderer.process_display_list(header[3],header[4],header[5]);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(height) {
#ifdef TGOD_BENCH_GPU
            if(resident){gpu.readback(renderer,0,8*1024*1024);renderer.render_surfaces.sync_all();}
#endif
            std::vector<uint16_t> frame;uint32_t w=0,h=0;
            if(!renderer.render_surfaces.copy_scanout(renderer.color_image_addr,renderer.color_image_width,
                renderer.framebuffer_height(),frame,w,h)) {std::cerr<<"Missing replay surface.\n";return 1;}
            if(iteration==0){expected_frame=std::move(frame);expected_width=w;expected_height=h;}
            else if(w!=expected_width||h!=expected_height||frame!=expected_frame){std::cerr<<"Non-deterministic HD surface.\n";return 1;}
        }
        if(iteration==0) {
            expected=ram;
#ifdef TGOD_BENCH_GPU
            warmup_stats=gpu.statistics();previous_stats=warmup_stats;
#endif
        }
        else {
            if(ram!=expected){std::cerr<<"Non-deterministic replay.\n";return 1;}
            total_ms+=ms;
            samples.push_back(ms);
#ifdef TGOD_BENCH_GPU
            const auto completed=gpu.statistics();
            if(completed.gpu_samples==previous_stats.gpu_samples+1)
                gpu_intervals.push_back(completed.gpu_elapsed_us-previous_stats.gpu_elapsed_us);
            previous_stats=completed;
#endif
        }
    }
    if(output_path) {
        std::ofstream output(output_path,std::ios::binary);
        output.write(reinterpret_cast<const char*>(expected.data()),expected.size());
        if(!output)return 2;
    }
    if(frame_path) {
        std::ofstream output(frame_path,std::ios::binary);
        const uint32_t dimensions[]={expected_width,expected_height};
        output.write(reinterpret_cast<const char*>(dimensions),sizeof(dimensions));
        output.write(reinterpret_cast<const char*>(expected_frame.data()),expected_frame.size()*sizeof(uint16_t));
        if(!output)return 2;
    }
    std::sort(samples.begin(),samples.end());
    const auto percentile=[&](size_t percent) {
        return samples[(samples.size()*percent+99)/100-1];
    };
    std::cout<<(height ? (resident ? "Resident HD CPU submission" : "HD replay with readback") : "Native replay")
        <<" ("<<iterations<<" repetitions, one warmup excluded): mean "
        <<total_ms/iterations<<" ms; p50 "<<percentile(50)<<" ms; p95 "
        <<percentile(95)<<" ms; p99 "<<percentile(99)<<" ms; max "<<samples.back()<<" ms\n";
#ifdef TGOD_BENCH_GPU
    const auto timing=gpu.statistics();
    const auto gpu_samples=timing.gpu_samples-warmup_stats.gpu_samples;
    if(gpu_samples)std::cout<<"GPU replay span: "<<(timing.gpu_elapsed_us-warmup_stats.gpu_elapsed_us)/double(gpu_samples)/1000.0
        <<" ms mean ("<<gpu_samples<<" completed queries; "<<(timing.timing_skipped-warmup_stats.timing_skipped)<<" skipped).\n";
    else std::cout<<"GPU replay span: no completed timing samples.\n";
    if(!gpu_intervals.empty()) {
        std::sort(gpu_intervals.begin(),gpu_intervals.end());
        const auto gpu_percentile=[&](size_t percent){return gpu_intervals[(gpu_intervals.size()*percent+99)/100-1]/1000.0;};
        std::cout<<"GPU replay span p50 "<<gpu_percentile(50)<<" ms; p95 "<<gpu_percentile(95)
            <<" ms; p99 "<<gpu_percentile(99)<<" ms ("<<gpu_intervals.size()<<" individually resolved samples).\n";
    }
    std::cout<<"GPU preparation totals: binning "<<(timing.bin_build_us-warmup_stats.bin_build_us)
        <<" us; input uploads "<<(timing.upload_us-warmup_stats.upload_us)<<" us / "
        <<(timing.upload_bytes-warmup_stats.upload_bytes)<<" bytes; Map "
        <<(timing.map_wait_us-warmup_stats.map_wait_us)<<" us / "<<(timing.map_count-warmup_stats.map_count)
        <<" calls; readback "<<(timing.readback_bytes-warmup_stats.readback_bytes)<<" bytes.\n";
#endif
}

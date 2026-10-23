// Replay a private --performance-check capture with identical initial RDRAM.
// These files contain game data: keep them out of source history and packages.
// The POD tail format is local ABI only, not a portable save-state format.
#include "f3dex2.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc,char** argv) {
    if(argc<2||argc>3){std::cerr<<"Usage: tgod_cpu_benchmark snapshot.bin [output.rdram]\n";return 2;}
    std::vector<uint8_t> input(8*1024*1024),ram(input.size()),expected;
    F3DEX2Interpreter initial(ram.data());
    auto* tail=reinterpret_cast<char*>(&initial.layout_width);
    const auto tail_bytes=sizeof(initial)-(tail-reinterpret_cast<char*>(&initial));
    uint32_t header[6]{};
    std::ifstream capture(argv[1],std::ios::binary);
    capture.read(reinterpret_cast<char*>(header),sizeof(header));
    if(!capture||header[0]!=0x43504754||header[1]!=1||header[2]!=tail_bytes) {
        std::cerr<<"Invalid capture or different renderer ABI.\n";return 2;
    }
    capture.read(reinterpret_cast<char*>(initial.segments),sizeof(initial.segments));
    capture.read(reinterpret_cast<char*>(initial.tmem),sizeof(initial.tmem));
    capture.read(tail,tail_bytes);
    capture.read(reinterpret_cast<char*>(input.data()),input.size());
    if(!capture||capture.peek()!=std::char_traits<char>::eof()) {
        std::cerr<<"Truncated or oversized capture.\n";return 2;
    }
    initial.diagnostics_enabled=false;initial.raster_backend=nullptr;
#ifdef TGOD_BENCH_GPU
    GPURaster gpu;
    if(!gpu.available())return 77;
#endif
    double total_ms=0;
    for(int iteration=0;iteration<11;++iteration) {
        ram=input;
        auto renderer=initial;renderer.rdram=ram.data();
        const auto start=std::chrono::steady_clock::now();
#ifdef TGOD_BENCH_GPU
        gpu.begin_frame(false);renderer.raster_backend=&gpu;
#endif
        renderer.process_display_list(header[3],header[4],header[5]);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(iteration==0)expected=ram;
        else {
            if(ram!=expected){std::cerr<<"Non-deterministic replay.\n";return 1;}
            total_ms+=ms;
        }
    }
    if(argc==3) {
        std::ofstream output(argv[2],std::ios::binary);
        output.write(reinterpret_cast<const char*>(expected.data()),expected.size());
        if(!output)return 2;
    }
    std::cout<<"Mean native replay (10 repetitions, warmup excluded): "<<total_ms/10<<" ms\n";
}

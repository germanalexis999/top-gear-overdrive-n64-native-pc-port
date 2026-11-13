#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace tgod::gpu_bins {
inline constexpr uint32_t command_words=96;
using Command=std::array<uint32_t,command_words>;

// Stable prefix-sum binning: every bin retains the original command order.
inline void build(const std::vector<Command>& commands,uint32_t bx,uint32_t by,
                  std::vector<uint32_t>& spans,std::vector<uint32_t>& refs,
                  std::vector<uint32_t>& cursors) {
    if(commands.size()>8192)throw std::runtime_error("GPU command limit exceeded");
    const size_t bins=size_t(bx)*by;
    spans.assign(bins*2,0);
    for(const auto& c:commands) {
        if(c[2]>=c[4]||c[3]>=c[5])continue;
        if((c[4]-1)/32>=bx||(c[5]-1)/32>=by)throw std::runtime_error("GPU command outside bins");
        for(uint32_t y=c[3]/32;y<=(c[5]-1)/32;++y)
            for(uint32_t x=c[2]/32;x<=(c[4]-1)/32;++x)++spans[(size_t(y)*bx+x)*2+1];
    }
    uint64_t count=0;cursors.resize(bins);
    for(size_t i=0;i<bins;++i) {
        spans[i*2]=uint32_t(count);cursors[i]=uint32_t(count);count+=spans[i*2+1];
        if(count>(1u<<29))throw std::runtime_error("GPU bin reference limit exceeded");
    }
    refs.resize(size_t(count));
    for(uint32_t i=0;i<commands.size();++i){const auto& c=commands[i];
        if(c[2]>=c[4]||c[3]>=c[5])continue;
        for(uint32_t y=c[3]/32;y<=(c[5]-1)/32;++y)
            for(uint32_t x=c[2]/32;x<=(c[4]-1)/32;++x)
                refs[cursors[size_t(y)*bx+x]++]=i*command_words;
    }
}
}

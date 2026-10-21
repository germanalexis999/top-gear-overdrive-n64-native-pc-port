#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace tgod {
// Enlarged render targets live outside emulated RDRAM. The game keeps its
// original addresses, strides and CPU framebuffer reads at native resolution.
class RenderSurfaces {
    struct Surface {
        uint32_t address, native_width, native_height, width, height;
        float scale;
        uint64_t used = 0;
        bool dirty = false;
        std::vector<uint16_t> pixels, mirror;
    };
    std::map<uint64_t, Surface> surfaces;
    uint32_t render_height = 0;
    uint64_t clock = 0;
    uint8_t* ram = nullptr;

    void import(Surface& s) {
        std::memcpy(s.mirror.data(), ram+s.address, s.mirror.size()*2);
        for (uint32_t y=0; y<s.height; ++y) {
            const auto sy=std::min(uint32_t(y/s.scale),s.native_height-1);
            for (uint32_t x=0; x<s.width; ++x)
                s.pixels[size_t(y)*s.width+x]=s.mirror[size_t(sy)*s.native_width+
                    std::min(uint32_t(x/s.scale),s.native_width-1)];
        }
        s.dirty=false;
    }
    void sync(Surface& s) {
        if (!s.dirty) return;
        for (uint32_t y=0; y<s.native_height; ++y) {
            const auto sy=std::min(uint32_t((y+0.5f)*s.scale),s.height-1);
            for (uint32_t x=0; x<s.native_width; ++x)
                s.mirror[size_t(y)*s.native_width+x]=s.pixels[size_t(sy)*s.width+
                    std::min(uint32_t((x+0.5f)*s.scale),s.width-1)];
        }
        std::memcpy(ram+s.address,s.mirror.data(),s.mirror.size()*2);
        s.dirty=false;
    }
public:
    struct Buffer {
        uint16_t* pixels = nullptr;
        uint32_t width = 0, height = 0;
        float scale = 1;
    };
    uint32_t height() const { return render_height; }
    bool set_height(uint32_t height) {
        if (height!=0 && height!=720 && height!=1080 && height!=2160) return false;
        if (height!=render_height) { sync_all(); surfaces.clear(); render_height=height; }
        return true;
    }
    void clear() { surfaces.clear(); clock=0; }
    void begin_task(uint8_t* rdram) {
        if (ram!=rdram) { clear(); ram=rdram; }
        // Import CPU writes between tasks without throwing away the extra detail
        // of targets whose native mirror has not changed.
        for (auto& [key,s] : surfaces)
            if (std::memcmp(ram+s.address,s.mirror.data(),s.mirror.size()*2)) import(s);
    }
    Buffer acquire(uint8_t* rdram, uint32_t address, uint32_t width, uint32_t height, bool dirty=true) {
        if (!rdram || !width || !height || uint64_t(address)+uint64_t(width)*height*2>8*1024*1024) return {};
        if (ram!=rdram) { clear(); ram=rdram; }
        if (!render_height || (width!=320 && width!=640))
            return {reinterpret_cast<uint16_t*>(ram+address),width,height,1};
        const uint64_t key=(uint64_t(address)<<32)|width;
        auto it=surfaces.find(key);
        if (it==surfaces.end()) {
            if (surfaces.size()>=8) {
                auto oldest=std::min_element(surfaces.begin(),surfaces.end(),
                    [](const auto& a,const auto& b) {return a.second.used<b.second.used;});
                sync(oldest->second); surfaces.erase(oldest);
            }
            const float scale=float(render_height)/(width==320 ? 240.0f : 480.0f);
            Surface s{address,width,height,uint32_t(width*scale),uint32_t(height*scale),scale};
            s.pixels.resize(size_t(s.width)*s.height);s.mirror.resize(size_t(width)*height);
            import(s);it=surfaces.emplace(key,std::move(s)).first;
        }
        Surface& s=it->second;s.used=++clock;s.dirty|=dirty;
        return {s.pixels.data(),s.width,s.height,s.scale};
    }
    void sync_all() { if (ram) for (auto& [key,s] : surfaces) sync(s); }
    void sync_range(uint64_t address,uint64_t bytes) {
        for (auto& [key,s] : surfaces)
            if (address<uint64_t(s.address)+s.mirror.size()*2 && address+bytes>s.address) sync(s);
    }
    bool copy_scanout(uint32_t origin,uint32_t width,uint32_t height,
        std::vector<uint16_t>& pixels,uint32_t& output_width,uint32_t& output_height) const {
        const Surface* found=nullptr;
        for (const auto& [key,s] : surfaces)
            if (s.native_width==width && origin>=s.address &&
                origin<uint64_t(s.address)+s.mirror.size()*2 && (!found || s.used>found->used)) found=&s;
        if (!found) return false;
        const auto& s=*found;
        const uint32_t offset=(origin-s.address)/2;
        const uint32_t ox=uint32_t((offset%width)*s.scale),oy=uint32_t((offset/width)*s.scale);
        output_width=uint32_t(width*s.scale);output_height=uint32_t(height*s.scale);
        if (!output_width || !output_height || uint64_t(output_width)*output_height>4096ull*4096) return false;
        pixels.resize(size_t(output_width)*output_height);
        for (uint32_t y=0;y<output_height;++y) {
            const uint16_t* source=s.pixels.data()+size_t(std::min(oy+y,s.height-1))*s.width;
            for (uint32_t x=0;x<output_width;++x) pixels[size_t(y)*output_width+x]=source[std::min(ox+x,s.width-1)];
        }
        return true;
    }
};
}

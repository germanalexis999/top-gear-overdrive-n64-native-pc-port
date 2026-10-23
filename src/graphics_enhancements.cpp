#include "graphics_enhancements.hpp"
#include "recomp.h"
#include "funcs.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
tgod::GraphicsEnhancements settings;
// MEM_* subtracts the sign-extended KSEG base. Zero-extending these addresses
// would access 4 GiB past RDRAM instead of the intended emulated word.
bool racing(uint8_t* rdram) { return MEM_W(0,(gpr)(int32_t)0x80117DA4)==2; }
bool ram_range(uint64_t address,uint32_t bytes) {
    const uint32_t a=uint32_t(address);
    return (a&0xFF800000u)==0x80000000u && uint64_t(a&0x7FFFFF)+bytes<=8*1024*1024;
}
float unpack_float(uint64_t word) {
    const uint32_t bits=uint32_t(word);float value;
    std::memcpy(&value,&bits,sizeof(value));return value;
}
uint32_t pack_float(float value) {
    uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));return bits;
}
}

void tgod::configure_graphics_enhancements(GraphicsEnhancements options) {
    options.draw_distance_percent=std::clamp(options.draw_distance_percent,100u,200u);
    options.extra_track_sections=std::min(options.extra_track_sections,2u);
    settings=options;
}

// USA v0 func_800121A8 entry: f12 is near, f14 is far, a2/a3 are detail
// cutoffs. Update their inputs together, before the game derives camera planes.
// Never accumulate a multiplier on already-derived camera/FOV state.
extern "C" void tgod_extend_camera(uint8_t* rdram,recomp_context* ctx) {
    if(settings.draw_distance_percent==100 || !racing(rdram))return;
    const float far=ctx->f14.fl,near=ctx->f12.fl;
    if(!std::isfinite(far)||!std::isfinite(near)||near<=0||far<=near)return;
    // Preserve the original value if a scene already exceeds this bound.
    const float extended=std::max(far,std::min(far*(settings.draw_distance_percent/100.0f),30000.0f));
    const float factor=extended/far;
    ctx->f14.fl=extended;
    for(auto* argument:{&ctx->r6,&ctx->r7}) {
        const float distance=unpack_float(*argument);
        if(std::isfinite(distance)&&distance>0)
            *argument=pack_float(std::min(distance*factor,extended));
    }
}

// USA v0 func_80004B50 before 0x80005CC4: v0 just received the selected detail
// byte. This branch belongs to the 0x400 model-array family. Change only the
// array index, keeping queue flags, object visibility and simulation intact.
// Level zero is the same model used at close distance.
extern "C" void tgod_select_high_detail(uint8_t* rdram,recomp_context* ctx) {
    if(settings.force_high_detail && racing(rdram))ctx->r2=0;
}

// USA v0 func_8003597C before finalizing the deduplicated scenery list.
// The original current/previous/next queries run first. Extra queries retain
// the game's frustum callback, deduplication, and 255-entry capacity check.
extern "C" void tgod_extend_track_visibility(uint8_t* rdram,recomp_context* ctx) {
    if(!settings.extra_track_sections || !racing(rdram) ||
       !ram_range(ctx->r16,0x1C) || !ram_range(ctx->r17,0xB0))return;
    const int count=MEM_W(4,ctx->r17);
    const int section=MEM_H(0x18,ctx->r16);
    const bool loop=MEM_BU(0x14,ctx->r16)==0;
    if(count<=0 || count>4096 || section<0 || section>=count)return;
    for(int step=2;step<=int(settings.extra_track_sections)+1;++step) {
        for(int direction:{-1,1}) {
            if(uint32_t(MEM_W(0,(gpr)(int32_t)0x800F8780))>=0xFF)return;
            int extra=section+direction*step;
            if(loop)extra=(extra%count+count)%count;
            else if(extra<0 || extra>=count)continue;
            // A separate context preserves the caller's live MIPS registers.
            recomp_context call=*ctx;
            call.r4=ctx->r17;call.r5=extra;call.r6=(int32_t)0x80035C9C;call.r7=loop;
            func_80017C7C(rdram,&call);
        }
    }
}

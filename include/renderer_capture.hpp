#pragma once

// Private diagnostic snapshots. Version 2 stores explicit little-endian fields;
// it never serializes object padding, pointers, or RenderSurfaces ownership.
#include "f3dex2.hpp"
#include <bit>
#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

namespace tgod::capture {
constexpr uint32_t magic=0x43504754,version=2;
constexpr size_t ram_bytes=8*1024*1024,max_state_bytes=65536;

template<bool Reading> class Archive {
    std::istream* in=nullptr;
    std::ostream* out=nullptr;
    bool valid=true;
public:
    explicit Archive(std::istream& stream):in(&stream){}
    explicit Archive(std::ostream& stream):out(&stream){}
    bool ok() const {return valid && (Reading ? bool(*in) : bool(*out));}
    template<class T> void field(T& value) {
        if constexpr(std::is_array_v<T>) {
            for(auto& element:value)field(element);
        } else if constexpr(std::is_same_v<T,bool>) {
            uint8_t v=value ? 1:0;field(v);
            if constexpr(Reading){if(v>1)valid=false;value=v!=0;}
        } else if constexpr(std::is_enum_v<T>) {
            using U=std::underlying_type_t<T>;U v=static_cast<U>(value);field(v);
            if constexpr(Reading)value=static_cast<T>(v);
        } else if constexpr(std::is_floating_point_v<T>) {
            static_assert(sizeof(T)==sizeof(uint32_t));
            static_assert(std::numeric_limits<T>::is_iec559);
            uint32_t bits=std::bit_cast<uint32_t>(value);field(bits);
            if constexpr(Reading)value=std::bit_cast<T>(bits);
        } else {
            static_assert(std::is_integral_v<T> && sizeof(T)<=8);
            using U=std::make_unsigned_t<T>;
            U bits=static_cast<U>(value);
            for(size_t i=0;i<sizeof(T);++i){
                if constexpr(Reading){const int c=in->get();if(c==std::char_traits<char>::eof()){valid=false;return;}
                    if(i==0)bits=0;bits|=U(U(uint8_t(c))<<(i*8));}
                else out->put(char(uint8_t(bits>>(i*8))));
            }
            if constexpr(Reading) {
                if constexpr(std::is_signed_v<T>)value=std::bit_cast<T>(bits);
                else value=bits;
            }
        }
    }
    template<class... T> void operator()(T&... fields){(field(fields),...);}
    void length(size_t& value,size_t limit) {
        uint64_t n=value;field(n);
        if constexpr(Reading){if(n>limit || n>std::numeric_limits<size_t>::max()){valid=false;return;}value=size_t(n);}
    }
};

template<bool R> void matrix(Archive<R>& a,Matrix4x4& v){a(v.m);}
template<bool R> void vertex(Archive<R>& a,Vertex3D& v){
    a(v.x,v.y,v.z,v.clip_x,v.clip_y,v.clip_z,v.clip_w,v.screen_x,v.screen_y,v.screen_z,
      v.u,v.v,v.r,v.g,v.b,v.a,v.clipped,v.is_screen_space);
}
template<bool R> bool state(Archive<R>& a,F3DEX2Interpreter& r){
    a(r.segments,r.tmem,r.layout_width,r.layout_height,r.color_image_addr,r.color_image_width,
      r.color_image_format,r.color_image_size,r.depth_image_addr,r.geometry_mode,
      r.fill_color,r.env_color,r.prim_color,r.primitive_lod_fraction,r.primitive_min_lod,
      r.blend_color,r.fog_color,r.combine_mode_w0,r.combine_mode_w1,r.combine_mode_set,r.combine_mode_updates,
      r.cached_combine_w0,r.cached_combine_w1);
    for(auto& c:r.cached_combine)a(c.ca,c.cb,c.cc,c.cd,c.aa,c.ab,c.ac,c.ad);
    a(r.primitive_depth,r.primitive_depth_delta,r.texture_image_addr,r.texture_format,r.texture_size,
      r.texture_width,r.texture_tile,r.texture_max_level,r.texture_enabled,r.texture_scale_s,r.texture_scale_t,
      r.tlut_tmem_addr,r.tmem_version);
    for(auto& t:r.tiles)a(t.format,t.size,t.line,t.tmem_addr,t.palette,t.clamp_s,t.mirror_s,t.mask_s,t.shift_s,
        t.clamp_t,t.mirror_t,t.mask_t,t.shift_t,t.uls,t.ult,t.lrs,t.lrt,t.uls_fraction,t.ult_fraction);
    a(r.scissor_xh,r.scissor_yh,r.scissor_xl,r.scissor_yl,r.other_mode_h,r.other_mode_l,r.dl_stack);
    a.length(r.dl_stack_depth,F3DEX2Interpreter::MAX_DL_STACK);
    a(r.rects_drawn,r.triangles_drawn,r.lines_drawn,r.line_pixels_written,r.microcode_loads,r.microcode_load_failures,
      r.microcode,r.triangles_culled,r.pixels_depth_rejected,r.pixels_texel_invalid,r.pixels_written,
      r.combine_cycle_triangles,r.untextured_triangles,r.lit_vertices,r.light_commands,r.texgen_vertices,r.second_texture_triangles);
    for(auto& light:r.lights)a(light.color,light.direction);
    a(r.light_count,r.look_at,r.rdp_half_1,r.rdp_half_2,r.opcode_counts,r.fmt_counts);
    matrix(a,r.modelview_mtx);matrix(a,r.proj_mtx);matrix(a,r.mvp_mtx);
    for(auto& m:r.modelview_stack)matrix(a,m);
    a.length(r.modelview_stack_depth,F3DEX2Interpreter::MAX_MTX_STACK);
    for(auto& m:r.proj_stack)matrix(a,m);
    a.length(r.proj_stack_depth,4);
    a(r.viewport_scale,r.viewport_trans);
    for(auto& v:r.vertex_buffer)vertex(a,v);
    if constexpr(R) {
        if(r.light_count>8 || r.texture_tile>7)return false;
        if(r.microcode!=F3DEX2Interpreter::Microcode::F3DEX2 &&
           r.microcode!=F3DEX2Interpreter::Microcode::L3DEX2 && r.microcode!=F3DEX2Interpreter::Microcode::Unknown)return false;
    }
    return a.ok();
}

inline bool write(std::ostream& out,F3DEX2Interpreter& r,uint32_t dl,uint32_t data,uint32_t data_size){
    if(!r.rdram)return false;
    std::ostringstream payload(std::ios::out|std::ios::binary);Archive<false> writer(payload);
    if(!state(writer,r))return false;
    const std::string bytes=payload.str();if(bytes.size()>max_state_bytes)return false;
    uint32_t m=magic,v=version,n=uint32_t(bytes.size());Archive<false> header(out);
    header(m,v,n,dl,data,data_size);
    out.write(bytes.data(),bytes.size());out.write(reinterpret_cast<const char*>(r.rdram),ram_bytes);
    return header.ok()&&bool(out);
}

inline bool read_v2(std::istream& in,F3DEX2Interpreter& r,uint32_t size){
    if(size>max_state_bytes)return false;
    std::string bytes(size,'\0');in.read(bytes.data(),size);if(!in)return false;
    std::istringstream payload(bytes,std::ios::in|std::ios::binary);Archive<true> reader(payload);
    return state(reader,r)&&payload.peek()==std::char_traits<char>::eof();
}
}

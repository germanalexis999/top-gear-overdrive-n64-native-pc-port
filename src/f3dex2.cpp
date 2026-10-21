#include "f3dex2.hpp"
#include <cstring>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <vector>
#include <iomanip>
#include <string>

static inline uint16_t f3d_read_u16(const uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<const uint16_t*>(rdram + ((addr & 0x00FFFFFF) ^ 2));
}

static inline int16_t f3d_read_s16(const uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<const int16_t*>(rdram + ((addr & 0x00FFFFFF) ^ 2));
}

static inline uint8_t f3d_read_u8(const uint8_t* rdram, uint32_t addr) {
    return *(rdram + ((addr & 0x00FFFFFF) ^ 3));
}

Matrix4x4 Matrix4x4::identity() {
    Matrix4x4 res{};
    res.m[0][0] = 1.0f;
    res.m[1][1] = 1.0f;
    res.m[2][2] = 1.0f;
    res.m[3][3] = 1.0f;
    return res;
}

Matrix4x4 Matrix4x4::from_n64_fixed(const int16_t* int_part, const uint16_t* frac_part) {
    Matrix4x4 res{};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int idx = r * 4 + c;
            int32_t val = ((int32_t)int_part[idx] << 16) | (frac_part[idx] & 0xFFFF);
            res.m[r][c] = (float)val / 65536.0f;
        }
    }
    return res;
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& rhs) const {
    Matrix4x4 res{};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            res.m[r][c] = m[r][0] * rhs.m[0][c] +
                          m[r][1] * rhs.m[1][c] +
                          m[r][2] * rhs.m[2][c] +
                          m[r][3] * rhs.m[3][c];
        }
    }
    return res;
}

void Matrix4x4::transform(float x, float y, float z, float& ox, float& oy, float& oz, float& ow) const {
    ox = x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0];
    oy = x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1];
    oz = x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2];
    ow = x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3];
}

F3DEX2Interpreter::F3DEX2Interpreter(uint8_t* rdram_in) : rdram(rdram_in) {
    reset();
}

void F3DEX2Interpreter::reset() {
    std::memset(segments, 0, sizeof(segments));
    std::memset(tmem, 0, sizeof(tmem));
    std::memset(tiles, 0, sizeof(tiles));
    color_image_addr = 0;
    color_image_width = 320;
    color_image_format = 0;
    color_image_size = 2;
    depth_image_addr = 0;
    geometry_mode = 0;
    fill_color = 0;
    env_color = 0xFFFFFFFF;
    prim_color = 0xFFFFFFFF;
    primitive_lod_fraction = primitive_min_lod = 0;
    blend_color = 0;
    fog_color = 0;
    combine_mode_w0 = 0;
    combine_mode_w1 = 0;
    combine_mode_set = false;
    combine_mode_updates = 0;
    primitive_depth = 0;
    primitive_depth_delta = 0;
    texture_image_addr = 0;
    texture_format = 0;
    texture_size = 2;
    texture_width = 0;
    scissor_xh = 0;
    scissor_yh = 0;
    scissor_xl = 320;
    scissor_yl = 240;
    dl_stack_depth = 0;
    rects_drawn = 0;
    triangles_drawn = 0;
    lines_drawn = line_pixels_written = microcode_loads = microcode_load_failures = 0;
    microcode = Microcode::F3DEX2;
    triangles_culled = 0;
    pixels_depth_rejected = 0;
    pixels_texel_invalid = 0;
    pixels_written = 0;
    std::memset(combine_cycle_triangles, 0, sizeof(combine_cycle_triangles));
    untextured_triangles = 0;
    lit_vertices = 0;
    light_commands = 0;
    texgen_vertices = 0;
    second_texture_triangles = 0;
    light_count = 0;
    std::memset(lights, 0, sizeof(lights));
    std::memset(look_at, 0, sizeof(look_at));
    look_at[0][0] = look_at[1][1] = 1.0f;
    rdp_half_1 = 0;
    rdp_half_2 = 0;
    std::memset(opcode_counts, 0, sizeof(opcode_counts));
    std::memset(fmt_counts, 0, sizeof(fmt_counts));

    modelview_mtx = Matrix4x4::identity();
    proj_mtx = Matrix4x4::identity();
    mvp_mtx = Matrix4x4::identity();
    modelview_stack_depth = 0;
    proj_stack_depth = 0;
    viewport_scale[0] = 640; viewport_scale[1] = 480; viewport_scale[2] = 511; viewport_scale[3] = 0;
    viewport_trans[0] = 640; viewport_trans[1] = 480; viewport_trans[2] = 511; viewport_trans[3] = 0;
    texture_tile = 0;
    texture_max_level = 0;
    texture_enabled = true;
    texture_scale_s = 0x10000;
    texture_scale_t = 0x10000;
    tlut_tmem_addr = 2048;
    std::memset(vertex_buffer, 0, sizeof(vertex_buffer));
}

void F3DEX2Interpreter::update_mvp() {
    mvp_mtx = modelview_mtx * proj_mtx;
}

void F3DEX2Interpreter::transform_vertex(Vertex3D& v) {
    mvp_mtx.transform(v.x, v.y, v.z, v.clip_x, v.clip_y, v.clip_z, v.clip_w);
    if (v.clip_w <= 0.0001f) {
        v.clipped = true;
        return;
    }
    float inv_w = 1.0f / v.clip_w;
    float ndc_x = v.clip_x * inv_w;
    float ndc_y = v.clip_y * inv_w;
    float ndc_z = v.clip_z * inv_w;

    float vs_x = (float)viewport_scale[0] / 4.0f;
    float vs_y = (float)viewport_scale[1] / 4.0f;
    float vs_z = (float)viewport_scale[2];
    float vt_x = (float)viewport_trans[0] / 4.0f;
    float vt_y = (float)viewport_trans[1] / 4.0f;
    float vt_z = (float)viewport_trans[2];

    v.screen_x = ndc_x * vs_x + vt_x;
    v.screen_y = -ndc_y * vs_y + vt_y;
    v.screen_z = ndc_z * vs_z + vt_z;
    v.clipped = (v.clip_z < -v.clip_w || v.clip_z > v.clip_w);
}

void F3DEX2Interpreter::light_and_texgen_vertex(Vertex3D& vertex, int8_t nx, int8_t ny, int8_t nz) {
    const float normal[3] = {nx / 127.0f, ny / 127.0f, nz / 127.0f};
    // With row-vector vertex transforms, M * L brings the view-space light
    // direction into the normal's local space. Translation has no effect.
    auto normal_dot_direction = [&](const float* direction) {
        float local[3]{};
        float length_squared = 0;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column)
                local[row] += modelview_mtx.m[row][column] * direction[column];
            length_squared += local[row] * local[row];
        }
        if (length_squared < 1e-12f) return 0.0f;
        const float inverse_length = 1.0f / std::sqrt(length_squared);
        return std::clamp((normal[0] * local[0] + normal[1] * local[1] + normal[2] * local[2]) * inverse_length, -1.0f, 1.0f);
    };
    if (geometry_mode & 0x00020000) {
        float color[3] = {float(lights[light_count].color[0]), float(lights[light_count].color[1]), float(lights[light_count].color[2])};
        for (uint32_t light = 0; light < light_count; ++light) {
            const float strength = std::max(0.0f, normal_dot_direction(lights[light].direction));
            for (int channel = 0; channel < 3; ++channel) color[channel] += strength * lights[light].color[channel];
        }
        vertex.r = uint8_t(std::clamp(std::lround(color[0]), 0l, 255l));
        vertex.g = uint8_t(std::clamp(std::lround(color[1]), 0l, 255l));
        vertex.b = uint8_t(std::clamp(std::lround(color[2]), 0l, 255l));
    }
    if (geometry_mode & 0x00040000) {
        ++texgen_vertices;
        float generated[2]{};
        for (int axis = 0; axis < 2; ++axis) {
            const float dot = normal_dot_direction(look_at[axis]);
            generated[axis] = (geometry_mode & 0x00080000)
                ? std::acos(-dot) * (1024.0f / 3.14159265358979323846f) : (dot + 1.0f) * 512.0f;
        }
        vertex.u = generated[0] * texture_scale_s / 65536.0f;
        vertex.v = generated[1] * texture_scale_t / 65536.0f;
    }
}

void F3DEX2Interpreter::rasterize_triangle(const Vertex3D& v0, const Vertex3D& v1, const Vertex3D& v2) {
    if (v0.is_screen_space || v1.is_screen_space || v2.is_screen_space) {
        rasterize_triangle_screen(v0, v1, v2);
        return;
    }

    Vertex3D poly_buf[2][16];
    int cur_buf = 0;
    poly_buf[0][0] = v0;
    poly_buf[0][1] = v1;
    poly_buf[0][2] = v2;
    int poly_count = 3;

    const auto plane_distance = [](const Vertex3D& v, int plane) {
        switch (plane) {
            case 0: return v.clip_x + v.clip_w; // left
            case 1: return v.clip_w - v.clip_x; // right
            case 2: return v.clip_y + v.clip_w; // bottom
            case 3: return v.clip_w - v.clip_y; // top
            case 4: return v.clip_z + v.clip_w; // near
            default: return v.clip_w - v.clip_z; // far
        }
    };
    const auto interpolate = [](const Vertex3D& a, const Vertex3D& b, float t) {
        Vertex3D out{};
        const auto mix = [t](float x, float y) { return x + (y - x) * t; };
        out.clip_x = mix(a.clip_x, b.clip_x);
        out.clip_y = mix(a.clip_y, b.clip_y);
        out.clip_z = mix(a.clip_z, b.clip_z);
        out.clip_w = mix(a.clip_w, b.clip_w);
        out.u = mix(a.u, b.u);
        out.v = mix(a.v, b.v);
        out.r = (uint8_t)std::clamp((int)std::lround(mix(a.r, b.r)), 0, 255);
        out.g = (uint8_t)std::clamp((int)std::lround(mix(a.g, b.g)), 0, 255);
        out.b = (uint8_t)std::clamp((int)std::lround(mix(a.b, b.b)), 0, 255);
        out.a = (uint8_t)std::clamp((int)std::lround(mix(a.a, b.a)), 0, 255);
        out.is_screen_space = false;
        return out;
    };

    for (int plane = 0; plane < 6 && poly_count >= 3; ++plane) {
        int next_buf = 1 - cur_buf;
        int next_count = 0;

        Vertex3D previous = poly_buf[cur_buf][poly_count - 1];
        float previous_distance = plane_distance(previous, plane);
        bool previous_inside = previous_distance >= 0.0f;

        for (int i = 0; i < poly_count; ++i) {
            const Vertex3D& current = poly_buf[cur_buf][i];
            const float current_distance = plane_distance(current, plane);
            const bool current_inside = current_distance >= 0.0f;

            if (current_inside != previous_inside) {
                const float denominator = previous_distance - current_distance;
                if (std::abs(denominator) > 1.0e-7f && next_count < 15) {
                    poly_buf[next_buf][next_count++] = interpolate(previous, current, previous_distance / denominator);
                }
            }
            if (current_inside && next_count < 15) {
                poly_buf[next_buf][next_count++] = current;
            }
            previous = current;
            previous_distance = current_distance;
            previous_inside = current_inside;
        }

        cur_buf = next_buf;
        poly_count = next_count;
    }

    if (poly_count < 3) return;
    for (int i = 0; i < poly_count; ++i) {
        Vertex3D& vertex = poly_buf[cur_buf][i];
        if (vertex.clip_w <= 1.0e-7f) return;
        const float inv_w = 1.0f / vertex.clip_w;
        vertex.screen_x = vertex.clip_x * inv_w * ((float)viewport_scale[0] / 4.0f) + (float)viewport_trans[0] / 4.0f;
        vertex.screen_y = -vertex.clip_y * inv_w * ((float)viewport_scale[1] / 4.0f) + (float)viewport_trans[1] / 4.0f;
        vertex.screen_z = vertex.clip_z * inv_w * (float)viewport_scale[2] + (float)viewport_trans[2];
        vertex.clipped = false;
    }
    for (int i = 1; i + 1 < poly_count; ++i) {
        rasterize_triangle_screen(poly_buf[cur_buf][0], poly_buf[cur_buf][i], poly_buf[cur_buf][i + 1]);
    }
}

bool F3DEX2Interpreter::fetch_tile_texel(const Tile& tile, int32_t su, int32_t sv,
    uint8_t& out_r, uint8_t& out_g, uint8_t& out_b, uint8_t& out_a) const {
    const int32_t tw = tile.mask_s ? (1 << tile.mask_s) : int32_t(tile.lrs - tile.uls) + 1;
    const int32_t th = tile.mask_t ? (1 << tile.mask_t) : int32_t(tile.lrt - tile.ult) + 1;
    if (tw <= 0 || th <= 0 || tw > 1024 || th > 1024) return false;
    const uint32_t pixel_size = tile.format == 0 && tile.size == 3 ? 2 : tile.size;
    const uint32_t stride = tile.line ? tile.line * 8 : ((uint32_t(tw) << pixel_size) + 1) / 2;
    // TMEM is a circular 4 KiB address space; a tile access
    // crossing its end wraps instead of producing an invalid texel.
    auto read_tmem8 = [&](uint32_t address) -> uint8_t { return tmem[address & 0xFFF]; };
    if (tile.clamp_s) {
        su = std::clamp(su, 0, tw - 1);
    } else if (tile.mask_s > 0) {
        int32_t m = 1 << tile.mask_s;
        su = su % (m * 2);
        if (su < 0) su += m * 2;
        if (tile.mirror_s && su >= m) {
            su = (m * 2 - 1) - su;
        } else {
            su = su % m;
        }
        if (su >= tw) su = tw - 1;
    } else {
        su = su % tw;
        if (su < 0) su += tw;
    }

    if (tile.clamp_t) {
        sv = std::clamp(sv, 0, th - 1);
    } else if (tile.mask_t > 0) {
        int32_t m = 1 << tile.mask_t;
        sv = sv % (m * 2);
        if (sv < 0) sv += m * 2;
        if (tile.mirror_t && sv >= m) {
            sv = (m * 2 - 1) - sv;
        } else {
            sv = sv % m;
        }
        if (sv >= th) sv = th - 1;
    } else {
        sv = sv % th;
        if (sv < 0) sv += th;
    }

    if (tile.format == 0 && tile.size == 3) { // RGBA32: RG and BA occupy separate 2 KiB banks.
        const uint32_t index = ((tile.tmem_addr + sv * stride + su * 2) ^ ((sv & 1) ? 4u : 0u)) & 0x7FF;
        out_r = tmem[index]; out_g = tmem[(index + 1) & 0x7FF];
        out_b = tmem[index | 0x800]; out_a = tmem[((index + 1) & 0x7FF) | 0x800];
        return true;
    } else if (tile.format == 3 && tile.size == 2) { // IA16
        const uint32_t index = (tile.tmem_addr + sv * stride + su * 2) ^ ((sv & 1) ? 4u : 0u);
        out_r = out_g = out_b = read_tmem8(index);
        out_a = read_tmem8(index + 1);
        return true;
    } else if (tile.format == 0 && tile.size == 2) { // RGBA16
        uint32_t tmem_idx = ((tile.tmem_addr + (sv * stride) + su * 2) ^ ((sv & 1) ? 4u : 0u)) & 0xFFF;
        {
            uint16_t c16 = ((uint16_t)read_tmem8(tmem_idx) << 8) | read_tmem8(tmem_idx + 1);
            out_r = ((c16 >> 11) & 0x1F) * 255 / 31;
            out_g = ((c16 >> 6) & 0x1F) * 255 / 31;
            out_b = ((c16 >> 1) & 0x1F) * 255 / 31;
            out_a = (c16 & 1) ? 255 : 0;
            return true;
        }
    } else if (tile.format == 2 && tile.size == 1) { // CI8
        uint32_t tmem_idx = ((tile.tmem_addr + (sv * stride) + su) ^ ((sv & 1) ? 4u : 0u)) & 0xFFF;
        {
            uint8_t idx = read_tmem8(tmem_idx);
            uint32_t pal_offset = tlut_tmem_addr + (idx * 2);
            {
                uint16_t c16 = ((uint16_t)read_tmem8(pal_offset) << 8) | read_tmem8(pal_offset + 1);
                out_r = ((c16 >> 11) & 0x1F) * 255 / 31;
                out_g = ((c16 >> 6) & 0x1F) * 255 / 31;
                out_b = ((c16 >> 1) & 0x1F) * 255 / 31;
                out_a = (c16 & 1) ? 255 : 0;
                return true;
            }
        }
    } else if (tile.format == 2 && tile.size == 0) { // CI4
        uint32_t byte_idx = ((tile.tmem_addr + (sv * stride) + (su / 2)) ^ ((sv & 1) ? 4u : 0u)) & 0xFFF;
        {
            uint8_t b = read_tmem8(byte_idx);
            uint8_t nib = (su & 1) ? (b & 0x0F) : ((b >> 4) & 0x0F);
            uint32_t pal_offset = tlut_tmem_addr + ((tile.palette * 16 + nib) * 2);
            {
                uint16_t c16 = ((uint16_t)read_tmem8(pal_offset) << 8) | read_tmem8(pal_offset + 1);
                out_r = ((c16 >> 11) & 0x1F) * 255 / 31;
                out_g = ((c16 >> 6) & 0x1F) * 255 / 31;
                out_b = ((c16 >> 1) & 0x1F) * 255 / 31;
                out_a = (c16 & 1) ? 255 : 0;
                return true;
            }
        }
    } else if (tile.format == 3 && tile.size == 1) { // IA8
        uint32_t tmem_idx = ((tile.tmem_addr + (sv * stride) + su) ^ ((sv & 1) ? 4u : 0u)) & 0xFFF;
        {
            uint8_t byte = read_tmem8(tmem_idx);
            uint8_t alpha = byte & 0x0F;
            uint8_t intensity = (byte >> 4) & 0x0F;
            out_r = out_g = out_b = intensity * 255 / 15;
            out_a = alpha * 255 / 15;
            return true;
        }
    } else if (tile.format == 3 && tile.size == 0) { // IA4
        uint32_t byte_idx = ((tile.tmem_addr + (sv * stride) + (su / 2)) ^ ((sv & 1) ? 4u : 0u)) & 0xFFF;
        {
            uint8_t b = read_tmem8(byte_idx);
            uint8_t nib = (su & 1) ? (b & 0x0F) : ((b >> 4) & 0x0F);
            out_r = out_g = out_b = ((nib >> 1) & 0x07) * 255 / 7;
            out_a = (nib & 1) ? 255 : 0;
            return true;
        }
    } else if (tile.format == 4 && tile.size == 1) { // I8
        uint32_t tmem_idx = ((tile.tmem_addr + (sv * stride) + su) ^ ((sv & 1) ? 4u : 0u)) & 0xFFF;
        {
            uint8_t intensity = read_tmem8(tmem_idx);
            out_r = out_g = out_b = intensity;
            out_a = intensity;
            return true;
        }
    } else if (tile.format == 4 && tile.size == 0) { // I4
        uint32_t byte_idx = ((tile.tmem_addr + (sv * stride) + (su / 2)) ^ ((sv & 1) ? 4u : 0u)) & 0xFFF;
        {
            uint8_t b = read_tmem8(byte_idx);
            uint8_t nib = (su & 1) ? (b & 0x0F) : ((b >> 4) & 0x0F);
            uint8_t intensity = nib * 255 / 15;
            out_r = out_g = out_b = intensity;
            out_a = intensity;
            return true;
        }
    } else { // Generic 16-bit
        uint32_t tmem_idx = ((tile.tmem_addr + (sv * stride) + su * 2) ^ ((sv & 1) ? 4u : 0u)) & 0xFFF;
        {
            uint16_t c16 = ((uint16_t)read_tmem8(tmem_idx) << 8) | read_tmem8(tmem_idx + 1);
            out_r = ((c16 >> 11) & 0x1F) * 255 / 31;
            out_g = ((c16 >> 6) & 0x1F) * 255 / 31;
            out_b = ((c16 >> 1) & 0x1F) * 255 / 31;
            out_a = 255;
            return true;
        }
    }
    return false;
}

F3DEX2Interpreter::TextureLOD F3DEX2Interpreter::select_texture_lod(float footprint) const {
    TextureLOD lod;
    if (!(other_mode_h & (1u << 16))) return lod;
    const bool sharpen = (other_mode_h & (1u << 17)) != 0;
    const bool detail = (other_mode_h & (1u << 18)) != 0;
    if (sharpen || detail) footprint = std::max(footprint, primitive_min_lod / 32.0f);
    footprint = std::max(footprint, 1e-8f);
    int level = int(std::floor(std::log2(footprint)));
    lod.fraction = footprint / std::ldexp(1.0f, std::max(level, 0)) - 1.0f;
    if (sharpen && footprint < 1) lod.fraction = footprint - 1;
    if (detail) {
        if (lod.fraction < 0) lod.fraction = footprint;
        ++level;
    } else if (level >= texture_max_level) {
        lod.fraction = 1;
    }
    if (detail || sharpen) level = std::max(level, 0);
    else lod.fraction = std::max(lod.fraction, 0.0f);
    lod.first = uint8_t(std::clamp(level, 0, int(texture_max_level)));
    lod.second = uint8_t(std::clamp(level + 1, 0, int(texture_max_level)));
    return lod;
}

bool F3DEX2Interpreter::sample_tile(const Tile& tile, float s, float t, bool linear,
    uint8_t (&color)[4]) const {
    std::fill(color, color + 4, 0);
    auto shift_coordinate = [](float coordinate, uint32_t shift) {
        // RDP shifts 0..10 divide; 11..15 multiply by 2^(16-shift).
        return std::ldexp(coordinate, shift <= 10 ? -int(shift) : 16 - int(shift));
    };
    s = shift_coordinate(s, tile.shift_s) - float(tile.uls) - tile.uls_fraction / 4.0f;
    t = shift_coordinate(t, tile.shift_t) - float(tile.ult) - tile.ult_fraction / 4.0f;
    if (!std::isfinite(s) || !std::isfinite(t) ||
        std::abs(s) > 10000000.0f || std::abs(t) > 10000000.0f) return false;
    if (!linear) {
        return fetch_tile_texel(tile, int32_t(std::floor(s)), int32_t(std::floor(t)),
            color[0], color[1], color[2], color[3]);
    }

    // Native RDP filtering uses three samples from a 2x2 neighborhood, selected
    // by the diagonal. Coordinates already use texel units, not GPU UV centers.
    const int32_t x = int32_t(std::floor(s)), y = int32_t(std::floor(t));
    const float fs = std::floor((s - float(x))*32.0f)/32.0f;
    const float ft = std::floor((t - float(y))*32.0f)/32.0f;
    uint8_t samples[4][4]{};
    float weights[4]{};
    if (((other_mode_h>>12)&3)==3 && fs==0.5f && ft==0.5f) {
        std::fill(weights,weights+4,0.25f); // G_TF_AVERAGE center special case.
    } else if (fs+ft < 1.0f) {
        weights[0]=1-fs-ft; weights[1]=fs; weights[2]=ft;
    } else {
        weights[1]=1-ft; weights[2]=1-fs; weights[3]=fs+ft-1;
    }
    for (int i = 0; i < 4; ++i) {
        if (!fetch_tile_texel(tile, x + (i & 1), y + (i >> 1),
            samples[i][0], samples[i][1], samples[i][2], samples[i][3])) return false;
    }
    for (int channel = 0; channel < 4; ++channel) {
        float value = 0;
        for (int i = 0; i < 4; ++i) value += samples[i][channel] * weights[i];
        color[channel] = uint8_t(std::clamp(value, 0.0f, 255.0f));
    }
    return true;
}

uint8_t F3DEX2Interpreter::combine_colors(const uint8_t (&texel0)[4], const uint8_t (&texel1)[4],
    const uint8_t (&shade)[4], int32_t lod_fraction, uint8_t (&color)[4]) const {
    if (((other_mode_h >> 20) & 3) == 2) {
        std::copy(texel0, texel0 + 4, color);
        return color[3];
    }
    if (!combine_mode_set) {
        for (int i = 0; i < 4; ++i) color[i] = uint8_t(uint32_t(texel0[i]) * shade[i] / 255);
        return color[3];
    }
    // Evaluate the common N64 color-combiner equation:
    // (A - B) * C + D. Selector decoding follows RT64's
    // ColorCombiner mux layout; unsupported key/noise
    // sources resolve to zero until separately implemented.
    uint8_t combined[4] = {0, 0, 0, 0};
    const uint8_t* mux_texel0 = texel0;
    const uint8_t* mux_texel1 = texel1;
    const uint8_t primitive[4] = {
        (uint8_t)(prim_color >> 24), (uint8_t)(prim_color >> 16),
        (uint8_t)(prim_color >> 8), (uint8_t)prim_color
    };
    const uint8_t environment[4] = {
        (uint8_t)(env_color >> 24), (uint8_t)(env_color >> 16),
        (uint8_t)(env_color >> 8), (uint8_t)env_color
    };
    const uint32_t cycle_type = (other_mode_h >> 20) & 3;
    auto channel_from_color_mux = [&](uint32_t mux, uint32_t channel, uint32_t operand) -> int32_t {
        if (channel == 3) {
            switch (mux) {
            case 0: return combined[3];
            case 1: return mux_texel0[3];
            case 2: return mux_texel1[3];
            case 3: return primitive[3];
            case 4: return shade[3];
            case 5: return environment[3];
            case 6: return 255;
            default: return 0;
            }
        }
        switch (mux) {
        case 0: return combined[channel];
        case 1: return mux_texel0[channel];
        case 2: return mux_texel1[channel];
        case 3: return primitive[channel];
        case 4: return shade[channel];
        case 5: return environment[channel];
        case 6: return (operand == 0 || operand == 3) ? 255 : 0; // ONE for A/D; key inputs unsupported
        case 7: return operand == 2 ? combined[3] : 0;
        case 8: return operand == 2 ? mux_texel0[3] : 0;
        case 9: return operand == 2 ? mux_texel1[3] : 0;
        case 10: return operand == 2 ? primitive[3] : 0;
        case 11: return operand == 2 ? shade[3] : 0;
        case 12: return operand == 2 ? environment[3] : 0;
        case 13: return operand == 2 ? lod_fraction : 0;
        case 14: return operand == 2 ? primitive_lod_fraction : 0;
        default: return 0;
        }
    };
    auto channel_from_alpha_mux = [&](uint32_t mux, uint32_t operand) -> int32_t {
        switch (mux) {
        case 0: return operand == 2 ? lod_fraction : combined[3];
        case 1: return mux_texel0[3];
        case 2: return mux_texel1[3];
        case 3: return primitive[3];
        case 4: return shade[3];
        case 5: return environment[3];
        case 6: return operand == 2 ? primitive_lod_fraction : 255;
        default: return 0;
        }
    };
    uint8_t compare_alpha = 0;
    const uint32_t first_cycle = cycle_type == 1 ? 0 : 1;
    const uint32_t cycle_count = cycle_type == 1 ? 2 : 1;
    for (uint32_t cycle_offset = 0; cycle_offset < cycle_count; ++cycle_offset) {
        const uint32_t cycle = first_cycle + cycle_offset;
        const bool swap_textures = cycle_type == 1 && cycle == 1;
        mux_texel0 = swap_textures ? texel1 : texel0;
        mux_texel1 = swap_textures ? texel0 : texel1;
        const uint32_t mux_a = cycle ? ((combine_mode_w0 >> 5) & 0xF) : ((combine_mode_w0 >> 20) & 0xF);
        const uint32_t mux_b = cycle ? ((combine_mode_w1 >> 24) & 0xF) : ((combine_mode_w1 >> 28) & 0xF);
        const uint32_t mux_c = cycle ? (combine_mode_w0 & 0x1F) : ((combine_mode_w0 >> 15) & 0x1F);
        const uint32_t mux_d = cycle ? ((combine_mode_w1 >> 6) & 0x7) : ((combine_mode_w1 >> 15) & 0x7);
        const uint32_t alpha_a = cycle ? ((combine_mode_w1 >> 21) & 0x7) : ((combine_mode_w0 >> 12) & 0x7);
        const uint32_t alpha_b = cycle ? ((combine_mode_w1 >> 3) & 0x7) : ((combine_mode_w1 >> 12) & 0x7);
        const uint32_t alpha_c = cycle ? ((combine_mode_w1 >> 18) & 0x7) : ((combine_mode_w0 >> 9) & 0x7);
        const uint32_t alpha_d = cycle ? (combine_mode_w1 & 0x7) : ((combine_mode_w1 >> 9) & 0x7);
        for (uint32_t channel = 0; channel < 3; ++channel) {
            const int32_t a = channel_from_color_mux(mux_a, channel, 0);
            const int32_t b = channel_from_color_mux(mux_b, channel, 1);
            const int32_t c = channel_from_color_mux(mux_c, channel, 2);
            const int32_t d = channel_from_color_mux(mux_d, channel, 3);
            combined[channel] = (uint8_t)std::clamp(((a - b) * c + 127) / 255 + d, 0, 255);
        }
        const int32_t aa = channel_from_alpha_mux(alpha_a, 0);
        const int32_t ab = channel_from_alpha_mux(alpha_b, 1);
        const int32_t ac = channel_from_alpha_mux(alpha_c, 2);
        const int32_t ad = channel_from_alpha_mux(alpha_d, 3);
        combined[3] = (uint8_t)std::clamp(((aa - ab) * ac + 127) / 255 + ad, 0, 255);
        if (cycle_offset == 0) compare_alpha = combined[3];
    }
    std::copy(combined, combined + 4, color);
    return compare_alpha;
}

bool F3DEX2Interpreter::blend_colors(const uint8_t (&color)[4], uint8_t shade_alpha,
    uint8_t compare_alpha, uint16_t destination, int32_t x, int32_t y, uint16_t& pixel) const {
    const uint32_t alpha_compare = other_mode_l & 3;
    if (alpha_compare == 1 && compare_alpha < uint8_t(blend_color)) return false;
    if (alpha_compare == 3) {
        // Stable ordered approximation of alpha dithering; not the RDP noise generator.
        static constexpr uint8_t bayer[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
        if (compare_alpha < 1 + bayer[((y & 3)<<2)|(x & 3)]*16) return false;
    }
    const bool coverage_times_alpha = (other_mode_l & 0x1000) != 0;
    if (coverage_times_alpha && color[3] == 0) return false;
    // Without a subpixel coverage buffer, approximate full interior coverage.
    // ALPHA_CVG_SEL must not turn an opaque surface into source-alpha blending.
    const int alpha = (other_mode_l & 0x2000) && !coverage_times_alpha ? 255 : color[3];
    const uint8_t background[3] = {uint8_t(((destination>>11)&31)*255/31),
        uint8_t(((destination>>6)&31)*255/31), uint8_t(((destination>>1)&31)*255/31)};
    uint8_t result[3] = {color[0],color[1],color[2]};
    const uint32_t cycle_type = (other_mode_h >> 20) & 3;
    if (cycle_type < 2) {
        const uint32_t count = cycle_type == 1 ? 2 : 1;
        for (uint32_t cycle = 0; cycle < count; ++cycle) {
            const uint32_t p = (other_mode_l >> (cycle ? 28 : 30)) & 3;
            const uint32_t m = (other_mode_l >> (cycle ? 20 : 22)) & 3;
            const uint32_t a = (other_mode_l >> (cycle ? 24 : 26)) & 3;
            const uint32_t b = (other_mode_l >> (cycle ? 16 : 18)) & 3;
            auto input = [&](uint32_t source, int channel) -> int {
                switch (source) {
                case 0: return result[channel];
                case 1: return background[channel];
                case 2: return uint8_t(blend_color >> (24-channel*8));
                default: return uint8_t(fog_color >> (24-channel*8));
                }
            };
            const int weight_a = a == 0 ? alpha : a == 1 ? uint8_t(fog_color) : a == 2 ? shade_alpha : 0;
            const int weight_b = b == 0 ? 255-weight_a : b == 3 ? 0 : 255;
            for (int channel=0; channel<3; ++channel) {
                const int source_p = input(p,channel), source_m = input(m,channel);
                int value;
                if (cycle == count-1 && !(other_mode_l & 0x4000)) value = source_p;
                else if (a == 3) value = source_m;
                else if (b == 3) value = source_p;
                else {
                    const int denominator = std::max(1,weight_a+weight_b);
                    value = (source_p*weight_a + source_m*weight_b + denominator/2)/denominator;
                }
                result[channel] = uint8_t(std::clamp(value,0,255));
            }
        }
    }
    pixel = uint16_t(((result[0]>>3)<<11)|((result[1]>>3)<<6)|((result[2]>>3)<<1)|1);
    return true;
}

void F3DEX2Interpreter::rasterize_triangle_screen(const Vertex3D& v0, const Vertex3D& v1, const Vertex3D& v2) {
    if (color_image_size != 2 || !rdram) return;
    if (color_image_addr + color_image_width * 240 * 2 > 8 * 1024 * 1024) return;

    float min_x = std::min({ v0.screen_x, v1.screen_x, v2.screen_x });
    float max_x = std::max({ v0.screen_x, v1.screen_x, v2.screen_x });
    float min_y = std::min({ v0.screen_y, v1.screen_y, v2.screen_y });
    float max_y = std::max({ v0.screen_y, v1.screen_y, v2.screen_y });

    int32_t i_min_x = std::max((int32_t)std::floor(min_x), scissor_xh);
    int32_t i_max_x = std::min((int32_t)std::ceil(max_x), scissor_xl - 1);
    int32_t i_min_y = std::max((int32_t)std::floor(min_y), scissor_yh);
    int32_t i_max_y = std::min((int32_t)std::ceil(max_y), scissor_yl - 1);

    if (i_min_x > i_max_x || i_min_y > i_max_y) return;

    auto edge_func = [](float ax, float ay, float bx, float by, float px, float py) -> float {
        return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    };

    float area = edge_func(v0.screen_x, v0.screen_y, v1.screen_x, v1.screen_y, v2.screen_x, v2.screen_y);
    if (std::abs(area) < 0.0001f) return;

    constexpr uint32_t G_CULL_FRONT = 0x00000200 | 0x00001000;
    constexpr uint32_t G_CULL_BACK = 0x00000400 | 0x00002000;
    constexpr uint32_t G_ZBUFFER = 0x00000001;
    if ((geometry_mode & G_CULL_FRONT) && (geometry_mode & G_CULL_BACK)) { ++triangles_culled; return; }
    // edge_func has the opposite sign from the conventional 2D cross product.
    if ((geometry_mode & G_CULL_BACK) && area < 0.0f) { ++triangles_culled; return; }
    if ((geometry_mode & G_CULL_FRONT) && area > 0.0f) { ++triangles_culled; return; }

    float inv_area = 1.0f / area;

    uint8_t cur_tile = (texture_tile < 8) ? texture_tile : 0;
    if (combine_mode_set) {
        ++combine_cycle_triangles[(other_mode_h >> 20) & 3];
    }
    const bool two_cycles = ((other_mode_h >> 20) & 3) == 1;
    bool uses_second_texture = false;
    if (combine_mode_set && two_cycles) {
        for (uint32_t cycle = 0; cycle < 2; ++cycle) {
            const uint32_t color_selectors[4] = {
                cycle ? ((combine_mode_w0 >> 5) & 15) : ((combine_mode_w0 >> 20) & 15),
                cycle ? ((combine_mode_w1 >> 24) & 15) : ((combine_mode_w1 >> 28) & 15),
                cycle ? (combine_mode_w0 & 31) : ((combine_mode_w0 >> 15) & 31),
                cycle ? ((combine_mode_w1 >> 6) & 7) : ((combine_mode_w1 >> 15) & 7)
            };
            const uint32_t alpha_selectors[4] = {
                cycle ? ((combine_mode_w1 >> 21) & 7) : ((combine_mode_w0 >> 12) & 7),
                cycle ? ((combine_mode_w1 >> 3) & 7) : ((combine_mode_w1 >> 12) & 7),
                cycle ? ((combine_mode_w1 >> 18) & 7) : ((combine_mode_w0 >> 9) & 7),
                cycle ? (combine_mode_w1 & 7) : ((combine_mode_w1 >> 9) & 7)
            };
            const uint32_t texture_selector = cycle ? 1 : 2;
            for (uint32_t operand = 0; operand < 4; ++operand) {
                uses_second_texture |= color_selectors[operand] == texture_selector;
                uses_second_texture |= operand == 2 && color_selectors[operand] == texture_selector + 7;
                uses_second_texture |= alpha_selectors[operand] == texture_selector;
            }
        }
    }
    // With LOD enabled and no mip levels, both inputs read the base tile.
    // Sampling the adjacent stale tile here corrupts single-texture HUD materials.
    const uint8_t second_tile = (cur_tile + ((other_mode_h & (1u << 16)) ?
        std::min<uint8_t>(texture_max_level, 1) : 1)) & 7;
    uses_second_texture &= second_tile != cur_tile;
    if (uses_second_texture) ++second_texture_triangles;
    Tile tile = tiles[cur_tile];
    int32_t tw = (tile.mask_s > 0) ? (1 << tile.mask_s) : ((int32_t)(tile.lrs - tile.uls) + 1);
    int32_t th = (tile.mask_t > 0) ? (1 << tile.mask_t) : ((int32_t)(tile.lrt - tile.ult) + 1);
    if (tw <= 0 || th <= 0 || tw > 256 || th > 256) {
        Tile& t0 = tiles[0];
        int32_t t0w = (t0.mask_s > 0) ? (1 << t0.mask_s) : ((int32_t)(t0.lrs - t0.uls) + 1);
        int32_t t0h = (t0.mask_t > 0) ? (1 << t0.mask_t) : ((int32_t)(t0.lrt - t0.ult) + 1);
        if (t0w > 0 && t0h > 0 && t0w <= 256 && t0h <= 256) {
            tile = t0;
            tw = t0w;
            th = t0h;
        }
    }
    bool has_texture = texture_enabled && (tw > 0 && th > 0 && tw <= 256 && th <= 256);
    if (has_texture) {
        fmt_counts[tile.format & 7][tile.size & 3]++;
    } else {
        untextured_triangles++;
    }
    const bool perspective = (other_mode_h & (1u << 19)) != 0;
    float inv_w0 = perspective ? 1.0f / (std::abs(v0.clip_w) > 0.0001f ? std::abs(v0.clip_w) : 1.0f) : 1.0f;
    float inv_w1 = perspective ? 1.0f / (std::abs(v1.clip_w) > 0.0001f ? std::abs(v1.clip_w) : 1.0f) : 1.0f;
    float inv_w2 = perspective ? 1.0f / (std::abs(v2.clip_w) > 0.0001f ? std::abs(v2.clip_w) : 1.0f) : 1.0f;
    // Derivatives of the perspective numerator/denominator in screen pixels.
    const float dx[3] = {(v2.screen_y-v1.screen_y)*inv_w0,
        (v0.screen_y-v2.screen_y)*inv_w1, (v1.screen_y-v0.screen_y)*inv_w2};
    const float dy[3] = {(v1.screen_x-v2.screen_x)*inv_w0,
        (v2.screen_x-v0.screen_x)*inv_w1, (v0.screen_x-v1.screen_x)*inv_w2};
    const float wx = dx[0]+dx[1]+dx[2], wy = dy[0]+dy[1]+dy[2];
    const float ux = dx[0]*v0.u+dx[1]*v1.u+dx[2]*v2.u;
    const float uy = dy[0]*v0.u+dy[1]*v1.u+dy[2]*v2.u;
    const float vx = dx[0]*v0.v+dx[1]*v1.v+dx[2]*v2.v;
    const float vy = dy[0]*v0.v+dy[1]*v1.v+dy[2]*v2.v;

    bool has_depth = (geometry_mode & G_ZBUFFER) && depth_image_addr != 0 &&
        depth_image_addr + color_image_width * 240 * 2 <= 8 * 1024 * 1024;

    for (int32_t py = i_min_y; py <= i_max_y; ++py) {
        uint16_t* row = reinterpret_cast<uint16_t*>(rdram + color_image_addr + (py * color_image_width) * 2);
        uint16_t* depth_row = has_depth ? reinterpret_cast<uint16_t*>(rdram + depth_image_addr + (py * color_image_width) * 2) : nullptr;
        float p_y = (float)py + 0.5f;

        for (int32_t px = i_min_x; px <= i_max_x; ++px) {
            float p_x = (float)px + 0.5f;

            float w0 = edge_func(v1.screen_x, v1.screen_y, v2.screen_x, v2.screen_y, p_x, p_y);
            float w1 = edge_func(v2.screen_x, v2.screen_y, v0.screen_x, v0.screen_y, p_x, p_y);
            float w2 = edge_func(v0.screen_x, v0.screen_y, v1.screen_x, v1.screen_y, p_x, p_y);

            if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0)) {

                float l0 = w0 * inv_area;
                float l1 = w1 * inv_area;
                float l2 = w2 * inv_area;

                uint16_t z_val = 0;
                if (has_depth) {
                    float cur_z_f = l0 * v0.screen_z + l1 * v1.screen_z + l2 * v2.screen_z;
                    z_val = (other_mode_l & 4) ? (primitive_depth & 0x7FFF) :
                        (uint16_t)std::clamp((int)(cur_z_f * 32.0f), 0, 0xFFFC);
                    if ((other_mode_l & 0x10) && z_val > depth_row[px]) {
                        ++pixels_depth_rejected;
                        continue; // Occluded by closer geometry
                    }
                }

                // Gouraud shading: interpolate vertex colors and alpha
                uint8_t vr = (uint8_t)std::clamp((int)(l0 * v0.r + l1 * v1.r + l2 * v2.r), 0, 255);
                uint8_t vg = (uint8_t)std::clamp((int)(l0 * v0.g + l1 * v1.g + l2 * v2.g), 0, 255);
                uint8_t vb = (uint8_t)std::clamp((int)(l0 * v0.b + l1 * v1.b + l2 * v2.b), 0, 255);
                uint8_t va = (uint8_t)std::clamp((int)(l0 * v0.a + l1 * v1.a + l2 * v2.a), 0, 255);

                bool texel_valid = false;
                uint8_t tr = 255, tg = 255, tb = 255;
                uint8_t ta = 255;
                uint8_t second_texel[4] = {255, 255, 255, 255};
                int32_t lod_fraction = 255;
                float tu = 0.0f, tv = 0.0f;

                if (has_texture) {
                    // Perspective-correct texture coordinate interpolation
                    float w0_p = w0 * inv_w0;
                    float w1_p = w1 * inv_w1;
                    float w2_p = w2 * inv_w2;
                    float sum_p = w0_p + w1_p + w2_p;
                    float inv_sum = (std::abs(sum_p) > 1e-7f) ? (1.0f / sum_p) : 1.0f;
                    tu = (w0_p * v0.u + w1_p * v1.u + w2_p * v2.u) * inv_sum;
                    tv = (w0_p * v0.v + w1_p * v1.v + w2_p * v2.v) * inv_sum;

                    uint8_t sampled[4]{};
                    const bool linear = ((other_mode_h >> 12) & 3) >= 2 && ((other_mode_h >> 20) & 3) != 2;
                    TextureLOD lod;
                    const float footprint = std::max({std::abs((ux-tu*wx)*inv_sum),
                        std::abs((uy-tu*wy)*inv_sum), std::abs((vx-tv*wx)*inv_sum),
                        std::abs((vy-tv*wy)*inv_sum)});
                    lod = select_texture_lod(footprint);
                    lod_fraction = int32_t(lod.fraction * 255.0f);
                    const Tile& first = lod.first ? tiles[(cur_tile + lod.first) & 7] : tile;
                    texel_valid = sample_tile(first, tu, tv, linear, sampled);
                    tr = sampled[0]; tg = sampled[1]; tb = sampled[2]; ta = sampled[3];
                    if (uses_second_texture) {
                        sample_tile(tiles[(cur_tile + lod.second) & 7], tu, tv, linear, second_texel);
                    } else {
                        std::copy(sampled, sampled + 4, second_texel);
                    }
                }

                if (has_texture && !texel_valid) {
                    ++pixels_texel_invalid;
                    continue;
                }
                const uint8_t texel0[4] = {tr, tg, tb, ta};
                const uint8_t shade[4] = {vr, vg, vb, va};
                uint8_t color[4]{};
                const uint8_t compare_alpha = combine_colors(texel0, second_texel, shade, lod_fraction, color);
                uint16_t pixel_col = 0;
                if (!blend_colors(color, va, compare_alpha, row[px], px, py, pixel_col)) continue;
                if (has_depth && (other_mode_l & 0x20)) depth_row[px] = z_val;
                row[px] = pixel_col;
                ++pixels_written;
            }
        }
    }
    triangles_drawn++;
}


uint32_t F3DEX2Interpreter::resolve_segmented_address(uint32_t addr) const {
    uint8_t seg = (addr >> 24) & 0x0F;
    uint32_t offset = addr & 0x00FFFFFF;
    uint32_t base = segments[seg];
    return ((base + offset) & 0x01FFFFFF);
}

bool F3DEX2Interpreter::load_microcode(uint32_t data_address, uint32_t data_size) {
    // Microcode addresses are physical/KSEG addresses, not segment-table entries.
    const uint32_t address = data_address & 0x1FFFFFFF;
    microcode = Microcode::Unknown;
    auto unsupported = [&] {
        if (microcode_load_failures++ == 0)
            std::cout << "[RSP] Unsupported microcode data at 0x" << std::hex << address
                      << " (size 0x" << data_size << ")" << std::dec << '\n';
        return false;
    };
    if (!rdram || !data_size || data_size > 4096 ||
        uint64_t(address) + data_size > 8 * 1024 * 1024) return unsupported();
    std::string data(data_size, '\0');
    for (uint32_t i = 0; i < data_size; ++i) data[i] = char(f3d_read_u8(rdram, address+i));
    const size_t signature = data.find("RSP Gfx ucode ");
    if (signature == std::string::npos) return unsupported();
    const std::string version = data.substr(signature, data.find('\0', signature)-signature);
    // The game's 2.05 identifiers say F3DEX/L3DEX without the trailing "2".
    if (version.find("fifo 2.") == std::string::npos) return unsupported();
    if (version.starts_with("RSP Gfx ucode F3DEX")) microcode = Microcode::F3DEX2;
    else if (version.starts_with("RSP Gfx ucode L3DEX")) microcode = Microcode::L3DEX2;
    if (microcode == Microcode::Unknown) return unsupported();
    return true;
}

void F3DEX2Interpreter::rasterize_line(const Vertex3D& v0, const Vertex3D& v1,
    int8_t width, const Vertex3D& flat_color) {
    const float thickness = 1.5f + float(width)*0.5f;
    if (!rdram || color_image_size != 2 || !color_image_width || thickness <= 0) return;
    Vertex3D a = v0, b = v1;
    if (!a.is_screen_space && !b.is_screen_space) {
        // Clip the center segment before the perspective divide, including lines
        // crossing the near plane. A clipped endpoint alone must not drop a line.
        auto distance = [](const Vertex3D& v, int plane) {
            switch (plane) {
            case 0: return v.clip_w + v.clip_x;
            case 1: return v.clip_w - v.clip_x;
            case 2: return v.clip_w + v.clip_y;
            case 3: return v.clip_w - v.clip_y;
            case 4: return v.clip_w + v.clip_z;
            default: return v.clip_w - v.clip_z;
            }
        };
        auto interpolate = [](const Vertex3D& x, const Vertex3D& y, float t) {
            Vertex3D v = x;
            auto mix = [t](float p, float q) { return p+(q-p)*t; };
            v.clip_x = mix(x.clip_x,y.clip_x); v.clip_y = mix(x.clip_y,y.clip_y);
            v.clip_z = mix(x.clip_z,y.clip_z); v.clip_w = mix(x.clip_w,y.clip_w);
            v.r = uint8_t(std::clamp(std::lround(mix(x.r,y.r)),0l,255l));
            v.g = uint8_t(std::clamp(std::lround(mix(x.g,y.g)),0l,255l));
            v.b = uint8_t(std::clamp(std::lround(mix(x.b,y.b)),0l,255l));
            v.a = uint8_t(std::clamp(std::lround(mix(x.a,y.a)),0l,255l));
            return v;
        };
        for (int plane=0; plane<6; ++plane) {
            const float da = distance(a,plane), db = distance(b,plane);
            if (!std::isfinite(da) || !std::isfinite(db) || (da<0 && db<0)) return;
            if ((da<0) != (db<0)) {
                Vertex3D intersection = interpolate(a,b,da/(da-db));
                if (da<0) a=intersection; else b=intersection;
            }
        }
        for (Vertex3D* v : {&a,&b}) {
            if (v->clip_w <= 1e-7f) return;
            v->screen_x = v->clip_x/v->clip_w*viewport_scale[0]/4.0f+viewport_trans[0]/4.0f;
            v->screen_y = -v->clip_y/v->clip_w*viewport_scale[1]/4.0f+viewport_trans[1]/4.0f;
            v->screen_z = v->clip_z/v->clip_w*viewport_scale[2]+viewport_trans[2];
        }
    }
    const float dx = b.screen_x-a.screen_x, dy = b.screen_y-a.screen_y;
    const float length_squared = dx*dx+dy*dy;
    if (!std::isfinite(length_squared) || length_squared < 1e-8f ||
        !std::isfinite(a.screen_z) || !std::isfinite(b.screen_z)) return;
    const float length = std::sqrt(length_squared), radius = thickness*0.5f;
    const uint64_t available_pixels = (8*1024*1024-std::min(color_image_addr,8u*1024*1024))/2;
    const int height = int(available_pixels/color_image_width);
    const float left = std::max({0.0f,float(scissor_xh),std::min(a.screen_x,b.screen_x)-radius});
    const float right = std::min({float(color_image_width),float(scissor_xl),std::max(a.screen_x,b.screen_x)+radius});
    const float top = std::max({0.0f,float(scissor_yh),std::min(a.screen_y,b.screen_y)-radius});
    const float bottom = std::min({float(height),float(scissor_yl),std::max(a.screen_y,b.screen_y)+radius});
    if (left>=right || top>=bottom) return;
    ++lines_drawn;
    const bool has_depth = (geometry_mode&1) && depth_image_addr &&
        uint64_t(depth_image_addr)+uint64_t(color_image_width)*int(std::ceil(bottom))*2 <= 8*1024*1024;
    const uint8_t white[4] = {255,255,255,255};
    for (int y=int(std::floor(top)); y<int(std::ceil(bottom)); ++y) {
        auto* row = reinterpret_cast<uint16_t*>(rdram+color_image_addr+uint64_t(y)*color_image_width*2);
        auto* depth = has_depth ? reinterpret_cast<uint16_t*>(rdram+depth_image_addr+uint64_t(y)*color_image_width*2) : nullptr;
        for (int x=int(std::floor(left)); x<int(std::ceil(right)); ++x) {
            const float px=x+0.5f-a.screen_x, py=y+0.5f-a.screen_y;
            const float t=(px*dx+py*dy)/length_squared;
            // Butt caps; evaluate each pixel once, avoiding a blended quad seam.
            if (t<0 || t>=1 || std::abs(px*dy-py*dx)>=radius*length) continue;
            const float z = a.screen_z+(b.screen_z-a.screen_z)*t;
            const uint16_t z_value = (other_mode_l&4) ? (primitive_depth&0x7FFF) :
                uint16_t(std::clamp(z*32.0f,0.0f,65532.0f));
            if (depth && (other_mode_l&0x10) && z_value>depth[x]) {
                ++pixels_depth_rejected; continue;
            }
            uint8_t shade[4]{};
            const uint8_t ca[4] = {a.r,a.g,a.b,a.a}, cb[4] = {b.r,b.g,b.b,b.a};
            const uint8_t flat[4] = {flat_color.r,flat_color.g,flat_color.b,flat_color.a};
            for (int channel=0; channel<4; ++channel) {
                shade[channel] = !(geometry_mode&4) ? uint8_t(prim_color>>(24-channel*8)) :
                    !(geometry_mode&0x200000) ? flat[channel] :
                    uint8_t(std::clamp(ca[channel]+(cb[channel]-ca[channel])*t,0.0f,255.0f));
            }
            uint8_t color[4]{};
            const uint8_t alpha = combine_colors(white,white,shade,255,color);
            uint16_t pixel;
            if (!blend_colors(color,shade[3],alpha,row[x],x,y,pixel)) continue;
            row[x]=pixel;
            if (depth && (other_mode_l&0x20)) depth[x]=z_value;
            ++pixels_written; ++line_pixels_written;
        }
    }
}

void F3DEX2Interpreter::process_display_list(uint32_t dl_addr, uint32_t ucode_data, uint32_t ucode_size) {
    if (!rdram) return;
    dl_stack_depth = 0;
    if (ucode_size) load_microcode(ucode_data, ucode_size);
    uint32_t pc = resolve_segmented_address(dl_addr);

    // Limit instruction count per display list to prevent infinite loops
    size_t instructions_executed = 0;
    constexpr size_t MAX_INSTRUCTIONS = 250000;

    while (instructions_executed++ < MAX_INSTRUCTIONS && pc + 8 <= 8 * 1024 * 1024) {
        uint32_t w0 = *reinterpret_cast<const uint32_t*>(rdram + pc);
        uint32_t w1 = *reinterpret_cast<const uint32_t*>(rdram + pc + 4);
        pc += 8;
        uint8_t op = (w0 >> 24) & 0xFF;
        opcode_counts[op]++;

        switch (op) {
            case 0xDD: { // G_LOAD_UCODE; E1 supplies the physical data address.
                ++microcode_loads;
                load_microcode(rdp_half_1,(w0&0xFFFF)+1);
                // LOAD_UCODE discards the old display-list return links and RSP mode.
                dl_stack_depth = modelview_stack_depth = 0;
                geometry_mode = 0;
                break;
            }
            case 0x08: { // L3DEX2 G_LINE3D: two doubled indices and signed width.
                const uint32_t a=(w0>>17)&0x7F, b=(w0>>9)&0x7F;
                if (microcode == Microcode::L3DEX2 && a<64 && b<64)
                    rasterize_line(vertex_buffer[a],vertex_buffer[b],int8_t(w0),vertex_buffer[a]);
                break;
            }
            case 0xE2: // G_SETOTHERMODE_L
            case 0xE3: { // G_SETOTHERMODE_H
                const uint32_t shift = (w0 >> 8) & 0xFF;
                const uint32_t length = (w0 & 0xFF) + 1;
                if (shift + length <= 32) {
                    // F3DEX2 encodes the right shift from bit 32, not the
                    // destination bit offset. Data is already field-aligned.
                    const uint32_t offset = 32 - shift - length;
                    const uint32_t field_mask = length == 32 ? 0xFFFFFFFFu : ((1u << length) - 1u);
                    const uint32_t mask = field_mask << offset;
                    uint32_t& mode = (op == 0xE2) ? other_mode_l : other_mode_h;
                    mode = (mode & ~mask) | (w1 & mask);
                }
                break;
            }
            case 0xE6: // G_RDPLOADSYNC
            case 0xE7: // G_RDPPIPESYNC
            case 0xE8: // G_RDPTILESYNC
            case 0xE9: // G_RDPFULLSYNC
                // Ordering barriers have no extra work in the synchronous software rasterizer.
                break;
            case 0xEE: // G_SET_PRIM_DEPTH
                primitive_depth = static_cast<uint16_t>(w1 >> 16);
                primitive_depth_delta = static_cast<uint16_t>(w1);
                break;
            case 0xFC: { // G_SETCOMBINE
                combine_mode_w0 = w0 & 0x00FFFFFF;
                combine_mode_w1 = w1;
                combine_mode_set = true;
                static uint32_t logged_combine_w0[8]{};
                static uint32_t logged_combine_w1[8]{};
                static uint32_t logged_combine_count = 0;
                bool already_logged = false;
                for (uint32_t i = 0; i < logged_combine_count; ++i) {
                    if (logged_combine_w0[i] == combine_mode_w0 && logged_combine_w1[i] == combine_mode_w1) {
                        already_logged = true;
                        break;
                    }
                }
                if (!already_logged && logged_combine_count < 8) {
                    std::cout << "[RDP Combine #" << combine_mode_updates << "] W0=0x" << std::hex
                              << combine_mode_w0 << " W1=0x" << combine_mode_w1
                              << " modeH=0x" << other_mode_h << std::dec << "\n";
                    logged_combine_w0[logged_combine_count] = combine_mode_w0;
                    logged_combine_w1[logged_combine_count] = combine_mode_w1;
                    ++logged_combine_count;
                }
                ++combine_mode_updates;
                break;
            }
            case 0xF9: // G_SETBLENDCOLOR
                blend_color = w1;
                break;
            case 0xF8: // G_SETFOGCOLOR
                fog_color = w1;
                break;
            case 0xDA: { // G_MTX
                uint8_t params = (w0) & 0xFF;
                uint32_t mtx_addr = resolve_segmented_address(w1);
                if (mtx_addr + 64 <= 8 * 1024 * 1024) {
                    int16_t int_part[16];
                    uint16_t frac_part[16];
                    for (int k = 0; k < 16; k++) {
                        int_part[k] = f3d_read_s16(rdram, mtx_addr + k * 2);
                        frac_part[k] = f3d_read_u16(rdram, mtx_addr + 32 + k * 2);
                    }
                    Matrix4x4 new_mtx = Matrix4x4::from_n64_fixed(int_part, frac_part);

                    bool is_proj = (params & 0x04) != 0;
                    bool is_load = (params & 0x02) != 0;
                    // F3DEX2 stores the PUSH bit inverted (see RT64's matrix handler).
                    bool is_push = (params & 0x01) == 0;

                    if (is_proj) {
                        if (is_push && proj_stack_depth < 4) proj_stack[proj_stack_depth++] = proj_mtx;
                        proj_mtx = is_load ? new_mtx : (new_mtx * proj_mtx);
                    } else {
                        if (is_push && modelview_stack_depth < MAX_MTX_STACK) modelview_stack[modelview_stack_depth++] = modelview_mtx;
                        modelview_mtx = is_load ? new_mtx : (new_mtx * modelview_mtx);
                    }
                    update_mvp();
                }
                break;
            }
            case 0xD8: { // G_POPMTX
                uint32_t count = (w1 >> 6);
                if (count == 0) count = 1;
                while (count-- > 0 && modelview_stack_depth > 0) {
                    modelview_mtx = modelview_stack[--modelview_stack_depth];
                }
                update_mvp();
                break;
            }
            case 0xDC: { // G_MOVEMEM
                uint8_t type = w0 & 0xFF;
                uint32_t mem_addr = resolve_segmented_address(w1);
                if (type == 10) ++light_commands;
                if (type == 8 && mem_addr + 16 <= 8 * 1024 * 1024) { // G_MV_VIEWPORT
                    viewport_scale[0] = f3d_read_s16(rdram, mem_addr + 0);
                    viewport_scale[1] = f3d_read_s16(rdram, mem_addr + 2);
                    viewport_scale[2] = f3d_read_s16(rdram, mem_addr + 4);
                    viewport_trans[0] = f3d_read_s16(rdram, mem_addr + 8);
                    viewport_trans[1] = f3d_read_s16(rdram, mem_addr + 10);
                    viewport_trans[2] = f3d_read_s16(rdram, mem_addr + 12);
                } else if (type == 10 && mem_addr + 12 <= 8 * 1024 * 1024) {
                    const uint32_t slot = (((w0 >> 8) & 0xFF) * 8) / 24;
                    float direction[3]{};
                    for (int channel = 0; channel < 3; ++channel)
                        direction[channel] = static_cast<int8_t>(f3d_read_u8(rdram, mem_addr + 8 + channel));
                    if (slot < 2) {
                        std::copy(direction, direction + 3, look_at[slot]);
                    } else if (slot - 2 < 8) {
                        auto& light = lights[slot - 2];
                        for (int channel = 0; channel < 3; ++channel) {
                            light.color[channel] = f3d_read_u8(rdram, mem_addr + channel);
                            light.direction[channel] = direction[channel];
                        }
                    }
                }
                break;
            }
            case 0x01: { // G_VTX
                uint32_t num_vtx = (w0 >> 12) & 0xFF;
                if (num_vtx == 0) num_vtx = (w0 >> 12) & 0xFFF;
                uint32_t end_idx = (w0 >> 1) & 0x7F;
                uint32_t dest_idx = (end_idx >= num_vtx) ? (end_idx - num_vtx) : 0;
                uint32_t vtx_addr = resolve_segmented_address(w1);

                for (uint32_t i = 0; i < num_vtx && (dest_idx + i) < 64; ++i) {
                    uint32_t addr = vtx_addr + i * 16;
                    if (addr + 16 <= 8 * 1024 * 1024) {
                        int16_t ox = f3d_read_s16(rdram, addr + 0);
                        int16_t oy = f3d_read_s16(rdram, addr + 2);
                        int16_t oz = f3d_read_s16(rdram, addr + 4);
                        int16_t tc_s = f3d_read_s16(rdram, addr + 8);
                        int16_t tc_t = f3d_read_s16(rdram, addr + 10);
                        uint8_t cr = f3d_read_u8(rdram, addr + 12);
                        uint8_t cg = f3d_read_u8(rdram, addr + 13);
                        uint8_t cb = f3d_read_u8(rdram, addr + 14);
                        uint8_t ca = f3d_read_u8(rdram, addr + 15);

                        Vertex3D& v = vertex_buffer[dest_idx + i];
                        if (geometry_mode & 0x00020000) ++lit_vertices;
                        v.x = (float)ox;
                        v.y = (float)oy;
                        v.z = (float)oz;
                        float sc_s = (texture_scale_s > 0) ? ((float)texture_scale_s / 65536.0f) : 1.0f;
                        float sc_t = (texture_scale_t > 0) ? ((float)texture_scale_t / 65536.0f) : 1.0f;
                        v.u = ((float)tc_s / 32.0f) * sc_s;
                        v.v = ((float)tc_t / 32.0f) * sc_t;
                        v.r = cr;
                        v.g = cg;
                        v.b = cb;
                        v.a = ca;
                        v.is_screen_space = false;

                        if (geometry_mode & (0x00020000 | 0x00040000))
                            light_and_texgen_vertex(v, static_cast<int8_t>(cr), static_cast<int8_t>(cg), static_cast<int8_t>(cb));

                        transform_vertex(v);
                    }
                }
                break;
            }
            case 0x02: { // G_MODIFYVTX
                uint8_t where = (w0 >> 16) & 0xFF;
                uint32_t vtx_idx = ((w0) & 0xFFFF) / 2;
                if (vtx_idx < 64) {
                    Vertex3D& v = vertex_buffer[vtx_idx];
                    if (where == 0x10) { // RGBA
                        v.r = (w1 >> 24) & 0xFF;
                        v.g = (w1 >> 16) & 0xFF;
                        v.b = (w1 >> 8) & 0xFF;
                        v.a = w1 & 0xFF;
                    } else if (where == 0x14) { // ST
                        v.u = (float)(int16_t)(w1 >> 16) / 32.0f;
                        v.v = (float)(int16_t)(w1 & 0xFFFF) / 32.0f;
                    } else if (where == 0x18) { // XYSCREEN
                        v.screen_x = (float)(int16_t)(w1 >> 16) / 4.0f;
                        v.screen_y = (float)(int16_t)(w1 & 0xFFFF) / 4.0f;
                        v.is_screen_space = true;
                        v.clipped = false;
                    } else if (where == 0x1C) { // ZSCREEN
                        v.screen_z = (float)(uint32_t)w1 / 65536.0f;
                    }
                }
                break;
            }
            case 0x05: { // G_TRI1
                uint32_t v0 = (w0 >> 17) & 0x7F;
                uint32_t v1 = (w0 >> 9) & 0x7F;
                uint32_t v2 = (w0 >> 1) & 0x7F;
                if (v0 < 64 && v1 < 64 && v2 < 64) {
                    if (microcode == Microcode::L3DEX2) {
                        const int8_t width = int8_t(w1>>24);
                        if (v0!=v1) rasterize_line(vertex_buffer[v0],vertex_buffer[v1],width,vertex_buffer[v0]);
                        if (v1!=v2) rasterize_line(vertex_buffer[v1],vertex_buffer[v2],width,vertex_buffer[v0]);
                        if (v2!=v0) rasterize_line(vertex_buffer[v2],vertex_buffer[v0],width,vertex_buffer[v0]);
                    } else if (microcode == Microcode::F3DEX2) {
                        rasterize_triangle(vertex_buffer[v0], vertex_buffer[v1], vertex_buffer[v2]);
                    }
                }
                break;
            }
            case 0x06: { // G_TRI2
                uint32_t v00 = (w0 >> 17) & 0x7F;
                uint32_t v01 = (w0 >> 9) & 0x7F;
                uint32_t v02 = (w0 >> 1) & 0x7F;
                uint32_t v10 = (w1 >> 17) & 0x7F;
                uint32_t v11 = (w1 >> 9) & 0x7F;
                uint32_t v12 = (w1 >> 1) & 0x7F;
                auto draw = [&](uint32_t a,uint32_t b,uint32_t c) {
                    if (a>=64 || b>=64 || c>=64) return;
                    if (microcode == Microcode::L3DEX2) {
                        const int8_t width = int8_t(w1>>24);
                        if (a!=b) rasterize_line(vertex_buffer[a],vertex_buffer[b],width,vertex_buffer[a]);
                        if (b!=c) rasterize_line(vertex_buffer[b],vertex_buffer[c],width,vertex_buffer[a]);
                        if (c!=a) rasterize_line(vertex_buffer[c],vertex_buffer[a],width,vertex_buffer[a]);
                    } else if (microcode == Microcode::F3DEX2) {
                        rasterize_triangle(vertex_buffer[a],vertex_buffer[b],vertex_buffer[c]);
                    }
                };
                draw(v00,v01,v02); draw(v10,v11,v12);
                break;
            }
            case 0xD9: { // G_GEOMETRYMODE
                geometry_mode = (geometry_mode & (w0 & 0x00FFFFFF)) | (w1 & 0x00FFFFFF);
                break;
            }
            case 0xD7: { // G_TEXTURE
                texture_tile = (w0 >> 8) & 0x7;
                texture_max_level = (w0 >> 11) & 0x7;
                texture_enabled = ((w0 >> 1) & 0x7F) != 0;
                texture_scale_s = (w1 >> 16) & 0xFFFF;
                texture_scale_t = w1 & 0xFFFF;
                break;
            }
            case 0xDB: { // G_MOVEWORD
                uint8_t type = (w0 >> 16) & 0xFF;
                uint16_t offset = w0 & 0xFFFF;
                if (type == 0x06) { // G_MW_SEGMENT
                    uint32_t seg_idx = (offset >> 2) & 0x0F;
                    segments[seg_idx] = w1;
                } else if (type == 0x02) { // G_MW_NUMLIGHT
                    light_count = std::min(w1 / 24, 7u);
                } else if (type == 0x0A && offset / 24 < 8) { // G_MW_LIGHTCOL
                    auto& light = lights[offset / 24];
                    light.color[0] = uint8_t(w1 >> 24);
                    light.color[1] = uint8_t(w1 >> 16);
                    light.color[2] = uint8_t(w1 >> 8);
                }
                break;
            }
            case 0xDE: { // G_DL
                uint8_t param = (w0 >> 16) & 0xFF;
                uint32_t child_addr = resolve_segmented_address(w1);
                if (param == 0) { // Push & call
                    if (dl_stack_depth < MAX_DL_STACK) {
                        dl_stack[dl_stack_depth++] = pc;
                        pc = child_addr;
                    }
                } else { // Branch
                    pc = child_addr;
                }
                break;
            }
            case 0xDF: { // G_ENDDL
                if (dl_stack_depth > 0) {
                    pc = dl_stack[--dl_stack_depth];
                } else {
                    return; // Completed root DL
                }
                break;
            }
            case 0xFF: { // G_SETCIMG
                color_image_format = (w0 >> 21) & 0x7;
                color_image_size = (w0 >> 19) & 0x3;
                color_image_width = (w0 & 0xFFF) + 1;
                color_image_addr = resolve_segmented_address(w1);
                break;
            }
            case 0xFE: { // G_SETZIMG
                depth_image_addr = resolve_segmented_address(w1);
                break;
            }
            case 0xF0: { // G_LOADTLUT
                uint8_t tile_idx = (w1 >> 24) & 0x7;
                uint32_t count = ((w1 >> 14) & 0x3FF) + 1;
                uint32_t tmem_dest = tiles[tile_idx].tmem_addr;
                tlut_tmem_addr = tmem_dest;
                uint32_t bytes_to_copy = count * 2;
                if (tmem_dest + bytes_to_copy <= sizeof(tmem) && texture_image_addr + bytes_to_copy <= 8 * 1024 * 1024) {
                    for (uint32_t i = 0; i < bytes_to_copy; i++) {
                        tmem[tmem_dest + i] = rdram[(texture_image_addr + i) ^ 3];
                    }
                }
                break;
            }
            case 0xED: { // G_SETSCISSOR
                scissor_xh = ((w0 >> 12) & 0xFFF) / 4;
                scissor_yh = (w0 & 0xFFF) / 4;
                scissor_xl = ((w1 >> 12) & 0xFFF) / 4;
                scissor_yl = (w1 & 0xFFF) / 4;
                break;
            }
            case 0xF7: { // G_SETFILLCOLOR
                fill_color = w1;
                break;
            }
            case 0xFB: { // G_SETENVCOLOR
                env_color = w1;
                break;
            }
            case 0xFA: { // G_SETPRIMCOLOR
                prim_color = w1;
                primitive_lod_fraction = uint8_t(w0);
                primitive_min_lod = uint8_t((w0 >> 8) & 31);
                break;
            }
            case 0xF6: { // G_FILLRECT
                int32_t xl = ((w1 >> 12) & 0xFFF) / 4;
                int32_t yl = (w1 & 0xFFF) / 4;
                int32_t xh = ((w0 >> 12) & 0xFFF) / 4;
                int32_t yh = (w0 & 0xFFF) / 4;

                xl = std::max(xl, scissor_xh);
                yl = std::max(yl, scissor_yh);
                xh = std::min(xh, scissor_xl);
                yh = std::min(yh, scissor_yl);

                if (color_image_size == 2 && color_image_addr + color_image_width * 240 * 2 <= 8 * 1024 * 1024) {
                    uint16_t col16 = (fill_color & 0xFFFF);
                    for (int32_t y = yl; y < yh; ++y) {
                        uint16_t* row = reinterpret_cast<uint16_t*>(rdram + color_image_addr + (y * color_image_width) * 2);
                        for (int32_t x = xl; x < xh; ++x) {
                            row[x] = col16;
                        }
                    }
                    rects_drawn++;
                }
                break;
            }
            case 0xFD: { // G_SETTIMG
                texture_format = (w0 >> 21) & 0x7;
                texture_size = (w0 >> 19) & 0x3;
                texture_width = (w0 & 0xFFF) + 1;
                texture_image_addr = resolve_segmented_address(w1);
                break;
            }
            case 0xF5: { // G_SETTILE
                uint8_t tile_idx = (w1 >> 24) & 0x7;
                Tile& t = tiles[tile_idx];
                t.format = (w0 >> 21) & 0x7;
                t.size = (w0 >> 19) & 0x3;
                t.line = (w0 >> 9) & 0x1FF;
                t.tmem_addr = (w0 & 0x1FF) * 8;
                t.palette = (w1 >> 20) & 0xF;
                t.clamp_t = (w1 >> 19) & 1;
                t.mirror_t = (w1 >> 18) & 1;
                t.mask_t = (w1 >> 14) & 0xF;
                t.shift_t = (w1 >> 10) & 0xF;
                t.clamp_s = (w1 >> 9) & 1;
                t.mirror_s = (w1 >> 8) & 1;
                t.mask_s = (w1 >> 4) & 0xF;
                t.shift_s = w1 & 0xF;
                break;
            }
            case 0xF2: { // G_SETTILESIZE
                uint8_t tile_idx = (w1 >> 24) & 0x7;
                Tile& t = tiles[tile_idx];
                t.uls = ((w0 >> 12) & 0xFFF) / 4;
                t.ult = (w0 & 0xFFF) / 4;
                t.uls_fraction = (w0 >> 12) & 3;
                t.ult_fraction = w0 & 3;
                t.lrs = ((w1 >> 12) & 0xFFF) / 4;
                t.lrt = (w1 & 0xFFF) / 4;
                break;
            }
            case 0xF3: { // G_LOADBLOCK
                uint8_t tile_idx = (w1 >> 24) & 0x7;
                const Tile& tile = tiles[tile_idx];
                const uint32_t uls = (w0 >> 12) & 0xFFF;
                const uint32_t ult = w0 & 0xFFF;
                const uint32_t lrs = (w1 >> 12) & 0xFFF;
                uint32_t dxt = w1 & 0xFFF;
                // RDP LOADBLOCK addresses its source using the raw 10.2 S/T
                // coordinates and transfers 64-bit words, not a linear byte
                // count. DXT advances rows in TMEM and swaps odd 32-bit halves.
                if (lrs < uls || texture_size > 3 || tile.size > 3) break;

                const uint32_t bytes_per_source_row = (texture_width << texture_size) >> 1;
                uint64_t source_addr = static_cast<uint64_t>(texture_image_addr)
                    + ((static_cast<uint64_t>(uls) << texture_size) >> 1)
                    + static_cast<uint64_t>(bytes_per_source_row) * ult;
                const uint32_t word_count = ((lrs - uls) >> (4 - tile.size)) + 1;
                const bool rgba32 = tile.size == 3 && tile.format == 0;
                const uint32_t tmem_mask = rgba32 ? 0x7FF : 0xFFF;
                const uint32_t tmem_stride = tile.line * 8;
                uint32_t tmem_addr = tile.tmem_addr & tmem_mask;
                uint32_t dxt_counter = 0;
                uint32_t tmem_xor_mask = 0;

                for (uint32_t word = 0; word < word_count; ++word) {
                    if (source_addr + 8 <= 8 * 1024 * 1024) {
                        if (rgba32) {
                            // RGBA32 stores color and alpha in separate TMEM banks.
                            static constexpr uint32_t src_offsets[8] = {0, 1, 4, 5, 2, 3, 6, 7};
                            for (uint32_t i = 0; i < 8; ++i) {
                                uint32_t dst = (tmem_addr + (i & 3)) ^ tmem_xor_mask;
                                if (i >= 4) dst |= 0x800;
                                tmem[dst] = rdram[(source_addr + src_offsets[i]) ^ 3];
                            }
                            tmem_addr = (tmem_addr + 4) & tmem_mask;
                        } else {
                            for (uint32_t i = 0; i < 8; ++i) {
                                tmem[((tmem_addr + i) & tmem_mask) ^ tmem_xor_mask] = rdram[(source_addr + i) ^ 3];
                            }
                            tmem_addr = (tmem_addr + 8) & tmem_mask;
                        }
                        source_addr += 8;
                    }
                    // DXT advances after the word has been transferred. Doing
                    // this before the copy swaps the last word of every row.
                    dxt_counter += dxt;
                    while (dxt_counter >= 0x800) {
                        tmem_addr = (tmem_addr + tmem_stride) & tmem_mask;
                        dxt_counter -= 0x800;
                        tmem_xor_mask ^= 4;
                    }
                }
                break;
            }
            case 0xE1: { // G_RDPHALF_1
                rdp_half_1 = w1;
                break;
            }
            case 0xF1: { // G_RDPHALF_2
                rdp_half_2 = w1;
                break;
            }
            case 0xE4: // G_TEXRECT
            case 0xE5: { // G_TEXRECTFLIP
                int32_t xh = ((w0 >> 12) & 0xFFF) / 4;
                int32_t yh = (w0 & 0xFFF) / 4;
                uint8_t tile_idx = (w1 >> 24) & 0x7;
                int32_t xl = ((w1 >> 12) & 0xFFF) / 4;
                int32_t yl = (w1 & 0xFFF) / 4;

                if (pc + 8 <= 8 * 1024 * 1024) {
                    uint32_t next_w0 = *reinterpret_cast<const uint32_t*>(rdram + pc);
                    if (((next_w0 >> 24) & 0xFF) == 0xE1) {
                        rdp_half_1 = *reinterpret_cast<const uint32_t*>(rdram + pc + 4);
                        pc += 8;
                    }
                }
                if (pc + 8 <= 8 * 1024 * 1024) {
                    uint32_t next_w0 = *reinterpret_cast<const uint32_t*>(rdram + pc);
                    if (((next_w0 >> 24) & 0xFF) == 0xF1 || ((next_w0 >> 24) & 0xFF) == 0xB3) {
                        rdp_half_2 = *reinterpret_cast<const uint32_t*>(rdram + pc + 4);
                        pc += 8;
                    }
                }

                int32_t s0 = (int16_t)((rdp_half_1 >> 16) & 0xFFFF);
                int32_t t0 = (int16_t)(rdp_half_1 & 0xFFFF);
                int32_t dsdx = (int16_t)((rdp_half_2 >> 16) & 0xFFFF);
                int32_t dtdy = (int16_t)(rdp_half_2 & 0xFFFF);

                if (xl > xh) std::swap(xl, xh);
                if (yl > yh) std::swap(yl, yh);

                const Tile& tile = tiles[tile_idx];
                const bool copy_cycle = ((other_mode_h >> 20) & 3) == 2;
                // Copy rectangles include the last pixel; one/two-cycle bounds
                // are exclusive. COPY alone divides the S derivative by four.
                const int32_t right = xh + (copy_cycle ? 1 : 0);
                const int32_t bottom = yh + (copy_cycle ? 1 : 0);
                const int32_t left = std::max(xl, scissor_xh);
                const int32_t top = std::max(yl, scissor_yh);
                const int32_t end_x = std::min(right, scissor_xl);
                const int32_t end_y = std::min(bottom, scissor_yl);
                const float step_s = dsdx / (copy_cycle ? 4096.0f : 1024.0f);
                const float step_t = dtdy / 1024.0f;
                const bool filtered = !copy_cycle && ((other_mode_h>>12)&3)>=2;
                const bool rect_depth = !copy_cycle && depth_image_addr &&
                    depth_image_addr + uint64_t(color_image_width)*std::max(0,end_y)*2 <= 8*1024*1024;
                const uint16_t rect_z = (other_mode_l & 4) ? primitive_depth & 0x7FFF : 0;
                if (color_image_size == 2 && color_image_addr + uint64_t(color_image_width)*240*2 <= 8*1024*1024) {
                    for (int32_t y = top; y < end_y; ++y) {
                        auto* row = reinterpret_cast<uint16_t*>(rdram + color_image_addr + y*color_image_width*2);
                        auto* depth = rect_depth ? reinterpret_cast<uint16_t*>(rdram + depth_image_addr + y*color_image_width*2) : nullptr;
                        for (int32_t x = left; x < end_x; ++x) {
                            if (depth && (other_mode_l & 0x10) && rect_z > depth[x]) continue;
                            const float u = s0 / 32.0f + (op == 0xE5 ? y-yl : x-xl)*step_s;
                            const float v = t0 / 32.0f + (op == 0xE5 ? x-xl : y-yl)*step_t;
                            uint8_t color[4]{};
                            uint8_t texel[4]{}, next_texel[4]{};
                            if (!sample_tile(tile, u, v, filtered, texel)) continue;
                            std::copy(texel, texel+4, next_texel);
                            if (((other_mode_h>>20)&3)==1 && !(other_mode_h&(1u<<16)))
                                sample_tile(tiles[(tile_idx+1)&7], u, v, filtered, next_texel);
                            const uint8_t shade_value = combine_mode_set ? 0 : 255;
                            const uint8_t shade[4] = {shade_value,shade_value,shade_value,shade_value};
                            const uint8_t compare_alpha = combine_colors(texel, next_texel, shade, 255, color);
                            uint16_t pixel = 0;
                            if (blend_colors(color, shade[3], compare_alpha, row[x], x, y, pixel)) {
                                row[x] = pixel;
                                if (depth && (other_mode_l & 0x20)) depth[x] = rect_z;
                            }
                        }
                    }
                }
                rects_drawn++;
                break;
            }
            default: {
                static bool logged_unknown[256]{};
                if (!logged_unknown[op]) {
                    logged_unknown[op] = true;
                    std::cout << "[F3DEX2 Unknown Op] 0x" << std::hex << (int)op << " at PC=0x" << (pc - 8)
                              << " (w0=0x" << w0 << " w1=0x" << w1 << ")" << std::dec << "\n";
                }
                break;
            }
        }
    }
    if (instructions_executed >= MAX_INSTRUCTIONS) {
        std::cout << "[F3DEX2 WARNING] Hit MAX_INSTRUCTIONS (" << MAX_INSTRUCTIONS << ") on DL 0x" << std::hex << dl_addr << std::dec << "!\n";
    }
}

#include "f3dex2.hpp"
#include "rom_loader.hpp"
#include "vi_scanout.hpp"
#include <ultramodern/ultramodern.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void put16(uint8_t* ram, uint32_t address, uint16_t value) {
    std::memcpy(ram + (address ^ 2), &value, 2);
}

static void commands(F3DEX2Interpreter& r,
    std::initializer_list<std::pair<uint32_t, uint32_t>> commands) {
    uint32_t address = 0x100;
    for (auto [w0, w1] : commands) {
        std::memcpy(r.rdram + address, &w0, 4);
        std::memcpy(r.rdram + address + 4, &w1, 4);
        address += 8;
    }
    uint32_t end = 0xDF000000, zero = 0;
    std::memcpy(r.rdram + address, &end, 4);
    std::memcpy(r.rdram + address + 4, &zero, 4);
    r.process_display_list(0x100);
}

static void check_texture_rows(uint8_t* ram) {
    F3DEX2Interpreter r(ram);
    const std::array<uint16_t, 8> colors = {
        0xF801, 0x07C1, 0x003F, 0xFFFF, 0xFFC1, 0x07FF, 0xF83F, 0x8421
    };
    for (size_t i = 0; i < colors.size(); ++i) put16(ram, 0x1000 + i * 2, colors[i]);
    commands(r, {{0xFD100003, 0x1000}, {0xF5100000, 0x07000000},
                 {0xF3000000, 0x07007800}});
    // The first line remains in source order; the second has its 32-bit halves swapped.
    require(r.tmem[0] == 0xF8 && r.tmem[1] == 1, "LOADBLOCK swaps the first row too early");
    require(r.tmem[8] == 0xF8 && r.tmem[9] == 0x3F, "LOADBLOCK does not swap the odd row");
    auto& tile = r.tiles[0];
    tile.format = 0; tile.size = 2; tile.line = 1;
    tile.lrs = 3; tile.lrt = 1; tile.clamp_s = tile.clamp_t = 1;
    r.color_image_addr = 0x20000;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) {
        Vertex3D a{}, b{}, c{};
        a.is_screen_space = b.is_screen_space = c.is_screen_space = true;
        a.screen_x = 4; a.screen_y = 4;
        b.screen_x = 12; b.screen_y = 4;
        c.screen_x = 4; c.screen_y = 12;
        a.u = b.u = c.u = float(x);
        a.v = b.v = c.v = float(y);
        r.rasterize_triangle(a, b, c);
        uint16_t pixel = 0;
        std::memcpy(&pixel, ram + r.color_image_addr + (5 * 320 + 5) * 2, 2);
        require(pixel == colors[y * 4 + x], "Texture draw does not restore odd-row TMEM order");
    }
    commands(r, {{0xE4020018, 0x00010010}, {0xE1000000, 0}, {0xF1000000, 0x04000400}});
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) {
        uint16_t pixel = 0;
        std::memcpy(&pixel, ram + r.color_image_addr + ((4+y)*320+4+x)*2, 2);
        require(pixel == colors[y*4+x], "Texture rectangle does not undo odd-row TMEM swizzling");
    }
    commands(r, {{0xE4018014, 0x00010010}, {0xE1000000, 0}, {0xF1000000, 0x08000400}});
    uint16_t pixel = 0;
    std::memcpy(&pixel, ram + r.color_image_addr + (4*320+5)*2, 2);
    require(pixel == colors[2], "One-cycle rectangle incorrectly divides a 2x S derivative by four");
    commands(r, {{0xE5018018, 0x00010010}, {0xE1000000, 0}, {0xF1000000, 0x04000400}});
    std::memcpy(&pixel, ram + r.color_image_addr + (5*320+4)*2, 2);
    require(pixel == colors[1], "TEXRECTFLIP does not exchange the S/T screen axes");
    r.other_mode_h = 2u << 20;
    for (int y = 4; y < 6; ++y) std::memset(ram+r.color_image_addr+(y*320+4)*2, 0, 8);
    commands(r, {{0xE401C014, 0x00010010}, {0xE1000000, 0}, {0xF1000000, 0x10000400}});
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) {
        std::memcpy(&pixel, ram+r.color_image_addr+((4+y)*320+4+x)*2, 2);
        require(pixel == colors[y*4+x], "COPY rectangle bounds or S derivative are incorrect");
    }
}

static void check_matrix_and_viewport(uint8_t* ram) {
    F3DEX2Interpreter r(ram);
    for (int i = 0; i < 16; ++i) {
        put16(ram, 0x4000 + i * 2, (i % 5 == 0) ? 1 : 0);
        put16(ram, 0x4020 + i * 2, 0);
        put16(ram, 0x4100 + i * 2, (i % 5 == 0) ? 1 : ((i == 12) ? 3 : 0));
        put16(ram, 0x4120 + i * 2, 0);
    }
    commands(r, {{0xDA380003, 0x4000}}); // LOAD, NOPUSH
    require(r.modelview_stack_depth == 0, "F3DEX2 NOPUSH pushes a matrix");
    commands(r, {{0xDA380002, 0x4100}}); // LOAD, PUSH
    require(r.modelview_stack_depth == 1 && r.modelview_mtx.m[3][0] == 3,
        "F3DEX2 PUSH does not save and load the modelview matrix");
    commands(r, {{0xD8380002, 64}});
    require(r.modelview_stack_depth == 0 && r.modelview_mtx.m[3][0] == 0,
        "POPMTX does not restore the previous transform");
    commands(r, {{0xDC08060A, 0x5000}}); // 16-byte light, not viewport
    require(r.viewport_scale[0] == 640 && r.viewport_scale[1] == 480,
        "MOVEMEM light length is mistaken for a viewport index");
}

static void check_tile_sampling(uint8_t* ram) {
    F3DEX2Interpreter r(ram);
    auto& tile = r.tiles[0];
    tile.format = 0; tile.size = 2; tile.line = 1;
    tile.lrs = 3; tile.lrt = 0; tile.clamp_s = tile.clamp_t = 1;
    const uint16_t colors[] = {0xF801, 0x07C1, 0x003F, 0xFFFF};
    for (int i = 0; i < 4; ++i) {
        r.tmem[i * 2] = uint8_t(colors[i] >> 8);
        r.tmem[i * 2 + 1] = uint8_t(colors[i]);
    }
    uint8_t color[4]{};
    require(r.sample_tile(tile, 1, 0, false, color) && color[1] == 255 && color[0] == 0,
        "Point filtering blends adjacent texels");
    require(r.sample_tile(tile, 1, 0.5f, true, color) && color[0] == 127 && color[1] == 127,
        "Linear filtering does not interpolate separate texels");
    tile.shift_s = 1;
    require(r.sample_tile(tile, 4, 0, false, color) && color[2] == 255 && color[0] == 0,
        "Tile coordinate right shift is ignored");
    tile.shift_s = 15;
    require(r.sample_tile(tile, 1, 0, false, color) && color[2] == 255 && color[0] == 0,
        "Tile coordinate left shift is ignored");
    tile.shift_s = 0; tile.uls = 4; tile.lrs = 7; tile.ult = tile.lrt = 2;
    require(r.sample_tile(tile, 5, 2, false, color) && color[1] == 255 && color[0] == 0,
        "Tile upper-left origin is ignored");
    tile.uls = tile.ult = tile.lrt = 0; tile.lrs = 3;
    tile.clamp_s = 0; tile.mask_s = 2; tile.mirror_s = 1;
    require(r.sample_tile(tile, -1, 0, false, color) && color[0] == 255 && color[1] == 0,
        "Negative mirrored texture coordinate wraps incorrectly");
}

static void check_second_texture(uint8_t* ram) {
    F3DEX2Interpreter r(ram);
    r.color_image_addr = 0x20000;
    r.other_mode_h = 1 << 20; // Two-cycle mode, point sampling.
    r.combine_mode_set = true;
    for (int i = 0; i < 2; ++i) {
        auto& tile = r.tiles[i];
        tile.format = 0; tile.size = 2; tile.line = 1;
        tile.clamp_s = tile.clamp_t = 1;
        tile.tmem_addr = i * 32;
    }
    r.tmem[0] = 0xF8; r.tmem[1] = 1; // Tile 0: red, opaque.
    r.tmem[32] = 0x07; r.tmem[33] = 0xC1; // Tile 1: green, opaque.
    Vertex3D a{}, b{}, c{};
    a.screen_x = 4; a.screen_y = 4;
    b.screen_x = 12; b.screen_y = 4;
    c.screen_x = 4; c.screen_y = 12;
    auto draw = [&](uint32_t d0, uint32_t d1, uint32_t alpha_d1) {
        // Both cycles: (ZERO-ZERO)*ZERO + D; independent alpha selectors.
        r.combine_mode_w0 = (8u << 20) | (31u << 15) | (7u << 12) | (7u << 9) | (8u << 5) | 31u;
        r.combine_mode_w1 = (8u << 28) | (8u << 24) | (7u << 21) | (7u << 18) |
            (d0 << 15) | (7u << 12) | (6u << 9) | (d1 << 6) | (7u << 3) | alpha_d1;
        uint16_t pixel = 0;
        auto* destination = ram + r.color_image_addr + (5 * 320 + 5) * 2;
        std::memcpy(destination, &pixel, 2);
        r.rasterize_triangle_screen(a, b, c);
        std::memcpy(&pixel, destination, 2);
        return pixel;
    };
    require(draw(1, 1, 6) == 0x07C1, "Second-cycle TEXEL0 does not select physical tile 1");
    require(draw(1, 2, 6) == 0xF801, "Second-cycle TEXEL1 does not select physical tile 0");
    require(draw(2, 0, 6) == 0x07C1, "First-cycle TEXEL1 does not reach COMBINED");
    require(r.second_texture_triangles == 2, "Independent texture-use diagnostics are wrong");
    r.tmem[33] = 0xC0; // Tile 1 transparent: test alpha swapping separately.
    require(draw(1, 3, 1) == 0, "Second texture alpha is aliased to tile 0");
    r.other_mode_h |= 1u << 16; // LOD on, maximum mip level zero.
    require(draw(1, 1, 1) == 0xF801, "Zero-level LOD samples a stale adjacent HUD tile");
    commands(r, {{0xD7001802, 0xFFFFFFFF}});
    require(r.texture_max_level == 3 && r.texture_enabled, "G_TEXTURE mip level is decoded incorrectly");
    commands(r, {{0xD7000001, 0xFFFFFFFF}});
    require(!r.texture_enabled, "G_TEXTURE reserved bit enables texturing");
    r.texture_enabled = true;
    r.other_mode_h = 0; // One-cycle approximation aliases both texture muxes to tile 0.
    require(draw(2, 2, 6) == 0xF801, "One-cycle TEXEL1 incorrectly uses physical tile 1");
    r.tiles[2] = r.tiles[0]; r.tiles[2].tmem_addr = 64;
    r.tmem[64] = 0; r.tmem[65] = 0x3F; // Tile 2: blue.
    r.texture_max_level = 2;
    r.other_mode_h = (1u << 20) | (1u << 16);
    b.u = 64; // Eight texels per screen pixel: clamp to the last mip level.
    require(draw(1, 0, 6) == 0x003F, "Triangle UV derivatives do not select the last mip tile");
    b.u = 24; r.tmem[33] = 0xC1; // Footprint 3 -> levels 1/2, fraction 0.5.
    draw(1, 0, 6);
    r.combine_mode_w0 = (r.combine_mode_w0 & ~((15u<<20)|(31u<<15))) | (2u<<20) | (13u<<15);
    r.combine_mode_w1 = (r.combine_mode_w1 & ~(15u<<28)) | (1u<<28);
    r.rasterize_triangle_screen(a, b, c); // (TEXEL1-TEXEL0)*LOD_FRACTION + TEXEL0
    uint16_t pixel = 0;
    const auto* destination = ram + r.color_image_addr + (5*320+5)*2;
    std::memcpy(&pixel, destination, 2);
    require(pixel == 0x041F, "Color combiner ignores the computed LOD fraction");
    commands(r, {{0xFA001040, 0xFFFFFFFF}});
    require(r.primitive_lod_fraction == 64 && r.primitive_min_lod == 16,
        "SETPRIMCOLOR does not preserve primitive LOD parameters");
    r.combine_mode_w0 = (r.combine_mode_w0 & ~(31u<<15)) | (14u<<15);
    r.rasterize_triangle_screen(a, b, c);
    std::memcpy(&pixel, destination, 2);
    require(pixel == 0x0611, "Color combiner ignores primitive LOD fraction");
}

static void check_texture_lod(uint8_t* ram) {
    F3DEX2Interpreter r(ram);
    r.other_mode_h = 1u << 16;
    r.texture_max_level = 3;
    auto lod = r.select_texture_lod(0.25f);
    require(lod.first == 0 && lod.second == 0 && lod.fraction == 0,
        "Magnified texture LOD is incorrect");
    lod = r.select_texture_lod(3);
    require(lod.first == 1 && lod.second == 2 && lod.fraction == 0.5f,
        "Mip interpolation does not use the adjacent levels");
    lod = r.select_texture_lod(32);
    require(lod.first == 3 && lod.second == 3 && lod.fraction == 1,
        "Texture LOD exceeds the G_TEXTURE maximum");
    r.texture_max_level = 0;
    lod = r.select_texture_lod(8);
    require(lod.first == 0 && lod.second == 0, "HUD LOD does not clamp to its only tile");
    r.other_mode_h = 0;
    lod = r.select_texture_lod(3);
    require(lod.first == 0 && lod.second == 1 && lod.fraction == 1,
        "LOD-disabled mode changes the physical tile pair");
}

static void check_texture_formats(uint8_t* ram) {
    F3DEX2Interpreter r(ram);
    auto& tile = r.tiles[0];
    tile.format = 0; tile.size = 3; tile.line = 1;
    tile.lrs = 1; tile.lrt = 1; tile.clamp_s = tile.clamp_t = 1;
    r.tmem[14] = 11; r.tmem[15] = 22;
    r.tmem[0x80E] = 33; r.tmem[0x80F] = 44;
    uint8_t c[4]{};
    require(r.sample_tile(tile, 1, 1, false, c) && c[0]==11 && c[1]==22 && c[2]==33 && c[3]==44,
        "RGBA32 sampling does not read split TMEM banks and odd rows");
    tile.format = 3; tile.size = 2;
    require(r.sample_tile(tile, 1, 1, false, c) && c[0]==11 && c[1]==11 && c[2]==11 && c[3]==22,
        "IA16 is decoded as RGBA16");
    tile.format = 4; tile.size = 1; r.tmem[0] = 85;
    require(r.sample_tile(tile, 0, 0, false, c) && c[0]==85 && c[3]==85,
        "I8 alpha does not equal intensity");
    tile.size = 0; r.tmem[0] = 0x50;
    require(r.sample_tile(tile, 0, 0, false, c) && c[0]==85 && c[3]==85,
        "I4 alpha does not equal intensity");
}

static void check_vi_scanout() {
    ultramodern::renderer::ViRegs vi{};
    vi.VI_STATUS_REG = 2;
    vi.VI_ORIGIN_REG = 0x1D6380;
    vi.VI_WIDTH_REG = 320;
    vi.VI_H_START_REG = (108u << 16) | 748u;
    vi.VI_V_START_REG = (37u << 16) | 511u;
    vi.VI_X_SCALE_REG = 512;
    vi.VI_Y_SCALE_REG = 1024;
    auto frame = tgod::vi_frame_layout(vi);
    require(frame.visible && frame.width == 320 && frame.height == 240, "Low-resolution VI scanout is incorrect");
    vi.VI_STATUS_REG |= 0x40;
    vi.VI_ORIGIN_REG = 0x26C600;
    vi.VI_WIDTH_REG = 1280;
    vi.VI_X_SCALE_REG = 1024;
    vi.VI_V_START_REG = (35u << 16) | 509u;
    frame = tgod::vi_frame_layout(vi);
    require(frame.visible && frame.width == 640 && frame.height == 476,
        "Interlaced showroom VI stride is mistaken for framebuffer width");
    vi.VI_H_START_REG = 0;
    require(!tgod::vi_frame_layout(vi).visible, "Black VI output is reported as visible");
    vi.VI_H_START_REG = (108u << 16) | 748u;
    vi.VI_ORIGIN_REG = 0x7FFFF0;
    require(!tgod::vi_frame_layout(vi).visible, "VI scanout accepts an image outside RDRAM");
}

static void check_thread_queue(uint8_t* rdram) {
    const int32_t queue = int32_t(0x80006000u);
    const std::array<int32_t, 3> nodes = {
        int32_t(0x80006100u), int32_t(0x80006200u), int32_t(0x80006300u)
    };
    for (int i = 0; i < 3; ++i) {
        auto* thread = TO_PTR(OSThread, nodes[i]);
        *thread = {};
        thread->priority = 30 - 10 * i;
        ultramodern::thread_queue_insert(rdram, queue, nodes[i]);
    }
    require(ultramodern::thread_queue_remove(rdram, queue, nodes[1]), "Cannot remove middle thread");
    require(TO_PTR(OSThread, nodes[1])->queue == 0 && TO_PTR(OSThread, nodes[1])->next == 0,
        "Removed thread retains queue ownership or a link");
    require(ultramodern::thread_queue_remove(rdram, queue, nodes[2]), "Cannot remove tail thread");
    require(!ultramodern::thread_queue_remove(rdram, queue, nodes[2]), "Absent thread removal succeeds");
    require(ultramodern::thread_queue_pop(rdram, queue) == nodes[0], "Wrong remaining thread");
    require(ultramodern::thread_queue_empty(rdram, queue), "Thread queue retains stale links");
}

static void check_lighting(uint8_t* ram) {
    F3DEX2Interpreter r(ram);
    auto byte = [&](uint32_t address, uint8_t value) { ram[address ^ 3] = value; };
    const uint8_t directional[3] = {100, 20, 30};
    const uint8_t ambient[3] = {10, 20, 30};
    for (int i = 0; i < 3; ++i) {
        byte(0x5400 + i, directional[i]);
        byte(0x5500 + i, ambient[i]);
    }
    byte(0x5408, 127);
    byte(0x560C, 127); // Vertex normal +X; alpha is independent.
    byte(0x560D, 0); byte(0x560E, 0); byte(0x560F, 173);
    commands(r, {{0xDC08060A, 0x5400}, {0xDC08090A, 0x5500},
                 {0xDB020000, 24}, {0xD9000000, 0x00020000}, {0x01001002, 0x5600}});
    auto v = r.vertex_buffer[0];
    require(v.r == 110 && v.g == 40 && v.b == 60 && v.a == 173,
        "Directional + ambient light or vertex alpha is wrong");
    byte(0x560C, uint8_t(-127));
    commands(r, {{0x01001002, 0x5600}});
    v = r.vertex_buffer[0];
    require(v.r == 10 && v.g == 20 && v.b == 30,
        "A back-facing normal must receive only ambient light");
    byte(0x560C, 127);
    r.modelview_mtx.m[0][0] = r.modelview_mtx.m[1][1] = 0;
    r.modelview_mtx.m[0][1] = 1;
    r.modelview_mtx.m[1][0] = -1;
    byte(0x5408, 0); byte(0x5409, 127);
    commands(r, {{0xDC08060A, 0x5400}, {0x01001002, 0x5600}});
    require(r.vertex_buffer[0].r == 110, "Rotated normals use the wrong light transform");
    r.modelview_mtx = Matrix4x4::identity();
    r.texture_scale_s = r.texture_scale_t = 0x07C0;
    commands(r, {{0xD9000000, 0x00060000}, {0x01001002, 0x5600}});
    require(std::abs(r.vertex_buffer[0].u - 31.0f) < 0.001f &&
            std::abs(r.vertex_buffer[0].v - 15.5f) < 0.001f,
        "Reflection texture coordinates ignore normal direction or texture scale");
}

int main() {
    try {
        const std::vector<uint8_t> expected = {0x80, 0x37, 0x12, 0x40, 1, 2, 3, 4};
        auto big_endian = expected;
        std::vector<uint8_t> swapped16 = {0x37, 0x80, 0x40, 0x12, 2, 1, 4, 3};
        std::vector<uint8_t> swapped32 = {0x40, 0x12, 0x37, 0x80, 4, 3, 2, 1};
        std::vector<uint8_t> invalid = {0, 0, 0, 0};
        std::vector<uint8_t> truncated = {0x80, 0x37, 0x12};
        require(tgod::normalize_rom_bytes(big_endian) && big_endian == expected, "z64 normalization failed");
        require(tgod::normalize_rom_bytes(swapped16) && swapped16 == expected, "v64 normalization failed");
        require(tgod::normalize_rom_bytes(swapped32) && swapped32 == expected, "n64 normalization failed");
        require(!tgod::normalize_rom_bytes(invalid) && !tgod::normalize_rom_bytes(truncated), "Invalid ROM header accepted");
        std::vector<uint8_t> ram(8 * 1024 * 1024);
        check_texture_rows(ram.data());
        check_tile_sampling(ram.data());
        check_second_texture(ram.data());
        check_texture_lod(ram.data());
        check_texture_formats(ram.data());
        check_vi_scanout();
        check_matrix_and_viewport(ram.data());
        check_thread_queue(ram.data());
        check_lighting(ram.data());
        std::cout << "PASS: ROM byte orders, triangle/rectangle texture rows, coordinates/filtering, dual textures/LOD, texture formats, VI scanout, matrix push/pop, viewport isolation, thread queues, lighting and texgen\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

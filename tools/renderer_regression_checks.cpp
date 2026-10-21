#include "f3dex2.hpp"
#include <ultramodern/ultramodern.hpp>
#include <array>
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

int main() {
    try {
        std::vector<uint8_t> ram(8 * 1024 * 1024);
        check_texture_rows(ram.data());
        check_matrix_and_viewport(ram.data());
        check_thread_queue(ram.data());
        std::cout << "PASS: texture rows, matrix push/pop, viewport isolation, thread queue removal\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

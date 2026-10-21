#pragma once
#include <cstdint>
#include <cstddef>

struct Matrix4x4 {
    float m[4][4]{};
    static Matrix4x4 identity();
    static Matrix4x4 from_n64_fixed(const int16_t* int_part, const uint16_t* frac_part);
    Matrix4x4 operator*(const Matrix4x4& rhs) const;
    void transform(float x, float y, float z, float& ox, float& oy, float& oz, float& ow) const;
};

struct Vertex3D {
    float x = 0, y = 0, z = 0;
    float clip_x = 0, clip_y = 0, clip_z = 0, clip_w = 1.0f;
    float screen_x = 0, screen_y = 0, screen_z = 0;
    float u = 0, v = 0;
    uint8_t r = 255, g = 255, b = 255, a = 255;
    bool clipped = false;
    bool is_screen_space = false;
};

class F3DEX2Interpreter {
public:
    uint8_t* rdram = nullptr;
    uint32_t segments[16]{};
    uint8_t tmem[4096]{};

    // Framebuffer targets
    uint32_t color_image_addr = 0;
    uint32_t color_image_width = 320;
    uint32_t color_image_format = 0;
    uint32_t color_image_size = 2; // 0=4b, 1=8b, 2=16b, 3=32b
    uint32_t depth_image_addr = 0;
    uint32_t geometry_mode = 0;

    // RDP colors
    uint32_t fill_color = 0;
    uint32_t env_color = 0xFFFFFFFF;
    uint32_t prim_color = 0xFFFFFFFF;
    uint8_t primitive_lod_fraction = 0, primitive_min_lod = 0;
    uint32_t blend_color = 0;
    uint32_t fog_color = 0;
    uint32_t combine_mode_w0 = 0;
    uint32_t combine_mode_w1 = 0;
    bool combine_mode_set = false;
    uint32_t combine_mode_updates = 0;
    uint16_t primitive_depth = 0;
    uint16_t primitive_depth_delta = 0;

    // Texture image state
    uint32_t texture_image_addr = 0;
    uint32_t texture_format = 0;
    uint32_t texture_size = 2;
    uint32_t texture_width = 0;
    uint8_t texture_tile = 0;
    uint8_t texture_max_level = 0;
    bool texture_enabled = true;
    uint32_t texture_scale_s = 0x10000;
    uint32_t texture_scale_t = 0x10000;
    uint32_t tlut_tmem_addr = 2048;

    // Tiles (0..7)
    struct Tile {
        uint32_t format = 0;
        uint32_t size = 0;
        uint32_t line = 0;
        uint32_t tmem_addr = 0;
        uint32_t palette = 0;
        uint32_t clamp_s = 0, mirror_s = 0, mask_s = 0, shift_s = 0;
        uint32_t clamp_t = 0, mirror_t = 0, mask_t = 0, shift_t = 0;
        uint32_t uls = 0, ult = 0, lrs = 0, lrt = 0;
        uint8_t uls_fraction = 0, ult_fraction = 0;
    } tiles[8];

    // Scissor bounds
    int32_t scissor_xh = 0, scissor_yh = 0;
    int32_t scissor_xl = 320, scissor_yl = 240;

    // Other modes
    uint32_t other_mode_h = 0;
    uint32_t other_mode_l = 0;

    // Call stack for nested DLs
    static constexpr size_t MAX_DL_STACK = 18;
    uint32_t dl_stack[MAX_DL_STACK]{};
    size_t dl_stack_depth = 0;

    uint32_t rects_drawn = 0;
    uint32_t triangles_drawn = 0;
    uint64_t triangles_culled = 0;
    uint64_t pixels_depth_rejected = 0;
    uint64_t pixels_texel_invalid = 0;
    uint64_t pixels_written = 0;
    uint64_t combine_cycle_triangles[4]{};
    uint32_t untextured_triangles = 0;
    uint64_t lit_vertices = 0;
    uint64_t light_commands = 0;
    uint64_t texgen_vertices = 0;
    uint64_t second_texture_triangles = 0;
    struct DirectionalLight {
        uint8_t color[3]{};
        float direction[3]{};
    } lights[8];
    uint32_t light_count = 0;
    float look_at[2][3] = {{1, 0, 0}, {0, 1, 0}};
    uint32_t rdp_half_1 = 0;
    uint32_t rdp_half_2 = 0;
    uint32_t opcode_counts[256]{};
    uint32_t fmt_counts[8][4]{};

    // 3D Geometry State
    static constexpr size_t MAX_MTX_STACK = 32;
    Matrix4x4 modelview_mtx{};
    Matrix4x4 proj_mtx{};
    Matrix4x4 mvp_mtx{};
    Matrix4x4 modelview_stack[MAX_MTX_STACK]{};
    size_t modelview_stack_depth = 0;
    Matrix4x4 proj_stack[4]{};
    size_t proj_stack_depth = 0;

    int16_t viewport_scale[4] = { 640, 480, 511, 0 };
    int16_t viewport_trans[4] = { 640, 480, 511, 0 };

    Vertex3D vertex_buffer[64]{};

    void update_mvp();
    void transform_vertex(Vertex3D& v);
    void light_and_texgen_vertex(Vertex3D& v, int8_t nx, int8_t ny, int8_t nz);
    bool fetch_tile_texel(const Tile& tile, int32_t s, int32_t t,
        uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const;
    bool sample_tile(const Tile& tile, float s, float t, bool linear, uint8_t (&color)[4]) const;
    struct TextureLOD { uint8_t first = 0, second = 1; float fraction = 1; };
    TextureLOD select_texture_lod(float footprint) const;
    uint8_t combine_colors(const uint8_t (&texel0)[4], const uint8_t (&texel1)[4],
        const uint8_t (&shade)[4], int32_t lod_fraction, uint8_t (&color)[4]) const;
    bool blend_colors(const uint8_t (&color)[4], uint8_t shade_alpha, uint8_t compare_alpha,
        uint16_t destination, int32_t x, int32_t y, uint16_t& pixel) const;
    void rasterize_triangle(const Vertex3D& v0, const Vertex3D& v1, const Vertex3D& v2);
    void rasterize_triangle_screen(const Vertex3D& v0, const Vertex3D& v1, const Vertex3D& v2);

    F3DEX2Interpreter(uint8_t* rdram_in = nullptr);
    void reset();
    uint32_t resolve_segmented_address(uint32_t addr) const;
    void process_display_list(uint32_t dl_addr);
};

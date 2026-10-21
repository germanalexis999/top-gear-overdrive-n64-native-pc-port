#pragma once
#include <ultramodern/renderer_context.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tgod {
struct VIFrameLayout {
    uint32_t origin = 0, width = 0, height = 0;
    bool visible = false;
};

inline VIFrameLayout vi_frame_layout(const ultramodern::renderer::ViRegs& vi) {
    VIFrameLayout frame;
    frame.origin = vi.VI_ORIGIN_REG & 0x1FFFFFFF;
    frame.width = vi.VI_WIDTH_REG;
    const uint32_t x_scale = vi.VI_X_SCALE_REG & 0xFFF;
    const uint32_t y_scale = vi.VI_Y_SCALE_REG & 0xFFF;
    const int h_span = int(vi.VI_H_START_REG & 0x3FF) - int((vi.VI_H_START_REG >> 16) & 0x3FF);
    const int v_span = int(vi.VI_V_START_REG & 0x3FF) - int((vi.VI_V_START_REG >> 16) & 0x3FF);
    if (!frame.origin || !frame.width || frame.width > 2048 || !x_scale || !y_scale || h_span <= 0 || v_span <= 0)
        return {};
    // Non-deflickered interlace steps over alternate rows. VI_WIDTH then
    // describes a field's stride, which can be twice the stored image width.
    const bool serrate = (vi.VI_STATUS_REG & 0x40) != 0;
    const float estimated_width = float(h_span) * x_scale / 1024.0f;
    if (serrate && estimated_width < frame.width / 1.875f) frame.width /= 2;
    const float rows = float(v_span) * y_scale / 2048.0f * (float(vi.VI_WIDTH_REG) / frame.width);
    // Include the filter margin and round to complete groups of four rows,
    // following the framebuffer-size inference used by RT64's VI path.
    frame.height = uint32_t(std::lround((std::lround(rows) + 2) / 4.0f)) * 4;
    if (!frame.height || frame.height > 1024 ||
        frame.origin + uint64_t(frame.width) * frame.height * 2 > 8 * 1024 * 1024) return {};
    frame.visible = (vi.VI_STATUS_REG & 3) == 2; // Current rasterizer writes RGBA16 targets.
    return frame;
}
}

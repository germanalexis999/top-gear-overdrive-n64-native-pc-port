#pragma once
#include <cstdint>

namespace tgod {
struct GraphicsEnhancements {
    bool force_high_detail = false;
    uint32_t draw_distance_percent = 100;
    uint32_t extra_track_sections = 0;
};
// Set before starting the game threads. Changes take effect on next launch.
void configure_graphics_enhancements(GraphicsEnhancements options);
}

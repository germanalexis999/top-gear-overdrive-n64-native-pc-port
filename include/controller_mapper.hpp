#pragma once
#include "controller_profiles.hpp"
#include <windows.h>

namespace tgod::controls {
void initialize(const std::filesystem::path& directory);
void set_game_window(HWND window);
void shutdown();
void poll();
input::Snapshot snapshot();
void show_mapper(HWND owner);
void close_mapper();
bool handle_message(MSG& message);
}

#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tgod {
struct RomResult {
    std::vector<uint8_t> bytes;
    std::string error;
    explicit operator bool() const { return error.empty(); }
};

// Converts recognized N64 byte orders to big endian, entirely in memory.
bool normalize_rom_bytes(std::vector<uint8_t>& bytes);
RomResult load_verified_rom(const std::filesystem::path& path);
std::filesystem::path executable_directory();
std::filesystem::path choose_rom_file();
}

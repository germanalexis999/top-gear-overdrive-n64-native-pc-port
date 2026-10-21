#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <bcrypt.h>
#include "rom_loader.hpp"
#include <algorithm>
#include <array>
#include <fstream>

namespace tgod {
bool normalize_rom_bytes(std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4 || bytes.size() % 4 != 0) return false;
    const std::array<uint8_t, 4> magic = {bytes[0], bytes[1], bytes[2], bytes[3]};
    if (magic == std::array<uint8_t, 4>{0x80, 0x37, 0x12, 0x40}) return true;
    if (magic == std::array<uint8_t, 4>{0x37, 0x80, 0x40, 0x12}) {
        for (size_t i = 0; i < bytes.size(); i += 2) std::swap(bytes[i], bytes[i + 1]);
        return true;
    }
    if (magic == std::array<uint8_t, 4>{0x40, 0x12, 0x37, 0x80}) {
        for (size_t i = 0; i < bytes.size(); i += 4) {
            std::swap(bytes[i], bytes[i + 3]);
            std::swap(bytes[i + 1], bytes[i + 2]);
        }
        return true;
    }
    return false;
}

RomResult load_verified_rom(const std::filesystem::path& path) {
    constexpr std::streamsize expected_size = 12 * 1024 * 1024;
    // Identifier for the supported USA v0 dump, not a security signature.
    constexpr std::array<uint8_t, 20> expected_sha1 = {
        0x75, 0xeb, 0xbe, 0x45, 0x19, 0x06, 0xce, 0x68, 0xb0, 0xdd,
        0x43, 0xee, 0xf9, 0xee, 0x44, 0x41, 0x64, 0x52, 0xa4, 0xf1
    };
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {{}, "Cannot open the selected ROM."};
    if (file.tellg() != expected_size) return {{}, "Expected a 12 MiB Top Gear Overdrive USA v0 ROM."};
    RomResult result;
    result.bytes.resize(expected_size);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(result.bytes.data()), expected_size))
        return {{}, "Cannot read the complete ROM."};
    if (!normalize_rom_bytes(result.bytes)) return {{}, "Unsupported ROM format. Select a .z64, .v64 or .n64 file."};

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<uint8_t, 20> digest{};
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
    if (status >= 0) status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, result.bytes.data(), static_cast<ULONG>(result.bytes.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return {{}, "Windows could not validate the ROM hash."};
    if (digest != expected_sha1) return {{}, "Unsupported ROM revision or modified ROM. Expected Top Gear Overdrive USA v0."};
    return result;
}

std::filesystem::path executable_directory() {
    std::vector<wchar_t> path(32768);
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

std::filesystem::path choose_rom_file() {
    std::vector<wchar_t> path(32768, 0);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = L"Seleccionar ROM propia de Top Gear Overdrive USA v0";
    dialog.lpstrFilter = L"ROM de Nintendo 64 (*.z64;*.v64;*.n64)\0*.z64;*.v64;*.n64\0Todos los archivos\0*.*\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return {};
    return std::filesystem::path(path.data());
}
}

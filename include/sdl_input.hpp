#pragma once
#include "controller_profiles.hpp"
#include <memory>

namespace tgod::input {
// Owned and called by the Win32 UI thread. N64 threads only read its snapshot.
class SDLInput {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    SDLInput();
    ~SDLInput();
    bool initialize(const std::filesystem::path& directory);
    void update(RawPads& pads);
    std::array<std::string,ports> names() const;
};
}

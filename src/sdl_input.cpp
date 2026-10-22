#include "sdl_input.hpp"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <chrono>

namespace tgod::input {
struct SDLInput::Impl {
    struct Device {
        SDL_JoystickID id=0;
        SDL_Joystick* joystick=nullptr;
        SDL_Gamepad* gamepad=nullptr;
        std::string identity,name;
    };
    std::array<Device,ports> devices{};
    bool initialized=false;
    std::chrono::steady_clock::time_point next_scan{};
    static void close(Device& d) {
        if(d.gamepad)SDL_CloseGamepad(d.gamepad);else if(d.joystick)SDL_CloseJoystick(d.joystick);
        d.gamepad=nullptr;d.joystick=nullptr;d.id=0;
    }
};
SDLInput::SDLInput():impl(std::make_unique<Impl>()){}
SDLInput::~SDLInput(){for(auto& d:impl->devices)Impl::close(d);if(impl->initialized)SDL_QuitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_JOYSTICK);}
bool SDLInput::initialize(const std::filesystem::path& directory) {
    SDL_SetMainReady(); // The port owns main() and the Win32 message loop.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_JOYSTICK)) {
        std::cerr<<"[SDL input] Initialization failed: "<<SDL_GetError()<<"; keyboard remains available.\n";return false;
    }
    impl->initialized=true;
    SDL_SetJoystickEventsEnabled(false);SDL_SetGamepadEventsEnabled(false);
    const auto mappings=directory/L"gamecontrollerdb.txt";std::error_code ec;
    if(std::filesystem::exists(mappings,ec)) {
        const auto utf8=mappings.u8string();const int n=SDL_AddGamepadMappingsFromFile(reinterpret_cast<const char*>(utf8.c_str()));
        std::cout<<"[SDL input] Local gamepad mappings: "<<n<<'.'<<(n<0?SDL_GetError():"")<<'\n';
    }
    std::cout<<"[SDL input] SDL "<<SDL_VERSIONNUM_MAJOR(SDL_GetVersion())<<'.'<<SDL_VERSIONNUM_MINOR(SDL_GetVersion())<<'.'<<SDL_VERSIONNUM_MICRO(SDL_GetVersion())
        <<"; gamepads and raw joysticks, four independent ports.\n";return true;
}
std::array<std::string,ports> SDLInput::names() const {
    std::array<std::string,ports> out{};
    for(size_t i=0;i<ports;++i)out[i]=impl->devices[i].name;return out;
}
void SDLInput::update(RawPads& pads) {
    pads={};if(!impl->initialized)return;
    SDL_PumpEvents();SDL_UpdateJoysticks();SDL_UpdateGamepads();
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    for(size_t i=0;i<ports;++i) {
        auto& d=impl->devices[i];
        if(d.joystick && !SDL_JoystickConnected(d.joystick)) {
            std::cout<<"[SDL input] Port "<<i+1<<" disconnected: "<<d.name<<'\n';Impl::close(d);
        }
    }
    const auto now=std::chrono::steady_clock::now();
    if(now>=impl->next_scan) {
    impl->next_scan=now+std::chrono::milliseconds(250);
    int count=0;auto* ids=SDL_GetJoysticks(&count);
    for(int n=0;ids && n<count;++n) {
        const auto id=ids[n];bool assigned=false;
        for(const auto& d:impl->devices)if(d.id==id)assigned=true;
        if(assigned)continue;
        bool free_slot=false;for(const auto& d:impl->devices)if(!d.id)free_slot=true;
        if(!free_slot)continue;
        SDL_Gamepad* gamepad=SDL_IsGamepad(id)?SDL_OpenGamepad(id):nullptr;
        SDL_Joystick* joystick=gamepad?SDL_GetGamepadJoystick(gamepad):SDL_OpenJoystick(id);
        if(!joystick)continue;
        char guid[33]{};SDL_GUIDToString(SDL_GetJoystickGUID(joystick),guid,sizeof(guid));
        const char* serial=SDL_GetJoystickSerial(joystick);const char* path=SDL_GetJoystickPath(joystick);
        const std::string identity=std::string(guid)+":"+(serial && *serial?serial:path?path:"");
        int slot=-1;
        for(size_t i=0;i<ports;++i)if(!impl->devices[i].id && impl->devices[i].identity==identity){slot=int(i);break;}
        if(slot<0 && gamepad){const int preferred=SDL_GetGamepadPlayerIndex(gamepad);if(preferred>=0 && preferred<int(ports) && !impl->devices[preferred].id && impl->devices[preferred].identity.empty())slot=preferred;}
        if(slot<0)for(size_t i=0;i<ports;++i)if(!impl->devices[i].id && impl->devices[i].identity.empty()){slot=int(i);break;}
        if(slot<0)for(size_t i=0;i<ports;++i)if(!impl->devices[i].id){slot=int(i);break;}
        if(slot<0){if(gamepad)SDL_CloseGamepad(gamepad);else SDL_CloseJoystick(joystick);continue;}
        auto& d=impl->devices[slot];const char* name=SDL_GetJoystickName(joystick);
        d={id,joystick,gamepad,identity,name?name:"Joystick"};
        std::cout<<"[SDL input] Port "<<slot+1<<": "<<d.name<<(gamepad?" (mapped gamepad)":" (raw joystick: configure with F7)")<<'\n';
    }
    SDL_free(ids);
    }
    for(size_t i=0;i<ports;++i) {
        const auto& d=impl->devices[i];if(!d.joystick)continue;
        auto& raw=pads[i];raw.connected=true;raw.raw_mode=!d.gamepad;
        raw.axis_count=std::clamp(SDL_GetNumJoystickAxes(d.joystick),0,16);
        raw.button_count=std::clamp(SDL_GetNumJoystickButtons(d.joystick),0,64);
        raw.hat_count=std::clamp(SDL_GetNumJoystickHats(d.joystick),0,4);
        for(int a=0;a<raw.axis_count;++a)raw.axes[a]=SDL_GetJoystickAxis(d.joystick,a);
        for(int b=0;b<raw.button_count;++b)if(SDL_GetJoystickButton(d.joystick,b))raw.buttons|=uint64_t(1)<<b;
        for(int h=0;h<raw.hat_count;++h)raw.hats[h]=SDL_GetJoystickHat(d.joystick,h);
        if(!d.gamepad)continue;
        constexpr SDL_GamepadButton source[]{SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
            SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_LEFT_STICK,SDL_GAMEPAD_BUTTON_RIGHT_STICK,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_NORTH};
        constexpr uint16_t target[]{1,2,4,8,0x10,0x20,0x40,0x80,0x100,0x200,0x1000,0x2000,0x4000,0x8000};
        for(size_t b=0;b<std::size(source);++b)if(SDL_GetGamepadButton(d.gamepad,source[b]))raw.pad.buttons|=target[b];
        raw.pad.lx=SDL_GetGamepadAxis(d.gamepad,SDL_GAMEPAD_AXIS_LEFTX);
        raw.pad.ly=int16_t(std::clamp(-int(SDL_GetGamepadAxis(d.gamepad,SDL_GAMEPAD_AXIS_LEFTY)),-32768,32767));
        raw.pad.rx=SDL_GetGamepadAxis(d.gamepad,SDL_GAMEPAD_AXIS_RIGHTX);
        raw.pad.ry=int16_t(std::clamp(-int(SDL_GetGamepadAxis(d.gamepad,SDL_GAMEPAD_AXIS_RIGHTY)),-32768,32767));
        raw.pad.left_trigger=uint8_t(std::max(0,int(SDL_GetGamepadAxis(d.gamepad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)))*255/32767);
        raw.pad.right_trigger=uint8_t(std::max(0,int(SDL_GetGamepadAxis(d.gamepad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)))*255/32767);
    }
}
}

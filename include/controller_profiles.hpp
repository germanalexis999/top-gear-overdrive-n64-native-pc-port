#pragma once
#include "controller_input.hpp"
#include <filesystem>
#include <string>

namespace tgod::input {
enum class Action { A,B,Z,L,R,Start,CUp,CDown,CLeft,CRight,DUp,DDown,DLeft,DRight,Left,Right,Up,Down,Count };
constexpr size_t action_count=size_t(Action::Count), binding_count=3;
enum class BindingKind { None, Button, Axis, Key, RawButton, RawAxis, Hat };
struct Binding { BindingKind kind=BindingKind::None; int code=0; int center=0; };
using Bindings=std::array<std::array<Binding,binding_count>,action_count>;
struct Profile {
    std::string name;
    Bindings gamepad{},keyboard{};
    int deadzone=24;
    bool invert_x=false,invert_y=false;
};
struct PlayerConfig { int slot=-1; bool keyboard=false; Profile profile; };
using ControlsConfig=std::array<PlayerConfig,ports>;
struct RawPad {
    bool connected=false; Gamepad pad{};
    bool raw_mode=false;
    int axis_count=0,button_count=0,hat_count=0;
    std::array<int16_t,16> axes{};
    uint64_t buttons=0;
    std::array<uint8_t,4> hats{};
};
using RawPads=std::array<RawPad,ports>;
using Keys=std::array<bool,256>;
ControlsConfig default_controls();
Snapshot map_controls(const ControlsConfig&,const RawPads&,const Keys&);
bool load_controls(const std::filesystem::path&,ControlsConfig&,std::string& error);
bool save_controls(const std::filesystem::path&,const ControlsConfig&,std::string& error);
bool valid_controls(const ControlsConfig&,std::string& error);
}

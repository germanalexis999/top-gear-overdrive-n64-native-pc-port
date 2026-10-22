#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace tgod::input {
constexpr std::size_t ports = 4;
// Matches the XInput gamepad ABI without requiring its import library.
struct Gamepad {
    uint16_t buttons;
    uint8_t left_trigger, right_trigger;
    int16_t lx, ly, rx, ry;
};
struct State { bool connected=false; uint16_t buttons=0; float x=0, y=0; };
using Snapshot = std::array<State,ports>;
inline State map_gamepad(const Gamepad& pad) {
    State s{true};
    constexpr std::array<uint16_t,11> source{0x0010,0x1000,0x2000,0x4000,0x8000,0x0100,0x0200,0x0001,0x0002,0x0004,0x0008};
    constexpr std::array<uint16_t,11> target{0x1000,0x8000,0x4000,0x2000,0x0004,0x0020,0x0010,0x0800,0x0400,0x0200,0x0100};
    for(std::size_t i=0;i<source.size();++i) if(pad.buttons&source[i])s.buttons|=target[i];
    if(pad.right_trigger>30)s.buttons|=0x8000;
    if(pad.left_trigger>30)s.buttons|=0x4000;
    s.x=((pad.buttons&8)?1.0f:0)-((pad.buttons&4)?1.0f:0);
    s.y=((pad.buttons&1)?1.0f:0)-((pad.buttons&2)?1.0f:0);
    auto axis=[](int16_t value){constexpr int deadzone=7849;
        return value>deadzone ? float(value-deadzone)/(32767-deadzone) :
               value<-deadzone ? float(value+deadzone)/(32768-deadzone) : 0.0f;};
    if(std::abs(int(pad.lx))>7849)s.x=axis(pad.lx);
    if(std::abs(int(pad.ly))>7849)s.y=axis(pad.ly);
    if(pad.ry<-12000)s.buttons|=0x0004;
    if(pad.rx<-12000)s.buttons|=0x0002;
    if(pad.rx>12000)s.buttons|=0x0001;
    if(pad.ry>12000)s.buttons|=0x0008;
    return s;
}
// The game consumes four 8-byte SI replies and four 6-byte OSContPad records.
// RDRAM stores guest bytes in word-swapped order, unlike host render surfaces.
inline void write_si(uint8_t* ram,const Snapshot& states) {
    auto byte=[&](uint32_t address,uint8_t value){ram[(address&0x7FFFFF)^3]=value;};
    auto half=[&](uint32_t address,uint16_t value){byte(address,uint8_t(value>>8));byte(address+1,uint8_t(value));};
    uint8_t mask=0;
    for(std::size_t i=0;i<ports;++i) {
        const auto& s=states[i];
        if(s.connected)mask|=uint8_t(1u<<i);
        const uint16_t buttons=s.connected?s.buttons:0;
        const auto x=uint8_t(int8_t(std::clamp(int(s.x*80),-80,80)));
        const auto y=uint8_t(int8_t(std::clamp(int(s.y*80),-80,80)));
        const uint32_t pad=0x80077058+uint32_t(i)*6,reply=0x800F7B30+uint32_t(i)*8;
        half(pad,buttons);byte(pad+2,s.connected?x:0);byte(pad+3,s.connected?y:0);
        byte(pad+4,s.connected?0:8);byte(pad+5,0);
        byte(reply,0xFF);byte(reply+1,1);byte(reply+2,s.connected?4:0x84);byte(reply+3,1);
        half(reply+4,buttons);byte(reply+6,s.connected?x:0);byte(reply+7,s.connected?y:0);
    }
    byte(0x800522D0,mask);
    // This is the number of channels to scan, not the connected-device count.
    byte(0x80100184,4);
}
}

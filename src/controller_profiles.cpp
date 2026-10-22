#include "controller_profiles.hpp"
#include "json/json.hpp"
#include <fstream>
#include <windows.h>

namespace tgod::input {
ControlsConfig default_controls() {
    ControlsConfig config;
    for(size_t player=0;player<ports;++player) {
        auto& c=config[player];c.slot=int(player);c.keyboard=player==0;
        auto& p=c.profile;p.name="Jugador "+std::to_string(player+1);
        auto pad=[&](Action a,int index,BindingKind k,int code){p.gamepad[size_t(a)][index]={k,code};};
        auto key=[&](Action a,std::initializer_list<int> keys){size_t i=0;for(int k:keys)p.keyboard[size_t(a)][i++]={BindingKind::Key,k};};
        constexpr uint16_t buttons[]{0x1000,0x2000,0x4000,0x0100,0x0200,0x0010,0,0x8000,0,0,1,2,4,8};
        for(size_t a=0;a<14;++a)if(buttons[a])pad(Action(a),0,BindingKind::Button,buttons[a]);
        pad(Action::A,1,BindingKind::Axis,6);pad(Action::B,1,BindingKind::Axis,5);
        pad(Action::CUp,0,BindingKind::Axis,4);pad(Action::CDown,1,BindingKind::Axis,-4);
        pad(Action::CLeft,0,BindingKind::Axis,-3);pad(Action::CRight,0,BindingKind::Axis,3);
        for(int i=0;i<4;++i) {
            constexpr int axes[]{-1,1,2,-2},dpad[]{4,8,1,2};
            pad(Action(size_t(Action::Left)+i),0,BindingKind::Axis,axes[i]);
            pad(Action(size_t(Action::Left)+i),1,BindingKind::Button,dpad[i]);
        }
        key(Action::A,{'X',VK_SPACE,'W'});key(Action::B,{'Z',VK_BACK,'S'});
        key(Action::Z,{'C',VK_LCONTROL});key(Action::L,{'A','Q'});key(Action::R,{'D','E'});
        key(Action::Start,{VK_RETURN,VK_TAB});key(Action::CUp,{'T','4'});
        key(Action::CDown,{'V','1'});key(Action::CLeft,{'B','2'});key(Action::CRight,{'N','3'});
        key(Action::DUp,{VK_UP});key(Action::DDown,{VK_DOWN});
        key(Action::DLeft,{VK_LEFT,'A'});key(Action::DRight,{VK_RIGHT,'D'});
        key(Action::Left,{VK_LEFT,'A'});key(Action::Right,{VK_RIGHT,'D'});
        key(Action::Up,{VK_UP});key(Action::Down,{VK_DOWN});
    }
    return config;
}

static float strength(const Binding& b,const Gamepad& p,const Keys& keys,int deadzone,bool analog) {
    if(b.kind==BindingKind::Key)return b.code>0 && b.code<256 && keys[b.code] ? 1:0;
    if(b.kind==BindingKind::Button)return (p.buttons&b.code)?1:0;
    if(b.kind!=BindingKind::Axis || !b.code || std::abs(b.code)>6)return 0;
    const int a=std::abs(b.code)-1;
    const float axes[]{p.lx/(p.lx<0?32768.0f:32767.0f),p.ly/(p.ly<0?32768.0f:32767.0f),
        p.rx/(p.rx<0?32768.0f:32767.0f),p.ry/(p.ry<0?32768.0f:32767.0f),p.left_trigger/255.0f,p.right_trigger/255.0f};
    const float v=std::max(0.0f,axes[a]*(b.code<0?-1:1));
    if(!analog)return v>(a>=4?30.0f/255:12000.0f/32768)?1:0;
    const float dz=a>=4?0.0f:deadzone/100.0f;
    return std::clamp((v-dz)/(1-dz),0.0f,1.0f);
}

Snapshot map_controls(const ControlsConfig& config,const RawPads& pads,const Keys& keys) {
    Snapshot out{};
    constexpr uint16_t masks[]{0x8000,0x4000,0x2000,0x0020,0x0010,0x1000,8,4,2,1,0x0800,0x0400,0x0200,0x0100};
    for(size_t i=0;i<ports;++i) {
        const auto& c=config[i];const auto& p=c.profile;
        const bool connected=c.slot>=0 && c.slot<int(ports) && pads[c.slot].connected;
        auto& s=out[i];s.connected=connected||c.keyboard;
        std::array<float,action_count> values{};
        for(size_t a=0;a<action_count;++a) {
            if(connected)for(const auto& b:p.gamepad[a])values[a]=std::max(values[a],strength(b,pads[c.slot].pad,keys,p.deadzone,a>=14));
            if(c.keyboard)for(const auto& b:p.keyboard[a])values[a]=std::max(values[a],strength(b,{},keys,p.deadzone,a>=14));
            if(a<14 && values[a]>0)s.buttons|=masks[a];
        }
        s.x=values[size_t(Action::Right)]-values[size_t(Action::Left)];
        s.y=values[size_t(Action::Up)]-values[size_t(Action::Down)];
        if(p.invert_x)s.x=-s.x;if(p.invert_y)s.y=-s.y;
    }
    return out;
}

bool valid_controls(const ControlsConfig& config,std::string& error) {
    std::array<bool,ports> assigned{};bool keyboard=false;
    for(const auto& c:config) {
        if(c.slot<-1 || c.slot>=int(ports) || (c.slot>=0 && assigned[c.slot])) {error="Un mando no puede ocupar dos jugadores.";return false;}
        if(c.slot>=0)assigned[c.slot]=true;
        if(c.keyboard && keyboard){error="El teclado ya pertenece a otro jugador.";return false;}keyboard|=c.keyboard;
        if(c.profile.deadzone<0 || c.profile.deadzone>50 || c.profile.name.empty() || c.profile.name.size()>96) {error="Perfil o zona muerta invalida (0-50%).";return false;}
        for(int device=0;device<2;++device)for(const auto& action:device?c.profile.keyboard:c.profile.gamepad)for(const auto& b:action) {
            bool valid=b.kind==BindingKind::None && b.code==0;
            if(device==1 && b.kind==BindingKind::Key) valid=b.code>0 && b.code<256 && b.code!=VK_ESCAPE && !(b.code>=VK_F1 && b.code<=VK_F12);
            if(device==0 && b.kind==BindingKind::Axis)valid=b.code && b.code>=-6 && b.code<=6;
            if(device==0 && b.kind==BindingKind::Button)valid=b.code>0 && !(b.code&(b.code-1)) && !(b.code&~0xF3FF);
            if(!valid){error="Asignacion de control invalida.";return false;}
        }
    }
    return true;
}

using json=nlohmann::json;
static json bindings_json(const Bindings& bindings) {
    json out=json::array();for(const auto& action:bindings){json row=json::array();for(const auto& b:action)row.push_back({int(b.kind),b.code});out.push_back(row);}return out;
}
static Bindings parse_bindings(const json& rows) {
    if(!rows.is_array() || rows.size()!=action_count)throw std::runtime_error("Numero de acciones invalido.");
    Bindings out{};for(size_t a=0;a<action_count;++a) {
        if(!rows[a].is_array() || rows[a].size()!=binding_count)throw std::runtime_error("Numero de bindings invalido.");
        for(size_t b=0;b<binding_count;++b){const auto& item=rows[a][b];
            if(!item.is_array() || item.size()!=2 || !item[0].is_number_integer() || !item[1].is_number_integer())throw std::runtime_error("Binding invalido.");
            const auto kind=item[0].get<int64_t>(),code=item[1].get<int64_t>();
            if(kind<0 || kind>3 || code<-65535 || code>65535)throw std::runtime_error("Binding fuera de rango.");
            out[a][b]={BindingKind(kind),int(code)};
        }
    }return out;
}
bool load_controls(const std::filesystem::path& path,ControlsConfig& out,std::string& error) {
    try {
        if(std::filesystem::file_size(path)>256*1024)throw std::runtime_error("controls.json demasiado grande.");
        std::ifstream file(path);auto root=json::parse(file);
        if(!root.at("version").is_number_integer() || root.at("version")!=1 || !root.at("players").is_array() || root.at("players").size()!=ports)throw std::runtime_error("Version o jugadores incompatibles.");
        ControlsConfig config;
        for(size_t i=0;i<ports;++i) {
            const auto& j=root.at("players").at(i);auto& c=config[i];auto& p=c.profile;
            if(!j.at("slot").is_number_integer() || !j.at("deadzone").is_number_integer())throw std::runtime_error("Jugador o zona muerta invalida.");
            const auto slot=j.at("slot").get<int64_t>(),dz=j.at("deadzone").get<int64_t>();
            if(slot<-1 || slot>=int(ports) || dz<0 || dz>50)throw std::runtime_error("Jugador fuera de rango.");
            c.slot=int(slot);c.keyboard=j.at("keyboard").get<bool>();p.name=j.at("name").get<std::string>();p.deadzone=int(dz);
            p.invert_x=j.at("invert_x").get<bool>();p.invert_y=j.at("invert_y").get<bool>();
            p.gamepad=parse_bindings(j.at("gamepad"));p.keyboard=parse_bindings(j.at("keys"));
        }
        if(!valid_controls(config,error))return false;out=std::move(config);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool save_controls(const std::filesystem::path& path,const ControlsConfig& config,std::string& error) {
    if(!valid_controls(config,error))return false;
    auto temporary=path;temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    try {
        json root={{"version",1},{"players",json::array()}};
        for(const auto& c:config){const auto& p=c.profile;root["players"].push_back({{"slot",c.slot},{"keyboard",c.keyboard},{"name",p.name},
            {"deadzone",p.deadzone},{"invert_x",p.invert_x},{"invert_y",p.invert_y},{"gamepad",bindings_json(p.gamepad)},{"keys",bindings_json(p.keyboard)}});}
        {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file.exceptions(std::ios::badbit|std::ios::failbit);file<<root.dump(2)<<'\n';file.close();}
        if(std::filesystem::exists(path)) {auto backup=path;backup+=L".bak";if(!CopyFileW(path.c_str(),backup.c_str(),FALSE))throw std::runtime_error("No se pudo respaldar controls.json.");}
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("No se pudo guardar controls.json.");
        return true;
    }catch(const std::exception& e){std::error_code ignored;std::filesystem::remove(temporary,ignored);error=e.what();return false;}
}
}

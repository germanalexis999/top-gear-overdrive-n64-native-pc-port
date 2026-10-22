#include "controller_mapper.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <atomic>
#include <sstream>

namespace tgod::controls {
using namespace input;
namespace {
std::mutex mutex;
ControlsConfig config=default_controls();
RawPads raw{};
Keys keys{};
Snapshot current{};
std::filesystem::path config_path;
bool editing=false,release_gate=false;
std::array<bool,ports> connected_before{};
static_assert(sizeof(Gamepad)==12, "XInput gamepad ABI changed");
using GetState=DWORD(WINAPI*)(DWORD,void*);
GetState get_state=nullptr;
std::once_flag xinput_once;
std::atomic<HWND> panel{nullptr};
HWND owner_window=nullptr;
ControlsConfig draft;
int player=0,device=0,capture=-1;
bool capture_armed=false;
std::chrono::steady_clock::time_point capture_deadline;
std::string load_warning;
constexpr int id_player=100,id_slot=101,id_keyboard=102,id_device=103,id_actions=104,
    id_name=105,id_deadzone=106,id_invert_x=107,id_invert_y=108,id_reset=109,
    id_bind=120,id_clear=130,id_status=140,id_preview=141,id_save=150,id_cancel=151;
constexpr const wchar_t* action_names[]{L"Acelerar (A)",L"Frenar (B)",L"Freno de mano (Z)",L"Marcha abajo (L)",L"Marcha arriba (R)",L"Start",
    L"Bocina / mapa (C arriba)",L"Camara (C abajo)",L"Vista trasera (C izquierda)",L"Nitro (C derecha)",
    L"Menu arriba",L"Menu abajo",L"Menu izquierda",L"Menu derecha",L"Direccion izquierda",L"Direccion derecha",L"Stick arriba",L"Stick abajo"};

std::wstring widen(const std::string& text) {
    if(text.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(!n)return L"Perfil";std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),out.data(),n);return out;
}
std::string narrow(const std::wstring& text) {
    if(text.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,' ');WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),n,nullptr,nullptr);return out;
}
HWND item(int id){return GetDlgItem(panel,id);}
void text(int id,const std::wstring& value){SetWindowTextW(item(id),value.c_str());}
int selection(int id){return int(SendMessage(item(id),CB_GETCURSEL,0,0));}
bool checked(int id){return SendMessage(item(id),BM_GETCHECK,0,0)==BST_CHECKED;}
void check(int id,bool value){SendMessage(item(id),BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);}

std::wstring binding_name(const Binding& b) {
    if(b.kind==BindingKind::None)return L"Sin asignar";
    if(b.kind==BindingKind::Button) {
        constexpr int codes[]{1,2,4,8,0x10,0x20,0x40,0x80,0x100,0x200,0x1000,0x2000,0x4000,0x8000};
        constexpr const wchar_t* names[]{L"Cruceta arriba",L"Cruceta abajo",L"Cruceta izquierda",L"Cruceta derecha",L"Start",L"Back",L"Click stick izq.",L"Click stick der.",L"LB",L"RB",L"A",L"B",L"X",L"Y"};
        for(size_t i=0;i<std::size(codes);++i)if(b.code==codes[i])return names[i];
    }
    if(b.kind==BindingKind::Axis) {
        constexpr const wchar_t* names[]{L"Stick izq. X",L"Stick izq. Y",L"Stick der. X",L"Stick der. Y",L"LT",L"RT"};
        if(b.code && std::abs(b.code)<=6)return std::wstring(names[std::abs(b.code)-1])+(b.code<0?L" -":L" +");
    }
    if(b.kind==BindingKind::Key) {
        wchar_t name[80]{};UINT scan=MapVirtualKeyW(UINT(b.code),MAPVK_VK_TO_VSC);
        if(b.code==VK_LEFT || b.code==VK_RIGHT || b.code==VK_UP || b.code==VK_DOWN || b.code==VK_INSERT || b.code==VK_DELETE || b.code==VK_HOME || b.code==VK_END || b.code==VK_PRIOR || b.code==VK_NEXT || b.code==VK_DIVIDE)scan|=0x100;
        if(GetKeyNameTextW(LONG(scan<<16),name,80))return name;
        return L"Tecla "+std::to_wstring(b.code);
    }
    return L"?";
}
Bindings& bindings(){return device==0?draft[player].profile.gamepad:draft[player].profile.keyboard;}
int selected_action(){return int(SendMessage(item(id_actions),LB_GETCURSEL,0,0));}
void refresh_bindings() {
    int a=selected_action();if(a<0 || a>=int(action_count))a=0;
    SendMessage(item(id_actions),LB_RESETCONTENT,0,0);
    for(size_t i=0;i<action_count;++i) {
        std::wstring row=action_names[i];row+=L"    ";
        for(size_t b=0;b<binding_count;++b){if(b)row+=L" / ";row+=binding_name(bindings()[i][b]);}
        SendMessageW(item(id_actions),LB_ADDSTRING,0,LPARAM(row.c_str()));
    }
    SendMessage(item(id_actions),LB_SETCURSEL,a,0);
    for(size_t b=0;b<binding_count;++b)text(id_bind+int(b),binding_name(bindings()[a][b]));
}
void capture_stop(const wchar_t* message) {
    capture=-1;capture_armed=false;
    for(int id:{id_player,id_slot,id_keyboard,id_device,id_actions,id_name,id_deadzone,id_invert_x,id_invert_y,id_reset,id_save})EnableWindow(item(id),TRUE);
    for(size_t b=0;b<binding_count;++b){EnableWindow(item(id_bind+int(b)),TRUE);EnableWindow(item(id_clear+int(b)),TRUE);}
    text(id_status,message);refresh_bindings();
}
bool read_player_fields() {
    wchar_t name[128]{},number[20]{};GetWindowTextW(item(id_name),name,128);GetWindowTextW(item(id_deadzone),number,20);
    wchar_t* end=nullptr;const long dz=wcstol(number,&end,10);
    const auto profile_name=narrow(name);
    if(!*number || *end || dz<0 || dz>50 || profile_name.empty() || profile_name.size()>96) {
        MessageBoxW(panel,L"Usa un nombre de perfil y una zona muerta entre 0 y 50%.",L"Controles",MB_OK|MB_ICONWARNING);return false;
    }
    auto& c=draft[player];c.profile.name=profile_name;c.profile.deadzone=int(dz);
    c.profile.invert_x=checked(id_invert_x);c.profile.invert_y=checked(id_invert_y);
    return true;
}
void refresh_player() {
    const auto& c=draft[player];SendMessage(item(id_player),CB_SETCURSEL,player,0);
    SendMessage(item(id_slot),CB_SETCURSEL,c.slot+1,0);check(id_keyboard,c.keyboard);
    text(id_name,widen(c.profile.name));text(id_deadzone,std::to_wstring(c.profile.deadzone));
    check(id_invert_x,c.profile.invert_x);check(id_invert_y,c.profile.invert_y);refresh_bindings();
}

// All keys must be released before returning input to the game. Ignore menu
// shortcuts and lock keys, which must not leave the release gate latched.
bool held(const Keys& k) {
    for(int i=8;i<255;++i)if(k[i] && i!=VK_CAPITAL && i!=VK_NUMLOCK && i!=VK_SCROLL)return true;
    return false;
}
bool pad_held(const Gamepad& p) {
    return p.buttons || p.left_trigger>30 || p.right_trigger>30 ||
        std::abs(int(p.lx))>18000 || std::abs(int(p.ly))>18000 || std::abs(int(p.rx))>18000 || std::abs(int(p.ry))>18000;
}
Binding capture_binding(const Gamepad& p,const Keys& k) {
    if(device==1) {
        for(int i=8;i<255;++i) {
            if(i==VK_SHIFT || i==VK_CONTROL || i==VK_MENU || i==VK_ESCAPE || (i>=VK_F1 && i<=VK_F12))continue;
            if(k[i])return {BindingKind::Key,i};
        }
    } else {
        if(p.buttons)for(int bit=0;bit<16;++bit)if((p.buttons&(1<<bit)) && ((1<<bit)&0xF3FF))return {BindingKind::Button,1<<bit};
        const int axes[]{p.lx,p.ly,p.rx,p.ry};
        int largest=0;for(int a=1;a<4;++a)if(std::abs(axes[a])>std::abs(axes[largest]))largest=a;
        if(std::abs(axes[largest])>18000)return {BindingKind::Axis,(largest+1)*(axes[largest]<0?-1:1)};
        if(p.left_trigger>120)return {BindingKind::Axis,5};if(p.right_trigger>120)return {BindingKind::Axis,6};
    }
    return {};
}
void timer() {
    poll();RawPads pads;Keys k;
    {std::lock_guard lock(mutex);pads=raw;k=keys;}
    if(capture<0) {
        BOOL valid=FALSE;const UINT dz=GetDlgItemInt(panel,id_deadzone,&valid,FALSE);
        if(valid && dz<=50)draft[player].profile.deadzone=int(dz);
        draft[player].profile.invert_x=checked(id_invert_x);draft[player].profile.invert_y=checked(id_invert_y);
    }
    std::wstring status=L"Detectados: ";for(size_t i=0;i<ports;++i)status+=L"XInput "+std::to_wstring(i+1)+(pads[i].connected?L" conectado   ":L" ausente   ");
    const auto slot=draft[player].slot;const Gamepad pad=slot>=0 && pads[slot].connected?pads[slot].pad:Gamepad{};
    status+=L"\nMando: ";bool any=false;
    for(int bit=0;bit<16;++bit)if(pad.buttons&(1<<bit)){status+=binding_name({BindingKind::Button,1<<bit})+L" ";any=true;}
    if(!any)status+=L"sin botones pulsados";
    status+=L" | LT "+std::to_wstring(pad.left_trigger*100/255)+L"%  RT "+std::to_wstring(pad.right_trigger*100/255)+L"%";
    const auto preview=map_controls(draft,pads,k)[player];
    std::wostringstream axes;axes.setf(std::ios::fixed);axes.precision(2);
    axes<<L"\nDireccion del perfil: X "<<preview.x<<L"  Y "<<preview.y;status+=axes.str();
    text(id_preview,status);
    if(capture<0)return;
    if(std::chrono::steady_clock::now()>capture_deadline){capture_stop(L"Captura cancelada por tiempo. No se cambio el binding.");return;}
    if(device==0 && (slot<0 || !pads[slot].connected)) {capture_stop(L"Conecta el mando asignado a este jugador antes de capturar.");return;}
    const bool active=device==0?pad_held(pad):held(k);
    if(!capture_armed){if(!active)capture_armed=true;return;}
    const auto binding=capture_binding(pad,k);
    if(binding.kind!=BindingKind::None) {
        bindings()[selected_action()][capture]=binding;
        capture_stop(L"Asignado en la copia temporal. Usa Guardar para aplicar.");
    }
}
HWND control(const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id) {
    HWND child=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,panel,HMENU(INT_PTR(id)),GetModuleHandle(nullptr),nullptr);
    SendMessage(child,WM_SETFONT,WPARAM(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return child;
}
void build_panel() {
    control(L"STATIC",L"Jugador",0,16,14,70,20,0);
    control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,90,10,130,150,id_player);
    for(int i=0;i<4;++i){const auto s=L"Jugador "+std::to_wstring(i+1);SendMessageW(item(id_player),CB_ADDSTRING,0,LPARAM(s.c_str()));}
    control(L"STATIC",L"Mando",0,240,14,55,20,0);
    control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,295,10,170,160,id_slot);
    SendMessageW(item(id_slot),CB_ADDSTRING,0,LPARAM(L"Ninguno"));
    for(int i=0;i<4;++i){const auto s=L"XInput "+std::to_wstring(i+1);SendMessageW(item(id_slot),CB_ADDSTRING,0,LPARAM(s.c_str()));}
    control(L"BUTTON",L"Compartir teclado",BS_AUTOCHECKBOX|WS_TABSTOP,490,10,180,24,id_keyboard);
    control(L"STATIC",L"Perfil",0,16,48,70,20,0);
    control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,90,44,210,24,id_name);SendMessage(item(id_name),EM_SETLIMITTEXT,64,0);
    control(L"STATIC",L"Zona muerta % (0-50)",0,320,48,145,20,0);
    control(L"EDIT",L"",WS_BORDER|ES_NUMBER|WS_TABSTOP,465,44,45,24,id_deadzone);SendMessage(item(id_deadzone),EM_SETLIMITTEXT,2,0);
    control(L"BUTTON",L"Invertir X",BS_AUTOCHECKBOX|WS_TABSTOP,530,44,90,24,id_invert_x);
    control(L"BUTTON",L"Invertir Y",BS_AUTOCHECKBOX|WS_TABSTOP,635,44,90,24,id_invert_y);
    control(L"STATIC",L"Configurar",0,16,82,74,20,0);
    control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,90,78,150,100,id_device);
    for(const auto* s:{L"Mando",L"Teclado"})SendMessageW(item(id_device),CB_ADDSTRING,0,LPARAM(s));SendMessage(item(id_device),CB_SETCURSEL,0,0);
    control(L"STATIC",L"Elige una accion y pulsa una asignacion para reemplazarla.",0,255,82,480,20,0);
    control(L"LISTBOX",L"",LBS_NOTIFY|WS_BORDER|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,16,112,720,286,id_actions);
    SendMessage(item(id_actions),LB_SETHORIZONTALEXTENT,1100,0);
    for(size_t i=0;i<binding_count;++i) {
        const int x=16+int(i)*240;
        control(L"BUTTON",L"Asignar",BS_PUSHBUTTON|WS_TABSTOP,x,408,166,30,id_bind+int(i));
        control(L"BUTTON",L"Borrar",BS_PUSHBUTTON|WS_TABSTOP,x+170,408,62,30,id_clear+int(i));
    }
    control(L"STATIC",L"",0,16,450,720,62,id_preview);
    control(L"STATIC",L"",0,16,520,720,38,id_status);
    control(L"BUTTON",L"Restaurar jugador",BS_PUSHBUTTON|WS_TABSTOP,16,566,160,32,id_reset);
    control(L"BUTTON",L"Guardar y cerrar",BS_DEFPUSHBUTTON|WS_TABSTOP,440,566,150,32,id_save);
    control(L"BUTTON",L"Cancelar",BS_PUSHBUTTON|WS_TABSTOP,605,566,130,32,id_cancel);
    refresh_player();text(id_status,load_warning.empty()?L"F7 / Escape: cancelar. Las teclas F1-F12 estan reservadas. El teclado pertenece a un solo jugador.":widen(load_warning));
    SetTimer(panel,1,30,nullptr);SetFocus(item(id_player));
}
LRESULT CALLBACK panel_proc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case DM_GETDEFID:return MAKELRESULT(id_save,DC_HASDEFID);
    case WM_CREATE:panel=hwnd;build_panel();return 0;
    case WM_TIMER:timer();return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1);panel=nullptr;
        {std::lock_guard lock(mutex);editing=false;release_gate=true;}
        if(IsWindow(owner_window)){EnableWindow(owner_window,TRUE);SetForegroundWindow(owner_window);}return 0;
    case WM_COMMAND: {
        const int id=LOWORD(w),notification=HIWORD(w);
        if(id==id_cancel || id==IDCANCEL){if(capture>=0)capture_stop(L"Captura cancelada.");else DestroyWindow(hwnd);return 0;}
        if(id==id_player && notification==CBN_SELCHANGE) {
            if(read_player_fields()){player=selection(id_player);refresh_player();}else SendMessage(item(id_player),CB_SETCURSEL,player,0);return 0;
        }
        if(id==id_slot && notification==CBN_SELCHANGE) {
            const int next=selection(id_slot)-1,previous=draft[player].slot;
            for(size_t i=0;i<ports;++i)if(int(i)!=player && next>=0 && draft[i].slot==next)draft[i].slot=previous;
            draft[player].slot=next;text(id_status,L"Si el mando pertenecia a otro jugador, se intercambiaron las asignaciones.");return 0;
        }
        if(id==id_keyboard && notification==BN_CLICKED) {
            const bool enabled=checked(id_keyboard);if(enabled)for(auto& c:draft)c.keyboard=false;draft[player].keyboard=enabled;return 0;
        }
        if(id==id_device && notification==CBN_SELCHANGE){device=selection(id_device);refresh_bindings();return 0;}
        if(id==id_actions && notification==LBN_SELCHANGE){refresh_bindings();return 0;}
        if(id>=id_bind && id<id_bind+int(binding_count) && notification==BN_CLICKED) {
            if(!read_player_fields())return 0;capture=id-id_bind;capture_armed=false;capture_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            for(int field:{id_player,id_slot,id_keyboard,id_device,id_actions,id_name,id_deadzone,id_invert_x,id_invert_y,id_reset,id_save})EnableWindow(item(field),FALSE);
            for(size_t b=0;b<binding_count;++b){EnableWindow(item(id_bind+int(b)),FALSE);EnableWindow(item(id_clear+int(b)),FALSE);}
            text(id_status,L"Suelta los controles y pulsa un boton, tecla o mueve un eje. Escape cancela (15 segundos).");return 0;
        }
        if(id>=id_clear && id<id_clear+int(binding_count) && notification==BN_CLICKED) {
            const int a=selected_action();if(a>=0)bindings()[a][id-id_clear]={};refresh_bindings();return 0;
        }
        if(id==id_reset && notification==BN_CLICKED) {
            draft[player].profile=default_controls()[player].profile;refresh_player();text(id_status,L"Perfil restaurado en la copia temporal. Usa Guardar para aplicar.");return 0;
        }
        if(id==id_save && notification==BN_CLICKED) {
            if(!read_player_fields())return 0;std::string error;
            if(!save_controls(config_path,draft,error)){MessageBoxW(hwnd,widen(error).c_str(),L"No se pudo guardar",MB_OK|MB_ICONERROR);return 0;}
            {std::lock_guard lock(mutex);config=draft;}
            load_warning.clear();std::cout<<"[Controls] Saved controls.json; player profiles applied.\n";DestroyWindow(hwnd);return 0;
        }
        return 0;
    }
    }return DefWindowProcW(hwnd,message,w,l);
}
}

void initialize(const std::filesystem::path& directory) {
    config_path=directory/L"controls.json";std::error_code error;
    if(std::filesystem::exists(config_path,error)) {
        std::string reason;if(!load_controls(config_path,config,reason)) {
            load_warning="No se pudo cargar controls.json: "+reason+" Se usan los valores iniciales.";
            std::cerr<<"[Controls] "<<load_warning<<'\n';
        }
    }
}
void set_game_window(HWND window){owner_window=window;}
void poll() {
    std::lock_guard lock(mutex);
    static auto last=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();if(now-last<std::chrono::milliseconds(4))return;last=now;
    std::call_once(xinput_once,[]{
        for(const auto* dll:{L"xinput1_4.dll",L"xinput9_1_0.dll",L"xinput1_3.dll"})if(auto module=LoadLibraryW(dll)) {
            get_state=reinterpret_cast<GetState>(GetProcAddress(module,"XInputGetState"));if(get_state)break;
        }
        std::cout<<"[Controls] XInput "<<(get_state?"ready; four slots available.":"unavailable; keyboard remains enabled.")<<'\n';
    });
    static std::array<std::chrono::steady_clock::time_point,ports> retry{};
    for(size_t i=0;i<ports;++i) {
        struct {DWORD packet;Gamepad pad;} state{};bool connected=false;
        if(connected_before[i] || now>=retry[i]) {
            connected=get_state && get_state(DWORD(i),&state)==ERROR_SUCCESS;retry[i]=now+std::chrono::milliseconds(250);
        }
        raw[i]={connected,state.pad};
        if(connected!=connected_before[i])std::cout<<"[Input] XInput "<<i+1<<(connected?" connected.":" disconnected.")<<'\n';
        connected_before[i]=connected;
    }
    const HWND foreground=GetForegroundWindow();const bool keyboard_focus=foreground==owner_window || foreground==panel;
    for(int k=0;k<256;++k)keys[k]=keyboard_focus && (GetAsyncKeyState(k)&0x8000);
    current=map_controls(config,raw,keys);
    if(release_gate) {
        bool active=held(keys);for(const auto& r:raw)if(r.connected)active|=pad_held(r.pad);
        if(!active)release_gate=false;
    }
}
Snapshot snapshot() {
    std::lock_guard lock(mutex);auto out=current;
    if(editing || release_gate)for(auto& s:out){s.buttons=0;s.x=s.y=0;}
    return out;
}
void show_mapper(HWND owner) {
    if(panel){SetForegroundWindow(panel);return;}
    {std::lock_guard lock(mutex);draft=config;editing=true;}
    player=0;device=0;capture=-1;
    WNDCLASSW cls{};cls.lpfnWndProc=panel_proc;cls.hInstance=GetModuleHandle(nullptr);cls.hCursor=LoadCursor(nullptr,IDC_ARROW);
    cls.hbrBackground=HBRUSH(COLOR_BTNFACE+1);cls.lpszClassName=L"TGOD_CONTROLLER_MAPPER";RegisterClassW(&cls);
    RECT r{0,0,752,614};AdjustWindowRectEx(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_CONTROLPARENT|WS_EX_DLGMODALFRAME);
    HWND created=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_DLGMODALFRAME,cls.lpszClassName,L"Top Gear Overdrive - Controles (XInput)",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,owner,nullptr,cls.hInstance,nullptr);
    if(!created){std::lock_guard lock(mutex);editing=false;std::cerr<<"[Controls] Could not create mapper window.\n";return;}
    EnableWindow(owner,FALSE);ShowWindow(created,SW_SHOW);SetForegroundWindow(created);
}
void close_mapper(){if(panel)DestroyWindow(panel);}
bool handle_message(MSG& message) {
    if(!panel)return false;
    if(message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN) {
        if(message.wParam==VK_ESCAPE){if(capture>=0)capture_stop(L"Captura cancelada.");else close_mapper();return true;}
        if(message.wParam==VK_F7){if(!(message.lParam&(1ll<<30)))close_mapper();return true;}
        if(message.wParam==VK_F11 || (message.wParam==VK_RETURN && (message.lParam&(1ll<<29)))) {
            SendMessage(owner_window,message.message,message.wParam,message.lParam);return true;
        }
    }
    if(capture>=0 && (message.message==WM_KEYDOWN || message.message==WM_KEYUP || message.message==WM_SYSKEYDOWN || message.message==WM_SYSKEYUP || message.message==WM_CHAR))return true;
    return IsDialogMessageW(panel,&message)!=FALSE;
}
}

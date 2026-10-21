#include "gpu_raster.hpp"
#include "f3dex2.hpp"
#include <vector>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

static void presentation_checks(GPURaster& gpu) {
    HWND window=CreateWindowExW(0,L"STATIC",L"GPU regression",WS_POPUP,0,0,128,96,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    require(window!=nullptr,"Create hidden presentation test window");
    std::vector<uint32_t> output;uint32_t w=0,h=0;
    auto capture=[&]{require(gpu.capture_output(output,w,h),"Capture conversion output");};
    auto red=[](uint32_t pixel){return (pixel&255)>=250&&((pixel>>8)&65535)==0;};
    auto blue=[](uint32_t pixel){return ((pixel>>16)&255)>=250&&(pixel&65535)==0;};
    std::vector<uint16_t> native(320*240,0xF801);
    gpu.present_native(window,native,320,240,false);capture();
    require(w==128&&h==96&&red(output[size_t(h/2)*w+w/2]),"Native RGB conversion");
    SetWindowPos(window,nullptr,0,0,200,100,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    gpu.present_native(window,native,320,240,true);capture();
    require(w==200&&h==100&&red(output[50*w+100])&&(output[50*w]&0xFFFFFF)==0,"Resize and 4:3 pillarbox");
    SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    require(!gpu.present_native(window,native,320,240,true),"Zero-sized window must skip presentation");
    SetWindowPos(window,nullptr,0,0,128,96,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    HWND minimized=CreateWindowExW(0,L"STATIC",L"Minimized GPU check",WS_POPUP|WS_MINIMIZE,
        0,0,128,96,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    require(minimized&&IsIconic(minimized),"Create hidden minimized test window");
    require(!gpu.present_native(minimized,native,320,240,true),"Minimized window must skip presentation");
    DestroyWindow(minimized);

    for(uint32_t height:{720u,1080u,2160u}) {
        uint64_t allocations=0,inputs=0;
        for(int frame=0;frame<2;++frame){
            std::vector<uint8_t> ram(8*1024*1024);
            F3DEX2Interpreter r(ram.data());r.diagnostics_enabled=false;r.color_image_addr=0x100000;
            r.texture_enabled=false;r.render_surfaces.set_height(height);
            r.raster_backend=&gpu;
            auto* fb=reinterpret_cast<uint16_t*>(ram.data()+r.color_image_addr);
            std::fill(fb,fb+320*240,frame==0 ? 0xF801:0x003F);
            gpu.begin_frame(true);
            Vertex3D a,b,c;a.screen_x=200.125f;a.screen_y=200.125f;b.screen_x=220.3f;b.screen_y=200.125f;c.screen_x=200.125f;c.screen_y=220.7f;
            r.rasterize_triangle_screen(a,b,c);const auto before=gpu.statistics();
            gpu.finish(r);
            require(gpu.statistics().readback_bytes==before.readback_bytes&&gpu.statistics().final_readbacks==before.final_readbacks,"Resident finish must not read back final surfaces");
            gpu.present_target(window,r.color_image_addr,320,240,0,false);capture();
            std::cout<<"[DXGI check] "<<height<<"p frame "<<frame<<", output "<<w<<'x'<<h<<", sample "<<std::hex<<output[10*w+10]<<std::dec<<", target "<<gpu.has_target(r.color_image_addr,320)<<'\n';
            require(frame==0 ? red(output[10*w+10]):blue(output[10*w+10]),"Reused surface imports fresh snapshot contents");
            if(frame==0){allocations=gpu.statistics().surface_allocations;inputs=gpu.statistics().input_allocations;}
            else require(allocations==gpu.statistics().surface_allocations&&inputs==gpu.statistics().input_allocations,"Stable frames must reuse surfaces and input resources");
            if(frame==1){
                gpu.readback(r,r.color_image_addr,320*240*2);
                std::vector<uint16_t> scanout;uint32_t sw=0,sh=0;
                require(r.render_surfaces.copy_scanout(r.color_image_addr,320,240,scanout,sw,sh) && sw==height*4/3 && sh==height,"HD/4K feedback dimensions");
                require(gpu.statistics().readback_bytes>before.readback_bytes,"Explicit framebuffer feedback still reads back");
            }
        }
    }
    {
        std::vector<uint8_t> ram(8*1024*1024);F3DEX2Interpreter r(ram.data());
        r.diagnostics_enabled=false;r.color_image_addr=0x100000;r.texture_enabled=false;
        r.render_surfaces.set_height(1080);r.raster_backend=&gpu;
        auto* fb=reinterpret_cast<uint16_t*>(ram.data()+r.color_image_addr);
        std::fill(fb,fb+320*20,0xF801);std::fill(fb+320*20,fb+320*240,0x003F);
        gpu.begin_frame(true);
        Vertex3D a,b,c;a.screen_x=300.125f;a.screen_y=230.125f;b.screen_x=310.3f;b.screen_y=230.125f;c.screen_x=300.125f;c.screen_y=235.7f;
        r.rasterize_triangle_screen(a,b,c);gpu.finish(r);
        gpu.present_target(window,r.color_image_addr,320,240,0,false);capture();
        require(red(output[w/2]),"Uncropped GPU scanout origin");
        gpu.present_target(window,r.color_image_addr,320,220,320*20*2,false);capture();
        require(blue(output[w/2]),"VI scanout row offset and cropped height");
    }
    SetWindowPos(window,nullptr,0,0,8,8,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
    native={0xF801,0x07C1,0x003F,0xFFFF};
    gpu.present_native(window,native,2,2,false);capture();const auto point=output[3*w+3];
    gpu.present_native(window,native,2,2,true);capture();const auto smooth=output[3*w+3];
    require(red(point)&&smooth!=point&&(smooth&255)>0&&((smooth>>8)&255)>0&&((smooth>>16)&255)>0,"Point and smooth output filters");
    gpu.release_presentation();
    gpu.present_native(window,native,2,2,false);capture();
    require(w==8&&h==8&&red(output[3*w+3]),"Swapchain release and recreation");
    const auto stats=gpu.statistics();
    gpu.release_presentation();DestroyWindow(window);
    std::cout<<"PASS: DXGI conversion, resize, 4:3, zero/minimized window, native/720p/1080p/4K, filters, resource reuse and feedback; presents "<<stats.presents<<", resizes "<<stats.resize_count<<".\n";
}
int main(){
    std::cout<<std::unitbuf; GPURaster gpu;if(!gpu.available())return 77;
    for(uint32_t height:{720u,1080u}){
        auto compare=[&](const char* name,const std::function<void(F3DEX2Interpreter&)>& draw,uint32_t native_width=320){
            std::vector<uint8_t> a(8*1024*1024),b(a.size());
            F3DEX2Interpreter cpu(a.data()),hw(b.data());
            cpu.diagnostics_enabled=hw.diagnostics_enabled=false;
            cpu.color_image_addr=hw.color_image_addr=0x100000;
            cpu.depth_image_addr=hw.depth_image_addr=0x180000;
            cpu.color_image_width=hw.color_image_width=native_width;
            cpu.scissor_xl=hw.scissor_xl=native_width;
            cpu.scissor_yl=hw.scissor_yl=native_width==640 ? 480:240;
            cpu.render_surfaces.set_height(height);hw.render_surfaces.set_height(height);
            gpu.begin_frame();hw.raster_backend=&gpu;draw(cpu);const auto authoritative=a;
            draw(hw);gpu.readback(hw,0,8*1024*1024);
            if(a!=authoritative)throw std::runtime_error("GPU changed external reference RAM");
            std::vector<uint16_t> ca,cb;uint32_t w,h,gw,gh;
            cpu.render_surfaces.copy_scanout(cpu.color_image_addr,native_width,cpu.framebuffer_height(),ca,w,h);
            hw.render_surfaces.copy_scanout(hw.color_image_addr,native_width,hw.framebuffer_height(),cb,gw,gh);
            if(w!=gw||h!=gh||ca.empty()||ca.size()!=cb.size())throw std::runtime_error("Missing GPU surface");
            size_t differences=0,significant=0;for(size_t i=0;i<ca.size();++i){differences+=ca[i]!=cb[i];bool bad=false;for(int shift:{1,6,11})bad|=std::abs(int((ca[i]>>shift)&31)-int((cb[i]>>shift)&31))>1;significant+=bad;}
            std::cout<<"[GPU check] "<<name<<" "<<height<<"p different pixels: "<<differences<<'/'<<ca.size()<<'\n';
            std::cout<<" significant: "<<significant<<"\n";if(significant){for(size_t i=0,shown=0;i<ca.size() && shown<8;++i)if(std::abs(int((ca[i]>>11)&31)-int((cb[i]>>11)&31))>1){std::cout<<"  pixel "<<i%w<<","<<i/w<<" CPU "<<ca[i]<<" GPU "<<cb[i]<<"\n";++shown;}throw std::runtime_error(name);}
            // Only private replay RAM is writable. CPU reference writes are compared separately above.
        };
        compare("triangle opaque",[](F3DEX2Interpreter& r){
            r.texture_enabled=false;
            Vertex3D a,b,c;a.screen_x=20.125f;a.screen_y=20.125f;b.screen_x=107.3f;b.screen_y=20.125f;c.screen_x=20.125f;c.screen_y=103.7f;
            a.r=b.r=c.r=200;a.g=b.g=c.g=100;a.b=b.b=c.b=40;
            r.rasterize_triangle_screen(a,b,c);
        });
        compare("depth blend and ordered overlap",[](F3DEX2Interpreter& r){
            auto z=r.render_buffer(r.depth_image_addr);std::fill(z.pixels,z.pixels+size_t(z.width)*z.height,65535);
            r.texture_enabled=false;r.geometry_mode=1;r.other_mode_l=0x00404030;
            Vertex3D a,b,c;a.screen_x=20.125f;a.screen_y=20.125f;b.screen_x=107.3f;b.screen_y=20.125f;c.screen_x=20.125f;c.screen_y=103.7f;
            a.screen_z=b.screen_z=c.screen_z=300;a.a=b.a=c.a=128;r.rasterize_triangle_screen(a,b,c);
            a.r=b.r=c.r=0;a.screen_z=b.screen_z=c.screen_z=500;r.rasterize_triangle_screen(a,b,c);
            a.screen_z=b.screen_z=c.screen_z=100;r.rasterize_triangle_screen(a,b,c);
        });
        compare("COPY strip mask 15",[](F3DEX2Interpreter& r){
            r.other_mode_h=2<<20;r.tiles[0].size=2;r.tiles[0].line=80;r.tiles[0].lrs=319;r.tiles[0].lrt=5;r.tiles[0].mask_s=r.tiles[0].mask_t=15;
            for(size_t i=0;i<4096;i+=2){r.tmem[i]=0xF8;r.tmem[i+1]=1;}
            uint32_t dl[]={0xE4000000u|(319u<<14)|(5u<<2),0,0xE1000000,0,0xF1000000,4096u<<16|1024,0xDF000000,0};
            std::memcpy(r.rdram+0x1000,dl,sizeof(dl));r.process_display_list(0x1000);
        });
        compare("line width and flat shade",[](F3DEX2Interpreter& r){
            r.geometry_mode=4;Vertex3D a,b;a.is_screen_space=b.is_screen_space=true;
            a.screen_x=22;a.screen_y=30;b.screen_x=110;b.screen_y=74;a.r=90;a.g=170;a.b=240;
            r.rasterize_line(a,b,3,a);
        });
        for (uint32_t format : {0u,2u,3u,4u}) for (uint32_t size : {0u,1u,2u,3u}) {
            // Includes the software renderer's generic 16-bit fallbacks.
            const auto name="filtered texture format "+std::to_string(format)+" size "+std::to_string(size);
            compare(name.c_str(),[=](F3DEX2Interpreter& r){
                r.other_mode_h=(1u<<19)|(2u<<12);
                auto& t=r.tiles[0];t.format=format;t.size=size;t.line=4;t.lrs=15;t.lrt=15;
                t.clamp_s=t.clamp_t=1;t.mask_s=t.mask_t=4;
                for(uint32_t i=0;i<4096;++i)r.tmem[i]=uint8_t((i*17+i/7)&255);
                Vertex3D a,b,c;a.screen_x=20.125f;a.screen_y=20.125f;b.screen_x=107.3f;b.screen_y=20.125f;c.screen_x=20.125f;c.screen_y=103.7f;
                // Constant fractional UV isolates format/filter behavior from
                // CPU/GPU ULP differences on exact texel/coverage boundaries.
                a.u=b.u=c.u=3.37f;a.v=b.v=c.v=4.19f;
                a.clip_w=0.7f;b.clip_w=1.3f;c.clip_w=0.9f;
                r.rasterize_triangle_screen(a,b,c);
            });
        }
        compare("two cycle LOD primitive and environment",[](F3DEX2Interpreter& r){
            r.other_mode_h=(1u<<20)|(1u<<16)|(1u<<19)|(2u<<12);r.texture_max_level=0;
            r.combine_mode_set=true;r.prim_color=0xC8A09080;r.env_color=0x20304050;
            r.combine_mode_w0=(3u<<5)|1;
            r.combine_mode_w1=(15u<<24)|(1u<<21)|(3u<<18)|(7u<<6)|(7u<<3)|7;
            r.tiles[0].format=4;r.tiles[0].size=1;r.tiles[0].line=1;r.tiles[0].lrs=7;r.tiles[0].lrt=7;
            std::fill(std::begin(r.tmem),std::end(r.tmem),160);
            Vertex3D a,b,c;a.screen_x=20.125f;a.screen_y=20.125f;b.screen_x=107.3f;b.screen_y=20.125f;c.screen_x=20.125f;c.screen_y=103.7f;
            a.u=b.u=c.u=3.37f;a.v=b.v=c.v=4.19f;
            r.rasterize_triangle_screen(a,b,c);
        });
        compare("640 pixel target",[](F3DEX2Interpreter& r){
            r.texture_enabled=false;
            Vertex3D a,b,c;a.screen_x=40.125f;a.screen_y=40.125f;b.screen_x=207.3f;b.screen_y=40.125f;c.screen_x=40.125f;c.screen_y=203.7f;
            a.r=b.r=c.r=200;a.g=b.g=c.g=100;a.b=b.b=c.b=40;
            r.rasterize_triangle_screen(a,b,c);
        },640);
        compare("ordered switches between 320 and 640 targets",[](F3DEX2Interpreter& r){
            r.texture_enabled=false;
            Vertex3D a,b,c;a.screen_x=20.125f;a.screen_y=20.125f;b.screen_x=107.3f;b.screen_y=20.125f;c.screen_x=20.125f;c.screen_y=103.7f;
            a.r=b.r=c.r=200;a.g=b.g=c.g=100;a.b=b.b=c.b=40;
            r.rasterize_triangle_screen(a,b,c);
            const auto address=r.color_image_addr;
            r.color_image_addr=0x300000;r.color_image_width=640;r.scissor_xl=640;r.scissor_yl=480;
            r.rasterize_triangle_screen(a,b,c);
            r.color_image_addr=address;r.color_image_width=320;r.scissor_xl=320;r.scissor_yl=240;
            a.screen_x+=130;b.screen_x+=130;c.screen_x+=130;
            r.rasterize_triangle_screen(a,b,c);
        });
    }
    std::cout<<"PASS: hardware GPU parity checks.\n";
    presentation_checks(gpu);
}





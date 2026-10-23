#include "gpu_raster.hpp"
#include "f3dex2.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include "gpu_shader.hpp"
#include <array>
#include <vector>
#include <map>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace {
template<class T> struct Com {
    T* p=nullptr;
    ~Com(){if(p)p->Release();}
    Com()=default;
    Com(const Com&)=delete;
    Com& operator=(const Com&)=delete;
    void reset(){if(p){p->Release();p=nullptr;}}
    T** out(){reset();return &p;}
    T* operator->()const{return p;}
};
void checked(HRESULT hr,const char* operation) {
    if(FAILED(hr)) throw std::runtime_error(std::string(operation)+" (HRESULT "+std::to_string(uint32_t(hr))+")");
}
using Command=std::array<uint32_t,256>;
void put_float(Command& c,int i,float value){std::memcpy(&c[i],&value,4);}
uint32_t rgba(const Vertex3D& v){return uint32_t(v.r)<<24|uint32_t(v.g)<<16|uint32_t(v.b)<<8|v.a;}
void vertex(Command& c,int at,const Vertex3D& v,bool perspective) {
    put_float(c,at,v.screen_x);put_float(c,at+1,v.screen_y);put_float(c,at+2,v.screen_z);
    put_float(c,at+3,perspective ? 1.0f/(std::abs(v.clip_w)>0.0001f ? std::abs(v.clip_w):1.0f) : 1.0f);
    put_float(c,at+4,v.u);put_float(c,at+5,v.v);c[at+6]=rgba(v);
}
}

struct GPURaster::Impl {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<ID3D11ComputeShader> shader;
    Com<ID3D11Buffer> constants,present_constants;
    Com<ID3D11ComputeShader> present_shader;
    Statistics stats;
    bool keep_on_gpu=false;
    uint64_t frame_id=0,use_clock=0;
    uint32_t cache_height=0;
    uint64_t content_clock=0,native_version=0,last_present_content=0;
    std::vector<uint16_t> native_cache;
    uint32_t native_width=0,native_height=0;
    std::array<uint32_t,16> last_present_parameters{};
    bool last_present_valid=false;
    struct Timing {
        Com<ID3D11Query> disjoint,start,end;
        bool pending=false;
        uint64_t frame=0;
    } timings[8];
    int active_timing=-1;
    bool timing_enabled=true;
    struct Input {
        Com<ID3D11Buffer> buffer;
        Com<ID3D11ShaderResourceView> view;
        uint32_t capacity=0;
    } inputs[4],native_input;
    std::vector<uint32_t> upload_scratch,spans,refs;
    std::vector<std::vector<uint32_t>> bins;
    Com<IDXGISwapChain1> swapchain;
    Com<ID3D11Texture2D> output,output_staging;
    Com<ID3D11UnorderedAccessView> output_view;
    HWND window=nullptr;
    uint32_t output_width=0,output_height=0;

    struct Surface {
        uint32_t address=0,native_width=0,native_height=0,width=0,height=0;
        Com<ID3D11Buffer> buffer,staging;
        Com<ID3D11UnorderedAccessView> uav;
        Com<ID3D11ShaderResourceView> srv;
        uint64_t frame=0,used=0,revision=0;
        bool dirty=false,written=false;
    };
    std::map<uint64_t,std::unique_ptr<Surface>> surfaces;
    Surface* color=nullptr;Surface* depth=nullptr;
    std::vector<Command> commands;
    std::vector<uint32_t> texture_words;
    std::array<uint8_t,4096> last_tmem{};
    bool have_tmem=false;
    uint32_t tmem_offset=0;
    float scale=1, scale_y=1;
    std::string name="unavailable";
    bool ready=false;
    void poll_timings() {
        if(!ready||!timing_enabled)return;
        for(auto& t:timings)if(t.pending){
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};UINT64 start=0,end=0;
            const UINT flags=D3D11_ASYNC_GETDATA_DONOTFLUSH;
            if(context->GetData(t.disjoint.p,&d,sizeof(d),flags)!=S_OK||
                context->GetData(t.start.p,&start,sizeof(start),flags)!=S_OK||
                context->GetData(t.end.p,&end,sizeof(end),flags)!=S_OK)continue;
            t.pending=false;
            if(!d.Disjoint&&d.Frequency&&end>=start){
                const uint64_t us=uint64_t((end-start)*1000000.0/d.Frequency);
                ++stats.gpu_samples;stats.gpu_elapsed_us+=us;stats.gpu_max_us=std::max(stats.gpu_max_us,us);
                if(us>100000){++stats.gpu_long_spans;if(!stats.gpu_first_long_frame)stats.gpu_first_long_frame=t.frame;}
            }
        }
    }
    void end_timing() {
        if(active_timing<0)return;
        auto& t=timings[active_timing];context->End(t.end.p);context->End(t.disjoint.p);
        t.pending=true;active_timing=-1;
    }
    void begin_timing() {
        end_timing();poll_timings();
        if(!timing_enabled)return;
        for(int i=0;i<8;++i)if(!timings[i].pending){
            auto& t=timings[i];
            if(!t.disjoint.p){
                D3D11_QUERY_DESC d{};d.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;
                if(FAILED(device->CreateQuery(&d,t.disjoint.out()))){timing_enabled=false;return;}
                d.Query=D3D11_QUERY_TIMESTAMP;
                if(FAILED(device->CreateQuery(&d,t.start.out()))||FAILED(device->CreateQuery(&d,t.end.out()))){timing_enabled=false;return;}
            }
            active_timing=i;t.frame=frame_id;context->Begin(t.disjoint.p);context->End(t.start.p);return;
        }
        ++stats.timing_skipped;
    }
    Impl() {
        try {
            D3D_FEATURE_LEVEL level;
            const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
            checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,1,
                D3D11_SDK_VERSION,device.out(),&level,context.out()),"Create hardware D3D11 device");
            Com<IDXGIDevice> dxdevice;Com<IDXGIAdapter> adapter;
            checked(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxdevice.out())),"Query DXGI device");
            checked(dxdevice->GetAdapter(adapter.out()),"Get GPU adapter");
            DXGI_ADAPTER_DESC description{};checked(adapter->GetDesc(&description),"Get GPU description");
            char utf8[512]{};WideCharToMultiByte(CP_UTF8,0,description.Description,-1,utf8,sizeof(utf8),nullptr,nullptr);name=utf8;
            checked(device->CreateComputeShader(tgod_gpu_shader,sizeof(tgod_gpu_shader),nullptr,shader.out()),"Create compute shader");
            D3D11_BUFFER_DESC d{};d.ByteWidth=32;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            checked(device->CreateBuffer(&d,nullptr,constants.out()),"Create GPU constants");
            checked(device->CreateComputeShader(tgod_present_shader,sizeof(tgod_present_shader),nullptr,present_shader.out()),"Create presentation shader");
            d.ByteWidth=64;
            checked(device->CreateBuffer(&d,nullptr,present_constants.out()),"Create presentation constants");
            ready=true;
            std::cout<<"[HD GPU] Direct3D 11 hardware: "<<name<<".\n";
        } catch(const std::exception& e){std::cerr<<"[HD GPU] "<<e.what()<<"; using software HD fallback.\n";}
    }
    Surface* surface(F3DEX2Interpreter& r,uint32_t address) {
        if(r.color_image_size!=2)return nullptr;
        if(cache_height!=r.render_surfaces.height()) {
            flush();surfaces.clear();color=nullptr;depth=nullptr;
            cache_height=r.render_surfaces.height();
        }
        const uint64_t key=(uint64_t(address)<<32)|r.color_image_width;
        auto it=surfaces.find(key);
        if(it!=surfaces.end() && it->second->native_height!=r.framebuffer_height()) {
            flush();color=nullptr;depth=nullptr;surfaces.erase(it);it=surfaces.end();
        }
        if(it!=surfaces.end()&&it->second->frame==frame_id)return it->second.get();
        if(it==surfaces.end()&&surfaces.size()>=8) {
            auto oldest=surfaces.end();
            for(auto candidate=surfaces.begin();candidate!=surfaces.end();++candidate)
                if(candidate->second->frame!=frame_id&&(oldest==surfaces.end()||candidate->second->used<oldest->second->used))oldest=candidate;
            if(oldest==surfaces.end())throw std::runtime_error("GPU surface cache limit reached");
            surfaces.erase(oldest);
        }
        auto cpu=r.render_buffer(address);
        if(!cpu.pixels)return nullptr;
        if(r.color_image_width!=320 && r.color_image_width!=640)
            throw std::runtime_error("Unsupported GPU framebuffer width");
        if(r.render_surfaces.height()==0) {
            const uint64_t end=uint64_t(address)+uint64_t(cpu.width)*cpu.height*2;
            for(const auto& [other_key,other]:surfaces)if(other_key!=key&&other->frame==frame_id&&
                address<uint64_t(other->address)+uint64_t(other->native_width)*other->native_height*2&&end>other->address)
                throw std::runtime_error("Overlapping native targets require CPU reference rendering");
        }
        upload_scratch.resize(size_t(cpu.width)*cpu.height);
        std::copy(cpu.pixels,cpu.pixels+upload_scratch.size(),upload_scratch.begin());
        if(it==surfaces.end()) {
            auto s=std::make_unique<Surface>();s->address=address;s->native_width=r.color_image_width;
            s->native_height=r.framebuffer_height();s->width=cpu.width;s->height=cpu.height;
            D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(upload_scratch.size()*4);d.Usage=D3D11_USAGE_DEFAULT;
            d.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
            d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;d.StructureByteStride=4;
            checked(device->CreateBuffer(&d,nullptr,s->buffer.out()),"Create GPU surface");
            D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=UINT(upload_scratch.size());
            checked(device->CreateUnorderedAccessView(s->buffer.p,&view,s->uav.out()),"Create surface UAV");
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;srv.Buffer.NumElements=UINT(upload_scratch.size());
            checked(device->CreateShaderResourceView(s->buffer.p,&srv,s->srv.out()),"Create surface SRV");
            it=surfaces.emplace(key,std::move(s)).first;++stats.surface_allocations;
        }
        auto* result=it->second.get();
        // Allocation reuse never implies content reuse: each private snapshot
        // imports the native contents again before its first draw/read.
        context->UpdateSubresource(result->buffer.p,0,nullptr,upload_scratch.data(),0,0);
        result->frame=frame_id;result->used=++use_clock;result->dirty=false;result->written=false;result->revision=++content_clock;
        return result;
    }
    void upload(const void* bytes,uint32_t count,Input& input) {
        if(!count)throw std::runtime_error("Empty GPU upload");
        if(count>input.capacity) {
            uint32_t capacity=256;while(capacity<count)capacity*=2;
            D3D11_BUFFER_DESC d{};d.ByteWidth=capacity*4;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;d.StructureByteStride=4;
            checked(device->CreateBuffer(&d,nullptr,input.buffer.out()),"Create reusable GPU input");
            D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;v.Buffer.NumElements=capacity;
            checked(device->CreateShaderResourceView(input.buffer.p,&v,input.view.out()),"Create reusable input SRV");
            input.capacity=capacity;++stats.input_allocations;
        }
        const D3D11_BOX box{0,0,0,count*4,1,1};
        context->UpdateSubresource(input.buffer.p,0,&box,bytes,0,0);
    }
    void flush() {
        if(commands.empty())return;
        const uint32_t bx=(color->width+31)/32,by=(color->height+31)/32;
        bins.resize(size_t(bx)*by);for(auto& bin:bins)bin.clear();
        for(uint32_t i=0;i<commands.size();++i){const auto& c=commands[i];
            for(uint32_t y=c[3]/32;y<=(c[5]-1)/32;++y)
                for(uint32_t x=c[2]/32;x<=(c[4]-1)/32;++x)bins[size_t(y)*bx+x].push_back(i*256);
        }
        spans.resize(bins.size()*2);refs.clear();
        for(size_t i=0;i<bins.size();++i){spans[i*2]=uint32_t(refs.size());spans[i*2+1]=uint32_t(bins[i].size());refs.insert(refs.end(),bins[i].begin(),bins[i].end());}
        if(refs.empty()){commands.clear();return;}
        if(texture_words.empty())texture_words.resize(1024);
        upload(commands.data(),uint32_t(commands.size()*256),inputs[0]);
        upload(texture_words.data(),uint32_t(texture_words.size()),inputs[1]);
        upload(spans.data(),uint32_t(spans.size()),inputs[2]);
        upload(refs.data(),uint32_t(refs.size()),inputs[3]);
        struct Constants{uint32_t width,height,bins;float scale,scale_y,padding[3];} c{color->width,color->height,bx,scale,scale_y,{}};
        context->UpdateSubresource(constants.p,0,nullptr,&c,0,0);
        ID3D11ShaderResourceView* srvs[]={inputs[0].view.p,inputs[1].view.p,inputs[2].view.p,inputs[3].view.p};
        ID3D11UnorderedAccessView* uavs[]={color->uav.p,depth ? depth->uav.p : nullptr};
        context->CSSetShader(shader.p,nullptr,0);context->CSSetShaderResources(0,4,srvs);
        context->CSSetConstantBuffers(0,1,&constants.p);context->CSSetUnorderedAccessViews(0,2,uavs,nullptr);
        context->Dispatch((color->width+7)/8,(color->height+7)/8,1);++stats.dispatches;
        ID3D11UnorderedAccessView* empty[2]{};context->CSSetUnorderedAccessViews(0,2,empty,nullptr);
        ID3D11ShaderResourceView* no_srvs[4]{};context->CSSetShaderResources(0,4,no_srvs);
        color->dirty=true;color->written=true;color->revision=++content_clock;
        if(depth){depth->dirty=true;depth->written=true;depth->revision=++content_clock;}
        commands.clear();texture_words.clear();have_tmem=false;
    }
    void release_presentation() {
        ID3D11UnorderedAccessView* no_uavs[2]{};
        ID3D11ShaderResourceView* no_srvs[4]{};
        context->CSSetUnorderedAccessViews(0,2,no_uavs,nullptr);
        context->CSSetShaderResources(0,4,no_srvs);
        output_view.reset();output.reset();output_staging.reset();swapchain.reset();
        window=nullptr;output_width=output_height=0;last_present_valid=false;context->Flush();
    }
    bool present(HWND hwnd,ID3D11ShaderResourceView* source,uint32_t sw,uint32_t sh,
                 uint32_t ox,uint32_t oy,uint32_t vw,uint32_t vh,bool smooth,uint64_t content,float aspect=4.0f/3.0f) {
        if(!hwnd || !IsWindow(hwnd) || IsIconic(hwnd)){last_present_valid=false;return false;}
        RECT client{};if(!GetClientRect(hwnd,&client))return false;
        const uint32_t w=std::max(0l,client.right-client.left),h=std::max(0l,client.bottom-client.top);
        if(!w || !h || !sw || !sh || !vw || !vh || !source){last_present_valid=false;return false;}
        uint32_t dw=w,dh=std::max(1u,w*3/4);
        if(dh>h){dh=h;dw=std::max(1u,h*4/3);}
        // Half is a reduced area inside the original 4:3 canvas, including
        // when the window/monitor is wider than that canvas.
        if(aspect>2.0f)dh=std::max(1u,dh/2);
        const std::array<uint32_t,16> c={sw,sh,std::min(ox,sw-1),std::min(oy,sh-1),
            vw,vh,w,h,(w-dw)/2,(h-dh)/2,dw,dh,smooth ? 1u:0u,0,0,0};
        if(window==hwnd&&last_present_valid&&last_present_content==content&&last_present_parameters==c)return true;
        if(window!=hwnd)release_presentation();
        if(!swapchain.p) {
            Com<IDXGIDevice> dxdevice;Com<IDXGIAdapter> adapter;Com<IDXGIFactory2> factory;
            checked(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxdevice.out())),"Query presentation device");
            checked(dxdevice->GetAdapter(adapter.out()),"Get presentation adapter");
            checked(adapter->GetParent(__uuidof(IDXGIFactory2),reinterpret_cast<void**>(factory.out())),"Get DXGI factory");
            DXGI_SWAP_CHAIN_DESC1 d{};d.Width=w;d.Height=h;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            d.SampleDesc.Count=1;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.BufferCount=2;
            d.Scaling=DXGI_SCALING_STRETCH;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
            checked(factory->CreateSwapChainForHwnd(device.p,hwnd,&d,nullptr,nullptr,swapchain.out()),"Create DXGI swapchain");
            checked(factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES),"Set DXGI window ownership");
            window=hwnd;
            std::cout<<"[DXGI] Flip presentation active; GPU output stays on device.\n";
        } else if(w!=output_width || h!=output_height) {
            // No backbuffer or bound view survives a resize. Window messages
            // only update HWND state; every DXGI call belongs to this worker.
            output_view.reset();output.reset();output_staging.reset();
            checked(swapchain->ResizeBuffers(2,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,0),"Resize DXGI buffers");
            ++stats.resize_count;
        }
        if(!output.p) {
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;
            d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
            d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
            checked(device->CreateTexture2D(&d,nullptr,output.out()),"Create GPU output texture");
            checked(device->CreateUnorderedAccessView(output.p,nullptr,output_view.out()),"Create GPU output UAV");
            output_width=w;output_height=h;
        }
        context->UpdateSubresource(present_constants.p,0,nullptr,c.data(),0,0);
        context->CSSetShader(present_shader.p,nullptr,0);context->CSSetShaderResources(0,1,&source);
        context->CSSetConstantBuffers(0,1,&present_constants.p);
        context->CSSetUnorderedAccessViews(0,1,&output_view.p,nullptr);
        context->Dispatch((w+7)/8,(h+7)/8,1);
        ID3D11UnorderedAccessView* no_uav=nullptr;ID3D11ShaderResourceView* no_srv=nullptr;
        context->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);context->CSSetShaderResources(0,1,&no_srv);
        Com<ID3D11Texture2D> backbuffer;
        checked(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(backbuffer.out())),"Get DXGI backbuffer");
        context->CopyResource(backbuffer.p,output.p);
        const HRESULT result=swapchain->Present(0,DXGI_PRESENT_DO_NOT_WAIT);
        if(result==DXGI_ERROR_WAS_STILL_DRAWING){++stats.present_skipped;last_present_valid=false;return false;}
        checked(result,"Present DXGI image");++stats.presents;
        if(result==DXGI_STATUS_OCCLUDED)++stats.occluded;
        last_present_content=content;last_present_parameters=c;last_present_valid=result!=DXGI_STATUS_OCCLUDED;
        return true;
    }
    Command state(F3DEX2Interpreter& r,uint32_t kind,int left,int top,int right,int bottom,bool z) {
        Surface* next=surface(r,r.color_image_addr);Surface* nz=z ? surface(r,r.depth_image_addr) : nullptr;
        // Keep the depth binding even for primitives that do not use it, to avoid
        // splitting the batch at every HUD primitive. A change of color target is a barrier.
        if(next!=color || (nz && nz!=depth) || commands.size()>=8192){flush();color=next;depth=nz==next ? nullptr:nz;}
        if(!color)return {};
        scale=float(color->width)/r.color_image_width;scale_y=float(color->height)/r.framebuffer_height();
        Command c{};c[0]=kind;c[2]=std::clamp(left,0,int(color->width));c[3]=std::clamp(top,0,int(color->height));c[4]=std::clamp(right,0,int(color->width));c[5]=std::clamp(bottom,0,int(color->height));
        c[6]=nz ? (nz==next ? 2:1):0;c[7]=r.texture_tile&7;c[8]=r.other_mode_h;c[9]=r.other_mode_l;c[10]=r.geometry_mode;
        c[11]=r.prim_color;c[12]=r.env_color;c[13]=r.blend_color;c[14]=r.fog_color;c[15]=r.combine_mode_w0;c[16]=r.combine_mode_w1;
        c[17]=r.combine_mode_set;c[18]=r.primitive_lod_fraction;c[19]=r.primitive_min_lod;c[20]=r.primitive_depth;
        c[21]=r.fill_color&65535;c[22]=r.texture_max_level;c[23]=r.texture_enabled;c[25]=r.tlut_tmem_addr;
        if(kind!=1){
            if(!have_tmem || std::memcmp(last_tmem.data(),r.tmem,4096)){
                tmem_offset=uint32_t(texture_words.size());const size_t old=texture_words.size();texture_words.resize(old+1024);
                std::memcpy(texture_words.data()+old,r.tmem,4096);std::memcpy(last_tmem.data(),r.tmem,4096);have_tmem=true;
            }c[1]=tmem_offset;
        }
        for(int i=0;i<8;++i){const auto& t=r.tiles[i];auto* p=c.data()+64+i*20;
            p[0]=t.format;p[1]=t.size;p[2]=t.line;p[3]=t.tmem_addr;p[4]=t.palette;
            p[5]=t.clamp_s;p[6]=t.mirror_s;p[7]=t.mask_s;p[8]=t.shift_s;
            p[9]=t.clamp_t;p[10]=t.mirror_t;p[11]=t.mask_t;p[12]=t.shift_t;
            p[13]=t.uls;p[14]=t.ult;p[15]=t.lrs;p[16]=t.lrt;p[17]=t.uls_fraction;p[18]=t.ult_fraction;
        }
        return c;
    }
    void push(Command c){if(color && c[2]<c[4] && c[3]<c[5])commands.push_back(c);}
};
GPURaster::GPURaster():impl(std::make_unique<Impl>()){}
GPURaster::~GPURaster(){if(impl->ready)impl->release_presentation();}
bool GPURaster::available()const{return impl->ready;}
const char* GPURaster::adapter_name()const{return impl->name.c_str();}
GPURaster::Statistics GPURaster::statistics()const{impl->poll_timings();return impl->stats;}
void GPURaster::begin_frame(bool keep_on_gpu){
    impl->commands.clear();impl->color=nullptr;impl->depth=nullptr;
    impl->texture_words.clear();impl->have_tmem=false;impl->keep_on_gpu=keep_on_gpu;++impl->frame_id;
    impl->begin_timing();
}
void GPURaster::finish(F3DEX2Interpreter& r){
    impl->flush();
    // End the raster interval before a diagnostic Map can flush and wait.
    // Otherwise the end query itself can remain batched until the next task.
    impl->end_timing();
    if(!impl->keep_on_gpu){++impl->stats.final_readbacks;readback(r,0,8*1024*1024);}
}
void GPURaster::commit_native_results(const F3DEX2Interpreter& r,uint8_t* destination) {
    if(!destination||!r.rdram||r.render_surfaces.height()!=0||impl->keep_on_gpu||
       !impl->commands.empty()||impl->active_timing>=0)
        throw std::runtime_error("Native commit requires completed native-resolution readbacks");
    for(const auto& [key,s]:impl->surfaces)if(s->frame==impl->frame_id&&s->written) {
        if(s->dirty||s->width!=s->native_width||s->height!=s->native_height||
           uint64_t(s->address)+uint64_t(s->native_width)*s->native_height*2>8*1024*1024)
            throw std::runtime_error("Incomplete or invalid native GPU target");
    }
    for(const auto& [key,s]:impl->surfaces)if(s->frame==impl->frame_id&&s->written)
        std::memcpy(destination+s->address,r.rdram+s->address,size_t(s->native_width)*s->native_height*2);
}
void GPURaster::triangle(F3DEX2Interpreter& r,const Vertex3D& a,const Vertex3D& b,const Vertex3D& c) {
    const auto target=r.render_buffer(r.color_image_addr,false);
    const float s=target.scale, sy=target.scale_y;
    auto cmd=impl->state(r,0,int(std::floor(std::max({0.f,float(r.scissor_xh),std::min({a.screen_x,b.screen_x,c.screen_x})})*s)),
        int(std::floor(std::max({0.f,float(r.scissor_yh),std::min({a.screen_y,b.screen_y,c.screen_y})})*sy)),
        int(std::ceil(std::min({float(r.color_image_width),float(r.scissor_xl),std::max({a.screen_x,b.screen_x,c.screen_x})})*s)),
        int(std::ceil(std::min({float(r.framebuffer_height()),float(r.scissor_yl),std::max({a.screen_y,b.screen_y,c.screen_y})})*sy)),(r.geometry_mode&1)&&r.depth_image_addr);
    const bool perspective=r.other_mode_h&(1<<19);vertex(cmd,32,a,perspective);vertex(cmd,40,b,perspective);vertex(cmd,48,c,perspective);
    impl->push(cmd);
}
void GPURaster::line(F3DEX2Interpreter& r,const Vertex3D& a,const Vertex3D& b,const Vertex3D& flat,float thickness) {
    const auto target=r.render_buffer(r.color_image_addr,false);
    const float s=target.scale, sy=target.scale_y, radius=thickness/2;
    auto c=impl->state(r,3,int(std::floor(std::max({0.f,float(r.scissor_xh),std::min(a.screen_x,b.screen_x)-radius})*s)),
        int(std::floor(std::max({0.f,float(r.scissor_yh),std::min(a.screen_y,b.screen_y)-radius})*sy)),
        int(std::ceil(std::min({float(r.color_image_width),float(r.scissor_xl),std::max(a.screen_x,b.screen_x)+radius})*s)),
        int(std::ceil(std::min({float(r.framebuffer_height()),float(r.scissor_yl),std::max(a.screen_y,b.screen_y)+radius})*sy)),(r.geometry_mode&1)&&r.depth_image_addr);
    vertex(c,32,a,false);vertex(c,40,b,false);vertex(c,48,flat,false);put_float(c,56,thickness);impl->push(c);
}
void GPURaster::rectangle(F3DEX2Interpreter& r,int xl,int yl,int xh,int yh,bool fill,uint32_t tile,float u,float v,float ds,float dt,bool flip) {
    const bool copy=((r.other_mode_h>>20)&3)==2;
    const auto target=r.render_buffer(r.color_image_addr,false);
    const float s=target.scale, sy=target.scale_y;
    auto c=impl->state(r,fill ? 1:2,int(std::ceil(std::max({0,xl,r.scissor_xh})*s)),int(std::ceil(std::max({0,yl,r.scissor_yh})*sy)),
        int(std::ceil(std::min({int(r.color_image_width),xh+(!fill&&copy ? 1:0),r.scissor_xl})*s)),
        int(std::ceil(std::min({int(r.framebuffer_height()),yh+(!fill&&copy ? 1:0),r.scissor_yl})*sy)),!fill&&!copy&&r.depth_image_addr);
    c[7]=tile;put_float(c,32,float(xl));put_float(c,33,float(yl));put_float(c,34,u);put_float(c,35,v);put_float(c,36,ds);put_float(c,37,dt);c[38]=flip;impl->push(c);
}
void GPURaster::readback(F3DEX2Interpreter& r,uint64_t address,uint64_t bytes) {
    bool overlap=false;
    for(const auto& [key,s]:impl->surfaces)if(s->frame==impl->frame_id&&address<uint64_t(s->address)+uint64_t(s->native_width)*s->native_height*2 && address+bytes>s->address){overlap=true;break;}
    if(!overlap)return;
    impl->flush();
    for(auto& [key,s]:impl->surfaces){
        if(s->frame!=impl->frame_id || !s->dirty || address>=uint64_t(s->address)+uint64_t(s->native_width)*s->native_height*2 || address+bytes<=s->address)continue;
        if(!s->staging.p){
            D3D11_BUFFER_DESC d{};s->buffer->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;
            d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            checked(impl->device->CreateBuffer(&d,nullptr,s->staging.out()),"Create lazy GPU readback");
        }
        impl->context->CopyResource(s->staging.p,s->buffer.p);
        impl->stats.readback_bytes+=uint64_t(s->width)*s->height*4;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(impl->context->Map(s->staging.p,0,D3D11_MAP_READ,0,&mapped),"Read GPU framebuffer");
        auto cpu=r.render_surfaces.acquire(r.rdram,s->address,s->native_width,s->native_height);
        const auto* source=static_cast<const uint32_t*>(mapped.pData);
        if(cpu.pixels)std::copy(source,source+size_t(s->width)*s->height,cpu.pixels);
        impl->context->Unmap(s->staging.p,0);s->dirty=false;
    }
}

void GPURaster::release_presentation(){if(impl->ready)impl->release_presentation();}
bool GPURaster::has_target(uint32_t address,uint32_t width)const{
    auto it=impl->surfaces.find((uint64_t(address)<<32)|width);
    return it!=impl->surfaces.end()&&it->second->frame==impl->frame_id;
}
bool GPURaster::present_target(void* hwnd,uint32_t address,uint32_t native_width,uint32_t visible_height,uint32_t offset,bool smooth) {
    auto it=impl->surfaces.find((uint64_t(address)<<32)|native_width);
    if(it==impl->surfaces.end()||it->second->frame!=impl->frame_id)return false;
    impl->flush();const auto& s=*it->second;
    const float scale=float(s.width)/native_width, scale_y=float(s.height)/s.native_height;
    const uint32_t pixels=offset/2;
    return impl->present(static_cast<HWND>(hwnd),s.srv.p,s.width,s.height,
        uint32_t((pixels%native_width)*scale),uint32_t((pixels/native_width)*scale_y),
        s.width,uint32_t((visible_height ? visible_height:s.native_height)*scale_y),smooth,s.revision,s.native_height==120 ? 8.0f/3.0f : 4.0f/3.0f);
}
bool GPURaster::present_native(void* hwnd,const std::vector<uint16_t>& pixels,uint32_t width,uint32_t height,bool smooth) {
    if(!width||!height||pixels.size()!=uint64_t(width)*height)return false;
    if(impl->native_cache!=pixels||impl->native_width!=width||impl->native_height!=height){
        impl->upload_scratch.assign(pixels.begin(),pixels.end());
        impl->upload(impl->upload_scratch.data(),uint32_t(pixels.size()),impl->native_input);
        impl->native_cache=pixels;impl->native_width=width;impl->native_height=height;
        impl->native_version=++impl->content_clock;
    }
    return impl->present(static_cast<HWND>(hwnd),impl->native_input.view.p,width,height,0,0,width,height,smooth,impl->native_version,(width==640 && height>=116 && height<=124) ? 8.0f/3.0f : 4.0f/3.0f);
}
bool GPURaster::capture_output(std::vector<uint32_t>& rgba,uint32_t& width,uint32_t& height) {
    if(!impl->output.p)return false;
    if(!impl->output_staging.p){
        D3D11_TEXTURE2D_DESC d{};impl->output->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;
        d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checked(impl->device->CreateTexture2D(&d,nullptr,impl->output_staging.out()),"Create output capture");
    }
    impl->context->CopyResource(impl->output_staging.p,impl->output.p);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    checked(impl->context->Map(impl->output_staging.p,0,D3D11_MAP_READ,0,&mapped),"Capture GPU output");
    width=impl->output_width;height=impl->output_height;rgba.resize(size_t(width)*height);
    for(uint32_t y=0;y<height;++y)std::memcpy(rgba.data()+size_t(y)*width,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,width*4);
    impl->context->Unmap(impl->output_staging.p,0);return true;
}

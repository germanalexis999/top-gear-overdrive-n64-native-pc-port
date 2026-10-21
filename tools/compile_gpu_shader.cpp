// Compile the port-owned shaders during the build; releases embed bytecode.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstring>
const char raster_source[]=
#include "../src/gpu_raster_shader.inc"
;
const char present_source[]=
#include "../src/gpu_present_shader.inc"
;
bool compile(std::ofstream& out,const char* source,const char* name) {
    ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
    const HRESULT hr=D3DCompile(source,std::strlen(source),name,nullptr,nullptr,"main","cs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_IEEE_STRICTNESS,0,&code,&errors);
    if(errors){std::cerr<<static_cast<const char*>(errors->GetBufferPointer());errors->Release();}
    if(FAILED(hr))return false;
    out<<"inline constexpr unsigned char "<<name<<"[]={\n";
    const auto* bytes=static_cast<const unsigned char*>(code->GetBufferPointer());
    for(size_t i=0;i<code->GetBufferSize();++i){out<<unsigned(bytes[i])<<',';if(i%24==23)out<<'\n';}
    out<<"\n};\n";code->Release();return bool(out);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::filesystem::path target(argv[1]);std::filesystem::create_directories(target.parent_path());
    std::ofstream out(target,std::ios::binary|std::ios::trunc);
    out<<"// Generated port-owned shaders. Do not edit.\n#pragma once\n";
    return compile(out,raster_source,"tgod_gpu_shader")&&compile(out,present_source,"tgod_present_shader") ? 0:1;
}

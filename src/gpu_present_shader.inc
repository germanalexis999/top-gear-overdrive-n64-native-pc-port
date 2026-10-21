R"TGOD_PRESENT(
StructuredBuffer<uint> Source : register(t0);
RWTexture2D<float4> Output : register(u0);
cbuffer View : register(b0) {
    uint4 SourceSize; // width,height,crop x,crop y
    uint4 VisibleSize; // crop width,height,window width,height
    uint4 Destination; // viewport x,y,width,height
    uint4 Options; // smooth
};
float3 pixel(int2 p) {
    p=clamp(p,int2(SourceSize.zw),int2(min(SourceSize.xy,SourceSize.zw+VisibleSize.xy))-1);
    uint v=Source[p.y*SourceSize.x+p.x];
    return float3((v>>11)&31,(v>>6)&31,(v>>1)&31)/31.0;
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=VisibleSize.zw))return;
    if(any(id.xy<Destination.xy)||any(id.xy>=Destination.xy+Destination.zw)) {
        Output[id.xy]=float4(0,0,0,1);return;
    }
    float2 uv=(float2(id.xy-Destination.xy)+0.5)/float2(Destination.zw)*VisibleSize.xy+SourceSize.zw;
    float3 color;
    if(Options.x==0)color=pixel(int2(floor(uv)));
    else {
        uv-=0.5;int2 p=int2(floor(uv));float2 f=frac(uv);
        color=lerp(lerp(pixel(p),pixel(p+int2(1,0)),f.x),lerp(pixel(p+int2(0,1)),pixel(p+1),f.x),f.y);
    }
    Output[id.xy]=float4(color,1);
}
)TGOD_PRESENT"

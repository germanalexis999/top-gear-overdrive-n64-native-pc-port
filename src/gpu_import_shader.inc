R"TGOD_IMPORT(
StructuredBuffer<uint> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u0);
cbuffer Import : register(b0) { uint Width; uint Height; uint NativeWidth; uint XOffset; uint YOffset; uint3 Padding; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=Width || id.y>=Height)return;
    uint x=Input[XOffset+id.x],y=Input[YOffset+id.y];
    Output[id.y*Width+id.x]=Input[y*NativeWidth+x];
}
)TGOD_IMPORT"

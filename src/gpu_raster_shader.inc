R"TGOD_HLSL(
// Port-owned compute rasterizer. Each pixel walks its tile's primitives in DL
// order; no racing blend/depth atomics, no events or access to game RDRAM.
StructuredBuffer<uint> Commands : register(t0);
StructuredBuffer<uint> Textures : register(t1);
StructuredBuffer<uint> Spans : register(t2);
StructuredBuffer<uint> References : register(t3);
RWStructuredBuffer<uint> Color : register(u0);
RWStructuredBuffer<uint> Depth : register(u1);
cbuffer Frame : register(b0) {uint Width;uint Height;uint BinsX;float ScaleX;float ScaleY;float3 FramePadding;};
uint C(uint b,uint i){return Commands[b+i];}
float F(uint b,uint i){return asfloat(C(b,i));}
int4 unpack(uint c){return int4(c>>24,(c>>16)&255,(c>>8)&255,c&255);}
int4 unpack16(uint c){return int4(((c>>11)&31)*255/31,((c>>6)&31)*255/31,((c>>1)&31)*255/31,(c&1)*255);}
uint texbyte(uint b,uint address){uint word=Textures[C(b,1)+((address&4095)>>2)];return (word>>((address&3)*8))&255;}
int coord(int v,int extent,uint cl,uint mi,uint mask){
    if(cl!=0)v=clamp(v,0,extent-1);
    if(mask!=0){int period=1<<mask;int phase=v%(period*2);if(phase<0)phase+=period*2;v=mi!=0&&phase>=period ? period*2-1-phase : phase%period;}
    return v;
}
bool fetch(uint b,uint ti,int x,int y,out int4 col){
    uint t=b+64+ti*20;
    int tw=int(Commands[t+15])-int(Commands[t+13])+1,th=int(Commands[t+16])-int(Commands[t+14])+1;
    col=0;if(tw<=0||th<=0||tw>1024||th>1024)return false;
    uint fmt=Commands[t],sz=Commands[t+1],tm=Commands[t+3];
    uint ps=fmt==0&&sz==3 ? 2:sz;
    uint stride=Commands[t+2]!=0 ? Commands[t+2]*8 : ((uint(tw)<<ps)+1)/2;
    x=coord(x,tw,Commands[t+5],Commands[t+6],Commands[t+7]);
    y=coord(y,th,Commands[t+9],Commands[t+10],Commands[t+11]);
    uint odd=(y&1)!=0 ? 4:0;
    if(fmt==0&&sz==3){uint a=((tm+y*stride+x*2)^odd)&2047;col=int4(texbyte(b,a),texbyte(b,(a+1)&2047),texbyte(b,a|2048),texbyte(b,((a+1)&2047)|2048));return true;}
    if(fmt==3&&sz==2){uint a=(tm+y*stride+x*2)^odd;uint i=texbyte(b,a);col=int4(i,i,i,texbyte(b,a+1));return true;}
    bool small=(fmt==2||fmt==3||fmt==4)&&sz<=1;
    uint a=(tm+y*stride+(small ? (sz==0 ? (x>>1):x):x*2))^odd;
    uint raw=texbyte(b,a);uint nib=(x&1)!=0 ? raw&15:raw>>4;
    if(fmt==2&&(sz==0||sz==1)){uint idx=sz==0 ? Commands[t+4]*16+nib:raw;uint p=C(b,25)+idx*2;col=unpack16((texbyte(b,p)<<8)|texbyte(b,p+1));return true;}
    if(fmt==3&&sz==1){uint i=(raw>>4)*17;col=int4(i,i,i,(raw&15)*17);return true;}
    if(fmt==3&&sz==0){uint i=(nib>>1)*255/7;col=int4(i,i,i,(nib&1)*255);return true;}
    if(fmt==4&&(sz==0||sz==1)){uint i=sz==0 ? nib*17:raw;col=int4(i,i,i,i);return true;}
    col=unpack16((raw<<8)|texbyte(b,a+1));if(fmt!=0||sz!=2)col.a=255;return true;
}
bool sampleTile(uint b,uint ti,float2 uv,bool filtered,out int4 col){
    uint t=b+64+ti*20;uint ss=Commands[t+8],st=Commands[t+12];
    uv*=float2(exp2(ss<=10 ? -int(ss):16-int(ss)),exp2(st<=10 ? -int(st):16-int(st)));
    uv-=float2(Commands[t+13],Commands[t+14])+float2(Commands[t+17],Commands[t+18])/4;
    col=0;if(!all(isfinite(uv))||any(abs(uv)>10000000))return false;
    int2 xy=int2(floor(uv));if(!filtered)return fetch(b,ti,xy.x,xy.y,col);
    float2 f=floor(frac(uv)*32)/32;float4 weights=0;
    if(((C(b,8)>>12)&3)==3&&f.x==0.5&&f.y==0.5)weights=0.25;
    else if(f.x+f.y<1)weights=float4(1-f.x-f.y,f.x,f.y,0);
    else weights=float4(0,1-f.y,1-f.x,f.x+f.y-1);
    float4 result=0;
    [unroll]for(int i=0;i<4;++i){int4 s;if(!fetch(b,ti,xy.x+(i&1),xy.y+(i>>1),s))return false;result+=s*weights[i];}
    col=int4(clamp(result,0,255));return true;
}
int colorMux(uint b,uint mux,uint channel,uint operand,int4 combined,int4 t0,int4 t1,int4 shade,int lod){
    int4 prim=unpack(C(b,11)),env=unpack(C(b,12));
    switch(mux){
    case 0:return combined[channel];case 1:return t0[channel];case 2:return t1[channel];case 3:return prim[channel];case 4:return shade[channel];case 5:return env[channel];
    case 6:return operand==0||operand==3 ? 255:0;
    case 7:return operand==2 ? combined.a:0;case 8:return operand==2 ? t0.a:0;case 9:return operand==2 ? t1.a:0;
    case 10:return operand==2 ? prim.a:0;case 11:return operand==2 ? shade.a:0;case 12:return operand==2 ? env.a:0;
    case 13:return operand==2 ? lod:0;case 14:return operand==2 ? int(C(b,18)):0;default:return 0;}
}
int alphaMux(uint b,uint mux,uint operand,int4 combined,int4 t0,int4 t1,int4 shade,int lod){
    switch(mux){case 0:return operand==2 ? lod:combined.a;case 1:return t0.a;case 2:return t1.a;case 3:return int(C(b,11)&255);case 4:return shade.a;case 5:return int(C(b,12)&255);case 6:return operand==2 ? int(C(b,18)):255;default:return 0;}
}
int combine(uint b,int4 tex0,int4 tex1,int4 shade,int lod,out int4 color){
    uint type=(C(b,8)>>20)&3;
    if(type==2){color=tex0;return color.a;}
    if(C(b,17)==0){color=tex0*shade/255;return color.a;}
    int4 combined=0;int compare=0;
    uint w0=C(b,15),w1=C(b,16),first=type==1 ? 0:1,count=type==1 ? 2:1;
    [loop]for(uint i=0;i<count;++i){uint cycle=first+i;bool swap=type==1&&cycle==1;
        int4 t0=swap ? tex1:tex0,t1=swap ? tex0:tex1;
        uint ma=cycle!=0 ? (w0>>5)&15:(w0>>20)&15,mb=cycle!=0 ? (w1>>24)&15:(w1>>28)&15;
        uint mc=cycle!=0 ? w0&31:(w0>>15)&31,md=cycle!=0 ? (w1>>6)&7:(w1>>15)&7;
        uint aa=cycle!=0 ? (w1>>21)&7:(w0>>12)&7,ab=cycle!=0 ? (w1>>3)&7:(w1>>12)&7;
        uint ac=cycle!=0 ? (w1>>18)&7:(w0>>9)&7,ad=cycle!=0 ? w1&7:(w1>>9)&7;
        [unroll]for(uint k=0;k<3;++k){int a=colorMux(b,ma,k,0,combined,t0,t1,shade,lod),v=colorMux(b,mb,k,1,combined,t0,t1,shade,lod);
            int c=colorMux(b,mc,k,2,combined,t0,t1,shade,lod),d=colorMux(b,md,k,3,combined,t0,t1,shade,lod);combined[k]=clamp(((a-v)*c+127)/255+d,0,255);}
        int a=alphaMux(b,aa,0,combined,t0,t1,shade,lod),v=alphaMux(b,ab,1,combined,t0,t1,shade,lod);
        int c=alphaMux(b,ac,2,combined,t0,t1,shade,lod),d=alphaMux(b,ad,3,combined,t0,t1,shade,lod);
        combined.a=clamp(((a-v)*c+127)/255+d,0,255);if(i==0)compare=combined.a;
    }color=combined;return compare;
}
int3 blendInput(uint source,int3 result,int3 background,int4 blend,int4 fog){
    return source==0 ? result:source==1 ? background:source==2 ? blend.rgb:fog.rgb;
}
bool blend(uint b,int4 color,int shadeAlpha,int compare,uint destination,int2 pixel,out uint packed){
    packed=destination;uint mode=C(b,9),ac=mode&3;
    if(ac==1&&compare<int(C(b,13)&255))return false;
    if(ac==3){const int bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};if(compare<1+bayer[((pixel.y&3)<<2)|(pixel.x&3)]*16)return false;}
    bool times=(mode&4096)!=0;if(times&&color.a==0)return false;
    int alpha=(mode&8192)!=0&&!times ? 255:color.a;
    int3 background=unpack16(destination).rgb,result=color.rgb;uint type=(C(b,8)>>20)&3;
    int4 bc=unpack(C(b,13)),fc=unpack(C(b,14));
    if(type<2){uint count=type==1 ? 2:1;
        [loop]for(uint i=0;i<count;++i){uint p=(mode>>(i!=0 ? 28:30))&3,m=(mode>>(i!=0 ? 20:22))&3;
            uint a=(mode>>(i!=0 ? 24:26))&3,v=(mode>>(i!=0 ? 16:18))&3;
            int wa=a==0 ? alpha:a==1 ? fc.a:a==2 ? shadeAlpha:0;
            int wb=v==0 ? 255-wa:v==3 ? 0:255;
            int3 sp=blendInput(p,result,background,bc,fc),sm=blendInput(m,result,background,bc,fc);
            if(i==count-1&&(mode&16384)==0)result=sp;
            else if(a==3)result=sm;else if(v==3)result=sp;
            else{int den=max(1,wa+wb);result=(sp*wa+sm*wb+den/2)/den;}
            result=clamp(result,0,255);
        }
    }
    packed=((result.r>>3)<<11)|((result.g>>3)<<6)|((result.b>>3)<<1)|1;return true;
}
float edge(float2 a,float2 b,float2 p){return (p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x);}
float2 position(uint b,uint at){return float2(F(b,at),F(b,at+1));}
float2 uvAt(uint b,uint at){return float2(F(b,at+4),F(b,at+5));}
uint2 lodTiles(uint b,float footprint,out int fraction){
    fraction=255;if((C(b,8)&65536)==0)return uint2(0,1);
    bool sharp=(C(b,8)&131072)!=0,detail=(C(b,8)&262144)!=0;
    if(sharp||detail)footprint=max(footprint,C(b,19)/32.0);
    footprint=max(footprint,1e-8);int level=int(floor(log2(footprint)));
    float f=footprint/exp2(max(level,0))-1;
    if(sharp&&footprint<1)f=footprint-1;
    if(detail){if(f<0)f=footprint;++level;}else if(level>=int(C(b,22)))f=1;
    if(detail||sharp)level=max(level,0);else f=max(f,0);
    fraction=int(f*255);return uint2(clamp(level,0,int(C(b,22))),clamp(level+1,0,int(C(b,22))));
}
bool secondTexture(uint b){
    if(C(b,17)==0||((C(b,8)>>20)&3)!=1)return false;
    if((C(b,8)&65536)!=0&&C(b,22)==0)return false;
    uint w0=C(b,15),w1=C(b,16);
    [unroll]for(uint i=0;i<2;++i){uint tex=i!=0 ? 1:2;
        uint4 colors=i!=0 ? uint4((w0>>5)&15,(w1>>24)&15,w0&31,(w1>>6)&7):uint4((w0>>20)&15,(w1>>28)&15,(w0>>15)&31,(w1>>15)&7);
        uint4 alphas=i!=0 ? uint4((w1>>21)&7,(w1>>3)&7,(w1>>18)&7,w1&7):uint4((w0>>12)&7,(w1>>12)&7,(w0>>9)&7,(w1>>9)&7);
        if(any(colors==tex)||colors.z==tex+7||any(alphas==tex))return true;
    }return false;
}
bool validTile(uint b,uint ti){uint t=b+64+ti*20;int w=int(Commands[t+15])-int(Commands[t+13])+1,h=int(Commands[t+16])-int(Commands[t+14])+1;return w>0&&h>0&&w<=1024&&h<=1024;}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID){
    if(id.x>=Width||id.y>=Height)return;
    uint at=id.y*Width+id.x,bin=(id.y/32)*BinsX+id.x/32;
    uint start=Spans[bin*2],count=Spans[bin*2+1];
    if(count==0)return;
    uint pixel=Color[at],z=0;bool depthLoaded=false,depthChanged=false;
    float2 p=(float2(id.xy)+0.5)/float2(ScaleX,ScaleY);
    [loop]for(uint ri=0;ri<count;++ri){uint b=References[start+ri];
        if(id.x<C(b,2)||id.y<C(b,3)||id.x>=C(b,4)||id.y>=C(b,5))continue;
        uint kind=C(b,0),mode=C(b,9),type=(C(b,8)>>20)&3;
        if(kind==1){pixel=C(b,21);continue;}
        int4 shade=255,tex0=255,tex1=255;int lod=255;float curZ=0;uint zValue=0;
        uint tile=C(b,7);bool filtered=((C(b,8)>>12)&3)>=2&&type!=2;
        if(kind==0){
            float2 a=position(b,32),v=position(b,40),c=position(b,48);
            float area=edge(a,v,c),w0=edge(v,c,p),w1=edge(c,a,p),w2=edge(a,v,p);
            if(!((area>0&&w0>=0&&w1>=0&&w2>=0)||(area<0&&w0<=0&&w1<=0&&w2<=0)))continue;
            float3 weights=float3(w0,w1,w2)/area;
            curZ=dot(weights,float3(F(b,34),F(b,42),F(b,50)));
            shade=int4(clamp(weights.x*unpack(C(b,38))+weights.y*unpack(C(b,46))+weights.z*unpack(C(b,54)),0,255));
            uint base=tile;if(!validTile(b,base)&&validTile(b,0))base=0;
            bool textured=C(b,23)!=0&&validTile(b,base);
            if(textured){
                float3 inv=float3(F(b,35),F(b,43),F(b,51));float3 wp=float3(w0,w1,w2)*inv;
                float sum=wp.x+wp.y+wp.z,invSum=abs(sum)>1e-7 ? 1/sum:1;
                float2 uv=(wp.x*uvAt(b,32)+wp.y*uvAt(b,40)+wp.z*uvAt(b,48))*invSum;
                float3 dx=float3(c.y-v.y,a.y-c.y,v.y-a.y)*inv,dy=float3(v.x-c.x,c.x-a.x,a.x-v.x)*inv;
                float wx=dx.x+dx.y+dx.z,wy=dy.x+dy.y+dy.z;
                float2 ux=dx.x*uvAt(b,32)+dx.y*uvAt(b,40)+dx.z*uvAt(b,48);
                float2 uy=dy.x*uvAt(b,32)+dy.y*uvAt(b,40)+dy.z*uvAt(b,48);
                float2 fx=abs((ux-uv*wx)*invSum),fy=abs((uy-uv*wy)*invSum);
                uint2 levels=lodTiles(b,max(max(fx.x,fx.y)/ScaleX,max(fy.x,fy.y)/ScaleY),lod);
                uint first=levels.x!=0 ? (tile+levels.x)&7:base;
                if(!sampleTile(b,first,uv,filtered,tex0))continue;
                tex1=tex0;if(secondTexture(b))sampleTile(b,(tile+levels.y)&7,uv,filtered,tex1);
            }
        }else if(kind==2){
            float2 delta=float2(id.xy)/float2(ScaleX,ScaleY)-float2(F(b,32),F(b,33));if(C(b,38)!=0)delta=delta.yx;
            float2 uv=float2(F(b,34),F(b,35))+delta*float2(F(b,36),F(b,37));
            if(!sampleTile(b,tile,uv,filtered,tex0))continue;
            tex1=tex0;if(type==1&&(C(b,8)&65536)==0)sampleTile(b,(tile+1)&7,uv,filtered,tex1);
            shade=C(b,17)!=0 ? 0:255;
        }else{
            float2 a=position(b,32),v=position(b,40),d=v-a,rel=p-a;
            float len2=dot(d,d),t=dot(rel,d)/len2;
            if(t<0||t>=1||abs(rel.x*d.y-rel.y*d.x)>=F(b,56)*0.5*sqrt(len2))continue;
            curZ=F(b,34)+(F(b,42)-F(b,34))*t;
            shade=(C(b,10)&4)==0 ? unpack(C(b,11)):(C(b,10)&0x200000)==0 ? unpack(C(b,54)):int4(clamp(lerp(unpack(C(b,38)),unpack(C(b,46)),t),0,255));
        }
        if(C(b,6)!=0){
            if(C(b,6)==2)z=pixel;
            else if(!depthLoaded){z=Depth[at];depthLoaded=true;}
            zValue=(mode&4)!=0 ? C(b,20)&32767:uint(clamp(curZ*32,0,65532));
            if((mode&16)!=0&&zValue>z)continue;
        }
        int4 color;int compare=combine(b,tex0,tex1,shade,lod,color);uint outPixel;
        if(!blend(b,color,shade.a,compare,pixel,int2(float2(id.xy)/float2(ScaleX,ScaleY)),outPixel))continue;
        pixel=outPixel;if(C(b,6)==1&&(mode&32)!=0){z=zValue;depthChanged=true;}
    }
    Color[at]=pixel;if(depthChanged)Depth[at]=z;
}
)TGOD_HLSL"


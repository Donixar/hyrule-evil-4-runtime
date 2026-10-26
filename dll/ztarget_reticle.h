#pragma once
#include "ztarget.h"
#include <cmath>
#include <cstring>
namespace link_runtime::ztarget::reticle {
constexpr uint32_t idsys=0x859788,tex_slot=0x859B8C,dummy_unit=0x8599D0,unit_size=0x144,owner=0xCA1;
constexpr uint8_t id_class=0x3B,tex_id=0x9A,mark=0xA1,unit_no=0x5B;
struct Point {float x=0,y=0,half_x=0,half_y=0;};
// IdSys.move uses d=240/tan(Fovy*.5*degrees), then translates the HUD plane to z=-d.
// Its quads use the FINAL camera projection. Invert that same projection for HUD coordinates;
// PC attribution/pins live in ztarget_reticle_runtime.h. Renderer crop is checked separately.
inline bool Project(const View& camera,Vec world,float fovy,Point& out){
    if(!camera.valid||!Finite(world)||!std::isfinite(fovy)||fovy<=1||fovy>=170)return false;
    for(const auto& row:camera.view.v)for(float x:row)if(!std::isfinite(x))return false;
    for(const auto& row:camera.projection)for(float x:row)if(!std::isfinite(x))return false;
    const auto& p=camera.projection;
    if(p[0][0]<=0||p[1][1]<=0||std::fabs(p[3][2]+1)>0.00001f||
       p[3][0]!=0||p[3][1]!=0||p[3][3]!=0||p[0][1]!=0||p[1][0]!=0||p[0][3]!=0||p[1][3]!=0)return false;
    const auto& m=camera.view.v;
    const float v[4]={m[0][0]*world.x+m[0][1]*world.y+m[0][2]*world.z+m[0][3],
        m[1][0]*world.x+m[1][1]*world.y+m[1][2]*world.z+m[1][3],
        m[2][0]*world.x+m[2][1]*world.y+m[2][2]*world.z+m[2][3],1};
    if(!std::isfinite(v[2])||v[2]>=-1)return false;
    float clip[4]{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)clip[i]+=p[i][j]*v[j];
    for(float x:clip)if(!std::isfinite(x))return false;
    // Native GX perspective uses -w <= z <= 0 (the D3D consumer converts the depth later).
    if(clip[3]<=0||clip[2]<-clip[3]||clip[2]>0)return false;
    const float nx=clip[0]/clip[3],ny=clip[1]/clip[3];
    if(std::fabs(nx)>1||std::fabs(ny)>1)return false;
    // Constants match loaded native doubles (including the original degree conversion precision).
    const float half=static_cast<float>(double(fovy)*0.5*0.017453293005625408);
    const float tangent=static_cast<float>(std::tan(double(half)));
    if(!std::isfinite(tangent)||tangent<=0)return false;
    const float d=240.f/tangent;
    Point r{(nx+p[0][2])*d/p[0][0],(ny+p[1][2])*d/p[1][1],d/p[0][0],d/p[1][1]};
    if(!std::isfinite(r.x)||!std::isfinite(r.y)||r.half_x<10||r.half_x>4096||r.half_y<10||r.half_y>4096)return false;
    // Fully hide an edge-clipped marker; never clamp it into a different apparent target position.
    if(std::fabs(nx)+12.f/r.half_x>1||std::fabs(ny)+12.f/r.half_y>1)return false;
    out=r;return true;
}
// Conservative admission for the observed RC1 configuration, not a general viewport solver.
// Fresh renderer probe: symmetric 4:3 P, post-P Y=4/3, client16:9, HUD640x360.
// The post-P read is a veto only; position always comes from the final camera/target frame.
// half_w/half_h: half extents of the quad drawn around p (12 = the old 24x24 marker).
inline bool ObservedViewport(const View& v,const Point& p,const float post[16],int width,int height,float half_w=12,float half_h=12){
    if(width<=0||height<=0||std::abs(int64_t(width)*9-int64_t(height)*16)>16||
       v.projection[0][2]!=0||v.projection[1][2]!=0||
       std::fabs(p.half_x-320)>0.01f||std::fabs(p.half_y-240)>0.01f)return false;
    for(unsigned i=0;i<16;++i){const float expected=i==5?4.f/3.f:(i==0||i==10||i==15?1.f:0.f);
        if(!std::isfinite(post[i])||std::fabs(post[i]-expected)>0.0001f)return false;}
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(half_w)&&std::isfinite(half_h)&&half_w>0&&half_h>0&&
        std::fabs(p.x)+half_w<=320&&std::fabs(p.y)+half_h<=180;
}
// ---- TP notice cursor (17-oct) ------------------------------------------------------------------------------
// One unit, two texture frames (link_ztarget.eff, scripts/ztarget_reticle_assets.py build-tp): 0 = red arrow (locked),
// 1 = yellow chevron (notice). TP draws zelda_notice_cursor_{red,yellow}.bmd as a camera-facing billboard above the
// target, constant screen size (d_attention.cpp dAttDraw_c::draw), animated by its bck (joint 1 = the arrow):
//   red loop, 16 frames at 30 fps: width 1 -> 0.75 in 2 frames, held, back to 1 at frame 16; height lifts 0 -> 3
//     model units over 13 frames and drops back in 3 (the arrow taps down onto the target);
//   lock impact f_znc_r_impact, 17 frames once: scale (3,4) -> (1.33,1.67) at 2 -> 0.5 at 3..4 -> 1.15 at 6 -> 1 at 7
//     -> 1.15 at 8 -> 1 at 9, dropping 2.5 units at frames 3..4 (it slams onto the target);
//   yellow loop, 39 frames at speed 1.7: lift 0.06 -> 1.39 at 27 -> 0.19 at 38 -> 0 at 39.
// dAttDraw_CallBack scales the root's lift by 0.6; applied to both lifts here. Frame geometry: the 64x64 frames
// put the arrow tip kTipFraction of the frame height above the bottom edge (scripts/ztarget_cursor_art.py BOX).
enum class Mode : uint8_t {Off=0,Notice=1,Locked=2};
constexpr float kTipFraction=1.1f/6.8f;
// HUD units (the 640x480 IdSys plane, 360 visible rows at 16:9). Red: 6.8 model units x TP scale 14 = 95 world
// units at TP's 350-unit reference = 0.27 of the half height; shown a little smaller (56), as in Donald's TP
// screenshot (the arrow is ~11% of the screen height). Yellow: TP's select scale (6,4.5) on its 33-unit frame.
constexpr float kLockedSize=56.f,kNoticeW=24.f,kNoticeH=22.f;
// Model units per frame height, to turn the bck lift into HUD units.
constexpr float kRedUnits=6.8f,kYellowUnits=22.f/4.5f;
struct Look {uint8_t frame=0;float w=24,h=24,dy=0;uint8_t rgba[4]{255,255,255,255};};
struct Key {float t,v;};
template<size_t N> inline float Track(const Key (&k)[N],float t){
    if(!(t>k[0].t))return k[0].v;
    for(size_t i=1;i<N;++i)if(t<=k[i].t){const float a=(t-k[i-1].t)/(k[i].t-k[i-1].t);return k[i-1].v+(k[i].v-k[i-1].v)*a;}
    return k[N-1].v;
}
constexpr float kImpactFrames=17.f,kRedLoop=16.f,kYellowLoop=39.f/1.7f;
// seconds: time since this mode/target was first shown. The tip stays at the anchor; dy moves the quad centre.
inline Look Animate(Mode mode,double seconds){
    Look l;if(!std::isfinite(seconds)||seconds<0)seconds=0;
    const float f=float(seconds*30.0);
    if(mode==Mode::Notice){
        static constexpr Key lift[]={{0,0.064f},{27,1.387f},{38,0.188f},{39,0}};
        const float t=std::fmod(f,kYellowLoop)*1.7f;
        l.frame=1;l.w=kNoticeW;l.h=kNoticeH;l.dy=(0.5f-kTipFraction)*l.h+0.6f*Track(lift,t)*l.h/kYellowUnits;return l;
    }
    float sx=1,sy=1,lift=0;
    if(f<kImpactFrames){
        static constexpr Key ix[]={{0,3},{2,1.333f},{3,0.5f},{4,0.5f},{5,0.825f},{6,1.15f},{7,1},{8,1.15f},{9,1}};
        static constexpr Key iy[]={{0,4},{2,1.667f},{3,0.5f},{4,0.5f},{5,0.825f},{6,1.15f},{7,1},{8,1.15f},{9,1}};
        static constexpr Key it[]={{0,0},{2,-1.667f},{3,-2.5f},{4,-2.5f},{6,0}};
        sx=Track(ix,f);sy=Track(iy,f);lift=Track(it,f);
    }else{
        static constexpr Key rx[]={{0,1},{2,0.75f},{15,0.75f},{16,1}};
        static constexpr Key rt[]={{0,0},{12,2.97f},{13,3},{14,2},{15,1},{16,0}};
        const float t=std::fmod(f-kImpactFrames,kRedLoop);sx=Track(rx,t);lift=Track(rt,t);
    }
    l.frame=0;l.w=kLockedSize*sx;l.h=kLockedSize*sy;l.dy=(0.5f-kTipFraction)*l.h+0.6f*lift*kLockedSize/kRedUnits;return l;
}
enum class Why {Hidden,Shown,Occupied,Lost,Fault,NoBlock};
inline const char* Name(Why why){switch(why){case Why::Hidden:return "hidden";case Why::Shown:return "shown";
    case Why::Occupied:return "occupied";case Why::Lost:return "lost";case Why::Fault:return "fault";default:return "no_block";}}
template<class T>inline void Put(unsigned char* p,T v){std::memcpy(p,&v,sizeof(v));}
class Painter {
    unsigned char table_[8+0x8C]{};void* block_=nullptr;
    uint32_t unit_=0,pool_=0;bool texture_=false,live_=false,failed_=false;
    template<class N>bool Owner(N& n,uint32_t& value){uint32_t tex=0;return n.Read(n.base+tex_slot,&tex,4)&&tex>=0x10000&&
        uint64_t(tex)+tex_id*0x54+0x54<0x100000000ULL&&n.Read(tex+tex_id*0x54+0x50,&value,4);}
    // Scan the real pool as well as the class bit. Never treat a stale matching owner as ours.
    template<class N>bool Census(N& n,uint32_t& pool,unsigned& count,bool& bit){
        uint32_t maximum=0,bits=0;
        if(!n.Read(n.base+idsys,&maximum,4)||maximum==0||maximum>4096||
           !n.Read(n.base+idsys+0x50,&pool,4)||pool<0x10000||(pool&3)||uint64_t(pool)+maximum*unit_size>0x100000000ULL||
           !n.Read(n.base+idsys+0x0C+(id_class>>5)*4,&bits,4))return false;
        bit=(bits&(0x80000000U>>(id_class&31)))!=0;count=0;
        for(unsigned i=0;i<maximum;++i){unsigned char row[4]{};
            if(!n.Read(pool+i*unit_size,row,4))return false;
            if(row[0]!=0xFF&&(row[0]&1)&&row[2]==id_class)++count;
        }return true;
    }
    template<class N>bool Ours(N& n){
        uint32_t pool=0,value=0,maximum=0;unsigned count=0;bool bit=false;unsigned char h[4]{},tex=0;
        return live_&&unit_&&unit_!=n.base+dummy_unit&&Census(n,pool,count,bit)&&pool==pool_&&count==1&&bit&&
            n.Read(n.base+idsys,&maximum,4)&&unit_>=pool&&uint64_t(unit_)<uint64_t(pool)+maximum*unit_size&&
            (unit_-pool)%unit_size==0&&Owner(n,value)&&value==owner&&
            n.Read(unit_,h,4)&&h[0]!=0xFF&&(h[0]&1)&&h[1]==unit_no&&h[2]==id_class&&h[3]==mark&&
            n.Read(unit_+0x78,&tex,1)&&tex==tex_id;
    }
    template<class N>Why Drop(N& n){
        if(live_){
            if(!Ours(n)){live_=texture_=false;unit_=pool_=0;failed_=true;return Why::Lost;}
            if(!n.Kill()){failed_=true;return Why::Fault;}live_=false;unit_=pool_=0;
        }
        if(texture_){uint32_t value=0;if(!Owner(n,value)||value!=owner){texture_=false;failed_=true;return Why::Lost;}
            if(!n.TexRelease()){failed_=true;return Why::Fault;}texture_=false;
        }return Why::Hidden;
    }
public:
    Painter(){
        std::memcpy(table_,"2.00",4);table_[5]=1;auto* e=table_+8;
        e[4]=mark;e[5]=unit_no;e[7]=0xFF;e[0x0B]=tex_id;e[0x6F]=2;
        Put(e+0x50,24.f);Put(e+0x54,24.f);e[0x58]=e[0x59]=e[0x5A]=255;e[0x5B]=0;
    }
    void SetBlock(void* block){block_=block;}
    template<class N>Why Tick(N& n,bool show,const Point& p){Look l;return Tick(n,show,p,l,false);}
    // styled: also write the frame (texNo +0x7A, held by tex_flag 2), size_W/H (+0xDC/+0xE0, read every frame by
    // IdCalcVertex 0x241E20 from idSysMove00) and move the quad centre by look.dy (pos0 +0x94 is the centre).
    template<class N>Why Tick(N& n,bool show,const Point& p,const Look& look,bool styled=true){
        if(!show){const Why why=Drop(n);if(why==Why::Hidden)failed_=false;return why;}
        if(failed_)return Why::Fault;
        if(!block_)return Why::NoBlock;
        if(!std::isfinite(p.x)||!std::isfinite(p.y))return Drop(n);
        if(live_&&!Ours(n)){live_=texture_=false;failed_=true;unit_=pool_=0;return Why::Lost;}
        if(!live_){
            uint32_t value=0,pool=0;unsigned count=0;bool bit=false;
            if(!Owner(n,value)||!Census(n,pool,count,bit)){failed_=true;return Why::Fault;}
            if(value||count||bit)return Why::Occupied;
            if(!n.TexLoad(block_)){failed_=true;return Why::Fault;}
            texture_=true;
            if(!Owner(n,value)||value!=owner){texture_=false;failed_=true;return Why::Fault;}
            const bool set=n.Set(table_);unit_=n.UnitPtr();pool_=pool;live_=true;
            if(!set||!Ours(n)){
                // A dummy/full pool has no units of ours. Release only our texture in that case.
                unsigned after=0;uint32_t current=0;bool flag=false;
                if(Census(n,current,after,flag)&&current==pool&&after==0){live_=false;Drop(n);}
                else {live_=texture_=false;unit_=pool_=0;}
                failed_=true;return Why::Fault;
            }
        }
        uint8_t flags=0;const float position[3]={p.x,p.y+(styled?look.dy:0.f),0};
        if(styled&&(!std::isfinite(position[1])||!std::isfinite(look.w)||!std::isfinite(look.h)||look.w<=0||look.h<=0||
           look.w>512||look.h>512||look.frame>1))return Drop(n);
        bool ok=n.Read(unit_,&flags,1);const uint8_t hidden=uint8_t(flags&~8U),visible=uint8_t(flags|8U);
        ok=ok&&n.Write(unit_,&hidden,1)&&n.Write(unit_+0x94,position,sizeof(position))&&
            (!styled||(n.Write(unit_+0x7A,&look.frame,1)&&n.Write(unit_+0xDC,&look.w,4)&&n.Write(unit_+0xE0,&look.h,4)))&&
            n.Write(unit_+0xEC,look.rgba,sizeof(look.rgba))&&n.Write(unit_,&visible,1);
        if(!ok){Drop(n);failed_=true;return Why::Fault;}
        return Why::Shown;
    }
};
}

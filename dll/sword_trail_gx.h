#pragma once
// Shared untextured GX draw for the sword trail (docs/estela-espada-diseno-2026-10-20.md sections 7 and 8): the gate on
// kRuntimePins, the write-arena scan that gives the GX state queued before our OT callback, the section 7 setup and
// the restore. Used by the Task 3b probe (sword_trail_probe.h) and the trail drawer (sword_trail_draw_ot.h). No D3D
// hook, no import; every native entry is in kRuntimePins. Pure parts are templated or take function pointers, so the
// native tests drive them with a fake image and fake GX entries.
#include "sword_trail_ot.h"
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace link_runtime::sword_trail::gx {
namespace ot=link_runtime::sword_trail::ot;
using ot::Vec3;
namespace rva {
constexpr uint32_t kCameraType=0x7FBEF4;    // CameraCurrentProjection: 1 perspective, 2 ortho
constexpr uint32_t kQueueSlot=0xDD1A3C;     // GX write arena; cursor +0x300004, capacity 0x300000
constexpr uint32_t kProjectionCb=0x553FC0,kPositionMatrixCb=0x553960,kCurrentMtxCb=0x553E30,kBlendCb=0x553D20,
    kZModeCb=0x554320,kCullCb=0x553E10,kTexGensCb=0x553FA0;
}
// pG offsets: view 3x4 +0xA4 (ztarget_reticle_runtime.h), generation +0x530C (incremented after ClearOt at 0x254BA8).
constexpr uint32_t kGlobalView=0xA4,kGlobalGeneration=0x530C;

enum class Why:uint8_t {Ready,Off,NoFrameHook,NoSlots,Pins};
inline const char* WhyName(Why w){
    return w==Why::Ready?"ready":w==Why::Off?"off":w==Why::NoFrameHook?"no per-frame player dispatch":
        w==Why::NoSlots?"player/global slots":"kRuntimePins";
}
// Pins are read only when requested.
template<class Mem> Why Gate(Mem& m,uint32_t base,bool requested,bool frame_hook,bool slots){
    if(!requested)return Why::Off;
    if(!frame_hook)return Why::NoFrameHook;
    if(!slots||!base)return Why::NoSlots;
    return ot::MatchesRuntime(m,base)?Why::Ready:Why::Pins;
}

// The GX state section 7 changes, as last queued in this frame's write arena before our callback. GX is deferred: the
// game-thread side keeps no state copy, but every producer appends its exact arguments (record: callback +0, u16
// size +4, payload +8), so the latest record of each kind is the state our callback must leave behind. Items with no
// record since the arena start are left as the draw set them (counted as missing).
// Range checks on a value before it is queued again: an out-of-range latest record leaves that item unrestored
// (counted missing), never an older value. GX enums: blend type 0..3, factors 0..7, logic op 0..15; compare 0..7;
// cull 0..3; texgens 0..8; current matrix index 0..0x1B (the immediate CurrentMtx table).
constexpr uint32_t kMaxMatrixIndex=0x1B;
inline bool FiniteAll(const float* v,unsigned n){for(unsigned i=0;i<n;++i)if(!std::isfinite(v[i]))return false;return true;}
inline bool BlendInRange(const int v[4]){
    return v[0]>=0&&v[0]<=3&&v[1]>=0&&v[1]<=7&&v[2]>=0&&v[2]<=7&&v[3]>=0&&v[3]<=15;
}
// cursor: the arena's write offset at the scan (bytes of this frame's queue so far, of kArenaBytes).
constexpr uint32_t kArenaBytes=0x300000;
struct Prior {
    bool projection=false,position=false,current=false,blend=false,z=false,cull=false,gens=false;
    float proj[16]{};int proj_type=0;float pos[12]{};uint32_t current_index=0;int blend_v[4]{};
    uint8_t z_enable=0,z_write=0;int z_compare=0;int cull_mode=0;uint8_t tex_gens=0;uint32_t records=0,cursor=0;
    unsigned Missing() const {return unsigned(!projection)+!position+!current+!blend+!z+!cull+!gens;}
};
template<class Mem> bool ScanQueue(Mem& m,uint32_t base,Prior& out){
    out=Prior{};uint32_t arena=0,cursor=0;
    if(!m.Read(base+rva::kQueueSlot,&arena,4)||!arena||!m.Read(arena+0x300004,&cursor,4)||cursor>0x300000)return false;
    out.cursor=cursor;
    static unsigned char window[8192];uint32_t lo=0,hi=0;   // OT callbacks run one at a time on the game thread
    for(uint32_t at=0;at<cursor;){
        if(at+0x50>hi||at<lo){lo=at;hi=at+((cursor-at)<sizeof(window)?(cursor-at):uint32_t(sizeof(window)));
            if(!m.Read(arena+lo,window,hi-lo))return false;}
        if(cursor-at<8||at+8>hi)return false;
        const unsigned char* r=window+(at-lo);uint32_t cb=0;uint16_t size=0;std::memcpy(&cb,r,4);std::memcpy(&size,r+4,2);
        if(size<8||size>cursor-at)return false;
        const uint32_t kind=cb-base;uint32_t want=0;
        switch(kind){case rva::kProjectionCb:want=0x4C;break;case rva::kPositionMatrixCb:want=0x3C;break;
            case rva::kCurrentMtxCb:case rva::kCullCb:case rva::kTexGensCb:want=0xC;break;
            case rva::kBlendCb:want=0x18;break;case rva::kZModeCb:want=0x14;break;default:break;}
        if(want){
            if(size!=want||at+want>hi)return false;   // a known producer with another layout: refuse
            switch(kind){
            case rva::kProjectionCb:std::memcpy(out.proj,r+8,64);std::memcpy(&out.proj_type,r+0x48,4);
                out.projection=FiniteAll(out.proj,16)&&(out.proj_type==0||out.proj_type==1);break;
            case rva::kPositionMatrixCb:{uint32_t index=0;std::memcpy(&index,r+0x38,4);
                if(index==0){std::memcpy(out.pos,r+8,48);out.position=FiniteAll(out.pos,12);}break;}
            case rva::kCurrentMtxCb:std::memcpy(&out.current_index,r+8,4);out.current=out.current_index<=kMaxMatrixIndex;break;
            case rva::kBlendCb:std::memcpy(out.blend_v,r+8,16);out.blend=BlendInRange(out.blend_v);break;
            case rva::kZModeCb:out.z_enable=r[8];std::memcpy(&out.z_compare,r+0xC,4);out.z_write=r[0x10];
                out.z=out.z_enable<=1&&out.z_write<=1&&out.z_compare>=0&&out.z_compare<=7;break;
            case rva::kCullCb:std::memcpy(&out.cull_mode,r+8,4);out.cull=out.cull_mode>=0&&out.cull_mode<=3;break;
            case rva::kTexGensCb:out.tex_gens=r[8];out.gens=out.tex_gens<=8;break;
            default:break;}
        }
        at+=size;++out.records;
    }
    return true;
}

// The section 7 producers, all in kRuntimePins (bodies called directly; GX copies every argument by value).
struct Api {
    ot::CameraProjectionFn camera=nullptr;ot::ProjectionFn projection=nullptr;ot::PositionMatrixFn position_matrix=nullptr;
    ot::GxSetCurrentMtxFn current=nullptr;ot::BlendFn blend=nullptr;ot::GxSetZModeFn z=nullptr;ot::GxSetCullModeFn cull=nullptr;
    ot::TextureGeneratorsFn gens=nullptr;ot::BeginFn begin=nullptr;ot::PositionFn position=nullptr;ot::ColorFn color=nullptr;
    ot::TexcoordFn texcoord=nullptr;ot::EndFn end=nullptr;
};
inline Api Resolve(uint32_t base){
    Api a;namespace r=ot::rva;
    a.camera=reinterpret_cast<ot::CameraProjectionFn>(base+r::kCameraProjection);
    a.projection=reinterpret_cast<ot::ProjectionFn>(base+r::kProjection);
    a.position_matrix=reinterpret_cast<ot::PositionMatrixFn>(base+r::kPositionMatrix);
    a.current=reinterpret_cast<ot::GxSetCurrentMtxFn>(base+r::kGxSetCurrentMtx);
    a.blend=reinterpret_cast<ot::BlendFn>(base+r::kBlend);a.z=reinterpret_cast<ot::GxSetZModeFn>(base+r::kGxSetZMode);
    a.cull=reinterpret_cast<ot::GxSetCullModeFn>(base+r::kGxSetCullMode);
    a.gens=reinterpret_cast<ot::TextureGeneratorsFn>(base+r::kTextureGenerators);
    a.begin=reinterpret_cast<ot::BeginFn>(base+r::kBegin);a.position=reinterpret_cast<ot::PositionFn>(base+r::kPosition);
    a.color=reinterpret_cast<ot::ColorFn>(base+r::kColor);a.texcoord=reinterpret_cast<ot::TexcoordFn>(base+r::kTexcoord);
    a.end=reinterpret_cast<ot::EndFn>(base+r::kEnd);
    return a;
}
// GX destination blend factors: INVSRCALPHA (the trail) and ONE (additive: the charge glow; RE4 queues
// GXSetBlendMode(1,4,1,0) itself, e.g. re4-decomp trans.cpp, filter00.cpp, ss_main.cpp).
constexpr int kDstInvSrcAlpha=5,kDstOne=1;
// Section 7, item 4, up to Begin: camera projection, the native view at matrix 0, its selection, SRCALPHA/`dst`
// (INVSRCALPHA unless given), Z LEQUAL test without write, no cull, no texgens. World vertices follow.
// `steps` (optional, for a drawer's fault path): how many of the kSetupSteps producers returned, in that order; from
// kSetupBlended on, our blend is queued.
constexpr uint8_t kSetupSteps=7,kSetupBlended=4;
inline void Setup(const Api& a,const float view[12],int dst=kDstInvSrcAlpha,volatile uint8_t* steps=nullptr){
    volatile uint8_t none=0;volatile uint8_t& s=steps?*steps:none;
    s=0;a.camera();s=1;a.position_matrix(view,0);s=2;a.current(0);s=3;a.blend(1,4,dst,0);s=kSetupBlended;
    a.z(1,3,0);s=5;a.cull(0);s=6;a.gens(0);s=kSetupSteps;
}
// Section 7, item 5: projection/type, matrix 0 then its selection, blend, Z, cull, texgens, as last queued.
inline void Restore(const Api& a,const Prior& p){
    if(p.projection)a.projection(p.proj,p.proj_type);
    if(p.position)a.position_matrix(p.pos,0);
    if(p.current)a.current(p.current_index);
    if(p.blend)a.blend(p.blend_v[0],p.blend_v[1],p.blend_v[2],p.blend_v[3]);
    if(p.z)a.z(p.z_enable,p.z_compare,p.z_write);
    if(p.cull)a.cull(p.cull_mode);
    if(p.gens)a.gens(p.tex_gens);
}

// SEH-guarded copies (CmbMem style); never keeps a game pointer across frames.
struct SehMem {
    bool Read(uint32_t address,void* out,size_t n){
        if(!address)return false;
        __try{std::memcpy(out,reinterpret_cast<const void*>(address),n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
template<class T> bool Get(uint32_t address,T& out){SehMem m;return m.Read(address,&out,sizeof(T));}
inline bool Pointer(uint32_t v){return v>0x10000&&v<0x7FFF0000;}
inline bool Restored(const Api& a,const Prior& p){
    __try{Restore(a,p);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// A Setup that faulted after its blend was queued (Z, cull or texgens raised): only the frame's blend goes back, so our
// blend (additive for the charge glow and the particles) never outlives the node. Nothing else is queued through the
// producers that faulted. Only with a blend to put back and a write arena that still parses whole (a fresh scan: the
// faulting producer left no torn record) with room for the record; the call itself SEH-guarded. false: left as found.
constexpr uint32_t kBlendRecordBytes=0x18;
inline bool BlendCalled(const Api& a,const int v[4]){
    __try{a.blend(v[0],v[1],v[2],v[3]);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool BlendRestored(uint32_t base,const Api& a,const Prior& p){
    if(!p.blend)return false;
    Prior now;SehMem m;
    if(!ScanQueue(m,base,now)||kArenaBytes-now.cursor<kBlendRecordBytes)return false;
    return BlendCalled(a,p.blend_v);
}
// Inside an OT callback: perspective camera, finite view and a readable write arena, or nothing may be queued.
inline bool DrawContext(uint32_t base,uint32_t global_slot,float view[12],Prior& prior){
    uint32_t type=0,global=0;SehMem m;
    if(!Get(base+rva::kCameraType,type)||type!=1||!Get(global_slot,global)||!Pointer(global)||
       !m.Read(global+kGlobalView,view,48)||!ScanQueue(m,base,prior))return false;
    return FiniteAll(view,12);
}
} // namespace link_runtime::sword_trail::gx

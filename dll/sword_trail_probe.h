#pragma once
// Task 3b probe (docs/estela-espada-diseno-2026-10-20.md section 7): two static test quads 60 x 180 cm standing 1.5 m in
// front of Link, facing the camera. Red in the world table 17 (AddOtWorldPos, depth-sorted with the world ESPs and
// models, before the screen tables); blue 70 cm to his right in OT22 (AddOtDirect, after-render) only with
// SwordTrailProbeOt22=1. Each callback emits the section 7 untextured sequence and then re-issues the GX state it
// changed, as the frame's own write queue last set it (range-checked). Visual only, SwordTrailProbe=1. No D3D hook, no import; every native entry is in kRuntimePins.
// Pure parts (geometry, gate, queue scan, draw sequence over an Api) are templated or take function pointers, so the
// native test drives them with a fake image and fake GX entries; link_runtime.cpp only feeds Frame() and logs. The GX
// gate, queue scan, setup and restore are shared with the trail drawer (sword_trail_gx.h).
#include "sword_trail_gx.h"
#include "thread_binding.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace link_runtime::sword_trail::probe {
namespace ot=link_runtime::sword_trail::ot;
namespace gx=link_runtime::sword_trail::gx;
using ot::Vec3;
constexpr uint16_t kWorldTable=17;          // g_OtWork[17]: AddOtWorldPos 0x2EDBD0, 0x400 depth buckets
constexpr uint16_t kAfterRenderTable=22;    // OT22: after_render_proc, one bucket
constexpr uint16_t kKind=0x20;              // EspAddOtAfterRender's kind; not 8, so EspCommonTrans redoes its setup
constexpr float kWorldZLimit=200.f;         // native world ESP limit (GC esp.cpp:487); nearer than 20 cm is refused
constexpr float kWidth=600.f,kHeight=1800.f,kAhead=1500.f,kRightShift=700.f;   // game units are mm
constexpr uint8_t kRed[4]={255,0,0,200},kBlue[4]={0,0,255,200};
constexpr unsigned kRing=4;                 // per-table data slots; ClearOt drops the nodes every generation
namespace rva=link_runtime::sword_trail::gx::rva;
using gx::kGlobalView;using gx::kGlobalGeneration;
constexpr uint32_t kPlayerPos=0x94,kPlayerYaw=0xA4;   // cPlayer pos XYZ, native yaw (forward = sin, cos)

inline bool Finite(const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline Vec3 Add(const Vec3& a,const Vec3& b,float s=1.f){return {a.x+b.x*s,a.y+b.y*s,a.z+b.z*s};}

// Camera position from the 3x4 view [R|t] (row-major GX Mtx): -R^T t.
inline bool CameraPosition(const float view[12],Vec3& out){
    for(unsigned i=0;i<12;++i)if(!std::isfinite(view[i]))return false;
    out={-(view[0]*view[3]+view[4]*view[7]+view[8]*view[11]),
         -(view[1]*view[3]+view[5]*view[7]+view[9]*view[11]),
         -(view[2]*view[3]+view[6]*view[7]+view[10]*view[11])};
    return Finite(out);
}
struct Pose {Vec3 pos{};float yaw=0;};
// v0..v3: bottom, bottom, top, top round the perimeter (GX quad order); centre is the registration point.
struct Quad {Vec3 v[4]{};Vec3 centre{};uint8_t rgba[4]{};};
// Link's forward is (sin yaw, 0, cos yaw) and his right model -X = (-cos yaw, 0, sin yaw). The quad stands on Link's
// floor height, kAhead in front and `right` to his right, turned round Y to face the camera (Link's right axis when
// the camera is straight above it).
inline bool BuildQuad(const Pose& p,const Vec3& camera,float right,const uint8_t rgba[4],Quad& out){
    if(!Finite(p.pos)||!Finite(camera)||!std::isfinite(p.yaw)||!std::isfinite(right))return false;
    const Vec3 forward{std::sin(p.yaw),0.f,std::cos(p.yaw)},side{-std::cos(p.yaw),0.f,std::sin(p.yaw)};
    const Vec3 base=Add(Add(p.pos,forward,kAhead),side,right);
    const float dx=camera.x-base.x,dz=camera.z-base.z,len=std::sqrt(dx*dx+dz*dz);
    const Vec3 axis=len>=1.f?Vec3{dz/len,0.f,-dx/len}:side;
    Quad q;const float half=kWidth*0.5f;
    q.v[0]=Add(base,axis,-half);q.v[1]=Add(base,axis,half);
    q.v[2]={q.v[1].x,q.v[1].y+kHeight,q.v[1].z};q.v[3]={q.v[0].x,q.v[0].y+kHeight,q.v[0].z};
    q.centre={base.x,base.y+kHeight*0.5f,base.z};
    std::memcpy(q.rgba,rgba,4);
    for(const auto& v:q.v)if(!Finite(v))return false;
    out=q;return true;
}

// Off unless the INI value is exactly "1"; pins are read only when requested.
inline bool ExactOne(const wchar_t* v){return v&&v[0]==L'1'&&v[1]==0;}
using gx::Why;using gx::WhyName;using gx::Gate;
using gx::kMaxMatrixIndex;using gx::FiniteAll;using gx::BlendInRange;using gx::Prior;using gx::ScanQueue;
using gx::Api;using gx::Resolve;using gx::Restore;
// Section 7, item 4, verbatim: world vertices with the native view loaded at matrix 0.
inline void Emit(const Api& a,const Quad& q,const float view[12]){
    static constexpr float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    gx::Setup(a,view);
    a.begin(0x80,0,4);
    for(unsigned i=0;i<4;++i){a.position(q.v[i].x,q.v[i].y,q.v[i].z);a.color(q.rgba[0],q.rgba[1],q.rgba[2],q.rgba[3]);
        a.texcoord(uv[i][0],uv[i][1]);}
    a.end(0);
}
// Game-thread runtime. Registration in Frame() (player dispatch, once per generation), drawing in Draw() when the
// native ExecOt reaches our node on the same thread. Data lives in static rings: a node of generation g points at
// slot g % kRing, which is rewritten only four generations later (each ClearOt drops the old nodes).
namespace runtime {
struct Natives {ot::AddOtWorldPosFn world=nullptr;ot::AddOtDirectFn direct=nullptr;Api gx;};
inline Natives ResolveNatives(uint32_t base){
    return {reinterpret_cast<ot::AddOtWorldPosFn>(base+ot::rva::kAddOtWorldPos),
            reinterpret_cast<ot::AddOtDirectFn>(base+ot::rva::kAddOtDirect),Resolve(base)};
}
struct Entry {Quad quad;uint32_t generation=0;uint16_t table=0;};
struct Stats {unsigned frames,world_regs,ot22_regs,fails,world_draws,ot22_draws,restore_missing,draw_fails;};
using Mem=gx::SehMem;using gx::Get;using gx::Pointer;using gx::Restored;
inline std::atomic<bool> ready{false};
inline uint32_t base=0,player_slot=0,global_slot=0;inline Natives natives;inline bool ot22=false,restore=true;
inline thread_binding::Binding owner;   // the dispatching game thread (skills-fix-01: handed over once it exited)
inline bool have_generation=false;inline uint32_t last_generation=0;inline unsigned slot_index=0;
inline Entry world_ring[kRing],after_ring[kRing];
inline std::atomic<unsigned> frames{0},world_regs{0},ot22_regs{0},fails{0},world_draws{0},ot22_draws{0},
    restore_missing{0},draw_fails{0};
inline Stats Snapshot(){return {frames.load(),world_regs.load(),ot22_regs.load(),fails.load(),world_draws.load(),
    ot22_draws.load(),restore_missing.load(),draw_fails.load()};}
// Binds the image, slots and native entries. with_ot22: also the blue OT22 quad (SwordTrailProbeOt22, default off:
// OT22 runs after GXDrawDone, so a fault there stays attributable). with_restore: re-issue the prior GX state
// (SwordTrailProbeRestore, default on; 0 is a debug switch).
inline void Start(uint32_t image_base,uint32_t player,uint32_t global,const Natives& n,bool with_ot22,bool with_restore){
    base=image_base;player_slot=player;global_slot=global;natives=n;owner.Reset();have_generation=false;
    ot22=with_ot22;restore=with_restore;
    ready.store(true,std::memory_order_release);
}
inline bool Emitted(const Api& a,const Quad& q,const float view[12]){
    __try{Emit(a,q,view);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// OT callback (both tables), on the thread that runs the native ExecOt. Perspective camera, finite view and a
// readable write arena, or nothing is queued.
inline void __cdecl Draw(void* data){
    const auto* e=static_cast<const Entry*>(data);
    if(!e||!ready.load(std::memory_order_acquire))return;
    (e->table==kWorldTable?world_draws:ot22_draws).fetch_add(1);
    float view[12]{};Prior prior;
    if(!gx::DrawContext(base,global_slot,view,prior)){draw_fails.fetch_add(1);return;}
    if(!Emitted(natives.gx,e->quad,view)||(restore&&!Restored(natives.gx,prior))){draw_fails.fetch_add(1);return;}
    if(restore)restore_missing.fetch_add(prior.Missing());
}
inline uint16_t RegisterWorld(Entry& e){
    __try{return static_cast<uint16_t>(natives.world(&e,&Draw,&e.quad.centre,kKind,kWorldZLimit));}
    __except(EXCEPTION_EXECUTE_HANDLER){return 0xFFFF;}
}
inline uint16_t RegisterAfter(Entry& e){
    __try{return natives.direct(kAfterRenderTable,&e,&Draw,0,kKind,0.f,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER){return 0xFFFF;}
}
// Player dispatch (game thread), once per native generation. A refused world node (depth below kWorldZLimit or an
// exhausted primitive buffer) and any failed read count as fails.
inline void Frame(){
    if(!ready.load(std::memory_order_acquire))return;
    if(!thread_binding::Serves(owner.Take())){fails.fetch_add(1);return;}
    uint32_t global=0,generation=0,player=0;Pose pose;float view[12]{};Mem m;
    if(!Get(global_slot,global)||!Pointer(global)||!Get(global+kGlobalGeneration,generation))return;
    if(have_generation&&generation==last_generation)return;
    have_generation=true;last_generation=generation;frames.fetch_add(1);
    Vec3 camera;
    if(!Get(player_slot,player)||!Pointer(player)||!m.Read(player+kPlayerPos,&pose.pos,12)||!Get(player+kPlayerYaw,pose.yaw)||
       !m.Read(global+kGlobalView,view,sizeof(view))||!CameraPosition(view,camera)){fails.fetch_add(1);return;}
    const unsigned slot=slot_index++%kRing;Entry& world=world_ring[slot];Entry& after=after_ring[slot];
    world.generation=after.generation=generation;world.table=kWorldTable;after.table=kAfterRenderTable;
    if(!BuildQuad(pose,camera,0.f,kRed,world.quad)||(ot22&&!BuildQuad(pose,camera,kRightShift,kBlue,after.quad))){fails.fetch_add(1);return;}
    if(RegisterWorld(world)==0xFFFF)fails.fetch_add(1);else world_regs.fetch_add(1);
    if(!ot22)return;
    if(RegisterAfter(after)==0xFFFF)fails.fetch_add(1);else ot22_regs.fetch_add(1);
}
} // namespace runtime
} // namespace link_runtime::sword_trail::probe

#pragma once
// Task 4a sword trail drawer (docs/estela-espada-diseno-2026-10-20.md sections 7 and 8): the strip BuildStrip made for
// this frame goes into the world table 17 as ONE node (AddOtWorldPos, kind 0x20, zlimit 200, as the in-game probe; at
// the registration point below), whose callback draws every quad with the section 7 untextured sequence and then
// re-issues the GX state the frame's write queue last set (sword_trail_gx.h, shared with the probe). Untextured
// ribbon: per-vertex colour = Style.rgb, per-vertex alpha = the quad's age fade (a0 old edge, a1 new edge) x a
// cross-ribbon factor, 35% at the inner edge and 100% at the tip (blur.bti's ramp without a texture). No D3D hook, no
// import; entries in kRuntimePins.
// The registration point (bughunt-01, ot_world_place.h): the centroid, or when it is nearer than zlimit while part of
// the strip is not, the centroid moved along the view direction past zlimit; a strip wholly behind the eye plane or
// within zlimit is not registered (not a failure). RegisterNode below, shared by the charge glow, the particles and
// the Z-target arrow.
// Data lives in a static ring of frame slots (generation g -> slot g % kRing, rewritten four generations later; every
// ClearOt drops the nodes), never freed.
#include "sword_trail.h"
#include "sword_trail_gx.h"
#include "ot_world_place.h"
#include "thread_binding.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace link_runtime::sword_trail::ot {
namespace core=::sword_trail;
namespace gx=link_runtime::sword_trail::gx;
constexpr int kMaxQuads=256;          // per frame; BuildStrip drops the oldest beyond it (the newest is always drawn)
constexpr int kBatchQuads=64;         // quads per Begin/End (256 vertices)
constexpr unsigned kRing=4;
constexpr uint16_t kWorldTable=17;    // g_OtWork[17]: AddOtWorldPos 0x2EDBD0
constexpr uint16_t kKind=0x20;        // EspAddOtAfterRender's kind; not 8, so EspCommonTrans redoes its setup
constexpr float kWorldZLimit=200.f;   // native world ESP limit; a point nearer than 20 cm is refused (RegisterNode)
constexpr double kInnerAlpha=0.35;    // cross-ribbon factor at the inner edge (tip edge 1.0)

struct Vertex {Vec3 p{};uint8_t rgba[4]{};float u=0,v=0;};
struct FrameData {uint32_t generation=0;int quads=0;Vec3 centre{};Vertex v[kMaxQuads*4];};

inline uint8_t Scale(uint8_t a,double f){const long v=std::lround(double(a)*f);return uint8_t(v<0?0:v>255?255:v);}
// Core quads (v0 old inner, v1 old tip, v2 new tip, v3 new inner) to float vertices with colour, alpha and uv (u across:
// 0 inner, 1 tip; v along: 0 old, 1 new). Over kMaxQuads the oldest are dropped. false: nothing or a non-finite vertex.
inline bool Build(const core::Quad* q,int n,const core::Style& st,FrameData& out){
    if(!q||n<=0)return false;
    if(n>kMaxQuads){q+=n-kMaxQuads;n=kMaxQuads;}
    const uint8_t r=uint8_t(st.rgb>>16),g=uint8_t(st.rgb>>8),b=uint8_t(st.rgb);
    static constexpr float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    double cx=0,cy=0,cz=0;
    for(int i=0;i<n;++i){
        const uint8_t alpha[4]={Scale(q[i].a0,kInnerAlpha),q[i].a0,q[i].a1,Scale(q[i].a1,kInnerAlpha)};
        for(int k=0;k<4;++k){
            const core::Vec3& p=q[i].v[k];
            if(!core::Finite(p))return false;
            Vertex& v=out.v[i*4+k];
            v.p={float(p.x),float(p.y),float(p.z)};
            if(!std::isfinite(v.p.x)||!std::isfinite(v.p.y)||!std::isfinite(v.p.z))return false;
            v.rgba[0]=r;v.rgba[1]=g;v.rgba[2]=b;v.rgba[3]=alpha[k];v.u=uv[k][0];v.v=uv[k][1];
            cx+=p.x;cy+=p.y;cz+=p.z;
        }
    }
    const double count=4.0*n;
    out.centre={float(cx/count),float(cy/count),float(cz/count)};
    out.quads=n;return true;
}
// How far Emit got, for the fault path of Draw: `steps` of the section 7 setup that returned (gx::Setup; from
// gx::kSetupBlended on, the node's blend is queued), `setup` once it was queued whole, `open` from a Begin that returned
// until its End returned. Volatile: read after the SEH handler caught a fault inside Emit.
struct Progress {volatile uint8_t steps=0;volatile bool setup=false;volatile bool open=false;
    bool Blended() const {return steps>=gx::kSetupBlended;}};
// Section 7, item 4: setup once, then Begin(0x80,0,4k) / 4k x (Position3f32, Color4u8, TexCoord2f32) / End(0) per batch
// of up to kBatchQuads quads (one batch, 4N vertices, for N <= kBatchQuads). dst: the blend's destination factor
// (the trail's INVSRCALPHA unless given; the charge glow passes ONE). EmitQuads takes any run of quads (4 vertices
// each): the frame slots here and the particles' larger buffer (sword_particles_draw.h).
inline void EmitQuads(const gx::Api& a,const Vertex* v,int quads,const float view[12],Progress& progress,int dst=gx::kDstInvSrcAlpha){
    gx::Setup(a,view,dst,&progress.steps);progress.setup=true;
    for(int first=0;first<quads;first+=kBatchQuads){
        const int count=quads-first<kBatchQuads?quads-first:kBatchQuads;
        a.begin(0x80,0,uint16_t(count*4));progress.open=true;
        for(int i=first*4;i<(first+count)*4;++i){
            const Vertex& x=v[i];
            a.position(x.p.x,x.p.y,x.p.z);a.color(x.rgba[0],x.rgba[1],x.rgba[2],x.rgba[3]);a.texcoord(x.u,x.v);
        }
        a.end(0);progress.open=false;
    }
}
inline void Emit(const gx::Api& a,const FrameData& d,const float view[12],Progress& progress,int dst=gx::kDstInvSrcAlpha){
    EmitQuads(a,d.v,d.quads,view,progress,dst);
}
inline void Emit(const gx::Api& a,const FrameData& d,const float view[12]){Progress p;Emit(a,d,view,p);}

// Game-thread runtime. Submit() from the player dispatch (once per native generation), Draw() when the native ExecOt
// reaches the node on the same thread.
namespace runtime {
// place: CameraGetLookVecInverse's pins (ot_world_place.h) matched, so RegisterNode reads the camera AddOtWorldPos uses.
struct Natives {AddOtWorldPosFn world=nullptr;gx::Api gx;bool place=false;};
inline Natives ResolveNatives(uint32_t base){
    gx::SehMem m;
    return {reinterpret_cast<AddOtWorldPosFn>(base+rva::kAddOtWorldPos),gx::Resolve(base),place::Matches(m,base)};
}
// Hidden: nothing registered because the geometry is wholly behind the eye plane or within zlimit (RegisterNode).
enum class Result:uint8_t {NotReady,Empty,WrongThread,NoGeneration,SameGeneration,Failed,Refused,Registered,Hidden};
// One node into table 17: AddOtWorldPos(data, callback, point, kKind, kWorldZLimit) at the point place::Place picks
// from `vertices` points v[i].p with centroid `centre` and the camera at pG (global). Without natives.place or a
// readable camera the centroid goes as before. Refused: AddOtWorldPos returned 0xFFFF (or faulted) at the chosen point:
// with the camera, a prim buffer with no room (or none); without it, also a centroid nearer than zlimit.
enum class Placed:uint8_t {Centroid,Clamped,Behind,Near,Refused};
inline int WorldCalled(AddOtWorldPosFn world,void* data,Callback callback,const Vec3* at){
    __try{return world(data,callback,at,kKind,kWorldZLimit);}__except(EXCEPTION_EXECUTE_HANDLER){return 0xFFFF;}
}
template<class V> Placed RegisterNode(const Natives& n,uint32_t global,void* data,Callback callback,const V* v,int vertices,
                                      const Vec3& centre){
    Vec3 at=centre;place::Where where=place::Where::Centroid;place::Eye eye;gx::SehMem m;
    if(n.place&&place::ReadEye(m,global,eye))where=place::Place(eye,v,vertices,centre,kWorldZLimit,at);
    if(where==place::Where::Behind)return Placed::Behind;
    if(where==place::Where::Near)return Placed::Near;
    if(uint16_t(WorldCalled(n.world,data,callback,&at))==0xFFFF)return Placed::Refused;
    return where==place::Where::Clamped?Placed::Clamped:Placed::Centroid;
}
// reg_fails is the sum of reg_wrong_thread, reg_failed and reg_refused. restore_skipped: faulted draws whose GX state was
// left as the fault found it (see Draw); batch_closed: faulted draws whose open batch Draw closed before the restore.
// Not failures (RegisterNode): reg_behind / reg_near, strips not registered (wholly behind the eye plane / within
// zlimit); reg_clamped, strips registered at the clamped point (their centroid nearer than zlimit).
struct Stats {unsigned frames_drawn,quads,reg_fails,draw_fails,restore_missing,reg_wrong_thread,reg_failed,reg_refused,
    restore_skipped,batch_closed,reg_behind,reg_near,reg_clamped;};
inline std::atomic<bool> ready{false};
inline uint32_t base=0,global_slot=0;inline Natives natives;
inline thread_binding::Binding owner;   // the submitting game thread (skills-fix-01: handed over once it exited)
inline bool have_generation=false;inline uint32_t last_generation=0;inline unsigned slot_index=0;
inline FrameData ring[kRing];
inline std::atomic<unsigned> frames_drawn{0},quads{0},reg_fails{0},draw_fails{0},restore_missing{0},reg_wrong_thread{0},
    reg_failed{0},reg_refused{0},restore_skipped{0},batch_closed{0},reg_behind{0},reg_near{0},reg_clamped{0};
inline Stats Snapshot(){
    return {frames_drawn.load(),quads.load(),reg_fails.load(),draw_fails.load(),restore_missing.load(),reg_wrong_thread.load(),
        reg_failed.load(),reg_refused.load(),restore_skipped.load(),batch_closed.load(),reg_behind.load(),reg_near.load(),
        reg_clamped.load()};
}
// The worker's stats line. The trail counters (spins .. identity_fails) come from feed::Trail.
inline int FormatStats(char* out,size_t size,unsigned spins,unsigned samples,unsigned cuts,unsigned identity_fails,const Stats& s){
    return std::snprintf(out,size,"sword_trail stats spins=%u samples=%u cuts=%u frames_drawn=%u quads=%u reg_fails=%u "
        "draw_fails=%u restore_missing=%u reg_wrong_thread=%u reg_failed=%u reg_refused=%u identity_fails=%u "
        "restore_skipped=%u batch_closed=%u reg_behind=%u reg_near=%u reg_clamped=%u",spins,samples,cuts,s.frames_drawn,
        s.quads,s.reg_fails,s.draw_fails,s.restore_missing,s.reg_wrong_thread,s.reg_failed,s.reg_refused,identity_fails,
        s.restore_skipped,s.batch_closed,s.reg_behind,s.reg_near,s.reg_clamped);
}
inline void Start(uint32_t image_base,uint32_t global,const Natives& n){
    base=image_base;global_slot=global;natives=n;owner.Reset();have_generation=false;
    ready.store(true,std::memory_order_release);
}
inline bool Emitted(const gx::Api& a,const FrameData& d,const float view[12],Progress& progress,int dst=gx::kDstInvSrcAlpha){
    __try{Emit(a,d,view,progress,dst);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool EmittedQuads(const gx::Api& a,const Vertex* v,int count,const float view[12],Progress& progress,int dst){
    __try{EmitQuads(a,v,count,view,progress,dst);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool Closed(const gx::Api& a){
    __try{a.end(0);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// OT callback (table 17). Perspective camera, finite view and a readable write arena, or nothing is queued.
// After a fault inside Emit (always a draw fail):
// - Setup not complete: one of the section 7 producers itself faulted (its arena or entry is not what the pins say).
//   No restore: seven more records through the same producers would repeat the fault or append to a broken arena.
//   Counted in restore_skipped. Once the setup's blend was queued (Progress::Blended: Z, cull or texgens faulted), the
//   frame's blend alone goes back (gx::BlendRestored: a fresh scan of the arena first, the call SEH-guarded), so the
//   node's blend never stays set behind it.
// - A batch open (its Begin returned, its End did not): End(0) first, then the restore. GX is deferred: Begin, every
//   vertex attribute and End are separate arena records, and the consumer's End draws only the vertices actually
//   queued (count/4 whole quads for 0x80, so a short batch never reads past them). Restore records queued inside an
//   open primitive would run between its Begin and End instead, and the stale Begin would only be dropped by the next
//   native Begin. Closing first keeps the record stream well formed (batch_closed). If that End faults too, nothing
//   more is queued (restore_skipped). A fault inside Begin leaves no batch open and the restore follows directly: an
//   End there would redraw the previous native primitive's vertices.
// - Between batches with the setup done: the restore as usual, so the trail's state never leaks into the frame.
inline void __cdecl Draw(void* data){
    const auto* d=static_cast<const FrameData*>(data);
    if(!d||!ready.load(std::memory_order_acquire))return;
    float view[12]{};gx::Prior prior;
    if(!gx::DrawContext(base,global_slot,view,prior)){draw_fails.fetch_add(1);return;}
    Progress progress;
    const bool emitted=Emitted(natives.gx,*d,view,progress);
    if(!emitted){
        draw_fails.fetch_add(1);
        if(!progress.setup){restore_skipped.fetch_add(1);if(progress.Blended())gx::BlendRestored(base,natives.gx,prior);return;}
        if(progress.open){
            if(!Closed(natives.gx)){restore_skipped.fetch_add(1);return;}
            batch_closed.fetch_add(1);
        }
    }
    const bool restored=gx::Restored(natives.gx,prior);
    restore_missing.fetch_add(prior.Missing());
    if(!emitted)return;
    if(!restored){draw_fails.fetch_add(1);return;}
    frames_drawn.fetch_add(1);
}
// This frame's strip, once per native generation (pG+0x530C). WrongThread (not the first Submit's thread), Failed
// (non-finite strip) and Refused (0xFFFF at the RegisterNode point: exhausted primitive buffer or a fault; without the
// camera also a centroid below kWorldZLimit) count as reg_fails, and each in its own counter. Hidden (wholly behind
// the eye plane or within zlimit) counts in reg_behind / reg_near only.
inline Result Submit(const core::Quad* q,int n,const core::Style& st){
    if(!ready.load(std::memory_order_acquire))return Result::NotReady;
    if(!q||n<=0)return Result::Empty;
    if(!thread_binding::Serves(owner.Take())){reg_fails.fetch_add(1);reg_wrong_thread.fetch_add(1);return Result::WrongThread;}
    uint32_t global=0,generation=0;
    if(!gx::Get(global_slot,global)||!gx::Pointer(global)||!gx::Get(global+gx::kGlobalGeneration,generation))return Result::NoGeneration;
    if(have_generation&&generation==last_generation)return Result::SameGeneration;
    have_generation=true;last_generation=generation;
    FrameData& d=ring[slot_index++%kRing];
    if(!Build(q,n,st,d)){d.quads=0;reg_fails.fetch_add(1);reg_failed.fetch_add(1);return Result::Failed;}
    d.generation=generation;
    switch(RegisterNode(natives,global,&d,&Draw,d.v,d.quads*4,d.centre)){
    case Placed::Behind:reg_behind.fetch_add(1);return Result::Hidden;
    case Placed::Near:reg_near.fetch_add(1);return Result::Hidden;
    case Placed::Refused:reg_fails.fetch_add(1);reg_refused.fetch_add(1);return Result::Refused;
    case Placed::Clamped:reg_clamped.fetch_add(1);break;
    default:break;
    }
    quads.fetch_add(unsigned(d.quads));return Result::Registered;
}
} // namespace runtime
} // namespace link_runtime::sword_trail::ot

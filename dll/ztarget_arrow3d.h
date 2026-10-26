#pragma once
// Z-target 3D arrow (ztarget-3d-01, 21-oct; docs/ztarget-3d-diseno-2026-10-21.md): TP's cursor as a solid arrowhead in
// the world instead of the 2D IdSys sprite (ztarget_reticle*.h, kept installed as the fallback). The trail's and the
// charge glow's route (docs/estela-espada-diseno-2026-10-20.md sections 7 and 8, sword_charge_draw.h): ONE world table
// 17 node per native generation (AddOtWorldPos at ot::runtime::RegisterNode's point: the arrow's centroid, or past
// zlimit along the view direction when only the centroid is too near, bughunt-01; kind 0x20, zlimit 200), its own
// node, whose callback draws the quads with the section 7 untextured sequence, blend SRCALPHA/INVSRCALPHA (solid, as the trail), Z
// LEQUAL test without write, then re-issues the GX state the frame's write queue last set. It uses the frame's own
// camera and keeps the depth: walls and Ganados in front hide it.
// Geometry (pure Build; the native view pG+0xA4 read at the submit): a downward triangular prism, the triangle W wide
// and kHeight W tall, kThick W thick, its tip (an edge) at the target's feet + kCursorTip (ztarget_runtime.h), turning
// about the world vertical at kSpinDegPerS; W = kWidthPerDistance x the eye-to-tip distance, clamped to
// kMinWidth..kMaxWidth (a constant apparent size); the yellow notice arrow is kNoticeScale of the red one (TP's is
// smaller). Flat per-face shading, kAmbient + (1 - kAmbient) x clamp(n.L / peak, 0, 1) with L a light fixed to the
// camera (up, left, toward the eye) and peak the most any face of this prism gets over a whole turn of the spin: the
// brightest face the spin shows is the full colour (red kLockedRgb locked, yellow kNoticeRgb notice), a face turned
// from the light kAmbient of it; alpha kAlpha. Faces turned away from the eye are dropped on the CPU (a convex prism
// shows at most 3 of its 5 faces and they never overlap, so no Z write is needed for a correct order); a triangle goes
// as a degenerate quad (the 0x80 batch), as the charge glow's disc. reticle::Animate's curves (the lock impact, the red
// tap loop, the yellow bob) become the world scale and lift (LookAt).
// The Z-target camera hands over a value snapshot (Target; ztarget_runtime.h ArrowPublish / ArrowFrame). No D3D hook,
// no import; entries in kRuntimePins. Data lives in a static ring of frame slots (as the trail's), never freed.
#include "ztarget_reticle.h"
#include "sword_trail_draw_ot.h"
#include "thread_binding.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace link_runtime::ztarget::arrow3d {
namespace core=::sword_trail;
namespace ot=link_runtime::sword_trail::ot;
namespace gx=link_runtime::sword_trail::gx;
using Vec3=core::Vec3;
constexpr double kWidthPerDistance=0.055,kMinWidth=120.0,kMaxWidth=900.0;   // W, RE4 units
constexpr double kHeight=1.25,kThick=0.35;          // x W
constexpr double kNoticeScale=0.6;                  // the yellow arrow, x the red one (size and bob)
constexpr double kSpinDegPerS=120.0;
constexpr double kAmbient=0.45;
constexpr uint32_t kLockedRgb=0xFF2A1E,kNoticeRgb=0xFFD23C;
constexpr uint8_t kAlpha=235;
constexpr uint32_t kStaleMs=100;                    // an older snapshot draws nothing
constexpr int kFaces=5,kMaxQuads=kFaces;
constexpr double kPi=3.14159265358979323846;
static_assert(kMaxQuads<=ot::kBatchQuads,"one batch");

inline Vec3 Add(const Vec3& a,const Vec3& b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 Sub(const Vec3& a,const Vec3& b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 Mul(const Vec3& a,double k){return {a.x*k,a.y*k,a.z*k};}
inline Vec3 Cross(const Vec3& a,const Vec3& b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double Dot(const Vec3& a,const Vec3& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline double Length(const Vec3& a){return std::sqrt(Dot(a,a));}
inline Vec3 Unit(const Vec3& a){const double l=Length(a);return l>1e-9?Mul(a,1.0/l):Vec3{0,0,0};}
// The camera's right, up and back (world, unit; back points toward the eye: GX looks down -z) and eye, from the 3x4
// view (rows = camera axes, column 3 = translation): sword_charge_draw.h CameraOf plus the back axis for the light.
struct Camera {Vec3 right{},up{},back{},eye{};};
inline bool CameraOf(const float view[12],Camera& out){
    for(int i=0;i<12;++i)if(!std::isfinite(view[i]))return false;
    const Vec3 r0{view[0],view[1],view[2]},r1{view[4],view[5],view[6]},r2{view[8],view[9],view[10]};
    const double l0=Length(r0),l1=Length(r1),l2=Length(r2);
    if(!(l0>1e-6)||!(l1>1e-6)||!(l2>1e-6))return false;
    out.right=Mul(r0,1.0/l0);out.up=Mul(r1,1.0/l1);out.back=Mul(r2,1.0/l2);
    out.eye=Mul(Add(Add(Mul(r0,view[3]),Mul(r1,view[7])),Mul(r2,view[11])),-1.0);
    return core::Finite(out.eye);
}

// What one frame shows: the colour (alpha 0 = nothing), the scale (x: width and thickness, y: height; 1 = rest), the
// lift (x the rest height, + up) and the yaw (radians).
struct Look {uint32_t rgb=0;uint8_t alpha=0;double sx=1,sy=1,lift=0,yaw=0;};
// reticle::Animate in world terms. anim_seconds: since this mode/target was first shown (hud::Clock); spin_seconds: any
// steady clock. At rest the red arrow is scale 1 (W wide) and the yellow one kNoticeScale; the lift is Animate's offset
// of the tip over its rest place, over the frame height (the 2D quad's dy minus its rest centring), x the same scale.
inline Look LookAt(reticle::Mode mode,double anim_seconds,double spin_seconds){
    Look l;
    if(mode!=reticle::Mode::Locked&&mode!=reticle::Mode::Notice)return l;
    const reticle::Look r=reticle::Animate(mode,anim_seconds);
    const bool locked=mode==reticle::Mode::Locked;
    const double w=locked?reticle::kLockedSize:reticle::kNoticeW,h=locked?reticle::kLockedSize:reticle::kNoticeH;
    const double k=locked?1.0:kNoticeScale;
    l.rgb=locked?kLockedRgb:kNoticeRgb;l.alpha=kAlpha;
    l.sx=k*double(r.w)/w;l.sy=k*double(r.h)/h;l.lift=k*double(r.dy-(0.5f-reticle::kTipFraction)*r.h)/h;
    const double degrees=std::isfinite(spin_seconds)?std::fmod(spin_seconds*kSpinDegPerS,360.0):0.0;
    l.yaw=degrees*kPi/180.0;return l;
}
inline double Width(double distance){const double w=kWidthPerDistance*distance;return w<kMinWidth?kMinWidth:w>kMaxWidth?kMaxWidth:w;}
inline uint32_t Shade(uint32_t rgb,double s){
    auto c=[&](unsigned shift){const long v=std::lround(double((rgb>>shift)&0xFF)*s);return uint32_t(v<0?0:v>255?255:v)<<shift;};
    return c(16)|c(8)|c(0);
}
// The prism's corners: 0 tip, 1 top left, 2 top right on the front (+depth), 3..5 the same on the back. Faces as quads
// in perimeter order (a triangle repeats its first corner): front, back, top, left slope, right slope. The normal comes
// from corners 1..3 of each (distinct in all five).
struct Face {int v[4];};
constexpr Face kFace[kFaces]={{{0,0,1,2}},{{3,3,5,4}},{{1,2,5,4}},{{0,1,4,3}},{{0,3,5,2}}};
// This frame's arrow: the faces toward the eye, at most 3. true with no quads when the look's alpha is 0 or the eye is
// inside the prism. false: a non-finite input, a degenerate camera or a scale that is not positive.
inline bool Build(const Look& look,const Vec3& tip_world,const float view[12],ot::FrameData& out){
    out.quads=0;Camera cam;
    if(!core::Finite(tip_world)||!CameraOf(view,cam))return false;
    if(!std::isfinite(look.sx)||!std::isfinite(look.sy)||!std::isfinite(look.lift)||!std::isfinite(look.yaw)||
       !(look.sx>0)||!(look.sy>0))return false;
    if(!look.alpha)return true;
    const double distance=Length(Sub(cam.eye,tip_world));
    if(!std::isfinite(distance))return false;
    const double w=Width(distance),half=0.5*w*look.sx,height=kHeight*w*look.sy,depth=0.5*kThick*w*look.sx;
    const Vec3 tip=Add(tip_world,{0,kHeight*w*look.lift,0});
    const Vec3 across{std::cos(look.yaw),0,std::sin(look.yaw)},through{-std::sin(look.yaw),0,std::cos(look.yaw)};
    const Vec3 top=Add(tip,{0,height,0});
    const Vec3 tri[3]={tip,Sub(top,Mul(across,half)),Add(top,Mul(across,half))};
    Vec3 p[6],centre{0,0,0};
    for(int i=0;i<3;++i){p[i]=Add(tri[i],Mul(through,depth));p[i+3]=Sub(tri[i],Mul(through,depth));}
    for(const Vec3& v:p)centre=Add(centre,Mul(v,1.0/6.0));
    const Vec3 light=Unit(Add(Sub(cam.up,cam.right),cam.back));   // up, left, toward the eye
    // Outward normals first, and peak: yaw turns only a normal's horizontal part, so over a whole turn a face gets at
    // most |n_h| |L_h| + n_y L_y. The brightest of those is the full colour (no face reaching the light: plain n.L).
    const double light_h=std::sqrt(light.x*light.x+light.z*light.z);
    Vec3 normal[kFaces];double peak=0;
    for(int i=0;i<kFaces;++i){
        const Vec3& on=p[kFace[i].v[1]];
        Vec3 u=Cross(Sub(p[kFace[i].v[2]],on),Sub(p[kFace[i].v[3]],on));
        const double len=Length(u);
        if(!(len>1e-9)||!std::isfinite(len))return false;
        u=Mul(u,1.0/len);
        if(Dot(u,Sub(on,centre))<0)u=Mul(u,-1.0);   // outward
        normal[i]=u;
        const double best=std::sqrt(u.x*u.x+u.z*u.z)*light_h+u.y*light.y;
        if(best>peak)peak=best;
    }
    if(!(peak>1e-6))peak=1.0;
    int n=0;double cx=0,cy=0,cz=0;
    for(int i=0;i<kFaces;++i){
        const Face& f=kFace[i];const Vec3& on=p[f.v[1]];
        if(!(Dot(normal[i],Sub(cam.eye,on))>0))continue;   // turned away from the eye, or edge-on
        const double lit=Dot(normal[i],light)/peak;
        const uint32_t rgb=Shade(look.rgb,kAmbient+(1.0-kAmbient)*(lit>1?1.0:lit>0?lit:0.0));
        for(int k=0;k<4;++k){
            ot::Vertex& v=out.v[n*4+k];const Vec3& q=p[f.v[k]];
            v.p={float(q.x),float(q.y),float(q.z)};
            if(!std::isfinite(v.p.x)||!std::isfinite(v.p.y)||!std::isfinite(v.p.z))return false;
            v.rgba[0]=uint8_t(rgb>>16);v.rgba[1]=uint8_t(rgb>>8);v.rgba[2]=uint8_t(rgb);v.rgba[3]=look.alpha;v.u=v.v=0;
            cx+=v.p.x;cy+=v.p.y;cz+=v.p.z;
        }
        ++n;
    }
    out.quads=n;
    if(n)out.centre={float(cx/(4.0*n)),float(cy/(4.0*n)),float(cz/(4.0*n))};
    return true;
}

// The Z-target camera's snapshot (ztarget_runtime.h ArrowPublish, FrontDetour after CaptureView): what HudWith gives the
// 2D cursor. tick: the camera frame (pG+0x530C); time: GetTickCount at the publish. The player dispatch reads it on
// the same OS thread (ArrowFrame refuses any other, as FaceWith does), so it is a plain value.
struct Target {bool valid=false;uint8_t mode=0;uint64_t identity=0;Vec3 tip{};uint32_t tick=0,time=0;uint16_t room=0;};
enum class Admit:uint8_t {Show,None,Stale,Unsafe,Room};
// At the dispatch. fresh: this frame read whole, Safe and the HUD running; room: its room.
inline Admit AdmitTarget(const Target& t,uint32_t now_ms,bool fresh,uint16_t room){
    if(!t.valid)return Admit::None;
    if(uint32_t(now_ms-t.time)>kStaleMs)return Admit::Stale;
    if(!fresh)return Admit::Unsafe;
    return room==t.room?Admit::Show:Admit::Room;
}

// Log lines. Each returns the snprintf length (<= 0 or >= size: not written whole).
inline int FormatReady(char* out,size_t size,bool ready,const char* reason,const char* reticle2d){
    return std::snprintf(out,size,"ztarget_arrow3d ready=%d reason=%s reticle2d=%s width_per_distance=%.3f width=%.0f..%.0f "
        "spin_deg_s=%.0f alpha=%u",ready?1:0,reason?reason:"-",reticle2d?reticle2d:"-",kWidthPerDistance,kMinWidth,kMaxWidth,
        kSpinDegPerS,unsigned(kAlpha));
}
inline int FormatStats(char* out,size_t size,unsigned frames_drawn,unsigned quads,unsigned reg_fails,unsigned draw_fails,
                       unsigned restore_missing){
    return std::snprintf(out,size,"ztarget_arrow3d stats frames_drawn=%u quads=%u reg_fails=%u draw_fails=%u restore_missing=%u",
        frames_drawn,quads,reg_fails,draw_fails,restore_missing);
}
inline int FormatFeed(char* out,size_t size,unsigned stale,unsigned room,unsigned unsafe,unsigned wrong_thread,
                      unsigned restore_skipped,unsigned batch_closed){
    return std::snprintf(out,size,"ztarget_arrow3d feed stale=%u room=%u unsafe=%u wrong_thread=%u restore_skipped=%u "
        "batch_closed=%u",stale,room,unsafe,wrong_thread,restore_skipped,batch_closed);
}
// Where the nodes went (ot::runtime::RegisterNode, bughunt-01): refused by AddOtWorldPos (in reg_fails); not registered,
// wholly behind the eye plane / within zlimit; registered at the clamped point.
inline int FormatPlace(char* out,size_t size,unsigned refused,unsigned behind,unsigned near_eye,unsigned clamped){
    return std::snprintf(out,size,"ztarget_arrow3d place reg_refused=%u reg_behind=%u reg_near=%u reg_clamped=%u",refused,behind,
        near_eye,clamped);
}

// Game-thread runtime, as sword_charge_draw.h's (own natives copy, ring, owner thread and counters): Submit() from the
// player dispatch (once per native generation), Draw() when the native ExecOt reaches the node. The feed counters
// (stale .. wrong_thread) are snapshots ArrowFrame refused.
namespace runtime {
using Result=ot::runtime::Result;
struct Stats {unsigned frames_drawn,quads,reg_fails,draw_fails,restore_missing,restore_skipped,batch_closed,stale,room,unsafe,
    wrong_thread,reg_refused,reg_behind,reg_near,reg_clamped;};
inline std::atomic<bool> ready{false};
inline uint32_t base=0,global_slot=0;inline ot::runtime::Natives natives;
inline thread_binding::Binding owner;   // the submitting game thread (skills-fix-01: handed over once it exited)
inline bool have_generation=false;inline uint32_t last_generation=0;inline unsigned slot_index=0;
inline ot::FrameData ring[ot::kRing];
inline std::atomic<unsigned> frames_drawn{0},quads{0},reg_fails{0},draw_fails{0},restore_missing{0},restore_skipped{0},
    batch_closed{0},stale{0},room{0},unsafe{0},wrong_thread{0},reg_refused{0},reg_behind{0},reg_near{0},reg_clamped{0};
inline Stats Snapshot(){
    return {frames_drawn.load(),quads.load(),reg_fails.load(),draw_fails.load(),restore_missing.load(),restore_skipped.load(),
        batch_closed.load(),stale.load(),room.load(),unsafe.load(),wrong_thread.load(),reg_refused.load(),reg_behind.load(),
        reg_near.load(),reg_clamped.load()};
}
inline void Start(uint32_t image_base,uint32_t global,const ot::runtime::Natives& n){
    base=image_base;global_slot=global;natives=n;owner.Reset();have_generation=false;
    ready.store(true,std::memory_order_release);
}
// OT callback (table 17): the trail's own (sword_trail_draw_ot.h Draw), with its SRCALPHA/INVSRCALPHA blend. A fault
// inside the setup after its blend was queued puts the frame's blend alone back (gx::BlendRestored, R05 as the trail,
// charge and particle drawers), so the arrow's blend never stays set over the additive effects drawn after it.
inline void __cdecl Draw(void* data){
    const auto* d=static_cast<const ot::FrameData*>(data);
    if(!d||!ready.load(std::memory_order_acquire))return;
    float view[12]{};gx::Prior prior;
    if(!gx::DrawContext(base,global_slot,view,prior)){draw_fails.fetch_add(1);return;}
    ot::Progress progress;
    const bool emitted=ot::runtime::Emitted(natives.gx,*d,view,progress);
    if(!emitted){
        draw_fails.fetch_add(1);
        if(!progress.setup){restore_skipped.fetch_add(1);if(progress.Blended())gx::BlendRestored(base,natives.gx,prior);return;}
        if(progress.open){
            if(!ot::runtime::Closed(natives.gx)){restore_skipped.fetch_add(1);return;}
            batch_closed.fetch_add(1);
        }
    }
    const bool restored=gx::Restored(natives.gx,prior);
    restore_missing.fetch_add(prior.Missing());
    if(!emitted)return;
    if(!restored){draw_fails.fetch_add(1);return;}
    frames_drawn.fetch_add(1);
}
// This frame's arrow, once per native generation (pG+0x530C). The view is the native one at the submit. WrongThread,
// Failed (a non-finite input or no view) and Refused (0xFFFF at the RegisterNode point) count as reg_fails; Empty
// (alpha 0, no face toward the eye) registers nothing; Hidden (wholly behind the eye plane or within zlimit) counts in
// reg_behind / reg_near.
inline Result Submit(const Look& look,const Vec3& tip){
    if(!ready.load(std::memory_order_acquire))return Result::NotReady;
    if(!thread_binding::Serves(owner.Take())){reg_fails.fetch_add(1);return Result::WrongThread;}
    uint32_t global=0,generation=0;
    if(!gx::Get(global_slot,global)||!gx::Pointer(global)||!gx::Get(global+gx::kGlobalGeneration,generation))return Result::NoGeneration;
    if(have_generation&&generation==last_generation)return Result::SameGeneration;
    have_generation=true;last_generation=generation;
    float view[12]{};gx::SehMem m;
    ot::FrameData& d=ring[slot_index++%ot::kRing];
    if(!m.Read(global+gx::kGlobalView,view,sizeof(view))||!Build(look,tip,view,d)){
        d.quads=0;reg_fails.fetch_add(1);return Result::Failed;
    }
    if(!d.quads)return Result::Empty;
    d.generation=generation;
    switch(ot::runtime::RegisterNode(natives,global,&d,&Draw,d.v,d.quads*4,d.centre)){
    case ot::runtime::Placed::Behind:reg_behind.fetch_add(1);return Result::Hidden;
    case ot::runtime::Placed::Near:reg_near.fetch_add(1);return Result::Hidden;
    case ot::runtime::Placed::Refused:reg_fails.fetch_add(1);reg_refused.fetch_add(1);return Result::Refused;
    case ot::runtime::Placed::Clamped:reg_clamped.fetch_add(1);break;
    default:break;
    }
    quads.fetch_add(unsigned(d.quads));return Result::Registered;
}
} // namespace runtime
} // namespace link_runtime::ztarget::arrow3d

#pragma once
// Sword charge glow drawer (charge-glow-01; dll/sword_charge.h decides what shows). Same route as the trail
// (docs/estela-espada-diseno-2026-10-20.md sections 7 and 8, sword_trail_draw_ot.h): ONE world table 17 node per native
// generation (AddOtWorldPos at ot::runtime::RegisterNode's point: the geometry's centroid, or past zlimit along the
// view direction when only the centroid is too near, bughunt-01; kind 0x20, zlimit 200), its own node beside the
// trail's, whose callback draws the quads with the section 7 untextured sequence, blend SRCALPHA/ONE (additive), Z test
// without write, then re-issues the GX state the frame's write queue last set. Geometry, camera-facing from the native
// view (pG+0xA4, read at the submit):
// - blade glow: from kInner to the tip, two strips (a wide soft halo in the colour, a narrow core lightened toward
//   white), each with the alpha at its centre line and 0 at both edges, dimmer toward the inner end;
// - glint: a 4-point star at hilt + (tip - hilt) x progress: a soft white disc and four coloured rays along the
//   camera's right and up, scaled by Look::star_scale.
// Sizes are fractions of the blade's length. No D3D hook, no import; entries in kRuntimePins. Data lives in a static
// ring of frame slots (as the trail's), never freed.
#include "sword_charge.h"
#include "sword_trail_draw_ot.h"
#include "thread_binding.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace link_runtime::sword_charge::draw {
namespace core=::sword_trail;
namespace ot=link_runtime::sword_trail::ot;
namespace gx=link_runtime::sword_trail::gx;
using core::Vec3;
constexpr double kInner=0.25;                       // the glow starts here along the blade (0 hilt .. 1 tip)
constexpr int kGlowSegments=3;                      // along the blade
constexpr int kDiscSegments=8;
constexpr double kHaloWidth=0.07,kCoreWidth=0.02;   // half-widths, x blade length
constexpr double kHaloAlpha=0.6,kInnerAlpha=0.55;   // halo x glow alpha; along factor at the inner end (tip 1.0)
constexpr double kCoreWhite=0.6;                    // the core's colour, this far toward white
constexpr double kDiscRadius=0.05,kRayLength=0.16,kRayWidth=0.022;   // x blade length x star scale
constexpr double kMinBlade=1.0;                     // a shorter blade draws nothing
constexpr int kMaxQuads=2*2*kGlowSegments+kDiscSegments+4;
static_assert(kMaxQuads<=ot::kBatchQuads,"one batch");

inline Vec3 Add(const Vec3& a,const Vec3& b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 Sub(const Vec3& a,const Vec3& b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 Mul(const Vec3& a,double k){return {a.x*k,a.y*k,a.z*k};}
inline Vec3 Cross(const Vec3& a,const Vec3& b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double Length(const Vec3& a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
// The camera's right, up (world, unit) and eye from the 3x4 view (rows = camera axes, column 3 = translation).
struct Camera {Vec3 right{},up{},eye{};};
inline bool CameraOf(const float view[12],Camera& out){
    for(int i=0;i<12;++i)if(!std::isfinite(view[i]))return false;
    const Vec3 r0{view[0],view[1],view[2]},r1{view[4],view[5],view[6]},r2{view[8],view[9],view[10]};
    const double l0=Length(r0),l1=Length(r1);
    if(!(l0>1e-6)||!(l1>1e-6))return false;
    out.right=Mul(r0,1.0/l0);out.up=Mul(r1,1.0/l1);
    out.eye=Mul(Add(Add(Mul(r0,view[3]),Mul(r1,view[7])),Mul(r2,view[11])),-1.0);
    return core::Finite(out.eye);
}
inline uint8_t Alpha(double a){return uint8_t(a<=0?0:a>=255?255:std::lround(a));}
inline uint32_t Lighten(uint32_t rgb,double f){
    auto c=[&](unsigned s){const double v=double((rgb>>s)&0xFF);return uint32_t(std::lround(v+(255.0-v)*f))<<s;};
    return c(16)|c(8)|c(0);
}
class Writer {
    ot::FrameData& d_;int v_=0;
public:
    explicit Writer(ot::FrameData& d):d_(d){}
    int Quads() const {return v_/4;}
    void Vertex(const Vec3& p,uint32_t rgb,double alpha){
        ot::Vertex& v=d_.v[v_++];v.p={float(p.x),float(p.y),float(p.z)};
        v.rgba[0]=uint8_t(rgb>>16);v.rgba[1]=uint8_t(rgb>>8);v.rgba[2]=uint8_t(rgb);v.rgba[3]=Alpha(alpha);v.u=v.v=0;
    }
};
// This frame's quads (none when both alphas round to 0). false: a non-finite input, a degenerate blade or camera.
inline bool Build(const Look& look,const Vec3& base,const Vec3& tip,const float view[12],ot::FrameData& out){
    out.quads=0;Camera cam;
    if(!core::Finite(base)||!core::Finite(tip)||!CameraOf(view,cam))return false;
    const Vec3 axis=Sub(tip,base);const double len=Length(axis);
    if(!std::isfinite(len)||len<kMinBlade)return false;
    if(!std::isfinite(look.glow_alpha)||!std::isfinite(look.star_alpha)||!std::isfinite(look.progress)||!std::isfinite(look.star_scale))
        return false;
    const Vec3 dir=Mul(axis,1.0/len);Writer w(out);
    if(Alpha(look.glow_alpha)){
        Vec3 centre[kGlowSegments+1],side[kGlowSegments+1];double along[kGlowSegments+1];
        for(int k=0;k<=kGlowSegments;++k){
            const double u=kInner+(1.0-kInner)*k/kGlowSegments;
            centre[k]=Add(base,Mul(axis,u));
            Vec3 s=Cross(dir,Sub(cam.eye,centre[k]));const double l=Length(s);
            side[k]=l>1e-6?Mul(s,1.0/l):cam.right;   // seen along the blade: the camera's right
            along[k]=kInnerAlpha+(1.0-kInnerAlpha)*k/kGlowSegments;
        }
        const struct {double width,alpha;uint32_t rgb;} layer[2]={{kHaloWidth,kHaloAlpha,look.rgb},{kCoreWidth,1.0,Lighten(look.rgb,kCoreWhite)}};
        for(const auto& l:layer){
            const double h=l.width*len;
            for(int k=0;k<kGlowSegments;++k){
                const double a0=look.glow_alpha*l.alpha*along[k],a1=look.glow_alpha*l.alpha*along[k+1];
                for(int s=-1;s<=1;s+=2){   // edge (alpha 0) to centre line and back, one quad per half
                    const Vec3 e0=Add(centre[k],Mul(side[k],h*s)),e1=Add(centre[k+1],Mul(side[k+1],h*s));
                    w.Vertex(e0,l.rgb,0);w.Vertex(centre[k],l.rgb,a0);w.Vertex(centre[k+1],l.rgb,a1);w.Vertex(e1,l.rgb,0);
                }
            }
        }
    }
    if(Alpha(look.star_alpha)){
        const double p=look.progress<0?0:look.progress>1?1:look.progress,scale=look.star_scale>0?look.star_scale:0;
        const Vec3 at=Add(base,Mul(axis,p));const double a=look.star_alpha;
        const double r=kDiscRadius*len*scale,ray=kRayLength*len*scale,half=kRayWidth*len*scale;
        for(int i=0;i<kDiscSegments;++i){   // soft white disc: centre (twice) and two rim points, rim alpha 0
            const double t0=2*kPi*i/kDiscSegments,t1=2*kPi*(i+1)/kDiscSegments;
            const Vec3 e0=Add(at,Add(Mul(cam.right,r*std::cos(t0)),Mul(cam.up,r*std::sin(t0))));
            const Vec3 e1=Add(at,Add(Mul(cam.right,r*std::cos(t1)),Mul(cam.up,r*std::sin(t1))));
            w.Vertex(at,0xFFFFFF,a);w.Vertex(at,0xFFFFFF,a);w.Vertex(e1,look.rgb,0);w.Vertex(e0,look.rgb,0);
        }
        const Vec3 rays[4]={cam.up,cam.right,Mul(cam.up,-1),Mul(cam.right,-1)};
        for(int i=0;i<4;++i){   // a coloured ray: its base across the centre, its point (twice) at alpha 0
            const Vec3 across=rays[(i+1)%4],point=Add(at,Mul(rays[i],ray));
            w.Vertex(Add(at,Mul(across,-half)),look.rgb,a);w.Vertex(Add(at,Mul(across,half)),look.rgb,a);
            w.Vertex(point,look.rgb,0);w.Vertex(point,look.rgb,0);
        }
    }
    double cx=0,cy=0,cz=0;const int n=w.Quads()*4;
    for(int i=0;i<n;++i){
        const auto& v=out.v[i].p;
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return false;
        cx+=v.x;cy+=v.y;cz+=v.z;
    }
    out.quads=w.Quads();
    if(n)out.centre={float(cx/n),float(cy/n),float(cz/n)};
    return true;
}

// Game-thread runtime, as sword_trail_draw_ot.h's (own natives copy, ring, owner thread and counters): Submit() from
// the player dispatch (once per native generation), Draw() when the native ExecOt reaches the node.
namespace runtime {
using Result=ot::runtime::Result;
// reg_refused: AddOtWorldPos refused the node (part of reg_fails). Not failures (ot::runtime::RegisterNode): reg_behind
// / reg_near, glows not registered (wholly behind the eye plane / within zlimit); reg_clamped, registered at the
// clamped point.
struct Stats {unsigned frames_drawn,quads,reg_fails,draw_fails,restore_missing,restore_skipped,batch_closed,reg_refused,
    reg_behind,reg_near,reg_clamped;};
inline std::atomic<bool> ready{false};
inline uint32_t base=0,global_slot=0;inline ot::runtime::Natives natives;
inline thread_binding::Binding owner;   // the submitting game thread (skills-fix-01: handed over once it exited)
inline bool have_generation=false;inline uint32_t last_generation=0;inline unsigned slot_index=0;
inline ot::FrameData ring[ot::kRing];
inline std::atomic<unsigned> frames_drawn{0},quads{0},reg_fails{0},draw_fails{0},restore_missing{0},restore_skipped{0},batch_closed{0},
    reg_refused{0},reg_behind{0},reg_near{0},reg_clamped{0};
inline Stats Snapshot(){
    return {frames_drawn.load(),quads.load(),reg_fails.load(),draw_fails.load(),restore_missing.load(),restore_skipped.load(),
        batch_closed.load(),reg_refused.load(),reg_behind.load(),reg_near.load(),reg_clamped.load()};
}
inline void Start(uint32_t image_base,uint32_t global,const ot::runtime::Natives& n){
    base=image_base;global_slot=global;natives=n;owner.Reset();have_generation=false;
    ready.store(true,std::memory_order_release);
}
// OT callback (table 17): the trail's fault handling (sword_trail_draw_ot.h Draw), with the additive blend. No blend
// record in this frame's queue: nothing drawn (a draw fail), so the additive blend is only queued with a blend to put
// back. A fault inside the setup after its blend: that blend alone goes back (gx::BlendRestored). It can stay set only
// when an open batch can be neither drawn nor closed (End faults twice: nothing more is queued, restore_skipped).
inline void __cdecl Draw(void* data){
    const auto* d=static_cast<const ot::FrameData*>(data);
    if(!d||!ready.load(std::memory_order_acquire))return;
    float view[12]{};gx::Prior prior;
    if(!gx::DrawContext(base,global_slot,view,prior)||!prior.blend){draw_fails.fetch_add(1);return;}
    ot::Progress progress;
    const bool emitted=ot::runtime::Emitted(natives.gx,*d,view,progress,gx::kDstOne);
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
// This frame's glow, once per native generation (pG+0x530C). The view is the native one at the submit. WrongThread,
// Failed (non-finite input or no view) and Refused (0xFFFF at the RegisterNode point) count as reg_fails; Empty (both
// alphas 0) registers nothing; Hidden (wholly behind the eye plane or within zlimit) counts in reg_behind / reg_near.
inline Result Submit(const Look& look,const Vec3& blade_base,const Vec3& blade_tip){
    if(!ready.load(std::memory_order_acquire))return Result::NotReady;
    if(!thread_binding::Serves(owner.Take())){reg_fails.fetch_add(1);return Result::WrongThread;}
    uint32_t global=0,generation=0;
    if(!gx::Get(global_slot,global)||!gx::Pointer(global)||!gx::Get(global+gx::kGlobalGeneration,generation))return Result::NoGeneration;
    if(have_generation&&generation==last_generation)return Result::SameGeneration;
    have_generation=true;last_generation=generation;
    float view[12]{};gx::SehMem m;
    ot::FrameData& d=ring[slot_index++%ot::kRing];
    if(!m.Read(global+gx::kGlobalView,view,sizeof(view))||!Build(look,blade_base,blade_tip,view,d)){
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
} // namespace link_runtime::sword_charge::draw

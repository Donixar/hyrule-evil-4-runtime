#pragma once
// TP sword particles drawer (sword-particles-01; sword_particles.h simulates). The trail's route (docs/estela-espada-
// diseno-2026-10-20.md sections 7 and 8, sword_trail_draw_ot.h, sword_charge_draw.h): per native generation up to two
// world table-17 nodes (AddOtWorldPos at ot::runtime::RegisterNode's point for each node: its centroid, or past zlimit
// along the view direction when only the centroid is too near, bughunt-01; kind 0x20, zlimit 200) whose callbacks
// draw with the section 7 untextured sequence and then re-issue the GX state the frame's queue last set:
// - additive (SRCALPHA/ONE): sparks, rays, stars, sparkles and the flash. Skipped (a draw fail) when the queue holds no
//   blend record to restore, so the additive blend is only queued with a blend to put back, also after a fault inside
//   the setup (gx::BlendRestored; as the charge glow, sword_charge_draw.h Draw);
// - alpha blend (SRCALPHA/INVSRCALPHA): debris, chips, leaves and dust, drawn far to near.
// Shapes, without textures (design section 2.2), camera-facing from the native view (pG+0xA4, read at the submit):
// - direction_cross (rays, sparks, stars): a spindle of 2 quads along the direction, its centre point lit and its ends
//   and sides at alpha 0 (the texture's soft bar, seen face on);
// - direction (chips, leaves): a flat spindle that turns about its axis with the particle's spin, opaque to its rim;
//   a bottom-anchored one (the leaf) starts at the particle and is widest at kLeafWidest;
// - billboard (dust, debris, sparkles, flash): a soft disc of kDiscSegments, an ellipse turned by the particle's spin.
// Colour: the centre point takes the TEV value at tex = 1, the rim the value at tex = 0 (sword_particles.h VisualOf);
// SwordParticlesGain scales the additive ones (alpha first, then the colour). At most kMaxQuads per generation (the
// rest are counted as quad_cuts). Before queuing, the GX write arena must keep kArenaReserve free after the node's
// quads at kBytesPerQuad, else the node is trimmed (arena_cuts). Data lives in a static ring of frame slots, never freed.
#include "sword_particles.h"
#include "thread_binding.h"
#include "sword_charge_draw.h"   // CameraOf (the camera's right, up and eye from the native view)
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace link_runtime::sword_particles::draw {
namespace ot=link_runtime::sword_trail::ot;
namespace gx=link_runtime::sword_trail::gx;
namespace scd=link_runtime::sword_charge::draw;
constexpr int kMaxQuads=2048;
constexpr int kDiscSegments=6;
constexpr double kSolidRim=0.6;        // alpha-blended prm*tex discs (rock fragments): rim alpha x this, a solid look
constexpr double kLeafWidest=0.4;      // a bottom-anchored flat spindle is widest at 40% of its length
constexpr double kMinSize=0.5;         // mm
constexpr uint32_t kBytesPerQuad=4*64; // GX write arena: 4 vertices of 3 records, generously
constexpr uint32_t kArenaReserve=512*1024;

// dst: the blend's destination factor, set by Build (kDstOne additive, kDstInvSrcAlpha blend). All-zero defaults: the
// ring of frame slots stays zero-initialised (.bss, not in the DLL file).
struct Node {const ot::Vertex* v=nullptr;int quads=0;ot::Vec3 centre{};int dst=0;};
struct FrameData {uint32_t generation=0;int quads=0;Node add,blend;ot::Vertex v[kMaxQuads*4];};
struct BuildStats {unsigned drawn=0,cut=0;};

// Additive gain: alpha first (up to 255), the rest on the colour.
inline void Gain(double gain,double& alpha,uint32_t& centre,uint32_t& edge){
    if(!(gain>0)||gain==1.0||!(alpha>0))return;
    const double a=alpha*gain;
    if(a<=255.0){alpha=a;return;}
    const double k=a/255.0;alpha=255.0;centre=Scale(centre,k);edge=Scale(edge,k);
}
class Writer {
    FrameData& d_;int q_;
    static uint8_t A(double a){return uint8_t(a<=0?0:a>=255?255:std::lround(a));}
    void Vertex(int i,const V3& p,uint32_t rgb,double alpha){
        ot::Vertex& v=d_.v[q_*4+i];v.p={float(p.x),float(p.y),float(p.z)};
        v.rgba[0]=uint8_t(rgb>>16);v.rgba[1]=uint8_t(rgb>>8);v.rgba[2]=uint8_t(rgb);v.rgba[3]=A(alpha);v.u=v.v=0;
    }
public:
    Writer(FrameData& d,int first):d_(d),q_(first){}
    int Quads() const {return q_;}
    bool Room(int quads) const {return q_+quads<=kMaxQuads;}
    void Quad(const V3 (&p)[4],const uint32_t (&rgb)[4],const double (&alpha)[4]){
        for(int i=0;i<4;++i)Vertex(i,p[i],rgb[i],alpha[i]);++q_;
    }
};
// Two quads around the centre line p0 -> m -> p1 (each half: side, far end, centre, near end, so both triangles of the
// quad have area). The centre point takes `centre` and alpha a; the side points `edge` and rim; the ends `end`, rim.
inline void Spindle(Writer& w,const V3& p0,const V3& m,const V3& p1,const V3& side,uint32_t centre,uint32_t edge,uint32_t end,
                    double a,double rim){
    w.Quad({Add(m,side),p1,m,p0},{edge,end,centre,end},{rim,rim,a,rim});
    w.Quad({Sub(m,side),p0,m,p1},{edge,end,centre,end},{rim,rim,a,rim});
}
inline void Disc(Writer& w,const V3& c,const V3& right,const V3& up,double rx,double ry,uint32_t centre,uint32_t edge,double a,double rim){
    for(int i=0;i<kDiscSegments;++i){
        const double t0=2*kPi*i/kDiscSegments,t1=2*kPi*(i+1)/kDiscSegments;
        const V3 e0=Add(c,Add(Mul(right,rx*std::cos(t0)),Mul(up,ry*std::sin(t0))));
        const V3 e1=Add(c,Add(Mul(right,rx*std::cos(t1)),Mul(up,ry*std::sin(t1))));
        w.Quad({c,c,e1,e0},{centre,centre,edge,edge},{a,a,rim,rim});
    }
}
inline int QuadsOf(Shape s){return s==Shape::Billboard?kDiscSegments:2;}
// One particle's quads (Shapes). false: nothing visible (alpha or size below a pixel's worth) or no room left.
inline bool Shapes(Writer& w,const Visual& vis,const scd::Camera& cam,double gain,bool& cut){
    double a=vis.alpha*255.0;uint32_t centre=vis.centre,edge=vis.edge;
    if(vis.additive)Gain(gain,a,centre,edge);
    if(!(a>=0.5)||!(vis.width>=kMinSize)||!(vis.height>=kMinSize))return false;
    if(!w.Room(QuadsOf(vis.shape))){cut=true;return false;}
    const V3 eye{cam.eye.x,cam.eye.y,cam.eye.z},right{cam.right.x,cam.right.y,cam.right.z},up{cam.up.x,cam.up.y,cam.up.z};
    if(vis.shape==Shape::Billboard){
        const double c=std::cos(vis.rot),s=std::sin(vis.rot);
        const V3 r=Add(Mul(right,c),Mul(up,s)),u=Sub(Mul(up,c),Mul(right,s));
        const double rim=vis.additive||vis.formula==Formula::EnvPrmLerp?0.0:a*kSolidRim;
        Disc(w,vis.pos,r,u,vis.width*0.5,vis.height*0.5,centre,edge,a,rim);
        return true;
    }
    // The axis: the velocity, or (dir type "position") the way from the emitter to the particle.
    const V3 vel=Unit(vis.dir,up);
    const V3 dir=vis.dir_type==DirType::Position?Unit(vis.local,vel):vel;
    V3 p0,p1,m;
    if(vis.anchor_bottom){p0=vis.pos;p1=Add(vis.pos,Mul(dir,vis.height));m=Add(vis.pos,Mul(dir,vis.height*kLeafWidest));}
    else{p0=Sub(vis.pos,Mul(dir,vis.height*0.5));p1=Add(vis.pos,Mul(dir,vis.height*0.5));m=vis.pos;}
    const V3 facing=Unit(Cross(dir,Sub(eye,m)),right);   // across the axis, in the screen plane
    if(vis.shape==Shape::DirectionCross){
        Spindle(w,p0,m,p1,Mul(facing,vis.width*0.5),centre,edge,edge,a,0.0);
        return true;
    }
    // Direction: a flat blade turning about its axis (its spin), opaque to the rim when alpha-blended.
    const V3 side=Add(Mul(facing,std::cos(vis.rot)),Mul(Cross(dir,facing),std::sin(vis.rot)));
    const double rim=vis.additive?0.0:a;
    Spindle(w,p0,m,p1,Mul(side,vis.width*0.5),centre,edge,vis.additive?edge:centre,a,rim);
    return true;
}
// This frame's quads at game ms `now`: the additive particles from v[0], then the alpha-blended ones far to near; each
// group's node with its centroid. false: a non-finite view or vertex (nothing kept).
inline bool Build(const System& s,double now,const float view[12],double gain,FrameData& out,BuildStats* stats=nullptr){
    out.quads=0;out.add=Node{};out.blend=Node{};
    BuildStats st;scd::Camera cam;
    if(!scd::CameraOf(view,cam)){if(stats)*stats=st;return false;}
    const double f=s.Fraction(now);
    const V3 eye{cam.eye.x,cam.eye.y,cam.eye.z};
    static unsigned order[kPool];static double far2[kPool];unsigned blended=0;
    Writer w(out,0);bool cut=false;
    for(unsigned i=0;i<kPool;++i){
        Visual vis;
        if(!VisualOf(s.At(i),f,vis))continue;
        if(!vis.additive){const V3 d=Sub(vis.pos,eye);far2[i]=Dot(d,d);order[blended++]=i;continue;}
        if(Shapes(w,vis,cam,gain,cut))++st.drawn;else if(cut){++st.cut;cut=false;}
    }
    const int additive=w.Quads();
    std::sort(order,order+blended,[](unsigned a,unsigned b){return far2[a]>far2[b]||(far2[a]==far2[b]&&a<b);});
    for(unsigned k=0;k<blended;++k){
        Visual vis;VisualOf(s.At(order[k]),f,vis);
        if(Shapes(w,vis,cam,gain,cut))++st.drawn;else if(cut){++st.cut;cut=false;}
    }
    const int total=w.Quads();
    auto centroid=[&](int first,int count,Node& n,int dst)->bool{
        n.v=out.v+first*4;n.quads=count;n.dst=dst;
        double cx=0,cy=0,cz=0;
        for(int i=first*4;i<(first+count)*4;++i){
            const auto& p=out.v[i].p;
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
            cx+=p.x;cy+=p.y;cz+=p.z;
        }
        if(count)n.centre={float(cx/(count*4)),float(cy/(count*4)),float(cz/(count*4))};
        return true;
    };
    if(!centroid(0,additive,out.add,gx::kDstOne)||!centroid(additive,total-additive,out.blend,gx::kDstInvSrcAlpha)){
        out.quads=0;out.add=Node{};out.blend=Node{};if(stats)*stats=st;return false;
    }
    out.quads=total;
    if(stats)*stats=st;
    return true;
}
// How many of `want` quads the GX write arena takes after `cursor` bytes with kArenaReserve kept free.
inline int ArenaQuads(uint32_t cursor,int want){
    if(want<=0||cursor>=gx::kArenaBytes)return 0;
    const uint32_t free=gx::kArenaBytes-cursor;
    if(free<=kArenaReserve)return 0;
    const uint32_t fit=(free-kArenaReserve)/kBytesPerQuad;
    return fit<uint32_t(want)?int(fit):want;
}

// Game-thread runtime, as sword_charge_draw.h's (own natives copy, ring, owner thread and counters): Submit() from the
// player dispatch (once per native generation), Draw() when the native ExecOt reaches a node.
namespace runtime {
using Result=ot::runtime::Result;
struct Stats {unsigned frames_drawn,quads,reg_fails,draw_fails,restore_missing,restore_skipped,batch_closed,arena_cuts,quad_cuts;};
inline std::atomic<bool> ready{false};
inline uint32_t base=0,global_slot=0;inline ot::runtime::Natives natives;
inline thread_binding::Binding owner;   // the submitting game thread (skills-fix-01: handed over once it exited)
inline bool have_generation=false;inline uint32_t last_generation=0;inline unsigned slot_index=0;
inline FrameData ring[ot::kRing];
inline std::atomic<unsigned> reg_wrong_thread{0},reg_no_view{0},reg_build{0},reg_refused{0};   // reg_fails by reason
// Not failures (ot::runtime::RegisterNode): nodes not registered, wholly behind the eye plane / within zlimit, and nodes
// registered at the clamped point (their centroid nearer than zlimit).
inline std::atomic<unsigned> reg_behind{0},reg_near{0},reg_clamped{0};
inline std::atomic<unsigned> frames_drawn{0},quads{0},reg_fails{0},draw_fails{0},restore_missing{0},restore_skipped{0},
    batch_closed{0},arena_cuts{0},quad_cuts{0};
inline Stats Snapshot(){
    return {frames_drawn.load(),quads.load(),reg_fails.load(),draw_fails.load(),restore_missing.load(),restore_skipped.load(),
        batch_closed.load(),arena_cuts.load(),quad_cuts.load()};
}
inline void Start(uint32_t image_base,uint32_t global,const ot::runtime::Natives& n){
    base=image_base;global_slot=global;natives=n;owner.Reset();have_generation=false;
    ready.store(true,std::memory_order_release);
}
// OT callback (table 17): the trail's fault handling (sword_trail_draw_ot.h Draw) with the node's blend; frames_drawn
// counts nodes drawn.
inline void __cdecl Draw(void* data){
    const auto* n=static_cast<const Node*>(data);
    if(!n||!n->v||n->quads<=0||!ready.load(std::memory_order_acquire))return;
    float view[12]{};gx::Prior prior;
    if(!gx::DrawContext(base,global_slot,view,prior)||(n->dst==gx::kDstOne&&!prior.blend)){draw_fails.fetch_add(1);return;}
    const int count=ArenaQuads(prior.cursor,n->quads);
    if(count<n->quads)arena_cuts.fetch_add(1);
    if(count<=0)return;   // nothing queued, nothing to restore
    ot::Progress progress;
    const bool emitted=ot::runtime::EmittedQuads(natives.gx,n->v,count,view,progress,n->dst);
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
// This frame's particles at game ms `now`, once per native generation (pG+0x530C). WrongThread, Failed (no view or a
// non-finite vertex) and each Refused node (0xFFFF at the RegisterNode point) count as reg_fails; Empty (nothing
// visible) registers nothing; a node wholly behind the eye plane or within zlimit is not registered (reg_behind /
// reg_near; Hidden when no node went and none was refused).
inline Result Submit(const System& s,double now,double gain){
    if(!ready.load(std::memory_order_acquire))return Result::NotReady;
    if(!thread_binding::Serves(owner.Take())){reg_fails.fetch_add(1);reg_wrong_thread.fetch_add(1);return Result::WrongThread;}
    uint32_t global=0,generation=0;
    if(!gx::Get(global_slot,global)||!gx::Pointer(global)||!gx::Get(global+gx::kGlobalGeneration,generation))return Result::NoGeneration;
    if(have_generation&&generation==last_generation)return Result::SameGeneration;
    have_generation=true;last_generation=generation;
    float view[12]{};gx::SehMem m;BuildStats st;
    FrameData& d=ring[slot_index++%ot::kRing];
    const bool viewed=m.Read(global+gx::kGlobalView,view,sizeof(view));
    if(!viewed)reg_no_view.fetch_add(1);
    const bool built=viewed&&Build(s,now,view,gain,d,&st);
    if(viewed&&!built)reg_build.fetch_add(1);
    quad_cuts.fetch_add(st.cut);
    if(!built){d.quads=0;d.add=Node{};d.blend=Node{};reg_fails.fetch_add(1);return Result::Failed;}
    if(!d.quads)return Result::Empty;
    d.generation=generation;bool any=false,refused=false;
    for(Node* n:{&d.add,&d.blend}){
        if(!n->quads)continue;
        switch(ot::runtime::RegisterNode(natives,global,n,&Draw,n->v,n->quads*4,n->centre)){
        case ot::runtime::Placed::Behind:reg_behind.fetch_add(1);continue;
        case ot::runtime::Placed::Near:reg_near.fetch_add(1);continue;
        case ot::runtime::Placed::Refused:reg_fails.fetch_add(1);reg_refused.fetch_add(1);refused=true;continue;
        case ot::runtime::Placed::Clamped:reg_clamped.fetch_add(1);break;
        default:break;
        }
        any=true;quads.fetch_add(unsigned(n->quads));
    }
    return any?Result::Registered:refused?Result::Refused:Result::Hidden;
}
} // namespace runtime
} // namespace link_runtime::sword_particles::draw

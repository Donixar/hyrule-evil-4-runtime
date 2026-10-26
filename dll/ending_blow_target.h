#pragma once
// Read-only targets for the two sword actions. The down state is attributed in
// docs/ending-blow-spin-attack-2026-10-15.md; falling and getting up are NOT down.
#include "ending_blow_height.h"
#include "mortal_draw_target.h"
#include <array>
#include <cstdio>

namespace link_runtime::ending_blow {
inline bool Grounded(const mortal_draw::target::Raw& r) {
    return r.r0==1 && r.r1==0x1E && (r.be_flg&0x01000010U)==0x01000010U;
}
// Getting up (DownWake, r1 0x1F; its body 0x67EB0 keeps the down flags, then clears them). Never selects a
// target and is never lethal on its own: only for one that was down at the Ending Blow start (Revalidate rise).
inline bool Rising(const mortal_draw::target::Raw& r) {return r.r0==1 && r.r1==0x1F;}
// Runtime calls this only after the pinned EXE / common combat pins pass.
template<class Reader> bool NativePins(Reader& m,uint32_t base) {
    uint32_t wait=0,wake=0;
    if(!m.Read(base+0x7F3ABC,&wait,4)||wait!=base+0x3E0E0||
       !m.Read(base+0x7F3AC4,&wake,4)||wake!=base+0x67EB0)return false;
    constexpr unsigned char floor[]={0x81,0x8E,0xB0,4,0,0,0x10,0,0,1};
    constexpr unsigned char leave[]={0xC7,0x86,0xFC,0,0,0,1,0x1F,0,0};
    unsigned char bytes[10]{};
    return m.Read(base+0x3E161,bytes,10)&&!std::memcmp(bytes,floor,10)&&
           m.Read(base+0x3E1E8,bytes,10)&&!std::memcmp(bytes,leave,10);
}
}
namespace link_runtime::sword_actions::target {
namespace tg=mortal_draw::target;
constexpr unsigned kMaxTargets=16;
// spin_radius 2200 (was 1800): live 16-oct, Ganados swing from 1.6-1.9 m and step back while the spin
// charges; at 1800 the strike found targets=0 with the Ganado at 1.78-1.85 m. Horizontal, from Link.
// Ending reach is split for the leap (ending_blow_leap.h): selection and the pre-start check (Select
// mode) reach leap_range for a latched Z-lock and leap_free_range otherwise, with the asymmetric height band
// leap_band beyond strike_radius (ending_blow_height.h) and strike_band within it; the strike (Strike mode) rechecks
// strike_radius with strike_band. With the leap off (leap=false) every ending check uses strike_radius and the 400 height, as before.
// leap_range and leap_free_range 8500 (17-oct, Donald: 8.5 m with and without the lock; was 7000 / 4000, before
// that 3000 / 2000 and 4500): the native kick lands a Ganado 3.6-4.6 m away and he asked for a longer leap.
// Without a lock the 45-degree cone still applies; a lock dropped past the Z-lock's hold (ztarget.h) falls back to it.
enum class Mode {Select,Strike};
struct Config {bool line_waiver=false,leap=false;float spin_radius=2200.f,strike_radius=1200.f,vertical=ending_blow::height::kInPlace,
    leap_range=8500.f,leap_free_range=8500.f;ending_blow::height::Band leap_band=ending_blow::height::kFar,
    strike_band=ending_blow::height::kStrike;};
struct Target {uint64_t identity=0;uint32_t pointer=0,index=0,scene=0;uint8_t id=0,type=0;float distance=0;bool locked=false;};
struct List {std::array<Target,kMaxTargets> items{};unsigned count=0;};
// The Z-lock the knife key suspended (ztarget_runtime.h KnifeLatch): Z pool slot and pointer, id/type,
// the combat scene it is offered in and its age in 60 Hz frames. Z identities are not combat ones.
struct Prefer {unsigned index=0;uint32_t pointer=0,scene=0;uint8_t id=0,type=0;float age=0;};
constexpr float kLatchMaxAge=120.f;
// Every live Ganado near Link from the same pool pass (the leap's body scan), nearest kept when full.
constexpr unsigned kMaxBodies=32;
struct Bodies {std::array<tg::Vec3,kMaxBodies> items{};std::array<uint32_t,kMaxBodies> pointers{};unsigned count=0;};
inline void Collect(Bodies& b,uint32_t pointer,const tg::Vec3& pos,const tg::Vec3& p) {
    if(!tg::detail::Finite(pos))return;
    auto far2=[&](const tg::Vec3& q){const float dx=q.x-p.x,dz=q.z-p.z;return dx*dx+dz*dz;};
    unsigned at=b.count;
    if(b.count==kMaxBodies){
        at=0;for(unsigned j=1;j<kMaxBodies;++j)if(far2(b.items[j])>far2(b.items[at]))at=j;
        if(far2(b.items[at])<=far2(pos))return;
    }else ++b.count;
    b.items[at]=pos;b.pointers[at]=pointer;
}
// The bodies other than the selected target.
inline Bodies Others(const Bodies& all,const Target& t) {
    Bodies out;
    for(unsigned i=0;i<all.count;++i)if(all.pointers[i]!=t.pointer){out.items[out.count]=all.items[i];out.pointers[out.count++]=all.pointers[i];}
    return out;
}
// Every live Ganado family member except the Merchant (mortal_draw_target.h GanadoFamily/Merchant, 16-oct:
// r101/r105 are id 0x15, r108 id 0x17). No type, weapon, shield or Plaga list: the spin's hits stay native
// ordinary hits, and the Ending Blow's lethal kind is decided by the floor alone, its amount by the damage
// limit (Revalidate). The Mortal Draw (Classify) and Z-targeting take this same set since 16-oct.
inline bool Actor(const tg::Raw& r,uint32_t base) {
    return (r.be_flag&0x201)==1 && r.vtable==base+tg::layout::em10_vtable_rva &&
        tg::GanadoFamily(r.id) && !tg::Merchant(r) && r.hp>0 && r.hp_max>0 && r.hp<=r.hp_max &&
        (r.status&0x20) && (r.r0==1||r.r0==2);
}
// What the Ending Blow may select: a Ganado down (Grounded) and, with the leap on, one getting up (Rising, r1 0x1F).
// 24-oct, bug 5: refusals logged on presses while the kicked Ganado was already getting up (ending_none reason=not_grounded
// d=1096 dy=-398 r=1/0x1F, and three of the six reason=vertical presses showed r=1/0x1F); the strike is lethal on rise
// (Revalidate rise) since 16-oct, so the selection now takes the same state. A Ganado down wins over one getting up.
inline bool Finishable(const tg::Raw& r,const Config& c){return ending_blow::Grounded(r)||(c.leap&&ending_blow::Rising(r));}
inline float SelectRange(const Config& c,bool locked){return !c.leap?c.strike_radius:locked?c.leap_range:c.leap_free_range;}
// The height band a check uses at XZ distance d (ending_blow_height.h): 400 both ways for the spin, with the leap off and
// leap_band beyond the strike reach and strike_band within it and at the strike with the leap on (never stricter closer).
inline ending_blow::height::Band Band(const Config& c,bool ending,Mode mode,float d){
    if(!ending||!c.leap)return {c.vertical,c.vertical};
    if(mode==Mode::Strike)return c.strike_band;
    return d>c.strike_radius?c.leap_band:c.strike_band;
}
// Line gate at the knife press (review 24-oct L1): with the 6000 / 2500 bands and no sight test, a Ganado down on another
// floor (under a walkway, upstairs) could take the press and cost Link a leap into a floor or ceiling. A target whose
// height differs from Link's by more than the in-place band (400) needs a clear path, tested with the Z-target's native
// segment test (ztarget_los.h Native::Segment, EatMgr: walls, floors, doors). Not Link's head to the target's chest: from
// a few metres back on a cliff that line cuts the ledge's lip and would refuse the leaps Donald asked for (bug 5). The
// path is L-shaped like the leap: the target's own column, from just above it (+600, it lies down) to Link's head height
// (+1500), then across to Link's head. A floor or ceiling between the two levels (an upstairs room, a walkway, a balcony)
// cuts the column; open air over a cliff or a slope does not; a wall across cuts the second leg (that leap would be
// blocked anyway). A level target keeps the old behaviour with no native call. ready: the segment test is available
// (Z-target sight pins passed, no debug waiver); without it nothing is refused, as before. seg(from, to, attr): false =
// fault (refused), attr != 0 = blocked.
constexpr float kLineLow=600.f,kLineEye=1500.f;
struct LineSeg {tg::Vec3 from,to;};
inline void LinePath(const tg::Vec3& p,const tg::Vec3& t,LineSeg out[2]){
    const tg::Vec3 low{t.x,t.y+kLineLow,t.z},over{t.x,p.y+kLineEye,t.z},head{p.x,p.y+kLineEye,p.z};
    out[0]={low,over};out[1]={over,head};
}
inline bool NeedsLine(const Config& c,float dy){return c.leap&&!(std::fabs(dy)<=c.vertical);}
template<class Seg> bool LineRefuses(const Config& c,const tg::Vec3& p,const tg::Vec3& t,bool ready,Seg seg){
    if(!ready||!NeedsLine(c,t.y-p.y))return false;
    LineSeg s[2];LinePath(p,t,s);
    for(const LineSeg& l:s){uint32_t attr=0;if(!seg(l.from,l.to,attr)||attr)return true;}
    return false;
}
inline bool Reach(const tg::Vec3& p,const tg::Raw& r,const Config& c,bool ending,Mode mode,float& distance,bool locked=false) {
    if(!c.line_waiver||!tg::detail::Finite(p)||!tg::detail::Finite(r.pos))return false;
    const float dx=r.pos.x-p.x,dz=r.pos.z-p.z;
    distance=std::sqrt(dx*dx+dz*dz);
    const float radius=!ending?c.spin_radius:mode==Mode::Strike?c.strike_radius:SelectRange(c,locked);
    return std::isfinite(distance)&&distance<=radius&&Band(c,ending,mode,distance).Holds(r.pos.y-p.y);
}
inline bool Pool(const tg::Manager& m) {
    return m.count<=256 && uint64_t(m.array)+uint64_t(m.block)*m.count<=0xFFFFFFFFULL;
}
// prefer: the latched Z-lock (ending with the leap only); it wins over a nearer target when it passes
// Actor, Grounded, the lock range and a 90 degree cone. bodies: filled for the leap's body scan.
template<class Reader> List Select(Reader& m,uint32_t base,tg::Tracker& tracker,const tg::Vec3& p,float yaw,const Config& c,bool ending,
                                   const Prefer* prefer=nullptr,Bodies* bodies=nullptr) {
    List out;tg::Manager pool;
    if(bodies)*bodies=Bodies{};
    if(!c.line_waiver||!tracker.Scene()||!std::isfinite(yaw)||tg::ReadManager(m,base,tg::Config{},pool)!=tg::Reject::None||!Pool(pool))return out;
    const bool latch=ending&&c.leap&&prefer&&prefer->scene==tracker.Scene()&&prefer->age<=kLatchMaxAge;
    bool held=false;   // the latched actor was taken: no nearer one replaces it
    bool down=false;   // the kept target is Grounded: a Ganado getting up never replaces it
    for(unsigned i=0;i<pool.count;++i) {
        const uint32_t ptr=pool.array+i*pool.block;tg::Raw r;
        if(tg::ReadRaw(m,ptr,r)!=tg::Reject::None){tracker.Observe(i,ptr,false,r);continue;}
        const uint64_t identity=tracker.Observe(i,ptr,(r.be_flag&0x201)==1,r);
        if(bodies&&ending&&Actor(r,base))Collect(*bodies,ptr,r.pos,p);
        // Matched by slot and pointer (the Z pool has its own tracker), then the combat identity.
        const bool locked=latch&&i==prefer->index&&ptr==prefer->pointer&&r.id==prefer->id&&r.type==prefer->type;
        float distance=0;
        if(!identity||!Actor(r,base)||!Reach(p,r,c,ending,Mode::Select,distance,locked))continue;
        if(ending&&(!Finishable(r,c)||distance<100.f||
            (std::sin(yaw)*(r.pos.x-p.x)+std::cos(yaw)*(r.pos.z-p.z))/distance<(locked?0.f:0.70710678f)))continue;
        const Target t{identity,ptr,i,tracker.Scene(),r.id,r.type,distance,locked};
        if(ending){
            const bool g=ending_blow::Grounded(r);
            if(locked||(!held&&(!out.count||(g&&!down)||(g==down&&distance<out.items[0].distance)))){
                out.items[0]=t;out.count=1;held=locked;down=g;}
            continue;
        }
        // Nearest first, ties in manager order; discard the farthest if full.
        unsigned at=out.count;while(at&&distance<out.items[at-1].distance)--at;
        if(at==kMaxTargets)continue;
        const unsigned last=out.count<kMaxTargets?out.count++:kMaxTargets-1;
        for(unsigned j=last;j>at;--j)out.items[j]=out.items[j-1];
        out.items[at]=t;
    }
    return out;
}
// ---- ending_none (diagnostics only) ---------------------------------------------------------------
// A knife attack press the Ending Blow refused because Select found nothing (Donald, 16-oct night: his
// C-locked presses at 3.7-3.9 m left no trace). Reports why the latched Z-lock was not taken or, without
// one, the nearest Ganado down (then getting up) within SelectRange(locked) + kMissWindow. Raw reads only:
// no Tracker call and no write, so the runtime decides the same with or without it. The checks run in
// Select's order and the first failing one is the reason; `passed` means it passed on this re-read.
// LatchScene and LatchStale are defensive, reached only offline: SwordLatch stamps the latch with the
// current combat scene and ztarget_runtime.h clears it past 120 frames or on an epoch change.
// Bodies is not Diagnose's: the corridor guard (ending_blow_leap.h Corridor) refused a selected target because
// another Ganado on the path would land Link beyond the strike reach (BodiesReport).
// NotDown (24-oct, bug 5): no latch and no Ganado down or getting up within the window, but a live one stands within
// kNotDownWindow: the line names its routine (r=R0/R1), e.g. a kicked Ganado still flying (not yet DownWakeWait).
// Before it such presses left no line at all, which is how the same-height failures went unexplained.
enum class Miss : uint8_t {None,NoScan,LatchScene,LatchStale,LatchGone,NotActor,OutOfRange,Vertical,NotGrounded,TooClose,Cone,Bodies,Passed,
    NotDown};
inline const char* MissName(Miss w){
    static const char* const n[]={"none","no_scan","latch_scene","latch_stale","latch_gone","not_actor","out_of_range",
        "vertical","not_grounded","too_close","cone","bodies","passed","not_down"};
    return n[unsigned(w)];
}
constexpr float kMissWindow=1500.f;
constexpr float kNotDownWindow=3000.f;
// why None: nothing worth a line (no latch and no Ganado down near Link). range/height: the reach and the
// height band the check used (logged height=-down/+up); cosine: the facing cone value (1 = straight ahead).
// bodies/travel/land/strike: reason=bodies only (counted bodies, the travel left, landing distance, strike reach).
struct MissReport {Miss why=Miss::None;bool latch=false;float age=0,d=0,dy=0,range=0,down=0,up=0,cosine=0;uint32_t pointer=0;
    uint8_t id=0,type=0,r0=0,r1=0;unsigned bodies=0;float travel=0,land=0,strike=0;};
// The measured fields for raw r at ptr (Diagnose's judge and BodiesReport).
inline void Fill(MissReport& out,const tg::Raw& r,uint32_t ptr,const tg::Vec3& p,float yaw,const Config& c,bool locked){
    out.pointer=ptr;out.id=r.id;out.type=r.type;out.r0=r.r0;out.r1=r.r1;
    const float dx=r.pos.x-p.x,dz=r.pos.z-p.z;
    out.d=std::sqrt(dx*dx+dz*dz);out.dy=r.pos.y-p.y;
    const auto band=Band(c,true,Mode::Select,out.d);
    out.range=SelectRange(c,locked);out.down=band.down;out.up=band.up;
    out.cosine=out.d>0.f?(std::sin(yaw)*dx+std::cos(yaw)*dz)/out.d:0.f;
}
template<class Reader> MissReport Diagnose(Reader& m,uint32_t base,uint32_t scene,const tg::Vec3& p,float yaw,const Config& c,
                                           const Prefer* prefer) {
    MissReport out;tg::Manager pool;
    out.latch=c.leap&&prefer;if(out.latch)out.age=prefer->age;
    const bool scan=c.line_waiver&&scene&&std::isfinite(yaw)&&tg::detail::Finite(p)&&
        tg::ReadManager(m,base,tg::Config{},pool)==tg::Reject::None&&Pool(pool);
    auto judge=[&](const tg::Raw& r,uint32_t ptr,bool locked){
        Fill(out,r,ptr,p,yaw,c,locked);
        if(!Actor(r,base))return Miss::NotActor;
        if(!(out.d<=out.range))return Miss::OutOfRange;
        if(!ending_blow::height::Band{out.down,out.up}.Holds(out.dy))return Miss::Vertical;
        if(!Finishable(r,c))return Miss::NotGrounded;
        if(out.d<100.f)return Miss::TooClose;
        if(out.cosine<(locked?0.f:0.70710678f))return Miss::Cone;
        return Miss::Passed;
    };
    if(out.latch){
        out.pointer=prefer->pointer;out.id=prefer->id;out.type=prefer->type;
        if(!scan){out.why=Miss::NoScan;return out;}
        if(prefer->scene!=scene){out.why=Miss::LatchScene;return out;}
        const uint32_t ptr=pool.array+prefer->index*pool.block;tg::Raw r;
        if(prefer->index>=pool.count||ptr!=prefer->pointer||tg::ReadRaw(m,ptr,r)!=tg::Reject::None||
           r.id!=prefer->id||r.type!=prefer->type){out.why=Miss::LatchGone;return out;}
        const Miss why=judge(r,ptr,true);
        out.why=prefer->age>kLatchMaxAge?Miss::LatchStale:why;
        return out;
    }
    if(!scan)return out;
    // No latch: the nearest Ganado down, else the nearest getting up, within the window (lock range + 1500: 10000).
    const float window=SelectRange(c,true)+kMissWindow;
    bool have=false,have_down=false;float best=0;uint32_t pick_ptr=0;tg::Raw pick;
    for(unsigned i=0;i<pool.count;++i){
        const uint32_t ptr=pool.array+i*pool.block;tg::Raw r;
        if(tg::ReadRaw(m,ptr,r)!=tg::Reject::None||!Actor(r,base)||!tg::detail::Finite(r.pos))continue;
        const bool down=ending_blow::Grounded(r);
        if(!down&&!(r.r0==1&&r.r1==0x1F))continue;
        const float dx=r.pos.x-p.x,dz=r.pos.z-p.z,d=std::sqrt(dx*dx+dz*dz);
        if(!(d<=window)||(have_down&&!down))continue;
        if(!have||(down&&!have_down)||d<best){have=true;have_down=down;best=d;pick_ptr=ptr;pick=r;}
    }
    if(have){out.why=judge(pick,pick_ptr,false);return out;}
    // Nothing down: the nearest live Ganado family member (any routine) within kNotDownWindow, for its routine only.
    for(unsigned i=0;i<pool.count;++i){
        const uint32_t ptr=pool.array+i*pool.block;tg::Raw r;
        if(tg::ReadRaw(m,ptr,r)!=tg::Reject::None||(r.be_flag&0x201)!=1||r.vtable!=base+tg::layout::em10_vtable_rva||
           !tg::GanadoFamily(r.id)||tg::Merchant(r)||r.hp<=0||!tg::detail::Finite(r.pos))continue;
        const float dx=r.pos.x-p.x,dz=r.pos.z-p.z,d=std::sqrt(dx*dx+dz*dz);
        if(!(d<=kNotDownWindow)||(have&&d>=best))continue;
        have=true;best=d;pick_ptr=ptr;pick=r;
    }
    if(have){Fill(out,pick,pick_ptr,p,yaw,c,false);out.why=Miss::NotDown;}
    return out;
}
// The corridor guard refused target t (raw r): reason=bodies, with what the guard counted and computed.
inline MissReport BodiesReport(const tg::Raw& r,const Target& t,const tg::Vec3& p,float yaw,const Config& c,const Prefer* prefer,
                               unsigned bodies,float travel,float land){
    MissReport out;out.why=Miss::Bodies;out.latch=c.leap&&prefer;if(out.latch)out.age=prefer->age;
    Fill(out,r,t.pointer,p,yaw,c,t.locked);out.bodies=bodies;out.travel=travel;out.land=land;out.strike=c.strike_radius;
    return out;
}
inline int FormatMiss(char* out,size_t n,uint32_t frame,const MissReport& r){
    char latch[16]="none";
    if(r.latch)std::snprintf(latch,sizeof(latch),"%.0f",double(r.age));
    const int k=std::snprintf(out,n,"ending_none frame=%u latch=%s reason=%s d=%.0f dy=%.0f range=%.0f height=-%.0f/+%.0f cos=%.2f "
        "actor=0x%08X id=0x%02X type=0x%02X r=%u/0x%02X",frame,latch,MissName(r.why),double(r.d),double(r.dy),double(r.range),
        double(r.down),double(r.up),double(r.cosine),r.pointer,unsigned(r.id),unsigned(r.type),unsigned(r.r0),unsigned(r.r1));
    if(k<0||r.why!=Miss::Bodies||size_t(k)>=n)return k;
    const int e=std::snprintf(out+k,n-size_t(k)," bodies=%u travel=%.0f land=%.0f strike=%.0f",r.bodies,double(r.travel),
        double(r.land),double(r.strike));
    return e<0?e:k+e;
}
// cap: tg::LethalCap of a lethal target (0 = none): the HP limit of the damage::Kind::Lethal amount.
// rising: lethal although getting up (rise, below).
struct Recheck {bool same=false,reachable=false,lethal=false,rising=false;int cap=0;tg::Raw raw;};
// Select mode before the start (the leap ranges), Strike mode at the strike (strike_radius; strike_band with the leap on).
// rise: the target was down when this Ending Blow started (spin_attack_runtime.h SwordAction::grounded). With the leap
// on, one that has begun getting up since (Rising, r1 0x1F) keeps the lethal kind and its cap (Donald, 16-oct night:
// lethal on rise; the kick leaves a Ganado down only 2.5-3.5 s and a 4-7 m leap takes 1 s). Fully up again (any
// other routine) stays ordinary, and with the leap off nothing changes.
template<class Reader> Recheck Revalidate(Reader& m,uint32_t base,tg::Tracker& tracker,const Target& t,const tg::Vec3& p,const Config& c,bool ending,
                                          Mode mode=Mode::Strike,bool rise=false) {
    Recheck out;tg::Manager pool;
    if(!t.identity||t.scene!=tracker.Scene()||tg::ReadManager(m,base,tg::Config{},pool)!=tg::Reject::None||!Pool(pool)||
       t.index>=pool.count||pool.array+t.index*pool.block!=t.pointer)return out;
    if(tg::ReadRaw(m,t.pointer,out.raw)!=tg::Reject::None){tracker.Observe(t.index,t.pointer,false,out.raw);return out;}
    const auto& r=out.raw;
    out.same=tracker.Observe(t.index,t.pointer,(r.be_flag&0x201)==1,r)==t.identity&&r.id==t.id&&r.type==t.type;
    float distance=0;out.reachable=out.same&&Actor(r,base)&&Reach(p,r,c,ending,mode,distance,t.locked);
    // Lethal = every grounded Actor (Donald, 16-oct: everything that can be knocked down is finishable).
    // Second 16-oct decision, damage limit: on a heavy target (max HP > 1500, a chainsaw or a claw type, tg::Heavy) the
    // lethal kind carries cap 1000, so the blow removes at most 1000 HP and kills only at HP <= 1000. The
    // native calc still caps a knife hit on a chainsaw carrier at 1000 (0x4B061), and an armoured part or an
    // emerged Plaga may scale the amount down: the runtime logs a survivor as ending_survived.
    out.lethal=out.reachable&&ending&&(ending_blow::Grounded(r)||(rise&&c.leap&&ending_blow::Rising(r)));
    out.rising=out.lethal&&!ending_blow::Grounded(r);
    out.cap=out.lethal?tg::LethalCap(r):0;
    return out;
}
}

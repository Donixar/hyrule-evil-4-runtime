#pragma once
#include "mortal_draw_target.h"
#include <initializer_list>
// Camera-only MVP, before native QFPS collision. No actor, health, input or final-camera writes.
// Attribution and deferred LOS/HUD/locomotion: docs/z-targeting-design-2026-10-15.md.
namespace link_runtime::ztarget {
namespace mt=link_runtime::mortal_draw::target;
using Vec=mt::Vec3;
struct Matrix { float v[3][4]; };
struct Offset { Vec campos[2]; Vec target; float roll,fovy; };
inline float Turn(float a,float b){return mt::Turn(a,b);}
static_assert(sizeof(Matrix)==0x30 && sizeof(Offset)==0x2C,"UHD camera argument layout");
inline bool Finite(Vec v){return mt::detail::Finite(v);}
inline float Dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline bool ValidDt(float dt){return std::isfinite(dt)&&dt>=0.03f&&dt<=2.f;}
struct Filter {
    bool known=false;uint32_t frame=0;float yaw=0;
    void Reset(){known=false;}
    float Step(uint32_t tick,float dt,float native,float desired,bool release,float tau_ms){
        if(!known){known=true;frame=tick-1;yaw=native;}
        if(tick!=frame){
            const float tau=(release?180.f:tau_ms)/1000.f;
            yaw=Turn(0.f,yaw-std::expm1(-dt/(30.f*tau))*Turn(yaw,desired));frame=tick;
        }
        return yaw;
    }
};
inline Vec World(const Matrix& m,Vec v){
    return {m.v[0][0]*v.x+m.v[0][1]*v.y+m.v[0][2]*v.z,
            m.v[1][0]*v.x+m.v[1][1]*v.y+m.v[1][2]*v.z,
            m.v[2][0]*v.x+m.v[2][1]*v.y+m.v[2][2]*v.z};
}
inline bool Rigid(const Matrix& m){
    for(const auto& row:m.v)for(float f:row)if(!std::isfinite(f))return false;
    const Vec a{m.v[0][0],m.v[0][1],m.v[0][2]},b{m.v[1][0],m.v[1][1],m.v[1][2]},c{m.v[2][0],m.v[2][1],m.v[2][2]};
    const Vec cross{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    return std::fabs(Dot(a,a)-1)<0.01f&&std::fabs(Dot(b,b)-1)<0.01f&&std::fabs(Dot(c,c)-1)<0.01f&&
        std::fabs(Dot(a,b))<0.01f&&std::fabs(Dot(a,c))<0.01f&&std::fabs(Dot(b,c))<0.01f&&Dot(cross,c)>0.99f;
}
inline Vec RotateVector(const Matrix& m,Vec v,float yaw){
    const Vec w=World(m,v);const float c=std::cos(yaw),s=std::sin(yaw);
    const Vec y{c*w.x+s*w.z,w.y,-s*w.x+c*w.z};
    return {m.v[0][0]*y.x+m.v[1][0]*y.y+m.v[2][0]*y.z,
            m.v[0][1]*y.x+m.v[1][1]*y.y+m.v[2][1]*y.z,
            m.v[0][2]*y.x+m.v[1][2]*y.y+m.v[2][2]*y.z};
}
inline bool Rotate(const Matrix& m,const Offset& in,float yaw,Offset& out){
    if(!Rigid(m)||!Finite(in.campos[0])||!Finite(in.campos[1])||!Finite(in.target)||
       !std::isfinite(in.roll)||!std::isfinite(in.fovy)||!std::isfinite(yaw))return false;
    out=in;out.campos[0]=RotateVector(m,in.campos[0],yaw);out.campos[1]=RotateVector(m,in.campos[1],yaw);
    out.target=RotateVector(m,in.target,yaw);
    return Finite(out.campos[0])&&Finite(out.campos[1])&&Finite(out.target);
}
enum class Phase {Idle,Recenter,Locked,Release,Suspended};
struct Input {bool safe=false,hold=false;uint32_t frame=0,epoch=0;float dt=0;};
struct Controller {
    Phase phase=Phase::Idle;Filter filter;bool known=false,held=false;uint32_t frame=0,epoch=0;float release_s=0;
    // The last Begin's frame-gap/epoch suspend and the frame it broke from (runtime `zt frame_gap` diagnostics).
    bool gap=false,gap_epoch=false;uint32_t gap_from=0;
    void Suspend(){phase=Phase::Suspended;filter.Reset();release_s=0;}
    void Begin(const Input& i,bool target){
        const bool same=known&&i.frame==frame;
        const bool broken=known&&(epoch!=i.epoch||(!same&&uint32_t(i.frame-frame)!=1));
        gap=broken;gap_epoch=broken&&epoch!=i.epoch;gap_from=frame;
        known=true;frame=i.frame;epoch=i.epoch;
        if(!i.safe||!ValidDt(i.dt)||broken){Suspend();held=i.hold;return;}
        if(same)return;
        if(phase==Phase::Suspended){if(!i.hold){phase=Phase::Idle;held=false;}return;}
        if(i.hold&&!held){phase=target?Phase::Locked:Phase::Recenter;filter.Reset();release_s=0;}
        else if(!i.hold&&held&&(phase==Phase::Locked||phase==Phase::Recenter)){phase=Phase::Release;release_s=0;}
        if(phase==Phase::Release){release_s+=i.dt/30.f;if(release_s>=0.7f){phase=Phase::Idle;filter.Reset();}}
        held=i.hold;
    }
    bool Active()const{return phase==Phase::Recenter||phase==Phase::Locked||phase==Phase::Release;}
};
// Neutral Z-target (shield-guard-01, Donald 18-oct night; TP's parallel targeting): the lock input held with no target
// keeps a state instead of doing nothing. Recenter is that phase (camera behind Link, held, no auto-acquire: a Ganado that
// walks into view is only locked by a new press). Link keeps the yaw he had when it began (no facing writes), A/D strafe
// around a virtual target kNeutralReach ahead along that yaw, the shield guard goes up (ShieldGuard) and the Shield Bash
// works. A soft suspension (the knife: a bash or slash) brings it back within kNeutralResume seconds while the input stays
// held; a hard release (aim, fire, action, damage, scene) ends it.
constexpr float kNeutralReach=5000.f,kNeutralResume=1.0f;
constexpr uint64_t kNeutralIdentity=0x4E45555452414C31ULL;   // "NEUTRAL1": the strafe's identity in neutral
enum class NeutralEvent:uint8_t {None,Enter,Exit,Resume};
struct Neutral {
    bool active=false,resume=false;float yaw=0,waited=0;uint32_t epoch=0;
    // After the controller's Begin. phase/held: the controller now; link_yaw: Link's yaw (+0xA4); soft: the phase
    // is a soft suspension (knife) with the input held (a resume may follow); hard: a hard release.
    NeutralEvent Step(Phase phase,bool held,float link_yaw,uint32_t ep,bool soft,bool hard){
        const bool now=phase==Phase::Recenter&&held;
        if(now&&!active){
            const bool resumed=resume&&epoch==ep;
            if(!resumed||!std::isfinite(yaw))yaw=link_yaw;
            active=true;resume=false;waited=0;epoch=ep;
            return resumed?NeutralEvent::Resume:NeutralEvent::Enter;
        }
        if(!now&&active){
            active=false;
            resume=soft&&!hard&&phase==Phase::Suspended&&held;waited=0;epoch=ep;
            return NeutralEvent::Exit;
        }
        if(hard||(!held&&phase!=Phase::Suspended))resume=false;
        return NeutralEvent::None;
    }
    // Soft suspension still pending (same epoch, within kNeutralResume of safe frames); seconds: this safe frame.
    bool Pending(uint32_t ep,float seconds){
        if(!resume||epoch!=ep)return resume=false;
        waited+=seconds>0?seconds:0;
        if(waited>kNeutralResume)return resume=false;
        return true;
    }
};
inline const char* NeutralName(NeutralEvent e){return e==NeutralEvent::Enter?"enter":e==NeutralEvent::Exit?"exit":e==NeutralEvent::Resume?"resume":"none";}
// The strafe's virtual target in neutral: kNeutralReach ahead of Link along the neutral yaw (RE4 yaw = atan2(dx, dz)).
inline Vec NeutralTarget(Vec pos,float yaw){return {pos.x+kNeutralReach*std::sin(yaw),pos.y,pos.z+kNeutralReach*std::cos(yaw)};}
inline const char* Name(Phase p){switch(p){case Phase::Recenter:return "recenter";case Phase::Locked:return "locked";
    case Phase::Release:return "release";case Phase::Suspended:return "suspend";default:return "idle";}}
inline bool Actor(const mt::Raw& r,uint32_t base){
    // Every live Ganado family member except the Merchant (mortal_draw_target.h GanadoFamily/Merchant, 16-oct:
    // r101/r105 are id 0x15, r108 id 0x17). Camera only, so no type, chainsaw, crossbow or Plaga exclusion (the
    // old type list {0,1,3,4,5} refused real Ganado types 7, 0xB, 0xC, 0xF, 0x10 and 0x18 seen in the census);
    // never inherit Mortal Draw's back/idle requirement.
    return (r.be_flag&0x201)==1&&r.vtable==base+mt::layout::em10_vtable_rva&&mt::GanadoFamily(r.id)&&!mt::Merchant(r)&&
        r.hp>0&&r.hp_max>=r.hp&&(r.status&0x20)&&(r.r0==1||r.r0==2)&&Finite(r.pos)&&std::isfinite(r.ang_y);
}
struct Candidate {
    bool valid=false,visible=false;float distance=0,angle=0;unsigned index=0;uint64_t identity=0;
    uint32_t pointer=0;mt::Raw raw;
};
inline int Pick(const Candidate* a,unsigned count,int current,int direction){
    float best=1e30f;int chosen=-1;
    for(unsigned n=0;n<count;++n){
        const auto& c=a[n];if(!c.valid||!c.visible)continue;
        float score=0.75f*std::fabs(c.angle)/0.87266463f+0.25f*c.distance/6000.f;
        if(current>=0&&unsigned(current)<count&&direction){
            const float d=Turn(a[current].angle,c.angle)*float(direction);
            if(d<=0.001f)continue;score=d;
        }
        if(score<best){best=score;chosen=int(n);}
    }
    return chosen<0?current:chosen;
}
inline bool LineAllowed(bool qualified_clear,bool waiver){return qualified_clear||waiver;}
inline bool ExactOne(const wchar_t* s){return s&&s[0]==L'1'&&!s[1];}
inline unsigned Hooks(bool enabled){return enabled?2U:0U;}
struct ButtonState{bool hold=false;int change=0;};
struct Buttons{
    bool armed=false,next=false,stick_armed=true;
    ButtonState Read(bool focused,bool hold,bool change,int stick,bool consume=true){
        if(!focused){armed=false;next=change;stick_armed=false;return {};}
        if(!consume)return {hold&&armed,0}; // another CameraMove in this tick cannot consume V
        if(!hold){armed=true;next=change;stick_armed=std::abs(stick)<9830;return {};}
        if(!armed){next=change;return {};}
        ButtonState out{true,change&&!next?1:0};next=change;
        if(std::abs(stick)<9830)stick_armed=true;
        else if(stick_armed&&std::abs(stick)>22937){out.change=stick>0?1:-1;stick_armed=false;}
        return out;
    }
};
struct View { Matrix view{};float projection[4][4]{};float yaw=0;bool valid=false; };
inline bool InView(const View& camera,Vec p){
    if(!camera.valid||!Finite(p))return false;
    Vec v=World(camera.view,p);v.x+=camera.view.v[0][3];v.y+=camera.view.v[1][3];v.z+=camera.view.v[2][3];
    if(!Finite(v)||v.z>=-1.f)return false;
    const float v4[4]={v.x,v.y,v.z,1};float clip[4]{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)clip[i]+=camera.projection[i][j]*v4[j];
    for(float f:clip)if(!std::isfinite(f))return false;
    return clip[3]>0.001f&&std::fabs(clip[0])<=clip[3]&&std::fabs(clip[1])<=clip[3];
}
// Lock hold: 9000 (was 7500; 17-oct, Donald's 8.5 m Ending Blow leap with the lock, EndingBlowLeapRange 8500).
// Acquisition stays at 6000 in view; a lock taken nearer now holds while Link backs off to 9 m.
constexpr float kHold=9000.f;
// Independent tracker. Reads the pool once per game tick, never modifies the combat tracker.
class Pool {
    mt::Tracker tracker;mt::Manager manager{};uint8_t types[256]{};uint32_t vtables[256]{};
public:
    Candidate candidates[256]{};unsigned count=0;uint32_t epoch=0;
    void Reset(uint32_t e){epoch=e;count=0;manager={};tracker.BeginScene(e);}
    // visible = geometric only (range, cone, frustum); line of sight is the runtime's (ztarget_los.h), per pick.
    template<class Mem> bool Read(Mem& m,uint32_t base,uint32_t e,Vec player,const View& camera){
        if(epoch!=e)Reset(e);
        mt::Manager now;mt::Config cfg;cfg.max_slots=256;
        if(mt::ReadManager(m,base,cfg,now)!=mt::Reject::None||(now.array&3)||
           uint64_t(now.array)+uint64_t(now.count)*now.block>0x100000000ULL){Reset(e);return false;}
        if(now.array!=manager.array||now.block!=manager.block||now.count!=manager.count){tracker.BeginScene(e);manager=now;}
        count=now.count;
        for(unsigned i=0;i<count;++i){
            Candidate c;c.index=i;c.pointer=now.array+i*now.block;
            const bool ok=mt::ReadRaw(m,c.pointer,c.raw)==mt::Reject::None&&Actor(c.raw,base);
            if(types[i]!=c.raw.type||vtables[i]!=c.raw.vtable)tracker.Observe(i,c.pointer,false,c.raw);
            types[i]=c.raw.type;vtables[i]=c.raw.vtable;c.identity=tracker.Observe(i,c.pointer,ok,c.raw);
            float yaw=0;const float dx=c.raw.pos.x-player.x,dz=c.raw.pos.z-player.z;
            c.distance=std::sqrt(dx*dx+dz*dz);c.angle=Turn(camera.yaw,std::atan2(dx,dz));
            c.valid=ok&&mt::FaceYaw(player,c.raw.pos,yaw)&&c.distance>=300&&c.distance<=kHold&&std::fabs(c.raw.pos.y-player.y)<=1800;
            Vec chest=c.raw.pos;chest.y+=1200;
            c.visible=c.valid&&c.distance<=6000&&std::fabs(c.angle)<=0.87266463f&&InView(camera,chest);
            candidates[i]=c;
        }
        mt::Manager end;
        if(mt::ReadManager(m,base,cfg,end)!=mt::Reject::None||std::memcmp(&now,&end,sizeof(now))){Reset(e);return false;}
        return true;
    }
    template<class Mem> bool Revalidate(Mem& m,uint32_t base,const Candidate& lock,Vec player,Vec& target){
        mt::Manager now;mt::Config cfg;mt::Raw raw;
        if(lock.index>=count)return false;
        const Candidate& fresh=candidates[lock.index];
        if(!fresh.valid||fresh.pointer!=lock.pointer||fresh.identity!=lock.identity||!lock.identity||
           mt::ReadManager(m,base,cfg,now)!=mt::Reject::None||std::memcmp(&now,&manager,sizeof(now))||
           now.array+lock.index*now.block!=lock.pointer||mt::ReadRaw(m,lock.pointer,raw)!=mt::Reject::None||!Actor(raw,base)||
           raw.id!=fresh.raw.id||raw.type!=fresh.raw.type||raw.hp>fresh.raw.hp||raw.hp_max!=fresh.raw.hp_max)return false;
        const float dx=raw.pos.x-player.x,dz=raw.pos.z-player.z,d2=dx*dx+dz*dz;
        if(d2<300.f*300.f||d2>kHold*kHold||std::fabs(raw.pos.y-player.y)>1800)return false;
        target=raw.pos;return true;
    }
};
}

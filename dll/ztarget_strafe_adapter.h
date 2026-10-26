#pragma once
#include "ztarget_strafe.h"
#include "ztarget_native.h"
namespace link_runtime::ztarget::strafe::adapter {
struct Context {
    uint32_t base=0,player=0,epoch=0;const Frame* snapshot=nullptr;Pool* pool=nullptr;const Candidate* locked=nullptr;
    bool live=false,enabled=false,observe=false,face_target=false,qualified=false,thread=false,dispatch_clear=false,busy=false;
    bool focused=false,focus_lost=false,held=false,lock_held=false,lock_active=false,waiver=false;
    bool neutral=false;float neutral_yaw=0;   // neutral Z-target (ztarget.h Neutral): strafe around a virtual target
};
inline bool Enabled(const Context& c){return c.base&&c.player&&c.snapshot&&c.live&&c.enabled&&!c.observe&&
    c.face_target&&c.qualified&&c.thread;}
template<class Mem>bool Read(const Context& c,Mem& m,Sample& out){
    if(!Enabled(c))return false;
    Frame f;
    if(!ReadFrame(m,c.base,f)||f.player!=c.player||uint64_t(f.player)+0x7E0>0x100000000ULL)return false;
    Sample s;s.actor=f.player;s.frame=f.frame;s.epoch=c.epoch;s.routine=f.routine;s.keys=f.keys;s.pos=f.pos;s.yaw=f.yaw;s.dt=f.dt;
    s.focused=c.focused&&!c.focus_lost;
    s.safe=c.dispatch_clear&&!c.busy&&Safe(f)&&SameScene(*c.snapshot,f)&&uint32_t(f.frame-c.snapshot->frame)<=1;
    uint32_t table=0,fcv=0,seq=0,normal_fcv=0,normal_seq=0,damaged_fcv=0,damaged_seq=0;
    if(!Get(m,f.player+0x46C,table)||!table||uint64_t(table)+0x18C>0x100000000ULL||
       !Get(m,f.player+0x1D8,fcv)||!Get(m,f.player+0x280,seq)||!Get(m,f.player+0x218,s.attrs)||
       !Get(m,f.player+0x21C,s.motion_flags)||!Get(m,f.player+0x2A8,s.secondary)||
       !Get(m,table+8,normal_fcv)||!Get(m,table+12,normal_seq)||
       !Get(m,table+0x184,damaged_fcv)||!Get(m,table+0x188,damaged_seq))return false;
    const auto pair=[&](uint32_t a,uint32_t b){return a&&a==fcv&&b<=0xFFFFFFFB&&seq==(b?b+4:0);};
    s.motion=fcv;s.sequence=seq;s.motion_table=table;
    s.walk_motion=pair(normal_fcv,normal_seq)||pair(damaged_fcv,damaged_seq);
    if(s.secondary){
        if(uint64_t(s.secondary)+0xCC>0x100000000ULL||!Get(m,f.player+0x7DC,s.neck)||
           !s.neck||uint64_t(s.neck)+0x1C>0x100000000ULL||
           !Get(m,s.neck+0x14,s.neck_left)||!Get(m,s.neck+0x18,s.neck_right)||
           !Get(m,s.secondary,s.secondary_motion)||!Get(m,s.secondary+0x40,s.secondary_attrs)||
           !Get(m,s.secondary+0x44,s.secondary_flags)||!Get(m,s.secondary+0xA8,s.secondary_sequence)||
           !Get(m,s.secondary+0xC8,s.secondary_weight))return false;
    }
    const bool lock=s.safe&&c.lock_active&&c.lock_held&&c.waiver&&c.pool&&c.locked&&
        c.pool->Revalidate(m,c.base,*c.locked,s.pos,s.target);
    if(lock)s.identity=c.locked->identity;
    // Neutral: no target, so A/D circle a point kNeutralReach ahead along the neutral yaw (a near-straight sidestep
    // that keeps Link facing that way). Recomputed from each sample's own position.
    const bool neutral=!lock&&s.safe&&c.neutral&&c.lock_held&&std::isfinite(c.neutral_yaw)&&Finite(s.pos);
    if(neutral){s.target=NeutralTarget(s.pos,c.neutral_yaw);s.identity=kNeutralIdentity;}
    // Loss of C/lock returns a readable native sample so owned Walk can stop before another root step.
    s.held=c.held&&(lock||neutral);out=s;return true;
}
inline bool Equal(Vec a,Vec b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
inline bool Equal(const Sample& a,const Sample& b){
    return a.actor==b.actor&&a.frame==b.frame&&a.epoch==b.epoch&&a.routine==b.routine&&a.motion_flags==b.motion_flags&&
        a.secondary==b.secondary&&a.motion==b.motion&&a.sequence==b.sequence&&a.motion_table==b.motion_table&&
        a.neck==b.neck&&a.neck_left==b.neck_left&&a.neck_right==b.neck_right&&
        a.secondary_motion==b.secondary_motion&&a.secondary_sequence==b.secondary_sequence&&
        a.secondary_flags==b.secondary_flags&&a.secondary_attrs==b.secondary_attrs&&a.secondary_weight==b.secondary_weight&&
        a.identity==b.identity&&a.keys==b.keys&&a.attrs==b.attrs&&Equal(a.pos,b.pos)&&Equal(a.target,b.target)&&
        a.yaw==b.yaw&&a.dt==b.dt&&a.safe==b.safe&&a.focused==b.focused&&a.held==b.held&&a.walk_motion==b.walk_motion;
}
template<class Mem>bool Routine(const Context& c,Mem& m,uint32_t actor,uint32_t expected,uint32_t next){
    Sample a,b;
    if(actor!=c.player||!Read(c,m,a)||a.routine!=expected||!a.safe||!a.focused)return false;
    if(next==0x100){
        Controller admission;admission.config={true,false,true};
        if(!admission.Begin(a)||!admission.NeedsWalk())return false;
    }else if(next==0){
        const bool stopped=!a.held||(a.keys&12)==0||(a.keys&12)==12;
        if(!stopped||(a.routine&0xFFFF)!=0x100||!a.walk_motion||(a.keys&3))return false;
    }else return false;
    if(!m.Writable(actor+0xFC,4)||!Read(c,m,b)||!Equal(a,b))return false;
    return m.Write(actor+0xFC,&next,4);
}
template<class Mem>bool Pose(const Context& c,Mem& m,const Sample& expected,const Output& out){
    constexpr float pi=3.14159265358979323846f;
    if(!Finite(out.pos)||!std::isfinite(out.yaw)||out.yaw<-pi||out.yaw>pi||out.pos.y!=expected.pos.y)return false;
    Sample a,b;
    if(!Read(c,m,a)||!Equal(a,expected)||!a.safe||!a.focused||!a.held||!a.identity||
       (a.routine&0xFFFF)!=0x100||!a.walk_motion||!RootOnly(a)||!(a.attrs&1)||(a.motion_flags&0x40000000)||
       ((a.keys&12)!=4&&(a.keys&12)!=8)||(a.keys&~uint64_t(13)))return false;
    float span[5]{};
    static_assert(sizeof(span)==20,"actor pose span");
    if(!m.Writable(a.actor+0x94,sizeof(span))||!m.Read(a.actor+0x94,span,sizeof(span))||
       span[0]!=a.pos.x||span[1]!=a.pos.y||span[2]!=a.pos.z||span[4]!=a.yaw||!std::isfinite(span[3])||
       !Read(c,m,b)||!Equal(a,b))return false;
    span[0]=out.pos.x;span[2]=out.pos.z;span[4]=out.yaw;
    // The synchronous native handler owns actor lifetime; Writable preflights the whole region.
    // A failed write is fail-stop in Transport. Never attempt a blind rollback after native motion.
    return m.Write(a.actor+0x94,span,sizeof(span));
}
}

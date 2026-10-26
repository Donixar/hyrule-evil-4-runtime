#pragma once
#include "ztarget.h"
namespace link_runtime::ztarget::strafe {
// speed (shield-guard-01, Donald: the locked strafe felt slow, it rides the native WALK): the sidestep covers
// speed x the native walk step of the frame; the native step still gates every check below. [ZTarget] StrafeSpeed,
// percent 100..220 (default 160; 220 is about the native run). Collision stays native (cPlayer::move after Core).
constexpr float kStrafeSpeedDefault=1.6f,kStrafeSpeedMin=1.0f,kStrafeSpeedMax=2.2f;
inline float StrafeSpeed(unsigned percent,bool given){
    if(!given)return kStrafeSpeedDefault;
    const float s=float(percent)/100.f;return s>=kStrafeSpeedMin&&s<=kStrafeSpeedMax?s:kStrafeSpeedDefault;
}
struct Config {bool enabled=false,observe=false,qualified=false;float speed=1.f;};
struct Sample {
    uint32_t actor=0,frame=0,epoch=0,routine=0,motion_flags=0,secondary=0,motion=0,sequence=0,motion_table=0;uint64_t identity=0,keys=0;
    uint16_t attrs=0;Vec pos{},target{};float yaw=0,dt=0;
    bool safe=false,focused=false,held=false,walk_motion=false;
    uint32_t neck=0,neck_left=0,neck_right=0,secondary_motion=0,secondary_sequence=0,secondary_flags=0;
    uint16_t secondary_attrs=0;float secondary_weight=0;
};
// Walk installs the native neck work even when its blend weight is zero. At zero,
// both secondary root motion and secondary Core are skipped by MotionMove. Never
// admit an active layer (including parry), or mutate its pointer/weight/flags.
inline bool RootOnly(const Sample& s){
    // SSE DAZ may compare a nonzero subnormal as zero; MotionMove uses x87.
    // Match the native exact-zero branch without changing the caller's FP state.
    uint32_t weight_bits=0;std::memcpy(&weight_bits,&s.secondary_weight,sizeof(weight_bits));
    return !s.secondary||(uint64_t(s.actor)+0x474==s.secondary&&s.neck&&s.secondary_motion&&
        (s.secondary_motion==s.neck_left||s.secondary_motion==s.neck_right)&&s.secondary_sequence==0&&
        s.secondary_attrs==5&&(s.secondary_flags&0xF0000000)==0x80000000&&(weight_bits&0x7FFFFFFFU)==0);
}
struct Output {Vec pos{};float yaw=0;};
class Controller {
    Sample start{};bool active=false,corrected=false,stamp=false;uint32_t last_frame=0;
    static int Side(uint64_t keys){return (keys&12)==4?-1:(keys&12)==8?1:0;}
    bool Ready(const Sample& s)const{
        return config.enabled&&!config.observe&&config.qualified&&s.actor&&s.identity&&s.safe&&s.focused&&s.held&&
            Side(s.keys)!=0&&!(s.keys&~uint64_t(13))&&Finite(s.pos)&&Finite(s.target)&&std::isfinite(s.yaw)&&ValidDt(s.dt);
    }
    bool Same(const Sample& s)const{
        return active&&Ready(s)&&s.actor==start.actor&&s.epoch==start.epoch&&s.identity==start.identity&&
            s.frame==start.frame&&s.dt==start.dt&&s.keys==start.keys;
    }
public:
    Config config;
    bool Begin(const Sample& s){
        if(active||!Ready(s)||(stamp&&last_frame==s.frame))return false;
        const unsigned r1=(s.routine>>8)&255;
        if((s.routine&255)||!(s.routine==0x10000||s.routine==0x1010000||r1==1||r1==4))return false;
        const float dx=s.target.x-s.pos.x,dz=s.target.z-s.pos.z;
        const float radius=std::hypot(dx,dz);
        if(radius<300.f||radius>kHold||std::fabs(Turn(s.yaw,std::atan2(dx,dz)))>0.35f)return false;
        start=s;active=true;corrected=false;return true;
    }
    bool NeedsWalk()const{return active&&((start.routine>>8)&255)!=1;}
    bool BeforeCore(const Sample& s,uint32_t caller_rva,uint32_t work,uint32_t camera,Output& out){
        if(!Same(s)||corrected||caller_rva!=0x262DFF||work!=s.actor+0x1D8||camera||
           (s.routine&0xFFFF)!=0x100||!s.walk_motion||!RootOnly(s)||!(s.attrs&1)||(s.motion_flags&0x40000000))return false;
        const float step=std::hypot(s.pos.x-start.pos.x,s.pos.z-start.pos.z);
        const float dx=s.target.x-start.pos.x,dz=s.target.z-start.pos.z,radius=std::hypot(dx,dz);
        const float target_shift=std::hypot(s.target.x-start.target.x,s.target.z-start.target.z);
        if(!std::isfinite(step)||step>400.f*s.dt*2.f||radius<300.f||radius>kHold||step>radius*0.25f||
           target_shift>200.f*s.dt*2.f||std::fabs(s.target.y-start.target.y)>200.f*s.dt*2.f)return false;
        const float speed=std::isfinite(config.speed)&&config.speed>=kStrafeSpeedMin&&config.speed<=kStrafeSpeedMax?config.speed:1.f;
        const float drive=step*speed;
        if(drive>radius*0.25f)return false;
        const float ux=dx/radius,uz=dz/radius,side=float(Side(s.keys));
        Output proposed;proposed.pos=s.pos;
        if(s.keys&1){
            constexpr float diagonal=0.7071067811865475244f;
            proposed.pos.x=start.pos.x+drive*(ux+side*uz)*diagonal;
            proposed.pos.z=start.pos.z+drive*(uz-side*ux)*diagonal;
        }else{
            // Chord length equals the (scaled) native horizontal step. Radius stays exact before collision.
            const float inward=(drive*drive)/(2.f*radius);
            const float lateral=std::sqrt(std::fmax(0.f,drive*drive-inward*inward));
            proposed.pos.x=start.pos.x+ux*inward+side*uz*lateral;
            proposed.pos.z=start.pos.z+uz*inward-side*ux*lateral;
        }
        proposed.yaw=std::atan2(s.target.x-proposed.pos.x,s.target.z-proposed.pos.z);
        if(!Finite(proposed.pos)||!std::isfinite(proposed.yaw))return false;
        stamp=true;last_frame=s.frame;corrected=true;out=proposed;return true;
    }
    bool LocalKeys(const Sample& s,uint32_t native_keys,uint32_t& out)const{
        if(!Same(s)||!corrected||(s.routine&0xFFFF)!=0x100||!s.walk_motion||!RootOnly(s)||
           !(s.attrs&1)||(s.motion_flags&0x40000000)||native_keys!=uint32_t(s.keys))return false;
        out=(native_keys&~uint32_t(12))|1;return true;
    }
    bool Active()const{return active;}
    void End(){active=false;corrected=false;}
};
}

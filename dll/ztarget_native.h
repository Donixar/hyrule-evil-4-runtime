#pragma once
#include "ztarget.h"
#include "native_pins.h"
#include "link_runtime_health.h"
namespace link_runtime::ztarget {
namespace rva {constexpr uint32_t front=0x19F410,hit=0x19C880,hit_return=0x19E226,ctrl=0x8702D0,qfps=ctrl+0x278,global=0x806F3C,player=0x857054;}
struct Frame {
    uint32_t player=0,global=0,frame=0,routine=0,costume=0,cam_enabled=0,cam_system=0,cam_state=0,stop=0;
    uint64_t keys=0;uint16_t damage=0,room=0;uint8_t mode=0,ready=0,action=0;float dt=0,yaw=0;Vec pos;bool healthy=false;
};
inline bool Safe(const Frame& f){
    return f.healthy&&f.player&&f.global&&f.mode==10&&f.cam_enabled&&!(f.cam_state&4)&&
        !(f.stop&0x40000000)&&!f.action&&(f.routine&0xFF)==0&&((f.routine>>8)&0xFF)<=4&&
        !f.damage&&!(f.keys&(0x10ULL|0x80ULL|0x800ULL|0x80000ULL))&&Finite(f.pos)&&std::isfinite(f.yaw)&&ValidDt(f.dt);
}
// Safe() but for the action prompt byte (RVA 0x850302). bug #4 (24-oct): the prompt shows whenever a Ganado staggers
// next to Link (the kick offer), so the lock vanished exactly when the target staggered (`face_prompt` ended 6 of 31
// locks in the 24-oct log, flickering lock/suspend every 4 frames). The prompt is only an offer; pressing it is the
// action key (0x80000) and a native routine, both still unsafe. The camera, the cursor and the lock state use this;
// facing writes still need Safe() (FaceWith skips the yaw write on a prompt frame and keeps the lock).
inline bool CameraSafe(const Frame& f){Frame g=f;g.action=0;return Safe(g);}
// The decisive reason a frame is not Safe, for the zt log and release policy. Hard releases precede soft flags:
// damage or a held action/knife must not mask death, an event, aim or fire on the same frame.
// Hard: a real release (aim 0x10, fire 0x80, a native event, health/scene);
// the rest (knife 0x800, action 0x80000, damage, a native routine or camera transition, the action prompt) only
// suspend the lock, which comes back on the same target while the lock input stays held (runtime Resume). bug #4
// (24-oct): damage and the action key were hard, so a hit taken (or a parry's damage record) and a kick with the lock
// key held killed the lock until the key was released (the 24-oct log: 3 of 3 `damage` ends, and damage or a kick
// inside 4 of the 11 knife suspensions that never came back).
enum class Unsafe:uint8_t {None,Damage,Aim,Fire,Action,Knife,Health,Event,Mode,Camera,Prompt,Routine,Pose,Dt};
inline Unsafe WhyUnsafe(const Frame& f){
    if(!f.healthy||!f.player||!f.global)return Unsafe::Health;if(f.stop&0x40000000)return Unsafe::Event;
    if(f.keys&0x10ULL)return Unsafe::Aim;if(f.keys&0x80ULL)return Unsafe::Fire;
    if(f.damage)return Unsafe::Damage;if(f.keys&0x80000ULL)return Unsafe::Action;if(f.keys&0x800ULL)return Unsafe::Knife;
    if(f.mode!=10)return Unsafe::Mode;if(!f.cam_enabled||(f.cam_state&4))return Unsafe::Camera;if(f.action)return Unsafe::Prompt;
    if((f.routine&0xFF)!=0||((f.routine>>8)&0xFF)>4)return Unsafe::Routine;
    if(!Finite(f.pos)||!std::isfinite(f.yaw))return Unsafe::Pose;if(!ValidDt(f.dt))return Unsafe::Dt;
    return Unsafe::None;
}
inline bool HardRelease(Unsafe u){
    return u==Unsafe::Aim||u==Unsafe::Fire||u==Unsafe::Health||u==Unsafe::Event;
}
inline const char* UnsafeName(Unsafe u){
    static const char* const n[]={"safe","damage","aim","fire","action","knife","health","event","mode","camera","prompt","routine","pose","dt"};
    return n[unsigned(u)];
}
inline const char* FaceUnsafeName(Unsafe u){
    static const char* const n[]={"face_safe","face_damage","face_aim","face_fire","face_action","face_knife","face_health",
        "face_event","face_mode","face_camera","face_prompt","face_routine_unsafe","face_pose","face_dt"};
    return n[unsigned(u)];
}
template<class Mem,class T> bool Get(Mem& m,uint32_t address,T& value){return m.Read(address,&value,sizeof(value));}
template<class Mem> bool ReadFrameOnce(Mem& m,uint32_t base,Frame& f){
    struct Words {Mem& m;bool Read32(uint32_t a,uint32_t& v){return Get(m,a,v);}} words{m};
    HealthState health;
    if(!ReadHealth(words,base+rva::player,base+rva::global,health)||!Get(m,base+rva::global,f.global))return false;
    f.player=health.player;f.healthy=health.eligible;
    return Get(m,f.global+0x530C,f.frame)&&Get(m,f.global+0x70,f.dt)&&Get(m,f.global+0x4FAC,f.room)&&
        Get(m,f.global+0x4FC8,f.costume)&&Get(m,f.player+0x94,f.pos)&&Get(m,f.player+0xA4,f.yaw)&&
        Get(m,f.player+0xFC,f.routine)&&Get(m,f.player+0x32C,f.damage)&&Get(m,f.global+0x170,f.stop)&&
        Get(m,base+rva::ctrl+0x28,f.cam_enabled)&&Get(m,base+rva::ctrl+0x2C,f.cam_system)&&
        Get(m,base+rva::ctrl+0x30,f.cam_state)&&Get(m,base+rva::ctrl+0x34,f.mode)&&
        Get(m,base+rva::qfps+0x1B5,f.ready)&&Get(m,base+0x862F08,f.keys)&&Get(m,base+0x850302,f.action);
}
template<class Mem> bool ReadFrame(Mem& m,uint32_t base,Frame& f){
    Frame a{},b{};if(!ReadFrameOnce(m,base,a)||!ReadFrameOnce(m,base,b)||
        a.player!=b.player||a.global!=b.global||a.frame!=b.frame||a.routine!=b.routine||a.room!=b.room||
        a.costume!=b.costume||a.cam_enabled!=b.cam_enabled||a.cam_system!=b.cam_system||a.cam_state!=b.cam_state||
        a.stop!=b.stop||a.keys!=b.keys||a.damage!=b.damage||a.mode!=b.mode||a.ready!=b.ready||a.action!=b.action||
        a.dt!=b.dt||a.yaw!=b.yaw||a.pos.x!=b.pos.x||a.pos.y!=b.pos.y||a.pos.z!=b.pos.z||a.healthy!=b.healthy)return false;
    f=a;return true;
}
inline bool SameScene(const Frame& a,const Frame& b){return a.player==b.player&&a.global==b.global&&a.room==b.room&&a.costume==b.costume;}
inline bool Adjust(Controller& c,const Input& i,const Matrix& m,const Offset& in,float desired,float tau,Offset& out){
    Offset checked;
    if(!c.Active())return false;   // an Idle controller is never suspended here (a press must not be lost)
    if(!i.safe||!ValidDt(i.dt)||!std::isfinite(desired)||!Rotate(m,in,0,checked)){c.Suspend();return false;}
    const Vec diff{in.target.x-in.campos[0].x,in.target.y-in.campos[0].y,in.target.z-in.campos[0].z};
    const Vec world=World(m,diff);
    if(!Finite(world)||world.x*world.x+world.z*world.z<1.f){c.Suspend();return false;}
    const float native=std::atan2(world.x,world.z);const bool release=c.phase==Phase::Release;
    const float q=c.filter.Step(i.frame,i.dt,native,release?native:desired,release,tau);
    if(release&&std::fabs(Turn(q,native))<0.00872665f){c.phase=Phase::Idle;c.filter.Reset();return false;}
    if(!Rotate(m,in,Turn(native,q),out)){c.Suspend();return false;}return true;
}
// Relocation-normalized bytes read from the loaded image before either hook is created.
// Both RET 0x0C exits and the exact caller constrain the x86 hitCheck contract.
inline constexpr native_pins::Pin kPins[]={
    {0x19C880,"558bec81ecf4000000"},
    {0x19E202,"8d8d88feffff518bcbe82de6e6ff8d55ac528d8588feffff508d4dcc518bcbe80792e6ff"},
    {0x19F410,"538b1d3c6fc0005657"},{0x19F438,"b9d002c700e80827e6ff"},
    {0x19E130,"558bec81ec9c010000"},{0x19CEFB,"c20c00"},{0x19D002,"c20c00"},
    {0x1B4A,"e9a1532f00"}
};
// hitCheck's thunk (the call at 0x19E221 goes through it). Vanilla: JMP 0x19C880. re4_fps (re4_tweaks,
// winmm.dll, CameraImprovements) rewrites it to JMP into its own module (live 16-oct: e910d1846f ->
// 0x6FD94542). We hook the thunk, not the body: the return address our detour sees stays 0x19E226 and
// re4_fps's wrapper still runs (our trampoline jumps to whatever the thunk held). Accepted: a rel32 JMP
// to the body, or to an address outside the bio4 image (another module's hook). Anything else fails closed.
inline constexpr uint32_t kHitThunk=0x742D;
inline constexpr uint32_t kImageSpan=0xA00000;   // bio4.exe SizeOfImage is below this
enum class Thunk {Bad,Native,Foreign};
template<class Mem> Thunk ReadThunk(Mem& m,uint32_t base){
    unsigned char op[5]{};if(!m.Read(base+kHitThunk,op,5)||op[0]!=0xE9)return Thunk::Bad;
    int32_t rel;std::memcpy(&rel,op+1,4);const uint32_t dest=base+kHitThunk+5+static_cast<uint32_t>(rel);
    if(dest==base+rva::hit)return Thunk::Native;
    return dest<base||dest>=base+kImageSpan?Thunk::Foreign:Thunk::Bad;
}
}

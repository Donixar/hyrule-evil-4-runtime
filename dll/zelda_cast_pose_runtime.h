#pragma once
#include <limits>
// Game thread only: Begin/Active/HandWorld. Install once after MinHook initialization.
// Hooks are independent of the existing combat dispatch and MotionMove hooks.
#include "zelda_cast_pose.h"
#include "zelda_cast_clip_runtime.h"   // zelda-companion-01: the TP cast clip on +0x2A8 (ZeldaCastClip=1)
#include "native_pins.h"
#include "link_runtime_health.h"
#include "thread_binding.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <cstdio>

namespace link_runtime::zelda_cast {
namespace detail {
using Log = void(*)(const char*, ...);
constexpr uint32_t kPlayer=0x857054, kGlobal=0x806F3C;
constexpr uint32_t kMove=0x351210, kWorld=0x25BED0, kRender=0x2EB450, kWorldReturn=0x3650D0;
constexpr uint32_t kHeld=0x862F08, kPressed=0x862F10;
// Exact preferred-base bytes; native_pins::Matches normalizes PE relocations.
inline constexpr native_pins::Pin kPins[] = {
    {0x351210,"568bf1e8021bcbff8bce5ee96c35cbff"},
    {0x2D1A,"e901203600"},
    {0x3650C0,"8b168b424c8bceffd08bcee8d30ccaff8bcee817f9c9ff56e8de17caff"},
    {0x25BED0,"558bec81ec80000000a1105ec30033c58945fc8bc1"},
    {0x25BF20,"f686c0010000028b5e6c0f8517020000d946488d4670d95d9050d946588d5590d95d9452d946688d7b0c57d95d98e8e98cdaff"},
    // Pending addRot offsets, multiplication order and flag consumption.
    {0x25C082,"8b86c0010000a9000000407472d9862801000051d91c248d55cc25ffffffbf6a78528986c0010000e84e10dbff578d45cc5057e85897daff"},
    {0x25C0BA,"d9863001000083c414d91c248d4dcc6a7a51e82c10dbff578d55cc5257e83697daffd9862c01000083c414d91c248d45cc6a7950e80a10dbff578d4dcc5751e81497daff83c4188d46705057e84823dbff"},
    {0x5DA3,"e928612500"},
    {0x2EB450,"558bec568b7508d98658010000d88e54010000d9eedae9dfe0f6c4440f8b07010000"},
    {0x948F,"e9bc1f2e00"},
    {0x2EB78F,"688f944000"},
    {0x25B7C0,"558bec8b55088b81f400000085d279068bc15dc2040085c075045dc20400f7410400200000740c69d2d801000003c25dc2040085d274094a8b80f400000075f75dc20400"}
};
struct Memory {
    bool Read(uint32_t a, void* p, size_t n) {
        if(a<0x10000 || !p || !n || uint64_t(a)+n>=0x7FFF0000) return false;
        __try { std::memcpy(p,reinterpret_cast<const void*>(a),n); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Write(uint32_t a, const void* p, size_t n) {
        if(a<0x10000 || !p || !n || uint64_t(a)+n>=0x7FFF0000) return false;
        __try { std::memcpy(reinterpret_cast<void*>(a),p,n); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Read32(uint32_t a, uint32_t& out) { return Read(a,&out,4); }
};
template<class T> inline bool Read(uint32_t a,T& out) { Memory m; return m.Read(a,&out,sizeof(out)); }
template<class T> inline bool Write(uint32_t a,const T& value) { Memory m; return m.Write(a,&value,sizeof(value)); }
struct Scene {
    uint32_t player=0,global=0,guid=0,parts=0,frame=0;
    uint16_t room=0;
};
inline uint32_t base=0;
inline Log log=nullptr;
inline std::atomic<bool> ready{false};
inline bool terminal_failure=false,created[3]{};
inline void* targets[3]{};
using Method=void(__thiscall*)(void*);
using Render=void(__cdecl*)(void*);
inline Method move_original=nullptr,world_original=nullptr;
inline Render render_original=nullptr;
inline Timeline timeline;
inline bool running=false;
inline Scene owner;
inline double now_ms=0;
inline Vec anchor_pos,anchor_ang;
inline uint32_t clock_frame=0;
inline thread_binding::Binding binding;
inline std::atomic<DWORD> thread{0}; // Published owner; only Begin can hand over from an exited thread.
inline bool hand_known=false;
inline Scene hand_scene;
inline Vec hand;
inline ULONGLONG hand_stamp=0;
inline unsigned applied=0,drawn=0;
// zelda-companion-01: the cast plays the TP clip on +0x2A8 instead of the procedural arm (clip_mode), and the facing
// eases from her own yaw to the target's over zelda_cast_clip::kTurnMs instead of snapping on the first frame.
namespace zcc=link_runtime::zelda_cast_clip;
inline bool clip_mode=false,eased=false,clip_yield_logged=false,clip_rig_logged=false;   // eased: ZeldaCastClip=1 asked
inline zcc::runtime::Layer player_layer;
inline float own_yaw=0,face_yaw=std::numeric_limits<float>::quiet_NaN();
inline bool OwnedThread() { return ready.load(std::memory_order_acquire) && thread.load(std::memory_order_acquire)==GetCurrentThreadId(); }

struct Failure {
    char reason[64]{};
    int bone=-1;
    const char* detail="read_or_value";
    RigidMetrics matrix;
    uint32_t got=0,expected=0;
    Vec value{},target{};
};
inline bool Reject(Failure* failure,const char* reason,int bone=-1) {
    if(failure) {
        std::snprintf(failure->reason,sizeof failure->reason,reason,bone);
        failure->bone=bone;
    }
    return false;
}
inline void LogFailure(const char* phase,const Failure& f) {
    if(log) log("zelda_cast %s=%s bone=%d detail=%s dot_error=%.9g det=%.9g finite=%u got=0x%X expected=0x%X value=(%.6g,%.6g,%.6g) target=(%.6g,%.6g,%.6g) applied=%u drawn=%u",
        phase,f.reason,f.bone,f.detail,f.matrix.dot_error,f.matrix.det,f.matrix.finite?1U:0U,f.got,f.expected,
        double(f.value.x),double(f.value.y),double(f.value.z),double(f.target.x),double(f.target.y),double(f.target.z),applied,drawn);
}
inline void Cancel(const char* reason,const Failure* failure=nullptr) {
    if(running) {
        if(failure) LogFailure("end",*failure);
        else if(log) log("zelda_cast end=%s applied=%u drawn=%u",reason,applied,drawn);
    }
    running=false; timeline.Cancel(); hand_known=false;
    if(clip_mode) zcc::runtime::Release(player_layer);
    clip_mode=false;
}
inline bool Same(const Scene& a,const Scene& b) {
    return a.player==b.player && a.global==b.global && a.guid==b.guid && a.parts==b.parts && a.room==b.room;
}
inline bool SceneNow(Scene& s) {
    Memory m; HealthState h; uint8_t r[2]{}; int fixed=0; uint32_t vt=0,flags=0;
    if(!base || !ReadHealth(m,base+kPlayer,base+kGlobal,h) || !Read(base+kGlobal,s.global) ||
       !m.Read(h.player+0xFC,r,2) || !Read(h.player+0x13C,fixed) ||
       !CanCast(h.character,h.in_play,r[0],r[1],fixed)) return false;
    s.player=h.player;
    return Read(s.player,vt) && vt==base+0x727DBC && Read(s.player+4,flags) && (flags&0x201)==1 &&
        Read(s.player+0xF8,s.guid) && Read(s.player+0xF4,s.parts) && IsPointer(s.parts) &&
        Read(s.global+0x4FAC,s.room) && Read(s.global+0x530C,s.frame);
}
inline bool OwnerNow(Scene& s) {
    if(!OwnedThread()) return false;
    if(!SceneNow(s) || !Same(owner,s)) { Cancel("scene_or_action"); return false; }
    return true;
}
// Validate even when Active was already called in this frame. GameNow sanitizes dt;
// that fallback is useful for normal gameplay but is not permission to hold a cast.
inline bool ClockDelta(const Scene& s,double& elapsed) {
    const int32_t delta=int32_t(s.frame-clock_frame);
    if(delta<0 || delta>8) { Cancel("frame_discontinuity"); return false; }
    float dt=0;
    if(!Read(s.global+0x70,dt) || !std::isfinite(dt) || dt<.03f || dt>2) { Cancel("invalid_dt"); return false; }
    elapsed=double(delta)*dt*(1000./30.); return true;
}
// Same game-time delta as GameNow: dt is in 1/30 s, once per native frame.
inline bool Tick(Scene& s) {
    double elapsed=0;
    if(!OwnerNow(s) || !ClockDelta(s,elapsed)) return false;
    now_ms+=elapsed; clock_frame=s.frame;
    if(!timeline.Active(now_ms)) { Cancel("complete"); return false; }
    return true;
}
struct Rig { uint32_t part[kChain]{}; Mat local[kChain]; Vec add_rot[kChain]{}; unsigned pending_mask=0; };
inline bool ReadRig(const Scene& s,Rig& rig,Failure* failure=nullptr) {
    rig=Rig{};
    if(failure) *failure=Failure{};
    uint8_t count=0; uint32_t p=s.parts; Vec scale{};
    const bool count_read=Read(s.player+0x102,count);
    if(failure) { failure->got=count; failure->expected=kChain; }
    if(!count_read || count<kChain) return Reject(failure,"rig_count");
    const bool scale_read=Read(s.player+0xAC,scale);
    if(failure) { failure->value=scale; failure->target={1,1,1}; }
    if(!scale_read ||
       !std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z) ||
       std::abs(scale.x-1)>.001f || std::abs(scale.y-1)>.001f || std::abs(scale.z-1)>.001f) return Reject(failure,"rig_scale");
    for(unsigned i=0;i<kChain;++i) {
        if(failure) *failure=Failure{};
        uint32_t parent=0,flags=0;
        if(failure) failure->got=p;
        if(!IsPointer(p)) return Reject(failure,"rig_part_%d_pointer",int(i));
        for(unsigned j=0;j<i;++j) if(rig.part[j]==p) return Reject(failure,"rig_part_%d_duplicate",int(i));
        rig.part[i]=p;
        const uint32_t expected=kParents[i]<0?s.player:rig.part[kParents[i]];
        const bool parent_read=Read(p+0x6C,parent);
        if(failure) { failure->got=parent; failure->expected=expected; }
        if(!parent_read || parent!=expected) return Reject(failure,"rig_part_%d_parent",int(i));
        const bool flags_read=Read(p+0x1C0,flags);
        if(failure) { failure->got=flags; failure->expected=0; }
        if(!flags_read || (flags&2U)) return Reject(failure,"rig_part_%d_flags",int(i));
        if(!Read(p+0x3C,rig.local[i])) return Reject(failure,"rig_part_%d_local_read",int(i));
        if(!Rigid(rig.local[i],failure?&failure->matrix:nullptr)) return Reject(failure,"rig_part_%d_rigid",int(i));
        if(flags&0x40000000U) {
            const bool rot_read=Read(p+0x128,rig.add_rot[i]);
            if(failure) failure->value=rig.add_rot[i];
            if(!rot_read || !std::isfinite(rig.add_rot[i].x) ||
               !std::isfinite(rig.add_rot[i].y) || !std::isfinite(rig.add_rot[i].z)) return Reject(failure,"rig_part_%d_addrot",int(i));
            rig.pending_mask|=1U<<i;
        }
        if(i+1<kChain && !Read(p+0xF4,p)) return Reject(failure,"rig_part_%d_next",int(i));
    }
    // Ashley's first arm chain, in mm. Root/torso translations may be animated.
    const unsigned bones[]={5,7,8,9,10};
    const Vec expected[]={{-10.431f,178.774f,-36.68f},{-122.038f,10.762f,1.848f},
                          {-240.648f,0,0},{-126.442f,0,0},{-108.893f,0,0}};
    for(unsigned i=0;i<5;++i) {
        const Vec p0=Position(rig.local[bones[i]]),e=expected[i];
        if(std::abs(p0.x-e.x)>2 || std::abs(p0.y-e.y)>2 || std::abs(p0.z-e.z)>2) {
            if(failure) { *failure=Failure{}; failure->value=p0; failure->target=e; }
            return Reject(failure,"rig_chain_%d_translation",int(bones[i]));
        }
    }
    return true;
}
inline bool RestoreLocals(const Rig& rig) {
    bool ok=true;
    for(unsigned i=0;i<kChain;++i) if(Driven(i)) ok=Write(rig.part[i]+0x3C,rig.local[i]) && ok;
    return ok;
}
// Exact post-animation call only. The other MotionMove/IK/world callers stay native.
inline void WorldAt(void* self,uintptr_t caller) {
    Scene s;
    if(!OwnedThread() || caller!=uintptr_t(base)+kWorldReturn || reinterpret_cast<uintptr_t>(self)!=owner.player || !Tick(s)) {
        world_original(self); return;
    }
    Rig rig; Mat posed[kChain],world[kChain],old_mat,old_local; Vec old_pos,old_ang;
    Failure failure; PoseDiagnostic pose_problem;
    if(eased) anchor_ang.y=zcc::EaseYaw(own_yaw,face_yaw,timeline.Age(now_ms));
    const Mat root=Euler(anchor_ang.x,anchor_ang.y,anchor_ang.z,anchor_pos);
    if(!ReadRig(s,rig,&failure)) {
        // The clip owns the arm: a rig this reader does not accept (a blended frame) only skips the anchor this frame.
        if(clip_mode) { if(!clip_rig_logged&&log) { clip_rig_logged=true; LogFailure("clip_rig_skip",failure); } world_original(self); return; }
        Cancel(failure.reason,&failure); world_original(self); return;
    }
    if(!BuildPose(root,rig.local,clip_mode?0.:Weight(timeline.Age(now_ms)),posed,world,rig.add_rot,&pose_problem)) {
        failure=Failure{};
        Reject(&failure,"build_pose",pose_problem.bone); failure.detail=pose_problem.detail; failure.matrix=pose_problem.matrix;
        Cancel(failure.reason,&failure); world_original(self); return;
    }
    if(!Read(s.player+0x0C,old_mat) || !Read(s.player+0x3C,old_local) || !Read(s.player+0x94,old_pos) || !Read(s.player+0xA0,old_ang)) {
        Cancel("read_player_mats"); world_original(self); return;
    }
    bool wrote=Write(s.player+0x94,anchor_pos) && Write(s.player+0xA0,anchor_ang) &&
               Write(s.player+0x0C,root) && Write(s.player+0x3C,root);
    for(unsigned i=0;i<kChain && wrote;++i) if(Driven(i)) wrote=Write(rig.part[i]+0x3C,posed[i]);
    if(!wrote) {
        RestoreLocals(rig); Write(s.player+0x94,old_pos); Write(s.player+0xA0,old_ang);
        Write(s.player+0x0C,old_mat); Write(s.player+0x3C,old_local);
        Cancel("write_failed"); world_original(self); return;
    }
    // Native propagation updates all descendants, world positions and accumulated scales.
    // Local animation stays native for next frame, blend history and cancellation.
    __try {
        world_original(self); ++applied;
        if(applied==1 && log) log("zelda_cast applied=1 pending_addrot=0x%X age_ms=%.3f",rig.pending_mask,timeline.Age(now_ms));
        // The hand from the posed propagation, on the game thread (live 23-oct: ModelRender runs on the render thread, so
        // RenderDetour never fed it: drawn=0 and every ray was dropped).
        Mat m;
        if(Read(rig.part[10]+0x0C,m) && Rigid(m)) {hand=Position(m); hand_scene=s; hand_stamp=GetTickCount64(); hand_known=true; ++drawn;}
    }
    __finally { if(!RestoreLocals(rig)) Cancel("restore_failed"); }
}
inline void __fastcall WorldDetour(void* self,void*) {
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    WorldAt(self,caller);
}
inline void __fastcall MoveDetour(void* self,void*) {
    if(!OwnedThread()) { move_original(self); return; }
    Scene s; uint32_t held=0,pressed=0;
    const bool freeze=reinterpret_cast<uintptr_t>(self)==owner.player && Tick(s);
    const bool keys=freeze && Read(base+kHeld,held) && Read(base+kPressed,pressed);
    if(freeze && !keys) Cancel("input_read");
    // The clip's weight before cPlayer::move runs MotBase->move (MotionMove blends +0x2A8), Neck->move comes after it.
    if(keys && clip_mode && !zcc::runtime::Tick(player_layer,timeline.Age(now_ms)) && !clip_yield_logged) {
        clip_yield_logged=true;
        if(log) log("zelda_cast clip=yielded age_ms=%.0f (a native layer took +0x2A8; the stop, the turn and the ray go on)",timeline.Age(now_ms));
    }
    if(keys) {
        // Only direction bits 0..3. Keep knife, action, menus and the run/action bit native.
        const uint32_t h=held&~15U,p=pressed&~15U;
        if(!Write(base+kHeld,h) || !Write(base+kPressed,p)) {
            Write(base+kHeld,held); Write(base+kPressed,pressed); Cancel("input_write");
        }
    }
    __try { move_original(self); }
    __finally {
        if(keys) {
            uint32_t h=0,p=0;
            if(Read(base+kHeld,h)) Write(base+kHeld,(h&~15U)|(held&15U));
            if(Read(base+kPressed,p)) Write(base+kPressed,(p&~15U)|(pressed&15U));
        }
    }
    // Collision/physics can still separate the player from geometry. Carry that safe
    // position forward; the player's input and root animation cannot move or turn her.
    if(reinterpret_cast<uintptr_t>(self)==owner.player && Tick(s)) {
        Vec corrected;
        if(Read(s.player+0x94,corrected) && std::isfinite(corrected.x) && std::isfinite(corrected.y) && std::isfinite(corrected.z)) anchor_pos=corrected;
        else Cancel("position");
    }
}
inline void __cdecl RenderDetour(void* self) {
    render_original(self);
    if(!OwnedThread()) return;
    Scene s; Rig rig; Mat m; float a=0,b=0;
    if(!SceneNow(s) || reinterpret_cast<uintptr_t>(self)!=s.player) return;
    hand_known=false;
    if(!Read(s.player+0x154,a) || !Read(s.player+0x158,b) || !std::isfinite(a*b) || a*b<=0 ||
       !ReadRig(s,rig) || !Read(rig.part[10]+0x0C,m) || !Rigid(m)) return;
    hand=Position(m); hand_scene=s; hand_stamp=GetTickCount64(); hand_known=true; ++drawn;
}
} // namespace detail

// face_yaw (finite): the facing she turns to (eased over zelda_cast_clip::kTurnMs), so the arm throws at the target
// (goal 23-oct); NaN keeps her own facing. clip: play the TP cast clip on +0x2A8 (ZeldaCastClip=1); when the clip is not
// loaded or the slot is busy the procedural pose runs instead (600 ms).
inline void Begin(double now,float face=std::numeric_limits<float>::quiet_NaN(),bool clip=false) {
    using namespace detail;
    if(!ready.load(std::memory_order_acquire)) return;
    const auto claim=binding.Take();
    if(!thread_binding::Serves(claim)) return;
    if(claim==thread_binding::Claim::Rebound) {
        // Never inherit a half-finished pose, clock or hand from the previous game thread,
        // even if this Begin is refused below. Do not restore pointers into its former scene.
        Cancel("thread_rebound"); owner={}; hand_scene={}; hand={}; hand_stamp=0;
        anchor_pos={}; anchor_ang={}; now_ms=0; clock_frame=0; applied=drawn=0;
    }
    thread.store(binding.Id(),std::memory_order_release);
    Scene s; Rig rig; Vec pos,ang; Failure failure;
    if(!std::isfinite(now) || now<0 || !SceneNow(s)) {
        if(log) log("zelda_cast begin=refused (scene/time)"); return;
    }
    if(!ReadRig(s,rig,&failure)) { LogFailure("begin_refused",failure); return; }
    if(!Read(s.player+0x94,pos) || !Read(s.player+0xA0,ang)) {
        if(log) log("zelda_cast begin_refused=read_player_mats"); return;
    }
    failure=Failure{};
    if(!Rigid(Euler(ang.x,ang.y,ang.z,pos),&failure.matrix)) {
        Reject(&failure,"build_pose"); failure.detail="model"; LogFailure("begin_refused",failure); return;
    }
    if(timeline.Active(now) && Same(owner,s)) return;
    if(clip_mode) zcc::runtime::Release(player_layer);
    clip_mode=clip && zcc::runtime::Ready() && zcc::runtime::Install(player_layer,s.player,s.player+zcc::kNeckWork);
    if(clip && !clip_mode && log) log("zelda_cast clip=refused ready=%u (the procedural pose runs)",zcc::runtime::Ready()?1U:0U);
    clip_yield_logged=clip_rig_logged=false;
    timeline.Cancel(); timeline.Begin(now,clip_mode?zcc::kClipMs:kDurationMs); running=true; owner=s; anchor_pos=pos; anchor_ang=ang;
    own_yaw=ang.y; face_yaw=face; eased=clip;
    if(!eased && std::isfinite(face)) anchor_ang.y=face;   // ZeldaCastClip=0: the 23-oct snap, unchanged
    now_ms=now; clock_frame=s.frame; applied=drawn=0; hand_known=false;
    if(log) log("zelda_cast begin player=0x%X room=0x%X duration=%.0f clip=%u",s.player,unsigned(s.room),timeline.Duration(),clip_mode?1U:0U);
}
inline bool Active(double now) {
    using namespace detail;
    if(!OwnedThread()) return false;
    Scene s; double elapsed=0;
    if(!std::isfinite(now) || now<now_ms-.01 || !OwnerNow(s)) { Cancel("clock_or_scene"); return false; }
    if(!ClockDelta(s,elapsed)) return false;
    now_ms=now; clock_frame=s.frame;
    if(!timeline.Active(now)) { Cancel("complete"); return false; }
    return true;
}
// The cast plays the clip (chosen at Begin) and, while +0x2A8 is still ours, where it is (SEQ entries).
inline bool ClipCast() { return detail::clip_mode; }
inline bool ClipFrame(double& entries) { return detail::clip_mode && zelda_cast_clip::runtime::SeqFrame(detail::player_layer,entries); }
inline bool HandWorld(float out[3]) {
    using namespace detail;
    Scene s;
    if(!out || !OwnedThread() || !hand_known ||
       !SceneNow(s) || !Same(s,hand_scene) || uint32_t(s.frame-hand_scene.frame)>1 || GetTickCount64()-hand_stamp>250) return false;
    out[0]=hand.x; out[1]=hand.y; out[2]=hand.z; return true;
}
inline bool Install(uintptr_t image_base, void(*logger)(const char*, ...)) {
    using namespace detail;
    if(ready.load(std::memory_order_acquire)) return image_base==base;
    if(terminal_failure || image_base<0x10000 || image_base>0x7F000000) return false;
    Memory memory;
    // No hook is even created until every code/ABI/caller pin matches.
    for(const auto& pin:kPins) if(!native_pins::Matches(memory,uint32_t(image_base),pin.rva,pin.hex)) {
        if(logger) logger("zelda_cast installed=0 pin=0x%X",pin.rva); return false;
    }
    base=uint32_t(image_base); log=logger;
    targets[0]=reinterpret_cast<void*>(image_base+kMove);
    targets[1]=reinterpret_cast<void*>(image_base+kWorld);
    targets[2]=reinterpret_cast<void*>(image_base+kRender);
    void* detours[]={reinterpret_cast<void*>(MoveDetour),reinterpret_cast<void*>(WorldDetour),reinterpret_cast<void*>(RenderDetour)};
    void** originals[]={reinterpret_cast<void**>(&move_original),reinterpret_cast<void**>(&world_original),reinterpret_cast<void**>(&render_original)};
    for(unsigned i=0;i<3;++i) {
        const auto result=MH_CreateHook(targets[i],detours[i],originals[i]);
        if(result!=MH_OK) {
            bool clean=true;
            for(unsigned j=0;j<i;++j) {
                if(MH_RemoveHook(targets[j])==MH_OK) created[j]=false; else clean=false;
            }
            terminal_failure=!clean;
            if(log) log("zelda_cast installed=0 create=%u status=%d cleanup=%u",i,int(result),clean?1U:0U);
            return false;
        }
        created[i]=true;
    }
    for(unsigned i=0;i<3;++i) {
        const auto result=MH_EnableHook(targets[i]);
        if(result!=MH_OK) {
            // Do not remove trampolines that another thread may already have entered.
            // All detours stay pass-through while ready=false, including a failed disable.
            terminal_failure=true; bool clean=true;
            for(unsigned j=0;j<3;++j) {
                const auto disabled=MH_DisableHook(targets[j]);
                if(disabled!=MH_OK && disabled!=MH_ERROR_DISABLED) clean=false;
            }
            if(log) log("zelda_cast installed=0 enable=%u status=%d disabled=%u resident_passthrough=1",i,int(result),clean?1U:0U);
            return false;
        }
    }
    ready.store(true,std::memory_order_release);
    if(log) log("zelda_cast installed=1 hooks=3 move=0x351210 world=0x25BED0 render=0x2EB450 caller=0x3650D0");
    return true;
}
} // namespace link_runtime::zelda_cast

#pragma once
#include "runtime_paths.h"
#include "ztarget_native.h"
#include "ztarget_input.h"
#include "ztarget_facing.h"
#include "ztarget_reticle_runtime.h"
#include "ztarget_arrow3d.h"
#include "ztarget_los.h"
#include "ztarget_voice.h"
#include "ztarget_strafe_native.h"
#include "ztarget_strafe_adapter.h"
#include "mortal_draw_prompt.h"
#include "ending_blow_camera.h"
#include "back_slice_camera.h"
#include "thread_binding.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cfenv>
#include <cstdio>
#include <vector>
#include <string>
// Camera and optional facing transport. All decisions/pool/filter state belong to the game
// thread. Facing runs only inside the qualified player dispatch, never in a camera hook/worker.
namespace link_runtime::ztarget::runtime {
using FrontFn=void(__cdecl*)();
using HitFn=void(__thiscall*)(void*,const Matrix*,Offset*,void*);
using Logger=void(*)(const char*,...);
struct Memory {
    // Actor storage is synchronous on its qualified game thread. Require the full span to
    // stay in one committed writable region before its single copy; never change protection.
    bool Writable(uint32_t address,size_t size){
        MEMORY_BASIC_INFORMATION info{};
        return address&&size&&uint64_t(address)+size<=0x100000000ULL&&
            VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))==sizeof(info)&&
            info.State==MEM_COMMIT&&info.Protect==PAGE_READWRITE&&
            uint64_t(address)+size<=uint64_t(reinterpret_cast<uint32_t>(info.BaseAddress))+info.RegionSize;
    }
    bool Read(uint32_t address,void* out,size_t size){
        if(!address||uint64_t(address)+size>0x100000000ULL)return false;
        __try{std::memcpy(out,reinterpret_cast<void*>(address),size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Write(uint32_t address,const void* in,size_t size){
        if(!address||uint64_t(address)+size>0x100000000ULL)return false;
        __try{std::memcpy(reinterpret_cast<void*>(address),in,size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
inline uint32_t base=0;inline FrontFn original_front=nullptr;inline HitFn original_hit=nullptr;
inline bool observe=false,waiver=false,face_target=false;inline float smooth_ms=160;
inline bool pad_enabled=false;inline pad::Channel pad_channel;inline pad::Gate pad_gate;
inline pad::HoldButton pad_hold=pad::HoldButton::R3;   // [ZTarget] ZTargetPadButton (default R3)
inline pad::PadMode pad_mode=pad::PadMode::Toggle;      // [ZTarget] ZTargetPadMode (default toggle: tap R3 locks / releases)
inline pad::Toggle pad_toggle;                          // game thread (Begin)
inline Facing facing;
inline std::atomic<unsigned> face_writes{0},face_rejected{0},face_faults{0};
inline bool(*busy)()=nullptr;inline Logger logger=nullptr;
// The 5 s stats lines (ZTarget stats/hud/strafe); null = logger. The DLL points it at its LogLevel 2 writer.
inline Logger stats_logger=nullptr;
inline std::atomic<bool> live{false},focus_lost{false};inline std::atomic<DWORD> game_thread{0};
inline link_runtime::thread_binding::Binding front_binding;   // FrontDetour's: game_thread is its id (skills-fix-01)
inline std::atomic<unsigned> fronts{0},hits{0},writes{0},rejected{0},dropped{0};
// 1 no snapshot, 2 frame read, 3 scene, 4 frame mismatch, 5 unsafe, 6 busy, 7 unfocused, 8 focus latch, 9 args.
inline std::atomic<unsigned> reject_why[10]{};
// The 5-8 conditions counted at every hit whatever gate failed first (`ZTarget stats2`; why=4 used to hide them).
// Process-wide totals, not scoped to a lock. skew1: hits whose frame was the snapshot's +1, admitted (PrepareHitWith).
inline std::atomic<unsigned> hit_unsafe{0},hit_busy{0},hit_unfocused{0},hit_latch{0},hit_skew1{0};
// `zt unsafe reason=%s frame=%u` (20-oct): Begin was silent while not safe, so a stuck lock left no trace. Logs a reason
// when it differs from the last one logged, at most once per second; a safe frame (null) re-arms it. Game thread.
struct UnsafeLog {
    const char* last=nullptr;uint32_t at=0;bool any=false;
    bool Step(const char* reason,uint32_t now_ms){
        if(!reason){last=nullptr;return false;}
        if((last&&!std::strcmp(last,reason))||(any&&now_ms-at<1000))return false;
        last=reason;at=now_ms;any=true;return true;
    }
};
// The other `zt` diagnostics (fix-0b r1): log when `key` differs from the last one logged, at most once per second;
// Rearm() when the condition ends so the next episode is logged again.
struct DiagLog {
    bool any=false,armed=true;uint32_t at=0;int32_t last=0;
    bool Step(int32_t key,uint32_t now_ms){
        if((!armed&&key==last)||(any&&now_ms-at<1000))return false;
        last=key;armed=false;at=now_ms;any=true;return true;
    }
    void Rearm(){armed=true;}
};
inline Controller controller;inline Buttons buttons;inline Pool pool;inline Candidate locked;
// ---- line of sight, notice cursor and lock sounds (17-oct, docs/ztarget-tp-2026-10-17.md) ----------------------
// los_ready: [ZTarget] LOS (on unless exactly 0) and its hitCheck pins qualified. A lock is admitted with a qualified
// LOS or the explicit DebugLineWaiver=1 (which then skips the per-target test, as before).
inline bool los_ready=false,lock_sound=false,notice_on=false;
inline bool Admitted(){return LineAllowed(los_ready,waiver);}
// The native segment test; tests inject a fake. Game thread only.
inline bool(*los_segment)(const Vec&,const Vec&,uint32_t&)=nullptr;
inline bool NativeSegment(const Vec& a,const Vec& b,uint32_t& r){los::Native n{base};return n.Segment(a,b,r);}
struct LosPort {bool Segment(const Vec& a,const Vec& b,uint32_t& r){return los_segment&&los_segment(a,b,r);}};
inline los::Result LineTo(Vec player,Vec target){
    if(waiver)return los::Result::Clear;
    if(!los_ready)return los::Result::Blocked;
    LosPort port;return los::Check(port,player,target);
}
inline los::Hold los_hold;
inline std::atomic<unsigned> los_refused{0},los_lost{0},los_skipped{0};
inline voice::Pair voices;
// The Pick order, skipping candidates whose line is blocked (marked not visible for this frame): at most kTries
// native tests per press or switch. -1 = none (or the current one only).
constexpr unsigned kTries=4;
template<class Line> int PickClear(Line line,Vec player,int current,int direction){
    for(unsigned i=0;i<kTries;++i){
        const int n=Pick(pool.candidates,pool.count,current,direction);
        if(n<0||n==current)return -1;
        const los::Result r=line(player,pool.candidates[n].raw.pos);
        if(r==los::Result::Clear)return n;
        if(r==los::Result::Fault)return -1;
        pool.candidates[n].visible=false;los_skipped.fetch_add(1);
    }
    return -1;
}
// TP's yellow cursor: the candidate Pick would lock, shown while nothing is held and its line is clear (tested when it
// changes, then every los::kEvery game frames).
struct Notice {Candidate c;bool shown=false;uint32_t checked=0;};
inline Notice notice;
// Cursor tip above the target's feet: a Ganado's head is ~1750-1800 (its own eyes at 1500, em10 sight test), plus a gap.
constexpr float kCursorTip=1950.f;
inline Frame snapshot;inline Input input;inline View view;inline uint32_t epoch=1,view_frame=0;
inline bool have_snapshot=false,have_view=false,hit_seen=false;
inline float switch_seconds=0;inline thread_local unsigned depth=0;
struct Record {Phase phase;uint32_t frame,actor;uint64_t identity;const char* reason;};
inline Record records[64];inline std::atomic<unsigned> read_index{0},write_index{0};
// Neutral Z-target (ztarget.h Neutral, shield-guard-01): game thread state and its own log ring (`zt neutral ...`).
inline Neutral neutral;
struct NeutralRecord {NeutralEvent event;uint32_t frame;float yaw;const char* reason;};
inline NeutralRecord neutral_records[16];inline std::atomic<unsigned> neutral_read{0},neutral_write{0};
inline void RecordNeutral(NeutralEvent e,const char* reason){
    if(e==NeutralEvent::None)return;
    const unsigned w=neutral_write.load(std::memory_order_relaxed),r=neutral_read.load(std::memory_order_acquire);
    if(w-r>=16){dropped.fetch_add(1);return;}
    neutral_records[w%16]={e,snapshot.frame,neutral.yaw,reason};neutral_write.store(w+1,std::memory_order_release);
}
// Diagnostic `zt` lines (20-oct, fix-0b), one ring drained with the others:
//   zt unsafe reason=%s frame=%u                       Begin while not safe (focus, busy or the WhyUnsafe name)
//   zt frame_gap delta=%d snapshot=%u frame=%u epoch_changed=%u phase=%s   Controller::Begin's frame-gap/epoch suspend
//   zt same_frame frame=%u phase=%s                    Begin returned early on a repeated frame with the lock input held
//   zt hit_skew delta=%d snapshot=%u frame=%u admitted=%u                  hitCheck's frame differs from Begin's snapshot
//   zt hit_unsafe reason=%s frame=%u suspended=%u      hitCheck's fresh read not safe after a safe Begin (28-oct)
enum class Diag:uint8_t {Unsafe,Gap,Same,Skew,HitUnsafe};
inline UnsafeLog unsafe_log,hit_unsafe_log;inline DiagLog gap_log,same_log,skew_log;
struct DiagRecord {Diag kind;const char* reason;uint32_t a,b;int32_t delta;uint8_t flag;Phase phase;};
inline DiagRecord diag_records[16];inline std::atomic<unsigned> diag_read{0},diag_write{0};
inline void RecordDiag(const DiagRecord& d){
    const unsigned w=diag_write.load(std::memory_order_relaxed),r=diag_read.load(std::memory_order_acquire);
    if(w-r>=16){dropped.fetch_add(1);return;}
    diag_records[w%16]=d;diag_write.store(w+1,std::memory_order_release);
}
inline void RecordUnsafe(const char* reason,uint32_t frame){RecordDiag({Diag::Unsafe,reason,frame,0,0,0,Phase::Idle});}
// After each Controller::Begin: its frame-gap/epoch suspend (`frame` is the one Begin was given).
inline void NoteGap(uint32_t frame){
    if(!controller.gap){gap_log.Rearm();return;}
    if(gap_log.Step(1,GetTickCount()))
        RecordDiag({Diag::Gap,nullptr,controller.gap_from,frame,int32_t(frame-controller.gap_from),
            uint8_t(controller.gap_epoch?1:0),controller.phase});
}
inline void LogDiag(Logger log,const DiagRecord& d){
    switch(d.kind){
    case Diag::Unsafe:log("zt unsafe reason=%s frame=%u",d.reason?d.reason:"-",d.a);break;
    case Diag::Gap:log("zt frame_gap delta=%d snapshot=%u frame=%u epoch_changed=%u phase=%s",d.delta,d.a,d.b,unsigned(d.flag),Name(d.phase));break;
    case Diag::Same:log("zt same_frame frame=%u phase=%s",d.a,Name(d.phase));break;
    case Diag::Skew:log("zt hit_skew delta=%d snapshot=%u frame=%u admitted=%u",d.delta,d.a,d.b,unsigned(d.flag));break;
    case Diag::HitUnsafe:log("zt hit_unsafe reason=%s frame=%u suspended=%u",d.reason?d.reason:"-",d.a,unsigned(d.flag));break;
    }
}
inline bool NeutralNow(){return neutral.active&&controller.phase==Phase::Recenter&&controller.held;}
inline void RecordState(const char* reason){
    const unsigned w=write_index.load(std::memory_order_relaxed),r=read_index.load(std::memory_order_acquire);
    if(w-r>=64){dropped.fetch_add(1);return;}
    records[w%64]={controller.phase,snapshot.frame,locked.pointer,locked.identity,reason};
    write_index.store(w+1,std::memory_order_release);
}
// [ZTarget] Key: the hold-to-lock key. Default left Ctrl (Donald, 16-oct night: C is awkward; Alt is refused
// because Alt+Space, the lock + knife combo, opens the window's system menu). Accepts LCONTROL/LCTRL/CTRL,
// RCONTROL/RCTRL, MB_MIDDLE, XBUTTON1/XBUTTON2 (mouse side buttons), a letter or digit, or 0xNN.
// lock_token is the input.ini spelling used for the conflict checks ("" for mouse buttons).
inline int lock_vk=0xA2;inline char lock_token[16]="LCONTROL";
// The [ZTarget] Key text as written (ASCII, "LCONTROL" when empty), for the start log lines: a refused key is
// named as the INI wrote it, never as the previous token.
inline char lock_name[24]="LCONTROL";
inline int ParseLockKey(const wchar_t* w){
    char v[24]{};for(int i=0;i<23&&w&&w[i];++i){if(w[i]>0x7E)return -1;v[i]=char(w[i]);}
    if(!*v)return 0xA2;
    struct Named{const char* name;int vk;};
    static const Named names[]={{"LCONTROL",0xA2},{"LCTRL",0xA2},{"CTRL",0xA2},{"CONTROL",0xA2},{"RCONTROL",0xA3},
        {"RCTRL",0xA3},{"MB_MIDDLE",0x04},{"MMB",0x04},{"XBUTTON1",0x05},{"MB_X1",0x05},{"XBUTTON2",0x06},{"MB_X2",0x06},
        {"LMENU",-1},{"RMENU",-1},{"ALT",-1},{"LALT",-1},{"RALT",-1}};
    for(const auto& n:names)if(link_runtime::mortal_draw::prompt::EqualNoCase(v,n.name))return n.vk;
    const int vk=link_runtime::mortal_draw::prompt::ParseKey(v);
    // Escape, V (it switches targets: a V lock could never switch), F-keys the mod or re4_tweaks use and
    // the two main mouse buttons stay refused; the mouse
    // middle/side buttons are allowed above on purpose.
    if(vk<=0||vk==0x01||vk==0x02||vk==0x1B||vk==0x56||(vk>=0x70&&vk<=0x7B))return -1;
    return vk;
}
// Offline tests drive Begin with these (null in the DLL: the real key and foreground window).
inline bool(*lock_down_hook)()=nullptr;inline bool(*focus_hook)()=nullptr;
inline bool LockDown(){return lock_down_hook?lock_down_hook():(GetAsyncKeyState(lock_vk)&0x8000)!=0;}
inline bool Focus(){
    if(focus_hook)return focus_hook();
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();
}
inline bool Busy(){return busy&&busy();}
inline pad::State PadState(bool focused){return pad_enabled?pad_gate.Read(pad_channel.Load(),GetTickCount(),focused):pad::State{};}
inline bool Held(){
    return pad::LockHeld(LockDown(),pad_enabled,pad_mode,pad_toggle.Latched(),
        pad_enabled&&pad_mode==pad::PadMode::Hold&&PadState(Focus()&&!focus_lost.load()).hold);
}
inline bool PadWanted(){return live.load(std::memory_order_acquire)&&pad_enabled;}
inline void PublishPad(bool connected,bool focused,uint16_t buttons_now,int16_t rx,uint8_t lt=0){
    if(PadWanted())pad_channel.PublishHold(connected,focused,pad::HoldPressed(pad_hold,buttons_now,lt),rx,GetTickCount());
}
inline strafe::Transport strafe_transport;
inline bool(*strafe_clear)(uint32_t)=nullptr;
inline thread_local uint32_t strafe_player=0;
inline std::atomic<unsigned> strafe_poses{0},strafe_entries{0},strafe_releases{0},strafe_faults{0};
inline strafe::adapter::Context StrafeContext(){
    strafe::adapter::Context c;c.base=base;c.player=strafe_player;c.epoch=epoch;c.snapshot=have_snapshot?&snapshot:nullptr;
    c.pool=&pool;c.locked=&locked;c.live=live.load(std::memory_order_acquire);c.enabled=strafe_transport.controller.config.enabled;
    c.observe=observe;c.face_target=face_target;c.qualified=strafe_transport.controller.config.qualified;
    c.thread=game_thread.load()==GetCurrentThreadId();c.dispatch_clear=strafe_clear&&strafe_clear(strafe_player);
    c.busy=Busy();c.focused=Focus();c.focus_lost=focus_lost.load();c.held=Held();
    c.lock_held=controller.held;c.lock_active=controller.phase==Phase::Locked;c.waiver=Admitted();
    c.neutral=NeutralNow();c.neutral_yaw=neutral.yaw;return c;
}
inline bool StrafeRead(void*,strafe::Sample& sample){Memory m;return strafe::adapter::Read(StrafeContext(),m,sample);}
inline bool StrafeRoutine(void*,uint32_t player,uint32_t expected,uint32_t next){
    Memory m;const bool ok=strafe::adapter::Routine(StrafeContext(),m,player,expected,next);
    if(ok){if(next)strafe_entries.fetch_add(1);else strafe_releases.fetch_add(1);}else strafe_faults.fetch_add(1);return ok;
}
inline bool StrafePose(void*,const strafe::Sample& expected,const strafe::Output& output){
    Memory m;const bool ok=strafe::adapter::Pose(StrafeContext(),m,expected,output);
    if(ok)strafe_poses.fetch_add(1);else strafe_faults.fetch_add(1);return ok;
}
inline void __cdecl StrafeNormal(strafe::Handler original,void* player){
    if(!live.load(std::memory_order_acquire)||observe||!face_target||game_thread.load()!=GetCurrentThreadId()){
        original(player);return;}
    const uint32_t previous=strafe_player;strafe_player=reinterpret_cast<uint32_t>(player);
    strafe_transport.thread=GetCurrentThreadId();
    __try{strafe_transport.Normal(original,player,strafe_clear&&strafe_clear(strafe_player));}
    __finally{strafe_player=previous;}
}
inline void __cdecl StrafeCore(void* player,void* work,void* camera,uintptr_t caller){
    if(!live.load(std::memory_order_acquire)||observe||!face_target||game_thread.load()!=GetCurrentThreadId()||
       !strafe_clear||!strafe_clear(reinterpret_cast<uint32_t>(player))||caller<base||caller-base>0xFFFFFFFFULL)return;
    strafe_transport.BeforeCore(player,work,camera,static_cast<uint32_t>(caller-base));
}
// Called after shared dispatch preparation and before it is enabled. Gate stays disabled
// until ZTarget.Start has also qualified camera/input/FaceTarget. Observe never calls here.
inline bool PrepareStrafe(uint32_t image,bool(*clear)(uint32_t),Logger log){
    Memory m;if(!clear||!strafe::native::Qualify(m,image)){log("ZTarget strafe disabled: motion/collision pins failed");return false;}
    strafe_clear=clear;strafe_transport.ports={nullptr,&StrafeRead,&StrafeRoutine,&StrafePose};
    strafe_transport.controller.config={true,false,true,strafe_transport.controller.config.speed};
    const auto status=strafe::native::PrepareGate(reinterpret_cast<void*>(image+0x3634FA),strafe_transport,true);
    if(status!=MH_OK){strafe_transport.controller.config={};log("ZTarget strafe disabled: gate_prepare=%s",MH_StatusToString(status));return false;}
    return true;
}
inline void Suspend(){controller.Suspend();locked={};have_view=false;facing.Reset();los_hold.Reset();notice={};}
// Knife latch (Ending Blow leap, ending_blow_leap.h): the knife key (0x800) and the knife-hold routine
// (r1 0x0B) make Safe() false, so the lock is suspended before the sword press can see it. Keep the
// lock the knife suspended (Z pool slot, pointer, id/type) while the knife stays held in the same
// epoch, for at most 120 game frames (60 Hz). Game thread only; cleared on release, scene or epoch
// change, age or use. The sword arbiter matches it against the combat pool (target Prefer).
struct KnifeLatch {bool valid=false;unsigned index=0;uint32_t pointer=0;uint8_t id=0,type=0;uint32_t epoch=0,frame=0;float age=0;};
inline KnifeLatch knife_latch;
inline bool KnifeHeld(const Frame& f){return (f.keys&0x800ULL)!=0||((f.routine>>8)&0xFF)==0x0B;}
// Before the lock is dropped for an unsafe frame of this scene: latch it when the knife is the cause.
inline void KnifeLatchCapture(const Frame& f){
    if(controller.phase==Phase::Locked&&locked.identity&&KnifeHeld(f))
        knife_latch={true,locked.index,locked.pointer,locked.raw.id,locked.raw.type,epoch,f.frame,0.f};
}
// Once per Begin with the fresh frame (after the epoch update).
inline void KnifeLatchAge(const Frame& f){
    auto& l=knife_latch;
    if(l.valid&&(l.epoch!=epoch||!KnifeHeld(f)))l={};
    if(l.valid&&f.frame!=l.frame){l.frame=f.frame;if(ValidDt(f.dt))l.age+=2.f*f.dt;if(l.age>120.f)l={};}
}
inline const KnifeLatch* Latched(){
    if(!live.load(std::memory_order_acquire)||observe||game_thread.load()!=GetCurrentThreadId()||
       !knife_latch.valid||knife_latch.epoch!=epoch)return nullptr;
    return &knife_latch;
}
inline void KnifeLatchUsed(){if(game_thread.load()==GetCurrentThreadId())knife_latch={};}
// Soft suspension (19-oct). The last lock Begin held; while the lock input stays held, a suspension that no hard
// release touched (knife, a mod action, a native routine/camera transition, a dropped camera call) gives the same
// target back on a safe frame, within kResumeSeconds of safe frames. Hard releases (WhyUnsafe: aim, fire,
// event, health; focus, scene) and the input going up clear it. Game thread only.
// bug #4 (24-oct): the tracker gives a slot a new identity whenever one pool read sees it invalid (a Ganado reeling
// from the swing that suspended the lock), so matching by identity alone lost the lock after 11 of 14 knife
// suspensions in the 24-oct log. The same actor is also accepted by slot: same pointer, id, type and max HP, HP not
// risen and alive (a respawn in that slot comes back with full HP, a dead one fails Actor). kResumeSeconds counts
// only safe frames without that actor (was 1 s: a kicked-down Ganado stays down longer); when it runs out with the
// input still held, the lock becomes neutral (as a lost target does) instead of staying suspended until a release.
struct Resume {uint64_t identity=0;uint32_t pointer=0,epoch=0;unsigned index=0;float waited=0;uint8_t id=0,type=0;int16_t hp=0,hp_max=0;};
inline Resume resume;
constexpr float kResumeSeconds=2.0f;
inline Resume ResumeOf(const Candidate& c,uint32_t ep){return {c.identity,c.pointer,ep,c.index,0.f,c.raw.id,c.raw.type,c.raw.hp,c.raw.hp_max};}
inline bool ResumeMatch(const Candidate& c,const Resume& r){
    if(!c.valid||!c.identity||c.pointer!=r.pointer||c.index!=r.index)return false;
    return c.identity==r.identity||(c.raw.id==r.id&&c.raw.type==r.type&&c.raw.hp_max==r.hp_max&&c.raw.hp>0&&c.raw.hp<=r.hp);
}
inline bool ResumePending(){return resume.identity&&resume.epoch==epoch;}
// FaceWith frames that only skipped the yaw write and kept the lock (routine transition, frame gap).
inline std::atomic<unsigned> face_skipped{0};
inline void FaceStop(const char* why){
    const bool active=controller.Active();Suspend();face_rejected.fetch_add(1);
    if(active)RecordState(why);
}
// snapshot can be the preceding camera frame: the player normally moves before CameraMove.
// Fresh reads below (including the target's identity/HP) are mandatory before every yaw write.
template<class Mem> bool FaceWith(Mem& m,bool dispatch_clear,uint32_t player,bool focused,bool held){
    if(!live.load(std::memory_order_acquire)||!face_target||observe||game_thread.load()!=GetCurrentThreadId()||depth)return false;
    if(controller.phase!=Phase::Locked){facing.Reset();return false;}
    if(!dispatch_clear||Busy()||!focused||focus_lost.load()){FaceStop("face_native_owner");return false;}
    // Preserve the frame stamp across a release/repress between two dispatch calls.
    // CameraMove owns lock transitions; the fresh key read here only denies writes.
    if(!held||!controller.held)return false;
    Frame f;const bool read=have_snapshot&&ReadFrame(m,base,f)&&player==f.player;
    if(!read||!SameScene(snapshot,f)){FaceStop("face_stale");return false;}
    // The action prompt alone (a staggered Ganado's kick offer): no yaw write this frame, the lock stays (bug #4).
    if(CameraSafe(f)&&!Safe(f)){facing.Reset();face_skipped.fetch_add(1);return false;}
    if(!Safe(f)){
        // With FaceTarget the knife key suspends the lock here, before Begin: keep it for the sword press.
        KnifeLatchCapture(f);FaceStop(FaceUnsafeName(WhyUnsafe(f)));return false;
    }
    // A camera frame not yet taken (frame gap): only this write is skipped, the lock stays (19-oct: this and the
    // routine gate below used to suspend the whole lock, e.g. on letting go of W with the lock key held).
    if(uint32_t(f.frame-snapshot.frame)>1){facing.Reset();face_skipped.fetch_add(1);return false;}
    // Same qualified normal routines as MortalDrawFromMove. Idle entry is not steady Footwork.
    // Walk's no-W branch writes routine=0 before native Footwork selects Turn. With
    // qualified lock key + A/D strafe, this locomotion transition must not discard the lock.
    // Facing only: let native Footwork run its entry/action selection; do not skip it.
    const auto& sc=strafe_transport.controller.config;
    const bool strafe_transition=f.routine==0&&sc.enabled&&!sc.observe&&sc.qualified&&
        strafe_transport.gate_ready&&!strafe_transport.Failed()&&
        ((f.keys&12)==4||(f.keys&12)==8)&&!(f.keys&~uint64_t(13));
    const unsigned r1=(f.routine>>8)&0xFF;
    // Not steady Footwork (Walk's routine=0 exit when W goes up, Turn 0x20000...): native owns the yaw this frame.
    // Safe() passed, so the lock and the camera stay; facing resumes on the next steady routine.
    if(!strafe_transition&&f.routine!=0x00010000U&&f.routine!=0x01010000U&&!((f.routine&0xFF)==0&&r1>=1&&r1<=4)){
        facing.Reset();face_skipped.fetch_add(1);return false;
    }
    Vec target;
    if(!Admitted()||!pool.Revalidate(m,base,locked,f.pos,target)){
        locked={};controller.phase=Phase::Recenter;facing.Reset();if(lock_sound)voice::Raise(voice::Unlock);
        RecordState("face_target_lost");return false;
    }
    float yaw=f.yaw;
    if(!facing.Step(f.frame,f.dt,f.yaw,f.pos,target,yaw))return false;
    if(!m.Write(f.player+0xA4,&yaw,sizeof(yaw))){face_faults.fetch_add(1);FaceStop("face_write_failed");return false;}
    face_writes.fetch_add(1);return true;
}
inline void PlayerDecide(bool dispatch_clear,uint32_t player){
    if(!live.load(std::memory_order_acquire)||!face_target||observe)return;
    const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);Memory m;
    FaceWith(m,dispatch_clear,player,Focus(),Held());
    std::fesetenv(&env);SetLastError(error);
}
inline void PlayerNative(){
    if(live.load(std::memory_order_acquire)&&face_target&&game_thread.load()==GetCurrentThreadId())FaceStop("face_native_event");
}
// Previous Begin's lock input sources and the last release reason not yet logged (game thread).
inline bool prev_key=false,prev_pad=false;inline const char* input_end=nullptr;
// Revalidate failed: the same live Ganado beyond the hold range/height (out_of_range) or anything else (target_lost).
inline bool OutOfRange(const Candidate& lock,Vec player){
    if(lock.index>=pool.count)return false;
    const Candidate& c=pool.candidates[lock.index];
    if(c.pointer!=lock.pointer||c.identity!=lock.identity||!Actor(c.raw,base))return false;
    const float dx=c.raw.pos.x-player.x,dz=c.raw.pos.z-player.z,d2=dx*dx+dz*dz;
    return d2<300.f*300.f||d2>kHold*kHold||std::fabs(c.raw.pos.y-player.y)>1800;
}
// The zt reason of a controller transition Begin did not name (was "input_or_target_loss" for all of them).
inline const char* TransitionReason(Phase before,Phase now,bool scene){
    if(now==Phase::Suspended)return scene?"scene":"frame_gap";
    if(now==Phase::Recenter)return before==Phase::Locked?"target_lost":"no_target";
    if(before==Phase::Release&&now==Phase::Idle)return "release_done";
    if(now==Phase::Release||now==Phase::Idle){const char* r=input_end;input_end=nullptr;
        return r?r:before==Phase::Suspended?"native_done":"input_up";}
    return "other";
}
inline void Begin(){
    Memory m;Frame f;const bool read=ReadFrame(m,base,f);
    const bool focus=Focus()&&!focus_lost.exchange(false,std::memory_order_acq_rel);
    const bool same=read&&have_snapshot&&snapshot.frame==f.frame&&SameScene(snapshot,f);
    const pad::State ps=PadState(focus);
    const Unsafe unsafe_why=read?WhyUnsafe(f):Unsafe::None;
    const bool hard=!focus||(read&&HardRelease(unsafe_why));
    // Toggle (ZTargetPadMode, default): a tap of the pad button latches the lock; Toggle releases it (pad::Toggle).
    // A soft-suspended lock (the knife, a mod action) still counts as locked, so the latch survives the swing.
    bool toggle_now=false;
    if(pad_mode==pad::PadMode::Toggle)
        toggle_now=pad_enabled&&pad_toggle.Step(ps.hold,(controller.phase==Phase::Locked&&locked.identity)||
            (controller.phase==Phase::Suspended&&(ResumePending()||neutral.resume)&&!hard)||
            (controller.phase==Phase::Recenter&&controller.held),   // neutral holds the latch (no auto-release)
            hard,read&&ValidDt(f.dt)?2.f*f.dt:1.f);
    // Keyboard and pad are independent sources (pad::LockHeld): no pad state can release what the key holds.
    const bool key_now=LockDown(),pad_now=pad::PadSource(pad_enabled,pad_mode,toggle_now,ps.hold);
    if(!same){
        if(const char* r=pad::ReleaseReason(prev_key,key_now,prev_pad,pad_now,pad_mode,pad_toggle.LastEnd()))input_end=r;
        else if(key_now||pad_now)input_end=nullptr;
        prev_key=key_now;prev_pad=pad_now;
    }
    const ButtonState key=buttons.Read(focus,pad::LockHeld(key_now,pad_enabled,pad_mode,toggle_now,ps.hold),
        (GetAsyncKeyState('V')&0x8000)!=0,ps.stick,!same);
    if(!read){if(have_snapshot){++epoch;pool.Reset(epoch);}have_snapshot=false;knife_latch={};resume={};Suspend();
        RecordNeutral(neutral.Step(controller.phase,false,0.f,epoch,false,true),"no_frame");return;}
    const bool safe=CameraSafe(f)&&focus&&!Busy();
    const bool scene=have_snapshot&&!SameScene(snapshot,f);
    if(scene){++epoch;pool.Reset(epoch);have_view=false;resume={};}
    snapshot=f;have_snapshot=true;input={safe,key.hold,f.frame,epoch,f.dt};
    KnifeLatchAge(f);
    if(hard||!key.hold)resume={};
    if(hard)neutral.resume=false;
    if(!safe){KnifeLatchCapture(f);const Phase old=controller.phase;controller.Begin(input,false);NoteGap(f.frame);locked={};have_view=false;
        const char* why_unsafe=!focus?"focus":unsafe_why!=Unsafe::None?UnsafeName(unsafe_why):"busy";
        if(unsafe_log.Step(why_unsafe,GetTickCount()))RecordUnsafe(why_unsafe,f.frame);
        RecordNeutral(neutral.Step(controller.phase,key.hold,f.yaw,epoch,!hard,hard),why_unsafe);
        if(old!=controller.phase)RecordState(why_unsafe);return;}
    unsafe_log.Step(nullptr,0);
    if(same){
        if(key.hold&&same_log.Step(1,GetTickCount()))RecordDiag({Diag::Same,nullptr,f.frame,0,0,0,controller.phase});
        return;
    }
    same_log.Rearm();
    if(have_view&&uint32_t(f.frame-view_frame)!=1)have_view=false;
    view.valid=have_view;
    const Phase before=controller.phase;const uint64_t old_id=locked.identity;
    if(ValidDt(f.dt))switch_seconds+=f.dt/30.f;
    const float seconds=ValidDt(f.dt)?f.dt/30.f:0.f;
    const bool admitted=Admitted();
    const bool want_notice=notice_on&&admitted&&!key.hold&&!controller.held&&controller.phase==Phase::Idle;
    if(key.hold||want_notice)pool.Read(m,base,epoch,f.pos,view);
    const char* why=nullptr;bool resumed=false;
    // Soft suspension over, lock input still held: the same target comes back (Resume), never another one.
    if(controller.phase==Phase::Suspended&&key.hold&&ResumePending()){
        int n=-1;
        if(resume.index<pool.count&&ResumeMatch(pool.candidates[resume.index],resume))n=int(resume.index);
        if(n>=0&&admitted&&LineTo(f.pos,pool.candidates[n].raw.pos)==los::Result::Clear){
            locked=pool.candidates[n];los_hold.Start(locked.identity,f.frame);resumed=true;why="resume";
            controller.phase=Phase::Locked;controller.held=true;controller.filter.Reset();controller.release_s=0;
        }else if((resume.waited+=seconds)>kResumeSeconds){
            // Gone (dead, out of reach or out of sight) for the whole window: neutral Z-target, never another lock.
            resume={};controller.phase=Phase::Recenter;controller.held=true;controller.filter.Reset();controller.release_s=0;
            why="resume_expired";
        }
    }
    // Neutral (no target) soft-suspended by the knife, input still held: neutral comes back, never a new lock.
    if(controller.phase==Phase::Suspended&&key.hold&&!ResumePending()&&neutral.Pending(epoch,seconds)){
        controller.phase=Phase::Recenter;controller.held=true;controller.filter.Reset();controller.release_s=0;why="neutral_resume";
    }
    const bool press=key.hold&&!controller.held&&controller.phase!=Phase::Suspended;
    if(press){
        const bool any=Pick(pool.candidates,pool.count,-1,0)>=0;
        const int picked=admitted?PickClear(&LineTo,f.pos,-1,0):-1;
        locked=picked>=0?pool.candidates[picked]:Candidate{};
        if(locked.identity)los_hold.Start(locked.identity,f.frame);
        else if(any){los_refused.fetch_add(1);why="no_los";}
    }
    if(controller.phase==Phase::Locked){
        Vec p;
        if(!pool.Revalidate(m,base,locked,f.pos,p)){why=OutOfRange(locked,f.pos)?"out_of_range":"target_lost";
            locked={};controller.phase=Phase::Recenter;}
        else if(!los_hold.Step(locked.identity,f.frame,seconds,[&]{return LineTo(f.pos,p);})){
            locked={};controller.phase=Phase::Recenter;los_lost.fetch_add(1);why="los_lost";}
        else if(key.change&&switch_seconds>=0.2f){
            const int next=PickClear(&LineTo,f.pos,int(locked.index),key.change);
            if(next>=0){locked=pool.candidates[next];los_hold.Start(locked.identity,f.frame);}switch_seconds=0;
        }
    }
    controller.Begin(input,locked.identity!=0);NoteGap(f.frame);
    if(controller.phase!=Phase::Locked){locked={};los_hold.Reset();}
    // Safe frame: a frame gap or a hit-side suspend (no_shoulder_call, a non-rigid matrix) is soft, as for a lock; a
    // scene is hard (the lock's resume is cleared on it too).
    RecordNeutral(neutral.Step(controller.phase,controller.held,f.yaw,epoch,!scene,false),
        controller.phase==Phase::Locked?"acquire":!key.hold?"input_up":why?why:"no_target");
    // This frame's pool read of the lock (its HP now, not at acquisition) when it is still that slot's identity.
    if(controller.phase==Phase::Locked&&locked.identity)resume=ResumeOf(locked.index<pool.count&&
        pool.candidates[locked.index].identity==locked.identity&&pool.candidates[locked.index].pointer==locked.pointer?
        pool.candidates[locked.index]:locked,epoch);
    else if(controller.phase!=Phase::Suspended)resume={};
    if(want_notice&&controller.phase==Phase::Idle){
        const int n=Pick(pool.candidates,pool.count,-1,0);
        if(n<0)notice={};
        else{
            const Candidate& c=pool.candidates[n];
            const bool fresh=c.identity!=notice.c.identity||uint32_t(f.frame-notice.checked)>=los::kEvery;
            notice.c=c;
            if(fresh){notice.checked=f.frame;notice.shown=LineTo(f.pos,c.raw.pos)==los::Result::Clear;}
        }
    }else notice={};
    if(lock_sound){
        if(controller.phase==Phase::Locked&&locked.identity&&locked.identity!=old_id&&!resumed)voice::Raise(voice::Lock);
        else if(before==Phase::Locked&&old_id&&(controller.phase==Phase::Release||controller.phase==Phase::Recenter))
            voice::Raise(voice::Unlock);
    }
    if(before!=controller.phase||old_id!=locked.identity)
        RecordState(why?why:locked.identity?(old_id&&before==Phase::Locked?"switch":"acquire"):
            TransitionReason(before,controller.phase,scene));
}
inline void CaptureView(){
    Memory m;Frame f;have_view=false;
    if(!have_snapshot||!ReadFrame(m,base,f)||!CameraSafe(f)||!SameScene(snapshot,f)||f.frame!=snapshot.frame||Busy())return;
    View v;Vec campos,target;
    if(!Get(m,f.global+0xA4,v.view)||!Get(m,f.global+0xD8,v.projection)||
       !Get(m,f.global+0x118,campos)||!Get(m,f.global+0x124,target)||!mt::FaceYaw(campos,target,v.yaw))return;
    for(const auto& row:v.view.v)for(float x:row)if(!std::isfinite(x))return;
    for(const auto& row:v.projection)for(float x:row)if(!std::isfinite(x))return;
    v.valid=true;view=v;view_frame=f.frame;have_view=true;
}
// Locked: the red cursor over the lock (the lock key held). Otherwise, with Notice=1 and nothing held, the yellow one
// over the clear candidate a press would lock (TP's notice cursor).
template<class Mem>bool HudWith(Mem& m,hud::Frame& out,bool focused,bool held){
    if(!live.load(std::memory_order_acquire)||observe||game_thread.load()!=GetCurrentThreadId()||
       !focused||focus_lost.load()||Busy()||!have_snapshot||!have_view||!view.valid||!Admitted())return false;
    const Candidate* c=nullptr;uint8_t mode=0;
    if(held&&controller.phase==Phase::Locked&&controller.held){c=&locked;mode=uint8_t(reticle::Mode::Locked);}
    else if(!held&&notice_on&&notice.shown&&notice.c.identity&&controller.phase==Phase::Idle&&!controller.held){
        c=&notice.c;mode=uint8_t(reticle::Mode::Notice);}
    else return false;
    Frame f;Vec target;float fovy=0;
    if(!ReadFrame(m,base,f)||!CameraSafe(f)||(f.stop&0x40)||!SameScene(snapshot,f)||f.frame!=snapshot.frame||
       view_frame!=f.frame||!pool.Revalidate(m,base,*c,f.pos,target)||!Get(m,f.global+0x134,fovy))return false;
    target.y+=kCursorTip;out.camera=view;out.target=target;out.fovy=fovy;out.tick=f.frame;out.mode=mode;out.identity=c->identity;
    out.player=f.player;out.global=f.global;out.costume=f.costume;out.room=f.room;out.cam_system=f.cam_system;return true;
}
inline bool HudFrame(hud::Frame& out){
    Memory m;const HWND window=GetForegroundWindow();DWORD pid=0;RECT client{};
    if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId()||!GetClientRect(window,&client)||
       !HudWith(m,out,true,Held())||!Get(m,base+0xCED730,out.post))return false;
    out.width=client.right-client.left;out.height=client.bottom-client.top;return true;
}
// ---- 3D arrow (ztarget_arrow3d.h, 21-oct; docs/ztarget-3d-diseno-2026-10-21.md) ----------------------------------
// [ZTarget] Reticle3D=1 with Reticle=1. The DLL starts the drawer before Start (arrow_request: requested, or why it was
// refused); Start turns it on with the camera (never with Observe=1) and then installs the 2D cursor muted (HudMuted):
// its hook stays as the fallback and paints only while the 3D arrow is off. FrontDetour publishes HudWith's target
// every camera frame (ArrowPublish); the player dispatch draws it (ArrowFrame), where the trail and the charge glow
// register. Both run on the game thread: FaceWith, called from that dispatch, already requires game_thread
// (face_writes > 0 live, 19-oct logs), so the snapshot is a plain value and ArrowFrame refuses any other thread.
struct ArrowRequest {bool requested=false;const char* refused=nullptr;};
inline ArrowRequest arrow_request;inline bool arrow_on=false;
inline arrow3d::Target arrow_target;inline hud::Clock arrow_clock;
inline bool HudMuted(hud::Frame&){return false;}
// Game thread, after CaptureView. An invalid snapshot (no cursor this frame) replaces the last one at once.
template<class Mem> void ArrowPublishWith(Mem& m,bool focused,bool held,uint32_t now_ms){
    hud::Frame f;arrow3d::Target t;
    if(HudWith(m,f,focused,held)){
        t.valid=true;t.mode=f.mode;t.identity=f.identity;t.tip={f.target.x,f.target.y,f.target.z};t.tick=f.tick;t.room=f.room;}
    t.time=now_ms;arrow_target=t;
}
inline void ArrowPublish(){if(arrow_on){Memory m;ArrowPublishWith(m,Focus(),Held(),GetTickCount());}}
// The player dispatch: the snapshot, refused from another thread, older than kStaleMs (then dropped: counted once, and
// never fresh again after a tick-count wrap), from another room, or with this frame unreadable, not Safe or the HUD
// stopped; then this frame's arrow (once per native generation). now_s: the cursor clocks' seconds (hud::Now), for the
// animation (restarts on a new mode or target, as the 2D) and the spin. live first: the dispatch can run while Start
// still writes arrow_on (it is set before live's release store and never after).
template<class Mem> arrow3d::runtime::Result ArrowFrameWith(Mem& m,uint32_t now_ms,double now_s){
    namespace ar=arrow3d::runtime;
    if(!live.load(std::memory_order_acquire)||!arrow_on)return ar::Result::NotReady;
    const DWORD thread=game_thread.load();
    if(!thread)return ar::Result::NotReady;   // no camera frame yet
    if(thread!=GetCurrentThreadId()){ar::wrong_thread.fetch_add(1);return ar::Result::WrongThread;}
    const arrow3d::Target t=arrow_target;
    if(!t.valid)return ar::Result::Empty;
    Frame f;const bool fresh=ReadFrame(m,base,f)&&CameraSafe(f)&&!(f.stop&0x40);
    switch(arrow3d::AdmitTarget(t,now_ms,fresh,f.room)){
    case arrow3d::Admit::Show:break;
    case arrow3d::Admit::Stale:ar::stale.fetch_add(1);arrow_target.valid=false;return ar::Result::Empty;
    case arrow3d::Admit::Unsafe:ar::unsafe.fetch_add(1);return ar::Result::Empty;
    case arrow3d::Admit::Room:ar::room.fetch_add(1);return ar::Result::Empty;
    default:return ar::Result::Empty;
    }
    return ar::Submit(arrow3d::LookAt(reticle::Mode(t.mode),arrow_clock.Seconds(t.mode,t.identity,now_s),now_s),t.tip);
}
inline void ArrowFrame(){
    if(!live.load(std::memory_order_acquire)||!arrow_on)return;
    const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);Memory m;
    ArrowFrameWith(m,GetTickCount(),hud::Now());
    std::fesetenv(&env);SetLastError(error);
}
// ---- Ending Blow camera lift (ending_blow_camera.h, 17-oct) --------------------------------------------------
// A HitDetour stage of its own, after the Z-target one: during an Ending Blow lease hitCheck gets a lifted copy of the
// offsets (of Z-target's copy when that one changed them) and CamSmth's state is kept in step. Admitted apart from
// Z-targeting (challenge 3): with ZTarget=0, a refused key or an input.ini conflict it still works when its own pins
// pass; FrontDetour then only fixes game_thread and depth. Its own observe switch. Game thread only, like the rest.
namespace cam=link_runtime::ending_blow::camera;
// Set by the DLL before Start (spin_attack_runtime.h SwordCameraPrepare). requested: EndingBlow=1 (its ready line is
// logged); refused: off (EndingBlowCameraLift=0), clip_mismatch (not the 2 m clip), no_clock.
struct LiftRequest {bool requested=false;unsigned percent=0;bool observe=false;const char* refused=nullptr;};
inline LiftRequest lift_request;
inline bool(*lift_source)(cam::Facts&)=nullptr;
// Game-thread log rings (the DLL's CmbLog / CmbLogV); lift_verbose: LogLevel 2, the samples read memory only then.
inline Logger lift_log=nullptr,lift_log_v=nullptr;inline bool lift_verbose=false;
inline std::atomic<bool> lift_live{false};inline bool lift_observe=false;inline unsigned lift_percent=0;
inline cam::Tracker lift_tracker;inline cam::Smooth lift_smooth;inline bool lift_seen=false;
// writes/dry count frames; state: CamSmth state writes; orphans: a lift left in CamSmth when hitCheck stopped.
inline std::atomic<unsigned> lift_hits{0},lift_writes{0},lift_dry{0},lift_rejects{0},lift_state{0},lift_orphans{0};
// This call's stage, from PrepareLiftWith to FinishLiftWith and LiftAfterFrontWith (depth 1, game thread).
// asked/native/shoulder: campos[0] as handed to hitCheck, before the lift, and campos[1] (local, like hitCheck's out).
struct LiftCall {
    bool engaged=false,fresh=false,read=false,have_state=false;float lift=0,applied=0,qfps_y=NAN,pull=NAN;
    Vec asked{},native{},shoulder{},state{};uint64_t lease=0;double e=0;float mot=NAN;
};
inline LiftCall lift_call;
inline void LiftLogClosed(){
    cam::Summary s;char line[512];
    while(lift_tracker.Take(s))if(lift_log&&cam::Format(line,sizeof(line),s)>0)lift_log("%s",line);
}
// Brings the camera chain's state to native + want (challenge 1): Campos.y and Target.y of CamSmth's m_effect, of its
// backup (the next move starts from it while the tail's near-head shove holds, UHD +0x12C; otherwise the tail
// overwrites it) and of m_Inter.param (CamSmth's input; it low-passes cur while its counter runs, else cur replaces
// it) (fix-08 review). All six are checked before any write. A failed read or write gives the state up (Drop) and
// returns false: no lifted copy, and the lease's lift ends.
template<class Mem> bool SmoothTo(Mem& m,float want){
    const float delta=lift_smooth.Delta(want);
    if(delta==0.f)return true;
    const uint32_t at[6]={base+cam::rva::smooth_campos+4,base+cam::rva::smooth_target+4,base+cam::rva::smooth_backup+4,
        base+cam::rva::smooth_backup+0x10,base+cam::rva::inter+4,base+cam::rva::inter+0x10};
    float y[6]{};
    for(unsigned i=0;i<6;++i)if(!m.Writable(at[i],4)||!m.Read(at[i],&y[i],4)||!std::isfinite(y[i])){lift_smooth.Drop();return false;}
    for(unsigned i=0;i<6;++i){y[i]+=delta;if(!m.Write(at[i],&y[i],4)){lift_smooth.Drop();return false;}}
    lift_smooth.Commit(want);lift_state.fetch_add(1);lift_tracker.Smoothed();return true;
}
// The smoothed Campos the next (or the last) CamSmth.move works from: the backup while hold is set.
template<class Mem> bool SmoothedCampos(Mem& m,Vec& out){
    uint8_t hold=0;return Get(m,base+cam::rva::smooth_hold,hold)&&Get(m,base+(hold?cam::rva::smooth_backup:cam::rva::smooth_campos),out);
}
// Kept separate from the ABI entry like PrepareHitWith. src: the offsets hitCheck would get (Z-target's copy or the
// caller's, never written). True: hand hitCheck `out` instead. Self, caller, thread and depth fail before any read; the
// frame, matrix and offsets are read only during an Ending Blow lease.
template<class Mem> bool PrepareLiftWith(Mem& m,void* self,uint32_t caller,const Matrix* matrix,const Offset* src,Offset& out){
    lift_call={};
    if(!lift_live.load(std::memory_order_acquire)||depth!=1||game_thread.load()!=GetCurrentThreadId()||
       reinterpret_cast<uint32_t>(self)!=base+rva::qfps||caller!=base+rva::hit_return)return false;
    lift_seen=true;lift_hits.fetch_add(1);
    cam::Facts facts;if(!lift_source||!lift_source(facts))facts={};
    cam::Scene s;Matrix mat{};Offset in{};
    if(facts.lease&&facts.ending){
        Frame f;
        s.read=ReadFrame(m,base,f)&&m.Read(reinterpret_cast<uint32_t>(matrix),&mat,sizeof(mat))&&
            m.Read(reinterpret_cast<uint32_t>(src),&in,sizeof(in));
        if(s.read){s.frame=f.frame;s.player=f.player;s.room=f.room;s.damage=f.damage;s.healthy=f.healthy;}
        else lift_rejects.fetch_add(1);
    }
    const cam::Tick t=lift_tracker.Step(facts,s,lift_percent);
    float lift=t.lift;Offset up;
    if(lift>0.f&&!cam::Apply(mat,in,lift,up)){lift_tracker.Abort(cam::End::NotRigid);lift=0.f;}
    bool write=lift>0.f&&!lift_observe;
    // Every call, lease or not: a lift left in CamSmth (landing, abort, lease end) comes out on the next hitCheck.
    if(!SmoothTo(m,write?lift:0.f)){lift_tracker.Abort(cam::End::Smooth);write=false;}
    if(t.fresh){
        if(t.first){float ratio=-1.f,inter=-1.f;Get(m,base+cam::rva::smooth_ratio,ratio);Get(m,base+cam::rva::inter_frame,inter);
            lift_tracker.Start(ratio,inter,facts.mot);}
        if(write){lift_writes.fetch_add(1);lift_tracker.Wrote();}
        else if(lift>0.f&&lift_observe){lift_dry.fetch_add(1);lift_tracker.Dry();}
    }
    LiftLogClosed();
    if(t.open){
        auto& c=lift_call;c.engaged=true;c.fresh=t.fresh;c.read=s.read;c.lift=lift;c.applied=write?lift:0.f;
        c.asked=write?up.campos[0]:in.campos[0];c.native=in.campos[0];c.shoulder=in.campos[1];c.lease=facts.lease;c.e=facts.e;c.mot=facts.mot;
        if(t.fresh&&lift_tracker.summary().f<0.f)c.have_state=SmoothedCampos(m,c.state);
    }
    if(write)out=up;
    return write;
}
// After hitCheck: its camera (out->Campos, local like the offsets; fix-08) against the one asked for: the pull and the
// ceiling cap.
template<class Mem> void FinishLiftWith(Mem& m,const void* out){
    auto& c=lift_call;Vec local;
    if(!c.engaged||!c.fresh||!c.read||!Get(m,reinterpret_cast<uint32_t>(out),local))return;
    const cam::Probe p=cam::Measure(c.native,c.asked,c.shoulder,local);
    c.qfps_y=local.y;c.pull=p.pull;lift_tracker.Pull(p.pull,p.ratio,c.applied);
}
// After CameraMove. No hitCheck this time (another camera mode): forget the lift left in CamSmth (the new camera sets
// ratio 0 or the QFPS init snaps; otherwise it eases out natively) and end this lease's lift. Otherwise the factor
// estimate (once per lease) and the LogLevel 2 sample every 4 game frames.
template<class Mem> void LiftAfterFrontWith(Mem& m){
    if(!lift_seen){
        if(lift_smooth.Applied()!=0.f){lift_smooth.Drop();lift_orphans.fetch_add(1);}
        lift_tracker.Abort(cam::End::Camera);return;
    }
    const auto& c=lift_call;if(!c.engaged||!c.fresh)return;
    Vec after,fed;float ratio=0,dt=0;uint32_t g=0;   // fed: m_Inter's camera, CamSmth's input
    if(c.have_state&&SmoothedCampos(m,after)&&Get(m,base+cam::rva::inter,fed)&&
       Get(m,base+cam::rva::smooth_ratio,ratio)&&Get(m,base+rva::global,g)&&Get(m,g+0x70,dt))
        lift_tracker.Factor(cam::Factor(c.state,after,fed,ratio,dt));
    if(!lift_verbose||!lift_log_v||!lift_tracker.Sample(c.e))return;
    cam::Sample s;s.lease=c.lease;s.e=c.e;s.mot=c.mot;s.lift=c.lift;s.applied=c.applied;s.qfps_y=c.qfps_y;s.pull=c.pull;
    Vec v;if(Get(m,base+cam::rva::smooth_campos,v))s.smooth_y=v.y;
    if((g||Get(m,base+rva::global,g))&&Get(m,g+0x118,v))s.final_y=v.y;
    char line[384];if(cam::FormatSample(line,sizeof(line),s)>0)lift_log_v("%s",line);
}
inline bool PrepareLift(void* self,uint32_t caller,const Matrix* matrix,const Offset* src,Offset& out){
    Memory m;return PrepareLiftWith(m,self,caller,matrix,src,out);
}
inline void FinishLift(const void* out){Memory m;FinishLiftWith(m,out);}
inline void LiftAfterFront(){Memory m;LiftAfterFrontWith(m);}
// SwordLifecycle end (game thread): close the lease's record and log it now.
inline void LiftLeaseEnd(uint64_t lease,bool finished){
    if(!lift_live.load(std::memory_order_acquire)||game_thread.load()!=GetCurrentThreadId())return;
    if(lift_tracker.Finish(lease,finished?cam::End::Finished:cam::End::Interrupted))LiftLogClosed();
}
// ---- Back Slice camera (back_slice_camera.h, backslice-cam-01, 19-oct) -------------------------------------------
// Two stages of its own, admitted apart from Z-targeting and the lift: the fall hold inside CameraControl::Check (a
// mid-function hook at 0x2F5990, FallDetour) and a HitDetour stage after the lift's (PrepareOrbitWith: the turned and
// vertically followed offsets, the camera-state carry; FinishOrbitWith: CamSmth's snap when a wall pulled the camera).
// Both act only during a clean Back Slice lease the dispatch owns, and the orbit's easing back after it. Game thread
// only (CameraMove, depth 1), like the rest.
namespace bsc=link_runtime::back_slice::camera;
// Set by the DLL before Start (spin_attack_runtime.h SwordBackCameraPrepare). requested: the Back Slice is on (its
// ready line is logged); refused: off (BackSliceCamera=0), no_clock.
struct OrbitRequest {bool requested=false;bsc::Config config;const char* refused=nullptr;};
inline OrbitRequest orbit_request;
inline bool(*orbit_source)(bsc::Facts&)=nullptr;
inline Logger orbit_log=nullptr;   // the DLL's game-thread ring (CmbLog)
inline std::atomic<bool> orbit_live{false};inline bsc::Config orbit_config;
inline bsc::Orbit orbit;inline bsc::Pose orbit_pose;inline bool orbit_seen=false;
// hits: hitCheck calls seen; writes: fresh frames handed a turned copy; carries: camera-state carries; holds: fall
// tests held; rejects: unreadable frames inside a Back Slice.
inline std::atomic<unsigned> orbit_hits{0},orbit_writes{0},orbit_carries{0},orbit_holds{0},orbit_rejects{0},orbit_snaps{0};
// This call's orbit, from PrepareOrbitWith to FinishOrbitWith: the camera the orbit wanted before the wall probe
// (local, like hitCheck's output), so a probe pull snaps CamSmth like a hitCheck pull.
struct OrbitCall {bool engaged=false;Vec asked{};};
inline OrbitCall orbit_call;
// The wall probe (bsc::ProbeRay through EatMgr, the LOS test's native segment; tests inject a fake). orbit_probe_t /
// orbit_probe_b: the shares of the follow's rise and back-off used last frame (1 = clear), reset when a lease opens.
inline bool orbit_probe_on=false;inline bool(*orbit_segment)(const Vec&,const Vec&,uint32_t&)=nullptr;
inline float orbit_probe_t=1.f,orbit_probe_b=1.f;
inline std::atomic<unsigned> orbit_probes{0},orbit_probe_pulls{0},orbit_probe_faults{0};
inline void* fall_original=nullptr;
// Check's d.y (Link's hips over his feet) as the fall hook saw it inside a Back Slice lease, and the CameraMove it
// belongs to (`fronts` when it was read): the vertical follow uses it only in that same CameraMove.
inline float orbit_hips=NAN;inline unsigned orbit_hips_front=0;inline bool orbit_hips_known=false;
inline void OrbitLogClosed(){
    bsc::Summary s;char line[384];
    while(orbit.Take(s))if(orbit_log&&bsc::Format(line,sizeof(line),s)>0)orbit_log("%s",line);
}
// The camera chain's state moved from the last frame's base transform to this one's (bsc::Carry): CamSmth m_effect
// (Campos, Target), its backup (the next move starts from it while the near-head hold is set) and m_Inter.param
// (CamSmth's input while m_Inter interpolates), the Ending Blow lift's addresses. All six read, checked writable and
// carried before any write; false = nothing written.
template<class Mem> bool CarryState(Mem& m,const bsc::Pose& from,const bsc::Pose& to){
    const uint32_t at[6]={base+cam::rva::smooth_campos,base+cam::rva::smooth_target,base+cam::rva::smooth_backup,
        base+cam::rva::smooth_backup+0xC,base+cam::rva::inter,base+cam::rva::inter+0xC};
    Vec v[6];
    for(unsigned i=0;i<6;++i)if(!m.Writable(at[i],sizeof(Vec))||!m.Read(at[i],&v[i],sizeof(Vec))||!Finite(v[i]))return false;
    for(unsigned i=0;i<6;++i)if(!bsc::Carry(from,to,(i&1)==0,v[i]))return false;
    for(unsigned i=0;i<6;++i)if(!m.Write(at[i],&v[i],sizeof(Vec)))return false;
    return true;
}
// The wall probe on this call's copy: the follow's own move of the camera (pose.dc, world) through EatMgr, from the
// turned camera at the lock camera's offset: the rise (or drop) first, then the back-off from where that stopped. A
// blocked leg comes in to the eased share of it (in at once, out over kProbeOutTau). h: this frame's seconds (0 on a
// second call: the shares are reused). A fault on a leg leaves it whole.
inline void ProbeWall(const Matrix& mat,Offset& turned,const bsc::Pose& pose,float h,bool fresh){
    const Vec dc=pose.dc;
    if(!(std::fabs(dc.y)>1.f||dc.x*dc.x+dc.z*dc.z>1.f))return;   // no follow: the lock camera's own spot
    const cam::Inverse inv=cam::Invert(mat);if(!inv.ok)return;
    const Vec b{mat.v[0][3],mat.v[1][3],mat.v[2][3]},c=World(mat,turned.campos[0]);
    const Vec asked{c.x+b.x,c.y+b.y,c.z+b.z},start{asked.x-dc.x,asked.y-dc.y,asked.z-dc.z},rise{start.x,start.y+dc.y,start.z};
    auto seg=[](Vec a,Vec z,bool& hit){uint32_t attr=0;if(!orbit_segment(a,z,attr))return false;hit=attr!=0;return true;};
    if(fresh){
        orbit_probes.fetch_add(1);float up=1.f,back=1.f;
        if(std::fabs(dc.y)>1.f&&bsc::ProbeRay(seg,start,rise,up)==bsc::Probe::Fault){orbit_probe_faults.fetch_add(1);orbit.ProbeFault();up=1.f;}
        orbit_probe_t=bsc::EaseProbe(orbit_probe_t,up,h);
        const Vec mid=bsc::Lerp(start,rise,orbit_probe_t),outer{mid.x+dc.x,mid.y,mid.z+dc.z};
        if(dc.x*dc.x+dc.z*dc.z>1.f&&bsc::ProbeRay(seg,mid,outer,back)==bsc::Probe::Fault){orbit_probe_faults.fetch_add(1);orbit.ProbeFault();back=1.f;}
        orbit_probe_b=bsc::EaseProbe(orbit_probe_b,back,h);
    }
    if(!(orbit_probe_t<1.f||orbit_probe_b<1.f))return;
    const Vec mid=bsc::Lerp(start,rise,orbit_probe_t),w=bsc::Lerp(mid,{mid.x+dc.x,mid.y,mid.z+dc.z},orbit_probe_b);
    const Vec local=cam::Mul(inv,{w.x-b.x,w.y-b.y,w.z-b.z});
    if(!Finite(local))return;
    if(fresh){orbit_probe_pulls.fetch_add(1);orbit.Probed(cam::Distance(w,asked));}
    turned.campos[0]=local;
}
// Kept separate from the ABI entry like PrepareLiftWith. src: the offsets hitCheck would get (the lift's or
// Z-target's copy, or the caller's; never written). True: hand hitCheck `out` instead. Self, caller, thread and depth
// fail before any read; outside a Back Slice (and its easing back) nothing more is read.
template<class Mem> bool PrepareOrbitWith(Mem& m,void* self,uint32_t caller,const Matrix* matrix,const Offset* src,Offset& out){
    orbit_call={};
    if(!orbit_live.load(std::memory_order_acquire)||depth!=1||game_thread.load()!=GetCurrentThreadId()||
       reinterpret_cast<uint32_t>(self)!=base+rva::qfps||caller!=base+rva::hit_return)return false;
    orbit_seen=true;orbit_hits.fetch_add(1);
    bsc::Facts facts;if(!orbit_source||!orbit_source(facts))facts={};
    if(orbit.phase()==bsc::Phase::Off&&!bsc::Engaged(facts))return false;
    bsc::Scene s;Matrix mat{};Offset in{};Frame f;
    const bool read=ReadFrame(m,base,f)&&m.Read(reinterpret_cast<uint32_t>(matrix),&mat,sizeof(mat))&&
        m.Read(reinterpret_cast<uint32_t>(src),&in,sizeof(in));
    if(read){s.read=true;s.frame=f.frame;s.player=f.player;s.room=f.room;s.damage=f.damage;s.healthy=f.healthy;s.dt=f.dt;
        if(orbit_hips_known&&orbit_hips_front==fronts.load())s.hips=orbit_hips;}
    else orbit_rejects.fetch_add(1);
    const bsc::Tick t=orbit.Step(facts,s,read?bsc::ViewYaw(mat,in):NAN,orbit_config);
    if(!t.apply){OrbitLogClosed();return false;}
    bsc::Pose pose;Offset turned;const bsc::Crane crane=bsc::CraneAt(t.height,orbit_config.lift);
    if(!bsc::Apply(mat,in,t.delta,crane,turned,pose)){
        orbit.Abort(bsc::End::Matrix);OrbitLogClosed();return false;}
    const Vec asked=turned.campos[0];
    if(t.first)orbit_probe_t=orbit_probe_b=1.f;
    if(orbit_probe_on&&orbit_segment)ProbeWall(mat,turned,pose,t.fresh?game_clock::ValidDt(s.dt)/30.f:0.f,t.fresh);
    if(t.fresh){
        if(t.carry&&orbit_pose.valid){
            if(!CarryState(m,orbit_pose,pose)){orbit.Abort(bsc::End::Smooth);orbit_pose={};OrbitLogClosed();return false;}
            orbit.Carried();orbit_carries.fetch_add(1);
        }
        orbit_pose=pose;orbit_writes.fetch_add(1);
    }
    orbit_call.engaged=true;orbit_call.asked=asked;
    out=turned;return true;
}
// After hitCheck (its camera is local, like the offsets): a wall pulled the asked camera in by more than kSnapPull, so
// this CameraMove's CamSmth snaps to the collided camera (m_flag bit 0, cleared by CamSmth itself; CameraControl::move
// clears it too while m_Inter runs). A failed read or write leaves the native smoothing.
template<class Mem> void FinishOrbitWith(Mem& m,const void* out){
    auto& c=orbit_call;Vec local;
    if(!c.engaged)return;
    c.engaged=false;
    if(!Get(m,reinterpret_cast<uint32_t>(out),local)||!Finite(local))return;
    const float pull=cam::Distance(local,c.asked);
    if(!(pull>bsc::kSnapPull))return;
    const uint32_t at=base+cam::rva::smooth_flag;uint32_t flag=0;
    if(!m.Writable(at,sizeof(flag))||!Get(m,at,flag))return;
    flag|=1U;
    if(m.Write(at,&flag,sizeof(flag))){orbit.Snapped(pull);orbit_snaps.fetch_add(1);}
}
inline void FinishOrbit(const void* out){Memory m;FinishOrbitWith(m,out);}
inline bool PrepareOrbit(void* self,uint32_t caller,const Matrix* matrix,const Offset* src,Offset& out){
    Memory m;return PrepareOrbitWith(m,self,caller,matrix,src,out);
}
// After CameraMove: no hitCheck this time (another camera mode) ends the orbit at once; the closed records' lines.
inline void OrbitAfterFront(){
    if(!orbit_seen&&orbit.phase()!=bsc::Phase::Off){orbit.Abort(bsc::End::Camera);orbit_pose={};}
    OrbitLogClosed();
}
// SwordLifecycle end (game thread): ease back from here.
inline void OrbitLeaseEnd(uint64_t lease,bool finished){
    if(!orbit_live.load(std::memory_order_acquire)||game_thread.load()!=GetCurrentThreadId())return;
    orbit.Finish(lease,finished?bsc::End::Finished:bsc::End::Interrupted);
}
// The fall hold, inside CameraControl::Check (FallDetour, at 0x2F5990 with Check's own frame): ebp is Check's frame
// pointer, self its `this` (ebx). In a clean Back Slice lease d.y (ebp-0xC, PSVECSubtract's result, about to be
// compared with 500) is kept for the vertical follow, and with the shoulder camera at or under 500 it becomes 501.
// Only on the game thread inside our CameraMove (depth 1) with self = CamCtrl; any failed read leaves the test native.
template<class Mem> bool FallHoldWith(Mem& m,uint32_t ebp,uint32_t self){
    if(!orbit_live.load(std::memory_order_acquire)||depth!=1||game_thread.load()!=GetCurrentThreadId()||
       self!=base+rva::ctrl||!ebp)return false;
    bsc::Facts facts;if(!orbit_source||!orbit_source(facts)||!bsc::Engaged(facts))return false;
    const uint32_t at=ebp+static_cast<uint32_t>(bsc::rva::fall_dy);uint8_t r0=0;float dy=NAN;
    if(!Get(m,at,dy))return false;
    if(std::isfinite(dy)){orbit_hips=dy;orbit_hips_front=fronts.load();orbit_hips_known=true;}
    if(!Get(m,self+bsc::rva::r0,r0)||!bsc::HoldFall(true,r0,dy))return false;
    const float held=bsc::kFallHeld;
    if(!m.Write(at,&held,sizeof(held)))return false;
    orbit_holds.fetch_add(1);orbit.Held();return true;
}
inline void __cdecl FallCall(uint32_t ebp,uint32_t self){
    const DWORD error=GetLastError();Memory m;FallHoldWith(m,ebp,self);SetLastError(error);
}
// Entry inside Check at 0x2F5990 (the 9 bytes fld [ebp-0xC]; fcomp qword [500.0] go to the trampoline). Every
// register, the flags (DF included: cleared for the C call, popfd restores it) and the FPU/SSE state come back as they
// were; only the stack local d.y may change (tests/native/test_ztarget_transport.cpp FallAbi).
__declspec(naked) inline void FallDetour(){__asm{
    pushfd
    pushad
    cld
    mov eax,esp
    sub esp,528
    and esp,0fffffff0h
    mov [esp+512],eax
    fxsave [esp]
    fninit
    mov dword ptr [esp+516],01f80h
    ldmxcsr [esp+516]
    push dword ptr [eax+16]
    push dword ptr [eax+8]
    call FallCall
    add esp,8
    fxrstor [esp]
    mov esp,[esp+512]
    popad
    popfd
    jmp dword ptr [fall_original]
}}
// Lift or orbit only (Z-targeting not admitted): game_thread and depth, then their after-move steps; no Begin, view or HUD.
inline void __cdecl FrontDetour(){
    const bool zt=live.load(std::memory_order_acquire),lift=lift_live.load(std::memory_order_acquire),
        orb=orbit_live.load(std::memory_order_acquire);
    if(!zt&&!lift&&!orb){original_front();return;}
    if(depth){++depth;original_front();--depth;return;}
    // skills-fix-01: the camera's game thread, handed over once the bound one has exited (thread_binding.h).
    const DWORD thread=GetCurrentThreadId();
    if(!link_runtime::thread_binding::Serves(front_binding.Take())){original_front();return;}
    if(game_thread.load()!=thread)game_thread.store(thread);
    ++depth;fronts.fetch_add(1);hit_seen=false;lift_seen=false;orbit_seen=false;
    const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);
    if(zt)Begin();
    std::fesetenv(&env);SetLastError(error);
    original_front();
    const DWORD after=GetLastError();std::fegetenv(&env);
    if(zt){
        if(!hit_seen&&controller.Active()){Suspend();RecordState("no_shoulder_call");}
        CaptureView();hud::Publish();ArrowPublish();
    }
    if(lift)LiftAfterFront();
    if(orb)OrbitAfterFront();
    std::fesetenv(&env);SetLastError(after);--depth;
}
// Kept separate from the ABI entry so tests can inject a caller without forging a return address.
template<class Mem,class FocusFn> bool PrepareHitWith(Mem& m,FocusFn focused,void* self,uint32_t caller,const Matrix* matrix,Offset* offsets,Offset& local){
    if(!live.load(std::memory_order_acquire)||depth!=1||game_thread.load()!=GetCurrentThreadId()||
       reinterpret_cast<uint32_t>(self)!=base+rva::qfps||caller!=base+rva::hit_return)return false;
    hit_seen=true;hits.fetch_add(1);Frame f;Matrix mat;Offset in;
    // First failing gate, counted per reason for the stats line (live diagnosis, 16-oct). 5-8 are also evaluated and
    // counted on their own (stats2), so a stale frame (why 4) cannot hide them.
    unsigned why=0;const bool read=have_snapshot&&ReadFrame(m,base,f);
    const bool unsafe=read&&!CameraSafe(f),busy_now=Busy(),unfocused=!focused(),latch=focus_lost.load();
    if(unsafe)hit_unsafe.fetch_add(1);if(busy_now)hit_busy.fetch_add(1);
    if(unfocused)hit_unfocused.fetch_add(1);if(latch)hit_latch.fetch_add(1);
    // The frame counter one ahead of Begin's snapshot is admitted, as FaceWith admits it (the snapshot can be the
    // preceding frame): every gate below and every write still use this fresh read. No log has shown that skew (why4=0
    // and skew1=0 in every stats line on disk, 27-oct): the 20-oct bad session was why=5, the stats line's fifth slot
    // read then as why=4 (!Safe right after `zt suspend reason=prompt`, fixed on 24-oct by CameraSafe). Any other
    // difference is still why 4.
    const int32_t skew=read?int32_t(f.frame-snapshot.frame):0;
    if(!have_snapshot)why=1;else if(!read)why=2;else if(!SameScene(snapshot,f))why=3;
    else if(skew!=0&&skew!=1)why=4;else if(unsafe)why=5;else if(busy_now)why=6;else if(unfocused)why=7;
    else if(latch)why=8;
    else if(!m.Read(reinterpret_cast<uint32_t>(matrix),&mat,sizeof(mat))||!m.Read(reinterpret_cast<uint32_t>(offsets),&in,sizeof(in)))why=9;
    if(read&&skew){
        if(why!=3&&skew_log.Step(skew,GetTickCount()))
            RecordDiag({Diag::Skew,nullptr,snapshot.frame,f.frame,skew,uint8_t(why==0?1:0),controller.phase});
        if(why==0)hit_skew1.fetch_add(1);
    }else if(read)skew_log.Rearm();
    if(why){
        reject_why[why].fetch_add(1);rejected.fetch_add(1);
        // A stale snapshot/frame (1-4) only skips this hit's write, as FaceWith's frame gap does (a suspend there drops
        // the lock until Resume brings it back); unsafe, busy, unfocused or the focus latch still suspend, as does
        // why 9. Idle is left Idle: Begin re-checks safety itself, and a suspended Idle swallowed a press made on the
        // next frame.
        // A soft reason on the fresh read (not aim, fire, an event, health or damage) right after a safe Begin skips this
        // write only (28-oct, two Ganados: Begin safe, this read not, every frame; each suspend was resumed on the next
        // Begin, 1593 `zt locked reason=resume` and the lock flickering). Begin re-checks the next frame itself.
        Frame g=f;g.action=0;const Unsafe hit_why=unsafe?WhyUnsafe(g):Unsafe::None;   // CameraSafe's reason
        const bool soft=why==5&&input.safe&&!busy_now&&!unfocused&&!latch&&hit_why!=Unsafe::None&&
            !HardRelease(hit_why)&&hit_why!=Unsafe::Damage;
        const bool suspend=!soft&&(why>=5||unsafe||busy_now||unfocused||latch)&&controller.phase!=Phase::Idle;
        if(unsafe&&controller.phase!=Phase::Idle&&hit_unsafe_log.Step(UnsafeName(hit_why),GetTickCount()))
            RecordDiag({Diag::HitUnsafe,UnsafeName(hit_why),f.frame,0,0,uint8_t(suspend?1:0),controller.phase});
        if(suspend)Suspend();
        return false;
    }
    hit_unsafe_log.Step(nullptr,0);
    float desired=f.yaw;
    if(controller.phase==Phase::Locked){
        Vec target;
        if(!Admitted()||!pool.Revalidate(m,base,locked,f.pos,target)||!mt::FaceYaw(f.pos,target,desired)){
            locked={};controller.phase=Phase::Recenter;desired=f.yaw;if(lock_sound)voice::Raise(voice::Unlock);
            RecordState("target_lost");
        }
    }
    input.safe=true;input.dt=f.dt;
    if(!Adjust(controller,input,mat,in,desired,smooth_ms,local)){
        if(controller.phase==Phase::Suspended){Suspend();rejected.fetch_add(1);}return false;
    }
    return !observe;
}
inline bool PrepareHit(void* self,uint32_t caller,const Matrix* matrix,Offset* offsets,Offset& local){
    Memory m;return PrepareHitWith(m,&Focus,self,caller,matrix,offsets,local);
}
// Z-target stage, then the lift stage on whatever Z-target would pass, then the Back Slice orbit on whatever those
// pass; the caller's offsets are never written. The lift's and the orbit's after-hitCheck steps keep hitCheck's own
// LastError and FPU environment.
inline void __fastcall HitDetour(void* self,void*,const Matrix* matrix,Offset* offsets,void* out){
    const uint32_t caller=reinterpret_cast<uint32_t>(_ReturnAddress());
    const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);Offset local,lifted,turned;
    const bool changed=PrepareHit(self,caller,matrix,offsets,local);
    const bool up=PrepareLift(self,caller,matrix,changed?&local:offsets,lifted);
    Offset* const now=up?&lifted:changed?&local:offsets;
    const bool orbited=PrepareOrbit(self,caller,matrix,now,turned);
    std::fesetenv(&env);SetLastError(error);
    if(changed)writes.fetch_add(1);
    original_hit(self,matrix,orbited?&turned:now,out);
    if(lift_call.engaged||orbit_call.engaged){
        const DWORD after=GetLastError();fenv_t native;std::fegetenv(&native);
        if(lift_call.engaged)FinishLift(out);
        if(orbit_call.engaged)FinishOrbit(out);
        std::fesetenv(&native);SetLastError(after);
    }
}
inline bool Switch(const wchar_t* ini,const wchar_t* section,const wchar_t* name){
    wchar_t value[32]{};GetPrivateProfileStringW(section,name,L"",value,32,ini);return ExactOne(value);
}
inline std::string ReadFileText(const wchar_t* name){
    FILE* f=nullptr;std::string text;
    if(!_wfopen_s(&f,name,L"rb")&&f){char part[4096];size_t n;
        while((n=std::fread(part,1,sizeof(part),f))&&text.size()<1024*1024)text.append(part,n);std::fclose(f);}
    return text;
}
inline bool KeyboardSafe(const wchar_t* ini){
    {wchar_t kv[32]{};GetPrivateProfileStringW(L"ZTarget",L"Key",L"",kv,32,ini);const int vk=ParseLockKey(kv);
        if(!*kv)strcpy_s(lock_name,"LCONTROL");
        else{size_t i=0;for(;i+1<sizeof(lock_name)&&kv[i];++i)lock_name[i]=kv[i]>0x20&&kv[i]<0x7F?char(kv[i]):'?';lock_name[i]=0;}
        if(vk<0){if(logger)logger("ZTarget Key invalid or refused (Alt, Escape, F-keys, left/right mouse)");return false;}
        lock_vk=vk;const char* t=vk==0x04||vk==0x05||vk==0x06?"":link_runtime::mortal_draw::prompt::InputToken(vk);
        strcpy_s(lock_token,t&&*t?t:"");
        if(!*lock_token&&vk!=0x04&&vk!=0x05&&vk!=0x06){if(logger)logger("ZTarget Key has no input.ini token; refused");return false;}}
    wchar_t path[MAX_PATH]{};const DWORD path_size=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!path_size||path_size>=MAX_PATH)return false;
    wchar_t* slash=std::wcsrchr(path,L'\\');if(!slash)return false;*slash=0;
    slash=std::wcsrchr(path,L'\\');if(!slash)return false;
    if(paths::Copy(slash+1,MAX_PATH-size_t(slash+1-path),L"BIO4\\input.ini"))return false;
    const std::string native=ReadFileText(path);
    if(native.find("###GAMEPLAY")==std::string::npos||native.find("###KEYBOARD")==std::string::npos)return false;
    if(Switch(ini,L"ZTarget",L"Pad")){
        // R3 keeps the 16-oct rule (refused when a gameplay profile binds GC_B15). LT/LB/L3 are Donald's explicit
        // choice (18-oct): allowed, and every native action on that button is named in the log (it still fires).
        wchar_t hv[16]{};GetPrivateProfileStringW(L"ZTarget",L"ZTargetPadButton",L"",hv,16,ini);
        if(!pad::ParseHold(hv,pad_hold)){pad_hold=pad::HoldButton::R3;if(logger)logger("ZTarget ZTargetPadButton invalid; R3 kept");}
        GetPrivateProfileStringW(L"ZTarget",L"ZTargetPadMode",L"",hv,16,ini);
        if(!pad::ParseMode(hv,pad_mode)){pad_mode=pad::PadMode::Toggle;if(logger)logger("ZTarget ZTargetPadMode invalid; toggle kept");}
        // RE4's own buttons per XInput profile (Type I = A, Type II = B, Type III = C; the active one is not stored in
        // a file this DLL can read): the Shield Bash / Back Slice tap is the profile's knife button (KEY_KAMAE_KNIFE).
        if(logger)logger("ZTarget pad mode=%s lock=%s knife[%s] aim[%s] fire[%s] (Type II = B: LT aim, RT fire, LB knife)",
            pad::PadModeName(pad_mode),pad::HoldName(pad_hold),pad::ProfileButtons(native,"KEY_KAMAE_KNIFE").c_str(),
            pad::ProfileButtons(native,"KEY_KAMAE").c_str(),pad::ProfileButtons(native,"KEY_FIRE").c_str());
        if(pad_hold==pad::HoldButton::R3){
            if(!pad::BindingsSafe(native)){pad_enabled=false;
                if(logger)logger("ZTarget pad disabled: input_conflict_or_unreadable RS/GC_B15; keyboard remains available");}
        }else{
            const pad::Uses u=pad::BindingUses(native,pad::HoldToken(pad_hold));
            if(!u.readable){pad_enabled=false;if(logger)logger("ZTarget pad disabled: XInput profiles A/B/C unreadable");}
            else if(u.profiles&&logger)logger("ZTarget pad hold=%s CONFLICT: RE4 also binds %s to %s; that native action still fires while locking",
                pad::HoldName(pad_hold),pad::HoldToken(pad_hold),u.actions.c_str());
        }
    }
    namespace prompt=link_runtime::mortal_draw::prompt;
    if((*lock_token&&prompt::GameplayKeyboardBinds(native.c_str(),lock_token))||prompt::GameplayKeyboardBinds(native.c_str(),"V"))return false;
    if(lock_vk==0x04&&prompt::GameplayKeyboardBinds(native.c_str(),"MB_MIDDLE"))return false;
    // All mod keyboard bindings, even disabled ones: conservative and deterministic.
    const wchar_t* keys[]={L"KeyToggle",L"KeyStow",L"KeyNoteA",L"KeyUp",L"KeyDown",L"KeyLeft",L"KeyRight"};
    wchar_t v[64]{};
    // A mod key clashes when it names V or the lock key (parsed the same way as [ZTarget] Key).
    auto clash=[](const wchar_t* w){if(!w||!*w)return false;if(!_wcsicmp(w,L"V")||!_wcsicmp(w,L"0x56"))return true;
        const int vk=ParseLockKey(w);return vk>0&&vk==lock_vk;};
    for(auto key:keys){GetPrivateProfileStringW(L"Ocarina",key,L"",v,64,ini);if(clash(v))return false;}
    GetPrivateProfileStringW(L"LinkRuntime",L"MortalDrawKey",L"E",v,64,ini);
    if(clash(v))return false;
    // re4_fps hotkey/modifier settings; missing file is normal without re4_fps.
    if(paths::Copy(path,ini))return false;slash=std::wcsrchr(path,L'\\');if(!slash)return false;
    if(paths::Copy(slash+1,MAX_PATH-size_t(slash+1-path),L"winmm.ini"))return false;
    std::string tweaks=ReadFileText(path);for(char& c:tweaks)if(c>='a'&&c<='z')c=char(c-32);
    size_t pos=0;bool hotkeys=false;
    while(pos<tweaks.size()){
        const size_t end=tweaks.find('\n',pos);std::string line=tweaks.substr(pos,end==std::string::npos?end:end-pos);
        pos=end==std::string::npos?tweaks.size():end+1;
        // Every key of [HOTKEYS] (re4_tweaks names most without KEY: ConfigMenu, Console, DebugMenu, FlipItem*,
        // JetSkiTricks); elsewhere only names with KEY or MODIFIER, as before.
        if(!line.empty()&&line[0]=='['){hotkeys=line.compare(0,9,"[HOTKEYS]")==0;continue;}
        const size_t eq=line.find('=');if(eq==std::string::npos||line.empty()||line[0]==';'||line[0]=='#')continue;
        const std::string key=line.substr(0,eq);
        if(!hotkeys&&key.find("KEY")==std::string::npos&&key.find("MODIFIER")==std::string::npos)continue;
        std::string value=line.substr(eq+1);const size_t comment=value.find_first_of(";#");if(comment!=std::string::npos)value.resize(comment);
        // A combo (CTRL+F3) only uses Ctrl as a modifier: holding the lock key alone never fires it, so a
        // Ctrl lock key clashes only with a bare Ctrl binding. A letter keeps the old any-token rule.
        const bool combo=value.find('+')!=std::string::npos;
        for(char& c:value)if(c=='+'||c==','||c=='|'||c=='\r'||c=='\t')c=' ';
        char letter[8]{},code[8]{},dec[8]{},vkname[8]{};
        if((lock_vk>='A'&&lock_vk<='Z')||(lock_vk>='0'&&lock_vk<='9')){letter[0]=char(lock_vk);sprintf_s(code,"0X%02X",lock_vk);
            sprintf_s(dec,"%d",lock_vk);sprintf_s(vkname,"VK_%c",char(lock_vk));}
        const bool ctrl=lock_vk==0xA2||lock_vk==0xA3;
        size_t a=0;while(a<value.size()){
            while(a<value.size()&&value[a]==' ')++a;size_t b=value.find(' ',a);const std::string token=value.substr(a,b==std::string::npos?b:b-a);
            if(token=="V"||token=="86"||token=="0X56"||token=="VK_V")return false;
            if(*letter&&(token==letter||token==code||token==dec||token==vkname))return false;
            if(ctrl&&!combo&&(token=="CTRL"||token=="CONTROL"||token==lock_token||token==(lock_vk==0xA2?"LCTRL":"RCTRL")))return false;
            a=b==std::string::npos?value.size():b+1;
        }
    }
    return true;
}
// The lift's ready line (only with EndingBlow=1). apex_lift: the camera's lift at the apex (game frame 25.6).
inline void LiftReady(Logger log,bool ready,const char* why,bool zt,unsigned hooks){
    if(!lift_request.requested||!log)return;
    log("EndingBlowCameraLift percent=%u ready=%u reason=%s observe=%u apex_lift=%.0f land=%.0f ztarget=%u hooks=%u",
        lift_request.percent,ready?1U:0U,why,lift_request.observe?1U:0U,double(cam::Lift(25.6,lift_request.percent)),
        cam::kLand60,zt?1U:0U,hooks);
}
// The Back Slice camera's ready line (only with the Back Slice on). reason: ok, off, no_clock, no_source, hit_pins,
// hit_thunk_unknown, smooth_pins, check_pins, minhook, front_create, hit_create, enable, fall_create or fall_enable.
// follow: the vertical follow is on (BackSliceCameraLift > 0); wall_probe: the EatMgr probe's pins passed (los::kPins);
// hooks: front, hitCheck and, when ready, the fall hold.
inline void OrbitReady(Logger log,bool ready,const char* why,bool zt,unsigned hooks){
    if(!orbit_request.requested||!log)return;
    log("BackSliceCamera ready=%u reason=%s ms=%u lift=%u fall_hold=%u follow=%u wall_probe=%u ztarget=%u hooks=%u",ready?1U:0U,why,
        orbit_request.config.ms,orbit_request.config.lift,ready?1U:0U,ready&&orbit_request.config.lift?1U:0U,
        ready&&orbit_probe_on?1U:0U,zt?1U:0U,hooks);
}
// Z-targeting, the Ending Blow camera lift and the Back Slice camera share the front and hitCheck hooks; each is
// admitted on its own (challenge 3). The Back Slice camera adds the fall hold's hook inside CameraControl::Check.
// Without the lift and the Back Slice camera every Z-target line and early return is as before (no image read when
// ZTarget is off or its key is refused). True when the hooks are installed for any of them.
inline bool Start(uint32_t image,const wchar_t* ini,bool(*busy_fn)(),Logger log,bool dispatch_ready=false){
    const bool lift_wanted=lift_request.requested&&lift_request.percent&&!lift_request.refused&&lift_source;
    const char* lift_why=lift_request.refused?lift_request.refused:!lift_source?"no_source":"ok";
    const bool orbit_wanted=orbit_request.requested&&!orbit_request.refused&&orbit_source;
    const char* orbit_why=orbit_request.refused?orbit_request.refused:!orbit_source?"no_source":"ok";
    bool zt=Switch(ini,L"LinkRuntime",L"ZTarget");
    if(!zt)log(lift_wanted&&orbit_wanted?"ZTarget=0; no Z-target camera hooks (the Ending Blow camera lift and the Back Slice camera may still hook)":
        lift_wanted?"ZTarget=0; no Z-target camera hooks (the Ending Blow camera lift may still hook)":
        orbit_wanted?"ZTarget=0; no Z-target camera hooks (the Back Slice camera may still hook)":"ZTarget=0; no camera hooks installed");
    else{
        logger=log;pad_enabled=Switch(ini,L"ZTarget",L"Pad");
        if(!KeyboardSafe(ini)){log("ZTarget ready=0 input_conflict_or_unreadable key=%s/V; hooks=0",lock_name);zt=false;}
        else log("ZTarget key vk=0x%02X token=%s (hold to lock; V switches)",lock_vk,*lock_token?lock_token:"mouse");
    }
    if(!zt&&!lift_wanted&&!orbit_wanted){LiftReady(log,false,lift_why,false,0);OrbitReady(log,false,orbit_why,false,0);return false;}
    Memory m;const bool pins=native_pins::All(m,image,kPins);const Thunk thunk=pins?ReadThunk(m,image):Thunk::Bad;
    if(zt&&!pins){log("ZTarget ready=0 pins_failed; hooks=0");zt=false;}
    else if(zt&&thunk==Thunk::Bad){log("ZTarget ready=0 hit_thunk_unknown; hooks=0");zt=false;}
    const bool smooth_pins=(lift_wanted||orbit_wanted)&&native_pins::All(m,image,cam::kSmoothPins);
    bool lift=lift_wanted;
    if(lift&&!pins){lift=false;lift_why="hit_pins";}
    else if(lift&&thunk==Thunk::Bad){lift=false;lift_why="hit_thunk_unknown";}
    else if(lift&&!smooth_pins){lift=false;lift_why="smooth_pins";}
    bool orbit_on=orbit_wanted;
    if(orbit_on&&!pins){orbit_on=false;orbit_why="hit_pins";}
    else if(orbit_on&&thunk==Thunk::Bad){orbit_on=false;orbit_why="hit_thunk_unknown";}
    else if(orbit_on&&!smooth_pins){orbit_on=false;orbit_why="smooth_pins";}
    else if(orbit_on&&!native_pins::All(m,image,bsc::kCheckPins)){orbit_on=false;orbit_why="check_pins";}
    if(!zt&&!lift&&!orbit_on){LiftReady(log,false,lift_why,false,0);OrbitReady(log,false,orbit_why,false,0);return false;}
    base=image;busy=busy_fn;logger=log;
    if(zt){
        observe=Switch(ini,L"ZTarget",L"Observe");waiver=Switch(ini,L"ZTarget",L"DebugLineWaiver");
        const bool face_requested=Switch(ini,L"ZTarget",L"FaceTarget");face_target=face_requested&&dispatch_ready;
        if(face_requested&&!dispatch_ready)log("ZTarget FaceTarget disabled: qualified player dispatch unavailable; camera remains available");
        const unsigned tau=GetPrivateProfileIntW(L"ZTarget",L"SmoothMs",160,ini);smooth_ms=tau>=50&&tau<=1000?float(tau):160.f;
        // LOS: on unless exactly 0. Without its pins no lock is admitted (unless DebugLineWaiver=1).
        wchar_t lv[8]{};GetPrivateProfileStringW(L"ZTarget",L"LOS",L"1",lv,8,ini);const bool los_wanted=!(lv[0]==L'0'&&!lv[1]);
        los_segment=&NativeSegment;los_ready=los_wanted&&native_pins::All(m,image,los::kPins);
        if(los_wanted&&!los_ready)log("ZTarget LOS disabled: hitCheck pins failed (EatMgr 0x%X); locks need DebugLineWaiver=1",image+los::rva::eat_mgr);
        notice_on=Switch(ini,L"ZTarget",L"Notice");lock_sound=Switch(ini,L"ZTarget",L"LockSound");
        if(lock_sound){
            wchar_t dir[MAX_PATH]{};const bool path_ok=paths::Copy(dir,ini)==0;
            wchar_t* slash=path_ok?std::wcsrchr(dir,L'\\'):nullptr;if(slash)slash[1]=0;
            const unsigned volume=GetPrivateProfileIntW(L"ZTarget",L"LockSoundVolumePercent",70,ini);
            const unsigned loaded=slash?voices.Load(dir,volume<=100?volume:70):0;
            log("ZTarget lock sounds loaded=%u/2 volume=%u (link_ztarget_lock.wav / link_ztarget_unlock.wav)",loaded,volume<=100?volume:70);
            if(!loaded)lock_sound=false;
        }
    }
    // A failure before any hook is enabled: both ready lines with this reason (or their own refusal).
    auto fail=[&](const char* why){
        LiftReady(log,false,lift?why:lift_why,zt,0);OrbitReady(log,false,orbit_on?why:orbit_why,zt,0);return false;};
    MH_STATUS status=MH_Initialize();
    if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return fail("minhook");
    void* ft=reinterpret_cast<void*>(base+rva::front);void* ht=reinterpret_cast<void*>(base+kHitThunk);
    status=MH_CreateHook(ft,reinterpret_cast<void*>(&FrontDetour),reinterpret_cast<void**>(&original_front));
    if(status!=MH_OK){if(zt)log("ZTarget ready=0 front_create=%s",MH_StatusToString(status));return fail("front_create");}
    status=MH_CreateHook(ht,reinterpret_cast<void*>(&HitDetour),reinterpret_cast<void**>(&original_hit));
    if(status!=MH_OK){MH_RemoveHook(ft);if(zt)log("ZTarget ready=0 hit_create=%s",MH_StatusToString(status));return fail("hit_create");}
    // Hooks enabled while admission is false. Publish only after both are installed.
    status=MH_EnableHook(ht);if(status==MH_OK)status=MH_EnableHook(ft);
    if(status!=MH_OK){MH_DisableHook(ht);MH_DisableHook(ft);if(zt)log("ZTarget ready=0 enable=%s; admission=0",MH_StatusToString(status));
        return fail("enable");}
    unsigned hooks=2;
    if(orbit_on){   // the fall hold: Check's own 9 bytes at 0x2F5990 run from the trampoline
        void* site=reinterpret_cast<void*>(base+bsc::rva::fall_site);
        status=MH_CreateHook(site,reinterpret_cast<void*>(&FallDetour),&fall_original);
        if(status!=MH_OK){orbit_on=false;orbit_why="fall_create";}
        else if(MH_EnableHook(site)!=MH_OK){MH_RemoveHook(site);orbit_on=false;orbit_why="fall_enable";}
        else ++hooks;
    }
    if(lift){
        lift_percent=lift_request.percent;lift_observe=lift_request.observe;lift_tracker=cam::Tracker{};lift_smooth=cam::Smooth{};
        lift_live.store(true,std::memory_order_release);
    }
    if(orbit_on){
        orbit_config=orbit_request.config;orbit=bsc::Orbit{};orbit_pose={};orbit_probe_t=orbit_probe_b=1.f;
        orbit_segment=&NativeSegment;orbit_probe_on=native_pins::All(m,image,los::kPins);
        orbit_live.store(true,std::memory_order_release);
    }
    LiftReady(log,lift,lift?"ok":lift_why,zt,2);   // front and hitCheck
    OrbitReady(log,orbit_on,orbit_on?"ok":orbit_why,zt,hooks);   // plus the fall hold when it is on
    if(!zt)return true;
    // The 3D arrow (Reticle3D=1, its drawer started): on with the camera, then the 2D cursor is installed muted.
    if(arrow_request.requested&&!arrow_request.refused){
        if(observe)arrow_request.refused="ZTarget Observe=1";
        else arrow_on=arrow3d::runtime::ready.load(std::memory_order_acquire);
    }
    const bool reticle=Switch(ini,L"ZTarget",L"Reticle")&&!observe&&hud::Start(base,ini,arrow_on?&HudMuted:&HudFrame,&game_thread,log);
    const bool strafe_requested=Switch(ini,L"ZTarget",L"Strafe");
    {   // [ZTarget] StrafeSpeed (percent 100..220, default 160; ztarget_strafe.h)
        wchar_t v[16]{};GetPrivateProfileStringW(L"ZTarget",L"StrafeSpeed",L"",v,16,ini);
        unsigned n=0;bool digits=v[0]!=0;for(const wchar_t* q=v;*q&&digits;++q){if(*q<L'0'||*q>L'9'||n>10000)digits=false;else n=n*10+unsigned(*q-L'0');}
        strafe_transport.controller.config.speed=strafe::StrafeSpeed(n,digits);
        if(v[0]&&(!digits||n<100||n>220))log("ZTarget StrafeSpeed invalid (100..220); using 160");
    }
    const bool strafe_on=strafe_requested&&!observe&&face_target&&dispatch_ready&&strafe::native::created&&
        strafe::native::EnableGate()==MH_OK;
    if(strafe_requested)log("ZTarget strafe ready=%u normal_walk=1 pre_core=1 native_collision=1 local_keys=1 requires_face_target=1 speed=%.2f neutral=1",
        strafe_on?1U:0U,double(strafe_transport.controller.config.speed));
    live.store(true,std::memory_order_release);
    log("ZTarget line_of_sight=%u (EatMgr hitCheck, mask 0x4000, every %u frames, drop after %.1f s) notice=%u lock_sound=%u",
        los_ready?1U:0U,los::kEvery,double(los::kDrop),notice_on?1U:0U,lock_sound?1U:0U);
    log("ZTarget pins_ok ready=1 observe=%u debug_line_waiver=%u smooth_ms=%.0f keys=%s/V hooks=2 hit_thunk=%s face_target=%u face_rate_rad_s=8 reticle=%u strafe=%u pad=%u pad_hold=%s pad_mode=%s pad_switch=RX stale_ms=100",observe?1U:0U,waiver?1U:0U,smooth_ms,lock_name,thunk==Thunk::Native?"native":"foreign",face_target?1U:0U,reticle?1U:0U,strafe_on?1U:0U,pad_enabled?1U:0U,pad::HoldName(pad_hold),pad::PadModeName(pad_mode));
    return true;
}
inline void Poll(){
    const bool zt=live.load(std::memory_order_acquire),lift=lift_live.load(std::memory_order_acquire),
        orb=orbit_live.load(std::memory_order_acquire);
    if(!zt&&!lift&&!orb)return;
    if(zt){
        if(!Focus())focus_lost.store(true,std::memory_order_release);
        if(lock_sound)voices.Poll();
        unsigned r=read_index.load(std::memory_order_relaxed);const unsigned w=write_index.load(std::memory_order_acquire);
        while(r!=w){const Record rec=records[r%64];logger("zt %s frame=%u target=0x%X identity=%llu reason=%s observe=%u",Name(rec.phase),rec.frame,rec.actor,rec.identity,rec.reason,observe?1U:0U);
            read_index.store(++r,std::memory_order_release);}
        unsigned nr=neutral_read.load(std::memory_order_relaxed);const unsigned nw=neutral_write.load(std::memory_order_acquire);
        while(nr!=nw){const NeutralRecord rec=neutral_records[nr%16];
            logger("zt neutral %s frame=%u yaw=%.3f reason=%s",NeutralName(rec.event),rec.frame,rec.yaw,rec.reason?rec.reason:"-");
            neutral_read.store(++nr,std::memory_order_release);}
        unsigned dr=diag_read.load(std::memory_order_relaxed);const unsigned dw=diag_write.load(std::memory_order_acquire);
        while(dr!=dw){LogDiag(logger,diag_records[dr%16]);diag_read.store(++dr,std::memory_order_release);}
    }
    static ULONGLONG next=0;const auto now=GetTickCount64();
    if(now>=next){next=now+5000;const Logger stats=stats_logger?stats_logger:logger;
        if(zt){stats("ZTarget stats fronts=%u hits=%u writes=%u rejected=%u dropped=%u why=%u/%u/%u/%u/%u/%u/%u/%u/%u face_writes=%u face_rejected=%u face_faults=%u face_skipped=%u lift_writes=%u lift_dry=%u lift_rejects=%u",fronts.load(),hits.load(),writes.load(),rejected.load(),dropped.load(),
                reject_why[1].load(),reject_why[2].load(),reject_why[3].load(),reject_why[4].load(),reject_why[5].load(),reject_why[6].load(),reject_why[7].load(),reject_why[8].load(),reject_why[9].load(),face_writes.load(),face_rejected.load(),face_faults.load(),face_skipped.load(),
                lift_writes.load(),lift_dry.load(),lift_rejects.load());
            stats("ZTarget stats2 unsafe=%u busy=%u unfocused=%u latch=%u skew1=%u",hit_unsafe.load(),hit_busy.load(),
                hit_unfocused.load(),hit_latch.load(),hit_skew1.load());
            stats("ZTarget los calls=%u clear=%u blocked=%u faults=%u refused=%u lost=%u skipped=%u sounds=%u",los::calls.load(),
                los::clear.load(),los::blocked.load(),los::faults.load(),los_refused.load(),los_lost.load(),los_skipped.load(),voices.played.load());
            hud::Poll(stats);
            if(strafe_transport.controller.config.enabled)stats("ZTarget strafe poses=%u entries=%u releases=%u faults=%u",strafe_poses.load(),strafe_entries.load(),strafe_releases.load(),strafe_faults.load());}
        if(lift)stats("EndingBlowCameraLift stats fronts=%u hits=%u writes=%u dry=%u rejects=%u state=%u orphans=%u observe=%u",fronts.load(),lift_hits.load(),
            lift_writes.load(),lift_dry.load(),lift_rejects.load(),lift_state.load(),lift_orphans.load(),lift_observe?1U:0U);
        if(orb)stats("BackSliceCamera stats fronts=%u hits=%u writes=%u carries=%u holds=%u snaps=%u probes=%u probe_pulls=%u "
            "probe_faults=%u rejects=%u",fronts.load(),orbit_hits.load(),orbit_writes.load(),orbit_carries.load(),orbit_holds.load(),
            orbit_snaps.load(),orbit_probes.load(),orbit_probe_pulls.load(),orbit_probe_faults.load(),orbit_rejects.load());}
}
}

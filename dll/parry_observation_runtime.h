#pragma once
#include "parry_owner_runtime.h"
#include "parry_input_runtime.h"
#include "parry_sword_epoch.h"
#include "parry_contact_diagnostic.h"
#include <type_traits>

// Copied diagnostics only. No input consumption, damage write or native reaction.
namespace link_runtime::parry::observation {
namespace epoch=sword::epoch;
enum class Kind:uint32_t {Tick,Candidate,InputEdge,AttackSample};
enum Reason:uint32_t {
    EpochOff=1U<<0,EpochThread=1U<<1,EpochOpen=1U<<2,EpochPoisoned=1U<<3,EpochNoPose=1U<<4,EpochCurrentRejected=1U<<5,
    InputOff=1U<<6,InputThread=1U<<7,InputUnreadable=1U<<8,InputNotGameplay=1U<<9,InputContext=1U<<10,
    WindowUnknown=1U<<11,WindowNotArmed=1U<<12,ClockBackward=1U<<13,OwnerRejected=1U<<14,OwnerRoute=1U<<15,
    OwnerEpoch=1U<<16,Reentered=1U<<17,ReadException=1U<<18,EpochLifecycle=1U<<19
};
enum Flag:uint32_t {EpochCurrent=1U<<0,InputFresh=1U<<1,InputAligned=1U<<2,WindowArmed=1U<<3,OwnerRead=1U<<4,OwnerFirstRoute=1U<<5,OwnerAligned=1U<<6,InputSampleArmed=1U<<7,OwnerSukiRoute=1U<<8};
struct Record {
    Kind kind=Kind::Tick;uint32_t reasons=0,flags=0,thread=0;
    uint32_t frame=0,input_frame=0,owner_frame=0,profile=0,epoch_depth=0,completed_frame=0;
    uint32_t epoch_ready=0,epoch_poisoned=0,epoch_sampling=0,epoch_failure=0,epoch_reader_failure=0;
    uint64_t epoch_serial=0,on=0,trg=0,window_age_ms=0;
    uint32_t window_remaining_ms=0,window_duration_ms=0,window_down=0,window_released=0;
    uint32_t room=0,weapon=0,character=0,producer_return_rva=0,owner_weapon_type=0;
    uint32_t tail_samples=0,candidate_samples=0;
    double blade_base[3]{},blade_tip[3]{};
    float attack_current[3]{},attack_previous[3]{};
    unsigned char owner_routines[4]{};
    bool contact_valid=false;contact_diagnostic::Result contact{};
};
static_assert(std::is_trivially_copyable<Record>::value&&sizeof(Record)<=320,"worker records are bounded values without actor pointers");
struct Counters {uint32_t tails=0,candidates=0,epoch_ready_tails=0,published=0,dropped_full=0,dropped_busy=0,reentered=0;};
inline volatile LONG tail_count=0,candidate_count=0,ready_count=0,published_count=0,full_count=0,busy_count=0,reentered_count=0;
inline uint32_t Count(volatile LONG& n){return static_cast<uint32_t>(InterlockedCompareExchange(&n,0,0));}
inline Counters SnapshotCounters(){return {Count(tail_count),Count(candidate_count),Count(ready_count),Count(published_count),Count(full_count),Count(busy_count),Count(reentered_count)};}
inline constexpr unsigned kCapacity=32;
inline SRWLOCK queue_lock=SRWLOCK_INIT;
inline Record queue[kCapacity]{};
inline unsigned head=0,used=0;
inline volatile LONG active=0,generation=0;
inline bool started=false;
inline shield::NativeAccess access;
inline owner::Code owner_code;
inline front_native::Code front_code;
inline uint32_t tick_stride=60;
inline bool Enabled(){return InterlockedCompareExchange(&active,0,0)==1;}
inline bool Publish(const Record& record,LONG version){
    if(!Enabled()||InterlockedCompareExchange(&generation,0,0)!=version)return false;
    if(!TryAcquireSRWLockExclusive(&queue_lock)){InterlockedIncrement(&busy_count);return false;}
    bool ok=false;
    if(Enabled()&&InterlockedCompareExchange(&generation,0,0)==version){
        if(used<kCapacity){queue[(head+used)%kCapacity]=record;++used;InterlockedIncrement(&published_count);ok=true;}
        else InterlockedIncrement(&full_count);
    }
    ReleaseSRWLockExclusive(&queue_lock);return ok;
}
// Worker copies one record. It never reads game pointers, native TLS or state.
inline bool Pop(Record& out){
    out={};if(!TryAcquireSRWLockExclusive(&queue_lock))return false;
    const bool ok=used!=0;if(ok){out=queue[head];head=(head+1)%kCapacity;--used;}
    ReleaseSRWLockExclusive(&queue_lock);return ok;
}
namespace detail {
struct EpochSample {bool enabled=false,thread=false,lifecycle=false,current=false;epoch::State state{};uint32_t completed_frame=0;};
struct InputSample {bool enabled=false,thread=false,read=false;input::Sample fresh{};input::State state{};};
struct NativeSources {
    uint32_t Thread(){return GetCurrentThreadId();}
    uint64_t Now(){return GetTickCount64();}
    EpochSample Epoch(Kind kind){
        EpochSample e;e.enabled=epoch::Enabled();e.thread=Count(epoch::owner_thread)==Thread();
        if(!e.enabled||!e.thread)return e;
        e.lifecycle=epoch::seen_lifecycle==Count(epoch::lifecycle);
        e.state=epoch::state;
        e.completed_frame=e.state.stamp.frame;
        if(e.lifecycle&&e.state.ready&&!e.state.depth&&!e.state.sampling&&!e.state.poisoned){
            sword::reader::Observation current;epoch::Stamp sampled{};
            e.current=(kind==Kind::Candidate||kind==Kind::AttackSample)?epoch::AtContact(current,sampled):epoch::Current(current);
            e.state.failure=epoch::state.failure;e.state.reader_failure=epoch::state.reader_failure;
            if(e.current){e.state.pose=current;if((kind==Kind::Candidate||kind==Kind::AttackSample))e.state.stamp=sampled;}
        }
        return e;
    }
    InputSample InputState(){
        InputSample p;p.enabled=input::Enabled();p.thread=Count(input::owner_thread)==Thread();
        if(p.enabled&&p.thread&&input::seen_epoch==Count(input::epoch)&&!input::in_tick)p.state=input::state;
        return p;
    }
    InputSample Input(){
        auto p=InputState();if(!p.enabled||!p.thread)return p;
        input::Memory memory;p.read=input::Read(memory,access,p.fresh);return p;
    }
    bool Owner(const Candidate& candidate,owner::Observation& out){return candidate.body_part?owner::ObserveCurrent(access,owner_code,candidate,out):owner::ObserveSampleCurrent(access,owner_code,candidate,out);}
    bool Contact(const Candidate& candidate,contact_diagnostic::Result& out){return contact_diagnostic::ObserveCurrent(access,owner_code,candidate,out,front_code,!candidate.body_part);}
};
// Template dependency seam for offline fixtures; production callbacks construct
// NativeSources internally and accept no caller-ready/epoch-valid flags.
template<class Sources> Record Build(Sources& source,Kind kind,const Candidate* candidate){
    Record r;r.kind=kind;r.thread=source.Thread();const auto e=source.Epoch(kind);const auto p=source.Input();const auto now=source.Now();
    r.frame=e.state.stamp.frame;r.epoch_serial=e.state.serial;r.epoch_depth=e.state.depth;
    r.completed_frame=e.completed_frame;
    r.epoch_ready=e.state.ready;r.epoch_poisoned=e.state.poisoned;r.epoch_sampling=e.state.sampling;
    r.epoch_failure=static_cast<uint32_t>(e.state.failure);r.epoch_reader_failure=static_cast<uint32_t>(e.state.reader_failure);
    if(!e.enabled)r.reasons|=EpochOff;if(!e.thread)r.reasons|=EpochThread;if(!e.lifecycle)r.reasons|=EpochLifecycle;
    if(e.state.depth||e.state.sampling)r.reasons|=EpochOpen;if(e.state.poisoned)r.reasons|=EpochPoisoned;
    if(!e.state.ready)r.reasons|=EpochNoPose;else if(!e.current)r.reasons|=EpochCurrentRejected;
    const bool epoch_ok=e.enabled&&e.thread&&e.lifecycle&&e.current&&e.state.ready&&!e.state.depth&&!e.state.sampling&&!e.state.poisoned;
    const bool input_ok=p.enabled&&p.thread&&p.read&&p.fresh.valid;
    if(epoch_ok){
        r.flags|=EpochCurrent;r.profile=static_cast<uint32_t>(e.state.pose.profile);
        const auto& b=e.state.pose.blade;r.blade_base[0]=b.base.x;r.blade_base[1]=b.base.y;r.blade_base[2]=b.base.z;
        r.blade_tip[0]=b.tip.x;r.blade_tip[1]=b.tip.y;r.blade_tip[2]=b.tip.z;
    }
    if(!p.enabled)r.reasons|=InputOff;if(!p.thread)r.reasons|=InputThread;if(!p.read||!p.fresh.valid)r.reasons|=InputUnreadable;
    if(input_ok){
        r.flags|=InputFresh;r.input_frame=p.fresh.frame;r.on=p.fresh.on;r.trg=p.fresh.trg;r.room=p.fresh.room;r.weapon=p.fresh.weapon;r.character=p.fresh.character;
        if(!p.fresh.gameplay)r.reasons|=InputNotGameplay;
        if(epoch_ok&&p.fresh.player==e.state.stamp.player&&p.fresh.global==e.state.stamp.global&&p.fresh.frame==e.state.stamp.frame&&
           input::Coherent(p.fresh)&&p.fresh.gameplay)r.flags|=InputAligned;else r.reasons|=InputContext;
    }
    const auto& window=p.state.window;r.window_down=window.down;r.window_released=window.released;r.window_duration_ms=window.duration;
    const bool temporal=p.state.known&&input_ok&&input::Context(p.state.sample,p.fresh)&&p.state.sample.frame==p.fresh.frame&&
        p.state.sample.on==p.fresh.on&&p.state.sample.trg==p.fresh.trg&&input::Coherent(p.fresh)&&p.fresh.gameplay;
    if(!temporal)r.reasons|=WindowUnknown;
    if(now<p.state.time||now<window.last_time||(window.armed&&now<window.armed_at))r.reasons|=ClockBackward;
    else if(window.duration&&now>=window.armed_at){
        // The timestamp can survive disarm/consumption. Age alone never means
        // the window is still valid, nor explains why it was disarmed.
        r.window_age_ms=now-window.armed_at;
        if(window.armed&&r.window_age_ms<=window.duration)r.window_remaining_ms=window.duration-static_cast<uint32_t>(r.window_age_ms);
    }
    if(temporal&&window.armed)r.flags|=InputSampleArmed; // Stored sample, distinct from current deadline validity.
    if(temporal&&!(r.reasons&ClockBackward)&&window.armed&&(p.fresh.on&0x800)&&window.duration&&r.window_age_ms<=window.duration)r.flags|=WindowArmed;
    else r.reasons|=WindowNotArmed;
    if((kind==Kind::Candidate||kind==Kind::AttackSample)){
        owner::Observation o;
        if(!candidate||!source.Owner(*candidate,o))r.reasons|=OwnerRejected;
        else {
            r.flags|=OwnerRead;r.owner_frame=o.engine_frame;r.producer_return_rva=o.producer_return;r.owner_weapon_type=o.weapon_type;
            std::memcpy(r.owner_routines,o.routines.data(),4);std::memcpy(r.attack_current,o.current.data(),12);std::memcpy(r.attack_previous,o.previous.data(),12);
            if(o.FirstHandWeaponRoute())r.flags|=OwnerFirstRoute;else if(o.SukiRoute())r.flags|=OwnerSukiRoute;else r.reasons|=OwnerRoute;
            if(epoch_ok&&o.global==e.state.stamp.global&&o.engine_frame==e.state.stamp.frame&&r.flags&InputAligned)r.flags|=OwnerAligned;else r.reasons|=OwnerEpoch;
        }
    }
    if((kind==Kind::Candidate||kind==Kind::AttackSample)&&candidate&&(r.flags&OwnerAligned)){
        contact_diagnostic::Result contact;
        if(source.Contact(*candidate,contact)){r.contact=contact;r.contact_valid=true;}
    }
    return r;
}
// Only copied owner TLS is consulted each tail. Native memory and pose reads
// still happen only when a record is selected. Ignore unrelated input bits and
// repeat TRG while held; ON, policy history, sampled arm and current deadline
// transitions cannot be hidden by the pose-state/periodic sampling gate.
struct InputEdgeState {bool seen=false;uint32_t bits=0;input::Sample context{};};
inline bool InputChanged(InputEdgeState& previous,const InputSample& p,uint64_t now){
    uint32_t bits=uint32_t(p.enabled)|(uint32_t(p.thread)<<1);input::Sample context{};
    if(p.enabled&&p.thread){
        const auto& s=p.state;const auto& w=s.window;context=s.sample;
        const bool temporal=s.known&&s.sample.valid&&s.sample.gameplay&&input::Coherent(s.sample)&&
            now>=s.time&&now>=w.last_time&&now>=w.armed_at;
        const bool current=temporal&&w.armed&&(s.sample.on&0x800)&&w.duration&&now-w.armed_at<=w.duration;
        bits|=(uint32_t(s.known)<<2)|(uint32_t(s.sample.valid)<<3)|(uint32_t(s.sample.gameplay)<<4)|
            (uint32_t(bool(s.sample.on&0x800))<<5)|(uint32_t(w.down)<<6)|(uint32_t(w.released)<<7)|
            (uint32_t(w.armed)<<8)|(uint32_t(current)<<9)|(uint32_t(temporal)<<10);
    }
    const bool changed=!previous.seen||bits!=previous.bits||!input::Context(previous.context,context);
    previous={true,bits,context};return changed;
}
inline __declspec(thread) InputEdgeState last_input_edge;
inline __declspec(thread) bool recording=false,poisoned=false;
inline __declspec(thread) uint32_t last_tick_state=0xffffffff;
inline void Capture(Kind kind,const Candidate* candidate){
    if(!Enabled())return;
    if(recording){poisoned=true;InterlockedIncrement(&reentered_count);return;}
    recording=true;poisoned=false;const LONG version=InterlockedCompareExchange(&generation,0,0);
    Record r;r.kind=kind;
    __try {NativeSources sources;r=Build(sources,kind,candidate);}
    __except(EXCEPTION_EXECUTE_HANDLER){r.reasons|=ReadException;}
    if(poisoned){r.reasons|=Reentered;r.flags=0;}
    r.tail_samples=Count(tail_count);r.candidate_samples=Count(candidate_count);
    Publish(r,version);recording=false;
}
}
inline void __cdecl Tick(){
    const DWORD error=GetLastError();
    if(Enabled()){
        const auto tick=static_cast<uint32_t>(InterlockedIncrement(&tail_count));
        // Only the native callback thread reads its epoch TLS. Even a wrong-
        // thread invocation produces no borrowed TLS state in its record.
        const bool own=Count(epoch::owner_thread)==GetCurrentThreadId();
        const uint32_t status=own?(uint32_t(epoch::state.ready)|(uint32_t(epoch::state.poisoned)<<1)|(uint32_t(epoch::state.depth!=0)<<2)):8;
        if(own&&epoch::state.ready)InterlockedIncrement(&ready_count);
        const bool changed=status!=detail::last_tick_state;detail::last_tick_state=status;
        detail::NativeSources sources;
        const bool input_changed=detail::InputChanged(detail::last_input_edge,sources.InputState(),sources.Now());
        // One bounded queue attempt per tail, even if all three triggers fire.
        if(input_changed||changed||tick%tick_stride==0)detail::Capture(input_changed?Kind::InputEdge:Kind::Tick,nullptr);
    }
    SetLastError(error);
}
inline void __cdecl OnCandidate(const Candidate& candidate){
    const DWORD error=GetLastError();
    if(Enabled()){input::RefreshAtContact();InterlockedIncrement(&candidate_count);detail::Capture(Kind::Candidate,&candidate);}
    SetLastError(error);
}
inline void __cdecl OnSample(const Candidate& candidate){
    const DWORD error=GetLastError();
    owner::Observation observed;
    if(Enabled()&&owner::ObserveSampleCurrent(access,owner_code,candidate,observed)&&observed.QualifiedRoute()){
        input::RefreshAtContact();detail::Capture(Kind::AttackSample,&candidate);
    }
    SetLastError(error);
}
inline bool Start(const shield::NativeAccess& native,const owner::Code& code,bool enabled=false,uint32_t stride=60,const front_native::Code& front={}){
    if(started||!enabled||!stride||stride>600||!shield::Complete(native)||!code.verified||code.image_base!=native.image_base)return false;
    access=native;owner_code=code;front_code=front;tick_stride=stride;started=true;InterlockedIncrement(&generation);InterlockedExchange(&active,1);return true;
}
inline void Stop(){InterlockedExchange(&active,0);InterlockedIncrement(&generation);} // Retain immutable metadata for in-flight callbacks.
} // namespace link_runtime::parry::observation

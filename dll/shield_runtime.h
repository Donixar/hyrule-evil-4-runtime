#pragma once
#include "shield_observer.h"
#include "parry_pre_effects.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <type_traits>
#include "shield_life_bridge.h"
#include "rear_shield_band.h"

// This implementation is also exercised by the real x86 trampoline fixture.
// Observation remains native pass-through. Optional pre-effects plumbing has a
// production-deny decision until real owner/input/contact gates are implemented.
namespace link_runtime::shield::runtime {
using Wrapper=int(__cdecl*)(void*,void*,void*,int);
using Classifier=int(__cdecl*)(void*,void*,void*);
inline Wrapper original_wrapper=nullptr;
inline Classifier original_classifier=nullptr;
inline void* original_preclamp=nullptr;
inline PreClampCode preclamp_code;
inline OwnerCode owner_code;
inline bool preclamp_requested=false;
inline NativeAccess access;
inline ObserverConfig config;
inline volatile LONG active=0,remaining=0,dropped=0,failed=0,unscoped=0;
inline volatile LONG sequence=0,dropped_total=0,nested=0;
inline SRWLOCK queue_lock=SRWLOCK_INIT;
inline CopyQueue<64> queue;
inline __declspec(thread) Scope* current_scope=nullptr;
// Published before Start. Active tracking must never expire with diagnostic logs.
inline bool rear_observe=false,rear_active=false;
inline unsigned rear_kept_percent=defense::kRearPercentDefault;   // RearShieldDamagePercent, set before the hooks
// shield-guard-01 (shield_guard.h RearAllowed): 1 while the shield is in Link's hand or broken. The game thread
// publishes it; the preclamp then keeps native damage (rear_impact.reason = shield_away).
inline volatile LONG rear_suppressed=0;
inline volatile LONG rear_generation=0;
inline __declspec(thread) LONG rear_seen_generation=0;
inline __declspec(thread) rear_shield::measure::History rear_history;
inline bool CaptureBudget(){return rear_active||InterlockedCompareExchange(&remaining,0,0)>0;}
struct RearSound {rear_shield::Material material=rear_shield::Material::Unknown;uint32_t player=0,global=0,character=0,frame=0,tick=0,room=0;};
inline SRWLOCK rear_audio_lock=SRWLOCK_INIT;
inline RearSound rear_sounds[32]{};
inline unsigned rear_audio_begin=0,rear_audio_count=0;
inline volatile LONG rear_audio_dropped=0;
inline void QueueRearSound(const Scope& scope){
    if(!scope.rear_impact.sound)return;
    if(!TryAcquireSRWLockExclusive(&rear_audio_lock)){InterlockedIncrement(&rear_audio_dropped);return;}
    if(rear_audio_count==32)InterlockedIncrement(&rear_audio_dropped);
    else {rear_sounds[(rear_audio_begin+rear_audio_count++)%32]={scope.rear_impact.material,scope.event.player,
        scope.event.global,scope.classification.character,scope.rear_pose.frame,GetTickCount(),scope.rear_pose.room};}
    ReleaseSRWLockExclusive(&rear_audio_lock);
}
inline bool PopRearSound(RearSound& sound){
    if(!TryAcquireSRWLockExclusive(&rear_audio_lock))return false;
    const bool found=rear_audio_count!=0;
    if(found){sound=rear_sounds[rear_audio_begin];rear_audio_begin=(rear_audio_begin+1)%32;--rear_audio_count;}
    ReleaseSRWLockExclusive(&rear_audio_lock);return found;
}
static_assert(std::is_trivially_copyable<HitRecord>::value,"queue entries must contain copied values only");

struct Memory {
    bool Read(uint32_t address,void* output,size_t length) {
        if(!Range(address,length))return false;
        __try { std::memcpy(output,reinterpret_cast<const void*>(address),length);return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
};
// Keep the entire post-original x87/MMX/SSE state, including MXCSR/status/stack.
// UHD already requires SSE; aligned buffers are always owned by the caller's stack.
inline void SaveFP(void* state) {
    __asm {
        mov eax,state
        fxsave [eax]
    }
}
inline void RestoreFP(void* state) {
    __asm {
        mov eax,state
        fxrstor [eax]
    }
}

inline bool Enabled() { return InterlockedCompareExchange(&active,0,0)==1; }
inline void Drop() { InterlockedIncrement(&dropped);InterlockedIncrement(&dropped_total); }
inline void Enqueue(HitRecord& record) {
    // Collision queries are not damage applications. A rear-only session must
    // not spend its entire evidence budget on misses before the first hit.
    if((rear_observe||rear_active)&&record.event.native_hit<=0&&!record.preclamp.captures)return;
    if(!TryAcquireSRWLockExclusive(&queue_lock)) { Drop();return; }
    if(InterlockedCompareExchange(&remaining,0,0)>0) {
        // Invocation identity is owned by the wrapper. Neither ATK nor a caller cooldown deduplicates completions.
        if(queue.Push(record))InterlockedDecrement(&remaining);
        else Drop();
    } else Drop();
    ReleaseSRWLockExclusive(&queue_lock);
}
inline bool Pop(HitRecord& out) {
    if(!TryAcquireSRWLockExclusive(&queue_lock))return false;
    const bool result=queue.Pop(out);ReleaseSRWLockExclusive(&queue_lock);return result;
}

// PUSHFD followed by PUSHAD; saved_esp points at the saved flags, not native ESP.
struct PreClampFrame {uint32_t edi,esi,ebp,saved_esp,ebx,edx,ecx,eax,eflags;};
static_assert(sizeof(PreClampFrame)==36&&offsetof(PreClampFrame,eflags)==32,"x86 saved frame layout");
inline bool ApplyRearPreClamp(PreClampFrame& frame,Scope& scope,const rear_shield::Proof& proof){
    auto& p=scope.preclamp;
    if(!rear_active||scope.depth!=1||!scope.captured||!p.valid||p.rejected||p.captures!=1||
       frame.esi!=scope.event.global||static_cast<int32_t>(frame.edi)!=p.damage)return false;
    int32_t value=p.damage;
    if(InterlockedCompareExchange(&rear_suppressed,0,0)){
        if(!scope.rear_impact.attempted){scope.rear_impact.attempted=true;scope.rear_impact.original=scope.rear_impact.result=value;
            scope.rear_impact.reason=rear_shield::Reason::ShieldAway;}
        return false;
    }
    if(!rear_shield::Apply(true,proof,value,scope.rear_impact,rear_kept_percent))return false;
    p.mitigated=true;p.applied_damage=value;
    // The only damage write: EDI in the owned PUSHAD frame, before native HP
    // clamp. No ATK power, HP, reaction, caller arguments or other actor changes.
    frame.edi=static_cast<uint32_t>(value);QueueRearSound(scope);return true;
}
struct WrapperEntry {uint32_t stack=0,ebp=0,edi=0,esi=0;};
inline __declspec(thread) WrapperEntry wrapper_entry;
// One-shot handoff to the immediately tail-entered cdecl wrapper. It contains
// no live object references beyond copied pointer VALUES. Stack identity prevents
// a direct/nested call to the C++ body from consuming another entry's registers.
__declspec(noinline) inline void __cdecl RecordWrapperEntry(const PreClampFrame* frame) {
    const DWORD error=GetLastError();wrapper_entry={};
    __try {
        if(frame&&(parry::Enabled()||(Enabled()&&owner_code.verified&&CaptureBudget())))
            wrapper_entry={reinterpret_cast<uint32_t>(frame)+sizeof(PreClampFrame),frame->ebp,frame->edi,frame->esi};
    } __except(EXCEPTION_EXECUTE_HANDLER) {wrapper_entry={};InterlockedIncrement(&failed);}
    SetLastError(error);
}
__declspec(noinline) inline void __cdecl RecordPreClampFrame(const PreClampFrame* frame) {
    const DWORD error=GetLastError();Scope* scope=current_scope;
    __try {
        if(scope&&Enabled()&&preclamp_requested&&CaptureBudget()) {
            const PreClampSample prior=scope->preclamp;
            const bool admitted_attempt=ConsumePreClampAttempt(scope->preclamp);
            Memory memory;uint32_t frame_return=0;
            // Consume before the first checked read, including EBP+4. EBP+8 is scratch.
            const bool frame_read=frame&&frame->ebp<=0xFFFFFFFBU&&Word(memory,frame->ebp+4,frame_return);
            const uint32_t raw_return=frame_return;
            if(frame_read)frame_return=LifeReturn(frame->ebp,raw_return,scope,scope->event.player);
            auto& sample=scope->preclamp;
            if(sample.audit_count<2) {
                auto& audit=sample.audit[sample.audit_count++];
                audit.frame_read=frame_read;audit.return_address=frame_read?raw_return:0;
                audit.native_return=frame_return;audit.bridged=frame_return&&frame_return!=raw_return;
                audit.thread=GetCurrentThreadId();
                if(frame) {audit.global=frame->esi;audit.damage=static_cast<int32_t>(frame->edi);audit.raw_flags=frame->ebx;}
            } else sample.audit_overflow=true;
            if(!frame_read)RejectPreClamp(sample);
            else if(!admitted_attempt&&prior.valid&&!prior.rejected&&prior.captures==1) {
                HealthSample first{},last{};
                if(ReadHealthSample(memory,access,scope->event,first)&&
                   ReadHealthSample(memory,access,scope->event,last)&&
                   CanSeparateSecondaryZero(*scope,prior,access,preclamp_code,GetCurrentThreadId(),frame->esi,
                       frame_return,static_cast<int32_t>(frame->edi),frame->ebx,first)&&
                   CanSeparateSecondaryZero(*scope,prior,access,preclamp_code,GetCurrentThreadId(),frame->esi,
                       frame_return,static_cast<int32_t>(frame->edi),frame->ebx,last)) {
                    // Only the known zero follow-up is excluded from the PRIMARY
                    // count. Audit still records both. No native effect is skipped.
                    sample.captures=1;sample.valid=true;sample.rejected=false;sample.secondary_zero_seen=true;
                }
            }
            else if(admitted_attempt&&AdmitPreClampAttempt(scope,access,preclamp_code,GetCurrentThreadId(),frame->esi,frame_return)) {
                HealthSample first{},last{};
                if(!ReadHealthSample(memory,access,scope->event,first)||!ReadHealthSample(memory,access,scope->event,last))
                    RejectPreClamp(scope->preclamp);
                else if(CopyPreClampAttempt(*scope,static_cast<int32_t>(frame->edi),frame->ebx,first,last,
                                         static_cast<uint32_t>(preclamp_code.wrapper_return_rva))&&rear_active){
                  // Angle + height per hit (rear_shield_band.h): the only rear shield mode.
                  const auto band=rear_shield::band::Revalidate(memory,access,scope->event,scope->rear_band);
                  scope->rear_band_evidence=band.evidence;
                  ApplyRearPreClamp(*const_cast<PreClampFrame*>(frame),*scope,band.proof);
                  auto& impact=scope->rear_impact;
                  if(impact.attempted&&!impact.applied&&impact.reason!=rear_shield::Reason::ShieldAway&&
                     band.evidence.reason!=rear_shield::Reason::Applied)impact.reason=band.evidence.reason;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(scope)RejectPreClamp(scope->preclamp);
        InterlockedIncrement(&failed);
    }
    SetLastError(error);
}
// There is no callable return address at the observation site. Resume the seven
// stolen bytes once via MinHook. The callback's cdecl SEH paths preserve EBX.
__declspec(naked) inline void PreClampDetour() {
    __asm {
        pushfd
        pushad
        cld
        mov ebx,esp
        sub esp,528
        and esp,0fffffff0h
        fxsave [esp]
        fninit
        mov dword ptr [esp+512],1f80h
        ldmxcsr [esp+512]
        push ebx
        call RecordPreClampFrame
        add esp,4
        fxrstor [esp]
        mov esp,ebx
        popad
        popfd
        jmp dword ptr [original_preclamp]
    }
}

__declspec(noinline) inline int __cdecl WrapperDetour(void* attack,void* position,void* previous,int companion_filter) {
    const WrapperEntry entry=wrapper_entry;wrapper_entry={};
    const bool entry_matches=entry.stack==reinterpret_cast<uint32_t>(_AddressOfReturnAddress());
    // Invalidate the outer scope even if a nested invocation has no site or budget.
    if(current_scope){RejectPreClamp(current_scope->preclamp);rear_history.Reset();}
    const LONG generation=InterlockedCompareExchange(&rear_generation,0,0);
    if(rear_seen_generation!=generation){rear_history.Reset();rear_seen_generation=generation;}
    if(!Enabled()||!CaptureBudget())
        return parry::CallWrapper(original_wrapper,attack,position,previous,companion_filter,reinterpret_cast<uint32_t>(_ReturnAddress()),
            entry_matches?entry.ebp:0,entry_matches?entry.edi:0,entry_matches?entry.esi:0);
    const uint32_t caller=reinterpret_cast<uint32_t>(_ReturnAddress());
    const DWORD initial_error=GetLastError();
    __declspec(align(16)) unsigned char fp[512];SaveFP(fp);
    Scope scope;scope.previous=current_scope;scope.depth=scope.previous?scope.previous->depth+1:1;
    scope.event.attack=reinterpret_cast<uint32_t>(attack);scope.event.current=reinterpret_cast<uint32_t>(position);
    scope.event.previous=reinterpret_cast<uint32_t>(previous);scope.event.outer_return=caller;
    scope.companion_filter=companion_filter;
    if(scope.depth>1)InterlockedIncrement(&nested);
    if(Enabled()&&scope.depth==1&&CaptureBudget()) {
        // Drop immediately on contention; never hold the lock across native code or memory sampling.
        if(TryAcquireSRWLockExclusive(&queue_lock)) {
            ReleaseSRWLockExclusive(&queue_lock);
            scope.invocation=static_cast<uint32_t>(InterlockedIncrement(&sequence));scope.thread=GetCurrentThreadId();
            Memory memory;
            Word(memory,access.image_base+static_cast<uint32_t>(access.code.player_slot_rva),scope.event.player);
            Word(memory,access.image_base+static_cast<uint32_t>(access.code.global_slot_rva),scope.event.global);
            ReadHealthSample(memory,access,scope.event,scope.before);
            if(entry_matches) {
                __try {CaptureOwnerContext(memory,access,owner_code,scope.event,entry.ebp,entry.edi,companion_filter,scope.owner);}
                __except(EXCEPTION_EXECUTE_HANDLER) {scope.owner={};InterlockedIncrement(&failed);}
                if((rear_observe||rear_active)&&scope.owner.valid){
                    __try {rear_shield::measure::ReadRoute(memory,access,scope.event,entry.ebp,entry.edi,
                        __readfsdword(8),__readfsdword(4),scope.rear_route);}
                    __except(EXCEPTION_EXECUTE_HANDLER){scope.rear_route={};rear_history.Reset();}
                }
            }
        } else {Drop();rear_history.Reset();}
    }
    current_scope=&scope;
    RestoreFP(fp);SetLastError(initial_error);
    int result=0;
    // __finally restores TLS even when a native SEH/C++ exception crosses the trampoline.
    // Scope is trivially destructible; no C++ allocation or destructor enters this boundary.
    __try {
        result=parry::CallWrapper(original_wrapper,attack,position,previous,companion_filter,caller,
            entry_matches?entry.ebp:0,entry_matches?entry.edi:0,entry_matches?entry.esi:0);
    } __finally {
        if(AbnormalTermination())rear_history.Reset();
        current_scope=scope.previous;
    }
    // Normal return only. The finally boundary restores TLS but can never publish a fabricated completion.
    SaveFP(fp);const DWORD completed_error=GetLastError();
    if(scope.invocation&&Enabled()&&InterlockedCompareExchange(&remaining,0,0)>0) {
        Memory memory;HealthSample after;ReadHealthSample(memory,access,scope.event,after);
        HitRecord record;record.event=scope.event;record.frame=scope.classification;
        record.tick=GetTickCount();record.companion_filter=scope.companion_filter;
        record.caller_shape=scope.caller_shape;record.captured=scope.captured;
        record.completion=PairCompletion(scope.invocation,scope.thread,result,scope.before,after);
        record.preclamp=scope.preclamp;
        record.owner=scope.owner;
        record.rear_pose=scope.rear_pose;record.rear_impact=scope.rear_impact;
        record.rear_measure=scope.rear_measure;record.rear_band=scope.rear_band_evidence;
        Enqueue(record);
    }
    RestoreFP(fp);SetLastError(completed_error);return result;
}
// No call/return substitution: restore the entire native entry machine, then
// tail-enter the existing four-argument cdecl body on the original stack.
__declspec(naked) inline void WrapperEntryDetour() {
    __asm {
        pushfd
        pushad
        cld
        mov ebx,esp
        sub esp,528
        and esp,0fffffff0h
        fxsave [esp]
        push ebx
        call RecordWrapperEntry
        add esp,4
        fxrstor [esp]
        mov esp,ebx
        popad
        popfd
        jmp WrapperDetour
    }
}
__declspec(noinline) inline int __cdecl ClassifierDetour(void* attack,void* position,void* previous) {
    if(!Enabled()||!CaptureBudget())
        return parry::CallClassifier(original_classifier,attack,position,previous,reinterpret_cast<uint32_t>(_ReturnAddress()));
    const uint32_t caller=reinterpret_cast<uint32_t>(_ReturnAddress());
    const DWORD initial_error=GetLastError();
    __declspec(align(16)) unsigned char fp[512];SaveFP(fp);
    RestoreFP(fp);SetLastError(initial_error);
    // Native arguments, classification, flags, damage pointer and Ashley branch remain native.
    const int result=parry::CallClassifier(original_classifier,attack,position,previous,caller);
    SaveFP(fp);const DWORD original_error=GetLastError();
    // New pose geometry needs a clean FP environment; the native snapshot is
    // restored in full below, including its original rounding mode and stack.
    const uint32_t clean_mxcsr=0x1F80;
    __asm {fninit
        ldmxcsr clean_mxcsr}
    if(Enabled()&&CaptureBudget()) {
        Memory memory;HitRecord record;
        if(!Matches(current_scope,reinterpret_cast<uint32_t>(attack),reinterpret_cast<uint32_t>(position),
                    reinterpret_cast<uint32_t>(previous),caller,access)){
            InterlockedIncrement(&unscoped);rear_history.Reset();
        }
        // A missed classification may already hit the body. Neither a lost
        // scope nor lost admission can preserve older first-contact evidence.
        else if(!current_scope->invocation) {rear_history.Reset();}
        else if(CaptureHit(memory,access,current_scope,reinterpret_cast<uint32_t>(attack),reinterpret_cast<uint32_t>(position),
                           reinterpret_cast<uint32_t>(previous),caller,result,record)) {
            current_scope->event=record.event;current_scope->classification=record.frame;
            current_scope->caller_shape=record.caller_shape;current_scope->captured=true;
            if(rear_observe||rear_active){
                rear_shield::measure::Sample measured;
                rear_shield::measure::ReadSample(memory,access,record.event,record.frame,current_scope->rear_route,measured);
                if(!measured.valid)measured.route=current_scope->rear_route;
                rear_history.Observe(measured,current_scope->rear_measure);
                if(measured.valid)current_scope->rear_pose=measured.pose;
                else if(result>0)rear_shield::ReadPose(memory,access,record.event,current_scope->rear_pose);
                // Single classifier epoch; preclamp rereads it (band::Revalidate).
                rear_shield::band::Capture(memory,access,record.event,record.frame,current_scope->rear_route,current_scope->rear_band);
                current_scope->rear_band_evidence=rear_shield::band::Evaluate(current_scope->rear_band,1);
            }
        } else {InterlockedIncrement(&failed);rear_history.Reset();}
    }
    RestoreFP(fp);SetLastError(original_error);return result;
}

// Injectable operations are for failure controls only; ABI fixtures use actual MH.
struct HookOperations {
    decltype(&MH_Initialize) initialize=&MH_Initialize;
    decltype(&MH_Uninitialize) uninitialize=&MH_Uninitialize;
    decltype(&MH_CreateHook) create=&MH_CreateHook;
    decltype(&MH_EnableHook) enable=&MH_EnableHook;
    decltype(&MH_DisableHook) disable=&MH_DisableHook;
    decltype(&MH_RemoveHook) remove=&MH_RemoveHook;
};
struct OwnedHook {void* target=nullptr;bool created=false,ever_enabled=false,disabled=false;};
enum class HookState {empty,live,teardown_failed,retired};
inline OwnedHook owned_hooks[4];
inline HookState hook_state=HookState::empty;
inline HookOperations hook_operations;
inline bool owns_minhook=false;
inline MH_STATUS teardown_status=MH_OK;
inline void ClearOriginal(unsigned index) {
    if(index==0)original_wrapper=nullptr;
    else if(index==1)original_classifier=nullptr;
    else if(index==2)original_preclamp=nullptr;
    else parry::original_site=nullptr;
}
inline MH_STATUS Stop() {
    InterlockedIncrement(&rear_generation);
    InterlockedExchange(&active,0);InterlockedExchange(&parry::active,0);teardown_status=MH_OK;bool retained=false;
    for(int i=3;i>=0;--i) {
        auto& h=owned_hooks[i];if(!h.created)continue;
        if(h.ever_enabled) {
            // Disable is not quiescence. A detour may be between entry and its jump.
            // Retain the hook record/trampoline even after a successful Disable.
            if(!h.disabled) {
                const auto status=hook_operations.disable(h.target);
                if(status==MH_OK||status==MH_ERROR_DISABLED)h.disabled=true;
                else if(teardown_status==MH_OK)teardown_status=status;
            }
            retained=true;
        } else {
            const auto status=hook_operations.remove(h.target);
            if(status==MH_OK) {h={};ClearOriginal(static_cast<unsigned>(i));}
            else {retained=true;if(teardown_status==MH_OK)teardown_status=status;}
        }
    }
    if(!retained&&owns_minhook) {
        const auto status=hook_operations.uninitialize();
        if(status==MH_OK)owns_minhook=false;
        else {retained=true;teardown_status=status;}
    }
    if(!retained) {access={};preclamp_code={};owner_code={};preclamp_requested=false;parry::site_code={};parry::classifier_return=0;hook_state=HookState::empty;}
    else hook_state=teardown_status==MH_OK?HookState::retired:HookState::teardown_failed;
    return teardown_status;
}
// All originals/context are published before Enable; optional pre-effects is last.
// No global Disable/Remove/Uninitialize can affect existing feature ownership.
inline MH_STATUS Start(const NativeAccess& native,const ObserverConfig& settings,
                       const PreClampCode* preclamp=nullptr,const HookOperations& operations={},const OwnerCode* producer=nullptr,
                       const parry::Code* preeffects=nullptr) {
    if(!settings.enabled||!Complete(native)||native.ordinary_routes_verified||!native.ordinary_return_rvas.empty())
        return MH_ERROR_NOT_EXECUTABLE;
    if(hook_state!=HookState::empty||Enabled())return MH_ERROR_ALREADY_CREATED;
    if(producer&&(!producer->verified||producer->return_rva!=0x55CE2||
       !Area(native.regions.data(),native.regions.size(),producer->return_rva-5,5,true)))return MH_ERROR_NOT_EXECUTABLE;
    if(preclamp&&(!preclamp->verified||preclamp->site_rva!=preclamp->body_rva+0x167||
       preclamp->wrapper_return_rva!=native.code.wrapper_rva+0x42||
       !Area(native.regions.data(),native.regions.size(),preclamp->body_rva,0x1EC,true)||
       !Area(native.regions.data(),native.regions.size(),preclamp->site_rva,7,true)))return MH_ERROR_NOT_EXECUTABLE;
    if(preeffects) {
        const auto found=parry::FindCode(reinterpret_cast<const unsigned char*>(native.image_base),native.image_size,native);
        if(!preeffects->verified||!found.verified||preeffects->site_rva!=found.site_rva)return MH_ERROR_NOT_EXECUTABLE;
    }
    // Allocation can fail only before initializing/changing any shared hook state.
    access=native;config=settings;preclamp_code=preclamp?*preclamp:PreClampCode{};
    owner_code=producer?*producer:OwnerCode{};
    parry::site_code=preeffects?*preeffects:parry::Code{};
    parry::classifier_return=native.image_base+static_cast<uint32_t>(native.code.classifier_return_rva);
    preclamp_requested=preclamp!=nullptr;hook_operations=operations;
    MH_STATUS status=hook_operations.initialize();owns_minhook=status==MH_OK;
    if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return status;
    void* targets[]={reinterpret_cast<void*>(native.image_base+static_cast<uint32_t>(native.code.wrapper_rva)),
                     reinterpret_cast<void*>(native.image_base+static_cast<uint32_t>(native.code.classifier_rva)),
                     preclamp?reinterpret_cast<void*>(native.image_base+static_cast<uint32_t>(preclamp->site_rva)):nullptr,
                     preeffects?reinterpret_cast<void*>(native.image_base+preeffects->site_rva):nullptr};
    void* detours[]={reinterpret_cast<void*>(&WrapperEntryDetour),reinterpret_cast<void*>(&ClassifierDetour),reinterpret_cast<void*>(&PreClampDetour),reinterpret_cast<void*>(&parry::Detour)};
    void** originals[]={reinterpret_cast<void**>(&original_wrapper),reinterpret_cast<void**>(&original_classifier),&original_preclamp,&parry::original_site};
    const unsigned count=4;
    for(unsigned i=0;i<count;++i) {
        if(!targets[i])continue;
        status=hook_operations.create(targets[i],detours[i],originals[i]);
        if(status!=MH_OK){Stop();return status;}
        owned_hooks[i]={targets[i],true,false,false};
    }
    InterlockedExchange(&remaining,static_cast<LONG>(settings.events));
    for(unsigned i=0;i<count;++i) {
        if(!targets[i])continue;
        status=hook_operations.enable(targets[i]);
        if(status!=MH_OK){Stop();return status;}
        owned_hooks[i].ever_enabled=true;
    }
    hook_state=HookState::live;InterlockedIncrement(&rear_generation);InterlockedExchange(&active,1);
    InterlockedExchange(&parry::active,preeffects?1:0);return MH_OK;
}
} // namespace link_runtime::shield::runtime

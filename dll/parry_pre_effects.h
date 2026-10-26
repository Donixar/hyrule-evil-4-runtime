#pragma once
#include "shield_reader.h"
#include <windows.h>
#include <intrin.h>

// Pre-effects transport only. This does not qualify an owner, input or sword contact.
// The production resolver is absent unless an explicit defense startup wires it.
namespace link_runtime::parry {
using namespace link_runtime::shield;
struct Code {bool verified=false;uint32_t site_rva=0;};
inline Code FindCode(const unsigned char* image,size_t size,const NativeAccess& a) {
    if(!image||size!=a.image_size||!Complete(a))return {};
    const size_t site=a.code.helper_return_rva;
    if(site!=a.code.classifier_rva+0x96||site<5||site>size||size-site<28||
       !Area(a.regions.data(),a.regions.size(),site-5,33,true)||
       !Area(a.regions.data(),a.regions.size(),a.code.classifier_rva+0x2B,4,true))return {};
    size_t target=0;
    if(!Relative(image,size,site-5,0xE8,target)||target!=a.code.helper_rva||
       !Match(image+site,{0x83,0xC4,0x10,0x85,0xC0,0x74,0x8E,0xB9,0xFF,0xBF,0,0,0x66,0x21,0x48,0x24,0x8B,0x15,-1,-1,-1,-1,0x89,0x82,0x40,3,0,0})||
       U32(image+site+18)!=uint64_t(a.image_base)+a.code.player_slot_rva||
       !Match(image+a.code.classifier_rva+0x2B,{0x5F,0x5E,0x33,0xC0}))return {};
    return {true,static_cast<uint32_t>(site)};
}
// PUSHFD/PUSHAD frame; POPAD ignores saved_esp. Native ESP is frame+36.
struct MachineFrame {uint32_t edi,esi,ebp,saved_esp,ebx,edx,ecx,eax,eflags;};
static_assert(sizeof(MachineFrame)==36&&offsetof(MachineFrame,eax)==28,"x86 shim frame");
struct WrapperToken {
    WrapperToken* previous=nullptr;
    uint32_t attack=0,current=0,old=0,caller=0,thread=0;
    uint32_t producer_frame=0,producer_owner=0; // Native entry values, not validated actor identity.
    unsigned depth=0;
    bool tainted=false;
};
struct ClassifierToken {
    ClassifierToken* previous=nullptr;
    WrapperToken* wrapper=nullptr;
    uint32_t caller=0,thread=0;
    bool structural=false,attempted=false,tainted=false;
    bool sampled=false,sample_scope=false;
};
struct Candidate {
    uint32_t body_part=0,attack=0,current=0,old=0,outer_caller=0;
    uint32_t producer_frame=0,producer_owner=0; // Valid only within the current wrapper invocation.
};
inline __declspec(thread) WrapperToken* wrapper_token=nullptr;
inline __declspec(thread) ClassifierToken* classifier_token=nullptr;
inline volatile LONG active=0;
inline void* original_site=nullptr;
inline Code site_code;
inline uint32_t classifier_return=0;
// Set once before enabling transport; keep the callback's module resident.
// Observation receives copied values and cannot return a damage decision.
using Observer=void(__cdecl*)(const Candidate&);
inline Observer observer=nullptr;
// Diagnostic callback for a no-body-hit sample. Cannot authorize a decision.
inline Observer sample_observer=nullptr;
using Resolver=bool(__cdecl*)(const Candidate&);
inline Resolver resolver=nullptr; // Set before hook admission; module remains resident.
inline bool Enabled() {return InterlockedCompareExchange(&active,0,0)==1;}
// The current structural invocation (the generic part of owner::detail::Current, without the
// em10 producer-frame assumption): one wrapper of depth 1 on this thread, its untainted
// structural classifier with the hit attempted (or the bounded no-hit sample scope), and the
// candidate equal to the tokens. Both tokens live on this thread's stack. Tier 2 and the
// position-given sparks use it; tier 1 keeps owner::detail::Current on top of it.
struct Invocation {const WrapperToken* wrapper=nullptr;const ClassifierToken* classifier=nullptr;uint32_t low=0,high=0;};
inline bool StackSpan(uint32_t at,size_t size,uint32_t low,uint32_t high) {
    return Range(at,size)&&at>=low&&uint64_t(at)+size<=high;
}
inline Invocation CurrentInvocation(const Candidate& c,bool sample=false) {
    const uint32_t low=__readfsdword(8),high=__readfsdword(4),thread=GetCurrentThreadId();
    const auto* w=wrapper_token;const auto* t=classifier_token;
    const uint32_t wa=reinterpret_cast<uint32_t>(w),ta=reinterpret_cast<uint32_t>(t);
    if(!Enabled()||!site_code.verified||!StackSpan(wa,sizeof(*w),low,high)||!StackSpan(ta,sizeof(*t),low,high)||
       uint64_t(ta)+sizeof(*t)>wa||(sample?(c.body_part||!t->sample_scope||!t->sampled):(!c.body_part||!t->attempted))||!t->structural||t->tainted||t->previous||t->wrapper!=w||
       w->previous||w->depth!=1||w->tainted||w->thread!=thread||t->thread!=thread||t->caller!=classifier_return||
       c.attack!=w->attack||c.current!=w->current||c.old!=w->old||c.outer_caller!=w->caller||
       c.producer_frame!=w->producer_frame||c.producer_owner!=w->producer_owner)return {};
    return {w,t,low,high};
}
#ifdef LINK_PARRY_FIXTURE
// Defined only by the standalone test translation unit. Never a production knob.
bool FixtureDecision(const Candidate&);
#endif
inline bool Decision(const Candidate& candidate) {
#ifdef LINK_PARRY_FIXTURE
    return FixtureDecision(candidate);
#else
    return resolver?resolver(candidate):false;
#endif
}
inline void SaveFP(void* state) {__asm {mov eax,state
    fxsave [eax]}}
inline void RestoreFP(void* state) {__asm {mov eax,state
    fxrstor [eax]}}
using Wrapper=int(__cdecl*)(void*,void*,void*,int);
using Classifier=int(__cdecl*)(void*,void*,void*);
__declspec(noinline) inline int CallWrapper(Wrapper original,void* attack,void* current,void* old,int filter,uint32_t caller,uint32_t producer_frame=0,uint32_t producer_owner=0) {
    if(!Enabled())return original(attack,current,old,filter);
    __declspec(align(16)) unsigned char fp[512];SaveFP(fp);const DWORD error=GetLastError();
    WrapperToken token;token.previous=wrapper_token;token.depth=token.previous?token.previous->depth+1:1;
    if(token.previous)token.previous->tainted=true;
    token.attack=reinterpret_cast<uint32_t>(attack);token.current=reinterpret_cast<uint32_t>(current);token.old=reinterpret_cast<uint32_t>(old);
    token.caller=caller;token.thread=GetCurrentThreadId();
    token.producer_frame=producer_frame;token.producer_owner=producer_owner;wrapper_token=&token;
    RestoreFP(fp);SetLastError(error);int result=0;
    __try {result=original(attack,current,old,filter);}
    __finally {wrapper_token=token.previous;}
    return result;
}
__declspec(noinline) inline int CallClassifier(Classifier original,void* attack,void* current,void* old,uint32_t caller) {
    if(!Enabled())return original(attack,current,old);
    __declspec(align(16)) unsigned char fp[512];SaveFP(fp);const DWORD error=GetLastError();
    ClassifierToken token;token.previous=classifier_token;token.wrapper=wrapper_token;token.caller=caller;token.thread=GetCurrentThreadId();
    if(token.previous)token.previous->tainted=true;
    const auto* w=token.wrapper;
    token.structural=w&&w->depth==1&&!w->tainted&&w->thread==token.thread&&caller==classifier_return&&
        w->attack==reinterpret_cast<uint32_t>(attack)&&w->current==reinterpret_cast<uint32_t>(current)&&w->old==reinterpret_cast<uint32_t>(old);
    classifier_token=&token;RestoreFP(fp);SetLastError(error);int result=0;
    __try {result=original(attack,current,old);}
    __finally {classifier_token=token.previous;}
    return result;
}
__declspec(noinline) inline void __cdecl Record(MachineFrame* frame) {
    const DWORD error=GetLastError();
    __try {
        auto* t=classifier_token;auto* w=wrapper_token;
        if(Enabled()&&site_code.verified&&frame&&t&&w&&(frame->eax||(sample_observer&&!t->sampled))&&t->wrapper==w&&t->structural&&!t->attempted&&!t->tainted&&
           !w->tainted&&w->depth==1&&t->thread==GetCurrentThreadId()&&w->thread==t->thread&&t->caller==classifier_return) {
            if(frame->eax)t->attempted=true;
            if(Range(frame->ebp,20)&&frame->ebx==w->attack&&frame->esi==w->current&&frame->edi==w->old) {
                const auto* args=reinterpret_cast<const uint32_t*>(frame->ebp);
                if(args[2]==w->attack&&args[3]==w->current&&args[4]==w->old) {
                    const Candidate candidate{frame->eax,w->attack,w->current,w->old,w->caller,w->producer_frame,w->producer_owner};
                    if(!frame->eax){
                        t->sampled=true;t->sample_scope=true;
                        __try {sample_observer(candidate);}
                        __finally {t->sample_scope=false;}
                        SetLastError(error);return; // Never call resolver or change native miss result.
                    }
                    if(observer)observer(candidate);
                    // Decision exceptions fail open to native damage; commit EAX last.
                    if(Enabled()&&site_code.verified&&classifier_token==t&&wrapper_token==w&&
                       t->wrapper==w&&!t->tainted&&!w->tainted&&w->depth==1&&Decision(candidate))frame->eax=0;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {} // Invalid structural reads never cancel damage.
    SetLastError(error);
}
__declspec(naked) inline void Detour() {
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
        call Record
        add esp,4
        fxrstor [esp]
        mov esp,ebx
        popad
        popfd
        jmp dword ptr [original_site]
    }
}
} // namespace link_runtime::parry

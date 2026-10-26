#pragma once
#include "mortal_draw_dispatch.h"
#include "shield_reader.h"
#include "thread_binding.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>

namespace link_runtime::mortal_draw::dispatch_hooks {
using namespace dispatch;
inline Runtime runtime;
inline Handler normal_original=nullptr,event_original=nullptr;
inline native_scope::Core core_original=nullptr;
// Optional adapters are published before Observe, and immutable while hooks run.
// Null keeps the native dispatch plan unchanged (ZTarget Strafe defaults to 0).
using NormalForward=void(__cdecl*)(Handler,void*);
using BeforeCore=void(__cdecl*)(void*,void*,void*,uintptr_t);
inline NormalForward normal_forward=nullptr;
inline BeforeCore before_core=nullptr;
inline LONG thread=0;   // the served game thread (binding's id), read by OwnedThread
inline thread_binding::Binding binding;
inline void* targets[3]{};
inline bool created[3]{},ever_enabled=false;
inline bool OwnedThread(){return InterlockedCompareExchange(&thread,0,0)==static_cast<LONG>(GetCurrentThreadId());}
inline bool PlayerThread(void* p){
    // The shared animation core runs for non-player models too. Bind only at
    // a current-player handler, never from an arbitrary model update.
    if(!runtime.Current(p))return false;
    // skills-fix-01: a new game thread takes over once the bound one has exited (thread_binding.h);
    // nothing of the dead thread's handler is still running, so its reentry marks are stale.
    const auto claim=binding.Take();
    if(!thread_binding::Serves(claim))return false;
    if(claim==thread_binding::Claim::Rebound){runtime.busy=false;runtime.observing=false;runtime.current=nullptr;}
    if(claim!=thread_binding::Claim::Owner)InterlockedExchange(&thread,static_cast<LONG>(GetCurrentThreadId()));
    return true;
}
inline void __cdecl NormalNative(void* p){normal_forward(normal_original,p);}
inline void __cdecl Normal(void* p){if(PlayerThread(p))runtime.Handle(false,normal_forward?NormalNative:normal_original,p);else normal_original(p);}
inline void __cdecl Event(void* p){if(PlayerThread(p))runtime.Handle(true,event_original,p);else event_original(p);}
inline void __cdecl Core(void* p,void* w,void* c){
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(OwnedThread()){if(before_core)before_core(p,w,c,caller);runtime.Core(core_original,p,w,c);}
    else core_original(p,w,c);
}
// Mandatory existing finder output plus independent event/table/call-chain bytes.
inline bool Qualify(const shield::NativeAccess& a){
    using namespace shield;
    if(!Complete(a)||a.code.knife_dispatch_rva!=0x3632E0)return false;
    const auto* image=reinterpret_cast<const unsigned char*>(a.image_base);
    for(size_t rva:{size_t(0x352CE0),size_t(0x261D60),size_t(0x27E5F0),size_t(0x27E640),size_t(0x25CF80),size_t(0x25CFD0)})
        if(!Area(a.regions.data(),a.regions.size(),rva,64,true))return false;
    if(!Area(a.regions.data(),a.regions.size(),0x81BCB4,28,false)||!Area(a.regions.data(),a.regions.size(),0x81B9B0,12,false))return false;
    size_t resolved=0;
    return Match(image+0x352CE0,{0x55,0x8B,0xEC,0x8B,0x45,0x08,0x0F,0xB6,0x88,0xFD,0,0,0,0x8B,0x14,0x8D})&&
        U32(image+0x352CF0)==a.image_base+0x81B9B0&&
        U32(image+0x81BCB4)==a.image_base+0x3632E0&&
        U32(image+0x81BCB4+20)==a.image_base+0x7E46&&
        Relative(image,a.image_size,0x7E46,0xE9,resolved)&&resolved==0x352CE0&&
        U32(image+0x81B9B0)==a.image_base+0x3529D0&&
        Match(image+0x27E5F0,{0x56,0x8B,0xF1,0xE8})&&
        Match(image+0x27E640,{0x55,0x8B,0xEC,0x56,0x8B,0xF1,0xB8,2,0,0,0})&&
        Match(image+0x25CF80,{0x55,0x8B,0xEC,0x0F,0xB7,0x45,0x10})&&
        Match(image+0x25CFD0,{0x6A,0,0x51,0xE8})&&
        Match(image+0x261D60,{0x55,0x8B,0xEC,0x81,0xEC,0xAC,0,0,0});
}
// Retryable cleanup is allowed only before any enable attempt.
inline MH_STATUS ReleasePrepared(){
    if(ever_enabled)return MH_ERROR_ENABLED;
    for(unsigned i=0;i<3;++i)if(created[i]){const auto s=MH_RemoveHook(targets[i]);if(s!=MH_OK)return s;created[i]=false;}
    normal_original=nullptr;event_original=nullptr;core_original=nullptr;normal_forward=nullptr;before_core=nullptr;
    for(auto& target:targets)target=nullptr;
    runtime={};return MH_OK;
}
// Prepares disabled hooks only. Caller owns MinHook initialization and must
// verify the pinned image identity first; this is not a general version finder.
inline MH_STATUS Prepare(const shield::NativeAccess& a){
    if(created[0]||created[1]||created[2]||ever_enabled)return MH_ERROR_ALREADY_CREATED;
    if(!Qualify(a))return MH_ERROR_NOT_EXECUTABLE;
    runtime={};runtime.player_slot=reinterpret_cast<void* volatile*>(a.image_base+a.code.player_slot_rva);
    runtime.api={reinterpret_cast<Begin>(a.image_base+0x27E5F0),reinterpret_cast<End>(a.image_base+0x27E640),reinterpret_cast<Set>(a.image_base+0x25CF80),reinterpret_cast<Move>(a.image_base+0x25CFD0)};
    targets[0]=reinterpret_cast<void*>(a.image_base+0x3632E0);targets[1]=reinterpret_cast<void*>(a.image_base+0x352CE0);targets[2]=reinterpret_cast<void*>(a.image_base+0x261D60);
    void* detours[3]={reinterpret_cast<void*>(Normal),reinterpret_cast<void*>(Event),reinterpret_cast<void*>(Core)};
    void** originals[3]={reinterpret_cast<void**>(&normal_original),reinterpret_cast<void**>(&event_original),reinterpret_cast<void**>(&core_original)};
    for(unsigned i=0;i<3;++i){const auto s=MH_CreateHook(targets[i],detours[i],originals[i]);if(s!=MH_OK){const auto cleanup=ReleasePrepared();return cleanup==MH_OK?s:cleanup;}created[i]=true;}
    return MH_OK;
}
// Only observation is enabled here. Candidate admission requires an explicit
// game-thread decide callback and runtime.enabled; no key or damage wiring.
inline MH_STATUS Observe(){
    for(unsigned i=0;i<3;++i)if(!created[i])return MH_ERROR_NOT_CREATED;
    for(unsigned i=0;i<3;++i){const auto s=MH_QueueEnableHook(targets[i]);if(s!=MH_OK)return s;}
    ever_enabled=true;return MH_ApplyQueued();
}
// Call only on the bound game thread. Active action cancellation must first
// run through its next live handler (runtime.enabled=false), never a stale owner.
inline MH_STATUS StopObservation(){
    if(thread&&thread!=static_cast<LONG>(GetCurrentThreadId()))return MH_ERROR_ENABLED;
    if(runtime.active||runtime.busy)return MH_ERROR_ENABLED;
    runtime.enabled=false;
    for(unsigned i=0;i<3;++i)if(created[i]){const auto s=MH_QueueDisableHook(targets[i]);if(s!=MH_OK)return s;}
    return MH_ApplyQueued();
}
// Once enabled, no removal/uninitialize API is exposed: stopping transport is
// not a quiescence proof. DLL, trampolines and candidate buffers must stay live.
}

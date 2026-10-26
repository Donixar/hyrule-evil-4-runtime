#pragma once
#include "chapter_reader.h"
#include <windows.h>
#include <intrin.h>
#include "MinHook.h"
namespace link_runtime::chapter {
struct Config {bool enabled=false,trace=false;unsigned limit=100;bool master_from_castle=false;};
struct LifecycleGate {
    volatile LONG owner=0,busy=0;
    bool TryReserve() {return InterlockedCompareExchange(&busy,1,0)==0;}
    void Release() {InterlockedExchange(&busy,0);}
    // Both checks and transfers require the same reservation as snapshot/CAS.
    // A fresh TLS incarnation cannot inherit permission from a recycled TID.
    bool Allows(uint32_t thread,bool attributed) {
        return attributed&&thread&&static_cast<uint32_t>(InterlockedCompareExchange(&owner,0,0))==thread;
    }
    bool Attribute(uint32_t thread,bool& attributed) {
        if(!thread)return false;
        InterlockedExchange(&owner,static_cast<LONG>(thread));attributed=true;return true;
    }
};
struct TraceRecord {
    ReadEvent event{};ChapterSnapshot snapshot{};ChapterPlan plan{};
    uint32_t thread=0,effective_type=0,effective_costume=0;
    unsigned original_calls=0;bool completed=false,applied=false,attributed=false;
};
inline bool ExactOne(const wchar_t* value) {return value&&value[0]==L'1'&&value[1]==0;}
// Explicit save/restore (no destructor across native SEH). Diagnostics have only
// bounded integer work; retain native x87 stack, control/status and all XMM/MXCSR.
struct alignas(16) NativeState {
    unsigned char fp[512];DWORD error;unsigned char padding[12];
    void Save() {__asm {mov eax,this
        fxsave [eax]}
        error=GetLastError();}
    void Restore() {__asm {mov eax,this
        fxrstor [eax]}
        SetLastError(error);}
};
template<class Host> uint8_t RunCostume(Host& host,const Config& config,LifecycleGate& gate,uint32_t thread,bool nested,bool attributed) {
    const uint8_t native=host.OriginalCostume(); // Never inside an exception/retry handler.
    NativeState state;state.Save();uint8_t result=native;
    TraceRecord record;record.thread=thread;record.original_calls=1;record.completed=true;
    __try {
        if(!nested&&gate.TryReserve()) {
            __try {
                if(gate.Allows(thread,attributed)) {
                    record.attributed=true;
                    if(host.ReadSnapshot(record.event,record.snapshot)) {
                        record.plan=PlanChapter(record.snapshot,config.enabled,config.master_from_castle);
                        if(record.plan.apply&&host.Compare64(record.snapshot.global+0x4fc8,record.plan.expected,record.plan.desired)) {
                            record.applied=true;result=record.plan.costume;
                        }
                    }
                }
            }
            __finally {gate.Release();}
        }
        record.effective_costume=result;
        if(config.trace&&!nested)host.Record(record);
    }
    __finally {state.Restore();}
    return result;
}
// Keep the aligned FXSAVE allocation out of this SEH frame. MSVC x86 can use
// the argument-frame EBX without restoring it in nested aligned-frame funclets.
template<class Host> __declspec(noinline) void RunPlayerReadSaved(Host& host,const ReadEvent& event,const Config& config,
    LifecycleGate& gate,uint32_t thread,bool nested,bool& attributed,NativeState& state) {
    TraceRecord record;record.event=event;record.thread=thread;
    uint32_t costume=event.costume;
    // Each qualified room-init may establish a new lifecycle owner. Reserve
    // before reading: no previously sampled snapshot can authorize a transfer.
    __try {
        if(!nested&&thread&&event.entry==Entry::room_loader&&gate.TryReserve()) {
            __try {
                if(host.ReadSnapshot(event,record.snapshot)&&record.snapshot.valid&&gate.Attribute(thread,attributed)) {
                    record.attributed=true;record.plan=PlanChapter(record.snapshot,config.enabled,config.master_from_castle);
                    if(record.plan.apply&&host.Compare64(record.snapshot.global+0x4fc8,record.plan.expected,record.plan.desired)) {
                        record.applied=true;costume=record.plan.costume;
                    }
                }
            }
            __finally {gate.Release();}
        }
    }
    __finally {state.Restore();}
    host.OriginalRead(event.type,costume); // Outside reservation; exactly once, native may throw.
    state.Save();record.original_calls=1;record.completed=true;
    record.effective_type=event.type;record.effective_costume=costume;
    __try {if(config.trace&&!nested)host.Record(record);}
    __finally {state.Restore();}
}
template<class Host> void RunPlayerRead(Host& host,const ReadEvent& event,const Config& config,LifecycleGate& gate,uint32_t thread,bool nested,bool& attributed) {
    NativeState state;state.Save();RunPlayerReadSaved(host,event,config,gate,thread,nested,attributed,state);
}
}
namespace link_runtime::chapter::runtime {
using Costume=uint8_t(__cdecl*)();using PlayerRead=void(__cdecl*)(uint32_t,uint32_t);
inline Costume original_costume=nullptr;inline PlayerRead original_read=nullptr;
inline Config config;inline NativeCode code;inline LifecycleGate gate;inline volatile LONG active=0;
inline __declspec(thread) bool entered=false,attributed=false;
inline bool Enabled() {return InterlockedCompareExchange(&active,0,0)==1;}
// TLS is trivially destructible and unwinds through native SEH without catching it.
template<class Host> __declspec(noinline) uint8_t CostumeCallback() {
    Host host;if(!Enabled()||entered)return host.OriginalCostume();
    NativeState state;state.Save();entered=true;const uint32_t thread=GetCurrentThreadId();state.Restore();
    uint8_t result=0;
    __try {result=RunCostume(host,config,gate,thread,false,attributed);}
    __finally {entered=false;}
    return result;
}
template<class Host> __declspec(noinline) void ReadCallback(const ReadEvent& event) {
    Host host;if(!Enabled()||entered) {host.OriginalRead(event.type,event.costume);return;}
    NativeState state;state.Save();entered=true;const uint32_t thread=GetCurrentThreadId();state.Restore();
    __try {RunPlayerRead(host,event,config,gate,thread,false,attributed);}
    __finally {entered=false;}
}
struct Memory {
    bool Read(uint32_t at,void* out,size_t n) {
        if(at<0x10000||n>0x7fff0000U-at||at>=0x7fff0000U)return false;
        __try {std::memcpy(out,reinterpret_cast<void*>(at),n);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
};
// Bounded, copied records; callback never allocates, logs or waits for a lock.
inline SRWLOCK queue_lock=SRWLOCK_INIT;
inline std::array<TraceRecord,64> queue{};inline unsigned head=0,count=0,remaining=0,dropped=0;
inline void Enqueue(const TraceRecord& record) {
    if(!TryAcquireSRWLockExclusive(&queue_lock))return;
    if(remaining) {--remaining;if(count<queue.size()) {queue[(head+count)%queue.size()]=record;++count;}else ++dropped;}
    ReleaseSRWLockExclusive(&queue_lock);
}
inline bool Pop(TraceRecord& record) {
    if(!TryAcquireSRWLockExclusive(&queue_lock))return false;
    const bool available=count!=0;if(available) {record=queue[head];head=(head+1)%queue.size();--count;}
    ReleaseSRWLockExclusive(&queue_lock);return available;
}
struct Host {
    uint8_t OriginalCostume() {return original_costume();}
    void OriginalRead(uint32_t t,uint32_t c) {original_read(t,c);}
    bool ReadSnapshot(const ReadEvent& e,ChapterSnapshot& snapshot) {Memory memory;return chapter::ReadSnapshot(memory,code,e,snapshot);}
    bool Compare64(uint32_t at,uint64_t expected,uint64_t desired) {
        if(at!=code.image_base+code.global_object_rva+0x4fc8||(at&7))return false;
        __try {return static_cast<uint64_t>(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(at),
            static_cast<LONG64>(desired),static_cast<LONG64>(expected)))==expected;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    void Record(const TraceRecord& record) {Enqueue(record);}
};
__declspec(noinline) inline uint8_t __cdecl CostumeDetour() {return CostumeCallback<Host>();}
__declspec(noinline) inline void __cdecl ReadDetour(uint32_t type,uint32_t costume) {
    const ReadEvent event{Entry::room_loader,reinterpret_cast<uint32_t>(_ReturnAddress()),type,costume};ReadCallback<Host>(event);
}
inline bool QualifyTrampoline(void* target,void* trampoline,unsigned bytes) {
    if(!target||!trampoline||(bytes!=5&&bytes!=9))return false;
    Memory memory;unsigned char original[9]{},copy[14]{};
    if(!memory.Read(reinterpret_cast<uint32_t>(target),original,bytes)||
       !memory.Read(reinterpret_cast<uint32_t>(trampoline),copy,bytes+5)||std::memcmp(original,copy,bytes)||copy[bytes]!=0xE9)return false;
    return reinterpret_cast<uint32_t>(trampoline)+bytes+5+detail::U32(copy+bytes+1)==reinterpret_cast<uint32_t>(target)+bytes;
}
struct HookPair {
    void* selector=nullptr;void* loader=nullptr;bool owns_selector=false,owns_loader=false,enable_attempted=false;
    void Rollback() {
        InterlockedExchange(&active,0);
        // Never touch MH_ALL_HOOKS or uninitialize a shared MinHook instance.
        if(owns_selector) {MH_DisableHook(selector);if(!enable_attempted) {MH_RemoveHook(selector);owns_selector=false;}}
        if(owns_loader) {MH_DisableHook(loader);if(!enable_attempted) {MH_RemoveHook(loader);owns_loader=false;}}
        // An enable failure can leave in-flight native calls. Once enable was attempted,
        // retain disabled trampolines until process exit rather than free executable memory
        // under those callers. Reinitialization is then forbidden by the ownership flags.
    }
    MH_STATUS Install(void* selector_target,void* loader_target,void* selector_detour,void* loader_detour,
                      decltype(&MH_EnableHook) enable=&MH_EnableHook) {
        if(owns_selector||owns_loader||Enabled())return MH_ERROR_ALREADY_CREATED;
        selector=selector_target;loader=loader_target;
        MH_STATUS status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return status;
        status=MH_CreateHook(selector,selector_detour,reinterpret_cast<void**>(&original_costume));
        if(status!=MH_OK)return status;owns_selector=true;
        status=MH_CreateHook(loader,loader_detour,reinterpret_cast<void**>(&original_read));
        if(status!=MH_OK) {Rollback();return status;}owns_loader=true;
        if(!QualifyTrampoline(selector,reinterpret_cast<void*>(original_costume),5)||
           !QualifyTrampoline(loader,reinterpret_cast<void*>(original_read),9)) {Rollback();return MH_ERROR_UNSUPPORTED_FUNCTION;}
        enable_attempted=true;status=enable(selector);if(status==MH_OK)status=enable(loader);
        if(status!=MH_OK) {Rollback();return status;}
        InterlockedExchange(&active,1);return MH_OK;
    }
};
inline HookPair hooks;
inline MH_STATUS Start(const NativeCode& native,const Config& settings) {
    if((!settings.enabled&&!settings.trace)||!detail::Complete(native))return MH_ERROR_NOT_EXECUTABLE;
    if(Enabled()||hooks.owns_selector||hooks.owns_loader)return MH_ERROR_ALREADY_CREATED;
    code=native;config=settings;remaining=settings.limit>1000?1000:settings.limit;
    return hooks.Install(reinterpret_cast<void*>(code.image_base+code.selector_rva),reinterpret_cast<void*>(code.image_base+code.loader_rva),
        reinterpret_cast<void*>(&CostumeDetour),reinterpret_cast<void*>(&ReadDetour));
}
}

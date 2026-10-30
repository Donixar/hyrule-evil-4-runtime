#pragma once
#include "parry_sword_reader.h"
#include "parry_pre_effects.h"
#include "thread_binding.h"
#include "MinHook.h"

// Native player-update boundary transport. No damage Decision or installation.
namespace link_runtime::parry::sword::epoch {
namespace reader=link_runtime::parry::sword::reader;
using shield::NativeAccess;
struct Code {bool verified=false;uint32_t entry=0,tail=0;};
struct Fixup {unsigned offset,rva;};
inline constexpr Fixup kPlayerFixups[]={
    {0x4,0x806f3c},{0x10,0x806f3c},{0x27,0x806f3c},{0x37,0x806f3c},{0x42,0x806f3c},
    {0x51,0x806f3c},{0x60,0x806f3c},{0x6f,0x806f3c},{0x7e,0x806f3c},{0x8e,0x806f3c},
    {0x99,0x806f3c},{0xa8,0x806f3c},{0xb7,0x806f3c},{0xc2,0x806f3c},{0xf4,0x806f3c},
    {0x103,0x806f3c},{0x237,0x806f3c},{0x25b,0x862f08},{0x275,0xe224b0},{0x27c,0x870304},
    {0x289,0x862f10},{0x2cb,0x81bcb4},{0x2fc,0x70fb50},{0x33b,0xe224b0},{0x384,0xe224b0},
    {0x3ea,0x7f9a7c},{0x495,0x870548}};
inline bool Fingerprint(const unsigned char* image,const NativeAccess& a,uint32_t start,unsigned size,
                        const char* sha,const Fixup* fixups,size_t count) {
    if(size>2048||!shield::Area(a.regions.data(),a.regions.size(),start,size,true))return false;
    unsigned char normalized[2048]{};std::memcpy(normalized,image+start,size);
    for(size_t i=0;i<count;++i){const auto& f=fixups[i];
        if(f.offset>size-4||reader::U32(normalized+f.offset)!=uint64_t(a.image_base)+f.rva)return false;
        const uint32_t preferred=0x400000+f.rva;std::memcpy(normalized+f.offset,&preferred,4);
    }
    reader::Hasher hash;return hash.Add(normalized,size)&&hash.Matches(sha);
}
inline Code Find(const unsigned char* image,size_t size,const NativeAccess& a) {
    if(!image||size!=a.image_size||!shield::Complete(a)||a.code.player_slot_rva!=0x857054||a.code.global_slot_rva!=0x806f3c)return {};
    const Fixup cookie{7,0x835e10};
    if(!Fingerprint(image,a,0x364d20,1246,"c60e4d7e064a2dd6f10adfb270d39fb947e4f4496b725542f34ba8f22189950d",kPlayerFixups,std::size(kPlayerFixups))||
       !Fingerprint(image,a,0x25edb0,202,"8889fa6d677e195e61ee3c0d68a7bdc50519f6974af4f8fae60ed186e8e4b5ff",&cookie,1)||
       !Fingerprint(image,a,0x283870,17,"dcf98caa49f8342a6ad5d18e88b34c45f2435b53e53eaee09e98cb9e9d2e7901",nullptr,0))return {};
    for(const auto& pair:std::initializer_list<std::pair<uint32_t,uint32_t>>{{0x5da3,0x25bed0},{0x49ee,0x25c680},{0x68bb,0x25edb0},{0xcced,0x1955b0},{0xc635,0x283870}}){
        size_t destination=0;
        if(!shield::Area(a.regions.data(),a.regions.size(),pair.first,5,true)||!shield::Relative(image,size,pair.first,0xe9,destination)||destination!=pair.second)return {};
    }
    return {true,0x364d20,0x3651f0};
}
struct Stamp {uint32_t player=0,global=0,frame=0;};
inline bool Same(const Stamp& a,const Stamp& b){return a.player==b.player&&a.global==b.global&&a.frame==b.frame;}
// Rejection provenance only. Admission still depends on the existing epoch guards.
enum class Failure:uint32_t {None,Entry,Nested,OrphanTail,TailGuard,TailRead,TailChanged,ContactContext,ContactRead,ContactChanged,Exception};
struct State {
    Failure failure=Failure::None;reader::ReadFailure reader_failure=reader::ReadFailure::None;
    Stamp stamp{};reader::Observation pose{};
    uint64_t serial=0;
    unsigned depth=0;
    bool poisoned=false,ready=false,sampling=false;
};
inline __declspec(thread) State state;
// Configure before Start; callback/module remains resident after disabling.
// Called on the bound game thread after each tail, including failed captures.
inline void(__cdecl* after_tail)()=nullptr;
// After entry validation, before the original body. Zero values invalidate a
// nested/invalid entry; callers re-read the supplied native values themselves.
inline void(__cdecl* before_entry)(uint32_t,uint32_t,uint32_t)=nullptr;
inline __declspec(thread) uint32_t seen_lifecycle=0;
inline volatile LONG active=0,lifecycle=0,owner_thread=0;
// skills-fix-01 (thread_binding.h): owner_thread's exit handle. The game can replace its game thread (after the title
// screen); the new one takes owner_thread over only once the bound one has exited. Reset wherever owner_thread is reset.
inline thread_binding::Binding binding;
inline NativeAccess access;
inline void* original_entry=nullptr;
inline void* original_tail=nullptr;
inline bool Enabled(){return InterlockedCompareExchange(&active,0,0)==1;}
inline void Sync(){const auto version=static_cast<uint32_t>(InterlockedCompareExchange(&lifecycle,0,0));if(version!=seen_lifecycle){state={};seen_lifecycle=version;}}
inline reader::Slots Slots(){return {access.image_base+static_cast<uint32_t>(access.code.player_slot_rva),access.image_base+static_cast<uint32_t>(access.code.global_slot_rva)};}
inline bool ReadStamp(Stamp& out) {
    out={};reader::ProcessMemory memory;const auto slots=Slots();uint32_t p=0,g=0,frame=0;
    if(!memory.Read(slots.player,&out.player,4)||!reader::Pointer(out.player)||!memory.Read(slots.global,&out.global,4)||!reader::Pointer(out.global)||
       !memory.Read(out.global+0x530c,&out.frame,4)||!memory.Read(slots.player,&p,4)||!memory.Read(slots.global,&g,4)||
       !memory.Read(out.global+0x530c,&frame,4))return false;
    return out.player==p&&out.global==g&&out.frame==frame;
}
#ifdef LINK_SWORD_EPOCH_FIXTURE
void FixtureDuringCapture();
#endif
inline bool Capture(reader::Observation& out,reader::ReadFailure* failure=nullptr){
#ifdef LINK_SWORD_EPOCH_FIXTURE
    FixtureDuringCapture();
#endif
    return reader::ObserveCurrent(Slots(),out,failure);
}
// Contact re-read of the pose this epoch's tail already proved (resource hashed there): the same walk and matrices,
// the resource accepted by identity (reader::ReadPose, no bcrypt). SamePose below still compares the whole pose.
inline bool Recapture(const reader::Observation& known,reader::Observation& out,reader::ReadFailure* failure=nullptr){
#ifdef LINK_SWORD_EPOCH_FIXTURE
    FixtureDuringCapture();
#endif
    reader::ProcessMemory memory;return reader::ReadPose(memory,Slots(),reader::IdentityOf(known),out,failure);
}
// Entry/tail callbacks receive actual PUSHAD frames. They never accept an
// external frame-valid or pose-ready boolean.
__declspec(noinline) inline void __cdecl EntryRecord(const parry::MachineFrame* frame){
    const DWORD error=GetLastError();
    if(Enabled()){
        if(thread_binding::ClaimLatch(owner_thread,binding)){Sync();
            __try {
                ++state.serial;state.ready=false;
                if(state.depth){state.failure=Failure::Nested;state.poisoned=true;if(state.depth<0xffff)++state.depth;}
                else {
                    state.pose={};state.failure=Failure::None;state.reader_failure=reader::ReadFailure::None;state.poisoned=false;state.depth=1;
                    if(!frame||!ReadStamp(state.stamp)||frame->ecx!=state.stamp.player){state.failure=Failure::Entry;state.poisoned=true;}
                }
                if(before_entry) {
                    if(state.poisoned)before_entry(0,0,0);
                    else before_entry(frame->ecx,state.stamp.global,state.stamp.frame);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER){
                state.failure=Failure::Exception;state.poisoned=true;state.ready=false;
                __try {if(before_entry)before_entry(0,0,0);} __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }
    SetLastError(error);
}
__declspec(noinline) inline void __cdecl Tail(const parry::MachineFrame* frame){
    const DWORD error=GetLastError();
    if(Enabled()&&InterlockedCompareExchange(&owner_thread,0,0)==static_cast<LONG>(GetCurrentThreadId())){
        Sync();
        __try {
            if(!state.depth){state.failure=Failure::OrphanTail;state.ready=false;state.poisoned=true;}
            else if(state.depth>1){state.failure=Failure::Nested;--state.depth;state.ready=false;state.poisoned=true;}
            else {
                Stamp before{},after{};reader::Observation pose;state.ready=false;
                if(state.poisoned||state.sampling||!frame||frame->esi!=state.stamp.player||!ReadStamp(before)||!Same(before,state.stamp)){if(state.failure==Failure::None)state.failure=Failure::TailGuard;state.poisoned=true;}
                else {
                    state.sampling=true;const auto serial=state.serial;
                    const bool read=Capture(pose,&state.reader_failure);
                    if(read&&Enabled()&&seen_lifecycle==static_cast<uint32_t>(InterlockedCompareExchange(&lifecycle,0,0))&&!state.poisoned&&serial==state.serial&&state.depth==1&&ReadStamp(after)&&Same(after,before)&&pose.player==before.player&&pose.global==before.global){state.pose=pose;state.failure=Failure::None;state.ready=true;}
                    else {if(state.failure==Failure::None)state.failure=read?Failure::TailChanged:Failure::TailRead;state.poisoned=true;}
                    state.sampling=false;
                }
                state.depth=0;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){state.failure=Failure::Exception;state.ready=false;state.poisoned=true;state.sampling=false;state.depth=0;}
        __try {if(after_tail)after_tail();}
        __except(EXCEPTION_EXECUTE_HANDLER) {} // Diagnostics never escape into native motion.
    }
    SetLastError(error);
}
inline bool SamePose(const reader::Observation& a,const reader::Observation& b){
    return a.player==b.player&&a.global==b.global&&a.info==b.info&&a.resource==b.resource&&a.part0==b.part0&&a.part10==b.part10&&a.profile==b.profile&&std::memcmp(&a.pose,&b.pose,sizeof(a.pose))==0;
}
// Callable by the native incoming-classifier thread context. Returns a completed
// player-update observation only; later writers/task-order qualification remain
// separate from this transport and Observation::ContactQualified stays false.
inline bool CurrentImpl(reader::Observation& out,Stamp* contact_stamp=nullptr){
    out={};if(contact_stamp)*contact_stamp={};const DWORD error=GetLastError();bool result=false;
    if(Enabled()&&InterlockedCompareExchange(&owner_thread,0,0)==static_cast<LONG>(GetCurrentThreadId())){
        Sync();
        __try {
            Stamp before{},after{};
            const bool read_stamp=ReadStamp(before);
            const bool completed=read_stamp&&Same(before,state.stamp);
            // Enemy processing precedes the ordinary player update. At that
            // contact only, allow a fresh read of the unchanged completed pose
            // across exactly one forward frame. Never rewrite its tail stamp.
            const bool next_contact=contact_stamp&&read_stamp&&before.player==state.stamp.player&&
                before.global==state.stamp.global&&before.frame>state.stamp.frame&&before.frame-state.stamp.frame==1;
            if(state.ready&&!state.depth&&!state.poisoned&&!state.sampling&&(completed||next_contact)){
                const auto serial=state.serial;reader::Observation now;state.sampling=true;
                const bool read=Recapture(state.pose,now,&state.reader_failure);state.sampling=false;
                if(read&&Enabled()&&seen_lifecycle==static_cast<uint32_t>(InterlockedCompareExchange(&lifecycle,0,0))&&state.ready&&!state.poisoned&&!state.depth&&serial==state.serial&&ReadStamp(after)&&Same(before,after)&&SamePose(now,state.pose)){out=now;state.failure=Failure::None;if(contact_stamp)*contact_stamp=after;result=true;}
                else if(state.failure==Failure::None)state.failure=read?Failure::ContactChanged:Failure::ContactRead;
            }
            if(!result){if(state.failure==Failure::None)state.failure=Failure::ContactContext;state.ready=false;}
        } __except(EXCEPTION_EXECUTE_HANDLER){state.failure=Failure::Exception;state.ready=false;state.poisoned=true;state.sampling=false;}
    }
    SetLastError(error);return result;
}
// The incoming native caller may also have a full x87 stack/nondefault rounding.
// Preserve it while giving the geometry calculation a clean math environment.
inline bool ReadCurrent(reader::Observation& out,Stamp* contact_stamp){
    __declspec(align(16)) unsigned char fp[512];parry::SaveFP(fp);
    const uint32_t mxcsr=0x1f80;__asm {fninit
        ldmxcsr mxcsr}
    bool result=false;
    __try {result=CurrentImpl(out,contact_stamp);}
    __finally {parry::RestoreFP(fp);}
    return result;
}
inline bool Current(reader::Observation& out){return ReadCurrent(out,nullptr);}
// Diagnostic geometry read; does not qualify input, owner, contact or defense.
inline bool AtContact(reader::Observation& out,Stamp& sampled){return ReadCurrent(out,&sampled);}
__declspec(naked) inline void EntryDetour(){__asm {
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
    call EntryRecord
    add esp,4
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    jmp dword ptr [original_entry]
}}
__declspec(naked) inline void TailDetour(){__asm {
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
    call Tail
    add esp,4
    fxrstor [esp]
    mov esp,ebx
    popad
    popfd
    jmp dword ptr [original_tail]
}}
struct Operations {
    decltype(&MH_Initialize) initialize=&MH_Initialize;decltype(&MH_Uninitialize) uninitialize=&MH_Uninitialize;
    decltype(&MH_CreateHook) create=&MH_CreateHook;decltype(&MH_EnableHook) enable=&MH_EnableHook;
    decltype(&MH_DisableHook) disable=&MH_DisableHook;decltype(&MH_RemoveHook) remove=&MH_RemoveHook;
};
inline Operations operations;
inline void* targets[2]{};
inline bool created[2]{},ever_enabled[2]{},disabled[2]{},owns_minhook=false;
inline MH_STATUS Stop(){
    InterlockedExchange(&active,0);InterlockedIncrement(&lifecycle);
    MH_STATUS failure=MH_OK;
    for(unsigned i=0;i<2;++i)if(created[i]&&ever_enabled[i]&&!disabled[i]){
        const auto status=operations.disable(targets[i]);
        if(status==MH_OK||status==MH_ERROR_DISABLED)disabled[i]=true;else if(failure==MH_OK)failure=status;
    }
    if(ever_enabled[0]||ever_enabled[1])return failure; // Retain both trampolines; disabling is not quiescence.
    for(unsigned i=0;i<2;++i)if(created[i]){
        const auto status=operations.remove(targets[i]);if(status==MH_OK)created[i]=false;else if(failure==MH_OK)failure=status;
    }
    if(failure!=MH_OK)return failure;
    if(owns_minhook){const auto status=operations.uninitialize();if(status!=MH_OK)return status;owns_minhook=false;}
    targets[0]=targets[1]=original_entry=original_tail=nullptr;access={};InterlockedExchange(&owner_thread,0);binding.Reset();return MH_OK;
}
inline MH_STATUS Start(const NativeAccess& a,bool enabled=false,const Operations& op={}){
    if(targets[0]||targets[1]||created[0]||created[1]||Enabled())return MH_ERROR_ALREADY_CREATED;
    if(!enabled)return MH_ERROR_NOT_EXECUTABLE;
    const auto code=Find(reinterpret_cast<const unsigned char*>(a.image_base),a.image_size,a);
    if(!code.verified)return MH_ERROR_NOT_EXECUTABLE;
    access=a;operations=op;const auto initialized=operations.initialize();owns_minhook=initialized==MH_OK;
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return initialized;
    targets[0]=reinterpret_cast<void*>(a.image_base+code.entry);targets[1]=reinterpret_cast<void*>(a.image_base+code.tail);
    void* detours[2]={reinterpret_cast<void*>(&EntryDetour),reinterpret_cast<void*>(&TailDetour)};void** originals[2]={&original_entry,&original_tail};
    for(unsigned i=0;i<2;++i){const auto status=operations.create(targets[i],detours[i],originals[i]);if(status!=MH_OK){Stop();return status;}created[i]=true;}
    // Tail first: until both hooks are enabled, active remains false.
    for(unsigned i:{1U,0U}){const auto status=operations.enable(targets[i]);if(status!=MH_OK){Stop();return status;}ever_enabled[i]=true;}
    InterlockedExchange(&owner_thread,0);binding.Reset();InterlockedIncrement(&lifecycle);InterlockedExchange(&active,1);return MH_OK;
}
} // namespace link_runtime::parry::sword::epoch


#pragma once
#include "link_defense_policy.h"
#include "parry_pre_effects.h"
#include "thread_binding.h"
#include "MinHook.h"
#include <atomic>

// Game-thread logical input transport only. No damage, contact or attack admission.
namespace link_runtime::parry::input {
using namespace link_runtime::shield;
struct Config {bool enabled=false;uint32_t window_ms=100;}; // Engineering default, not balance evidence.
inline bool Duration(uint32_t value) {return value>=1&&value<=1000;}
struct Sample {
    bool valid=false,gameplay=false;
    uint32_t player=0,global=0,frame=0,room=0,weapon=0,character=0,kind=0;
    uint64_t on=0,trg=0;
};
inline bool Context(const Sample& a,const Sample& b) {
    return a.player==b.player&&a.global==b.global&&a.room==b.room&&a.weapon==b.weapon&&a.character==b.character&&a.kind==b.kind;
}
inline bool Coherent(const Sample& p) {return !(p.trg&0x800)||bool(p.on&0x800);}
// Published by the ocarina session (Session::BlocksParry) on the game thread. While true no
// knife press arms or consumes a parry window; the state is dropped, so a button still held
// after stowing is not a fresh press (the next real press arms as usual).
inline std::atomic<bool> ocarina_blocks{false};
inline void SetOcarinaBlocks(bool blocks) {ocarina_blocks.store(blocks,std::memory_order_release);}
inline bool OcarinaBlocks() {return ocarina_blocks.load(std::memory_order_acquire);}
// Published by the contextual Mortal Draw (combat runtime) while its own action owns Link:
// same effect as the ocarina gate. The F8 pilot never sets it.
inline std::atomic<bool> mortal_blocks{false};
inline void SetMortalBlocks(bool blocks) {mortal_blocks.store(blocks,std::memory_order_release);}
inline bool Blocks() {return OcarinaBlocks()||mortal_blocks.load(std::memory_order_acquire);}
struct State {
    defense::ParryWindow window;
    Sample sample;
    uint64_t time=0;
    uint32_t duration=0;
    bool known=false;
};
inline void Begin(State& s,const Sample& p,uint64_t now,uint32_t duration) {
    s={};s.sample=p;s.time=now;s.duration=duration;
    s.known=p.valid&&p.gameplay&&Coherent(p)&&Duration(duration);
    if(s.known) {
        defense::UpdateParryButton(s.window,bool(p.on&0x800),false,now,duration);
        if(!(p.on&0x800))defense::UpdateParryButton(s.window,false,true,now,duration);
    }
}
inline void Update(State& s,const Sample& p,uint64_t now,uint32_t duration) {
    if(Blocks()) {s={};return;}
    if(!p.valid||!p.gameplay||!Coherent(p)||!Duration(duration)||!s.known||now<s.time||
       !Context(s.sample,p)||duration!=s.duration||
       (p.frame!=s.sample.frame&&p.frame!=s.sample.frame+1U)||
       (p.frame==s.sample.frame&&(p.on!=s.sample.on||p.trg!=s.sample.trg))) {
        Begin(s,p,now,duration);return;
    }
    const bool down=bool(p.on&0x800);
    if(down&&!s.window.down&&!(p.trg&0x800)) {
        // A held value alone is not a fresh logical press (including remapping).
        Begin(s,p,now,duration);return;
    }
    defense::UpdateParryButton(s.window,down,true,now,duration);
    s.sample=p;s.time=now;
}
inline bool Consume(State& s,const Sample& fresh,uint64_t now,bool other_gates_qualified) {
    if(Blocks()) {s={};return false;}
    if(!s.known||!fresh.valid||!fresh.gameplay||!Context(s.sample,fresh)||fresh.frame!=s.sample.frame||
       !Coherent(fresh)||fresh.on!=s.sample.on||fresh.trg!=s.sample.trg||!(fresh.on&0x800)||now<s.time) {s={};return false;}
    return defense::ConsumeParry(s.window,now,other_gates_qualified);
}
struct Raw {Frame frame;uint32_t counter=0;uint16_t room=0;unsigned char weapon=0;};
template<class Memory> bool ReadOnce(Memory& memory,const NativeAccess& a,Raw& out) {
    auto& f=out.frame;
    if(!Word(memory,a.image_base+static_cast<uint32_t>(a.code.player_slot_rva),f.player)||!Range(f.player,0x32E)||
       !Word(memory,a.image_base+static_cast<uint32_t>(a.code.global_slot_rva),f.global)||!Range(f.global,0x5310))return false;
    if(!Bytes(memory,a.image_base+static_cast<uint32_t>(a.code.input_rva),f.input)||
       !Bytes(memory,f.player+0xFC,f.routines)||!Bytes(memory,f.player+0x100,f.kind)||!Bytes(memory,f.player+0x32C,f.damage)||
       !Word(memory,f.global+0x20,f.routine)||!Word(memory,f.global+0x54,f.system)||!Word(memory,f.global+0x170,f.stop)||
       !Word(memory,f.global+0x4FB4,f.health)||!Word(memory,f.global+0x4FC8,f.character)||!Bytes(memory,f.global+0x501C,f.status)||
       !Word(memory,f.global+0x530C,out.counter)||!memory.Read(f.global+0x4FAC,&out.room,2)||!memory.Read(f.global+0x4FC0,&out.weapon,1))return false;
    uint32_t player=0,global=0;
    return Word(memory,a.image_base+static_cast<uint32_t>(a.code.player_slot_rva),player)&&player==f.player&&
        Word(memory,a.image_base+static_cast<uint32_t>(a.code.global_slot_rva),global)&&global==f.global;
}
template<class Memory> bool Read(Memory& memory,const NativeAccess& a,Sample& out) {
    out={};if(!Complete(a))return false;Raw first{},last{};
    if(!ReadOnce(memory,a,first)||!ReadOnce(memory,a,last)||!Equal(first.frame,last.frame)||
       first.counter!=last.counter||first.room!=last.room||first.weapon!=last.weapon)return false;
    const auto& f=first.frame;
    out={true,Gameplay(f)&&(f.character&0xFF)==0&&f.kind[0]==0&&f.damage[0]==0&&f.damage[1]==0,
         f.player,f.global,first.counter,first.room,first.weapon,f.character,U16(f.kind.data()),
         uint64_t(U32(f.input.data()))|(uint64_t(U32(f.input.data()+4))<<32),
         uint64_t(U32(f.input.data()+8))|(uint64_t(U32(f.input.data()+12))<<32)};
    return true;
}

struct InputCode {bool verified=false;uint32_t site_rva=0;};
namespace fingerprint {
struct Fixup {uint32_t offset,rva;};
inline unsigned Hex(char c) {return c<='9'?unsigned(c-'0'):unsigned(c-'A'+10);}
template<size_t N> bool Window(const unsigned char* image,const NativeAccess& a,uint32_t at,const char (&hex)[N],std::initializer_list<Fixup> fixups) {
    const size_t length=(N-1)/2;
    if(!Area(a.regions.data(),a.regions.size(),at,length,true))return false;
    for(size_t i=0;i<length;++i) {
        bool relocated=false;
        for(const auto& f:fixups)if(i==f.offset) {
            if(i+4>length||U32(image+at+i)!=uint64_t(a.image_base)+f.rva)return false;
            relocated=true;i+=3;break;
        }
        if(!relocated&&image[at+i]!=((Hex(hex[i*2])<<4)|Hex(hex[i*2+1])))return false;
    }
    return true;
}
}
inline InputCode FindInputCode(const unsigned char* image,size_t size,const NativeAccess& a) {
    if(!image||size!=a.image_size||!Complete(a)||a.code.global_slot_rva!=0x806F3C||a.code.input_rva!=0x862F08)return {};
    if(!fingerprint::Window(image,a,0x254b80,"E88429DBFFE8D073DBFF6870D1B1006A04E8EBF2DAFF83C418E84333DBFFE8A5F4DAFFA13C6FC000FF800C5300008B0D3C6FC0008B8128500000A900100000741DA900040000751681A128500000FFEFFFFFA13C6FC000816054FFFBFFFFE88655DBFF",{{0xb,0x71d170},{0x24,0x806f3c},{0x30,0x806f3c},{0x53,0x806f3c}}))return {};
    if(!fingerprint::Window(image,a,0x56a4f0,"8B1D002FC6008B15042FC6008BC333C18BF023F133D78935102FC6008BF22315042FC60023F723C333DB8935142FC600A3182FC60089151C2FC600890D002FC600893D042FC600",{{0x2,0x862f00},{0x8,0x862f04},{0x18,0x862f10},{0x20,0x862f04},{0x2c,0x862f14},{0x31,0x862f18},{0x37,0x862f1c},{0x3d,0x862f00},{0x43,0x862f04}}))return {};
    if(!fingerprint::Window(image,a,0x7509,"E952FE5500",{}))return {};
    if(!fingerprint::Window(image,a,0xa169,"E952BC2B00",{}))return {};

    return {true,0x254BAE};
}
inline __declspec(thread) State state;
inline __declspec(thread) uint32_t seen_epoch=0;
inline __declspec(thread) bool in_tick=false,poisoned=false;
inline volatile LONG active=0,epoch=0,owner_thread=0;
// skills-fix-01 (thread_binding.h): owner_thread's exit handle. The game can replace its game thread (after the title
// screen); the new one takes owner_thread over only once the bound one has exited. Reset wherever owner_thread is reset.
inline thread_binding::Binding binding;
inline void* original_tick=nullptr;
inline NativeAccess access;
inline Config config;
// One resident session, configured before enabling the player-entry transport.
inline bool player_updates=false;
#ifdef LINK_PARRY_INPUT_FIXTURE
uint64_t FixtureNow();
#endif
inline uint64_t Now() {
#ifdef LINK_PARRY_INPUT_FIXTURE
    return FixtureNow();
#else
    return GetTickCount64();
#endif
}
inline bool Enabled() {return InterlockedCompareExchange(&active,0,0)==1;}
struct Memory {
    bool Read(uint32_t at,void* output,size_t length) {
        if(!Range(at,length))return false;
        __try {std::memcpy(output,reinterpret_cast<const void*>(at),length);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
};
inline void SyncEpoch() {
    const auto current=static_cast<uint32_t>(InterlockedCompareExchange(&epoch,0,0));
    if(seen_epoch!=current) {state={};seen_epoch=current;}
}
__declspec(noinline) inline void __cdecl Record(const parry::MachineFrame* frame) {
    const DWORD error=GetLastError();
    if(Enabled()&&!player_updates) {
        if(thread_binding::ClaimLatch(owner_thread,binding)) {
            SyncEpoch();
            if(in_tick) {poisoned=true;state={};}
            else {
                in_tick=true;poisoned=false;
                __try {
                    Sample sample;Memory memory;
                    if(!frame||!Read(memory,access,sample)||sample.global!=frame->eax||poisoned)state={};
                    else {const auto now=Now();if(poisoned)state={};else Update(state,sample,now,config.window_ms);}
                } __except(EXCEPTION_EXECUTE_HANDLER) {state={};}
                in_tick=false;
            }
        }
    }
    SetLastError(error);
}
// Values come from the intercepted player-entry ECX and the epoch stamp. They
// are re-read here; zero values invalidate an invalid/nested epoch entry.
__declspec(noinline) inline void __cdecl RecordPlayerUpdate(uint32_t player,uint32_t global,uint32_t frame) {
    const DWORD error=GetLastError();
    if(Enabled()&&player_updates) {
        if(thread_binding::LatchAdmits(owner_thread,binding)) {
            SyncEpoch();
            if(in_tick) {poisoned=true;state={};}
            else {
                in_tick=true;poisoned=false;
                const auto generation=seen_epoch;
                __try {
                    Sample sample{},after{};Memory memory;
                    if(!player||!global||!Read(memory,access,sample)||sample.player!=player||
                       sample.global!=global||sample.frame!=frame||poisoned)state={};
                    else {
                        const auto now=Now();
                        if(poisoned||!Enabled()||generation!=static_cast<uint32_t>(InterlockedCompareExchange(&epoch,0,0))||
                           !Read(memory,access,after)||!Context(sample,after)||after.frame!=frame||
                           after.gameplay!=sample.gameplay||after.on!=sample.on||after.trg!=sample.trg)state={};
                        else if(thread_binding::ClaimLatch(owner_thread,binding))Update(state,after,now,config.window_ms);
                        else state={};
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {state={};}
                in_tick=false;
            }
        }
    }
    SetLastError(error);
}
// Enemy updates can precede player entry. Refresh only on the already-bound
// player thread, through the same double-read/generation/reentry checks. This
// observes the logical input; it does not consume a press or authorize damage.
inline bool RefreshAtContact(){
    const DWORD error=GetLastError();bool refreshed=false;
    if(Enabled()&&player_updates&&InterlockedCompareExchange(&owner_thread,0,0)==static_cast<LONG>(GetCurrentThreadId())){
        if(in_tick){poisoned=true;state={};}
        else {
            Sample fresh;Memory memory;
            if(Read(memory,access,fresh)){
                RecordPlayerUpdate(fresh.player,fresh.global,fresh.frame);
                refreshed=Enabled()&&state.known&&Context(state.sample,fresh)&&state.sample.frame==fresh.frame&&
                    state.sample.on==fresh.on&&state.sample.trg==fresh.trg;
            } else state={};
        }
    }
    SetLastError(error);return refreshed;
}
// Caller supplies the *other* fully qualified gates. This method checks and
// consumes only the temporal input gate; it never authorizes or cancels damage.
inline bool ConsumeCurrent(bool other_gates_qualified) {
    const DWORD error=GetLastError();bool result=false;
    if(Enabled()&&InterlockedCompareExchange(&owner_thread,0,0)==static_cast<LONG>(GetCurrentThreadId())&&!in_tick) {
        SyncEpoch();in_tick=true;poisoned=false;
        const auto generation=seen_epoch;
        __try {
            const auto now=Now();Sample fresh;Memory memory;
            if(!poisoned&&Enabled()&&generation==static_cast<uint32_t>(InterlockedCompareExchange(&epoch,0,0))&&
               Read(memory,access,fresh))result=Consume(state,fresh,now,other_gates_qualified);
            else state={};
        } __except(EXCEPTION_EXECUTE_HANDLER) {state={};}
        in_tick=false;
    } else state={};
    SetLastError(error);return result;
}
// Tier-2 observe mode: would ConsumeCurrent(true) succeed now? Same thread/epoch/fresh-read
// checks, applied to a copy of the window: never consumes, disarms or poisons the real state.
inline bool PeekCurrent() {
    const DWORD error=GetLastError();bool result=false;
    if(Enabled()&&InterlockedCompareExchange(&owner_thread,0,0)==static_cast<LONG>(GetCurrentThreadId())&&!in_tick&&
       seen_epoch==static_cast<uint32_t>(InterlockedCompareExchange(&epoch,0,0))) {
        __try {
            Sample fresh;Memory memory;
            if(Read(memory,access,fresh)){State copy=state;result=Consume(copy,fresh,Now(),true);}
        } __except(EXCEPTION_EXECUTE_HANDLER) {result=false;}
    }
    SetLastError(error);return result;
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
        push ebx
        call Record
        add esp,4
        fxrstor [esp]
        mov esp,ebx
        popad
        popfd
        jmp dword ptr [original_tick]
    }
}
struct Operations {
    decltype(&MH_Initialize) initialize=&MH_Initialize;
    decltype(&MH_Uninitialize) uninitialize=&MH_Uninitialize;
    decltype(&MH_CreateHook) create=&MH_CreateHook;
    decltype(&MH_EnableHook) enable=&MH_EnableHook;
    decltype(&MH_DisableHook) disable=&MH_DisableHook;
    decltype(&MH_RemoveHook) remove=&MH_RemoveHook;
};
inline Operations operations;
inline void* target=nullptr;
inline bool created=false,ever_enabled=false,disabled=false,owns_minhook=false;
inline MH_STATUS Stop() {
    InterlockedExchange(&active,0);InterlockedIncrement(&epoch);
    if(player_updates)return MH_OK; // Callback may still be in flight: retain metadata.
    if(created&&ever_enabled) {
        if(!disabled) {
            const auto status=operations.disable(target);
            if(status!=MH_OK&&status!=MH_ERROR_DISABLED)return status;
            disabled=true;
        }
        return MH_OK; // Disable is not quiescence: retain original and metadata.
    }
    if(created) {const auto status=operations.remove(target);if(status!=MH_OK)return status;created=false;original_tick=nullptr;}
    if(owns_minhook) {const auto status=operations.uninitialize();if(status!=MH_OK)return status;owns_minhook=false;}
    target=nullptr;access={};config={};InterlockedExchange(&owner_thread,0);binding.Reset();return MH_OK;
}
// Configure before the already-qualified epoch hook is enabled. Owns no hook.
inline MH_STATUS StartPlayerUpdate(const NativeAccess& native,const Config& settings={}) {
    if(player_updates||created||target||Enabled())return MH_ERROR_ALREADY_CREATED;
    if(!settings.enabled||!Duration(settings.window_ms)||!Complete(native)||
       !FindInputCode(reinterpret_cast<const unsigned char*>(native.image_base),native.image_size,native).verified)return MH_ERROR_NOT_EXECUTABLE;
    access=native;config=settings;player_updates=true;
    InterlockedExchange(&owner_thread,0);binding.Reset();InterlockedIncrement(&epoch);InterlockedExchange(&active,1);return MH_OK;
}
// Historical main-thread transport: isolated regression fixture only. It cannot
// provide game-worker TLS and is deliberately unavailable in production builds.
#ifdef LINK_PARRY_LEGACY_MAIN_FIXTURE
inline MH_STATUS Start(const NativeAccess& native,const InputCode& code,const Config& settings={},const Operations& op={}) {
    if(player_updates||created||target||Enabled())return MH_ERROR_ALREADY_CREATED;
    if(!settings.enabled||!Duration(settings.window_ms)||!code.verified||!Complete(native))return MH_ERROR_NOT_EXECUTABLE;
    const auto found=FindInputCode(reinterpret_cast<const unsigned char*>(native.image_base),native.image_size,native);
    if(!found.verified||found.site_rva!=code.site_rva)return MH_ERROR_NOT_EXECUTABLE;
    access=native;config=settings;operations=op;
    const auto initialized=operations.initialize();owns_minhook=initialized==MH_OK;
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return initialized;
    target=reinterpret_cast<void*>(native.image_base+code.site_rva);
    auto status=operations.create(target,reinterpret_cast<void*>(&Detour),&original_tick);
    if(status!=MH_OK) {Stop();return status;}created=true;
    status=operations.enable(target);if(status!=MH_OK) {Stop();return status;}
    ever_enabled=true;InterlockedExchange(&owner_thread,0);binding.Reset();InterlockedIncrement(&epoch);InterlockedExchange(&active,1);return MH_OK;
}
#endif // LINK_PARRY_LEGACY_MAIN_FIXTURE
} // namespace link_runtime::parry::input

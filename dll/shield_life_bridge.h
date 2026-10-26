#pragma once
#include <cstdint>
#include <cstddef>

// Provenance across OUR LifeDown entry wrapper (FairyRevive). Never accepts a
// DLL return just because it is outside the game. Exact call slot + resume PC,
// TLS scope and player bind the native return to this one trampoline call.
namespace link_runtime::shield::runtime {
using LifeFunction=int(__cdecl*)(void*,int,int,int);
struct LifeBridge {
    LifeBridge* previous=nullptr;
    const void* scope=nullptr;
    uint32_t player=0,native_return=0,return_slot=0,resume=0;
};
static_assert(sizeof(void*)==4&&offsetof(LifeBridge,return_slot)==16&&offsetof(LifeBridge,resume)==20,"x86 bridge offsets");
inline __declspec(thread) LifeBridge* life_bridge=nullptr;
__declspec(naked) inline int __cdecl InvokeLifeBridge(LifeFunction,void*,int,int,int,LifeBridge*) {
    __asm {
        push ebp
        mov ebp,esp
        push esi
        mov esi,[ebp+28]
        lea eax,[esp-20]
        mov [esi+16],eax
        lea eax,life_resume
        mov [esi+20],eax
        push [ebp+24]
        push [ebp+20]
        push [ebp+16]
        push [ebp+12]
        call dword ptr [ebp+8]
    life_resume:
        add esp,16
        pop esi
        pop ebp
        ret
    }
}
// Caller saves/restores any own LastError as before. No system API, float math,
// allocation, logging or audio occurs here. __finally also covers native SEH.
inline int CallLifeOriginal(LifeFunction original,void* target,int damage,int random,int flags,
                            uint32_t native_return,const void* scope) {
    if(!scope)return original(target,damage,random,flags);
    LifeBridge bridge{life_bridge,scope,reinterpret_cast<uint32_t>(target),native_return,0,0};
    life_bridge=&bridge;int result=0;
    __try {result=InvokeLifeBridge(original,target,damage,random,flags,&bridge);}
    __finally {life_bridge=bridge.previous;}
    return result;
}
inline uint32_t LifeReturn(uint32_t frame_ebp,uint32_t raw_return,const void* scope,uint32_t player) {
    const auto* b=life_bridge;
    if(!b)return raw_return;
    if(b->previous||b->scope!=scope||b->player!=player||!b->return_slot||!b->resume||
       frame_ebp>0xFFFFFFFBU||frame_ebp+4!=b->return_slot||raw_return!=b->resume)return 0;
    return b->native_return;
}
}

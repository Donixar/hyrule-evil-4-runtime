#pragma once
#include "ztarget_strafe_transport.h"
#include "native_pins.h"
#include "MinHook.h"
namespace link_runtime::ztarget::strafe::native {
// No pins over the Walk entry: re4_fps owns 0x363331. No second MotionMove/Core hook.
// Qualify these in loaded RAM, relocation-normalized, before preparing the local input gate.
inline constexpr native_pins::Pin kPins[]={
    {0x364FE1,"0fb686fc0000008b0c85b4bcc10056ffd183c404"},
    {0x365026,"d986a400000051d91c24e85571caff33d2d99ea400000083c404"},
    {0x3650C0,"8b168b424c8bceffd08bcee8d30ccaff8bcee817f9c9ff56e8de17caff8b8e940000008b96980000008b869c00000056894dec8955f08945f4e81222caff8b8e8807000083c4085156b97c9abf00e87491caff"},
    {0x25CFD0,"6a0051e8219cdaff83c408c3"},
    {0x262D1A,"8d8518ffffff508d8d24ffffff518d530c52e8e8afdaff8db394000000568d8518ffffff5056e81e52daff8d83a0000000508d8d30ffffff5150e80a52daff"},
    {0x194FC0,"558bec51538b5d080fb783ce020000578bf9c745fc00000000a900010000750a5f33c05b8be55dc20800a802741153e818c3e6ff5f8945fc5b8be55dc20800"},
    {0x363440,"8bcee80463caff"},
    {0x3634FA,"8bc183e00133d20bc2750c899efc0000005e5b8be55dc3"},
    {0x262DF0,"6a008db3d80100005653e8ca54daff56e824bddaff"},
    {0x261E78,"f7474400000040752f8d93a0000000528d730c56e8367fdaff8d83940000005056e8b5c5daff81c3ac0000005356e87ebddaff83c41833f6"},
    // Native neck: embedded work player+474, attrs5, no SEQ, additive flag80000000.
    {0x27CAE3,"818eb804000000000010576a0051894d0c8dbe74040000db450cd91c246a056a086a00505756e83ce6d8ffd9e881a6b8040000ffffffef89bea8020000d99fc80000008bb6a802000083c420814e44000000805f5e5dc20800"},
    {0x27EC51,"894614894e1885c0743f85c9743b8b4d1051508bcee82d4fd8ff6a008bcee8c25bd8ff"},
    // Both root and secondary Core skip the zero-weight layer. Keep these exact,
    // including ordered zero comparison; NaN/nonzero weights are never admitted.
    {0x262BF8,"85c00f841a010000814844000000088b8ba8020000f7414400000080d981c8000000d99dd8feffffd985d8feffffd9eedae9dfe0756ef6c4440f8be3000000"},
    {0x262C9C,"f6c4447b79"},
    {0x262E3D,"814844000000088b8ba8020000f7414400000080d981c8000000d9eedae9dfe0754bf6c4440f8b44010000"},
    {0x262EAA,"f6c4440f8bf9000000"}
};
template<class Mem>bool Qualify(Mem& memory,uint32_t base){return native_pins::All(memory,base,kPins);}
inline Transport* transport=nullptr;inline void* gate_site=nullptr;inline void* gate_original=nullptr;
inline bool created=false,enabled=false;
inline uint32_t __cdecl GateCall(uint32_t player,uint32_t keys){
    if(!transport)return keys;
    const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);
    const uint32_t result=transport->LocalGate(player,keys);
    std::fesetenv(&env);SetLastError(error);return result;
}
// Entry is inside Walk with ESI=player and ECX=native KEY_ON low word. Restore every
// register/flags except ECX, then let the trampoline execute the original MOV/AND.
__declspec(naked) inline void GateDetour(){__asm{
    pushfd
    pushad
    mov eax,esp
    sub esp,528
    and esp,0fffffff0h
    mov [esp+512],eax
    fxsave [esp]
    fninit
    mov dword ptr [esp+516],01f80h
    ldmxcsr [esp+516]
    mov ecx,[eax+24]
    mov edx,[eax+4]
    push ecx
    push edx
    call GateCall
    add esp,8
    mov edx,[esp+512]
    mov [edx+24],eax
    fxrstor [esp]
    mov esp,[esp+512]
    popad
    popfd
    jmp dword ptr [gate_original]
}}
inline bool GateBytes(void* site){
    static const unsigned char expected[]={0x8B,0xC1,0x83,0xE0,0x01};
    if(!site)return false;
    __try{return std::memcmp(site,expected,sizeof(expected))==0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Low-level preparation: qualified means caller has checked kPins and BOTH normal/Core
// integration, including native return-address capture and game-thread ownership. This does
// not set controller.config.qualified, enable Strafe, or imply native gameplay verification.
inline MH_STATUS PrepareGate(void* site,Transport& value,bool qualified){
    if(!qualified||!GateBytes(site))return MH_ERROR_NOT_EXECUTABLE;
    if(created)return MH_ERROR_ALREADY_CREATED;
    const auto status=MH_CreateHook(site,reinterpret_cast<void*>(&GateDetour),&gate_original);
    if(status!=MH_OK)return status;
    gate_site=site;transport=&value;created=true;return MH_OK;
}
inline MH_STATUS EnableGate(){
    if(!created||!transport)return MH_ERROR_NOT_CREATED;
    const auto status=MH_EnableHook(gate_site);
    if(status==MH_OK){enabled=true;transport->gate_ready=true;}return status;
}
}

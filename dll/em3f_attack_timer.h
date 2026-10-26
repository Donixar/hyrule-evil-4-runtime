// dll/em3f_attack_timer.h
#pragma once
// UHD em3f routine 0x175A80, phase r2=0x0A. Native arms em+0x484 with 2.0,
// decrements dt and calls 0x16F3C0 only on EXACT zero (test ah,0x44; jp).
// At 165 FPS the eleventh decrement leaves -3.5762787e-7, so the call is lost.
// This local hook uses previous>0 && next<=0 and consumes the one-shot timer.
// It replays the native arithmetic and stack-argument scratch write, preserving
// the original x87 result/status/registers on the no-action path. No kick hook.
#include "native_pins.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace link_runtime::em3f_attack_timer {
namespace rva {
constexpr uint32_t kSite = 0x175D35, kResume = 0x175D7C, kAttack = 0x16F3C0;
constexpr uint32_t kGlobalSlot = 0x806F3C;
}
constexpr uint32_t kSiteLen = 5, kTimer = 0x484;
// The whole routine and dispatch tables (split below 128 bytes per pin), both
// setters, the 2.0 constant and the actual attack's entry and damage route.
inline constexpr native_pins::Pin kPins[] = {
    {0x175A80, "558bec568b75080fb686fe0000005783f8140f87e40200000fb6802c5e5700ff2485185e570080be1f0500000075226a0068002db100b950cdc600c6861f05000001e8b812e9ff6a01e815e6e8ff83c404e8ea95ffff8b86800300008b88940000008b90880000006a056a016a006a0003c86a00518b487c"},
    {0x175AF8, "03d05203c851e87d97ffff83c4208bfec786aa04000000001400c6861d05000000e802a8ffffd9054cf8b0006a006a0051d91c246a188bc6e83ba8ffffd90564f8b00083c4106a006a0051d91c246a148bc6e821a8ffff0fb68eb00400006a00566a006a006a28516a006a006aff56e858b9e8ff83c438d9"},
    {0x175B70, "eed99e78040000fe86fe000000e9fa0100006a0056e86f10e9ff83c40885c00f84e7010000e82695ffff8b86800300008b90980000008b888c0000006a006a016a006a0003d06a00528b908000000003c85103d052e8b696ffff83c4208bfee84ca7ffffd9ee6a006a0051d91c246a378bc6e889a7ffffd9"},
    {0x175BE8, "ee83c4106a006a0151d91c246a2a8bc6e873a7ffffd9058c00b1000fb686b0040000d99e840400006a00566a006a006a2a506a006a006aff56e89eb8e8ff83c438c686fe0000000ae9470100000fb68eb00400006a00566a006a006a29516a006a006aff56e872b8e8ff6a0056e89f0fe9ff83c43085c00f"},
    {0x175C60, "84d000000066ff8eac0400006683beac040000007f5c8b86800300008b909c0000008b88900000006a006a016a006a0003d06a00528b908400000003c85103d052e8da95ffff0fb686b00400006a00566a006a006a2b506a006a006aff56e801b8e8ff83c448c686fe00000014e9aa000000e8e993ffff8b"},
    {0x175CD8, "86800300008b88980000008b908c0000006a006a016a006a0003c86a00518b888000000003d05203c851e87995ffffd9058c00b1000fb696b0040000d99e840400006a00566a006a006a2a526a006a006aff56e894b7e8ff83c448eb47a13c6fc000d98684040000d86070d95d08d94508d99684040000d9"},
    {0x175D50, "eedae9dfe0f6c4447a22e86196ffffeb1b6a0056e8900ee9ff83c40885c0740c32c9b801000000e8e4bcffffd98678040000dc1d30f9b000dfe0f6c401751fd905e0f9b000d89e78040000dfe0f6c441750c818e0804000000000020eb0a81a608040000ffffffdf6683bea004000011754f80befe000000"},
    {0x175DC8, "147346a15470c500d905fc2cb1008b88940000008b90980000008b809c000000898e6404000089966804000089866c040000d99e70040000d9051800b100d99e74040000e81f90ffff5f5e5dc38d4900a65a5700825b5700355c5700615d57007c5d57000001040404040404040402040404040404040404"},
    {0x175E40, "03"},
    {0x71008C, "00000040"},
    {0x16F3C0, "558bec83ec60a1105ec30033c58945fc576a0a8bcee80f38e9ff8bf885ff0f84db010000a13c6fc0006683b8b44f0000000f8ec8010000a15470c50080b82c030000000f85b601000080b82d030000000f85a9010000"},
    {0x16F508, "e85d42e9ff83c41885c00f85b8000000"},
    {0x1D84, "e997a81b00"},
    {0x376A, "e9a18c1b00"},
};
inline bool Crossed(float prev, float next) { return prev > 0.0f && next <= 0.0f; }
inline bool Jump(uint32_t from, uint32_t to, uint8_t* out) {
    if (!out) return false;
    const uint32_t rel=to-(from+5); out[0]=0xE9; std::memcpy(out+1,&rel,4); return true;
}
enum class Verdict { Installed, Pins, Write };
inline const char* Name(Verdict v) { return v==Verdict::Installed?"installed":v==Verdict::Pins?"pins_differ":"write"; }
inline void (*g_line)(int level,const char* text)=nullptr;
inline uint32_t g_global_slot=0,g_target=0,g_resume=0,g_fire=0,g_calls=0,g_recovered=0,g_unread=0;
inline float g_prev=0.0f,g_next=0.0f;

#if defined(_MSC_VER) && defined(_M_IX86)
// The game-thread call site is non-reentrant; this decision performs no game
// call or logging. Read the ACTUAL native result after its fstp, so even a
// non-default x87 rounding mode retains the original arithmetic exactly.
inline void __cdecl Decide(uint32_t em) {
    g_fire=0;
    __try {
        const auto* p=reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(em));
        g_next=*reinterpret_cast<const volatile float*>(p+kTimer);
        g_fire=Crossed(g_prev,g_next)?1U:0U;
    } __except(1) { ++g_unread; }
}
inline void __cdecl OnFired(uint32_t em) {
    ++g_calls; if(g_next!=0.0f) ++g_recovered;
    if(!g_line) return;
    char text[256];
    std::snprintf(text,sizeof(text),
        "em3f_attack_timer fired em=%p prev=%.9g next=%.9g recovered=%u n=%u total_recovered=%u",
        reinterpret_cast<void*>(static_cast<uintptr_t>(em)),double(g_prev),double(g_next),
        g_next!=0.0f?1U:0U,g_calls,g_recovered);
    g_line(1,text);
}
__declspec(naked) inline void TimerStub() {
    __asm {
        push eax
        mov eax,[esi+0x484]
        mov dword ptr [g_prev],eax
        pop eax
        // Original 0x175D35..0x175D58, including the caller's scratch slot.
        mov eax,dword ptr [g_global_slot]
        mov eax,[eax]
        fld dword ptr [esi+0x484]
        fsub dword ptr [eax+0x70]
        fstp dword ptr [ebp+8]
        fld dword ptr [ebp+8]
        fst dword ptr [esi+0x484]
        fldz
        fucompp
        fnstsw ax
        test ah,044h
        pushfd
        pushad
        cld
        mov eax,esp
        sub esp,528
        and esp,0fffffff0h
        mov [esp+512],eax
        fxsave [esp]
        mov eax,fs:[0x34]
        mov [esp+520],eax
        fninit
        mov dword ptr [esp+516],01f80h
        ldmxcsr [esp+516]
        push esi
        call Decide
        add esp,4
        mov eax,[esp+520]
        mov fs:[0x34],eax
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        pushfd
        cmp dword ptr [g_fire],0
        jne fire
        popfd
        jmp dword ptr [g_resume]
    fire:
        popfd
        call dword ptr [g_target]
        pushfd
        pushad
        cld
        mov eax,esp
        sub esp,528
        and esp,0fffffff0h
        mov [esp+512],eax
        fxsave [esp]
        mov eax,fs:[0x34]
        mov [esp+520],eax
        fninit
        mov dword ptr [esp+516],01f80h
        ldmxcsr [esp+516]
        push esi
        call OnFired
        add esp,4
        mov eax,[esp+520]
        mov fs:[0x34],eax
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        mov dword ptr [esi+0x484],0
        jmp dword ptr [g_resume]
    }
}
inline void Arm(uint32_t base) {
    g_global_slot=base+rva::kGlobalSlot; g_target=base+rva::kAttack; g_resume=base+rva::kResume;
    g_fire=g_calls=g_recovered=g_unread=0;
}
template<class Mem> Verdict Install(Mem& m,uint32_t base,uint32_t& bad) {
    bad=0;
    for(const auto& p:kPins) if(!native_pins::Matches(m,base,p.rva,p.hex)) { bad=p.rva; return Verdict::Pins; }
    Arm(base); uint8_t code[kSiteLen]; const uint32_t at=base+rva::kSite;
    if(!Jump(at,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&TimerStub)),code) || !m.Write(at,code,kSiteLen)) return Verdict::Write;
    return Verdict::Installed;
}
#endif
}  // namespace link_runtime::em3f_attack_timer

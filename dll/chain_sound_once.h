// dll/chain_sound_once.h
#pragma once
// One chain-rattle per Chain_se_wait expiry in em10FootSe (Ganados/Garrador).
// UHD 1.1.0 19aed4af...: [esi+0x804] starts at 1.0; guard 0<t, t-=dt,
// helper 0x28820 true for -dt<t<dt. At 165 FPS two ticks fire SndCall 8/A0.
// The local call wrapper replays ALL SIX cdecl arguments, calls native once,
// then consumes this one-shot timer. The helper and native kick are untouched.
#include "foreign_patch.h"
#include "image_relocation.h"
#include "native_pins.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace link_runtime::chain_sound_once {
namespace rva {
constexpr uint32_t kSite = 0x35E92, kResume = 0x35E97, kSound = 0xADC1;
}  // namespace rva
constexpr uint32_t kSiteLen = 5, kTimer = 0x804;
inline constexpr native_pins::Pin kPins[] = {
    // Full function entry, type/weapon guard, countdown, sound arguments, call and following branch.
    {0x35E20, "558bec83ec10a1105ec30033c58945fc568bf18a8601010000573c0a740d3c0d740980bef00700000b754fd9ee8d8e04080000"
              "d819dfe0f6c4057a3ed901a13c6fc000d860706a00d919e87883fdff84c074276a008bcee86dcdfcff0fb68e00010000566a00"
              "5183c0705068a00000006a08e82a4ffdff"},
    {0x35E97, "83c4188a868a02000084c00f843a020000"},
    // Three native setters, each assigns 1.0; cases 0/1 converge on the first.
    {0x35F21, "d9e8d99e04080000"},
    {0x35F8D, "d9e8bf18000000d99e04080000"},
    {0x35F9F, "d9e8bf19000000d99e04080000"},
    {0x28820, "558becdb45088b153c6fc000d95d08d901d94270d94508d9c0dec2d9c9d95d08d94508d8dadfe0ddd9f6c4417521d901d94270"
              "deead9c9d95d08d94508ded9dfe0f6c4057a0bb8010000005dc20400ddd833c05dc20400"},
    {0xE1E7, "e934a60100"},
    {0x56E230, "558bec83ec18538b5d08560fb7f38d46fe57c645fa00c645f800c645f700c645"},
};

// SndCall's thunk (kSound): native `jmp 0x56E230`, or the one known foreign form. re4_tweaks
// (re4_fps, winmm.dll) redirects this thunk to its SndCall dedup (keyed by return address and args);
// live 3.0.2, 20-oct: e972d99a6c at base 0xC20000 -> WINMM.dll+0x8738. Accepted only as E9 rel32
// whose target the foreign probe places in code of another loaded module. The stub calls the
// thunk either way, so the foreign hook keeps running; the thunk itself is never written.
constexpr const char* kThunkHex = "e96a345600";
template <class Mem> bool ThunkOk(Mem& m, uint32_t base, const foreign::Probe* probe, uint32_t& foreign_target) {
    foreign_target = 0;
    if (native_pins::Matches(m, base, rva::kSound, kThunkHex)) return true;
    unsigned char at[5]{};
    if (!image_reloc::ReadPreferred(m, base, rva::kSound, at, sizeof(at)) || at[0] != 0xE9) return false;
    const uint32_t target = base + rva::kSound + 5 + foreign::U32(at + 1);
    if (!foreign::Accept(probe, target)) return false;
    foreign_target = target; return true;
}
inline bool Call(uint32_t from, uint32_t to, uint8_t* out) {
    if (!out) return false;
    const uint32_t rel = to - (from + 5);
    out[0] = 0xE8; std::memcpy(out + 1, &rel, 4); return true;
}
inline bool NativeTick(float& t, float dt) {
    if (!(0.0f < t)) return false;
    t -= dt;
    return -dt < t && t < dt;
}
inline bool FixedTick(float& t, float dt) {
    if (!NativeTick(t, dt)) return false;
    t = 0.0f; return true;
}
inline void (*g_line)(int level, const char* text) = nullptr;
inline uint32_t g_target = 0, g_calls = 0, g_blocked = 0, g_unread = 0;
enum class Verdict { Installed, Pins, Write };
inline const char* Name(Verdict v) {
    return v == Verdict::Installed ? "installed" : v == Verdict::Pins ? "pins_differ" : "write";
}

#if defined(_MSC_VER) && defined(_M_IX86)
// Called under saved GPRs/flags/x87/SSE/LastError. A log is emitted only for an
// actual rattle, never for each frame. It is evidence of a request, not playback.
inline void __cdecl OnFired(uint32_t em) {
    ++g_calls;
    float left; uint8_t id, type; int16_t hp;
    __try {
        const auto* p = reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(em));
        left = *reinterpret_cast<const volatile float*>(p + kTimer);
        id = p[0x100]; type = p[0x101]; hp = *reinterpret_cast<const volatile int16_t*>(p + 0x324);
    } __except (1) { ++g_unread; return; }
    if (left > 0.0f) ++g_blocked;
    if (!g_line) return;
    char text[256];
    std::snprintf(text, sizeof(text),
        "chain_sound_once fired em=%p left=%.6f refire_blocked=%u id=%02x type=%02x hp=%d n=%u blocked=%u",
        reinterpret_cast<void*>(static_cast<uintptr_t>(em)), double(left), left > 0.0f ? 1U : 0U,
        unsigned(id), unsigned(type), int(hp), g_calls, g_blocked);
    g_line(1, text);
}

__declspec(naked) inline void ChainStub() {
    __asm {
        // At entry [esp] is our resume, [esp+4..24] are SndCall's six args.
        // Each push moves the next original argument to this same offset.
        push dword ptr [esp+24]
        push dword ptr [esp+24]
        push dword ptr [esp+24]
        push dword ptr [esp+24]
        push dword ptr [esp+24]
        push dword ptr [esp+24]
        call dword ptr [g_target]
        lea esp,[esp+24]
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
        mov dword ptr [esi+0x804],0
        ret
    }
}

inline void Arm(uint32_t base) {
    g_target = base + rva::kSound; g_calls = g_blocked = g_unread = 0;
}
// `probe` null: only the native thunk. `foreign_target` gets the redirect target, 0 when native.
template <class Mem> Verdict Install(Mem& m, uint32_t base, uint32_t& bad_pin,
                                     const foreign::Probe* probe = nullptr, uint32_t* foreign_target = nullptr) {
    bad_pin = 0;
    if (foreign_target) *foreign_target = 0;
    for (const auto& p : kPins)
        if (!native_pins::Matches(m, base, p.rva, p.hex)) { bad_pin = p.rva; return Verdict::Pins; }
    uint32_t redirected = 0;
    if (!ThunkOk(m, base, probe, redirected)) { bad_pin = rva::kSound; return Verdict::Pins; }
    if (foreign_target) *foreign_target = redirected;
    Arm(base);
    uint8_t code[kSiteLen];
    const uint32_t at = base + rva::kSite;
    if (!Call(at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ChainStub)), code) || !m.Write(at, code, kSiteLen))
        return Verdict::Write;
    return Verdict::Installed;
}
#endif
}  // namespace link_runtime::chain_sound_once

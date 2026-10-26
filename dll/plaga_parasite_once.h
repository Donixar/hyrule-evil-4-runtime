// dll/plaga_parasite_once.h
#pragma once
// plaga-parasite-once-01 (19/20-oct-2026, docs/plaga-sonido-bucle-2026-10-19.md): a Ganado's head bursts into ONE
// Plaga at any framerate, as at 30 FPS. At 165 FPS the game grows two; the first is orphaned and squelches forever.
//
// Native facts (bio4.exe 1.1.0, SHA 19aed4af..., RVAs at base 0x400000). In cEm10::move (0x5AE20), every tick:
//   0x5B825  fldz; fcomp [esi+0x74C]; ... jp 0x5B856      -> only while 0 < Parasite_wait (float, em+0x74C)
//   0x5B83A  Parasite_wait -= deltaTime (pG+0x70)
//   0x5B848  call 0xE1E7 (-> 0x28820) with n = 0: true when n - dt < t < n + dt (the UHD "timer reached n" test)
//   0x5B851  call 0x55100 = em10SetParasite (em in esi): body + 4 tentacle cObj16 (SetObj16), stored at em+0x6BC and
//            em+0x6C0..0x6CC WITHOUT looking at what is there.
// em10LostHead (0x52BC0) arms it with 0x7CB6(r*30+1) = r*30+1 + 1 - dt. At 30 FPS (dt 1) and 60 FPS (dt 0.5) the
// countdown lands exactly on 0.0: one call, then the guard (0 < 0) stops it. At 165 FPS (dt 2/11, inexact) it lands
// at a small +x < dt (call), then x - dt in (-dt, 0) still passes both tests (second call). The second Plaga replaces
// the first in the em; the first keeps its em pointer, so cObj16::move (0x320BE0) never sees it lost (the dead
// Ganado stays in the em list), its Be_flag bit 0 is never set (em10CoreBreak 0x4B660 only reaches the second), and
// its R1_CoreMove (0x322D40) plays SE 8/0x10 every 15-19 frames forever at the dead Ganado's position.
// Live r113 (19-oct): the three Ganados that grew a Plaga had Parasite_wait -0.000259 / -0.0909 / -0.000259 (a
// residual in (-dt, 0) is only reachable through two calls) and each had exactly one orphan Plaga (vtable 0x71D694,
// type 2, Wait_mode 1, timer cycling 15..19) plus its 4 tentacles; the five without a Plaga had 0.0 and none.
//
// The fix (native-equivalent): the 5-byte `call 0x455100` at 0x5B851 becomes `call ParasiteStub`. The stub makes the
// same native call (same registers; em10SetParasite keeps esi), then, as the 30/60 FPS countdown does by landing on
// 0.0, sets Parasite_wait = 0.0 so the guard ends the countdown: one Plaga. At 30/60 FPS the value it overwrites is
// already 0.0 (byte for byte vanilla). The C call for the log line keeps every register, the flags and the x87/SSE
// state. Every byte the site, the native call target, the countdown and the crossing test rely on is pinned (kPins);
// one mismatch and nothing is written. [LinkRuntime] PlagaParasiteOnce (missing or 1 = on).
#include "native_pins.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace link_runtime::plaga_once {
namespace rva {
constexpr uint32_t kSite = 0x5B851, kResume = 0x5B856, kSetParasite = 0x55100, kCrossThunk = 0xE1E7, kCross = 0x28820;
}  // namespace rva
namespace off {
constexpr uint32_t kR0 = 0xFC, kId = 0x100, kType = 0x101, kHp = 0x324, kParasiteWait = 0x74C, kCore = 0x6BC;
}  // namespace off
constexpr uint32_t kSiteLen = 5;

// Exact bytes of bio4.exe 1.1.0 at the preferred base (absolute operands normalised by native_pins/image_relocation).
inline constexpr native_pins::Pin kPins[] = {
    // cEm10::move: [esi+0x624] = 0; call 0x4371C0; the Parasite_wait guard (fldz; fcomp [esi+0x74C]; test ah,5; jp),
    // -= deltaTime (mov edx,[0xC06F3C]; fsub [edx+0x70]), push ebx; fstp; call 0xE1E7; test al,al; je 0x5B856;
    // call 0x55100 (the site); call 0x437F30; cmp [esi+0x6D4],ebx
    {0x5B81A, "899e24060000e89bb9fdffd9eed89e4c0700008d8e4c070000dfe0f6c4057a1cd9018b153c6fc000d8627053d919e89a29fbff84c0"
              "7405e8aa98ffffe8d5c6fdff399ed406000074"},
    // the crossing test 0x28820: (n + dt > t) and (n - dt < t), n = int argument, t = [ecx]
    {rva::kCross, "558becdb45088b153c6fc000d95d08d901d94270d94508d9c0dec2d9c9d95d08d94508d8dadfe0ddd9f6c4417521d901d94270"
                  "deead9c9d95d08d94508ded9dfe0f6c4057a0bb8010000005dc20400ddd833c05dc2"},
    {rva::kCrossThunk, "e934a60100"},   // jmp 0x28820
    // em10SetParasite: prologue, cookie, `cmp word [esi+0x324],0; jle` (em in esi, returns when hp <= 0)
    {rva::kSetParasite, "558bec83ec50a1105ec30033c58945fc6683be24030000000f8e7d040000"},
};

// E8 rel32 from `from` to `to` (exactly the 5 bytes of the native call it replaces).
inline bool Call(uint32_t from, uint32_t to, uint8_t* out) {
    if (!out) return false;
    const uint32_t rel = to - (from + 5);
    out[0] = 0xE8; std::memcpy(out + 1, &rel, 4);
    return true;
}

// The native crossing test (0x28820) on single floats, as the x87 compares them after rounding to float.
inline bool Crossed(float t, float dt, int n = 0) {
    const float a = static_cast<float>(n) + dt, b = static_cast<float>(n) - dt;
    return a > t && b < t;
}
// One logic tick of the native countdown (0x5B825..0x5B84F): true when em10SetParasite is called this tick.
inline bool NativeTick(float& t, float dt) {
    if (!(0.0f < t)) return false;
    t -= dt;
    return Crossed(t, dt);
}
// The same tick with the fix: after the call the countdown is at 0.0.
inline bool FixedTick(float& t, float dt) {
    if (!NativeTick(t, dt)) return false;
    t = 0.0f;
    return true;
}
// em10LostHead's arming through 0x7CB6: n > 1 -> n + 1 - dt, else n.
inline float ArmTimer(int n, float dt) {
    float t = static_cast<float>(n);
    if (n > 1) t = t + (1.0f - dt);
    return t;
}

// Log policy: the first 16 calls, then every 64th (each Plaga burst is one call; never per frame).
inline bool ShouldLog(uint32_t n) { return n <= 16 || n % 64 == 0; }
// `left` = Parasite_wait right after the native call; a second call would follow next tick exactly when 0 < left.
inline int FormatLine(char* out, size_t n, uint32_t em, float left, uint32_t core, uint8_t id, uint8_t type, int16_t hp,
                      const uint8_t r[4], uint32_t count, uint32_t blocked) {
    return std::snprintf(out, n,
        "plaga_once fired em=%p left=%.6f refire_blocked=%u core=%08x id=%02x type=%02x hp=%d r=%02x%02x%02x%02x n=%u blocked=%u",
        reinterpret_cast<void*>(static_cast<uintptr_t>(em)), double(left), left > 0.0f ? 1U : 0U, core, unsigned(id),
        unsigned(type), int(hp), unsigned(r[0]), unsigned(r[1]), unsigned(r[2]), unsigned(r[3]), count, blocked);
}

enum class Verdict { Installed, Pins, Write };
inline const char* Name(Verdict v) { return v == Verdict::Installed ? "installed" : v == Verdict::Pins ? "pins_differ" : "write"; }

// ---- runtime state (game thread) and the x86 stub ---------------------------------------------------------------
// Sink: `line(level, text)`, level 1 = normal log. Set before Install.
inline void (*g_line)(int level, const char* text) = nullptr;
inline uint32_t g_target = 0;                    // base + 0x55100 (em10SetParasite)
inline uint32_t g_calls = 0, g_blocked = 0, g_unread = 0;

#if defined(_MSC_VER) && defined(_M_IX86)
struct Em { float left = 0.0f; uint32_t core = 0; int16_t hp = 0; uint8_t id = 0, type = 0, r[4]{}; bool read = false; };
inline bool ReadEm(uint32_t em, Em& e) {
    e = Em{};
    if (!em) return false;
    __try {
        const auto* p = reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(em));
        const uint32_t bits = *reinterpret_cast<const volatile uint32_t*>(p + off::kParasiteWait);
        std::memcpy(&e.left, &bits, 4);
        e.core = *reinterpret_cast<const volatile uint32_t*>(p + off::kCore);
        e.hp = *reinterpret_cast<const volatile int16_t*>(p + off::kHp);
        for (int i = 0; i < 4; ++i) e.r[i] = p[off::kR0 + i];
        e.id = p[off::kId]; e.type = p[off::kType];
        e.read = true;
    } __except (1) { e.read = false; }
    return e.read;
}
// Called from the stub right after the native em10SetParasite (cdecl; registers, flags, FPU/SSE saved around it).
inline void __cdecl OnFired(uint32_t em) {
    ++g_calls;
    Em e;
    if (!ReadEm(em, e)) { ++g_unread; return; }
    if (e.left > 0.0f) ++g_blocked;
    if (!g_line || !ShouldLog(g_calls)) return;
    char text[256];
    if (FormatLine(text, sizeof(text), em, e.left, e.core, e.id, e.type, e.hp, e.r, g_calls, g_blocked) > 0) g_line(1, text);
}

// The native call first (registers as the game left them), then the log call inside pushfd/pushad with an aligned
// fxsave area, DF cleared and a clean x87/MXCSR (the FallDetour pattern of ztarget_runtime.h, as gigante_grab_once.h),
// then Parasite_wait = 0.0 (a mov: no flag, register or x87 change) and back to 0x5B856.
__declspec(naked) inline void ParasiteStub() {
    __asm {
        call dword ptr [g_target]
        pushfd
        pushad
        cld
        mov eax,esp
        sub esp,528
        and esp,0fffffff0h
        mov [esp+512],eax
        fxsave [esp]
        fninit
        mov dword ptr [esp+516],01f80h
        ldmxcsr [esp+516]
        push esi
        call OnFired
        add esp,4
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        mov dword ptr [esi+0x74C],0
        ret
    }
}

// Points the stub at `base` and clears the counters.
inline void Arm(uint32_t base) { g_target = base + rva::kSetParasite; g_calls = g_blocked = g_unread = 0; }
// Mem: bool Read(uint32_t, void*, size_t) and bool Write(uint32_t, const void*, size_t). All pins first, then the one
// 5-byte write. `bad_pin` = rva of the first pin that differs.
template <class Mem> Verdict Install(Mem& m, uint32_t base, uint32_t& bad_pin) {
    bad_pin = 0;
    for (const auto& p : kPins)
        if (!native_pins::Matches(m, base, p.rva, p.hex)) { bad_pin = p.rva; return Verdict::Pins; }
    Arm(base);
    uint8_t code[kSiteLen];
    const uint32_t at = base + rva::kSite;
    if (!Call(at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ParasiteStub)), code) || !m.Write(at, code, kSiteLen))
        return Verdict::Write;
    return Verdict::Installed;
}
#endif
}  // namespace link_runtime::plaga_once

// dll/grab_hit_once.h
#pragma once
// Grab damage windows that hit once at any framerate, as at 30 FPS (successor of gigante_grab_once.h):
//   site 0 "gigante"  gigante-grab-once-01 (docs/gigante-grab-once-01-2026-10-19.md), [LinkRuntime] GiganteGrabOnce
//   site 1 "salazar"  em38-hit-once-01 (docs/em38-hit-once-01-2026-10-20.md),         [LinkRuntime] SalazarGrabOnce
//   sites 2/3 "saddler"/"saddler_step", SaddlerGrabOnce (both preflighted together)
//   site 4 "ganado_neck", GanadoNeckGrabOnce (dynamic native LifeDownSet2 damage)
// Missing or 1 = on. Sites 0/1/4 install independently; InstallSaddler groups 2/3.
//
// The pattern (bio4.exe 1.1.0, SHA 19aed4af..., RVAs at base 0x400000). Both are PLAYER damage routines (cPlayer in
// esi, installed by the grabbing em with SetPlDamage; the grabber is pl->pEmCatch, PC offset +0x3C0). Every logic tick
// of one state they test the player's own motion frame (pl->Motion.Seq_frame, +0x290) against a 0.6-frame window and,
// inside it, run a hit block with no hit-once flag:
//   gigante  em2b r1 0x1A (0xBCF30) -> SetPlDamage 0xB5B40, state 9 at 0xB6669:
//            86.7 < frame < 87.3 -> wrapper 0x718F(pPL, 800, 0) at 0xB669B, PlSetDamageSe 0x5696(0x0A/0x0D),
//            VibSetData 0x9020(0x0B, 1); outside and after the block: 0xB66DA.
//   salazar  em38 (Salazar's boss form, r228) r1 table 0x7F8308 entry 0x1F (em38_R1_T_CatchHit 0x13BFF0) ->
//            SetPlDamage 0x137480 (plem38_CatchHit), state 1 at 0x13763A (after MotionMove returned 0):
//            87.7 < frame < 88.3 -> wrapper 0x718F(pPL, 550, 0) at 0x137676, PlSetDamageSe 0x5696(0),
//            SndCall 0xADC1(5, 5, &pl->pos, 0, 0, pl), VibSetData 0x9020(0x0B, 1); outside and after: 0x1377E1.
// At 30 FPS the frame advances 1.0 per tick: one tick in the window, one hit. With re4_fps at 165 it advances 30/165:
// 3 or 4 ticks, 3 or 4 hits (and voices, sounds, vibrations).
//
// The rule (native-equivalent): one hit block per window. A raw jump replaces the window's first instruction
// (`fld dword [low]`, 6 bytes) with EntryN -> CommonStub, which asks OnVisit and then either replays the fld and
// resumes at the native fcomp (the native test runs unchanged), or jumps to the site's join (the native "outside the
// window" target: same stack; eax and the flags are dead there) so a repeat tick skips the whole hit block.
// OnVisit uses the native test (low < frame < high, the image's own floats; NaN outside). The latch is per (site,
// player): a tick outside the window opens it, the first tick inside applies natively and closes it, later ticks
// inside skip; it only counts as the same window while the visits stay at most kMaxGapTicks logic ticks apart
// (pG+0x530C). Every grab enters the state with the frame far below the window.
#include "native_pins.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace link_runtime::grab_once {
namespace off {
constexpr uint32_t kId = 0x100, kFrame = 0x290, kCatcher = 0x3C0, kTick = 0x530C;
}  // namespace off
constexpr uint32_t kGlobalSlot = 0x806F3C;   // VA 0xC06F3C, pG
constexpr uint32_t kSiteLen = 6, kMaxGapTicks = 4;

// Exact bytes of bio4.exe 1.1.0 at the preferred base (absolute operands normalised by native_pins/image_relocation).
inline constexpr native_pins::Pin kGigantePins[] = {
    // state 8's tail, state 9 = site 0xB6669, the two window tests, the hit block (call 0x718F at 0xB669B), the join 0xB66DA
    {0xB6660, "83c420fe86fe000000d9059814b100d89e90020000dfe0f6c4057a5ed9059414b100d89e90020000dfe0f6c441754ba15470c500576820"
              "03000050e8ef0af5ff8b0d3c6fc00083c40c6639b9b44f00007f046a0deb026a0ae8d9eff4ffa13c6fc0008b50488b4a1c83c4046a01"
              "03ca6a0b51e84929f5ff83c40c5756e81805f5"},
    // 0xB5B40: pPL->+0x3C0, esi = arg, [esi+0x3C0]+0x380, then the state switch ... jmp [ecx*4+0xB6760]
    {0xB5B56, "8b98c0030000568b75088b86c00300008b8880030000898e80030000"},
    {0xB5B72, "0fb68efe000000c6862d0300000283f9090f875a0a00005733ffff248d60674b00"},
    {0xB6784, "69664b00"},     // state 9 -> 0x4B6669
    {0x7F5580, "30cf4b00"},    // em2b r1 table 0x7F5518, entry 0x1A -> 0x4BCF30
    {0xBCFB3, "68405b4b00"},   // push 0x4B5B40 (SetPlDamage)
    {0x711494, "9a99ae426666ad42"},   // 87.3f, 86.7f
};
inline constexpr native_pins::Pin kSalazarPins[] = {
    // state 1 (0x13760C): MotionMove; done -> HP test, jmp 0x1377E1; site 0x13763A (fld 87.7f), the window tests (jp/jne
    // rel32 to 0x1377E1), the hit block (call 0x718F with 0x226 at 0x137676, 0x5696, 0xADC1, 0x9020), add esp,0x34;
    // jmp 0x1377E1
    {0x137606, "fe86fe0000006a0056e8e5f5ecff83c40885c0741f8b0d3c6fc0006683b9b44f0000000f8eb2010000fe86fe000000e9a7010000d905"
               "8022b100d89e90020000dfe0f6c4050f8a90010000d9057c22b100d89e90020000dfe0f6c4410f85790100008b155470c5006a0068"
               "2602000052e814fbecff6a00e814e0ecff566a006a"},
    {0x137686, "008d8694000000506a056a05e82a37edffa13c6fc0008b48488b511c6a0103d16a0b52e87219edff83c434e92b010000"},
    // the join 0x1377E1 (the routine's common tail)
    {0x1377E1, "8b8e940000008b96980000008b869c000000898eb00300008b8e84030000898e80030000"},
    // 0x137480: [esi+0x3C0]+0x380, the state switch on [esi+0xFE] through 0x537824; state 1 -> 0x53760C
    {0x1374B3, "8b86c00300008b88800300000fb686fe000000898e8003000083f8030f870c030000ff248524785300"},
    {0x137828, "0c765300"},
    {0x7F8384, "f0bf5300"},    // em38 r1 table 0x7F8308, entry 0x1F -> 0x53BFF0 (em38_R1_T_CatchHit)
    {0x13C055, "6880745300"},  // push 0x537480 (SetPlDamage, tentacle type 2)
    {0x13C076, "6880745300"},  // push 0x537480 (SetPlDamage, the other tentacle)
    {0x71227C, "9a99b0426666af42"},   // 88.3f, 87.7f
};

// Saddler (em31) normal/stepping hand crush. State 1, its setup tail, the complete
// window + sound/vibration/900/voice block and the join are pinned. Attribution:
// em31 R1 pair table 0x7F6760 [2*0x0C+1]/[2*0x0E+1] -> SetPlDamage(0xED940/0xEDE70).
inline constexpr native_pins::Pin kSaddlerPins[] = {
    {0xEDA5D, "fe86fe0000006a0056e88e91f1ff83c40885c0741b8b0d3c6fc0006683b9b44f0000007e0bfe86fe000000e981010000d905681bb100d89e90020000dfe0f6c4057a7ed905641bb100d89e90020000dfe0f6c441756b8b96c00300000fb68200010000566a00508d8e94000000516a2e6a08e8edd2f1ffa1"},
    {0xEDAD5, "3c6fc0008b50488b4a1c6a0103ca6a0b51e835b5f1ff8b155470c5006a00688403000052e89196f1ffa13c6fc00083c4306683b8b44f0000007e046a00eb026a0de87b7bf1ff83c404d98690020000dc1d581bb100dfe0f6c4410f85d9000000d98690020000dc1d481bb100dfe0f6c4050f8ac2000000"},
    {0xED966, "0fb696fe0000008b8ec00300008b818003000089868003000083fa030f8786020000ff249544dc4e00"},
    {0xEDC48, "63da4e00"},
    {0x7F67C4, "504b4f00"},
    {0xF4BC8, "6840d94e0056e8aac9f0ff"},
    {0x711B64, "9a99ac426666ab42"},
};
inline constexpr native_pins::Pin kSaddlerStepPins[] = {
    {0xEDF8D, "fe86fe0000006a0056e85e8cf1ff83c40885c0741b8b0d3c6fc0006683b9b44f0000007e0bfe86fe000000e981010000d905681bb100d89e90020000dfe0f6c4057a7ed905641bb100d89e90020000dfe0f6c441756b8b96c00300000fb68200010000566a00508d8e94000000516a2e6a08e8bdcdf1ffa1"},
    {0xEE005, "3c6fc0008b50488b4a1c6a0103ca6a0b51e805b0f1ff8b155470c5006a00688403000052e86191f1ffa13c6fc00083c4306683b8b44f0000007e046a00eb026a0de84b76f1ff83c404d98690020000dc1d581bb100dfe0f6c4410f85d9000000d98690020000dc1d481bb100dfe0f6c4050f8ac2000000"},
    {0xEDE96, "0fb696fe0000008b8ec00300008b818003000089868003000083fa030f8786020000ff249574e14e00"},
    {0xEE178, "93df4e00"},
    {0x7F67D4, "004f4f00"},
    {0xF4F78, "6870de4e0056e8fac5f0ff"},
    {0x711B64, "9a99ac426666ab42"},
};
// Ganado neck hold, player state 3: MotionMove, still-held checks, preceding
// window, complete dynamic-damage block (LifeDownSet2) and the join.
inline constexpr native_pins::Pin kGanadoNeckPins[] = {
    {0x3F74F, "e87c98feff8b3d5470c5008945088b87c003000080b8fc000000010f85b0feffff80b8fd000000340f85a3feffffd9054001b100d89e90020000dfe0f6c4057a37d9053c01b100d89e90020000dfe0f6c44175240fb68600010000566a00508d8e94000000516a096a01e803b6fcff8b3d5470c50083c418"},
    {0x3F7C7, "d9053801b100d89e90020000dfe0f6c4050f8abb000000d9053401b100d89e90020000dfe0f6c4410f85a40000008bc6e804a8ffffdc0de0fbb000e8e9f46200f686d0030000048b0d3c6fc000740cf64154207506992bc2d1f8406683b9b44f0000327e046a01eb026a006a005057e88679fcff83c4106a"},
    {0x3F83F, "0056e8278cfcff83c40885c0754c0fb696000100005650528dbe94000000576a056a05e85ab5fcff0fb68600010000566a0050576a126a01e845b5fcff8b0d3c6fc00083c4306683b9b44f0000007f0a6a0de8005efcff83c404d905d000b100d89e90020000dfe0f6c4057a3ad905cc00b100d89e900200"},
    {0x3F8B7, "00dfe0f6c44175276a"},
    {0x3F527, "0fb68efe0000008b86c00300008b808003000089868003000083f9060f8765060000ff248de4fb4300"},
    {0x3FBF0, "4ff74300"},
    {0x73185, "6800f5430083ec0cd95c2408d9eed9542404d90554"},
    {0x731AD, "6800f5430083ec0cd95c2408d9eed9542404d90518"},
    {0x736B5, "6800f5430083ec0cd95c2408d9eed9542404d90510"},
    {0x710134, "33331542cdcc1242"},
};

struct Site {
    const char* name;
    uint32_t site, resume, skip, low, high, damage_call, damage;
    const native_pins::Pin* pins; uint32_t pin_count;
    uint32_t damage_target = 0x718F; bool damage_dynamic = false;
};
enum : uint32_t { kGigante = 0, kSalazar = 1, kSaddler = 2, kSaddlerStep = 3, kGanadoNeck = 4, kSites = 5 };
inline constexpr Site kSiteTable[kSites] = {
    {"gigante", 0xB6669, 0xB666F, 0xB66DA, 0x711498, 0x711494, 0xB669B, 800,
     kGigantePins, uint32_t(sizeof(kGigantePins) / sizeof(kGigantePins[0]))},
    {"salazar", 0x13763A, 0x137640, 0x1377E1, 0x712280, 0x71227C, 0x137676, 550,
     kSalazarPins, uint32_t(sizeof(kSalazarPins) / sizeof(kSalazarPins[0]))},
    {"saddler", 0xEDA8D, 0xEDA93, 0xEDB1E, 0x711B68, 0x711B64, 0xEDAF9, 900,
     kSaddlerPins, uint32_t(sizeof(kSaddlerPins) / sizeof(kSaddlerPins[0]))},
    {"saddler_step", 0xEDFBD, 0xEDFC3, 0xEE04E, 0x711B68, 0x711B64, 0xEE029, 900,
     kSaddlerStepPins, uint32_t(sizeof(kSaddlerStepPins) / sizeof(kSaddlerStepPins[0]))},
    {"ganado_neck", 0x3F7C7, 0x3F7CD, 0x3F899, 0x710138, 0x710134, 0x3F836, 0,
     kGanadoNeckPins, uint32_t(sizeof(kGanadoNeckPins) / sizeof(kGanadoNeckPins[0])), 0x71C1, true},
};
inline constexpr float kWindow[kSites][2] = {{86.7f, 87.3f}, {87.7f, 88.3f}, {85.7f, 86.3f}, {85.7f, 86.3f}, {36.7f, 37.3f}};

inline bool Jump(uint32_t from, uint32_t to, uint32_t len, uint8_t* out) {
    if (len < 5 || len > 16 || !out) return false;
    const uint32_t rel = to - (from + 5);
    out[0] = 0xE9; std::memcpy(out + 1, &rel, 4);
    for (uint32_t i = 5; i < len; ++i) out[i] = 0x90;
    return true;
}
// The native test: fld low; fcomp frame (C0 and ordered) and fld high; fcomp frame (neither C0 nor C3).
inline bool InWindow(float frame, float low, float high) { return low < frame && frame < high; }

// ---- the latch (pure) -------------------------------------------------------------------------------------------
enum class Step { Outside, Hit, Skip };
inline const char* Name(Step s) { return s == Step::Outside ? "outside" : s == Step::Hit ? "hit" : "skip"; }
constexpr uint32_t kSlots = 8;
struct Slot { uint32_t site = 0, pl = 0, tick = 0, seq = 0, skips = 0; bool used = false, latched = false, tick_known = false; };
struct Latch {
    Slot slots[kSlots];
    uint32_t seq = 0;
    Slot* Find(uint32_t site, uint32_t pl) {
        for (Slot& s : slots) if (s.used && s.site == site && s.pl == pl) return &s;
        return nullptr;
    }
    Slot& Take(uint32_t site, uint32_t pl) {
        if (Slot* s = Find(site, pl)) return *s;
        Slot* pick = &slots[0];
        for (Slot& s : slots) {
            if (!s.used) { pick = &s; break; }
            if (s.seq < pick->seq) pick = &s;   // least recently visited
        }
        *pick = Slot{}; pick->used = true; pick->site = site; pick->pl = pl;
        return *pick;
    }
};
inline Step Visit(Latch& l, uint32_t site, uint32_t pl, bool in_window, bool tick_known, uint32_t tick) {
    ++l.seq;
    if (!in_window) {
        if (Slot* s = l.Find(site, pl)) { s->latched = false; s->skips = 0; s->tick = tick; s->tick_known = tick_known; s->seq = l.seq; }
        return Step::Outside;
    }
    Slot& s = l.Take(site, pl);
    bool same = s.latched;
    if (same && tick_known && s.tick_known) {
        const int32_t gap = static_cast<int32_t>(tick - s.tick);
        same = gap >= 0 && static_cast<uint32_t>(gap) <= kMaxGapTicks;
    }
    s.tick = tick; s.tick_known = tick_known; s.seq = l.seq;
    if (same) { ++s.skips; return Step::Skip; }
    s.latched = true; s.skips = 0;
    return Step::Hit;
}

// Per window: every skip up to 8 (a window holds 3-4 at 165 FPS), then every 256th (a frozen frame).
inline bool ShouldLog(uint32_t skips) { return skips <= 8 || skips % 256 == 0; }
struct Facts { uint32_t pl = 0, em = 0; float frame = 0.0f; uint8_t em_id = 0xFF; bool read = false, em_read = false; };
inline int FormatLine(char* out, size_t n, Step step, uint32_t site, const Facts& f, uint32_t tick, bool tick_known, uint32_t skips) {
    const char* name = site < kSites ? kSiteTable[site].name : "?";
    char tail[32] = "";
    if (step == Step::Skip) std::snprintf(tail, sizeof(tail), " n=%u", skips);
    return std::snprintf(out, n, "grab_once %s site=%s pl=%p em=%p em_id=%02x frame=%.4f tick=%u%s%s",
        step == Step::Skip ? "skip" : "hit", name, reinterpret_cast<void*>(static_cast<uintptr_t>(f.pl)),
        reinterpret_cast<void*>(static_cast<uintptr_t>(f.em)), unsigned(f.em_id), double(f.frame), tick,
        tick_known ? "" : "?", tail);
}

enum class Verdict { Installed, Pins, Write, Site, Rollback };
inline const char* Name(Verdict v) {
    return v == Verdict::Installed ? "installed" : v == Verdict::Pins ? "pins_differ" : v == Verdict::Write ? "write" : v == Verdict::Rollback ? "rollback_failed" : "site";
}

// ---- runtime state (game thread) and the x86 stubs --------------------------------------------------------------
inline void (*g_line)(int level, const char* text) = nullptr;
inline uint32_t g_resume[kSites]{}, g_skip[kSites]{}, g_low_addr[kSites]{};
inline float g_low[kSites]{}, g_high[kSites]{};
inline bool g_armed[kSites]{};
inline uint32_t g_global_slot = 0;
inline uint32_t g_jump = 0, g_fld = 0, g_skip_now = 0;   // OnVisit's decision for CommonStub (game thread only)
inline uint32_t g_visits[kSites]{}, g_hits[kSites]{}, g_skips[kSites]{}, g_unread[kSites]{};
inline Latch g_latch;

#if defined(_MSC_VER) && defined(_M_IX86)
inline bool ReadFacts(uint32_t pl, Facts& f) {
    f = Facts{}; f.pl = pl;
    if (!pl) return false;
    __try {
        const auto* p = reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(pl));
        const uint32_t bits = *reinterpret_cast<const volatile uint32_t*>(p + off::kFrame);
        std::memcpy(&f.frame, &bits, 4);
        f.read = true;
        f.em = *reinterpret_cast<const volatile uint32_t*>(p + off::kCatcher);
    } __except (1) { return f.read; }
    if (f.em) {
        __try { f.em_id = *reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(f.em + off::kId)); f.em_read = true; }
        __except (1) { f.em_read = false; }
    }
    return f.read;
}
inline bool ReadTick(uint32_t& tick) {
    tick = 0;
    if (!g_global_slot) return false;
    __try {
        const uint32_t g = *reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(g_global_slot));
        if (!g) return false;
        tick = *reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(g + off::kTick));
        return true;
    } __except (1) { return false; }
}
// Called from CommonStub on every visit of a site (cdecl; registers, flags and FPU/SSE are saved around it).
inline void __cdecl OnVisit(uint32_t site, uint32_t pl) {
    g_skip_now = 0;
    if (site >= kSites) return;   // never: only EntryN push an index
    g_jump = g_resume[site]; g_fld = g_low_addr[site];
    ++g_visits[site];
    Facts f;
    if (!ReadFacts(pl, f)) { ++g_unread[site]; return; }   // unreadable: native test, latch untouched
    uint32_t tick = 0;
    const bool tick_known = ReadTick(tick);
    const Step step = Visit(g_latch, site, pl, InWindow(f.frame, g_low[site], g_high[site]), tick_known, tick);
    if (step == Step::Outside) return;
    uint32_t skips = 0;
    if (step == Step::Skip) {
        g_skip_now = 1; g_jump = g_skip[site]; ++g_skips[site];
        if (const Slot* s = g_latch.Find(site, pl)) skips = s->skips;
        if (site < kSaddler && !ShouldLog(skips)) return;  // new fixes log every action, including a frozen window
    } else {
        ++g_hits[site];
    }
    if (!g_line) return;
    char text[256];
    if (FormatLine(text, sizeof(text), step, site, f, tick, tick_known, skips) > 0) g_line(1, text);
}

// Entered with the site index pushed on top of the game's stack. pushfd/pushad, an aligned fxsave area, DF cleared and
// a clean x87/MXCSR for the C call (the FallDetour pattern of ztarget_runtime.h). After popad/popfd the index is
// dropped with lea (flags kept); the native path replays `fld dword [low]` (eax kept) and jumps to the resume, the skip
// path jumps to the join.
__declspec(naked) inline void CommonStub() {
    __asm {
        pushfd
        pushad
        cld
        mov eax,esp
        sub esp,528
        and esp,0fffffff0h
        mov [esp+512],eax
        fxsave [esp]
        mov edx,fs:[0x34]
        mov [esp+520],edx
        fninit
        mov dword ptr [esp+516],01f80h
        ldmxcsr [esp+516]
        push esi
        push dword ptr [eax+36]
        call OnVisit
        add esp,8
        mov edx,[esp+520]
        mov fs:[0x34],edx
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        cmp dword ptr [g_skip_now],0
        jne grab_skip
        popfd
        lea esp,[esp+4]
        push eax
        mov eax,dword ptr [g_fld]
        fld dword ptr [eax]
        pop eax
        jmp dword ptr [g_jump]
    grab_skip:
        popfd
        lea esp,[esp+4]
        jmp dword ptr [g_jump]
    }
}
__declspec(naked) inline void Entry0() { __asm { push 0
                                                 jmp CommonStub } }
__declspec(naked) inline void Entry1() { __asm { push 1
                                                 jmp CommonStub } }
__declspec(naked) inline void Entry2() { __asm { push 2
                                                 jmp CommonStub } }
__declspec(naked) inline void Entry3() { __asm { push 3
                                                 jmp CommonStub } }
__declspec(naked) inline void Entry4() { __asm { push 4
                                                 jmp CommonStub } }
inline void* EntryFor(uint32_t site) {
    return site == kGigante ? reinterpret_cast<void*>(&Entry0) : site == kSalazar ? reinterpret_cast<void*>(&Entry1) :
           site == kSaddler ? reinterpret_cast<void*>(&Entry2) : site == kSaddlerStep ? reinterpret_cast<void*>(&Entry3) :
           site == kGanadoNeck ? reinterpret_cast<void*>(&Entry4) : nullptr;
}

// Points site `site` at `base` and takes its window floats; clears its counters and its latch slots.
inline void Arm(uint32_t site, uint32_t base, float low, float high) {
    const Site& s = kSiteTable[site];
    g_resume[site] = base + s.resume; g_skip[site] = base + s.skip; g_low_addr[site] = base + s.low;
    g_low[site] = low; g_high[site] = high; g_armed[site] = true;
    g_global_slot = base + kGlobalSlot;
    g_visits[site] = g_hits[site] = g_skips[site] = g_unread[site] = 0;
    for (Slot& sl : g_latch.slots) if (sl.used && sl.site == site) sl = Slot{};
}
// Mem: bool Read(uint32_t, void*, size_t) and bool Write(uint32_t, const void*, size_t). The site's pins first, then
// its one 6-byte write. `bad_pin` = rva of the first pin that differs.
template <class Mem> Verdict Install(Mem& m, uint32_t site, uint32_t base, uint32_t& bad_pin) {
    bad_pin = 0;
    if (site >= kSites) return Verdict::Site;
    const Site& s = kSiteTable[site];
    for (uint32_t i = 0; i < s.pin_count; ++i)
        if (!native_pins::Matches(m, base, s.pins[i].rva, s.pins[i].hex)) { bad_pin = s.pins[i].rva; return Verdict::Pins; }
    float low = 0.0f, high = 0.0f;
    if (!m.Read(base + s.low, &low, 4) || !m.Read(base + s.high, &high, 4)) { bad_pin = s.high; return Verdict::Pins; }
    Arm(site, base, low, high);
    uint8_t code[16];
    const uint32_t at = base + s.site;
    if (!Jump(at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(EntryFor(site))), kSiteLen, code) || !m.Write(at, code, kSiteLen)) {
        g_armed[site] = false;
        return Verdict::Write;
    }
    return Verdict::Installed;
}
#endif
#if defined(_MSC_VER) && defined(_M_IX86)
// Both em31 sites form one setting. Preflight every pin and save both original
// instructions before the first write. A failing second write restores both
// originals and disarms both sites. Rollback is explicit if the memory backend
// also refuses restoration; never report that as an installed group.
template <class Mem> Verdict InstallSaddler(Mem& m, uint32_t base, uint32_t& bad_pin) {
    bad_pin = 0;
    uint8_t original[2][kSiteLen]{};
    for (uint32_t n = 0; n < 2; ++n) {
        const Site& s = kSiteTable[kSaddler + n];
        for (uint32_t i = 0; i < s.pin_count; ++i)
            if (!native_pins::Matches(m, base, s.pins[i].rva, s.pins[i].hex)) { bad_pin = s.pins[i].rva; return Verdict::Pins; }
        if (!m.Read(base + s.site, original[n], kSiteLen)) { bad_pin = s.site; return Verdict::Pins; }
    }
    Verdict v = Install(m, kSaddler, base, bad_pin);
    if (v == Verdict::Installed) v = Install(m, kSaddlerStep, base, bad_pin);
    if (v == Verdict::Installed) return v;
    bool restored = true;
    for (uint32_t n = 0; n < 2; ++n) {
        const uint32_t site = kSaddler + n;
        const Site& s = kSiteTable[site];
        uint8_t have[kSiteLen]{};
        if (!m.Read(base + s.site, have, kSiteLen) ||
            (std::memcmp(have, original[n], kSiteLen) != 0 && !m.Write(base + s.site, original[n], kSiteLen))) restored = false;
        g_armed[site] = false;
    }
    return restored ? v : Verdict::Rollback;
}
#endif
}  // namespace link_runtime::grab_once

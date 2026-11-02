// dll/case_key_debounce.h
#pragma once
// One press of the attache-case key (TAB) = one toggle of the case (case-key-debounce-01, 21-oct).
// Pinned bio4.exe 19aed4af...; RVAs, preferred base 0x400000. Static analysis, not a live capture.
//
// Cause. KEY_SSCRN is logical bit 0x100000 (name table 0x2950A1). The case opens on its TRG in
// SubScreenCall (0x36924B: Key_btn_trg & 0x100000 -> SubScreenOpen(1, 0)) and the inventory screen
// closes on the same TRG (sscrnKey2Game 0x3725F0: open_flag 1 -> trg & 0x100000). PadRead 0x567360
// (thunk 0x7509, called once per frame at 0x254B80, before the tasks) rebuilds Key_btn_on every
// frame from a keyboard binding set chosen by a "menu" predicate (0x567EFF: byte 0x806F39, or
// STA_SUB_SCRN Status0 0x40000, STA_TITLE Status2 0x8000, card Status3 0x800). GAMEPLAY binds TAB
// to KEY_SSCRN, MENUS does not (BIO4/input.ini: MENUS KEY_SSCRN = DELETE). Inside the case the
// gameplay KEY_SSCRN is re-added only while the SsPzzl screen lives (byte 0x806F3A, set by its
// init 0x38F240, cleared by its exit 0x38FAD0; read at 0x56930C). So a TAB held through the
// sub-screen fade disappears from Key_btn_on (STA_SUB_SCRN on at 0x367CFF, no SsPzzl yet), and
// when SsPzzl starts it reappears: the edge detector 0x56A4F0 (trg = (old ^ on) & on, old = on)
// makes a new KEY_SSCRN trigger without any new press, and the case closes again. Every
// transition repeats the same drop/return. Not frame-rate specific; keyboard autorepeat plays no
// part (the keyboard is polled with GetDeviceState, states 0/1/2/3 at 0x28E074).
//
// Fix. Right after PadRead (MinHook on its entry, game thread): the first KEY_SSCRN trigger
// passes and latches; while latched every KEY_SSCRN trigger is removed from Key_btn_trg. The
// latch drops only on a frame where the key is SEEN released (Key_btn_on can show it: gameplay
// set, or SsPzzl alive; not while keys are stopped or input is suspended) and at least
// `debounce_ms` of real time have passed since the toggle. A drop of the bit during a
// transition is therefore never taken for a release. kMaxLatchMs bounds a latch that never sees
// an observable frame. Real time, not game time: the debounce is about a physical key, and the
// sub-screen suspends the game task (game time does not run there). Native kick untouched.
#include "native_pins.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if defined(_MSC_VER) && defined(_M_IX86)
#include <windows.h>
#endif

namespace link_runtime::case_key_debounce {
namespace rva {
constexpr uint32_t kPadRead = 0x567360;         // PadRead entry: the hook target
constexpr uint32_t kKeyOn = 0x862F08, kKeyTrg = 0x862F10;   // Key_btn_on / Key_btn_trg (low dwords)
constexpr uint32_t kGlobalSlot = 0x806F3C;      // pG
constexpr uint32_t kMenuInput = 0x806F39;       // byte: menu binding set requested
constexpr uint32_t kCaseScreen = 0x806F3A;      // byte: SsPzzl alive, gameplay KEY_SSCRN re-added
constexpr uint32_t kInputSuspended = 0xE2286A;  // byte: input manager suspended (on/trg zeroed, old kept)
}  // namespace rva
namespace layout {
constexpr uint32_t kKeySscrn = 0x100000;
constexpr uint32_t kStopFlg = 0x170, kStatus0 = 0x501C, kStatus2 = 0x5024, kStatus3 = 0x5028;
constexpr uint32_t kSpfKey = 0x80000000u;       // Stop_flg: KeyClear(0) strips KEY_SSCRN from on/trg
constexpr uint32_t kStaSubScrn = 0x40000, kStaTitle = 0x8000, kCardTask = 0x800;
}  // namespace layout
constexpr uint32_t kDefaultMs = 150, kMaxMs = 1000;
constexpr uint64_t kMaxLatchMs = 4000;
inline bool ValidMs(uint32_t ms) { return ms <= kMaxMs; }

// Exact windows (relocation-normalized), equal in the raw PE and in the mapped image bb59e550...
// (tests/native/test_case_key_debounce.cpp checks the image). None overlaps a match of the ~700
// hook patterns carried by re4_tweaks' winmm.dll; the nearest, 0x254B64 (7 bytes), ends before
// 0x254B80, a window docs/fps-coexistence-2026-10-12.md found intact with re4_tweaks loaded.
inline constexpr native_pins::Pin kPins[] = {
    // PadRead entry (hooked), its thunk, and the main loop: PadRead, then the frame counter.
    {0x567360, "558bec81ec7c050000a1105ec30033c58945fc"},
    {0x7509, "e952fe5500"},
    {0x254B80, "e88429dbffe8d073dbff6870d1b1006a04e8ebf2daff83c418e84333dbffe8a5f4daffa13c6fc000ff800c530000"},
    // Input suspended: the manager returns false and PadRead zeroes on/trg, keeping old.
    {0x566BE0, "803d6a28220100578bf97459"},
    {0x5673C8, "e840f3a9ff8bc8e84f0faaff84c075345f8935082fc60089350c2fc6008935102fc6008935142fc60089351c30c60089352030c600"},
    // Menu predicate: 0x806F39 | Status0 0x40000 | Status2 0x8000 | Status3 0x800 selects the MENUS set.
    {0x567EFF, "8a0d396fc000a13c6fc00033f6bb01000000899de8faffff89b5ecfaffff8935082fc60089350c2fc600888dfffaffff84c975378b"
               "881c500000c1e91222cb888dfffaffff75248b9024500000c1ea0f22d38895fffaffff75118b8028500000c1e80b22c38885fffaffff"},
    // While 0x806F3A: the GAMEPLAY binding of 0x100000 (state 2 -> on).
    {0x56930C, "803d3a6fc000000f845f010000e8bbd5a9ff8bc8e872bda9ff8bf085f60f8449010000837e08000f843f0100008d85b8faffffbf0000"
               "100033db508bce89bdb8"},
    {0x56938D, "8b401825ff01000033c98bd00bd174358b84857cfbffff83f80175128b0d142fc600093d102fc600890d142fc60083f80275128b150c"
               "2fc600093d082fc60089150c2fc600"},
    // The edge: trg = (old ^ on) & on, rel, old = on.
    {0x56A4F0, "8b1d002fc6008b15042fc6008bc333c18bf023f133d78935102fc6008bf22315042fc60023f723c333db8935142fc600a3182fc600"
               "89151c2fc600890d002fc600893d042fc600"},
    // SPF_KEY -> KeyClear(0) after the edge; KeyClear masks on/trg/rel/rep, never old.
    {0x56A790, "a13c6fc000b9000000808588700100007575"},
    {0x56A817, "0988700100006a006a00e81affa9ffa13c6fc00083c408"},
    {0x565610, "558bec8b4d088b450c8bd10bd0740d890d282a2201a32c2a2201eb0ba12c2a22018b0d282a220121050c2fc6002105142fc6002105"
               "1c2fc6002105242fc60021052c2fc600a13c6fc000210d082fc600210d102fc600210d182fc600210d202fc600210d282fc600"},
    // Consumers: open (SubScreenCall) and close (sscrnKey2Game) on trg & 0x100000.
    {0x36924B, "a1102fc600d91d14adc70033c925000010000bc15174046a01eb226800002000e89f59caff83c40884c074196a4ae857f3c9ff83c4"
               "0485c0750b506a02e80446caff"},
    {0x3725F0, "558bec8b450833c983782c01a1102fc600750d25000010000bc17411b0015dc325000010400bc17404b0015dc3"},
    // KEY_SSCRN = 0x100000 in the key-name table; SsPzzl init/exit; SubScreenExec sets STA_SUB_SCRN.
    {0x2950A1, "c745f4000010008975f8e86c1bd7ffc7001403b200"},
    {0x38F240, "558bec83ec545356578bf16a108975f4bf01000000c6053a6fc00001"},
    {0x38FAD0, "558bec53568b75085733db53568bf9881d3a6fc000"},
    {0x367CFA, "a13c6fc00081881c50000000000400"},
};

// Everything the decision needs from one frame, read right after PadRead.
struct Context {
    bool global = false;            // pG non-null and its words readable
    uint8_t menu_input = 0, case_screen = 0, suspended = 0;
    uint32_t stop = 0, status0 = 0, status2 = 0, status3 = 0;
};
// True when Key_btn_on & KEY_SSCRN equals the physical case key this frame: input polled, keys not
// stopped, and either the GAMEPLAY set is active or SsPzzl re-adds the GAMEPLAY KEY_SSCRN.
inline bool Observable(const Context& c) {
    if (!c.global || c.suspended || (c.stop & layout::kSpfKey)) return false;
    const bool menu = c.menu_input || (c.status0 & layout::kStaSubScrn) || (c.status2 & layout::kStaTitle) ||
                      (c.status3 & layout::kCardTask);
    return !menu || c.case_screen;
}
struct Sample {
    bool readable = false, observable = false;
    uint32_t on = 0, trg = 0;      // low dwords; KEY_SSCRN is bit 20
    uint8_t context = 0;           // log only: 1 menu byte, 2 STA_SUB_SCRN, 4 SsPzzl, 8 SPF_KEY, 16 suspended
};
enum class Event : uint8_t { None, Pass, Mask, Release };
struct Decision { bool mask = false; Event event = Event::None; uint64_t age_ms = 0; };

class Debounce {
    bool latched_ = false;
    uint64_t since_ = 0;
public:
    bool Latched() const { return latched_; }
    // `now_ms`: real time in ms. Unreadable frames change nothing (the game keeps its trigger).
    Decision Step(const Sample& s, uint64_t now_ms, uint32_t debounce_ms) {
        Decision d;
        if (!s.readable) return d;
        if (latched_) {
            if (now_ms < since_) since_ = now_ms;   // clock went back: count again from now
            d.age_ms = now_ms - since_;
            const bool seen_up = s.observable && !(s.on & layout::kKeySscrn);
            if ((seen_up && d.age_ms >= debounce_ms) || d.age_ms >= kMaxLatchMs) {
                latched_ = false; d.event = Event::Release;
            }
        }
        if (s.trg & layout::kKeySscrn) {
            if (latched_) { d.mask = true; d.event = Event::Mask; }
            else { latched_ = true; since_ = now_ms; d.event = Event::Pass; d.age_ms = 0; }
        }
        return d;
    }
};

template <class Mem> bool Read(Mem& m, uint32_t base, Sample& s) {
    s = Sample{};
    Context c;
    uint32_t global = 0;
    if (!m.Read(base + rva::kKeyOn, &s.on, 4) || !m.Read(base + rva::kKeyTrg, &s.trg, 4) ||
        !m.Read(base + rva::kMenuInput, &c.menu_input, 1) || !m.Read(base + rva::kCaseScreen, &c.case_screen, 1) ||
        !m.Read(base + rva::kInputSuspended, &c.suspended, 1) || !m.Read(base + rva::kGlobalSlot, &global, 4)) return false;
    if (global) {
        if (!m.Read(global + layout::kStopFlg, &c.stop, 4) || !m.Read(global + layout::kStatus0, &c.status0, 4) ||
            !m.Read(global + layout::kStatus2, &c.status2, 4) || !m.Read(global + layout::kStatus3, &c.status3, 4)) return false;
        c.global = true;
    }
    s.observable = Observable(c);
    s.context = static_cast<uint8_t>((c.menu_input ? 1 : 0) | ((c.status0 & layout::kStaSubScrn) ? 2 : 0) |
                                     (c.case_screen ? 4 : 0) | ((c.stop & layout::kSpfKey) ? 8 : 0) | (c.suspended ? 16 : 0));
    s.readable = true;
    return true;
}
// Removes KEY_SSCRN from Key_btn_trg only (Key_btn_on keeps the held state; old is PadRead's).
template <class Mem> bool MaskTrigger(Mem& m, uint32_t base) {
    uint32_t low = 0;
    if (!m.Read(base + rva::kKeyTrg, &low, 4)) return false;
    low &= ~layout::kKeySscrn;
    return m.Write(base + rva::kKeyTrg, &low, 4);
}

// Runtime state: written by Install on the worker before the hook is enabled, then only by the
// game thread (the PadRead caller).
inline void (*g_line)(int level, const char* text) = nullptr;
inline uint64_t (*g_now)() = nullptr;           // real-time ms clock (GetTickCount64 in the DLL)
inline uint32_t g_base = 0, g_debounce_ms = kDefaultMs;
inline Debounce g_state;
inline uint32_t g_passed = 0, g_masked = 0, g_unread = 0, g_write_fail = 0;

// One frame after PadRead. Logs: masked edges at LogLevel 1 (only when the old bug would toggle),
// passes and releases at LogLevel 2.
template <class Mem> Decision Tick(Mem& m) {
    Decision d;
    Sample s;
    if (!g_now || !g_base || !Read(m, g_base, s)) { ++g_unread; return d; }
    d = g_state.Step(s, g_now(), g_debounce_ms);
    if (d.mask && !MaskTrigger(m, g_base)) ++g_write_fail;
    if (d.event == Event::Pass) ++g_passed;
    if (d.event == Event::Mask) ++g_masked;
    if (!g_line || d.event == Event::None) return d;
    char text[192];
    if (d.event == Event::Mask)
        std::snprintf(text, sizeof(text), "case_key masked n=%u age_ms=%llu observable=%u held=%u ctx=0x%X write_fail=%u",
                      g_masked, static_cast<unsigned long long>(d.age_ms), s.observable ? 1U : 0U,
                      (s.on & layout::kKeySscrn) ? 1U : 0U, unsigned(s.context), g_write_fail);
    else if (d.event == Event::Pass)
        std::snprintf(text, sizeof(text), "case_key pass n=%u ctx=0x%X", g_passed, unsigned(s.context));
    else
        std::snprintf(text, sizeof(text), "case_key release age_ms=%llu observable=%u ctx=0x%X",
                      static_cast<unsigned long long>(d.age_ms), s.observable ? 1U : 0U, unsigned(s.context));
    g_line(d.event == Event::Mask ? 1 : 2, text);
    return d;
}

enum class Verdict { Installed, Pins, Hook };
inline const char* Name(Verdict v) { return v == Verdict::Installed ? "installed" : v == Verdict::Pins ? "pins_differ" : "hook"; }

#if defined(_MSC_VER) && defined(_M_IX86)
inline void(__cdecl* g_original)() = nullptr;   // MinHook trampoline of PadRead
// Run after each PadRead, menus included (2-nov: the TP hearts drop their units in the case, where the player frame
// that pauses them does not run). Set by the DLL; must not throw.
inline void(*g_after)() = nullptr;
struct LiveMemory {
    bool Read(uint32_t at, void* out, size_t n) {
        __try { std::memcpy(out, reinterpret_cast<const void*>(static_cast<uintptr_t>(at)), n); return true; }
        __except (1) { return false; }
    }
    bool Write(uint32_t at, const void* in, size_t n) {
        __try { std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(at)), in, n); return true; }
        __except (1) { return false; }
    }
};
// void __cdecl PadRead(void): the native body runs first, then this frame's decision.
__declspec(noinline) inline void __cdecl Detour() {
    g_original();
    const DWORD error = GetLastError();
    LiveMemory m;
    Tick(m);
    if (g_after) g_after();
    SetLastError(error);
}
// All pins or nothing, then `hook(target, detour, original_slot)` creates and enables the hook
// (MinHook in the DLL); it must store the trampoline in *original_slot before enabling.
template <class Mem, class Hooker>
Verdict Install(Mem& m, uint32_t base, uint32_t debounce_ms, uint32_t& bad_pin, Hooker&& hook) {
    bad_pin = 0;
    for (const auto& p : kPins)
        if (!native_pins::Matches(m, base, p.rva, p.hex)) { bad_pin = p.rva; return Verdict::Pins; }
    g_base = base; g_debounce_ms = ValidMs(debounce_ms) ? debounce_ms : kDefaultMs; g_state = Debounce{};
    g_passed = g_masked = g_unread = g_write_fail = 0;
    if (!hook(reinterpret_cast<void*>(static_cast<uintptr_t>(base + rva::kPadRead)), reinterpret_cast<void*>(&Detour),
              reinterpret_cast<void**>(&g_original)) || !g_original) {
        g_original = nullptr; g_base = 0; return Verdict::Hook;
    }
    return Verdict::Installed;
}
#endif
}  // namespace link_runtime::case_key_debounce

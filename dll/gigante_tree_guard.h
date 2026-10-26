// dll/gigante_tree_guard.h
#pragma once
// gigante-tree-guard-01 (19-oct-2026, docs/gigante-tree-guard-01-2026-10-19.md): a null check the game lacks in El
// Gigante's tree attack (em2b, r119), plus a watcher that records when the held tree pointer disappears.
//
// Crash 19-oct 22:27 (link_crash_11867890.dmp): eip bio4+0x57254F (PSMTXMultVec) reading 0x98 with the vector 0x94,
// called from em2b r1 routine 0x0E (0xBB550, table 0x7F5518 entry 0x0E, dispatcher 0xAB710 by byte [em+0xFD]) at
// 0xBB7CB: `lea ecx,[edi+0x94]` with edi = [em+0x908] = 0. Routine 0x0E loads edi = [em+0x908] (the held tree, set by
// routine 0x0D at 0xBB1A8 from [em+0x910]) once at entry and, in phase 1 ([em+0xFE] = 1), uses it unchecked:
//   0xBB6D9  cmp word [edi+0x324],1 ... (when byte [em+0x28B] & 1 and one of two calls says so)   -> site "hp"
//   0xBB78B  when the timer [em+0x40C] crosses 0: release the tree (0x14C4 with edi, 0xADC1 with &edi->pos,
//            0xB852 with ecx = edi) and then [em+0x908] = 0                                         -> site "release"
// The code after the release (0xBB7ED) already tests edi for 0. Who cleared [em+0x908] before the timer fired is
// NOT known (writers: 0xAC55D, 0xAF63F, 0xB1334, 0xB2862, 0xBDE41, 0xBE011, 0xBB7DA).
//
// The guard (raw jumps to the naked stubs below, as the rupee/ocarina call patches do; no MinHook):
//   hp       0xBB6D9 (8 bytes) -> test edi,edi; jz skip; cmp word [edi+0x324],1; jmp 0xBB6E1
//                                 skip: log, jmp 0xBB734 (the native "not above 1" branch)
//   release  0xBB78B (10 bytes) -> test edi,edi; jz skip; test dword [esi+0x408],0x200; jmp 0xBB795
//                                 skip: log, [esi+0x908] = 0 (what the native block ends with), jmp 0xBB7E4
//   watch    0xBB56E (6 bytes) -> mov edi,[esi+0x908]; record the entry (flags, registers and FPU/SSE kept); jmp 0xBB574
// With edi != 0 the native instruction runs with the same registers and sets the same flags, then native code
// continues at the next instruction: byte for byte the vanilla behaviour. The stack is the same at the skip
// targets (both are native branch targets reached with this frame). Every byte any site, resume or skip relies on
// is pinned (kPins); one mismatch and nothing is written. [LinkRuntime] GiganteTreeGuard (missing or 1 = on).
#include "native_pins.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace link_runtime::gigante_tree {
namespace rva {
constexpr uint32_t kRoutine = 0xBB550, kTableEntry = 0x7F5550, kDispatchLoad = 0xAB716;
constexpr uint32_t kWatchSite = 0xBB56E, kWatchResume = 0xBB574;
constexpr uint32_t kHpSite = 0xBB6D9, kHpResume = 0xBB6E1, kHpSkip = 0xBB734;
constexpr uint32_t kReleaseSite = 0xBB78B, kReleaseResume = 0xBB795, kReleaseSkip = 0xBB7E4;
}  // namespace rva
namespace off {
constexpr uint32_t kR0 = 0xFC, kId = 0x100, kFlags28B = 0x28B, kFlags408 = 0x408, kTimer = 0x40C;
constexpr uint32_t kTree = 0x908, kThrown = 0x90C, kPending = 0x910;
}  // namespace off
constexpr uint8_t kEmId = 0x2B, kRoutineR1 = 0x0E;

// Exact bytes of bio4.exe 1.1.0 (SHA 19aed4af...) at the preferred base (absolute operands are normalised by
// native_pins/image_relocation, so they hold at any base).
inline constexpr native_pins::Pin kPins[] = {
    // routine 0x0E: prologue, edi = [esi+0x908] (watch site 0xBB56E), the phase switch on [esi+0xFE]
    {0xBB550, "558becd9ee83ec0c568b7508838e08040000100fb686fe00000083e800578bbe08090000740f48ddd80f841c0100005f5e8be55dc3"},
    // phase 1: test [esi+0x28B]&1 ... cmp word [edi+0x324],1 (hp site 0xBB6D9); jle 0xBB734; hp = 1 ...
    {0xBB6B7, "f6868b0200000153747ae83a98ffff568bd8e8222fffff83c40485db750485c0745b6683bf24030000017e51b801000000668987240300008b8e08090000"},
    // ... the 0x14C4 effect, or [esi+0x408],0x200; hp skip target 0xBB734 = or [esi+0x3D0],4
    {0xBB6F5, "6a02e8ed74f4ffd9ee6a00d990ac00000057d990b00000006a00d998b40000006a006a0a6a016a006a006aff57e89d5df4ff83c428818e0804000000020000838ed003000004"},
    // the timer [esi+0x40C] -= deltaTime, 0xE1E7, then the release (site 0xBB78B: test [esi+0x408],0x200) ...
    {0xBB762, "d9ee8d8e0c040000d819dfe0f6c4057a71d9018b153c6fc000d862706a00d919e8602af5ff84c07459f78608040000000200006a00576a006a0074046a0eeb026a0d6a016a006a006aff57e8125df4ff"},
    // ... 0xADC1 with &edi->pos (the crash), 0xB852, [esi+0x908] = 0; release skip target 0xBB7E4 (edi tested at 0xBB7ED)
    {0xBB7B2, "0fb6860001000083c428576a00508d8f94000000516a316a08e8f1f5f4ff83c4188bcfe87800f5ffc7860809000000000000f6868b02000002747d85ff7479"},
    {rva::kTableEntry, "50b54b00"},                       // em2b r1 table 0x7F5518, entry 0x0E -> 0x4BB550
    {rva::kDispatchLoad, "0fb688fd0000008b148d1855bf00"},  // movzx ecx,byte [eax+0xFD]; mov edx,[ecx*4+0xBF5518]
};

struct Site { const char* name; uint32_t site, len, resume, skip; };
enum : uint32_t { kRelease = 0, kHp = 1, kWatch = 2, kSites = 3 };
inline constexpr Site kSiteTable[kSites] = {
    {"release", rva::kReleaseSite, 10, rva::kReleaseResume, rva::kReleaseSkip},
    {"hp", rva::kHpSite, 8, rva::kHpResume, rva::kHpSkip},
    {"watch", rva::kWatchSite, 6, rva::kWatchResume, 0},
};

// E9 rel32 from `from` to `to`, the rest of the replaced instruction(s) filled with NOP (never executed: nothing
// branches into a replaced range; tests/test_gigante_tree_guard.py checks every branch of the routine).
inline bool Jump(uint32_t from, uint32_t to, uint32_t len, uint8_t* out) {
    if (len < 5 || len > 16 || !out) return false;
    const uint32_t rel = to - (from + 5);
    out[0] = 0xE9; std::memcpy(out + 1, &rel, 4);
    for (uint32_t i = 5; i < len; ++i) out[i] = 0x90;
    return true;
}

// What the em looked like (read with SEH on the game thread).
struct Facts {
    uint32_t em = 0, tree = 0, thrown = 0, pending = 0, flags408 = 0;
    uint8_t r[4]{}, id = 0, flags28b = 0;
    float timer = 0.0f;
    bool read = false;
};
struct Snapshot { Facts f; uint32_t entry = 0; bool valid = false; };

// Watch: one slot per em seen in routine 0x0E (two Gigantes can share a room). A visit is a run of entries less than
// kGapMs apart; a new visit starts clean.
constexpr uint32_t kSlots = 4, kGapMs = 250;
enum class Seen { Holding, Cleared, EnteredEmpty, StillEmpty };
inline const char* Name(Seen s) {
    switch (s) {
    case Seen::Holding: return "holding"; case Seen::Cleared: return "cleared";
    case Seen::EnteredEmpty: return "entered_empty"; default: return "still_empty";
    }
}
struct Slot {
    uint32_t em = 0, entries = 0, last_ms = 0, prev_tree = 0;
    Snapshot last_tree, first_zero;
    bool used = false;
};
struct Watch {
    Slot slots[kSlots];
    Slot* Find(uint32_t em) {
        for (Slot& s : slots) if (s.used && s.em == em) return &s;
        return nullptr;
    }
    Slot& Take(uint32_t em, uint32_t now_ms) {
        if (Slot* s = Find(em)) return *s;
        Slot* pick = &slots[0];
        for (Slot& s : slots) {
            if (!s.used) { pick = &s; break; }
            if (now_ms - s.last_ms > now_ms - pick->last_ms) pick = &s;
        }
        *pick = Slot{}; pick->used = true; pick->em = em; pick->last_ms = now_ms;
        return *pick;
    }
};
// One entry of routine 0x0E (tree = edi as just loaded).
inline Seen Observe(Watch& w, const Facts& f, uint32_t now_ms) {
    Slot& s = w.Take(f.em, now_ms);
    const bool fresh = s.entries == 0 || now_ms - s.last_ms > kGapMs;
    if (fresh) { const uint32_t em = s.em; s = Slot{}; s.used = true; s.em = em; }
    ++s.entries; s.last_ms = now_ms;
    Seen out;
    if (f.tree) { s.last_tree = {f, s.entries, true}; s.first_zero = {}; out = Seen::Holding; }
    else if (fresh) { s.first_zero = {f, s.entries, true}; out = Seen::EnteredEmpty; }
    else if (s.prev_tree) { s.first_zero = {f, s.entries, true}; out = Seen::Cleared; }
    else out = Seen::StillEmpty;
    s.prev_tree = f.tree;
    return out;
}

// Skip log policy: the first four skips of a site, then every 256th (the hp site can skip every frame).
inline bool ShouldLog(uint32_t count) { return count <= 4 || count % 256 == 0; }

inline int FormatSkip(char* out, size_t n, uint32_t site, const Facts& f, uint32_t count) {
    return std::snprintf(out, n,
        "gigante_tree_guard skip site=%s em=%p r=%02x r0=%02x r2=%02x r3=%02x id=%02x timer=%.4f tree=%08x thrown=%08x "
        "pending=%08x f408=%08x f28b=%02x read=%u count=%u",
        site < kSites ? kSiteTable[site].name : "?", reinterpret_cast<void*>(static_cast<uintptr_t>(f.em)),
        unsigned(f.r[1]), unsigned(f.r[0]), unsigned(f.r[2]), unsigned(f.r[3]), unsigned(f.id), double(f.timer), f.tree,
        f.thrown, f.pending, f.flags408, unsigned(f.flags28b), f.read ? 1U : 0U, count);
}
// The watch context of the em at a skip: the last entry that still held a tree and the first one without it.
inline int FormatContext(char* out, size_t n, const Slot* s) {
    if (!s) return std::snprintf(out, n, "gigante_tree_guard watch none");
    const Snapshot& a = s->last_tree; const Snapshot& z = s->first_zero;
    return std::snprintf(out, n,
        "gigante_tree_guard watch em=%p entries=%u held=%u held_ago=%u held_tree=%08x held_r=%02x/%02x held_timer=%.4f "
        "held_thrown=%08x held_pending=%08x zero=%u zero_ago=%u zero_r=%02x/%02x zero_timer=%.4f zero_thrown=%08x zero_pending=%08x",
        reinterpret_cast<void*>(static_cast<uintptr_t>(s->em)), s->entries, a.valid ? 1U : 0U,
        a.valid ? s->entries - a.entry : 0U, a.f.tree, unsigned(a.f.r[1]), unsigned(a.f.r[2]), double(a.f.timer),
        a.f.thrown, a.f.pending, z.valid ? 1U : 0U, z.valid ? s->entries - z.entry : 0U, unsigned(z.f.r[1]),
        unsigned(z.f.r[2]), double(z.f.timer), z.f.thrown, z.f.pending);
}
inline int FormatWatch(char* out, size_t n, Seen seen, const Facts& f, uint32_t prev_tree) {
    return std::snprintf(out, n,
        "gigante_tree_watch %s em=%p prev_tree=%08x r=%02x r0=%02x r2=%02x r3=%02x id=%02x timer=%.4f thrown=%08x pending=%08x",
        Name(seen), reinterpret_cast<void*>(static_cast<uintptr_t>(f.em)), prev_tree, unsigned(f.r[1]), unsigned(f.r[0]),
        unsigned(f.r[2]), unsigned(f.r[3]), unsigned(f.id), double(f.timer), f.thrown, f.pending);
}

enum class Verdict { Installed, Pins, Write };
inline const char* Name(Verdict v) { return v == Verdict::Installed ? "installed" : v == Verdict::Pins ? "pins_differ" : "write"; }

// ---- runtime state (game thread) and the x86 stubs -------------------------------------------------------------
// Sinks: `line(level, text)` with level 1 = normal log, 2 = verbose; `now()` in ms. Set before Install.
inline void (*g_line)(int level, const char* text) = nullptr;
inline uint32_t (*g_now)() = nullptr;
inline uint32_t g_resume[kSites]{}, g_skip[kSites]{};
inline uint32_t g_skips[kSites]{}, g_entries = 0;
inline Watch g_watch;

#if defined(_MSC_VER) && defined(_M_IX86)
inline bool ReadFacts(uint32_t em, Facts& f) {
    f = Facts{}; f.em = em;
    if (!em) return false;
    __try {
        const auto* p = reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(em));
        for (int i = 0; i < 4; ++i) f.r[i] = p[off::kR0 + i];
        f.id = p[off::kId]; f.flags28b = p[off::kFlags28B];
        f.flags408 = *reinterpret_cast<const volatile uint32_t*>(p + off::kFlags408);
        const uint32_t t = *reinterpret_cast<const volatile uint32_t*>(p + off::kTimer); std::memcpy(&f.timer, &t, 4);
        f.tree = *reinterpret_cast<const volatile uint32_t*>(p + off::kTree);
        f.thrown = *reinterpret_cast<const volatile uint32_t*>(p + off::kThrown);
        f.pending = *reinterpret_cast<const volatile uint32_t*>(p + off::kPending);
        f.read = true;
    } __except (1) { f.read = false; }
    return f.read;
}
inline uint32_t Now() { return g_now ? g_now() : 0; }
// Called from the skip stubs (cdecl; every register, the flags and the FPU/SSE state are saved around it).
inline void __cdecl OnSkip(uint32_t site, uint32_t em) {
    if (site >= kSites) return;
    const uint32_t count = ++g_skips[site];
    if (!g_line || !ShouldLog(count)) return;
    Facts f; ReadFacts(em, f);
    char text[512];
    if (FormatSkip(text, sizeof(text), site, f, count) > 0) g_line(1, text);
    if (FormatContext(text, sizeof(text), g_watch.Find(em)) > 0) g_line(1, text);
}
// Called from the watch stub on every entry of routine 0x0E (tree = edi just loaded).
inline void __cdecl OnEntry(uint32_t em, uint32_t tree) {
    ++g_entries;
    Facts f; ReadFacts(em, f); f.tree = tree;
    const Slot* before = g_watch.Find(em);
    const uint32_t prev = before ? before->prev_tree : 0;
    const Seen seen = Observe(g_watch, f, Now());
    if (seen == Seen::Holding || seen == Seen::StillEmpty || !g_line) return;
    char text[512];
    if (FormatWatch(text, sizeof(text), seen, f, prev) > 0) g_line(2, text);
}

// The C calls from the stubs keep every register, the flags (DF cleared for the call, popfd restores it) and the
// x87/SSE state (fxsave/fxrstor; the routine has fldz live on the x87 stack at the watch site): the FallDetour
// pattern of ztarget_runtime.h.
__declspec(naked) inline void ReleaseStub() {
    __asm {
        test edi,edi
        jz release_skip
        test dword ptr [esi+0x408],0x200
        jmp dword ptr [g_resume+0]
    release_skip:
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
        push 0
        call OnSkip
        add esp,8
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        mov dword ptr [esi+0x908],0
        jmp dword ptr [g_skip+0]
    }
}
__declspec(naked) inline void HpStub() {
    __asm {
        test edi,edi
        jz hp_skip
        cmp word ptr [edi+0x324],1
        jmp dword ptr [g_resume+4]
    hp_skip:
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
        push 1
        call OnSkip
        add esp,8
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        jmp dword ptr [g_skip+4]
    }
}
__declspec(naked) inline void WatchStub() {
    __asm {
        mov edi,dword ptr [esi+0x908]
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
        push edi
        push esi
        call OnEntry
        add esp,8
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        jmp dword ptr [g_resume+8]
    }
}

inline void* StubFor(uint32_t site) {
    return site == kRelease ? reinterpret_cast<void*>(&ReleaseStub) : site == kHp ? reinterpret_cast<void*>(&HpStub)
         : reinterpret_cast<void*>(&WatchStub);
}
// Points the stubs at `base` (resume and skip targets) and clears the counters and the watch.
inline void Arm(uint32_t base) {
    for (uint32_t i = 0; i < kSites; ++i) {
        g_resume[i] = base + kSiteTable[i].resume;
        g_skip[i] = kSiteTable[i].skip ? base + kSiteTable[i].skip : 0;
        g_skips[i] = 0;
    }
    g_entries = 0; g_watch = Watch{};
}
// Mem: bool Read(uint32_t, void*, size_t) and bool Write(uint32_t, const void*, size_t). All pins first; then the
// guard sites (release, hp) and the watch. `bad_pin` = rva of the first pin that differs.
template <class Mem> Verdict Install(Mem& m, uint32_t base, uint32_t& bad_pin, uint32_t& written) {
    bad_pin = 0; written = 0;
    for (const auto& p : kPins)
        if (!native_pins::Matches(m, base, p.rva, p.hex)) { bad_pin = p.rva; return Verdict::Pins; }
    Arm(base);
    for (uint32_t i = 0; i < kSites; ++i) {
        uint8_t code[16];
        const uint32_t at = base + kSiteTable[i].site;
        if (!Jump(at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(StubFor(i))), kSiteTable[i].len, code) ||
            !m.Write(at, code, kSiteTable[i].len))
            return Verdict::Write;
        ++written;
    }
    return Verdict::Installed;
}
#endif
}  // namespace link_runtime::gigante_tree

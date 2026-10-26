#pragma once
// Mortal Draw prompt ("[E] MORTAL DRAW") and its key: pure logic, no hooks, no game writes.
// Pinned bio4.exe SHA 19aed4af...; every address is an RVA (preferred base 0x400000):
// live address = module base + RVA. Spec and evidence: docs/mortal-draw-prompt-2026-10-12.md.
//
// The prompt is a native cActionButton offer queued by us at the arbiter entry (0x18AB40),
// only when no native offer is queued, with func = NULL: the native arbiter draws it (message
// kind + 0x16 in core.udas MDT #6, key icon of the button kind through the cockpit) and never
// runs anything for it (0x18ABF3 skips a NULL func). Native offers keep absolute priority: with
// any queued offer, the lock or the arbiter early-outs, nothing is injected.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "mortal_draw_context.h"

namespace link_runtime::mortal_draw::prompt {
namespace layout {
// cActionButton::set body (thunk 0xCFDB; cdecl wrapper 0x18AD40 → thunk 0x6465). thiscall, ECX =
// manager 0x8502C0, 8 stack args (kind, slot, func, arg, flags, btn, type, d), RET 0x20.
// Pulls work [+0x40] (max 8, 0x18AA16), clamps kind >= 0x51 to 0x50 (0x18AA39), AddPrim on ot[slot].
constexpr uint32_t set_rva = 0x18AA10, set_thunk_rva = 0xCFDB;
constexpr uint8_t set_prologue[10] = {0x55, 0x8B, 0xEC, 0x8A, 0x41, 0x40, 0x3C, 0x08, 0x0F, 0x83};
// ACT_WORK (0x18 bytes) array at manager +0x44: +4 func, +0xC kind, +0xD slot, +0xE type, +0xF btn, +0x14 flags.
constexpr uint32_t works = 0x44, work_size = 0x18, w_func = 0x04, w_kind = 0x0C, w_btn = 0x0F, w_flags = 0x14;
// Kinds 0x44..0x50 are the unused "aaa" messages 0x5A..0x66 of core.udas MDT #6 (GC enum
// ACT_padding_68..80). No literal set() call in the EXE uses a kind above 0x42 (155 sites).
constexpr uint8_t kind_action = 0x44;    // message 0x5A "MORTAL DRAW "      (icon of KEY_A)
constexpr uint8_t kind_physical = 0x45;  // message 0x5B "[F] MORTAL DRAW "  (text only)
constexpr uint8_t message_base = 0x16;
// DISP_FLAG: 1 = A normal (icon = glyph of the logical action key), 0 = no icon.
constexpr uint8_t disp_a_normal = 1, disp_off = 0;
// Logical KEY_CK = 0x400 (constructor 0x294F61). Default keyboard binds both KEY_A and KEY_CK to E.
constexpr uint64_t key_check = 0x400ULL;
}  // namespace layout

struct OfferArgs { uint8_t kind = 0, slot = 0, btn = 0, type = 0; uint32_t flags = 0; };

// ---------------------------------------------------------------- key configuration
enum class KeyMode { Action, Physical };
enum class KeyWhy { Default, SameAsNative, Unbound, Unverified, Invalid, Reserved, Clash };
struct KeyChoice { KeyMode mode = KeyMode::Action; int vk = 0; KeyWhy why = KeyWhy::Default; };

inline bool EqualNoCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = char(x - 32);
        if (y >= 'a' && y <= 'z') y = char(y - 32);
        if (x != y) return false;
    }
    return !*a && !*b;
}

// MortalDrawKey: "Action" (logical action button), a letter, a digit, F1..F12 or 0xNN. -1 invalid, 0 = Action.
inline int ParseKey(const char* v) {
    if (!v || !*v || EqualNoCase(v, "Action")) return 0;
    if (!v[1]) {
        const char c = v[0];
        if (c >= 'A' && c <= 'Z') return c;
        if (c >= 'a' && c <= 'z') return c - 32;
        if (c >= '0' && c <= '9') return c;
        return -1;
    }
    if ((v[0] == 'F' || v[0] == 'f') && v[1] >= '1' && v[1] <= '9') {
        int n = v[1] - '0';
        if (v[2]) { if (v[3] || v[2] < '0' || v[2] > '9') return -1; n = n * 10 + (v[2] - '0'); }
        return n >= 1 && n <= 12 ? 0x70 + n - 1 : -1;
    }
    if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X') && v[2] && !(v[3] && v[4])) {
        int value = 0;
        for (const char* p = v + 2; *p; ++p) {
            const int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 :
                          *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
            if (d < 0) return -1;
            value = value * 16 + d;
        }
        return value >= 1 && value <= 0xFE ? value : -1;
    }
    return -1;
}
// Keys taken by this mod (F6, F8..F11) or re4_tweaks (F1 menu, F2 console, F5 FocusUI, HOME, END,
// INSERT, PAGE UP), mouse buttons and Escape.
inline bool Reserved(int vk) {
    static const int taken[] = {0x01, 0x02, 0x04, 0x05, 0x06, 0x1B, 0x70, 0x71, 0x74, 0x75, 0x77, 0x78, 0x79, 0x7A, 0x24, 0x23, 0x2D, 0x21};
    for (int t : taken) if (t == vk) return true;
    return false;
}
// input.ini physical keyboard token of a virtual key ("" when this table does not know it).
inline const char* InputToken(int vk) {
    static char one[2];
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { one[0] = char(vk); one[1] = 0; return one; }
    struct Named { int vk; const char* token; };
    static const Named names[] = {{0x20, "SPACE"}, {0x09, "TAB"}, {0x0D, "RETURN"}, {0x08, "BACKSPACE"}, {0x2E, "DELETE"},
        {0x22, "PGDN"}, {0xA0, "LSHIFT"}, {0xA1, "RSHIFT"}, {0xA2, "LCONTROL"}, {0xA3, "RCONTROL"}, {0x26, "UP"},
        {0x28, "DOWN"}, {0x25, "LEFT"}, {0x27, "RIGHT"}, {0x72, "F3"}, {0x73, "F4"}, {0x76, "F7"}, {0x7B, "F12"}};
    for (const auto& n : names) if (n.vk == vk) return n.token;
    return "";
}
// Logical names bound to `token` in the GAMEPLAY / KEYBOARD block of input.ini (or a user copy with the
// same layout). Bit 0 = KEY_A, bit 1 = KEY_CK, bit 2 = any other logical key. 0 = unbound.
inline unsigned GameplayKeyboardBinds(const char* ini, const char* token) {
    if (!ini || !token || !*token) return 0;
    unsigned out = 0; bool gameplay = false, keyboard = false;
    char line[160];
    for (const char* p = ini; *p;) {
        size_t n = 0;
        while (p[n] && p[n] != '\n') ++n;
        const size_t len = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
        std::memcpy(line, p, len); line[len] = 0;
        p += n; if (*p) ++p;
        size_t e = std::strlen(line);
        while (e && (line[e - 1] == '\r' || line[e - 1] == ' ' || line[e - 1] == '\t')) line[--e] = 0;
        if (line[0] == '#' && line[1] == '#' && line[2] == '#') {
            const char* tag = line + 3;
            if (EqualNoCase(tag, "GAMEPLAY")) { gameplay = true; keyboard = false; }
            else if (EqualNoCase(tag, "MENUS")) gameplay = keyboard = false;
            else keyboard = gameplay && EqualNoCase(tag, "KEYBOARD");
            continue;
        }
        if (!keyboard) continue;
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        char* name_end = eq; while (name_end > line && name_end[-1] == ' ') --name_end;
        *name_end = 0;
        const char* value = eq + 1; while (*value == ' ') ++value;
        if (!EqualNoCase(value, token)) continue;
        out |= EqualNoCase(line, "KEY_A") ? 1u : EqualNoCase(line, "KEY_CK") ? 2u : 4u;
    }
    return out;
}
// MortalDrawKey + the game's bindings -> mode. A key bound to the native action (E by default) is the
// action button itself (contextual, native glyph). A key bound to any other gameplay action is refused.
inline KeyChoice ResolveKey(const char* setting, const char* input_ini) {
    KeyChoice c;
    const int vk = ParseKey(setting);
    if (vk == 0) return c;
    if (vk < 0) { c.why = KeyWhy::Invalid; return c; }
    if (Reserved(vk)) { c.why = KeyWhy::Reserved; return c; }
    // E is the stock KEY_A/KEY_CK binding: without a readable input.ini it must stay the action button
    // (a dedicated E would fire both the native action and Mortal Draw).
    if (vk == 'E' && (!input_ini || !*input_ini)) return c;
    const char* token = InputToken(vk);
    if (!input_ini || !*token) { c.mode = KeyMode::Physical; c.vk = vk; c.why = KeyWhy::Unverified; return c; }
    const unsigned binds = GameplayKeyboardBinds(input_ini, token);
    if (binds & 4u) { c.why = KeyWhy::Clash; return c; }
    if (binds & 1u) { c.why = KeyWhy::SameAsNative; c.vk = vk; return c; }
    if (binds) { c.why = KeyWhy::Clash; return c; }  // KEY_CK alone: check key without the action
    c.mode = KeyMode::Physical; c.vk = vk; c.why = KeyWhy::Unbound;
    return c;
}
// MortalDrawPadButton (Physical mode only; Action mode already includes the pad's action button).
// XInput masks; 0 = None, -1 invalid.
inline int ParsePadButton(const char* v) {
    if (!v || !*v || EqualNoCase(v, "None")) return 0;
    struct Named { const char* name; int mask; };
    static const Named names[] = {{"Up", 0x1}, {"Down", 0x2}, {"Left", 0x4}, {"Right", 0x8}, {"Start", 0x10},
        {"Back", 0x20}, {"LS", 0x40}, {"RS", 0x80}, {"LB", 0x100}, {"RB", 0x200}, {"A", 0x1000}, {"B", 0x2000},
        {"X", 0x4000}, {"Y", 0x8000}};
    for (const auto& n : names) if (EqualNoCase(v, n.name)) return n.mask;
    return -1;
}
inline OfferArgs OfferFor(KeyMode mode) {
    OfferArgs a;
    a.kind = mode == KeyMode::Action ? layout::kind_action : layout::kind_physical;
    a.btn = mode == KeyMode::Action ? layout::disp_a_normal : layout::disp_off;
    return a;  // slot 0 (lowest), type 0, flags 0: no WEP_SET_IGNORE side effect, no NO_EXEC/NO_DISP
}

// ---------------------------------------------------------------- dedicated (physical) key
// One logic tick per call (arbiter detour, game thread): edges in ticks, no frame-count timing,
// so 60 and 165 FPS behave the same. Background or alt-tab: nothing, and the key must be released
// once in the foreground before it counts again.
class DedicatedKey {
    bool prev_ = false, need_release_ = true;
public:
    struct Edge { bool on = false, trg = false; };
    Edge Step(bool key_down, bool pad_down, bool foreground) {
        Edge e;
        const bool down = key_down || pad_down;
        if (!foreground) { need_release_ = true; prev_ = false; return e; }
        if (need_release_) { if (!down) need_release_ = false; return e; }
        e.on = down; e.trg = down && !prev_; prev_ = down;
        return e;
    }
};
// Physical mode: the Trigger sees the dedicated key in place of KEY_A, so E keeps doing only native things.
inline context::Sample WithDedicatedKey(context::Sample s, DedicatedKey::Edge e) {
    s.on = (s.on & ~context::layout::key_action) | (e.on ? context::layout::key_action : 0);
    s.trg = (s.trg & ~context::layout::key_action) | (e.trg ? context::layout::key_action : 0);
    return s;
}
// Activation frame: which logical TRG bits to clear. Action mode: KEY_A (MaskPress) plus KEY_CK when
// mask_check (E feeds both). Physical mode: none (the game never saw the key as an action).
inline uint64_t MaskBits(KeyMode mode, const context::Decision& d, bool mask_check) {
    if (mode != KeyMode::Action || !d.mask_trg) return 0;
    return context::layout::key_action | (mask_check ? layout::key_check : 0);
}
template <class Memory> bool MaskTrg(Memory& memory, uint32_t image_base, uint64_t bits) {
    if (!bits) return true;
    uint32_t low = 0;
    if (!memory.Read(image_base + context::layout::input_trg_rva, &low, 4)) return false;
    low &= ~uint32_t(bits);
    return memory.Write(image_base + context::layout::input_trg_rva, &low, 4);
}

// ---------------------------------------------------------------- prompt state
struct EntryFacts {
    bool readable = false;
    uint8_t offers = 0, lock = 0;
    bool actions_disabled = false;
    uint8_t max_native_kind = 0;  // highest kind among the queued native works (collision watch)
};
template <class Reader> bool ReadEntry(Reader& reader, uint32_t image_base, EntryFacts& f) {
    f = EntryFacts{};
    context::Sample s;
    if (!context::ReadPreArbiter(reader, image_base, s)) return false;
    f.offers = s.offers; f.lock = s.lock; f.actions_disabled = s.actions_disabled;
    const uint32_t manager = image_base + context::layout::action_manager_rva;
    for (uint32_t i = 0; i < f.offers && i < 8; ++i) {
        uint8_t kind = 0;
        if (!reader.Read(manager + layout::works + i * layout::work_size + layout::w_kind, &kind, 1)) return false;
        if (kind > f.max_native_kind) f.max_native_kind = kind;
    }
    f.readable = true;
    return true;
}
// After the native set(): exactly one work, ours, with no function.
template <class Reader> bool ReadOwnWork(Reader& reader, uint32_t image_base, const OfferArgs& a) {
    const uint32_t manager = image_base + context::layout::action_manager_rva;
    uint8_t count = 0, kind = 0, btn = 0; uint32_t func = 1, flags = 1;
    if (!reader.Read(manager + context::layout::offers, &count, 1) ||
        !reader.Read(manager + layout::works + layout::w_kind, &kind, 1) ||
        !reader.Read(manager + layout::works + layout::w_btn, &btn, 1) ||
        !reader.Read(manager + layout::works + layout::w_func, &func, 4) ||
        !reader.Read(manager + layout::works + layout::w_flags, &flags, 4)) return false;
    return count == 1 && kind == a.kind && btn == a.btn && func == 0 && flags == a.flags;
}

enum class Why { Shown, Off, Unreadable, NativeOffer, NativeLock, ActionsDisabled, NotAvailable };
inline const char* WhyName(Why w) {
    switch (w) {
    case Why::Shown: return "shown"; case Why::Off: return "off"; case Why::Unreadable: return "unreadable";
    case Why::NativeOffer: return "native_offer"; case Why::NativeLock: return "native_lock";
    case Why::ActionsDisabled: return "actions_disabled"; case Why::NotAvailable: return "not_available";
    }
    return "?";
}

// Entry (frame N+1) uses the availability decided at the exit of frame N: target selection runs
// after the arbiter. Native facts are always this frame's. Our own gates (NoTarget, PlayerBusy)
// may keep the prompt for kGraceFrames of game time against selection jitter; native offers, the
// lock, the early-outs, a drawn sword or a raised gun hide it on the same frame.
class Prompt {
    bool want_ = false, injected_ = false;
    double grace_until_ = -1, last_now_ = -1;
public:
    static constexpr double kGraceFrames = 6.0;  // 100 ms at 60 Hz game time
    Why Entry(const EntryFacts& e, bool enabled) {
        injected_ = false;
        if (!enabled) return Why::Off;
        if (!e.readable) return Why::Unreadable;
        if (e.offers) return Why::NativeOffer;
        if (e.lock) return Why::NativeLock;
        if (e.actions_disabled) return Why::ActionsDisabled;
        if (!want_) return Why::NotAvailable;
        injected_ = true;
        return Why::Shown;
    }
    // The caller calls set() only when Entry returned Shown; if set() or ReadOwnWork failed, Cancel().
    void Cancel() { injected_ = false; }
    bool Injected() const { return injected_; }
    // Post-arbiter: our work was walked and passed the native player check (actCheck, not aiming).
    bool Displayed(uint8_t active_after) const { return injected_ && active_after == 1; }
    // Exit of this frame: Trigger decision, after ClaimOwnOffer. `busy`: Mortal Draw running.
    void Exit(const context::Decision& d, bool busy, double now) {
        if (now < last_now_ - 1e-3) grace_until_ = -1;  // clock went back (scene reload)
        last_now_ = now;
        const bool ours = d.block == context::Block::PlayerBusy || d.block == context::Block::NoTarget;
        if (busy) { want_ = false; grace_until_ = -1; }
        else if (d.available) { want_ = true; grace_until_ = now + kGraceFrames; }
        else want_ = ours && now <= grace_until_ + 1e-3;
        if (!want_ && !ours) grace_until_ = -1;
    }
    bool Wanted() const { return want_; }
};
// Our own offer must not read as a native one: with nothing native queued at entry, the only work the
// arbiter could fire (+0x42) is ours.
inline void ClaimOwnOffer(context::Sample& s, bool injected) {
    if (injected && s.offers == 0) s.active = 0;
}
}  // namespace link_runtime::mortal_draw::prompt

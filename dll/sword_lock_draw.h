#pragma once
// Master Sword Blade Lock, milestone H1d: the drawn duel of the lab, keyboard first (docs/idea-ambiciosa-2026-11-03.md,
// "Mecanica del duelo: botones mezclados" and "Camino al 100%"; docs/swordlock-h1d-2026-11-04.md). Lab DLL only:
// link_runtime.cpp includes this header inside #if LINK_SWORD_LOCK_LAB (as sword_lock_icons.h), so the public build never
// sees it and stays byte-identical. Pure: no hooks, no process access; the runtime resolves the faces of the bound keys
// (sword_lock_icons.h Query, the gameplay binding set PadRead builds Key_btn from), reads the frame counter and the owner,
// and calls cActionButton::set with the offers below.
//
// What it decides:
//  * The keyboard bag (KeyboardPool): the physical key of each control is the code the gameplay set binds to its logical
//    bit, as the icon painter reads it (KEY_KAMAE_KNIFE Space, KEY_A E, KEY_RIGHT D, KEY_LEFT A, KEY_KAMAE the right mouse
//    button on Donald's input.ini). A control whose key the painter cannot draw (its face not ok: unbound, a code the atlas
//    has not, a menus frame) has no key in the bag, so every step on it stays out (sd::BreakUsable / MashUsable read
//    key != 0): Guard enters only with the right mouse button drawn. Mashes: Knife, Action, Swing (A/D), Alternate
//    (Space/E). Breaks: Twist (tap E), Slash (tap Space; the core never puts it next to a knife mash), Brace (hold E
//    500 ms), Lateral (A or D held + a Space tap of 250 ms or less; since 4-nov it may sit next to a knife mash, Donald's
//    "mas variedad": Pool::lateral_by_knife), Guard (hold the right button 400 ms). Never Pommel
//    (KEY_FIRE: the ammo risk) nor X (KEY_ASHLEY: Z on the keyboard, design rule). SwordLockPool on top (sd::ApplyUserPool:
//    the player's choice when it keeps 3 physical keys, else the device's bag). Floor: 3 distinct physical keys among the
//    breaks (sd::BreakKeyCount), or the H1c fixed sequence (logged why=keys).
//  * The draw (Drawer): sd::Sorter with the seed of the design, frameCounter GLOBAL+0x530C XOR the owner XOR the duel
//    counter (SwordLockSeed=N: N XOR the counter, for the bots), the history kept for the whole session (the Drawer lives
//    as long as the DLL), so two duels in a row never ask for the same physical signature (Sorter's levels say how much of
//    the rule a pick kept). The real Tuning per difficulty (sd::TuningFor: Easy/Normal 2 breaks, Pro 3; minimum hits 1/2/3;
//    windows 1300/1000/750 x SwordLockWindowScale 1.0..2.0) with SwordLockMashGain (missing 0.15) and SwordLockHold (a held
//    mash key beats at kHoldPps 7, the design's accessibility line; the core's own default stays 8 for its fixture).
//    A pad (the glyphs of another device: sword_lock_icons.h KeyboardGlyphs false) keeps H1c exactly: the fixed sequence,
//    LabSetup with the mash gain, H1c's two controls (why=device). No glyph is Status::Live on the pad, so the pad bag is
//    not widened.
//  * One native offer at a time (OfferFor / Offers): every step shows its real key through the icon painter
//    (PaintCtlWide: the mash key, the next Swing direction, only the next expected Alternate key, the hold's key, the
//    Lateral's direction then Space), with btn 0 under the icon (sword_lock_icons.h NativeOffer). Without the icon the btn
//    is the native glyph confirmed for that key: btn 1 DISP_A_NORMAL for E (Status::Live, "E KICK"), btn 5 DISP_B for the
//    knife (H1c's), btn 0 for the rest (kind only, no glyph). Never btn 0xD, 2 or 4, never kinds 0x44/0x45: mashes are kind
//    0x46, breaks 0x48 (messages 0x5C/0x5E, emptied).
// Tests: tests/native/test_sword_lock_draw.cpp (scripts/test_sword_lock_draw.bat).
#include "sword_lock_icons.h"
#include "sword_lock_build.h"   // LINK_SWORD_LOCK_GRADUATED: the 3.6.0 defaults
#include <cstdint>
#include <cstdio>

namespace link_runtime::sword_lock::draw {
namespace sd = link_runtime::sword_lock::duel;
namespace lab = link_runtime::sword_lock::lab;
namespace icons = link_runtime::sword_lock::icons;
namespace sg = link_runtime::sword_lock::glyphs;

// ------------------------------------------------------------------ physical keys
// The opaque id of a physical code for the duel's Pool (sd::Pool::key, 0 = none): a DirectInput scan code is itself
// (Space 0x39, E 0x12, A 0x1E, D 0x20), the three mouse buttons the tree of 0x1E600 names take 0xF1..0xF3 (no scan code
// reaches 0xF0). Anything else (an axis, a wheel, a high dword) is no key of the bag.
inline constexpr uint8_t kMouseLeft = 0xF1, kMouseRight = 0xF2, kMouseMiddle = 0xF3;
inline uint8_t KeyId(uint32_t lo, uint32_t hi) {
    if (hi) return 0;
    if (lo >= 1 && lo <= 0xEF) return static_cast<uint8_t>(lo);
    return lo == 0x200 ? kMouseLeft : lo == 0x400 ? kMouseRight : lo == 0x800 ? kMouseMiddle : 0;
}
inline uint8_t FaceKey(const icons::Face& f) {return f.ok ? KeyId(f.lo, f.hi) : 0;}

// ------------------------------------------------------------------ the keyboard bag
inline constexpr uint8_t kKeyboardBreaks = static_cast<uint8_t>(sd::BreakBit(sd::Break::Twist) | sd::BreakBit(sd::Break::Slash) |
                                                                sd::BreakBit(sd::Break::Brace) | sd::BreakBit(sd::Break::Lateral) |
                                                                sd::BreakBit(sd::Break::Guard));
inline constexpr uint8_t kKeyboardMashes = sd::kAllMashes;
static_assert(!(kKeyboardBreaks & (sd::BreakBit(sd::Break::Pommel) | sd::BreakBit(sd::Break::X))), "no Pommel, no X on the keyboard");
struct Bag {
    sd::Pool pool;            // what the draw uses (SwordLockPool applied)
    sd::Pool device;          // the device's bag before SwordLockPool
    unsigned keys = 0;        // distinct physical keys of the usable breaks (sd::BreakKeyCount)
    bool ok = false;          // keys >= 3 and the knife mash usable
    bool user_fell_back = false;   // SwordLockPool left fewer than 3 keys: the device's bag
    bool guard = false;       // Guard usable (the right mouse button drawn)
};
// device_off (candidate 4, DeviceOff: SwordLockLateral=0, SwordLockGuard=0) takes those breaks out of the device's bag
// itself, so a SwordLockPool that falls back can never bring them back: a keyboard without the right click (or with
// SwordLockGuard=0) then keeps 2 break keys (E, Space), below the 3 of the gate, and the Drawer plays the fixed duel
// (Why::Keys) instead of drawing a Lateral or a Guard.
inline Bag KeyboardPool(const icons::Faces& f, uint8_t user_breaks = sd::kAllBreaks, uint8_t device_off = 0, bool swing_ends = false) {
    Bag b;
    sd::Pool& p = b.device;
    p.key[sd::kKnife] = FaceKey(f.knife); p.key[sd::kAction] = FaceKey(f.action);
    p.key[sd::kRight] = FaceKey(f.right); p.key[sd::kLeft] = FaceKey(f.left); p.key[sd::kAim] = FaceKey(f.aim);
    p.breaks = static_cast<uint8_t>(kKeyboardBreaks & ~device_off);
    p.mashes = kKeyboardMashes;
    p.lateral_by_knife = true;   // Donald 4-nov: more variety (sword_lock_duel.h Pool::lateral_by_knife)
    p.swing_ends = swing_ends;   // candidate 5: the A/D mash as Push 1 and the Finish too (sword_lock_duel.h Pool::swing_ends)
    b.pool = sd::ApplyUserPool(p, user_breaks, &b.user_fell_back);
    b.keys = sd::BreakKeyCount(b.pool);
    b.ok = b.keys >= sd::kMinPhysicalKeys && sd::MashUsable(b.pool, sd::Mash::Knife);
    b.guard = sd::BreakUsable(b.pool, sd::Break::Guard);
    return b;
}

// ------------------------------------------------------------------ [Combat] keys of the drawn duel (lab DLL only)
// SwordLockPool, SwordLockWindowScale (1.0..2.0), SwordLockHold (0/1), SwordLockSeed (N, bots) and SwordLockCapMs with
// the core's own parser (sd::ReadConfig), and SwordLockMashGain with the lab's (lab::ReadMashGain, missing 0.15).
inline constexpr double kHoldPps = LINK_SWORD_LOCK_GRADUATED ? 8 : 7;   // the design's 7 pps; 3.6.0: Donald's decision 5 (8)
struct Config {
    sd::Config duel;
    double mash_gain = lab::kDefaultMashGain;
    double hold_pps = kHoldPps;
    // Candidate 2 (docs/swordlock-cand2-2026-11-04.md): a struct left at its defaults is H1d's duel exactly (the older tests
    // build it so); ReadConfig, the runtime's, turns the rhythm on and reads SwordLockTiming.
    bool rhythm = false;      // the slower, readable rhythm (Cand2Tuning): warning 500 ms, longer pushes, leftovers never wrong
    bool timing = false;      // [Combat] SwordLockTiming (missing = 1 in the lab): tap breaks are a timing check
    // Candidate 3 (docs/swordlock-cand3-2026-11-04.md):
    bool guard = true;        // [Combat] SwordLockGuard (missing = 1): 0 takes Guard (hold the right click) out of the keyboard bag
    bool draw_stun = false;   // [Combat] SwordLockDrawStun (missing = 1 in the lab): a draw knocks the owner down (the kick record)
    // Candidate 4 (Donald, 4-nov: "quizas quitar el corte lateral"; his Laterals failed 7 of 7 on candidate 3):
    bool lateral = true;      // [Combat] SwordLockLateral: missing = 0 in the lab (removed from the keyboard bag); 1 = cand3's chord
    // Candidate 5 (Donald's 1920x1080 recording: the prompt tiny and faint): the whole prompt scaled (icons::Icon::SetScale)
    float prompt_scale = 1.f; // [Combat] SwordLockPromptScale: missing = icons::kPromptScale (1.8) in the lab, 1.0..2.2
    // Candidate 5 (Donald, 4-nov: "un alternato mashing tipo spamear a y d constantemente, y no solamente 1 tecla"):
    bool swing_ends = false;  // [Combat] SwordLockSwingEnds: missing = 1 in the lab: the A/D mash also as Push 1 and the Finish
    double swing_gain = 0;    // [Combat] SwordLockSwingGain: missing = kSwingGain in the lab; 0 = mash_gain (the older tests)
    // Candidate 10 (Donald, 4-nov: "no debería poderse empatar"): overtime rounds until someone wins (sd::Tuning::overtime)
    bool overtime = false;    // [Combat] SwordLockOvertime: missing = 1 in the lab; 0 = cand2's 7.5 s cap and its draw
};
// SwordLockLateral: missing or "0" = the Lateral out of the bag, "1" = in; anything else counts a problem and stays out.
// SwordLockPromptScale: missing = icons::kPromptScale; a plain decimal in 1.0..2.2 ("1.5", "2"); anything else counts a
// problem and keeps the default.
template <class Get> double ReadDecimal(Get&& get, const wchar_t* key, double def, double lo, double hi, unsigned& problems) {
    wchar_t v[16]{}; get(key, v, 16);
    if (!v[0]) return def;
    double x = 0, frac = 0.1; bool dot = false, digit = false;
    for (const wchar_t* c = v; *c; ++c) {
        if (*c == L'.' && !dot) {dot = true; continue;}
        if (*c < L'0' || *c > L'9') {++problems; return def;}
        digit = true;
        if (!dot) x = x * 10 + (*c - L'0'); else {x += (*c - L'0') * frac; frac *= 0.1;}
    }
    if (!digit || x < lo - 1e-9 || x > hi + 1e-9) {++problems; return def;}
    return x;
}
template <class Get> float ReadPromptScale(Get&& get, unsigned& problems) {
    return static_cast<float>(ReadDecimal(get, L"SwordLockPromptScale", icons::kPromptScale, icons::kPromptScaleMin, icons::kPromptScaleMax, problems));
}
// Candidate 5: SwordLockSwingGain, PV per counted A/D flank (missing = kSwingGain; 0.05..0.30, as SwordLockMashGain).
inline constexpr double kSwingGain = 0.17;
template <class Get> double ReadSwingGain(Get&& get, unsigned& problems) {
    return ReadDecimal(get, L"SwordLockSwingGain", kSwingGain, lab::kMinMashGain, lab::kMaxMashGain, problems);
}
template <class Get> bool ReadLateral(Get&& get, unsigned& problems) {
    wchar_t v[16]{}; get(L"SwordLockLateral", v, 16);
    if (!v[0] || (v[0] == L'0' && !v[1])) return false;
    if (v[0] == L'1' && !v[1]) return true;
    ++problems; return false;
}
// The breaks the keyboard bag draws from: SwordLockPool, minus Guard (SwordLockGuard=0) and the Lateral (SwordLockLateral=0).
inline uint8_t DeviceOff(const Config& c) {
    return static_cast<uint8_t>((c.guard ? 0 : sd::BreakBit(sd::Break::Guard)) | (c.lateral ? 0 : sd::BreakBit(sd::Break::Lateral)));
}
inline uint8_t UserBreaks(const Config& c) {return static_cast<uint8_t>(c.duel.pool & ~DeviceOff(c));}
// [Combat] SwordLockGuard / SwordLockDrawStun: missing = 1, "0" = off, anything else counts a problem and stays on.
template <class Get> bool ReadSwitch(Get&& get, const wchar_t* key, unsigned& problems) {
    wchar_t v[16]{}; get(key, v, 16);
    if (!v[0] || (v[0] == L'1' && !v[1])) return true;
    if (v[0] == L'0' && !v[1]) return false;
    ++problems; return true;
}
// [Combat] SwordLockTiming: missing = 1, "0" = the old any-time window, anything else counts a problem and stays on.
template <class Get> bool ReadTiming(Get&& get, unsigned& problems) {
    wchar_t v[16]{}; get(L"SwordLockTiming", v, 16);
    if (!v[0] || (v[0] == L'1' && !v[1])) return true;
    if (v[0] == L'0' && !v[1]) return false;
    ++problems; return true;
}
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c;
    c.duel = sd::ReadConfig(get, problems);
    c.mash_gain = lab::ReadMashGain(get, problems);
    c.rhythm = true;
    c.timing = ReadTiming(get, problems);
    c.guard = ReadSwitch(get, L"SwordLockGuard", problems);
    c.draw_stun = ReadSwitch(get, L"SwordLockDrawStun", problems);
    c.lateral = ReadLateral(get, problems);
    c.prompt_scale = ReadPromptScale(get, problems);
    c.swing_ends = ReadSwitch(get, L"SwordLockSwingEnds", problems);
    c.swing_gain = ReadSwingGain(get, problems);
    c.overtime = ReadSwitch(get, L"SwordLockOvertime", problems);
    return c;
}

// ------------------------------------------------------------------ candidate 2: the readable rhythm and the timing check
// Donald, 4-nov (14 draws at the 5.5 s cap): "pasan muy rapido las teclas", "no se entienden ... ni idea si tengo que hold
// una tecla, ni idea si tengo que machacar", and "cuando hay que apretar con precision, tipo Dead by Daylight". So:
//  * the break's warning 250 -> 500 ms, the new prompt (text and icon) shown through it;
//  * Push 1 / Push 2 1000 / 800 -> 1400 / 1200 ms (Pro's Push 2 600 -> 1000);
//  * the previous step's keys never count as wrong in the next break (two of his misses were his A/D swing presses 204
//    and 283 ms into the next break, past the 200 ms grace);
//  * a tap break (Twist, Slash, the Lateral's knife) is a timing check: a needle sweeps the window at constant game speed
//    and a zone shows where to press; inside = hit, its first part (great) a little more; before it or letting the
//    needle leave it = a miss (early / late). Zones per difficulty, in fractions of the window (SwordLockWindowScale
//    stretches the window and so the zone's ms);
//  * the decay of Easy / Normal lowered (kCand2EasyDecay / kCand2NormalDecay) and the cap 5500 -> kCand2CapMs, so a clean
//    Normal duel at his measured pace wins with margin (the table of
//    docs/swordlock-cand2-2026-11-04.md, tests/native/test_sword_lock_cand2.cpp).
inline constexpr double kCand2WarnMs = 500;
inline constexpr double kCand2Push1Ms = 1400, kCand2Push2Ms = 1200, kCand2ProPush2Ms = 1000;
inline constexpr double kCand2CapMs = 7500;   // his clean Normal duel wins 6.6-6.7 s in (0.77 s of margin)
inline constexpr double kGreatBonus = 0.05;
// The bar's decay (PV/s) where Donald's pace (4.5 counted flanks/s at gain 0.15 = 0.675 PV/s, his first flank 600 ms
// into a push) could never outrun H1d's 0.48 / 0.60: Easy 0.20, Normal 0.25; Professional keeps 0.75 (5 pps must not win).
// Mashing alone still never wins: the Finish's ceiling without the breaks (0.70 / 0.40) is what stops it, not the decay.
inline constexpr double kCand2EasyDecay = 0.20, kCand2NormalDecay = 0.25;
struct Zone {double z0, z1, great;};
inline Zone ZoneFor(sd::Difficulty d) {
    switch (d) {
    case sd::Difficulty::Easy: return Zone{0.25, 0.85, 0.12};   // 1300 ms window: 325..1105 ms, great 325..481
    case sd::Difficulty::Pro: return Zone{0.40, 0.68, 0.08};    // 750 ms: 300..510 ms, great 300..360
    default: return Zone{0.35, 0.75, 0.10};                     // 1000 ms: 350..750 ms, great 350..450
    }
}
inline sd::Tuning Cand2Tuning(sd::Difficulty d, bool timing) {
    sd::Tuning t = sd::TuningFor(d);
    t.warn_ms = kCand2WarnMs; t.leftovers_free = true;
    t.push1_ms = kCand2Push1Ms; t.push2_ms = d == sd::Difficulty::Pro ? kCand2ProPush2Ms : kCand2Push2Ms;
    t.cap_ms = kCand2CapMs;
    if (d != sd::Difficulty::Pro) t.decay_per_s = d == sd::Difficulty::Easy ? kCand2EasyDecay : kCand2NormalDecay;
    t.timing = timing;
    const Zone z = ZoneFor(d);
    t.zone0 = z.z0; t.zone1 = z.z1; t.great = z.great; t.great_bonus = kGreatBonus;
    // candidate 3: a held-over key of this break waits for its release; the Lateral is a chord in either order; Guard's
    // directions before the right click never count (his 5 Guards: `wrong got=0x4`, D read off the mouse glyph)
    t.held_over = true; t.lateral_chord = true; t.guard_dirs_free = true;
    // candidate 6: a hold (Guard's right click, Brace's E) pressed in the warning and kept down counts from the opening
    t.hold_from_warning = true;
    return t;
}

// ------------------------------------------------------------------ the draw
enum class Why : uint8_t {Drawn, Device, Keys, NoSequence};
inline const char* WhyName(Why w) {
    switch (w) {
    case Why::Drawn: return "drawn"; case Why::Device: return "device"; case Why::Keys: return "keys"; default: return "no_sequence";
    }
}
struct Plan {
    bool drawn = false;       // the Sorter's sequence (else H1c's fixed one)
    Why why = Why::Drawn;
    sd::Pick pick;            // valid when drawn
    Bag bag;                  // the keyboard bag (empty on a pad)
    sd::Duel::Setup setup;    // what Lab::Start runs
    bool all = false;         // the pad words feed the whole bag (lab::ControlsAll)
    sd::Signature sig;        // the physical signature of what runs (H1c's for a fallback)
    char seq[64] = "";        // FormatSignature(sig)
};
// The H1c duel exactly (fixed sequence, LabSetup with the mash gain, H1c's two controls).
inline sd::Duel::Setup FallbackSetup(sd::Difficulty d, const Config& c) {return lab::LabSetup(d, c.duel, c.mash_gain);}
inline sd::Duel::Setup DrawnSetup(sd::Difficulty d, const Config& c, const sd::Sequence& s, const sd::Pool& p) {
    sd::Duel::Setup u = sd::Duel::For(d, c.duel, s, p);
    u.mash_gain = c.mash_gain;
    u.hold_pps = c.hold_pps;
    u.swing_gain = c.swing_gain;
    if (c.rhythm) {u.tuning = Cand2Tuning(d, c.timing); u.tuning.overtime = c.overtime;}
    return u;
}
class Drawer {
public:
    // keyboard: the active device draws the PC atlas (icons::KeyboardGlyphs); faces: the bound keys as the painter resolved
    // them on the Begin frame; frame: GLOBAL+0x530C; owner: the duel's owner pointer.
    Plan Next(const Config& c, sd::Difficulty d, bool keyboard, const icons::Faces& faces, uint32_t frame, uint32_t owner) {
        Plan p;
        if (!keyboard) return Fallback(p, Why::Device, d, c);
        p.bag = KeyboardPool(faces, UserBreaks(c), DeviceOff(c), c.swing_ends);
        if (!p.bag.ok) return Fallback(p, Why::Keys, d, c);
        p.pick = sorter_.Next(p.bag.pool, d, frame, owner, c.duel.seed);
        if (!p.pick.ok) return Fallback(p, Why::NoSequence, d, c);
        p.drawn = true; p.why = Why::Drawn; p.all = true;
        p.setup = DrawnSetup(d, c, p.pick.seq, p.bag.pool);
        p.sig = p.pick.sig;
        sd::FormatSignature(p.sig, p.seq, sizeof(p.seq));
        ++drawn_;
        return p;
    }
    const sd::Sorter& sorter() const {return sorter_;}
    unsigned drawn() const {return drawn_;}
    unsigned fallbacks() const {return fallbacks_;}
private:
    sd::Sorter sorter_;
    unsigned drawn_ = 0, fallbacks_ = 0;
    Plan& Fallback(Plan& p, Why why, sd::Difficulty d, const Config& c) {
        p.drawn = false; p.why = why; p.all = false;
        p.setup = FallbackSetup(d, c);
        p.sig = sd::Sign(lab::LabSequence(), lab::LabPool());
        sd::FormatSignature(p.sig, p.seq, sizeof(p.seq));
        ++fallbacks_;
        return p;
    }
};
// "q=twist,lateral,-" and the like, for the seq line.
inline const char* BreakName(const sd::Sequence& s, unsigned i) {return i < s.breaks ? sd::Info(s.q[i]).name : "-";}
inline const char* SideName(uint8_t side) {return side == sd::kSideLeft ? "left" : side == sd::kSideRight ? "right" : "-";}

// ------------------------------------------------------------------ offers
// The native btn that draws this control's key where no icon of ours does: E's DISP_A_NORMAL (Status::Live on the
// keyboard), the knife's DISP_B (H1c's mash offer), and nothing (btn 0, kind only) for the rest.
inline uint8_t FallbackBtn(uint8_t ctl) {
    return ctl == sd::kAction ? lab::kBtnBreak : ctl == sd::kKnife ? lab::kBtnPush : sg::btn::kOff;
}
inline bool AllowedBtn(uint8_t b) {return b != sg::btn::kBRapid && b != sg::btn::kARapid && b != sg::btn::kAB;}
static_assert(lab::kKindPush != 0x44 && lab::kKindPush != 0x45 && lab::kKindBreak != 0x44 && lab::kKindBreak != 0x45,
              "never the Mortal Draw prompt's kinds");
// Candidate 2 texts (sword-lock-texts-02: core.udas MDT #6 message 0x16 + kind, English block): one kind per GESTURE, the
// prompt says the action, not a TP move name. Mash 0x46 "MASH!", Tap 0x47 "TAP", Hold 0x48 "HOLD", Alternate 0x49
// "ALTERNATE", Swing 0x4A "LEFT / RIGHT", Lateral 0x4B "SIDE + TAP" (messages 0x5C..0x61, all inside the padding
// text_rename.py empties, 0x5C..0x66, offered by no native caller). Without the layer those messages are empty (icon only);
// with sword-lock-texts-01 still installed 0x5C..0x5F read PUSH/STRIKE/TWIST/BREAK: install -02 with this DLL.
inline constexpr uint8_t kKindMash = 0x46, kKindTap = 0x47, kKindHold = 0x48, kKindAlternate = 0x49, kKindSwing = 0x4A,
                         kKindLateral = 0x4B;
// Candidate 3 (sword-lock-texts-03): Guard's own "HOLD AIM" 0x4C (message 0x62) and "WAIT" 0x4D (message 0x63) while a
// timing check's needle has not reached its zone (the Lateral: once its direction is held).
inline constexpr uint8_t kKindHoldAim = 0x4C, kKindWait = 0x4D;
static_assert(lab::kKindPush == kKindMash, "the mash keeps 0x46 (message 0x5C)");
inline uint8_t GestureKind(sd::Gesture g) {
    switch (g) {
    case sd::Gesture::Tap: return kKindTap; case sd::Gesture::Hold: return kKindHold; case sd::Gesture::Alternate: return kKindAlternate;
    case sd::Gesture::Swing: return kKindSwing; case sd::Gesture::Lateral: return kKindLateral; default: return kKindMash;
    }
}
inline uint8_t BreakKind(sd::Break b) {return b == sd::Break::Guard ? kKindHoldAim : GestureKind(sd::Info(b).gesture);}
inline uint8_t PromptKind(const sd::Prompt& p, uint16_t held = 0) {
    if (p.phase == sd::Phase::Break && p.gesture == sd::Gesture::Hold && p.ctl == sd::kAim) return kKindHoldAim;
    if (p.phase == sd::Phase::Break && p.timing && p.needle < p.zone0 &&
        (p.gesture == sd::Gesture::Tap || (p.gesture == sd::Gesture::Lateral && p.ctl != sd::kNoCtl && (held & sd::CtlBit(p.ctl)))))
        return kKindWait;
    return GestureKind(p.gesture);
}
// The control a prompt shows now (held: the duel's last held controls, for the Lateral) and its offer.
inline uint8_t ShownCtl(const sd::Prompt& p, uint16_t held) {return icons::PaintCtlWide(p, held);}
inline const char* Want(const sd::Prompt& p, uint16_t held) {
    const uint8_t c = ShownCtl(p, held);
    return !p.show ? "none" : c == sd::kNoCtl ? "other" : sd::CtlName(c);
}
inline lab::Offer OfferFor(const sd::Prompt& p, uint16_t held) {
    lab::Offer o;
    if (!p.show) return o;
    const uint8_t ctl = ShownCtl(p, held);
    if (ctl == sd::kNoCtl) return o;
    o.show = true;
    o.kind = PromptKind(p, held);
    o.btn = FallbackBtn(ctl);
    o.flags = lab::kOfferFlags;
    o.glyph = p.glyph;
    return o;
}
// The control a resolved break showed (its feedback keeps that key 250 ms): the Lateral's knife tap, else the break's.
inline uint8_t BreakCtl(sd::Break b) {return b == sd::Break::Lateral ? static_cast<uint8_t>(sd::kKnife) : sd::Info(b).ctl;}
// lab::Offers with H1d's offer per step: the per-press gap of a knife mash under btn 5 (the native icon rebuilds), the
// resolved break's feedback on its own key with the colour flag (0x100 hit, 0x80 miss).
class Offers {
public:
    void Reset() {pulse_ = false; until_ = -1; ok_ = false; pulses_ = gaps_ = 0; fb_ctl_ = sd::kNoCtl; fb_glyph_ = sd::Glyph::None; fb_kind_ = kKindTap;}
    void Events(uint32_t ev, const sd::BreakRecord& rec, double game_ms) {
        if (ev & (sd::kEvFlank | sd::kEvBeat)) {pulse_ = true; ++pulses_;}
        if (ev & (sd::kEvHit | sd::kEvMiss)) {
            until_ = game_ms + lab::kFeedbackMs; ok_ = rec.ok;
            fb_ctl_ = BreakCtl(rec.id); fb_glyph_ = sd::Info(rec.id).glyph; fb_kind_ = BreakKind(rec.id);
        }
    }
    lab::Offer Next(const sd::Prompt& p, uint16_t held, bool running, double game_ms) {
        lab::Offer o;
        if (!running) {pulse_ = false; until_ = -1; return o;}
        if (until_ >= 0 && game_ms < until_) {
            o.show = true; o.kind = fb_kind_; o.btn = FallbackBtn(fb_ctl_); o.glyph = fb_glyph_; o.feedback = true;
            o.flags = lab::kOfferFlags | (ok_ ? lab::kColorHit : lab::kColorMiss);
            pulse_ = false;
            return o;
        }
        o = OfferFor(p, held);
        if (o.show && o.kind == lab::kKindPush && o.btn == lab::kBtnPush && pulse_) {pulse_ = false; o.show = false; o.gap = true; ++gaps_;}
        else if (o.show) pulse_ = false;
        return o;
    }
    uint8_t feedback_ctl() const {return fb_ctl_;}
    unsigned pulses() const {return pulses_;}
    unsigned gaps() const {return gaps_;}
private:
    bool pulse_ = false, ok_ = false; double until_ = -1; unsigned pulses_ = 0, gaps_ = 0;
    uint8_t fb_ctl_ = sd::kNoCtl, fb_kind_ = kKindTap; sd::Glyph fb_glyph_ = sd::Glyph::None;
};
}  // namespace link_runtime::sword_lock::draw

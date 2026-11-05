#pragma once
#include "sword_lock_build.h"   // LINK_SWORD_LOCK (the feature) / LINK_SWORD_LOCK_LAB (the lab tools) / _GRADUATED
// Master Sword Blade Lock, milestone H1b: the pure mixed duel (docs/idea-ambiciosa-2026-11-03.md, "Mecanica del duelo:
// botones mezclados (rondas 3 y 4)"). No hooks, no process reads, no native calls: the wiring (H1c, link_runtime.cpp)
// maps the native pad words to the logical controls below, passes game_clock's dt in ms on every tick, draws the prompt
// this class exposes and applies the result. Never the vanilla QTE: every duel mixes a mash with drawn breaks of other
// physical keys, and two duels in a row never ask for the same physical sequence.
//
// The bar (TP d_a_b_gnd.cpp:3374-3427, round 4 numbers, scripts/sword_lock_balance_sim.py):
//  - PushValue (PV) in [-1, 1]; PushAmount (PA) chases PV*50 with addCalc2(0.1, 2.0 per 30 Hz frame) taken to dt:
//    alpha = 1 - 0.9^(dt*30), largest step 2*dt*30 (dt in seconds).
//  - Mash: +0.1 PV per counted flank up to the stretch's ceiling; continuous decay 0.6 PV/s x the difficulty scale,
//    suspended 33.3 ms (one 30 Hz frame) after each counted flank. No decay in the Lock nor in breaks. Only the lab DLL
//    (LINK_SWORD_LOCK_LAB=1) carries Setup::mash_gain, the H1c lab's [Combat] SwordLockMashGain (sword_lock_lab.h);
//    everywhere else, and with the field at its default, the flank is TP's 0.1 (kStep).
//  - Win: PA >= 48 AND the minimum breaks hit; lose: PA <= -49.9; draw at the cap (5500 ms in every difficulty).
// Stretches: Lock 350 ms (no input) -> Push 1 (knife mash, 1000 ms, ceiling PV0 + 0.30) -> Break 1 -> Push 2 (another
// mash mode, 800 ms / Pro 600 ms, ceiling PV0 + 0.30) -> Break 2 [-> Break 3 in Pro] -> Finish (knife mash; absolute
// ceiling ceiling0 + 0.30 per hit: without the minimum hits PA cannot reach 48 by arithmetic, the gate is a second lock).
// Each break: the owner's thrust takes 0.30 PV when the 250 ms warning opens; the window then runs 1300/1000/750 ms
// (never under 600, x SwordLockWindowScale 1..2) with a 200 ms grace for flanks left over from the mash. Hit: the 0.30
// comes back plus the bonus; miss (a wrong bag key after the grace, two physical keys in one tick, or the window gone):
// the thrust stays, the penalty is taken and the mash is dead for 0/300/300 ms.
//
// Time. Every stretch, window and timer is in ms of game time (game_clock: Step60 frames / 0.06), the same at 60 and
// 165 FPS. Two clocks: game time runs always (warning, window, grace, tap and hold lengths, the stall guard); duel time
// stops while a correct gesture that started inside its window is being completed (tap release, the 400/500 ms holds)
// and the bar waits with it. The simulator answers every break at the reaction instant, so with this rule its table
// holds for every gesture (a tap or a hold); without it each hold's 400-500 ms would come out of the cap (Pro perfect
// wins at 4.87 s of 5.5, so two holds would not fit). The window bounds the gesture's START (a 500 ms Brace after a
// 350 ms reaction does not fit Pro's 750 ms window otherwise). The cap counts duel time; a stall guard ends in a draw at
// cap + 1500 ms of game time. Pending the team's vote: the docs ("Tiempos") measure the 5.5 s cap in game time, and
// their table never modelled the length of the holds; this rule is what keeps that table valid with holds.
//
// Variety (Donald, with Fan RE4's condition): Sorter draws the sequence from the device's bag (a parameter: the census
// and the per-device bag are H1a's), signs it by PHYSICAL key + gesture + side, and never returns a signature equal to
// either of the two previous ones, nor a first break with the previous first break's key + gesture. A bag too small for
// that relaxes first the first-break rule, then the two-back rule, and only last "not the previous duel's sequence"
// (Pick::level, logged; a full keyboard or pad bag always keeps the full rule). Inside a duel no break repeats (by kind
// or by physical key + gesture), Break 2 is of another family (tap / hold / lateral) and another physical key than
// Break 1, and two neighbouring steps never share a physical key (docs: "dos pasos vecinos nunca usan la misma tecla
// fisica"): Push 1 | Break 1 | Push 2 | Break 2 [| Break 3] | Finish, both ways, the Finish included. The one exception
// is the docs' own: Push 2 Alternate may share a key with Break 1, and then starts on its other key. So a player who
// only mashes the key of a neighbouring mash never answers a break (in Pro, Break 2 may sit on the Finish's knife and
// Break 3 on Push 2's key: at most one hit of the three needed). Push 1 and the Finish are the knife mash; Push 2 is
// drawn among the other modes. The Alternate mode shows only the next expected key; its first key is the one Break 1
// does not use (the knife when Break 1 uses neither). The draw is uniform over the valid sequences (enumerated, so a
// re-draw always terminates).
//
// Tests: tests/native/test_sword_lock_duel.cpp (scripts/test_sword_lock_duel.bat), with the balance table generated
// by scripts/sword_lock_balance_fixture.py into tests/native/sword_lock_balance_fixture.h.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cwctype>

namespace link_runtime::sword_lock::duel {

// ------------------------------------------------------------------ logical controls
// The wiring maps the native bits of each control per device with H1a's table (dll/sword_lock_glyphs.h): this header
// pins no native bit (H1a found that DISP_X tests KEY_ASHLEY 0x200, not the KEY_X 0x20000 the docs list).
enum Ctl : uint8_t {kKnife = 0, kAction, kRight, kLeft, kAim, kFire, kX, kCtlCount};
constexpr uint16_t CtlBit(unsigned c) {return static_cast<uint16_t>(1u << c);}
constexpr uint16_t kAllCtls = static_cast<uint16_t>((1u << kCtlCount) - 1);
constexpr uint8_t kNoCtl = 0xFF;
inline const char* CtlName(unsigned c) {
    static const char* const n[kCtlCount] = {"knife", "action", "right", "left", "aim", "fire", "x"};
    return c < kCtlCount ? n[c] : "none";
}
// ------------------------------------------------------------------ steps
enum class Gesture : uint8_t {None, Mash, Swing, Alternate, Tap, Hold, Lateral};
inline char GestureLetter(Gesture g) {
    switch (g) {
    case Gesture::Mash: return 'm'; case Gesture::Swing: return 'w'; case Gesture::Alternate: return 'a';
    case Gesture::Tap: return 't'; case Gesture::Hold: return 'h'; case Gesture::Lateral: return 'l'; default: return '-';
    }
}
enum class Family : uint8_t {None, Tap, Hold, Lateral};   // break families: Break 2 is never of Break 1's family
// Mash modes. Push 1 and the Finish are Knife; Push 2 is one of the other three.
enum class Mash : uint8_t {Knife, Action, Swing, Alternate};
constexpr unsigned kMashCount = 4;
inline const char* MashName(Mash m) {
    static const char* const n[kMashCount] = {"knife", "action", "swing", "alternate"};
    return static_cast<unsigned>(m) < kMashCount ? n[static_cast<unsigned>(m)] : "none";
}
// Breaks (SwordLockPool names in key): Twist = tap action, Slash = tap knife, Brace = hold action 500 ms, Lateral =
// drawn direction held + knife tap, Guard = hold aim 400 ms, X = tap X (pad only), Pommel = tap fire.
enum class Break : uint8_t {Twist, Slash, Brace, Lateral, Guard, X, Pommel};
constexpr unsigned kBreakCount = 7;
// Logical glyphs: H1a maps each to the cActionButton btn of the active device after set()'s remap (table 0x7FBFF0).
// ActionNormal DISP_A_NORMAL 00/20, Knife DISP_B 07/07, ActionAccent DISP_A_ACCENT 08/22, Gacha DISP_GACHA 06, X DISP_X
// 09/11, ActionRapid DISP_A_RAPID 17/21, KnifeRapid B_RAPID (DISP_B per press where set() collapses it, Modder's
// condition); Aim, Fire and the lateral pair only with a glyph the census confirms. The Alternate mode shows Knife or
// ActionNormal for the next expected key, one offer at a time.
enum class Glyph : uint8_t {None, KnifeRapid, ActionRapid, Gacha, ActionNormal, Knife, ActionAccent, Aim, X, Fire, LateralLeft, LateralRight};
inline const char* GlyphName(Glyph g) {
    static const char* const n[] = {"none", "knife_rapid", "action_rapid", "gacha", "action", "knife", "action_accent", "aim",
                                    "x", "fire", "lateral_left", "lateral_right"};
    return static_cast<unsigned>(g) < sizeof(n) / sizeof(n[0]) ? n[static_cast<unsigned>(g)] : "none";
}
struct BreakInfo {Gesture gesture; Family family; uint8_t ctl; double hold_ms; Glyph glyph; const char* name; const wchar_t* key;};
inline const BreakInfo& Info(Break b) {
    static const BreakInfo t[kBreakCount] = {
        {Gesture::Tap, Family::Tap, kAction, 0, Glyph::ActionNormal, "twist", L"Accion"},
        {Gesture::Tap, Family::Tap, kKnife, 0, Glyph::Knife, "slash", L"Cuchillo"},
        {Gesture::Hold, Family::Hold, kAction, 500, Glyph::ActionAccent, "brace", L"Aguante"},
        {Gesture::Lateral, Family::Lateral, kKnife, 0, Glyph::LateralLeft, "lateral", L"Lateral"},
        {Gesture::Hold, Family::Hold, kAim, 400, Glyph::Aim, "guard", L"Guardia"},
        {Gesture::Tap, Family::Tap, kX, 0, Glyph::X, "x", L"X"},
        {Gesture::Tap, Family::Tap, kFire, 0, Glyph::Fire, "pommel", L"Pomo"},
    };
    return t[static_cast<unsigned>(b) < kBreakCount ? static_cast<unsigned>(b) : 0];
}
constexpr uint8_t BreakBit(Break b) {return static_cast<uint8_t>(1u << static_cast<unsigned>(b));}
constexpr uint8_t MashBit(Mash m) {return static_cast<uint8_t>(1u << static_cast<unsigned>(m));}
constexpr uint8_t kAllBreaks = 0x7F, kAllMashes = 0x0F;
// Easy: a bag of one-control gestures (Accion, Cuchillo, Aguante, Guardia).
constexpr uint8_t kEasyBreaks = static_cast<uint8_t>(BreakBit(Break::Twist) | BreakBit(Break::Slash) | BreakBit(Break::Brace) |
                                                     BreakBit(Break::Guard));
constexpr uint8_t kSideNone = 0, kSideLeft = 1, kSideRight = 2;   // Lateral: LEFT bit 8 / RIGHT bit 4

// ------------------------------------------------------------------ the device's bag (H1a delivers it)
// key[c]: an opaque nonzero id of the PHYSICAL key or button behind control c on the active device and profile
// (GameplayKeyboardBinds / ProfileButtons, recomputed on a profile load); 0 = not available. Controls that share a
// physical key carry the same id: they are one key in the signature and pressing it never counts as a miss.
// breaks / mashes: the steps whose glyph the census confirmed on this device (and SwordLockPool allows).
struct Pool {
    uint8_t key[kCtlCount]{};
    uint8_t breaks = 0;
    uint8_t mashes = 0;
#if LINK_SWORD_LOCK
    // H1d (Donald, 4-nov: "Si, mas variedad"; lab DLL only): the Lateral may sit next to a knife mash (Push 1, the
    // Finish). Its Space is the knife's own key (one physical key: never a wrong key) and Space without the direction
    // never counts (kEvNoCount), so mashing the neighbour's key still never answers it. Slash keeps its rule.
    bool lateral_by_knife = false;
    // Candidate 5 (Donald, 4-nov: "un alternato mashing tipo spamear a y d constantemente"): the Swing mash (A/D
    // alternating) may also be Push 1 and the Finish, not only Push 2 (the neighbour rule and the history rule unchanged).
    bool swing_ends = false;
#endif
};
struct Keys {   // the physical keys of one step (at most two)
    uint8_t a = 0, b = 0;
    bool Has(uint8_t k) const {return k && (k == a || k == b);}
    bool Meets(const Keys& o) const {return Has(o.a) || Has(o.b);}
};
inline uint8_t DirCtl(uint8_t side) {return side == kSideLeft ? kLeft : kRight;}
inline Keys BreakKeys(const Pool& p, Break b, uint8_t side) {
    if (b == Break::Lateral) return Keys{p.key[DirCtl(side)], p.key[kKnife]};
    return Keys{p.key[Info(b).ctl], 0};
}
inline bool BreakUsable(const Pool& p, Break b, uint8_t side = kSideNone) {
    if (!(p.breaks & BreakBit(b))) return false;
    if (b == Break::Lateral) {
        if (side != kSideLeft && side != kSideRight) return false;
        const uint8_t d = p.key[DirCtl(side)], k = p.key[kKnife];
        return d && k && d != k;
    }
    return p.key[Info(b).ctl] != 0;
}
inline bool MashUsable(const Pool& p, Mash m) {
    if (!(p.mashes & MashBit(m))) return false;
    const uint8_t knife = p.key[kKnife], action = p.key[kAction], r = p.key[kRight], l = p.key[kLeft];
    if (!knife) return false;
    switch (m) {
    case Mash::Knife: return true;
    case Mash::Action: return action && action != knife;
    case Mash::Swing: return r && l && r != l && r != knife && l != knife;
    case Mash::Alternate: return action && action != knife;
    }
    return false;
}
inline uint16_t MashCtls(Mash m) {
    switch (m) {
    case Mash::Knife: return CtlBit(kKnife);
    case Mash::Action: return CtlBit(kAction);
    case Mash::Swing: return static_cast<uint16_t>(CtlBit(kRight) | CtlBit(kLeft));
    case Mash::Alternate: return static_cast<uint16_t>(CtlBit(kKnife) | CtlBit(kAction));
    }
    return 0;
}
inline Keys MashKeys(const Pool& p, Mash m) {
    switch (m) {
    case Mash::Knife: return Keys{p.key[kKnife], 0};
    case Mash::Action: return Keys{p.key[kAction], 0};
    case Mash::Swing: return Keys{p.key[kLeft], p.key[kRight]};
    case Mash::Alternate: return Keys{p.key[kKnife], p.key[kAction]};
    }
    return Keys{};
}
// Every control a usable step reads: a flank of any of them during a break is judged (another physical key = a miss).
inline uint16_t BagCtls(const Pool& p) {
    uint16_t m = 0;
    for (unsigned i = 0; i < kMashCount; ++i) if (MashUsable(p, static_cast<Mash>(i))) m |= MashCtls(static_cast<Mash>(i));
    for (unsigned i = 0; i < kBreakCount; ++i) {
        const Break b = static_cast<Break>(i);
        if (b == Break::Lateral) {
            if (BreakUsable(p, b, kSideLeft)) m |= static_cast<uint16_t>(CtlBit(kLeft) | CtlBit(kKnife));
            if (BreakUsable(p, b, kSideRight)) m |= static_cast<uint16_t>(CtlBit(kRight) | CtlBit(kKnife));
        } else if (BreakUsable(p, b)) m |= CtlBit(Info(b).ctl);
    }
    for (unsigned c = 0; c < kCtlCount; ++c) if (!p.key[c]) m = static_cast<uint16_t>(m & ~CtlBit(c));
    return m;
}
// Distinct physical keys of the usable BREAK steps (the break bag). The delivery gate: at least 3 on the keyboard after
// the H1a census, or the mixed duel goes back to design.
inline unsigned BreakKeyCount(const Pool& p) {
    Pool only = p; only.mashes = 0;
    const uint16_t bag = BagCtls(only);
    uint8_t seen[kCtlCount]{}; unsigned n = 0;
    for (unsigned c = 0; c < kCtlCount; ++c) {
        if (!(bag & CtlBit(c))) continue;
        bool dup = false;
        for (unsigned i = 0; i < n; ++i) dup = dup || seen[i] == p.key[c];
        if (!dup) seen[n++] = p.key[c];
    }
    return n;
}
constexpr unsigned kMinPhysicalKeys = 3;
// SwordLockPool on top of the device's bag: the player's choice when it keeps at least 3 physical keys, else the
// device's bag (docs: "si quedan menos, se vuelve a la bolsa base").
inline Pool ApplyUserPool(const Pool& device, uint8_t user_breaks, bool* fell_back = nullptr) {
    Pool p = device;
    p.breaks = static_cast<uint8_t>(device.breaks & user_breaks);
    const bool ok = BreakKeyCount(p) >= kMinPhysicalKeys;
    if (fell_back) *fell_back = !ok;
    return ok ? p : device;
}

// ------------------------------------------------------------------ difficulty and tuning
enum class Difficulty : uint8_t {Easy, Normal, Pro};
inline const char* DifficultyName(Difficulty d) {
    return d == Difficulty::Easy ? "easy" : d == Difficulty::Pro ? "pro" : "normal";
}
// pG+0x4F98 (UHD; GC Game_level 0x4F88, the rank LifeDownSet2 scales by, mortal_draw_damage.h:61): 0..10. Professional
// pins it at 10. Decision: 0..3 Easy, 4..9 Normal, 10 Pro; unreadable or out of range (< 0, > 10) = Normal.
// Pending H1c: the rank falls when a Normal player dies, so Normal could get Easy's duel. pG+0x847C (the mode the
// ammo table and the defense factor read, docs/ocarina-item-census-2026-10-11.md:38, defense-native-qualification.md:51)
// is the candidate mode byte: H1c logs both in each mode before H1d picks one.
inline Difficulty DifficultyFromLevel(int level) {
    if (level < 0 || level > 10) return Difficulty::Normal;
    return level <= 3 ? Difficulty::Easy : level >= 10 ? Difficulty::Pro : Difficulty::Normal;
}
struct Tuning {
    double decay_per_s;   // continuous PV decay (0.6/s x the difficulty scale)
    double bonus;         // a hit gives the thrust back plus this
    double fail;          // a miss keeps the thrust and takes this
    double lock_ms;       // the mash is dead this long after a miss
    double window_ms;     // break window (before SwordLockWindowScale)
    double push1_ms, push2_ms;
    double ceiling0;      // Finish ceiling with no hit
    unsigned breaks, min_hits;
    double cap_ms;
#if LINK_SWORD_LOCK
    // Candidate 2 (lab DLL only, sword_lock_draw.h Cand2Tuning; the public DLL's layout and code stay as they are):
    double warn_ms = 250;            // the break's warning (cand2: 500, the new prompt shown through it)
    bool leftovers_free = false;     // the previous step's keys never count as wrong in the next break (they only don't count)
    bool timing = false;             // tap breaks (Twist, Slash, the Lateral's knife) are a timing check (SwordLockTiming)
    double zone0 = 0, zone1 = 1;     // the zone, as fractions of the window the needle sweeps at constant game speed
    double great = 0;                // the "great" sub-zone at the zone's start (fraction of the window)
    double great_bonus = 0;          // a great hit's extra PV
    // Candidate 3 (Donald's 9 Normal duels of 4-nov):
    bool held_over = false;          // a key of this break held over from the previous step is ignored until seen released
    bool lateral_chord = false;      // the Lateral is a chord: direction + Space in either order, a hit when both are down
    bool guard_dirs_free = false;    // Guard (hold aim): a direction press before the right click is ignored, never wrong
    bool hold_from_warning = false;  // cand6: a hold's key pressed in the warning and still down starts the hold as the window opens
    // Candidate 10 (Donald, 4-nov: "el struggle tiene que durar hasta que alguien gane, no debería poderse empatar, puede
    // durar infinito en teoría"): no draw at the cap. A Finish that has not won after kOvertimeFinishMs starts another
    // round (the sequence's breaks again, then the Finish with the ceiling of every hit so far), and each round the
    // Salvador's pressure grows (the decay x (1 + kOvertimeFatigue x round)): stop pressing and he wins. From round
    // kSuddenDeathRound the round's first break is sudden death (a hit wins, a miss loses): growing pressure alone could
    // hold a good player and the Salvador level for ever (the 165 FPS Pro bot: 19 rounds). Only a safety stop
    // (kOvertimeSafetyMs of game time, a stuck duel) still ends in a draw.
    bool overtime = false;
#endif
};
inline Tuning TuningFor(Difficulty d) {
    switch (d) {
    case Difficulty::Easy: return Tuning{0.48, 0.25, 0.10, 0, 1300, 1000, 800, 0.70, 2, 1, 5500};
    case Difficulty::Pro: return Tuning{0.75, 0.22, 0.25, 300, 750, 1000, 600, 0.10, 3, 3, 5500};
    default: return Tuning{0.60, 0.20, 0.20, 300, 1000, 1000, 800, 0.40, 2, 2, 5500};
    }
}
inline constexpr double kLockMs = 350;          // Traba: bars, flash and HIT_SWORD, no input, no decay
inline constexpr double kWarnMs = 250;          // break warning: the thrust, the icon and a cue; nothing counts
inline constexpr double kGraceMs = 200;         // window start: a wrong flank (left over from the mash) is ignored
inline constexpr double kTapMs = 250;           // a tap is released within this
inline constexpr double kMinWindowMs = 600;
inline constexpr double kHolidayMs = 1000.0 / 30.0;   // decay suspended after each counted flank (TP: no decay that frame)
inline constexpr double kClipLockMs = 1000;     // between clip changes (30 TP frames)
inline constexpr double kCapMs = 5500;          // SwordLockCapMs=0
#if LINK_SWORD_LOCK
inline constexpr double kOvertimeFinishMs = 2500;   // candidate 10: a Finish this long without the win starts another round
inline constexpr double kOvertimeFatigue = 0.25;    // the decay grows by this per round
inline constexpr unsigned kSuddenDeathRound = 3;    // from this round on, the round's first break decides: a hit wins, a miss loses
inline constexpr double kOvertimeSafetyMs = 120000; // game time: a duel still running now is stuck (a draw, Stop::Stall)
#endif
inline constexpr double kStallMs = 1500;        // game-time guard past the cap (gestures in progress stop duel time)
inline constexpr double kPauseAbandonMs = 1000; // menu or 0x806F39 longer than this: Abandoned
// SwordLockHold: a held mash key counts like an 8 pps player (Fan RE4, round 4: 8 pps, not 7): a beat on the first tick
// it is held in a stretch, then every 125 ms of a metronome anchored at the start of that tick (the earliest the press
// can have happened, as a player's schedule is). A real flank restarts the metronome.
inline constexpr double kHoldPps = 8;
inline constexpr double kBeatMs = 1000.0 / kHoldPps;
inline constexpr double kMaxDtMs = 2.0 * 1000.0 / 30.0;   // game_clock kMaxDt (2.0 in 1/30 s units) = 66.7 ms
inline constexpr double kStep = 0.1, kSurge = 0.30, kPushCap = 0.30, kCeilingStep = 0.30;
inline constexpr double kWinPa = 48.0, kLosePa = -49.9, kBand = 0.4;
// Comparisons are raw (no epsilon) and associated as in the simulator (t0 + 250 + window, el = t - t0): with the same
// dt the duel adds the same doubles in the same order, so the balance fixture reproduces the simulator's ties at the
// exact 125/1000 ms edges instead of shifting them a tick. In the game dt varies and exact ties do not happen.

// ------------------------------------------------------------------ [Combat] keys of the duel
struct Config {
    double window_scale = 1.0;   // SwordLockWindowScale 1.0..2.0 (accessibility)
    bool hold = false;           // SwordLockHold: holding the mash key = 8 pps, mash stretches only
    double cap_ms = 0;           // SwordLockCapMs: 0 = 5500
    uint32_t seed = 0;           // SwordLockSeed: 0 = frame counter; N fixes the sequence (bot only)
    uint8_t pool = kAllBreaks;   // SwordLockPool: the breaks the player allows (on top of the device's bag)
};
namespace detail {
inline bool SameWord(const wchar_t* a, size_t n, const wchar_t* b) {
    size_t i = 0;
    for (; i < n && b[i]; ++i) if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return i == n && !b[i];
}
inline bool ParseUnsigned(const wchar_t* v, unsigned long& out) {
    if (!v || !*v) return false;
    wchar_t* end = nullptr;
    out = std::wcstoul(v, &end, 0);
    return end && !*end;
}
}  // namespace detail
// "Accion,Cuchillo,Aguante,Lateral,Guardia,X,Pomo" (any case, spaces allowed; "Accion" also with the accent). Empty =
// all. An unknown name counts a problem and is skipped.
inline uint8_t ParsePool(const wchar_t* v, unsigned& problems) {
    if (!v || !*v) return kAllBreaks;
    uint8_t m = 0;
    for (const wchar_t* p = v; *p;) {
        while (*p == L' ' || *p == L',') ++p;
        const wchar_t* s = p;
        while (*p && *p != L',') ++p;
        size_t n = static_cast<size_t>(p - s);
        while (n && s[n - 1] == L' ') --n;
        if (!n) continue;
        bool hit = false;
        for (unsigned i = 0; i < kBreakCount && !hit; ++i)
            if (detail::SameWord(s, n, Info(static_cast<Break>(i)).key)) {m |= BreakBit(static_cast<Break>(i)); hit = true;}
        if (!hit && detail::SameWord(s, n, L"Acción")) {m |= BreakBit(Break::Twist); hit = true;}
        if (!hit) ++problems;
    }
    return m ? m : kAllBreaks;
}
// get(key, out, count) fills `out` with the raw value ("" when missing). A bad value counts a problem and keeps the
// default of that key only.
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[128]{};
    get(L"SwordLockWindowScale", v, 128);
    if (v[0]) {
        wchar_t* end = nullptr; const double s = std::wcstod(v, &end);
        if (end && !*end && s >= 1.0 && s <= 2.0) c.window_scale = s; else ++problems;
    }
    v[0] = 0; get(L"SwordLockHold", v, 128);
    if (v[0] == L'1' && !v[1]) c.hold = true;
    else if (v[0] && !(v[0] == L'0' && !v[1])) ++problems;
    v[0] = 0; get(L"SwordLockCapMs", v, 128);
    unsigned long n = 0;
    if (v[0]) {
        if (detail::ParseUnsigned(v, n) && (n == 0 || (n >= 1000 && n <= 20000))) c.cap_ms = static_cast<double>(n); else ++problems;
    }
#if !LINK_SWORD_LOCK_GRADUATED
    v[0] = 0; get(L"SwordLockSeed", v, 128);
    if (v[0]) {
        if (detail::ParseUnsigned(v, n) && n <= 0xFFFFFFFFul) c.seed = static_cast<uint32_t>(n); else ++problems;
    }
#endif
    v[0] = 0; get(L"SwordLockPool", v, 128);
    c.pool = ParsePool(v, problems);
    return c;
}

// ------------------------------------------------------------------ sequence and physical signature
struct Sequence {
    Mash push2 = Mash::Action;   // Push 2 (Push 1 and the Finish are the knife mash)
    uint8_t breaks = 2;          // 1..3 (2 Easy/Normal, 3 Pro; 1 only for the H1c lab duel)
    Break q[3] = {Break::Twist, Break::Slash, Break::Pommel};
    uint8_t side = kSideNone;    // the Lateral break's direction
    uint8_t alt_first = kKnife;  // Alternate: the first expected key
#if LINK_SWORD_LOCK
    Mash push1 = Mash::Knife;    // candidate 5: Push 1's mash (Knife, or Swing with Pool::swing_ends)
    Mash finish = Mash::Knife;   // candidate 5: the Finish's mash (the same)
#endif
};
inline uint32_t StepCode(Gesture g, uint8_t k1, uint8_t k2, uint8_t side) {
    return static_cast<uint32_t>(g) << 24 | static_cast<uint32_t>(k1) << 16 | static_cast<uint32_t>(k2) << 8 | side;
}
inline uint32_t BreakCode(const Pool& p, Break b, uint8_t side) {
    const Keys k = BreakKeys(p, b, side);
    return StepCode(Info(b).gesture, k.a, k.b, b == Break::Lateral ? side : kSideNone);
}
// Two-key modes sign their keys in a fixed order: which key the Alternate starts on is not a different sequence (it
// follows from Break 1 anyway, AltFirst).
inline uint32_t MashCode(const Pool& p, Mash m) {
    const auto pair = [](Gesture g, uint8_t a, uint8_t b) {return StepCode(g, a < b ? a : b, a < b ? b : a, 0);};
    switch (m) {
    case Mash::Knife: return StepCode(Gesture::Mash, p.key[kKnife], 0, 0);
    case Mash::Action: return StepCode(Gesture::Mash, p.key[kAction], 0, 0);
    case Mash::Swing: return pair(Gesture::Swing, p.key[kLeft], p.key[kRight]);
    case Mash::Alternate: return pair(Gesture::Alternate, p.key[kKnife], p.key[kAction]);
    }
    return 0;
}
// The Alternate's first expected key: the one that does not share a physical key with the step before it (Break 1);
// the knife when Break 1 uses neither.
inline uint8_t AltFirst(const Pool& p, const Keys& before) {return before.Has(p.key[kKnife]) ? kAction : kKnife;}
// Steps in order: Push 1, Break 1, Push 2, Break 2, [Break 3], Finish (lab duel with one break: Push 1, Break 1, Finish).
struct Signature {
    uint32_t code[6]{};
    uint8_t n = 0;
    bool operator==(const Signature& o) const {
        if (n != o.n) return false;
        for (unsigned i = 0; i < n; ++i) if (code[i] != o.code[i]) return false;
        return true;
    }
    bool operator!=(const Signature& o) const {return !(*this == o);}
    uint32_t first_break() const {return n > 1 ? code[1] : 0;}
};
inline Signature Sign(const Sequence& s, const Pool& p) {
    Signature g;
#if LINK_SWORD_LOCK
    const uint32_t knife = MashCode(p, s.push1), finish = MashCode(p, s.finish);
#else
    const uint32_t knife = MashCode(p, Mash::Knife);
#endif
    g.code[g.n++] = knife;
    g.code[g.n++] = BreakCode(p, s.q[0], s.side);
    if (s.breaks > 1) {
        g.code[g.n++] = MashCode(p, s.push2);
        for (unsigned i = 1; i < s.breaks && i < 3; ++i) g.code[g.n++] = BreakCode(p, s.q[i], s.side);
    }
#if LINK_SWORD_LOCK
    g.code[g.n++] = finish;
#else
    g.code[g.n++] = knife;
#endif
    return g;
}
// seq= for the log: steps joined by '-', each a gesture letter, its physical key ids in hex and L/R for a side,
// e.g. "m20-t45-w4144-l4120R-m20".
inline void FormatSignature(const Signature& g, char* out, size_t size) {
    if (!out || !size) return;
    size_t at = 0; out[0] = 0;
    for (unsigned i = 0; i < g.n && at + 1 < size; ++i) {
        const uint32_t c = g.code[i];
        const char side = (c & 0xFF) == kSideLeft ? 'L' : (c & 0xFF) == kSideRight ? 'R' : 0;
        const unsigned k2 = (c >> 8) & 0xFF;
        char step[16];
        if (k2) std::snprintf(step, sizeof(step), "%s%c%02X%02X%s", i ? "-" : "", GestureLetter(static_cast<Gesture>(c >> 24)),
                              (c >> 16) & 0xFF, k2, side == 'L' ? "L" : side == 'R' ? "R" : "");
        else std::snprintf(step, sizeof(step), "%s%c%02X", i ? "-" : "", GestureLetter(static_cast<Gesture>(c >> 24)), (c >> 16) & 0xFF);
        for (const char* q = step; *q && at + 1 < size; ++q) out[at++] = *q;
        out[at] = 0;
    }
}

// Every sequence the rules allow for `nq` breaks drawn from `breaks_mask` (on top of the pool), in a fixed order.
// f(const Sequence&) is called for each; returns how many.
template <class F> unsigned ForEachSequence(const Pool& p, unsigned nq, uint8_t breaks_mask, F&& f) {
    struct V {Break b; uint8_t side; Keys k; uint32_t code; Family fam;};
#if LINK_SWORD_LOCK
    // The keys a break shows a neighbouring MASH (Push 1, Push 2, the Finish): the Lateral's direction only, with
    // Pool::lateral_by_knife (its Space may be the mash's: the knife mash, the Alternate). Break-to-break stays whole.
    const auto by_knife = [&p](const V& x, const Keys& mash) {
        if (p.lateral_by_knife && x.b == Break::Lateral) return Keys{x.k.a, 0}.Meets(mash);
        return x.k.Meets(mash);
    };
#else
    const auto by_knife = [](const V& x, const Keys& mash) {return x.k.Meets(mash);};
#endif
    V v[kBreakCount + 1]; unsigned nv = 0;
    for (unsigned i = 0; i < kBreakCount; ++i) {
        const Break b = static_cast<Break>(i);
        if (!(breaks_mask & BreakBit(b))) continue;
        if (b == Break::Lateral) {
            for (uint8_t side = kSideLeft; side <= kSideRight; ++side)
                if (BreakUsable(p, b, side)) v[nv++] = V{b, side, BreakKeys(p, b, side), BreakCode(p, b, side), Info(b).family};
        } else if (BreakUsable(p, b)) {
            v[nv++] = V{b, kSideNone, BreakKeys(p, b, kSideNone), BreakCode(p, b, kSideNone), Info(b).family};
        }
    }
    if (!MashUsable(p, Mash::Knife) || nq < 1 || nq > 3) return 0;
    unsigned count = 0;
#if LINK_SWORD_LOCK
    // candidate 5: Push 1 and the Finish each the knife mash or (Pool::swing_ends) the Swing; Knife first (H1d's order)
    const Mash ends[2] = {Mash::Knife, Mash::Swing};
    const unsigned n_ends = p.swing_ends && MashUsable(p, Mash::Swing) ? 2u : 1u;
    for (unsigned e1 = 0; e1 < n_ends; ++e1) for (unsigned e2 = 0; e2 < n_ends; ++e2) {
    const Keys knife = MashKeys(p, ends[e1]), fin = MashKeys(p, ends[e2]);
    const auto put = [&](Sequence& s) {s.push1 = ends[e1]; s.finish = ends[e2]; f(static_cast<const Sequence&>(s)); ++count;};
#else
    const Keys knife = MashKeys(p, Mash::Knife), fin = knife;
    const auto put = [&](Sequence& s) {f(static_cast<const Sequence&>(s)); ++count;};
#endif
    if (nq == 1) {   // the lab duel: Push 1, Break 1, Finish
        for (unsigned i = 0; i < nv; ++i) {
            if (by_knife(v[i], knife) || by_knife(v[i], fin)) continue;
            Sequence s; s.breaks = 1; s.q[0] = v[i].b; s.side = v[i].side; s.push2 = Mash::Action;
            put(s);
        }
#if LINK_SWORD_LOCK
        continue;
#else
        return count;
#endif
    }
    for (unsigned m = 1; m < kMashCount; ++m) {
        const Mash mm = static_cast<Mash>(m);
        if (!MashUsable(p, mm)) continue;
        const Keys mk = MashKeys(p, mm);
        const bool alt = mm == Mash::Alternate;
        for (unsigned i = 0; i < nv; ++i) {
            const V& a = v[i];
            if (by_knife(a, knife)) continue;                       // Push 1 | Break 1
            if (!alt && by_knife(a, mk)) continue;                  // Break 1 | Push 2 (Alternate: starts on its other key)
            const uint8_t first = AltFirst(p, a.k);
            for (unsigned j = 0; j < nv; ++j) {
                const V& b = v[j];
                if (b.b == a.b || b.code == a.code) continue;        // no break twice (kind, or physical key + gesture)
                if (by_knife(b, mk)) continue;                       // Push 2 | Break 2
                if (b.k.Meets(a.k) || b.fam == a.fam) continue;      // Break 2: another key and family than Break 1
                if (nq == 2) {
                    if (by_knife(b, fin)) continue;                  // Break 2 | Finish
                    Sequence s; s.breaks = 2; s.push2 = mm; s.q[0] = a.b; s.q[1] = b.b; s.alt_first = first;
                    s.side = a.side ? a.side : b.side;
                    put(s);
                    continue;
                }
                for (unsigned l = 0; l < nv; ++l) {
                    const V& c = v[l];
                    if (c.b == a.b || c.b == b.b || c.code == a.code || c.code == b.code) continue;
                    if (c.k.Meets(b.k) || by_knife(c, fin)) continue;     // Break 2 | Break 3 | Finish
                    Sequence s; s.breaks = 3; s.push2 = mm; s.q[0] = a.b; s.q[1] = b.b; s.q[2] = c.b; s.alt_first = first;
                    s.side = a.side ? a.side : b.side ? b.side : c.side;
                    put(s);
                }
            }
        }
    }
#if LINK_SWORD_LOCK
    }   // Push 1 x Finish
#endif
    return count;
}

// ------------------------------------------------------------------ the draw
inline uint64_t Mix64(uint64_t x) {   // splitmix64 finalizer
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
class Rng {
    uint64_t s_;
public:
    explicit Rng(uint64_t seed) : s_(Mix64(seed)) {}
    uint32_t Next() {s_ += 0x9E3779B97F4A7C15ull; return static_cast<uint32_t>(Mix64(s_) >> 32);}
    uint32_t Below(uint32_t n) {return n ? static_cast<uint32_t>((static_cast<uint64_t>(Next()) * n) >> 32) : 0;}
};
// How much of the history rule a pick kept. Full: not either of the two previous signatures and not the previous first
// break's key + gesture. History: the two previous signatures only. Previous: only not the previous duel's signature
// (Donald's requirement: two duels in a row never ask for the same sequence). None: the bag has one sequence.
enum class Freshness : uint8_t {Full, History, Previous, None};
inline const char* FreshnessName(Freshness f) {
    return f == Freshness::Full ? "full" : f == Freshness::History ? "history" : f == Freshness::Previous ? "previous" : "none";
}
struct Pick {
    bool ok = false;            // a sequence was drawn
    bool repeated = false;      // a rule of the history was relaxed (level below Full): the bag is too small, logged
    bool easy_fallback = false; // Easy's one-control bag had nothing at this level: the full bag was used
    Freshness level = Freshness::Full;
    Sequence seq;
    Signature sig;
    uint32_t seed = 0;
    unsigned valid = 0, fresh = 0;   // sequences the rules allow / candidates at the level taken
};
class Sorter {
public:
    // Seed: frameCounter GLOBAL+0x530C XOR the owner XOR the duel counter. SwordLockSeed=N replaces the frame and the
    // owner (pointers move between sessions) so the bot replays the same sequences: N XOR the counter.
    static uint32_t Seed(uint32_t frame, uint32_t owner, uint32_t counter, uint32_t fixed) {
        const uint32_t base = fixed ? (fixed ^ counter) : (frame ^ owner ^ counter);
        return static_cast<uint32_t>(Mix64(base));
    }
    // Uniform over the valid sequences that keep the strictest history level some sequence can keep; at each level the
    // difficulty's bag first, then (Easy) the full bag.
    Pick Next(const Pool& pool, Difficulty d, uint32_t frame, uint32_t owner, uint32_t fixed = 0) {
        Pick out;
        out.seed = Seed(frame, owner, counter_, fixed);
        ++counter_;
        const unsigned nq = TuningFor(d).breaks;
        const uint8_t bag = d == Difficulty::Easy ? kEasyBreaks : kAllBreaks;
        for (unsigned i = 0; i < 8 && !out.ok; ++i) {
            const Freshness level = static_cast<Freshness>(i / 2);
            const bool full_bag = i % 2 == 1;
            if (full_bag && bag == kAllBreaks) continue;
            Rng rng(static_cast<uint64_t>(out.seed) << 8 | i);
            unsigned k = 0, valid = 0;
            ForEachSequence(pool, nq, full_bag ? kAllBreaks : bag, [&](const Sequence& s) {
                const Signature g = Sign(s, pool);
                ++valid;
                if (!Keeps(g, level)) return;
                ++k;
                if (rng.Below(k) == 0) {out.seq = s; out.sig = g;}
            });
            if (i == 0) out.valid = valid;
            if (k) {out.ok = true; out.level = level; out.repeated = level != Freshness::Full; out.easy_fallback = full_bag; out.fresh = k;}
        }
        if (out.ok) Remember(out.sig);
        return out;
    }
    bool Keeps(const Signature& g, Freshness level) const {
        if (level == Freshness::None) return true;
        if (have_ >= 1 && g == last_[0]) return false;
        if (level == Freshness::Previous) return true;
        if (have_ >= 2 && g == last_[1]) return false;
        if (level == Freshness::History) return true;
        return !(have_ >= 1 && g.first_break() == last_[0].first_break());
    }
    bool Fresh(const Signature& g) const {return Keeps(g, Freshness::Full);}
    void Remember(const Signature& g) {last_[1] = last_[0]; last_[0] = g; if (have_ < 2) ++have_;}
    void Forget() {have_ = 0;}
    uint32_t counter() const {return counter_;}
    const Signature& last(unsigned i) const {return last_[i < 2 ? i : 1];}
    unsigned history() const {return have_;}
private:
    Signature last_[2];
    unsigned have_ = 0;
    uint32_t counter_ = 0;
};

// ------------------------------------------------------------------ the duel
enum class Phase : uint8_t {Idle, Lock, Push1, Break, Push2, Finish, Done};
inline const char* PhaseName(Phase p) {
    switch (p) {
    case Phase::Lock: return "lock"; case Phase::Push1: return "push1"; case Phase::Break: return "break";
    case Phase::Push2: return "push2"; case Phase::Finish: return "finish"; case Phase::Done: return "done"; default: return "idle";
    }
}
enum class Result : uint8_t {None, Win, Lose, Draw, Abandoned};
inline const char* ResultName(Result r) {
    switch (r) {
    case Result::Win: return "win"; case Result::Lose: return "lose"; case Result::Draw: return "draw";
    case Result::Abandoned: return "abandoned"; default: return "none";
    }
}
// Why the duel ended without a winner. Pause: the menu or 0x806F39 held longer than 1 s (counted here); the rest are
// the wiring's same-frame reasons (Duel::Stop).
enum class Stop : uint8_t {None, Cap, Stall, Pause, Qte, FreezeLost, Load, Other};
inline const char* StopName(Stop s) {
    switch (s) {
    case Stop::Cap: return "cap"; case Stop::Stall: return "stall"; case Stop::Pause: return "pause"; case Stop::Qte: return "qte";
    case Stop::FreezeLost: return "freeze_lost"; case Stop::Load: return "load"; case Stop::Other: return "other"; default: return "none";
    }
}
enum class Miss : uint8_t {None, Wrong, Multi, Expired, Released
#if LINK_SWORD_LOCK
    , Early, Late   // cand2's timing check: pressed before the zone / the needle left the zone
#endif
};
inline const char* MissName(Miss m) {
    switch (m) {
    case Miss::Wrong: return "wrong"; case Miss::Multi: return "multi"; case Miss::Expired: return "expired";
    case Miss::Released: return "released";
#if LINK_SWORD_LOCK
    case Miss::Early: return "early"; case Miss::Late: return "late";
#endif
    default: return "none";
    }
}
enum class Band : uint8_t {A, B, C};   // clips al_egnd_tubazeri_a (neutral) / _b (losing) / _c (winning)
inline char BandLetter(Band b) {return b == Band::B ? 'b' : b == Band::C ? 'c' : 'a';}

// One tick: the logical controls held (Key_btn_on) and pressed (Key_btn_trg, one flank per tick), game_clock's dt in
// ms and whether the game is paused (menu, 0x806F39).
struct Input {
    uint16_t held = 0, pressed = 0;
    double dt_ms = 0;
    bool paused = false;
};
// Events of one tick (bits).
enum Event : uint32_t {
    kEvPhase = 1u << 0,     // a stretch or break began (or the duel ended)
    kEvFlank = 1u << 1,     // a counted mash flank (+0.1 up to the ceiling)
    kEvBeat = 1u << 2,      // a SwordLockHold beat counted as a flank
    kEvLocked = 1u << 3,    // a mash flank swallowed by the lock after a miss
    kEvOffTurn = 1u << 4,   // Alternate: the other key (does not count, does not punish)
    kEvSurge = 1u << 5,     // a break's warning opened: the owner's thrust (-0.30)
    kEvWindow = 1u << 6,    // the break window opened
    kEvGesture = 1u << 7,   // a correct gesture started inside the window (duel time waits for it)
    kEvHit = 1u << 8,       // break hit
    kEvMiss = 1u << 9,      // break missed (BreakRecord::why)
    kEvNoCount = 1u << 10,  // a gesture that does not count (tap held over 250 ms, hold released early, knife without
                            // the lateral direction): the window goes on
    kEvIgnored = 1u << 11,  // flanks ignored by the warning or the grace
    kEvAltNext = 1u << 12,  // Alternate: the expected key changed (the offer's btn changes)
    kEvClip = 1u << 13,     // the clip changed (band, 1000 ms lock)
    kEvEnd = 1u << 14,
    kEvRound = 1u << 15,    // candidate 10 (lab): an overtime round began (the Finish did not win in time)
};
// What the wiring draws now: one native offer, centred like the vanilla QTE; the sequence is never shown ahead.
struct Prompt {
    bool show = false;
    Phase phase = Phase::Idle;
    uint8_t step = 0;            // index in the signature: Push 1 0, Break 1 1, Push 2 2, Break 2 3, Break 3 4, Finish last
    uint8_t break_index = 0;     // 0..2 during a break
    Gesture gesture = Gesture::None;
    uint8_t ctl = kNoCtl;        // the control asked now (Alternate: the next expected key; Lateral: the direction)
    uint8_t ctl2 = kNoCtl;       // Lateral: the knife; otherwise kNoCtl
    Glyph glyph = Glyph::None;
    uint8_t side = kSideNone;
    bool warning = false;        // the break's 250 ms warning (thrust and icon, nothing counts yet)
    bool executing = false;      // a correct gesture is being completed
    double remaining_ms = -1;    // to the end of this push or break window (game ms); -1 = open (Finish)
    double gesture_ms = 0;       // how long the gesture in progress has run
#if LINK_SWORD_LOCK
    bool timing = false;         // cand2: this break is a timing check (needle, zone, great)
    double needle = 0;           // 0..1 of the window (0 through the warning)
    double zone0 = 0, zone1 = 1, great1 = 0;   // the zone and the end of its great part (fractions of the window)
    double warn_ms = 250;        // this duel's warning length
#endif
};
struct BreakRecord {
    bool valid = false;
    uint8_t index = 0;
    Break id = Break::Twist;
    uint8_t side = kSideNone;
    bool ok = false;
    Miss why = Miss::None;
    uint16_t got = 0;           // controls pressed on the resolving tick (the gesture start for a hit)
    double ms = 0;              // game ms from the window opening to the resolution
#if LINK_SWORD_LOCK
    bool great = false;         // cand2: a timing hit inside the great part
    double needle = -1;         // cand2: the needle (0..1) at the press that resolved a timing break (-1: none)
    uint16_t ignored = 0;       // cand2: leftover keys of the previous step pressed in this break (they never counted)
    bool from_warning = false;  // cand6: the hold was pressed in the warning and kept down (started as the window opened)
#endif
};

class Duel {
public:
    struct Setup {
        Difficulty difficulty = Difficulty::Normal;
        Tuning tuning = TuningFor(Difficulty::Normal);
        Config config;
        Sequence sequence;
        Pool pool;
#if LINK_SWORD_LOCK
        double mash_gain = kStep;   // PV per counted flank (lab DLL only: the public DLL's layout and code stay as they are)
        double hold_pps = kHoldPps; // SwordLockHold's beat rate (lab DLL only; H1d's lab uses the design's 7 pps, sword_lock_draw.h)
        double swing_gain = 0;      // candidate 5: PV per counted Swing flank (A/D alternating); 0 = mash_gain
#endif
    };
    static Setup For(Difficulty d, const Config& c, const Sequence& s, const Pool& p) {
        Setup u; u.difficulty = d; u.tuning = TuningFor(d); u.config = c; u.sequence = s; u.pool = p; return u;
    }

    // held_now: the controls held when the lock opens (the parry's own press): each must be seen released (or pressed
    // anew) before it counts.
    void Start(const Setup& s, uint16_t held_now) {
        *this = Duel();
        set_ = s;
        if (set_.sequence.breaks < 1) set_.sequence.breaks = 1;
        if (set_.sequence.breaks > 3) set_.sequence.breaks = 3;
#if LINK_SWORD_LOCK
        if (!(set_.mash_gain > 0.0) || !std::isfinite(set_.mash_gain)) set_.mash_gain = kStep;
        if (!(set_.hold_pps >= 1.0) || !(set_.hold_pps <= 30.0)) set_.hold_pps = kHoldPps;
#endif
#if LINK_SWORD_LOCK
        kWarnMs = set_.tuning.warn_ms > 0 && set_.tuning.warn_ms <= 2000 ? set_.tuning.warn_ms : sword_lock::duel::kWarnMs;
#endif
        double scale = set_.config.window_scale;
        if (!(scale >= 1.0)) scale = 1.0;
        if (scale > 2.0) scale = 2.0;
        window_ = (set_.tuning.window_ms < kMinWindowMs ? kMinWindowMs : set_.tuning.window_ms) * scale;
        cap_ = set_.config.cap_ms > 0 ? set_.config.cap_ms : set_.tuning.cap_ms > 0 ? set_.tuning.cap_ms : kCapMs;
        bag_ = BagCtls(set_.pool);
        dirty_ = static_cast<uint16_t>(held_now & kAllCtls);
        phase_ = Phase::Lock;
    }
    // Same-frame end decided by the wiring (QTE, the freeze's bits lost, a load, ...): Abandoned, nothing else.
    void Abandon(Stop why) {if (phase_ != Phase::Idle && phase_ != Phase::Done) End(Result::Abandoned, why);}

    uint32_t Tick(const Input& in) {
        events_ = 0;
        if (phase_ == Phase::Idle || phase_ == Phase::Done) return 0;
        double dt = in.dt_ms;
        if (!(dt > 0) || !std::isfinite(dt)) return 0;
        if (dt > kMaxDtMs) dt = kMaxDtMs;
        uint16_t held = static_cast<uint16_t>((in.held | in.pressed) & kAllCtls);
        uint16_t press = static_cast<uint16_t>(in.pressed & kAllCtls);
        // A key held from the parry is clean once seen released, or pressed anew (a native flank) after the lock: in the
        // lock a flank can still be the parry's own press (the wiring may start the duel on that frame).
        const uint16_t proof = phase_ == Phase::Lock ? 0 : press;
        dirty_ = static_cast<uint16_t>(dirty_ & held & ~proof);
        held = static_cast<uint16_t>(held & ~dirty_);
        press = static_cast<uint16_t>(press & ~dirty_);
        if (in.paused) {
            pause_ms_ += dt;
            if (pause_ms_ >= kPauseAbandonMs) End(Result::Abandoned, Stop::Pause);
            return events_;
        }
        pause_ms_ = 0;
        g_ += dt;
        if (exec_) {Executing(held, press); return events_;}
        d_ += dt;
        switch (phase_) {
        case Phase::Lock: if (d_ - phase_d0_ >= kLockMs) Next(); break;
        case Phase::Push1: case Phase::Push2: case Phase::Finish: MashTick(held, press, dt); break;
        case Phase::Break: if (BreakTick(held, press, dt)) return events_; break;   // a gesture started: tail deferred
        default: break;
        }
        Tail(dt);
        return events_;
    }

    // State.
    Phase phase() const {return phase_;}
    Result result() const {return result_;}
    Stop stop() const {return stop_;}
    bool Running() const {return phase_ != Phase::Idle && phase_ != Phase::Done;}
    double pv() const {return pv_;}
    double pa() const {return pa_;}
    double ceiling() const {return ceiling_;}
    unsigned hits() const {return hits_;}
    unsigned misses() const {return misses_;}
#if LINK_SWORD_LOCK
    unsigned rounds() const {return round_;}   // candidate 10: overtime rounds
#endif
    unsigned flanks() const {return flanks_;}
    double duel_ms() const {return d_;}
    double game_ms() const {return g_;}
    double window_ms() const {return window_;}
    double cap_ms() const {return cap_;}
    uint8_t break_index() const {return break_i_;}
    Mash mash() const {return mode_;}
    uint8_t expected() const {return alt_expect_;}
    // Voices follow the band every tick (Fan Zelda, round 4); the clip follows it with the 1000 ms lock.
    Band band() const {return band_;}
    Band clip() const {return clip_;}
    unsigned clip_changes() const {return clip_changes_;}
    double last_clip_ms() const {return last_clip_ms_;}
    const BreakRecord& last_break() const {return rec_;}
    const Setup& setup() const {return set_;}
    uint16_t dirty() const {return dirty_;}
    bool executing() const {return exec_;}
    Prompt prompt() const {
        Prompt p; p.phase = phase_;
        const Sequence& s = set_.sequence;
        switch (phase_) {
        case Phase::Push1: case Phase::Push2: case Phase::Finish: {
            p.show = true;
            p.step = static_cast<uint8_t>(phase_ == Phase::Push1 ? 0 : phase_ == Phase::Push2 ? 2 : s.breaks == 1 ? 2 : 2 + s.breaks);
            const double dur = phase_ == Phase::Push1 ? set_.tuning.push1_ms : phase_ == Phase::Push2 ? set_.tuning.push2_ms : -1;
            p.remaining_ms = dur > 0 ? (dur - (d_ - phase_d0_) > 0 ? dur - (d_ - phase_d0_) : 0) : -1;
            switch (mode_) {
            case Mash::Knife: p.gesture = Gesture::Mash; p.ctl = kKnife; p.glyph = Glyph::KnifeRapid; break;
            case Mash::Action: p.gesture = Gesture::Mash; p.ctl = kAction; p.glyph = Glyph::ActionRapid; break;
            case Mash::Swing: p.gesture = Gesture::Swing; p.ctl = swing_last_ == kRight ? kLeft : kRight; p.glyph = Glyph::Gacha; break;
            case Mash::Alternate:
                p.gesture = Gesture::Alternate; p.ctl = alt_expect_;
                p.glyph = alt_expect_ == kAction ? Glyph::ActionNormal : Glyph::Knife;
                break;
            }
            return p;
        }
        case Phase::Break: {
            const Break b = s.q[break_i_];
            const BreakInfo& bi = Info(b);
            const double el = g_ - break_g0_;
            p.show = true; p.break_index = break_i_;
            p.step = static_cast<uint8_t>(break_i_ == 0 ? 1 : 2 + break_i_);
            p.gesture = bi.gesture; p.warning = g_ < break_g0_ + kWarnMs; p.executing = exec_;
#if LINK_SWORD_LOCK
            p.warn_ms = kWarnMs;
            if (Timed(b)) {
                p.timing = true; p.zone0 = set_.tuning.zone0; p.zone1 = set_.tuning.zone1; p.great1 = set_.tuning.zone0 + set_.tuning.great;
                const double f = (g_ - (break_g0_ + kWarnMs)) / window_;
                p.needle = exec_ ? exec_frac_ : f < 0 ? 0 : f > 1 ? 1 : f;
            }
#endif
            if (b == Break::Lateral) {
                p.side = s.side; p.ctl = DirCtl(s.side); p.ctl2 = kKnife;
                p.glyph = s.side == kSideLeft ? Glyph::LateralLeft : Glyph::LateralRight;
            } else {p.ctl = bi.ctl; p.glyph = bi.glyph;}
            const double left = kWarnMs + window_ - el;
            p.remaining_ms = left > 0 ? left : 0;
            p.gesture_ms = exec_ ? g_ - exec_g0_ : 0;
            return p;
        }
        default: return p;
        }
    }

private:
    Setup set_;
    Phase phase_ = Phase::Idle;
    Result result_ = Result::None;
    Stop stop_ = Stop::None;
    Mash mode_ = Mash::Knife;
    double window_ = 1000, cap_ = kCapMs;
    double d_ = 0, g_ = 0, phase_d0_ = 0, break_g0_ = 0;
    double pv_ = 0, pa_ = 0, ceiling_ = 0;
    double lock_until_ = -1, holiday_until_ = -1;
    double next_beat_ = -1;   // SwordLockHold: duel time of the next beat (-1: none yet in this stretch)
    double clip_lock_until_ = 0, last_clip_ms_ = -1;
    double pause_ms_ = 0, exec_g0_ = 0, pending_dt_ = 0;
    unsigned hits_ = 0, misses_ = 0, flanks_ = 0, clip_changes_ = 0;
    uint16_t bag_ = 0, dirty_ = 0;
    uint8_t break_i_ = 0, alt_expect_ = kKnife, swing_last_ = kNoCtl;
    bool exec_ = false, window_seen_ = false;
#if LINK_SWORD_LOCK
    unsigned round_ = 0;      // candidate 10: overtime rounds so far (0 = the sequence's own)
    double Fatigue() const {return set_.tuning.overtime ? 1.0 + kOvertimeFatigue * round_ : 1.0;}
#endif
    Band band_ = Band::A, clip_ = Band::A;
    Keys req_;
    BreakRecord rec_;
    uint32_t events_ = 0;
#if LINK_SWORD_LOCK
    uint16_t leftover_ = 0;   // cand2: the previous step's controls that are not this break's keys (never wrong here)
    uint16_t stale_ = 0;      // cand3: this break's keys held over from the previous step, until each is seen released
    bool stale_set_ = false;  // cand3: stale_ taken on the break's first tick (the keys held then)
    uint16_t stale_from_ = 0; // cand3: the previous step's controls (the candidates for stale_)
    uint16_t warn_down_ = 0;  // cand6: this hold break's key pressed in its warning and still down
    double exec_frac_ = -1;   // cand2: the needle at the press of the timing gesture in progress
    // The break's warning of this duel (cand2: 500 ms): a member that shadows the namespace's kWarnMs inside the duel, so
    // every use keeps the public build's own tokens (its code stays byte for byte); set from Tuning::warn_ms in Start.
    double kWarnMs = 250;
    bool Timed(Break b) const {
        return set_.tuning.timing && (Info(b).gesture == Gesture::Tap || b == Break::Lateral);
    }
    double Needle() const {return (g_ - (break_g0_ + kWarnMs)) / window_;}
    // The controls the previous step asked (a mash's, or the previous break's and its direction).
    uint16_t PreviousCtls(bool from_mash, uint8_t prev_break) const {
        if (from_mash) return MashCtls(mode_);
        const Break b = set_.sequence.q[prev_break];
        uint16_t c = CtlBit(Info(b).ctl);
        if (b == Break::Lateral) c = static_cast<uint16_t>(c | CtlBit(DirCtl(set_.sequence.side)) | CtlBit(kKnife));
        return c;
    }
#endif

    void End(Result r, Stop why = Stop::None) {
        result_ = r; stop_ = why; phase_ = Phase::Done; exec_ = false;
        events_ |= kEvEnd | kEvPhase;
    }
    void EnterMash(Phase ph, Mash m) {
        phase_ = ph; mode_ = m; phase_d0_ = d_; next_beat_ = -1; swing_last_ = kNoCtl;
        alt_expect_ = set_.sequence.alt_first == kAction ? kAction : kKnife;
        const double c = ph == Phase::Finish ? set_.tuning.ceiling0 + kCeilingStep * hits_ : pv_ + kPushCap;
        ceiling_ = c < 1.0 ? c : 1.0;
    }
    void EnterBreak(uint8_t i) {
#if LINK_SWORD_LOCK
        const bool from_mash = phase_ != Phase::Break;
        const uint16_t prev = set_.tuning.leftovers_free ? PreviousCtls(from_mash, break_i_) : 0;
        exec_frac_ = -1;
#endif
        phase_ = Phase::Break; break_i_ = i; phase_d0_ = d_; break_g0_ = g_; window_seen_ = false; exec_ = false;
        pv_ = pv_ - kSurge > -1.0 ? pv_ - kSurge : -1.0;
        req_ = BreakKeys(set_.pool, set_.sequence.q[i], set_.sequence.side);
        events_ |= kEvSurge;
#if LINK_SWORD_LOCK
        leftover_ = 0; rec_.ignored = 0;
        for (unsigned c = 0; c < kCtlCount; ++c)
            if ((prev & CtlBit(c)) && !req_.Has(set_.pool.key[c])) leftover_ = static_cast<uint16_t>(leftover_ | CtlBit(c));
        // cand3: the previous step's keys that ARE this break's (the Lateral's Space after a Space mash, the knife before a
        // Slash) are not leftovers any more: only one held over from the step before is ignored, until seen released.
        stale_from_ = set_.tuning.held_over ? static_cast<uint16_t>(prev & ~leftover_) : 0; stale_ = 0; stale_set_ = false;
        warn_down_ = 0; rec_.from_warning = false;
#endif
    }
    void Next() {
        events_ |= kEvPhase;
        const uint8_t nq = set_.sequence.breaks;
        switch (phase_) {
#if LINK_SWORD_LOCK
        case Phase::Lock: EnterMash(Phase::Push1, set_.sequence.push1); break;
#else
        case Phase::Lock: EnterMash(Phase::Push1, Mash::Knife); break;
#endif
        case Phase::Push1: EnterBreak(0); break;
        case Phase::Push2: EnterBreak(1); break;
        case Phase::Break:
#if LINK_SWORD_LOCK
            if (break_i_ + 1 >= nq) EnterMash(Phase::Finish, set_.sequence.finish);
#else
            if (break_i_ + 1 >= nq) EnterMash(Phase::Finish, Mash::Knife);
#endif
            else if (break_i_ == 0) EnterMash(Phase::Push2, set_.sequence.push2);
            else EnterBreak(static_cast<uint8_t>(break_i_ + 1));
            break;
        default: break;
        }
    }
    // The flank this tick counts for the mash: a real press of the asked key, or a SwordLockHold beat.
    void MashTick(uint16_t held, uint16_t press, double dt) {
        // decay of [d-dt, d] outside the 33.3 ms suspension of the previous flanks (the simulator's order)
        const double from = holiday_until_ > d_ - dt ? holiday_until_ : d_ - dt;
        double frac = (d_ - from) / dt;
        frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
#if LINK_SWORD_LOCK
        pv_ -= set_.tuning.decay_per_s * Fatigue() * (dt / 1000.0) * frac;
#else
        pv_ -= set_.tuning.decay_per_s * (dt / 1000.0) * frac;
#endif
        if (pv_ < -1.0) pv_ = -1.0;
        uint8_t c = kNoCtl;
        switch (mode_) {
        case Mash::Knife: if (press & CtlBit(kKnife)) c = kKnife; break;
        case Mash::Action: if (press & CtlBit(kAction)) c = kAction; break;
        case Mash::Swing: {
            const bool r = (press & CtlBit(kRight)) != 0, l = (press & CtlBit(kLeft)) != 0;
            if (r != l) {const uint8_t dir = r ? kRight : kLeft; if (dir != swing_last_) c = dir;}
            break;
        }
        case Mash::Alternate: {
            const uint8_t other = alt_expect_ == kKnife ? kAction : kKnife;
            if (press & CtlBit(other)) events_ |= kEvOffTurn;
            else if (press & CtlBit(alt_expect_)) c = alt_expect_;
            break;
        }
        }
        bool beat = false;
        const double tick0 = d_ - dt;   // the start of this tick
#if LINK_SWORD_LOCK
        const double beat_ms = 1000.0 / set_.hold_pps;
        if (c != kNoCtl) next_beat_ = tick0 + beat_ms;
        else if (set_.config.hold && (held & MashCtls(mode_)) && (next_beat_ < 0 || d_ >= next_beat_)) {
            beat = true; next_beat_ = next_beat_ < 0 ? tick0 + beat_ms : next_beat_ + beat_ms;
#else
        if (c != kNoCtl) next_beat_ = tick0 + kBeatMs;
        else if (set_.config.hold && (held & MashCtls(mode_)) && (next_beat_ < 0 || d_ >= next_beat_)) {
            beat = true; next_beat_ = next_beat_ < 0 ? tick0 + kBeatMs : next_beat_ + kBeatMs;
#endif
            c = mode_ == Mash::Alternate ? alt_expect_ : mode_ == Mash::Swing ? (swing_last_ == kRight ? kLeft : kRight) :
                mode_ == Mash::Action ? kAction : kKnife;
        }
        if (c != kNoCtl) {
            if (d_ >= lock_until_) {
#if LINK_SWORD_LOCK
                const double gain = mode_ == Mash::Swing && set_.swing_gain > 0 ? set_.swing_gain : set_.mash_gain;
#else
                const double gain = kStep;
#endif
                if (pv_ < ceiling_) pv_ = pv_ + gain < ceiling_ ? pv_ + gain : ceiling_;
                holiday_until_ = d_ + kHolidayMs;
                if (mode_ == Mash::Swing) swing_last_ = c;
                if (mode_ == Mash::Alternate) {alt_expect_ = c == kKnife ? kAction : kKnife; events_ |= kEvAltNext;}
                events_ |= beat ? kEvBeat : kEvFlank; ++flanks_;
            } else events_ |= kEvLocked;
        }
        const double dur = phase_ == Phase::Push1 ? set_.tuning.push1_ms : phase_ == Phase::Push2 ? set_.tuning.push2_ms : -1;
        if (dur > 0 && d_ - phase_d0_ >= dur) Next();
#if LINK_SWORD_LOCK
        else if (set_.tuning.overtime && phase_ == Phase::Finish && d_ - phase_d0_ >= kOvertimeFinishMs) {   // candidate 10
            ++round_; events_ |= kEvRound | kEvPhase; EnterBreak(0);
        }
#endif
    }
    // Pressed bag controls on another physical key than the asked ones; `distinct` counts all pressed physical keys.
    bool Others(uint16_t press, unsigned& distinct) const {
        bool other = false; uint8_t seen[kCtlCount]{}; distinct = 0;
        for (unsigned c = 0; c < kCtlCount; ++c) {
            if (!(press & bag_ & CtlBit(c))) continue;
            const uint8_t k = set_.pool.key[c];
            if (!req_.Has(k)) other = true;
            bool dup = false;
            for (unsigned i = 0; i < distinct; ++i) dup = dup || seen[i] == k;
            if (!dup) seen[distinct++] = k;
        }
        return other;
    }
    bool InGrace() const {return g_ < break_g0_ + kWarnMs + kGraceMs;}
    // A break tick outside a gesture. true = a correct gesture started (the rest of this tick waits for its end).
    bool BreakTick(uint16_t held, uint16_t press, double dt) {
        const double open = break_g0_ + kWarnMs;
#if LINK_SWORD_LOCK
        if (!stale_set_) {stale_set_ = true; stale_ = static_cast<uint16_t>(stale_from_ & held & ~press);}   // held, not pressed anew
        stale_ = static_cast<uint16_t>(stale_ & held);                 // seen released once: fresh from now on
        if (press & stale_) {rec_.ignored = static_cast<uint16_t>(rec_.ignored | (press & stale_)); events_ |= kEvIgnored;}
        held = static_cast<uint16_t>(held & ~stale_); press = static_cast<uint16_t>(press & ~stale_);
        // cand6 (Donald's Guards as Break 1, 3 of 3 `expired got=0x0`: the right click pressed 436 ms into the 500 ms warning
        // and held through the window): a hold starts only on a press inside the window, so a press in the warning, kept
        // down, never started it. Now that press (fresh in this break, never a held-over key) is remembered while it
        // stays down, and the hold starts when the window opens, its length counted from the opening.
        if (set_.tuning.hold_from_warning) {
            const BreakInfo& hb = Info(set_.sequence.q[break_i_]);
            if (hb.gesture == Gesture::Hold) {
                if (g_ < open) warn_down_ = static_cast<uint16_t>(warn_down_ | (press & CtlBit(hb.ctl)));
                warn_down_ = static_cast<uint16_t>(warn_down_ & held);
            } else warn_down_ = 0;
        }
#endif
        if (g_ < open) {if (press & bag_) events_ |= kEvIgnored; return false;}
        if (!window_seen_) {window_seen_ = true; events_ |= kEvWindow;}
#if LINK_SWORD_LOCK
        if (press & leftover_) {   // the previous step's keys: they never count here, right or wrong
            rec_.ignored = static_cast<uint16_t>(rec_.ignored | (press & leftover_));
            press = static_cast<uint16_t>(press & ~leftover_); events_ |= kEvIgnored;
        }
        const bool timed = Timed(set_.sequence.q[break_i_]);
        if (timed && Needle() > set_.tuning.zone1 && !exec_) {Resolve(false, Miss::Late, press); return false;}   // the needle left the zone
        if (set_.tuning.guard_dirs_free && set_.sequence.q[break_i_] == Break::Guard) {   // a direction before the right click
            const uint16_t dirs = static_cast<uint16_t>(press & (CtlBit(kLeft) | CtlBit(kRight)));
            if (dirs) {rec_.ignored = static_cast<uint16_t>(rec_.ignored | dirs); press = static_cast<uint16_t>(press & ~dirs); events_ |= kEvIgnored;}
        }
#endif
        unsigned distinct = 0;
        if (Others(press, distinct)) {
            if (InGrace()) events_ |= kEvIgnored;
            else {Resolve(false, distinct >= 2 ? Miss::Multi : Miss::Wrong, press); return false;}
        } else if (press) {
            const Break b = set_.sequence.q[break_i_];
            const BreakInfo& bi = Info(b);
            bool start = false;
            if (b == Break::Lateral) {
                if (press & CtlBit(kKnife)) {
                    if (held & CtlBit(DirCtl(set_.sequence.side))) start = true;
                    else events_ |= kEvNoCount;
                }
#if LINK_SWORD_LOCK
                // cand3: the chord in the other order (Space already down, pressed in this break, then the direction)
                if (!start && set_.tuning.lateral_chord && (press & CtlBit(DirCtl(set_.sequence.side))) && (held & CtlBit(kKnife))) start = true;
#endif
            } else start = (press & CtlBit(bi.ctl)) != 0;
#if LINK_SWORD_LOCK
            if (start && timed) {
                const double f = Needle();
                if (f < set_.tuning.zone0) {Resolve(false, Miss::Early, press); return false;}   // before the zone: a miss
                exec_frac_ = f;
            }
#endif
#if LINK_SWORD_LOCK
            if (start && b == Break::Lateral && set_.tuning.lateral_chord) {   // the chord complete: a hit now (no tap length)
                got_ = static_cast<uint16_t>(held | press); Resolve(true, Miss::None, got_); return false;
            }
#endif
            if (start) {exec_ = true; exec_g0_ = g_; pending_dt_ = dt; got_ = press; events_ |= kEvGesture; return true;}
        }
#if LINK_SWORD_LOCK
        if (warn_down_ && (held & warn_down_)) {   // cand6: pressed in the warning, still down: the hold runs from the opening
            exec_ = true; exec_g0_ = open; pending_dt_ = dt; got_ = warn_down_; warn_down_ = 0; rec_.from_warning = true;
            events_ |= kEvGesture; return true;
        }
#endif
        if (g_ >= open + window_) Resolve(false, Miss::Expired, press);
        return false;
    }
    // A tick while a correct gesture is completed: duel time and the bar wait.
    void Executing(uint16_t held, uint16_t press) {
        const Break b = set_.sequence.q[break_i_];
        const BreakInfo& bi = Info(b);
        unsigned distinct = 0;
#if LINK_SWORD_LOCK
        if (press & leftover_) {rec_.ignored = static_cast<uint16_t>(rec_.ignored | (press & leftover_)); press = static_cast<uint16_t>(press & ~leftover_); events_ |= kEvIgnored;}
        if (set_.tuning.guard_dirs_free && b == Break::Guard) press = static_cast<uint16_t>(press & ~(CtlBit(kLeft) | CtlBit(kRight)));
#endif
        if (Others(press, distinct)) {
            if (!InGrace()) {exec_ = false; Resolve(false, distinct >= 2 ? Miss::Multi : Miss::Wrong, press); Tail(pending_dt_); return;}
            events_ |= kEvIgnored;
        }
        const double run = g_ - exec_g0_;
        const uint8_t ctl = b == Break::Lateral ? kKnife : bi.ctl;
        const bool down = (held & CtlBit(ctl)) != 0;
        bool done = false, abort = false;
        if (bi.gesture == Gesture::Hold) {
            if (run >= bi.hold_ms) done = true;   // held this long (a release seen on this tick still counts)
            else if (!down) abort = true;
        } else if (!down) {
            if (run <= kTapMs) done = true; else abort = true;
        } else if (run > kTapMs) abort = true;
        if (done) {exec_ = false; Resolve(true, Miss::None, got_); Tail(pending_dt_); return;}
        if (abort) {
            exec_ = false; events_ |= kEvNoCount;
            if (g_ >= break_g0_ + kWarnMs + window_) Resolve(false, Miss::Released, press);
            Tail(pending_dt_);
        }
    }
    void Resolve(bool ok, Miss why, uint16_t got) {
        rec_.valid = true; rec_.index = break_i_; rec_.id = set_.sequence.q[break_i_]; rec_.side = set_.sequence.side;
        rec_.ok = ok; rec_.why = why; rec_.got = got; rec_.ms = g_ - (break_g0_ + kWarnMs);
#if LINK_SWORD_LOCK
        const bool timed = Timed(rec_.id);
        rec_.needle = timed ? (exec_frac_ >= 0 && ok ? exec_frac_ : Needle()) : -1;
        rec_.great = ok && timed && exec_frac_ >= set_.tuning.zone0 && exec_frac_ <= set_.tuning.zone0 + set_.tuning.great;
        const double bonus = set_.tuning.bonus + (rec_.great ? set_.tuning.great_bonus : 0.0);
        exec_frac_ = -1;
        if (ok) {
            ++hits_; pv_ = pv_ + kSurge + bonus < 1.0 ? pv_ + kSurge + bonus : 1.0; events_ |= kEvHit;
        } else {
#else
        if (ok) {
            ++hits_; pv_ = pv_ + kSurge + set_.tuning.bonus < 1.0 ? pv_ + kSurge + set_.tuning.bonus : 1.0; events_ |= kEvHit;
        } else {
#endif
            ++misses_; pv_ = pv_ - set_.tuning.fail > -1.0 ? pv_ - set_.tuning.fail : -1.0;
            lock_until_ = d_ + set_.tuning.lock_ms; events_ |= kEvMiss;
        }
#if LINK_SWORD_LOCK
        if (set_.tuning.overtime && round_ >= kSuddenDeathRound) {   // candidate 10: sudden death, this break decides
            pv_ = ok ? 1.0 : -1.0; pa_ = ok ? kWinPa : kLosePa; band_ = ok ? Band::C : Band::B;
            End(ok ? Result::Win : Result::Lose);
            return;
        }
#endif
        Next();
    }
    // Clip band, PushAmount and the end checks (the simulator's order: win, lose, cap).
    void Tail(double dt) {
        band_ = pv_ > kBand ? Band::C : pv_ < -kBand ? Band::B : Band::A;
        if (band_ != clip_ && d_ >= clip_lock_until_) {
            clip_ = band_; clip_lock_until_ = d_ + kClipLockMs; last_clip_ms_ = d_; ++clip_changes_; events_ |= kEvClip;
        }
        const double s = dt / 1000.0;   // the simulator's dt in seconds
        const double alpha = 1.0 - std::pow(0.9, s * 30), maxstep = 2.0 * s * 30;
        double step = alpha * (pv_ * 50.0 - pa_);
        step = step > maxstep ? maxstep : step < -maxstep ? -maxstep : step;
        pa_ += step;
        if (pa_ >= kWinPa && hits_ >= set_.tuning.min_hits) End(Result::Win);
        else if (pa_ <= kLosePa) End(Result::Lose);
#if LINK_SWORD_LOCK
        else if (set_.tuning.overtime) {if (g_ >= kOvertimeSafetyMs) End(Result::Draw, Stop::Stall);}   // candidate 10: no cap
#endif
        else if (d_ >= cap_) End(Result::Draw, Stop::Cap);
        else if (g_ >= cap_ + kStallMs) End(Result::Draw, Stop::Stall);
    }
    uint16_t got_ = 0;
};
}  // namespace link_runtime::sword_lock::duel

#pragma once
#include "sword_lock_build.h"   // LINK_SWORD_LOCK (the feature) / LINK_SWORD_LOCK_LAB (the lab tools) / _GRADUATED
// Master Sword Blade Lock, milestone H1c: the minimal playable lab duel (docs/idea-ambiciosa-2026-11-03.md, "Plan de
// H1c y camino al 100% (ronda 5)"). Lab DLL only: [Combat] SwordLock=1 and SwordLockLab=1 (both missing = 0). Never in a
// layer, the public INI or the default INI (tests/test_cases_swordlock.py and tests/test_package_build.py fail if it is).
//
// This header is the pure part. No hooks, no process access, no native calls: link_runtime.cpp reads the game, writes
// the masked pad words, calls cActionButton::set and drives the H0 machine of sword_lock_probe.h (the request queued by
// the tier-2 chainsaw block, the second WorldFreeze, the dispatch lease from the knife hold, the flash exit 0x17 and the
// same-frame abandons) with Config::external_release, so the duel below decides the release instead of H0's fixed 2 s.
// It consumes the H1b duel (sword_lock_duel.h: Duel, Setup, Input, Sequence) and the H1a table (sword_lock_glyphs.h:
// native bits and btns) without editing them.
//
// What it decides:
//  * LabSetup: the duel the plan fixes for the lab. Sequence breaks=1, q[0] = Twist (Giro de hoja, a tap of E: KEY_A),
//    Push 1 and the Finish the knife mash (Space: KEY_KAMAE_KNIFE and KEY_B are one physical key, never a miss). The
//    tuning is the difficulty's (pG+0x4F98: decay, bonus, fail, lock, window) with the two fixes of round 5:
//    min_hits = min(min_hits, breaks) = 1 and ceiling0 = 1.0 - 0.30 * breaks = 0.70 (TuningFor gives Normal/Pro 2/3 hits
//    and 0.40/0.10 for two or three breaks: with one break the lab would never win, sword_lock_duel.h:243-247 and :961).
//    SwordLockWindowScale is 1.0, no SwordLockHold, no SwordLockSeed/Pool/Mix (H1d): set to anything else they are
//    ignored and counted (Config::ignored, logged). SwordLockCapMs keeps its meaning (0 = 5500 ms).
//  * [Combat] SwordLockMashGain (lab DLL only, 3-nov): PV per counted knife flank, 0.05..0.30, missing = 0.15 (TP's 0.1
//    assumed 8 pps; Donald mashes Space at ~5 pps and every duel ended a draw at PA 34-45). With 0.15 the simulator
//    (scripts/sword_lock_balance_sim.py lab) gives, Easy and Normal at 60/165 FPS: 5 pps + the hit wins in 4.0-5.2 s,
//    4 pps + the hit a draw or a late win, mashing alone a draw at PA <= 35 (the Finish ceiling 0.70 without the hit),
//    no input a loss. Read apart from Config (ReadMashGain) and carried by sd::Duel::Setup::mash_gain. All of it is
//    compiled only with LINK_SWORD_LOCK_LAB=1: the public DLL carries Config and Lab, and must stay byte-identical (even
//    an unused inline function here changes its code), so the 4-argument LabSetup and Lab::Start stay TP's 0.1.
//  * The crowd rule (round 5, "Arreglos que vienen de H0" 1): no duel when another hostile live Ganado is within
//    SwordLockCrowdMm of Link (Config's 8000 below; the lab DLL applies 1500 when the key is missing or invalid,
//    sword_lock_winspin.h LabCrowdMm), or a cEmWep in flight within 10 m: refused reason=crowd and the parry stays as in
//    3.5.0 (Refuse::Crowd of the probe; H0's own admission never looked at the crowd). It fails closed: Link or the
//    enemy manager unread, or an em10 whose facts could not be read standing within the distance, is a crowd too.
//  * The input: Key_btn_on/Key_btn_trg read right after PadRead (case_key_debounce.h's hook, the duel's slot first),
//    the trg accumulated between two duel ticks; while the duel holds the world every gameplay bit of the low dword is
//    masked (pause and menus pass: KEY_ST, KEY_OPTION, KEY_SSCRN, KEY_MAP), so parry_input_runtime, zelda_light,
//    ztarget_native and zelda_cast_pose_runtime read the masked words; at the end each bit still held stays masked
//    until it is seen released once (no stuck key, no phantom press, no parry re-armed by the held Space). On a frame
//    PadRead built from the MENUS binding set (0x806F39, STA_SUB_SCRN, STA_TITLE, card: Enter = KEY_OK|KEY_B, Esc =
//    KEY_CANCEL|KEY_EV_CANCEL) nothing is masked or fed to the duel, so a menu opened mid-duel stays usable; the duel
//    pauses there (and on a sub-screen request) and, past 1 s of game time paused, ends Abandoned. A frame whose keys
//    did not reach Key_btn (input suspended, SPF_KEY) never counts as a release.
//  * One native offer at a time (cActionButton::set at the arbiter entry, func NULL, slot 0, ENFORCE_EXEC 2, only with
//    the native queue empty): the mash kind 0x46 with btn 5 DISP_B (one offer per press: a frame without it after each
//    counted press rebuilds the icon), the break kind 0x48 with btn 1 DISP_A_NORMAL; once the break resolves its icon
//    keeps 250 ms with the colour flag 0x100 (hit) or 0x80 (miss). Never btn 0xD, 2 or 4, never kinds 0x44/0x45 (the
//    Mortal Draw prompt's). Kinds 0x46/0x48 fall on the emptied messages 0x5C/0x5E: icon only.
//  * The exit in two times: win = the pending flash 0x17 (H0's release: publisher, freeze End, lease back), no slide (the
//    E KICK the flash offers keeps its reach; with [Combat] SwordLockWinSpin the world stays held for the Great Spin that
//    pushes the Ganados in reach, sword_lock_winspin.h); lose = the lease back first, Link slides 1300 in 20 frames with the world
//    still frozen (parry_pushback.h advances only inside Link's own MotionMove), then End; draw = the same with 600.
//    In the three, the parry's Follow is armed again on the blocked swing (fn and r0/r1 saved at the block, now = the
//    release), so the same chainsaw swing resuming at point blank is cancelled for up to 1500 ms; the same on every
//    same-frame abandon whose world goes on (all but a load, an unreadable frame or Link's death: RearmOnAbandon), the
//    ones without the exit blow (QTE, cutscene, freeze lost) included. A block whose owner routine was not read opens
//    no duel (Refuse::NoSwing): there would be nothing to arm again.
//  * The after watch: 1000 ms of game time from the End (chainsaw contacts that landed, Link's HP drops and reactions,
//    the SHAKE OFF offer, native offers seen: the E KICK after a win).
// Time: game time (game_clock 60 Hz frames, Ms = frames / 0.06), the same at 60 and 165 FPS; the duel clamps each dt to
// 66.7 ms. Tests: tests/native/test_sword_lock_lab.cpp (scripts/test_sword_lock_lab.bat).
#include "sword_lock_duel.h"
#include "sword_lock_glyphs.h"
#include "sword_lock_probe.h"
#include "case_key_debounce.h"
#include "game_clock.h"
#include <cmath>
#include <cstdint>
#include <cwchar>

namespace link_runtime::sword_lock::lab {
namespace sd = link_runtime::sword_lock::duel;
namespace sg = link_runtime::sword_lock::glyphs;
namespace slp = link_runtime::sword_lock::probe;
namespace ckd = link_runtime::case_key_debounce;

inline constexpr double Frames(double ms) {return ms * 0.06;}
inline constexpr double Ms(double frames) {return frames / 0.06;}

// ------------------------------------------------------------------ [Combat] keys of the lab
inline constexpr double kDefaultCooldownMs = 15000, kMaxCooldownMs = 120000;
inline constexpr double kDefaultCrowdMm = 8000, kMinCrowdMm = 1000, kMaxCrowdMm = 20000;
inline constexpr double kWeaponMm = 10000;   // a cEmWep in flight closer than this refuses the duel
struct Config {
    bool master = false;       // SwordLock: missing or 0 = 3.5.0 (no hook, no write)
    bool lab = false;          // SwordLockLab: the H1c duel (lab only)
    bool log = false;          // SwordLockLog: the input and bar lines (pv/pa/ceiling every 250 ms)
    double cooldown_ms = kDefaultCooldownMs;   // SwordLockCooldownMs: one duel per owner every this long
    double crowd_mm = kDefaultCrowdMm;         // SwordLockCrowdMm (lab only): the crowd distance
    sd::Config duel;           // SwordLockCapMs; window 1.0, no hold, no seed, the whole pool (H1d keys)
    unsigned ignored = 0;      // H1d keys set to something else than their H1c value (logged, never applied)
    bool On() const {return master && lab;}
};
namespace detail {
inline bool Switch(const wchar_t* v, bool& out) {   // "" or "0" = off, "1" = on, anything else = invalid
    if (!v[0] || (v[0] == L'0' && !v[1])) {out = false; return true;}
    if (v[0] == L'1' && !v[1]) {out = true; return true;}
    out = false; return false;
}
inline bool Number(const wchar_t* v, double lo, double hi, double& out) {
    if (!v || !*v) return false;
    double n = 0;
    for (const wchar_t* p = v; *p; ++p) {
        if (*p < L'0' || *p > L'9') return false;
        n = n * 10 + double(*p - L'0');
        if (n > hi) return false;
    }
    if (n < lo) return false;
    out = n; return true;
}
}  // namespace detail
// get(key, out, count) fills `out` with the raw value ("" when missing). A bad switch leaves that switch off (a typo
// never runs a half-configured lab) and counts a problem; a bad number keeps its default and counts a problem.
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[128]{};
    get(L"SwordLock", v, 128);
    if (!detail::Switch(v, c.master)) ++problems;
#if LINK_SWORD_LOCK_GRADUATED
    c.lab = true;   // 3.6.0: the duel is a feature behind SwordLock alone (no SwordLockLab key in the DLL)
#else
    v[0] = 0; get(L"SwordLockLab", v, 128);
    if (!detail::Switch(v, c.lab)) ++problems;
#endif
    v[0] = 0; get(L"SwordLockLog", v, 128);
    if (!detail::Switch(v, c.log)) ++problems;
    v[0] = 0; get(L"SwordLockCooldownMs", v, 128);
    if (v[0] && !detail::Number(v, 0, kMaxCooldownMs, c.cooldown_ms)) {++problems; c.cooldown_ms = kDefaultCooldownMs;}
    v[0] = 0; get(L"SwordLockCrowdMm", v, 128);
    if (v[0] && !detail::Number(v, kMinCrowdMm, kMaxCrowdMm, c.crowd_mm)) {++problems; c.crowd_mm = kDefaultCrowdMm;}
    c.duel = sd::ReadConfig(get, problems);   // the H1b keys, parsed with their own rules
    if (c.duel.window_scale != 1.0) {++c.ignored; c.duel.window_scale = 1.0;}
    if (c.duel.hold) {++c.ignored; c.duel.hold = false;}
    if (c.duel.seed) {++c.ignored; c.duel.seed = 0;}
    if (c.duel.pool != sd::kAllBreaks) {++c.ignored; c.duel.pool = sd::kAllBreaks;}
    v[0] = 0; get(L"SwordLockMix", v, 128);
    if (v[0]) ++c.ignored;
    return c;
}
// The PadRead hook (case_key_debounce.h) is installed for CaseKeyDebounce=1 or SwordLock=1; the TAB debounce runs only
// with CaseKeyDebounce=1; the duel's slot is filled only for the lab duel (SwordLock=1 and SwordLockLab=1).
struct HookPlan {bool install = false, debounce = false, duel = false;};
inline HookPlan PadHook(bool case_key_debounce, const Config& c) {return HookPlan{case_key_debounce || c.master, case_key_debounce, c.On()};}

// ------------------------------------------------------------------ the lab duel
inline constexpr uint8_t kSpaceKey = 0x20, kEKey = 0x45;   // the lab pool's two physical keys (opaque ids for the duel)
inline sd::Tuning LabTuning(sd::Difficulty d) {
    sd::Tuning t = sd::TuningFor(d);
    t.breaks = 1;
    if (t.min_hits > t.breaks) t.min_hits = t.breaks;
    t.ceiling0 = 1.0 - sd::kCeilingStep * t.breaks;
    return t;
}
inline sd::Sequence LabSequence() {
    sd::Sequence s;
    s.breaks = 1; s.q[0] = sd::Break::Twist; s.side = sd::kSideNone; s.alt_first = sd::kKnife; s.push2 = sd::Mash::Action;
    return s;
}
inline sd::Pool LabPool() {
    sd::Pool p;
    p.key[sd::kKnife] = kSpaceKey; p.key[sd::kAction] = kEKey;
    p.breaks = sd::BreakBit(sd::Break::Twist); p.mashes = sd::MashBit(sd::Mash::Knife);
    return p;
}
inline sd::Duel::Setup LabSetup(sd::Difficulty d, const sd::Config& c) {
    sd::Config fixed; fixed.cap_ms = c.cap_ms;   // everything else at its H1c value
    sd::Duel::Setup u = sd::Duel::For(d, fixed, LabSequence(), LabPool());
    u.tuning = LabTuning(d);
    return u;
}
#if LINK_SWORD_LOCK
// SwordLockMashGain (lab DLL only): PV per counted mash flank. Missing = kDefaultMashGain; a value that is not a plain
// decimal in kMinMashGain..kMaxMashGain ("0.15", ".2", "0.300") counts a problem and keeps the default.
inline constexpr double kDefaultMashGain = 0.15, kMinMashGain = 0.05, kMaxMashGain = 0.30;
namespace detail {
inline bool Decimal(const wchar_t* v, double lo, double hi, double& out) {
    if (!v || !*v) return false;
    double whole = 0, frac = 0, scale = 1; bool dot = false; unsigned digits = 0, decimals = 0;
    for (const wchar_t* p = v; *p; ++p) {
        if (*p == L'.' && !dot) {dot = true; continue;}
        if (*p < L'0' || *p > L'9') return false;
        ++digits;
        if (dot) {if (++decimals > 6) return false; frac = frac * 10 + double(*p - L'0'); scale *= 10;}
        else {whole = whole * 10 + double(*p - L'0'); if (whole > hi) return false;}
    }
    if (!digits) return false;
    const double n = whole + frac / scale;
    if (n < lo || n > hi) return false;
    out = n; return true;
}
}  // namespace detail
template <class Get> double ReadMashGain(Get&& get, unsigned& problems) {
    wchar_t v[128]{};
    get(L"SwordLockMashGain", v, 128);
    double g = kDefaultMashGain;
    if (v[0] && !detail::Decimal(v, kMinMashGain, kMaxMashGain, g)) {++problems; g = kDefaultMashGain;}
    return g;
}
// The lab setup with SwordLockMashGain as read (the 2-argument LabSetup keeps TP's 0.1).
inline sd::Duel::Setup LabSetup(sd::Difficulty d, const sd::Config& c, double mash_gain) {
    sd::Duel::Setup u = LabSetup(d, c);
    u.mash_gain = mash_gain;
    return u;
}
#endif

// ------------------------------------------------------------------ pad words
namespace bits {
constexpr uint32_t kKnife = uint32_t(sg::key::kKamaeKnife | sg::key::kB);   // Space on the keyboard (both, one key)
constexpr uint32_t kAction = uint32_t(sg::key::kA);                         // E (btn 1 tests KEY_A)
constexpr uint32_t kKeep = uint32_t(sg::key::kSt | sg::key::kOption | sg::key::kSscrn | sg::key::kMap);   // pause, menus
constexpr uint32_t kMask = ~kKeep;                                          // every other gameplay bit of the low dword
}  // namespace bits
inline uint16_t Controls(uint32_t keys) {
    uint16_t c = 0;
    if (keys & bits::kKnife) c |= sd::CtlBit(sd::kKnife);
    if (keys & bits::kAction) c |= sd::CtlBit(sd::kAction);
    return c;
}
#if LINK_SWORD_LOCK
// H1d (sword_lock_draw.h, lab DLL only): every control of the keyboard bag, KEY_RIGHT (D), KEY_LEFT (A) and KEY_KAMAE (the
// right mouse button) besides the knife and the action. Controls() above stays H1c's two (its fixed sequence reads no more).
namespace bits {
constexpr uint32_t kRight = uint32_t(sg::key::kRight), kLeft = uint32_t(sg::key::kLeft), kAim = uint32_t(sg::key::kKamae);
}  // namespace bits
inline uint16_t ControlsAll(uint32_t keys) {
    uint16_t c = Controls(keys);
    if (keys & bits::kRight) c |= sd::CtlBit(sd::kRight);
    if (keys & bits::kLeft) c |= sd::CtlBit(sd::kLeft);
    if (keys & bits::kAim) c |= sd::CtlBit(sd::kAim);
    return c;
}
#endif
// What PadRead built this frame (the same words case_key_debounce reads). menus: the MENUS binding set (the predicate of
// 0x567EFF: byte 0x806F39, STA_SUB_SCRN, STA_TITLE, card), where Enter, Esc and the arrows are KEY_OK|KEY_B,
// KEY_CANCEL|KEY_EV_CANCEL and KEY_L/KEY_R. live: the real keys reached Key_btn (pG readable, input not suspended, no
// SPF_KEY KeyClear). sscrn_request: Status_flg[2] kSscrnRequest (inventory, map or shop about to open).
struct PadContext {bool readable = false, menus = false, live = false, sscrn_request = false;};
inline constexpr PadContext kGameplay{true, false, true, false};
inline PadContext ContextOf(const ckd::Context& c) {
    PadContext p; p.readable = true;
    p.menus = c.menu_input || (c.status0 & ckd::layout::kStaSubScrn) || (c.status2 & ckd::layout::kStaTitle) ||
              (c.status3 & ckd::layout::kCardTask);
    p.live = c.global && !c.suspended && !(c.stop & ckd::layout::kSpfKey);
    p.sscrn_request = (c.status2 & slp::native::kSscrnRequest) != 0;
    return p;
}
// Reads only. An unreadable frame gives PadContext{}: masked, never fed, never a release.
template <class Mem> PadContext ReadContext(Mem& m, uint32_t base) {
    ckd::Context c; uint32_t global = 0;
    if (!m.Read(base + ckd::rva::kMenuInput, &c.menu_input, 1) || !m.Read(base + ckd::rva::kCaseScreen, &c.case_screen, 1) ||
        !m.Read(base + ckd::rva::kInputSuspended, &c.suspended, 1) || !m.Read(base + ckd::rva::kGlobalSlot, &global, 4))
        return PadContext{};
    if (global) {
        if (!m.Read(global + ckd::layout::kStopFlg, &c.stop, 4) || !m.Read(global + ckd::layout::kStatus0, &c.status0, 4) ||
            !m.Read(global + ckd::layout::kStatus2, &c.status2, 4) || !m.Read(global + ckd::layout::kStatus3, &c.status3, 4))
            return PadContext{};
        c.global = true;
    }
    return ContextOf(c);
}
// The duel pauses (sd::Input::paused) on a menu or a sub-screen request, and on a frame it cannot read.
inline bool Paused(const PadContext& p) {return !p.readable || p.menus || p.sscrn_request;}
// What the game must see after PadRead. write: on/trg differ from the real words (only clears, never sets a bit).
struct PadOut {bool write = false; uint32_t on = 0, trg = 0; bool released = false; uint32_t held = 0;};
class Mask {
public:
    void Begin() {on_ = true; hold_ = 0; frames_ = 0;}
    // The world runs again: every gameplay bit held now stays masked until it is seen released once.
    void End(uint32_t real_on) {on_ = false; hold_ = real_on & bits::kMask;}
    void Reset() {on_ = false; hold_ = 0; frames_ = 0;}
    // ctx: the frame's binding set. The MENUS set passes untouched (the menu keys share bits with the gameplay ones);
    // a frame whose keys did not reach Key_btn masks but never releases a held bit.
    PadOut Apply(uint32_t on, uint32_t trg, const PadContext& ctx = kGameplay) {
        PadOut o; o.on = on; o.trg = trg;
        if (ctx.menus) {o.held = hold_; return o;}
        if (on_) {
            o.on = on & ~bits::kMask; o.trg = trg & ~bits::kMask; ++frames_;
        } else if (hold_) {
            if (ctx.live) hold_ &= on;   // a bit seen released leaves the hold for good
            o.on = on & ~hold_; o.trg = trg & ~hold_;
            o.released = hold_ == 0;
        }
        o.held = hold_; o.write = o.on != on || o.trg != trg;
        return o;
    }
    bool Active() const {return on_ || hold_ != 0;}
    bool Duel() const {return on_;}
    uint32_t held() const {return hold_;}
    unsigned frames() const {return frames_;}
private:
    bool on_ = false; uint32_t hold_ = 0; unsigned frames_ = 0;
};
// The real words of every PadRead since the last duel tick: the last held state and the OR of the presses.
class PadWords {
public:
#if LINK_SWORD_LOCK
    void Reset(uint32_t on) {on_ = on; trg_ = 0; all_ = false;}
    void Feed(uint32_t on, uint32_t trg) {on_ = on; trg_ |= trg;}
    void All(bool on) {all_ = on;}   // H1d: the whole keyboard bag (ControlsAll), until the next Reset
    bool all() const {return all_;}
    sd::Input Take(double dt_ms, bool paused) {
        sd::Input in; in.held = all_ ? ControlsAll(on_) : Controls(on_); in.pressed = all_ ? ControlsAll(trg_) : Controls(trg_);
        in.dt_ms = dt_ms; in.paused = paused;
        trg_ = 0;
        return in;
    }
#else
    void Reset(uint32_t on) {on_ = on; trg_ = 0;}
    void Feed(uint32_t on, uint32_t trg) {on_ = on; trg_ |= trg;}
    sd::Input Take(double dt_ms, bool paused) {
        sd::Input in; in.held = Controls(on_); in.pressed = Controls(trg_); in.dt_ms = dt_ms; in.paused = paused;
        trg_ = 0;
        return in;
    }
#endif
    uint32_t on() const {return on_;}
    uint32_t pending() const {return trg_;}
private:
    uint32_t on_ = 0, trg_ = 0;
#if LINK_SWORD_LOCK
    bool all_ = false;
#endif
};

// ------------------------------------------------------------------ offers
inline constexpr uint8_t kKindPush = 0x46, kKindBreak = 0x48;   // messages 0x5C / 0x5E (emptied: icon only)
inline constexpr uint32_t kOfferFlags = 2, kColorHit = 0x100, kColorMiss = 0x80;   // ENFORCE_EXEC, the colour flags
inline constexpr uint8_t kBtnPush = sg::btn::kB, kBtnBreak = sg::btn::kANormal;  // DISP_B 07/07, DISP_A_NORMAL 00/20
inline constexpr double kFeedbackMs = 250;
static_assert(kBtnPush == 5 && kBtnBreak == 1, "btn 5 mash, btn 1 break");
struct Offer {
    bool show = false;
    uint8_t kind = 0, btn = 0;
    uint32_t flags = 0;
    sd::Glyph glyph = sd::Glyph::None;
    bool gap = false;        // a mash frame left without the offer after a counted press (the icon rebuilds)
    bool feedback = false;   // the resolved break's icon with its colour
};
inline bool SameOffer(const Offer& a, const Offer& b) {
    return a.show == b.show && a.kind == b.kind && a.btn == b.btn && a.flags == b.flags;
}
// The native offer for the duel's prompt: the knife mash (any knife glyph) or the action tap; nothing else in H1c.
inline Offer OfferFor(const sd::Prompt& p) {
    Offer o;
    if (!p.show) return o;
    if (p.glyph == sd::Glyph::KnifeRapid || p.glyph == sd::Glyph::Knife) {o.kind = kKindPush; o.btn = kBtnPush;}
    else if (p.glyph == sd::Glyph::ActionNormal) {o.kind = kKindBreak; o.btn = kBtnBreak;}
    else return o;
    o.show = true; o.flags = kOfferFlags; o.glyph = p.glyph;
    return o;
}
inline const char* Want(const sd::Prompt& p) {
    if (!p.show) return "none";
    if (p.ctl == sd::kKnife) return "knife";
    if (p.ctl == sd::kAction) return "action";
    return "other";
}
class Offers {
public:
    void Reset() {pulse_ = false; until_ = -1; ok_ = false; pulses_ = gaps_ = 0;}
    // After each duel tick with time: its events and the break record.
    void Events(uint32_t ev, const sd::BreakRecord& rec, double game_ms) {
        if (ev & (sd::kEvFlank | sd::kEvBeat)) {pulse_ = true; ++pulses_;}
        if (ev & (sd::kEvHit | sd::kEvMiss)) {until_ = game_ms + kFeedbackMs; ok_ = rec.ok;}
    }
    // One arbiter frame. running: the duel holds the world and has not ended.
    Offer Next(const sd::Prompt& p, bool running, double game_ms) {
        Offer o;
        if (!running) {pulse_ = false; until_ = -1; return o;}
        if (until_ >= 0 && game_ms < until_) {
            o.show = true; o.kind = kKindBreak; o.btn = kBtnBreak; o.glyph = sd::Glyph::ActionNormal; o.feedback = true;
            o.flags = kOfferFlags | (ok_ ? kColorHit : kColorMiss);
            pulse_ = false;
            return o;
        }
        o = OfferFor(p);
        if (o.show && o.kind == kKindPush && pulse_) {pulse_ = false; o.show = false; o.gap = true; ++gaps_;}
        return o;
    }
    unsigned pulses() const {return pulses_;}
    unsigned gaps() const {return gaps_;}
private:
    bool pulse_ = false, ok_ = false; double until_ = -1; unsigned pulses_ = 0, gaps_ = 0;
};

// ------------------------------------------------------------------ the crowd rule
// One actor of the enemy manager as the runtime reads it: a live hostile Ganado (H0's SwlOthers rule: em10 vtable,
// Ganado family, HP > 0, status 0x20, be_flag 1) or a cEmWep in flight (Throw, Shot, ThrowScythe). XZ distances.
// unread: an em10 whose Ganado facts could not be read but whose position could (counted within crowd_mm: fail closed).
struct Actor {bool ganado = false, weapon = false, unread = false; uint32_t ptr = 0; float pos[3]{};};
struct Crowd {
    unsigned ganados = 0;    // within crowd_mm
    unsigned between = 0;    // within 3..crowd_mm (log only: the openings the 8 m rule costs against a 3 m one)
    unsigned weapons = 0;    // in flight within kWeaponMm
    unsigned unread = 0;     // em10 actors not read, within crowd_mm
    double near_mm = -1;     // the nearest other live Ganado (-1: none)
    bool blind = false;      // Link's position or the enemy manager not read: refused
    bool refused = false;
};
inline Crowd BlindCrowd() {Crowd c; c.blind = c.refused = true; return c;}
inline Crowd JudgeCrowd(const Actor* a, unsigned n, uint32_t owner, const float link[3], double crowd_mm, double weapon_mm = kWeaponMm) {
    Crowd c;
    for (unsigned i = 0; a && i < n; ++i) {
        if (a[i].ptr && a[i].ptr == owner) continue;
        const double dx = double(a[i].pos[0]) - link[0], dz = double(a[i].pos[2]) - link[2];
        const double d = std::sqrt(dx * dx + dz * dz);
        if (!std::isfinite(d)) continue;
        if (a[i].ganado) {
            if (c.near_mm < 0 || d < c.near_mm) c.near_mm = d;
            if (d < crowd_mm) {++c.ganados; if (d >= 3000) ++c.between;}
        } else if (a[i].unread) {
            if (d < crowd_mm) ++c.unread;
        } else if (a[i].weapon && d < weapon_mm) ++c.weapons;
    }
    c.refused = c.ganados > 0 || c.weapons > 0 || c.unread > 0;
    return c;
}

// ------------------------------------------------------------------ the Follow at the block
// parry_general_block.h's Follow right after the press block that queued the duel (fn, owner, sample, r0/r1).
struct SavedSwing {bool valid = false, known = false; uint32_t fn = 0, owner = 0; float pos[3]{}; uint8_t routine[2]{};};
template <class FollowT> SavedSwing Save(const FollowT& f) {
    SavedSwing s;
    s.valid = f.armed && f.function && f.owner; s.fn = f.function; s.owner = f.owner;
    for (unsigned i = 0; i < 3; ++i) s.pos[i] = f.pos[i];
    s.known = f.swing.known; s.routine[0] = f.swing.routine[0]; s.routine[1] = f.swing.routine[1];
    return s;
}
// At the release: the same swing armed again in a Grace slot (Grace::ArmSwing, now = the release), so SameSwing cancels
// only that swing while the owner stays in its r0/r1 (Follow::kSwingMaxMs = 1500 ms, kSwingGapMs = 700 ms between
// contacts). False: nothing to arm (no owner or routine known at the block).
template <class GraceT, class SwingT> bool Rearm(GraceT& grace, const SavedSwing& s, uint64_t now_ms) {
    if (!s.valid || !s.known) return false;
    SwingT sw; sw.known = true; sw.routine[0] = s.routine[0]; sw.routine[1] = s.routine[1];
    grace.ArmSwing(s.fn, s.owner, s.pos, now_ms, sw);
    return true;
}
// The same-frame abandons of the probe after which the Follow is armed again on the same swing: every one whose world
// goes on, with or without the exit blow (QTE, cutscene and freeze lost have none); never after a load, an unreadable
// frame or Link's death (no swing to resume against him). The Follow cancels only that swing (owner, fn, r0/r1).
inline bool RearmOnAbandon(slp::Abandon a) {
    return a != slp::Abandon::None && a != slp::Abandon::Load && a != slp::Abandon::Unreadable && a != slp::Abandon::LinkDead;
}

// ------------------------------------------------------------------ the exit
enum class Release : uint8_t {None, Flash, Push};
inline const char* ReleaseName(Release r) {return r == Release::Flash ? "flash" : r == Release::Push ? "push" : "none";}
inline constexpr float kLosePush = 1300.f, kDrawPush = 600.f, kPushFrames = 20.f;
inline constexpr double kSlideMaxMs = 1000;   // the End at the latest after a push release (game time)
inline constexpr double kAfterMs = 1000;      // the after watch
inline constexpr double kBarMs = 250;         // SwordLockLog: pv/pa/ceiling every this long of duel time
struct ExitPlan {Release kind = Release::None; float distance = 0, frames = kPushFrames;};
inline ExitPlan PlanFor(sd::Result r) {
    ExitPlan p;
    switch (r) {
    case sd::Result::Win: case sd::Result::Abandoned: p.kind = Release::Flash; break;   // the world goes on: never frozen mid-swing
    case sd::Result::Lose: p.kind = Release::Push; p.distance = kLosePush; break;
    case sd::Result::Draw: p.kind = Release::Push; p.distance = kDrawPush; break;
    default: break;
    }
    return p;
}

// ------------------------------------------------------------------ after the release
// One game tick of the after watch, read by the runtime.
struct AfterFacts {
    bool readable = false;
    int link_hp = 0;
    uint8_t link_r0 = 0;     // Link's routine byte: 0 free, 5 the dispatch handing Link back; any other (a hit, a grab)
                             // counts once per entry
    unsigned hits = 0;       // chainsaw contacts of the owner that landed since the last tick (SwordLockGuard's pass)
};
struct AfterRecord {
    unsigned hits = 0, life_down = 0, grab = 0, shake = 0, offers = 0, kinds_n = 0;
    unsigned kick = 0;       // arbiter frames with the KICK offer (kind 0x07 with btn 1 DISP_A_NORMAL: "E KICK")
    unsigned action = 0;     // arbiter frames with another btn 1 offer (not SHAKE OFF): a kick of another kind?
    int hp_drop = 0;
    uint8_t kinds[6]{}, btns[6]{};   // distinct (kind, btn) native offers seen at the arbiter (the E KICK after a win)
    double ms = 0;
};
inline constexpr uint8_t kShakeOffKind = 0x2B;   // the grab's SHAKE OFF offer (0x280D9E, sword_lock_glyphs.h)
// The KICK prompt: kind k shows message 0x16 + k of core.udas MDT #6, "{00}KICK {08}" for kind 7 (scripts/text_rename.py).
inline constexpr uint8_t kKickKind = 0x07;
inline constexpr uint8_t kHandBackR0 = 5;        // EventNormal: the dispatch lease (ours) on its way out

enum class Stage : uint8_t {Off, Duel, Waiting, Sliding, After, Done};
inline const char* StageName(Stage s) {
    switch (s) {
    case Stage::Duel: return "duel"; case Stage::Waiting: return "waiting"; case Stage::Sliding: return "sliding";
    case Stage::After: return "after"; case Stage::Done: return "done"; default: return "off";
    }
}
// The lab machine on the game thread. The runtime calls, in this order of the duel's life: Start (the freeze Begin
// frame) -> Pad (every PadRead) and Tick (the probe's ticks while it holds) -> on WantsRelease: the probe's
// RequestRelease -> Release (the probe's release frame, the decide) -> SlideTick (Push) -> Ended (the freeze End) ->
// AfterTick until it returns true. Abandon at any point before Ended is the same-frame end (the runtime runs H0's
// abandon: exit blow when the world goes on, freeze End, lease back) and goes straight to Ended.
class Lab {
public:
    void Start(const Config& c, sd::Difficulty d, uint32_t real_on, double now) {
        cfg_ = c; stage_ = Stage::Duel; diff_ = d;
        duel_.Start(LabSetup(d, c.duel), Controls(real_on));
        mask_.Begin(); pad_.Reset(real_on); offers_.Reset(); in_ = sd::Input{};
        t0_ = last_ = now; next_bar_ = kBarMs; plan_ = ExitPlan{}; reason_ = "-"; abandoned_ = false;
        after_ = AfterRecord{}; after_t0_ = -1; last_hp_ = 0; last_busy_ = false; hp_known_ = false; slide_t0_ = -1;
        step_key_ = 0xFFFFFFFFu; ++duels_;
    }
#if LINK_SWORD_LOCK
    // The lab DLL's start: the same, with SwordLockMashGain as read (ReadMashGain) in the duel's Setup.
    void Start(const Config& c, sd::Difficulty d, uint32_t real_on, double now, double mash_gain) {
        Start(c, d, real_on, now);
        duel_.Start(LabSetup(d, c.duel, mash_gain), Controls(real_on));
    }
    // H1d (sword_lock_draw.h): the drawn duel's own setup (sequence, pool, real tuning, H1d keys); all: the pad words feed
    // the whole keyboard bag (ControlsAll), else H1c's two controls (the fixed-sequence fallback).
    void Start(const Config& c, const sd::Duel::Setup& setup, bool all, uint32_t real_on, double now) {
        Start(c, setup.difficulty, real_on, now);
        pad_.All(all);
        duel_.Start(setup, all ? ControlsAll(real_on) : Controls(real_on));
    }
#endif
    // Every PadRead (the real low dwords): the duel's presses while it runs, the real held state until the End (Ended
    // takes it as the bits to hold), and what the game must see.
    // ctx: the frame's binding set; a MENUS frame, or one whose keys did not reach Key_btn, feeds nothing to the duel.
    PadOut Pad(uint32_t on, uint32_t trg, const PadContext& ctx = kGameplay) {
        if (Holding() && !ctx.menus && ctx.live) pad_.Feed(on, trg);
        return mask_.Apply(on, trg, ctx);
    }
    // One probe tick while the world is held. Returns the duel's events (0 when no game time passed: the second call
    // of a frame keeps the presses for the next one).
    uint32_t Tick(double now, bool paused) {
        if (stage_ != Stage::Duel) return 0;
        const double dt = Ms(now - last_);
        if (!(dt > 0.0)) {if (now < last_) last_ = now; return 0;}
        last_ = now;
        in_ = pad_.Take(dt, paused);
        const uint32_t ev = duel_.Tick(in_);
        offers_.Events(ev, duel_.last_break(), duel_.game_ms());
        if (!duel_.Running()) {
            stage_ = Stage::Waiting; plan_ = PlanFor(duel_.result());
            reason_ = duel_.result() == sd::Result::Abandoned ? sd::StopName(duel_.stop()) : "-";
            abandoned_ = duel_.result() == sd::Result::Abandoned;
        }
        return ev;
    }
    // The runtime ends the duel by itself (the lease never started): the duel's Abandoned, released with the flash.
    void Abort(const char* why) {
        if (stage_ != Stage::Duel) return;
        duel_.Abandon(sd::Stop::Other);
        stage_ = Stage::Waiting; plan_ = PlanFor(sd::Result::Abandoned); reason_ = why ? why : "other"; abandoned_ = true;
    }
    bool WantsRelease() const {return stage_ == Stage::Waiting;}
    // The probe's release frame (it can cancel the lease): Flash -> the runtime fires, ends the freeze, hands Link back
    // and calls Ended; Push -> the lease back and the slide now, Ended when SlideTick says so.
    ExitPlan Release(double now) {
        if (stage_ != Stage::Waiting) return ExitPlan{};
        if (plan_.kind == Release::Push) {stage_ = Stage::Sliding; slide_t0_ = now;}
        return plan_;
    }
    // Each probe tick while sliding: true = End now (the slide is over, stopped, or ran out of time).
    bool SlideTick(bool slide_active, double now) {
        if (stage_ != Stage::Sliding) return false;
        return !slide_active || now < slide_t0_ || game_clock::Reached(Ms(now - slide_t0_), kSlideMaxMs);
    }
    // A same-frame abandon of the probe (or of the runtime) before the End: the duel stops; Ended follows at once.
    void Abandon(const char* why) {
        if (stage_ == Stage::Off || stage_ == Stage::After || stage_ == Stage::Done) return;
        if (duel_.Running()) duel_.Abandon(sd::Stop::Other);
        reason_ = why ? why : "other"; abandoned_ = true; plan_ = ExitPlan{};
    }
    // The freeze End: the world runs again. The held bits stay masked until seen released; the after watch starts.
    void Ended(double now, uint32_t real_on) {
        if (stage_ == Stage::Off || stage_ == Stage::After || stage_ == Stage::Done) return;
        mask_.End(real_on); stage_ = Stage::After; after_t0_ = now; after_ = AfterRecord{}; hp_known_ = false;
    }
    // Native offers queued at the arbiter entry during the after watch: each work's kind and btn.
    void SeenOffers(const uint8_t* kinds, const uint8_t* btns, unsigned n) {
        if (stage_ != Stage::After || !n) return;
        ++after_.offers;
        bool kick = false, action = false;
        for (unsigned i = 0; i < n; ++i) {
            if (kinds[i] == kShakeOffKind) ++after_.shake;
            else if (btns[i] == sg::btn::kANormal && kinds[i] == kKickKind) kick = true;
            else if (btns[i] == sg::btn::kANormal) action = true;
            bool seen = false;
            for (unsigned k = 0; k < after_.kinds_n; ++k) seen = seen || (after_.kinds[k] == kinds[i] && after_.btns[k] == btns[i]);
            if (!seen && after_.kinds_n < 6) {after_.kinds[after_.kinds_n] = kinds[i]; after_.btns[after_.kinds_n++] = btns[i];}
        }
        if (kick) ++after_.kick;
        if (action) ++after_.action;
    }
    // True once, when the watch is over (log the after line).
    bool AfterTick(double now, const AfterFacts& f) {
        if (stage_ != Stage::After) return false;
        after_.hits += f.hits;
        if (f.readable) {
            const bool busy = f.link_r0 != 0 && f.link_r0 != kHandBackR0;
            if (hp_known_ && f.link_hp < last_hp_) {++after_.life_down; after_.hp_drop += last_hp_ - f.link_hp;}
            if (busy && (!hp_known_ || !last_busy_)) ++after_.grab;
            last_hp_ = f.link_hp; last_busy_ = busy; hp_known_ = true;
        }
        after_.ms = now >= after_t0_ ? Ms(now - after_t0_) : kAfterMs;
        if (now < after_t0_ || game_clock::Reached(after_.ms, kAfterMs)) {stage_ = Stage::Done; return true;}
        return false;
    }
    // The prompt's step changed (phase, warning, window open, feedback): one step line.
    bool StepChanged() {
        const sd::Prompt p = duel_.prompt();
        const uint32_t key = uint32_t(p.phase) | uint32_t(p.warning) << 8 | uint32_t(p.show) << 9 | uint32_t(p.ctl) << 16;
        if (key == step_key_) return false;
        step_key_ = key; return true;
    }
    // SwordLockLog: true every kBarMs of duel time.
    bool BarDue() {
        if (!cfg_.log || stage_ != Stage::Duel || duel_.duel_ms() < next_bar_) return false;
        while (next_bar_ <= duel_.duel_ms()) next_bar_ += kBarMs;
        return true;
    }
    Offer NextOffer() {return offers_.Next(duel_.prompt(), stage_ == Stage::Duel && duel_.Running(), duel_.game_ms());}
    bool Holding() const {return stage_ == Stage::Duel || stage_ == Stage::Waiting || stage_ == Stage::Sliding;}
    bool MaskActive() const {return mask_.Active();}
    Stage stage() const {return stage_;}
    const sd::Duel& duel() const {return duel_;}
    const Mask& mask() const {return mask_;}
    const Offers& offers() const {return offers_;}
    const Config& config() const {return cfg_;}
    const AfterRecord& after() const {return after_;}
    const ExitPlan& plan() const {return plan_;}
    sd::Difficulty difficulty() const {return diff_;}
    const char* reason() const {return reason_;}
    bool abandoned() const {return abandoned_;}
    double ElapsedMs(double now) const {return Ms(now - t0_);}
    unsigned duels() const {return duels_;}
    uint32_t pad_on() const {return pad_.on();}
    const sd::Input& last_input() const {return in_;}   // what the last tick with time gave the duel
    void Off() {stage_ = Stage::Off; mask_.Reset();}
private:
    Config cfg_;
    Stage stage_ = Stage::Off;
    sd::Difficulty diff_ = sd::Difficulty::Normal;
    sd::Duel duel_;
    Mask mask_;
    PadWords pad_;
    sd::Input in_;
    Offers offers_;
    ExitPlan plan_;
    AfterRecord after_;
    const char* reason_ = "-";
    bool abandoned_ = false, hp_known_ = false, last_busy_ = false;
    double t0_ = 0, last_ = 0, next_bar_ = kBarMs, after_t0_ = -1, slide_t0_ = -1;
    int last_hp_ = 0;
    uint32_t step_key_ = 0xFFFFFFFFu;
    unsigned duels_ = 0;
};
}  // namespace link_runtime::sword_lock::lab

#pragma once
// Master Sword Blade Lock H1c, the win's Great Spin (Donald, 3-nov: "Quizas al ganar el duelo hacer que link haga un great
// slash que los empuje?"): [Combat] SwordLockWinSpin (lab DLL only: missing = 1 there, the public DLL has no such key)
// and the lab's crowd distance default. link_runtime.cpp includes this header inside #if LINK_SWORD_LOCK_LAB (as
// sword_lock_force.h), so the public build never sees it and stays byte-identical. Pure: no hooks, no process access.
// Tests: tests/native/test_sword_lock_winspin.cpp (scripts/test_sword_lock_winspin.bat).
//
// A win's exit (sword_lock_lab.h "the exit in two times"), lab only, with the key on:
//   1. the probe's release frame: the pending flash 0x17 on the owner through 0x1AEEF0, exactly as before (stunned, the
//      native E KICK), and the hold lease back; the world stays frozen (SPF_EM | SPF_SUBCHAR);
//   2. on the first decide where SwordDecide would start a clip (the dispatch free, Link in the knife hold or a normal
//      idle/move routine, this frame's arbiter sample without native priority), the existing Great Spin request is queued
//      (spin_attack_runtime.h SwordRequest kind Spin, great = 1 whatever Link's HP: the finisher Donald asked for) and
//      SwordDecide starts it in that same decide through its own route (the spin clip's lease, the TP sound or the spin
//      voice, sword in hand, trail, particles);
//   3. its strike (spin clip tick 16) is the normal Great Spin's: GreatSpinRadius, the Scaled knife hit at
//      GreatSpinPermille, GreatSpinSparkles on the ring, the spin bullets and breakables. Only the owner is left out of
//      the hit list (below);
//   4. the world goes on once every Ganado within GreatSpinRadius has its native knife record published (the first
//      `need` entries of the hit list resolved, none deferred), or at once when none is within it. On their first move
//      their own em10DmCk runs that record before any routine (the order H0 proved with the flash: r0=2 on the first
//      move), so a Ganado in reach takes the knife's native reaction (pushed back or down) instead of grabbing Link on
//      the frame the world goes on.
//   The hit list's deadline is WinSpinDeadline (past the hold's cap), so no Ganado in reach loses its record to the
//   normal 24-frame window while frozen; a deadline that passed anyway is released as `expired`, never as `hits`.
//   Never later than kWinSpinHoldMs of game time after the release frame (the probe's watch lasts 1000 ms), and with no
//   spin lease by kWinSpinStartMs the world goes on as before (the flash alone), the reason logged. Link's own safety
//   (load, death, QTE, cutscene, a grab or hit, the freeze lost) ends the hold at once.
//
// Damage while the world waits: the shared adapter holds one Scaled override at a time and a frozen Ganado never consumes
// its record, so each record waits the adapter's 3-frame TTL (mortal_draw_damage.h Adapter::Tick, Expired) before the
// next Ganado is published (about 3 frames each). An expired record stays on its Ganado and lands with the native knife
// amount when the world goes on; the last one published while frozen, and every one published after the release, keep
// the x GreatSpinPermille amount. The reaction is the native knife's either way.
//
// The owner is left out (excluded_owner=1), decided on evidence:
//   * after the flash its damage record stays pending (+0x32C bit 0) until its own em10DmCk runs, which a frozen owner
//     never does: the adapter refuses a pending target (mortal_draw_damage.h NativeHit, Status::PendingNative), the hit
//     list defers it as transient (spin_attack.h HitQueue::Transient) and retries it once the world goes on, when the
//     flash has been consumed: a knife record on the stunned owner then replaces the Dm_Flash stun with the knife's
//     reaction, and the E KICK H0 proved (docs/swordlock-h0-2026-11-03.md, learning 2) is lost;
//   * the other order is worse: the flash is published only on a free record (ocarina_song_effects.h, "only when the
//     record is not pending"), so a knife record first would leave the owner without the stun at all;
//   * the plan's balance rule (docs/idea-ambiciosa-2026-11-03.md, Fan RE4, round 1): winning only stuns with the native
//     flash 0x17, no extra damage on the owner.
//
// Prerequisites (logged at startup, `sword_lock duel winspin ready=`): [LinkRuntime] SpinAttack=1 (the spin clip, its
// lease and the adapter; turning them on for the lab alone would install the arbiter, dispatch and MotionMove hooks and
// the charge spin for the whole game) and MortalDrawLineWaiver=1 (without it the spin's target scan selects nobody, as
// for the normal spin, which it gates the same way). GreatSpin=0 is allowed without side effects: GreatSpinRadius,
// Permille and Sparkles are read with SpinAttack=1 alone, the win spin asks for great = 1 itself and only the TP sound is
// not loaded (the spin voice plays). Without them a win exits with the flash alone, as before.
//
// The crowd rule's lab default: round 5's SwordLockCrowdMm=8000 kept the duel shut in the r101 siege (villagers stood
// 0.5-2.2 m from the owner in the 8 live sessions of 3-nov). Missing or invalid now = kLabCrowdMm, 1500 mm: a Ganado
// practically on top of Link refuses the duel; the rest of the admission (a cEmWep in flight within 10 m, unread actors,
// blind) stays. sword_lock_lab.h keeps its Config default untouched: the public DLL carries that struct.
#include "sword_lock_lab.h"
#include "game_clock.h"
#include <cmath>
#include <cstdint>

namespace link_runtime::sword_lock::lab {
// ------------------------------------------------------------------ [Combat] keys
inline constexpr double kLabCrowdMm = 1500;
// SwordLockCrowdMm as the lab applies it: the given value when valid (kMinCrowdMm..kMaxCrowdMm), else kLabCrowdMm. An
// invalid value was already counted by ReadConfig.
template <class Get> double LabCrowdMm(Get&& get) {
    wchar_t v[128]{};
    get(L"SwordLockCrowdMm", v, 128);
    double mm = 0;
    return v[0] && detail::Number(v, kMinCrowdMm, kMaxCrowdMm, mm) ? mm : kLabCrowdMm;
}
struct WinSpinConfig {bool on = true;};
// SwordLockWinSpin: missing = on (the lab's default), exact 0/1, anything else a problem and off (the flash alone).
template <class Get> WinSpinConfig ReadWinSpinConfig(Get&& get, unsigned& problems) {
    WinSpinConfig c; wchar_t v[128]{};
    get(L"SwordLockWinSpin", v, 128);
    if (!v[0]) return c;
    if (!detail::Switch(v, c.on)) ++problems;
    return c;
}
enum class WinSpinReady : uint8_t {Ready, Off, SpinAttack, Clip, LineWaiver, Adapter};
inline const char* WinSpinReadyName(WinSpinReady r) {
    switch (r) {
    case WinSpinReady::Ready: return "ready"; case WinSpinReady::SpinAttack: return "spin_attack"; case WinSpinReady::Clip: return "clip";
    case WinSpinReady::LineWaiver: return "line_waiver"; case WinSpinReady::Adapter: return "adapter"; default: return "off";
    }
}
// What the runtime has after StartCombat: g_cmb.spin_attack (SwordPrepare drops it when the clip or voice is missing),
// the spin clip ready, g_cmb.line_waiver and the damage adapter.
struct WinSpinPrereq {bool spin_attack = false, clip = false, line_waiver = false, adapter = false;};
inline WinSpinReady Readiness(const WinSpinConfig& c, const WinSpinPrereq& p) {
    if (!c.on) return WinSpinReady::Off;
    if (!p.spin_attack) return WinSpinReady::SpinAttack;
    if (!p.clip) return WinSpinReady::Clip;
    if (!p.line_waiver) return WinSpinReady::LineWaiver;
    if (!p.adapter) return WinSpinReady::Adapter;
    return WinSpinReady::Ready;
}

// ------------------------------------------------------------------ the hit list at the strike
inline constexpr bool kExcludeOwner = true;   // the decision above: the stunned owner never takes the spin's hit
// Drops every entry of `actor` from a hit list (items[i].pointer, count; order kept). Returns how many were dropped.
template <class List> unsigned ExcludeActor(List& list, uint32_t actor) {
    if (!actor) return 0;
    unsigned out = 0, dropped = 0;
    for (unsigned i = 0; i < list.count; ++i) {
        if (list.items[i].pointer == actor) {++dropped; continue;}
        if (out != i) list.items[out] = list.items[i];
        ++out;
    }
    list.count = out;
    return dropped;
}
// The hit list's entries within `radius` (items[i].distance, XZ from Link): the spin's own scan comes first, nearest
// first, and the bullets append theirs; one of those may stand within the radius too (another height band).
template <class List> unsigned CountWithin(const List& list, float radius) {
    unsigned n = 0;
    for (unsigned i = 0; i < list.count; ++i) n += list.items[i].distance <= radius ? 1U : 0U;
    return n;
}
// The entries the release waits for: up to the last one within `radius` (the hit list publishes in order).
template <class List> unsigned NeedFor(const List& list, float radius) {
    unsigned need = 0;
    for (unsigned i = 0; i < list.count; ++i) if (list.items[i].distance <= radius) need = i + 1;
    return need;
}

// ------------------------------------------------------------------ the hold
inline constexpr double kWinSpinStartMs = 500;   // the spin's lease must start by then (game time from the release frame)
inline constexpr double kWinSpinHoldMs = 900;    // the world goes on by then, whatever happened (the probe watches 1000)
inline constexpr double kWinSpinHoldFrames = kWinSpinHoldMs * 0.06;   // the same cap in game frames (60 Hz): 54
// The hit list's deadline for the win spin (SwordDecide, its strike). The normal spin's is strike + span (span = max(24 +
// 2 x bullets, 2 x targets) frames): a running Ganado consumes its record on the next frame, so one target per 1-2 frames.
// A frozen one never consumes it: each record holds the adapter's override for its whole 3-frame TTL (about 3 game frames
// per target at 60 FPS, 6 at 30 FPS), so 8 Ganados in reach need about 24 frames at 60 FPS and 48 at 30 FPS, and the normal
// deadline would skip the last ones (HitQueue::Pump: no record at all) while the hold took the skips for resolved targets.
// The win spin's deadline is never before the hold's own cap plus the same span: nothing is skipped by the deadline while
// the world is held, and what the hold cap leaves gets the normal span with the world running.
inline double WinSpinDeadline(double strike_now, double span, double t0) {
    const double normal = strike_now + span, held = t0 + kWinSpinHoldFrames + span;
    return held > normal ? held : normal;
}
enum class SpinWhy : uint8_t {None, Hits, NoneInReach, NoStart, Lost, HoldCap, Unreadable, Load, Clock, LinkDead, Qte, Cutscene, Grab,
                              FreezeLost, Stopped, Expired};
inline const char* SpinWhyName(SpinWhy w) {
    switch (w) {
    case SpinWhy::Hits: return "hits"; case SpinWhy::NoneInReach: return "none_in_reach"; case SpinWhy::NoStart: return "no_start";
    case SpinWhy::Lost: return "lost"; case SpinWhy::HoldCap: return "hold_cap"; case SpinWhy::Unreadable: return "unreadable";
    case SpinWhy::Load: return "load"; case SpinWhy::Clock: return "clock"; case SpinWhy::LinkDead: return "link_dead";
    case SpinWhy::Qte: return "qte"; case SpinWhy::Cutscene: return "cutscene"; case SpinWhy::Grab: return "grab";
    case SpinWhy::FreezeLost: return "freeze_lost"; case SpinWhy::Stopped: return "stopped"; case SpinWhy::Expired: return "expired";
    default: return "-";
    }
}
// One tick of the hold (the probe's decide and core frames), read by the runtime.
struct SpinFacts {
    bool readable = false;     // the world read this tick
    bool same_world = true;    // the player, the global and the room of the release frame (no load)
    bool link_alive = true;    // Link's HP > 0, no death demo
    bool qte = false;          // SPF_ACTBTN: a native QTE
    bool cutscene = false;     // Status_flg[0] event / cut / movie
    bool freeze_ok = true;     // our freeze still holds the bits it took
    bool link_taken = false;   // Link's routine r0 neither 0 (free) nor 5 (a dispatch lease): a grab, a hit
    bool can_request = false;  // a decide where SwordDecide would start the spin now (its own gate, read by the runtime)
    bool requested = false;    // our request still queued (g_sword_request with lab_win)
    bool lease = false;        // our spin's lease runs
    bool struck = false;       // its strike ran (the hit list began)
    unsigned need = 0;         // NeedFor at the strike
    unsigned resolved = 0;     // the hit list's Published + Skipped
    unsigned deferred = 0;     // the hit list's Deferred
    bool hits_done = false;    // the hit list Done after the strike
    bool expired = false;      // the hit list's deadline passed (HitQueue::Pump skipped what was left: never "hits")
    double now = 0;            // game time (60 Hz frames)
};
struct SpinStep {bool request = false, started = false, struck = false, release = false; SpinWhy why = SpinWhy::None;};
enum class SpinPhase : uint8_t {Off, Wait, Requested, Spinning, Struck, Done};
class WinSpinHold {
public:
    void Begin(double now) {
        phase_ = SpinPhase::Wait; t0_ = now; requests_ = drops_ = 0; need_ = 0; started_ = struck_ = false;
        start_ms_ = strike_ms_ = release_ms_ = -1; why_ = SpinWhy::None;
    }
    // The duel ended some other way while held (never expected: the probe has no abandon after its release).
    void Stop(double now) {if (Holding()) End(SpinWhy::Stopped, now);}
    SpinStep Tick(const SpinFacts& f) {
        SpinStep s;
        if (!Holding()) return s;
        SpinWhy safety = SpinWhy::None;
        if (!f.readable) safety = SpinWhy::Unreadable;
        else if (!f.same_world) safety = SpinWhy::Load;
        else if (f.now + game_clock::kEpsilon < t0_) safety = SpinWhy::Clock;
        else if (!f.link_alive) safety = SpinWhy::LinkDead;
        else if (f.qte) safety = SpinWhy::Qte;
        else if (f.cutscene) safety = SpinWhy::Cutscene;
        else if (!f.freeze_ok) safety = SpinWhy::FreezeLost;
        else if (f.link_taken) safety = SpinWhy::Grab;
        if (safety != SpinWhy::None) return Release(s, safety, f.now);
        const double t = Ms(f.now - t0_);
        if (phase_ == SpinPhase::Wait || phase_ == SpinPhase::Requested) {
            if (f.lease) {phase_ = SpinPhase::Spinning; started_ = true; start_ms_ = t; s.started = true;}
            else {
                if (game_clock::Reached(t, kWinSpinStartMs)) return Release(s, SpinWhy::NoStart, f.now);
                if (phase_ == SpinPhase::Requested && !f.requested) {phase_ = SpinPhase::Wait; ++drops_;}   // dropped: ask again
                if (phase_ == SpinPhase::Wait && f.can_request) {phase_ = SpinPhase::Requested; ++requests_; s.request = true;}
                return s;
            }
        }
        if (phase_ == SpinPhase::Spinning) {
            if (!f.lease) return Release(s, SpinWhy::Lost, f.now);   // cut before its strike
            if (!f.struck) return game_clock::Reached(t, kWinSpinHoldMs) ? Release(s, SpinWhy::HoldCap, f.now) : s;
            phase_ = SpinPhase::Struck; struck_ = true; strike_ms_ = t; need_ = f.need; s.struck = true;
        }
        if (need_ == 0) return Release(s, SpinWhy::NoneInReach, f.now);
        // The deadline's skips are no records at all: never released as "hits" (WinSpinDeadline keeps it past the cap).
        if (f.expired) return Release(s, SpinWhy::Expired, f.now);
        if (f.hits_done || (f.resolved >= need_ && f.deferred == 0)) return Release(s, SpinWhy::Hits, f.now);
        if (!f.lease) return Release(s, SpinWhy::Lost, f.now);
        if (game_clock::Reached(t, kWinSpinHoldMs)) return Release(s, SpinWhy::HoldCap, f.now);
        return s;
    }
    bool Holding() const {return phase_ != SpinPhase::Off && phase_ != SpinPhase::Done;}
    SpinPhase phase() const {return phase_;}
    bool started() const {return started_;}
    bool struck() const {return struck_;}
    unsigned requests() const {return requests_;}
    unsigned drops() const {return drops_;}
    unsigned need() const {return need_;}
    double start_ms() const {return start_ms_;}     // game ms from the release frame (-1: never)
    double strike_ms() const {return strike_ms_;}
    double release_ms() const {return release_ms_;}
    SpinWhy why() const {return why_;}
    double t0() const {return t0_;}                 // game time of the release frame (Begin)
    double ElapsedMs(double now) const {return Ms(now - t0_);}
private:
    void End(SpinWhy why, double now) {phase_ = SpinPhase::Done; why_ = why; release_ms_ = now >= t0_ ? Ms(now - t0_) : 0;}
    SpinStep& Release(SpinStep& s, SpinWhy why, double now) {End(why, now); s.release = true; s.why = why; return s;}
    SpinPhase phase_ = SpinPhase::Off;
    SpinWhy why_ = SpinWhy::None;
    double t0_ = 0, start_ms_ = -1, strike_ms_ = -1, release_ms_ = -1;
    unsigned requests_ = 0, drops_ = 0, need_ = 0;
    bool started_ = false, struck_ = false;
};

// ------------------------------------------------------------------ pushed=
// The spin's targets watched after the release (the after watch, 1000 ms): one counts as pushed once it enters the
// damage routine (em10 r0 2: the knife's reaction, pushed back or knocked down) from another routine, or loses HP.
inline constexpr uint8_t kDamageR0 = 2;
inline constexpr unsigned kMaxTracked = 16;   // ending_blow_target.h kMaxTargets
struct Seen {bool read = false; uint8_t id = 0, type = 0, r0 = 0; int hp = 0; float pos[3]{};};
class Pushes {
public:
    void Reset() {n_ = 0;}
    // At the strike: the target as it stands (frozen). An unread one is not tracked.
    bool Add(uint32_t actor, const Seen& s) {
        if (n_ >= kMaxTracked || !actor || !s.read) return false;
        Tracked& t = t_[n_++];
        t = Tracked{}; t.actor = actor; t.id = s.id; t.type = s.type; t.r0 = s.r0; t.hp = s.hp;
        for (unsigned k = 0; k < 3; ++k) t.pos[k] = s.pos[k];
        return true;
    }
    // A tick of the after watch. Another id/type (the slot reused) or an unread slot is gone; what it showed before stays.
    void Observe(unsigned i, const Seen& s) {
        if (i >= n_) return;
        Tracked& t = t_[i];
        if (t.gone) return;
        if (!s.read || s.id != t.id || s.type != t.type) {t.gone = true; return;}
        if ((s.r0 == kDamageR0 && t.r0 != kDamageR0) || s.hp < t.hp) t.pushed = true;
        const double dx = double(s.pos[0]) - t.pos[0], dz = double(s.pos[2]) - t.pos[2];
        const double d = std::sqrt(dx * dx + dz * dz);
        if (std::isfinite(d) && d > t.moved) t.moved = d;
    }
    unsigned count() const {return n_;}
    uint32_t actor(unsigned i) const {return i < n_ ? t_[i].actor : 0;}
    unsigned pushed() const {unsigned n = 0; for (unsigned i = 0; i < n_; ++i) n += t_[i].pushed ? 1U : 0U; return n;}
    unsigned gone() const {unsigned n = 0; for (unsigned i = 0; i < n_; ++i) n += t_[i].gone ? 1U : 0U; return n;}
    double moved_max() const {double m = 0; for (unsigned i = 0; i < n_; ++i) m = t_[i].moved > m ? t_[i].moved : m; return m;}
private:
    struct Tracked {uint32_t actor = 0; uint8_t id = 0, type = 0, r0 = 0; int hp = 0; float pos[3]{}; bool pushed = false, gone = false; double moved = 0;};
    Tracked t_[kMaxTracked]{};
    unsigned n_ = 0;
};
}  // namespace link_runtime::sword_lock::lab

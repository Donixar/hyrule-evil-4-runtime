#pragma once
// Master Sword Blade Lock H2b: Link's TP blade-lock clips on the lab duel's lease (docs/idea-ambiciosa-2026-11-03.md,
// "Animación y sonido" and hito H2; D:/link-in-re4-work/swordlock-h2/H2a.md, "Lo que falta para H2b"). Lab DLL only:
// link_runtime.cpp includes this header inside #if LINK_SWORD_LOCK_LAB (as sword_lock_winspin.h), so the public build
// never sees it and stays byte-identical. Pure: no hooks, no process access, no native calls.
// Tests: tests/native/test_sword_lock_anim.cpp (scripts/test_sword_lock_anim.bat).
//
// The clips (scripts/sword_lock_anim.py, dll/sword_lock_assets.h): link_lock_a/b/c (loops of 40 TP frames, 1.33 s),
// link_lock_win (39 TP frames, 1.30 s, once) and link_lock_lose (74 TP frames, 2.47 s, once). The SEQ keeps one entry per
// TP frame, so a clip plays 2 game frames (60 Hz, game_clock) per TP frame: a/b/c 80, win 78, lose 148.
//
// The band rule (Fan Zelda's condition, d_a_b_gnd.cpp:3403-3427):
//  * the duel's lease starts with clip a (TP opens with parameter 0), never the ocarina loop;
//  * while the duel runs its push, break and finish stretches the clip follows PushValue's band only: PV > 0.4 -> c,
//    PV < -0.4 -> b, else a. Between two clips set on the model at least kLockMs (1000) of game_clock pass, the lease's
//    first clip included (TP: field_0xc44[2] = 30 at 30 Hz). A hit, a miss or the owner's thrust never sets a clip: they
//    move PV, and the clip follows only once the band changed and the lock ran out (test j);
//  * the Lock stretch (Traba) never changes the clip;
//  * a loop that ends (the dispatch's Proceed) restarts the same clip (Replace) without touching the lock;
//  * win and lose set their clip once, at once, without waiting for the lock (TP checks them before the bands). A draw
//    keeps the active loop and slides ~600 as in H1c.
// The director only decides; the runtime sets each clip with the dispatch's Replace on a decide (the only place Replace
// runs), so the lock is measured between clips really set on the model.
//
// The win (H2a point 5): the clip's lunge is visual (part 0): the hip goes 1.06 m forward and the guard 1.74 m
// (kWinReachMm) around game frame 29, and comes back by the end. The chainsaw contacts of every lab session (H0, H1c,
// force: 74 tier-2 blocks of producers 0x2C178/0x2C249/0x2C2CA) sat 0.30-0.63 m in front of Link (`half=`), so the owner
// stands about 1.0-1.4 m away when the lock opens: the whole lunge would go through him. Decision: with the owner's centre
// closer than SwordLockWinCutMm (default kWinCutDefaultMm: the reach plus ~0.46 m of the owner's body, so the blade never
// enters him) the win clip's lease ends before the lunge (kWinLungeTick, the first game frame whose hip step exceeds 30 mm:
// win_part0_z_BIN_per_game_frame of the H2a manifest), so Link gathers and turns the blade, and the Great Spin follows as
// before; farther (or SwordLockWinCutMm=0) the whole clip plays. At the cut's ask (game frame 14) the hip already sits
// ~0.26 m forward of frame 0 (161.6 -> 418 mm): the pose cleanup jumps from there to idle (to be judged in game). The distance is
// measured and logged at the duel's opening (`sword_lock duel start owner_mm=`) and at the win (`owner_mm=`). Scaling
// the clip's hip in scripts/sword_lock_anim.py was not chosen: a scaled lunge still crosses an owner 1 m away, and the
// pinned clip would change.
//
// The lose (H2a point 6): the clip carries no travel (its ankles stay put); assets::lose_travel_mm is TP's ankle midpoint
// per game frame (to -1.29 m at frame 51, -1.72 m at 58..92, back to -1.29 m by 115). Carry moves Link away from the owner
// (the direction of H1c's slide, ppb::Direction; his facing's back only with the owner unread) by that profile, keeping his
// yaw, interpolated by the clip's time (60, 165 FPS alike), written after the dispatch's own MotionMove of
// the lease and before cPlayer::move runs the native collision on it, as backflip.h: a collided position that falls more
// than back_slice::kBlockSlack short of the command two ticks running (a wall, a body) ends the carry there (fail
// closed), so does a height change beyond back_slice::kHeight (a ledge) or a frame without the scenery check. With the
// carry run through (CarryReplacesSlide: done, released or blocked by a wall, or at least kCarryEnoughMm back) the lose
// replaces H1c's 1300 slide (parry_pushback.h): the world stays frozen through the clip and goes on at its end. A carry cut
// short (no scenery check, a ledge, bad input, a failed write) keeps H1c's slide from where Link stands. Without the
// MotionMove hook the carry is not safe: the lose keeps H1c's lease-back and slide.
//
// Mixing (hokan): Runtime::Replace sets hokan 0 (a jump between clips, up to 1.15 m at the blade tip for b <-> c,
// H2a band_change_pop). [Combat] SwordLockClipHokan (lab only, default 0) passes another hokan to the setter on the band
// and the final clips (6 game frames = TP's 3-frame morph) for the in-game test; loops restart with 0. Not seen in game.
#include "sword_lock_duel.h"
#include "back_slice.h"
#include "game_clock.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace link_runtime::sword_lock::anim {
namespace sd = link_runtime::sword_lock::duel;
using back_slice::Vec;
using back_slice::Output;
using back_slice::Finite;

inline constexpr double Frames(double ms) {return ms * 0.06;}
inline constexpr double Ms(double frames) {return frames / 0.06;}

// ------------------------------------------------------------------ clips and timing (game frames, 60 Hz)
enum class Clip : uint8_t {None, A, B, C, Win, Lose};
inline constexpr unsigned kClipCount = 6;
inline const char* ClipName(Clip c) {
    switch (c) {
    case Clip::A: return "a"; case Clip::B: return "b"; case Clip::C: return "c";
    case Clip::Win: return "win"; case Clip::Lose: return "lose"; default: return "none";
    }
}
inline bool Loops(Clip c) {return c == Clip::A || c == Clip::B || c == Clip::C;}
inline constexpr unsigned kTicksPerTpFrame = 2;
inline constexpr unsigned kLoopTicks = 40 * kTicksPerTpFrame;   // a, b, c: 1.33 s
inline constexpr unsigned kWinTicks = 39 * kTicksPerTpFrame;    // 1.30 s
inline constexpr unsigned kLoseTicks = 74 * kTicksPerTpFrame;   // 2.47 s
inline unsigned ClipTicks(Clip c) {return Loops(c) ? kLoopTicks : c == Clip::Win ? kWinTicks : c == Clip::Lose ? kLoseTicks : 0;}
inline constexpr double kLockMs = 1000;        // between two clips set (30 TP frames)
inline constexpr double kBand = sd::kBand;     // 0.4
inline constexpr unsigned kWinLungeTick = 16;  // hip +28 mm at 15->16, then +39..+74 mm per frame to 1.06 m at 29
inline constexpr double kWinReachMm = 1740;    // the guard's furthest point in the win clip (H2a manifest)
inline constexpr double kOwnerBodyMm = 460;    // the owner's body around his centre (not read: ~0.4 m plus margin)
inline constexpr double kWinCutDefaultMm = kWinReachMm + kOwnerBodyMm;   // 2200: SwordLockWinCutMm's default
// The probe releases on the decide after the one that asks, so the ask comes this many game frames ahead (a 30 FPS frame
// is 2 game frames); a one-shot played to its end is asked kEndMarginTicks earlier still, so its lease is handed back
// before the clip's own end: a hitch up to ~130 ms between the ask and the release still finds the clip running (else
// Proceed sets it again from frame 0: a late one-shot). The last frames given up are the settled pose (lose: flat from
// game frame 115; win: the hip within ~80 mm of its end).
inline constexpr unsigned kAskAheadTicks = 2, kEndMarginTicks = 6;
inline constexpr double kFinalWaitMs = 250;    // a win/lose clip not set this long after the duel's end: released as H1c
inline Clip BandOf(double pv) {return pv > kBand ? Clip::C : pv < -kBand ? Clip::B : Clip::A;}
// The stretches whose band the clip follows: push, break and finish (never the Traba).
inline bool Banding(sd::Phase p) {
    return p == sd::Phase::Push1 || p == sd::Phase::Break || p == sd::Phase::Push2 || p == sd::Phase::Finish;
}

// ------------------------------------------------------------------ [Combat] keys (lab DLL only)
inline constexpr unsigned kMaxHokan = 30;
inline constexpr double kMinWinCutMm = 500, kMaxWinCutMm = 5000;
struct ClipsConfig {
    bool on = true;                  // SwordLockClips: missing or 1 = the H2b clips; 0 = H1c's hold (the ocarina loop)
    unsigned hokan = 0;              // SwordLockClipHokan: 0..30, the setter's hokan on band and final clips
    double win_cut_mm = kWinCutDefaultMm; // SwordLockWinCutMm: 0 = the whole win clip always; 500..5000
};
namespace detail {
inline bool Whole(const wchar_t* v, unsigned lo, unsigned hi, unsigned& out) {
    if (!v || !*v) return false;
    unsigned n = 0;
    for (const wchar_t* p = v; *p; ++p) {
        if (*p < L'0' || *p > L'9') return false;
        n = n * 10 + unsigned(*p - L'0');
        if (n > hi) return false;
    }
    if (n < lo) return false;
    out = n; return true;
}
}  // namespace detail
// get(key, out, count) fills `out` with the raw value ("" when missing). A bad value keeps its default and counts a problem.
template <class Get> ClipsConfig ReadClipsConfig(Get&& get, unsigned& problems) {
    ClipsConfig c; wchar_t v[128]{}; unsigned n = 0;
    get(L"SwordLockClips", v, 128);
    if (v[0]) {
        if (v[0] == L'0' && !v[1]) c.on = false;
        else if (!(v[0] == L'1' && !v[1])) ++problems;
    }
    v[0] = 0; get(L"SwordLockClipHokan", v, 128);
    if (v[0]) {if (detail::Whole(v, 0, kMaxHokan, n)) c.hokan = n; else ++problems;}
    v[0] = 0; get(L"SwordLockWinCutMm", v, 128);
    if (v[0]) {
        if (detail::Whole(v, 0, unsigned(kMaxWinCutMm), n) && (n == 0 || n >= unsigned(kMinWinCutMm))) c.win_cut_mm = n;
        else ++problems;
    }
    return c;
}

// ------------------------------------------------------------------ the director
enum class Reason : uint8_t {None, Start, Band, Win, Lose, Late};
inline const char* ReasonName(Reason r) {
    switch (r) {
    case Reason::Start: return "start"; case Reason::Band: return "band"; case Reason::Win: return "win";
    case Reason::Lose: return "lose"; case Reason::Late: return "late"; default: return "none";
    }
}
// What the runtime does on this decide: set `clip` now (Replace) and log the line.
struct Plan {
    bool set = false;
    Clip clip = Clip::None, band = Clip::None;
    double pv = 0, lock_ms = -1;     // lock_ms: game ms since the previous clip set (-1: the first)
    Reason reason = Reason::None;
    bool cut = false;                // win: its lease ends before the lunge
    double owner_mm = -1;            // win: the owner's XZ distance as read (-1 unread: cut)
    double ask_tick = 0;             // win/lose: the clip's game frame where the release is asked
};
// The win's plan: cut before the lunge when the owner stands closer than cut_mm (unread counts as close); 0 = never.
inline bool WinCut(double owner_mm, double cut_mm) {return cut_mm > 0 && (!(owner_mm >= 0) || owner_mm < cut_mm);}
inline double AskTick(Clip c, bool cut) {
    if (c == Clip::Win && cut) return double(kWinLungeTick - kAskAheadTicks);
    return double(ClipTicks(c)) - double(kAskAheadTicks + kEndMarginTicks);
}
class Director {
public:
    void Reset() {*this = Director();}
    // The lease started with clip a (Runtime::Start, hokan 0): the lock runs from here.
    Plan Start(double now) {
        Reset();
        on_ = true; clip_ = Clip::A; t_set_ = t_clip_ = now;
        Plan p; p.set = true; p.clip = p.band = Clip::A; p.reason = Reason::Start;
        return p;
    }
    bool Active() const {return on_;}
    // A decide with the lease held while the duel runs (any stretch): the band's clip once the band left the clip and the
    // lock ran out. Nothing in the Traba, nothing once a final clip is due or set.
    Plan Band(double now, double pv, sd::Phase phase) {
        Plan p;
        if (!on_ || final_ != Clip::None || pending_ != Clip::None || !Banding(phase) || !std::isfinite(pv) || !std::isfinite(now)) return p;
        const Clip b = BandOf(pv);
        if (b == clip_) return p;
        const double since = now - t_set_;
        if (!game_clock::Reached(since, Frames(kLockMs))) {++held_; return p;}
        Keep();
        p.set = true; p.clip = p.band = b; p.pv = pv; p.lock_ms = Ms(since); p.reason = Reason::Band;
        if (min_gap_ms_ < 0 || p.lock_ms < min_gap_ms_) min_gap_ms_ = p.lock_ms;
        clip_ = b; t_set_ = t_clip_ = now; ++changes_;
        return p;
    }
    // The duel ended (any frame): win and lose wait for the next decide to set their clip (Final); the rest nothing.
    // True: a final clip is due.
    bool Ended(sd::Result r, double now) {
        if (!on_ || final_ != Clip::None || pending_ != Clip::None) return false;
        pending_ = r == sd::Result::Win ? Clip::Win : r == sd::Result::Lose ? Clip::Lose : Clip::None;
        t_end_ = now;
        return pending_ != Clip::None;
    }
    // The decide that sets the pending final clip, once, whatever the lock (owner_mm and cut_mm: the win's cut rule).
    Plan Final(double now, double pv, double owner_mm, double cut_mm) {
        Plan p;
        if (!on_ || pending_ == Clip::None || !std::isfinite(now)) return p;
        Keep();
        p.set = true; p.clip = pending_; p.band = BandOf(pv); p.pv = pv; p.lock_ms = Ms(now - t_set_);
        p.reason = pending_ == Clip::Win ? Reason::Win : Reason::Lose;
        p.owner_mm = owner_mm; p.cut = pending_ == Clip::Win && WinCut(owner_mm, cut_mm);
        p.ask_tick = AskTick(pending_, p.cut);
        final_ = clip_ = pending_; pending_ = Clip::None; cut_ = p.cut; ask_tick_ = p.ask_tick; t_set_ = t_clip_ = now;
        return p;
    }
    // The runtime could not set the clip of the last Band or Final plan (the setter refused without abandoning the lease):
    // back to the state before that plan, so the clip compared is the one really on the model, the lock runs from the last
    // clip really set and a final stays due (the next decide tries again; HoldsRelease still caps its wait at kFinalWaitMs).
    // Once per plan; true when something was undone.
    bool Undo() {
        if (!kept_) return false;
        clip_ = k_.clip; final_ = k_.final_clip; pending_ = k_.pending; cut_ = k_.cut;
        t_set_ = k_.t_set; t_clip_ = k_.t_clip; ask_tick_ = k_.ask_tick; min_gap_ms_ = k_.min_gap_ms; changes_ = k_.changes;
        kept_ = false; ++undone_;
        return true;
    }
    // The clip ended in the dispatch (Proceed): the clip to set again. A loop restarts (the lock untouched); a one-shot
    // that outran its release ask is set again too (late: the lease must hold until the probe's release) and asks now.
    Clip Loop(double now) {
        if (!on_) return Clip::None;
        ++loops_;
        if (!Loops(clip_)) {++late_; late_flag_ = true;}
        else t_clip_ = now;
        return clip_;
    }
    // Holds the probe's release: a final clip due and not yet set (for at most kFinalWaitMs), or set and before its ask.
    bool HoldsRelease(double now) const {
        if (!on_) return false;
        if (pending_ != Clip::None) return !game_clock::Reached(now - t_end_, Frames(kFinalWaitMs));
        if (final_ == Clip::None) return false;
        return !late_flag_ && !game_clock::Reached(now - t_clip_, ask_tick_);
    }
    double ClipElapsed(double now) const {return now - t_clip_;}   // game frames since the current clip was set
    Clip clip() const {return clip_;}
    Clip final_clip() const {return final_;}
    Clip pending() const {return pending_;}
    bool cut() const {return cut_;}
    double ask_tick() const {return ask_tick_;}
    unsigned changes() const {return changes_;}     // band changes set
    unsigned loops() const {return loops_;}
    unsigned late() const {return late_;}
    unsigned held() const {return held_;}           // decides where the band left the clip inside the lock
    double min_gap_ms() const {return min_gap_ms_;} // the shortest lock_ms of a band change (-1: none)
    unsigned undone() const {return undone_;}       // plans the runtime could not set (Undo)
private:
    struct Kept {
        Clip clip = Clip::None, final_clip = Clip::None, pending = Clip::None; bool cut = false;
        double t_set = 0, t_clip = 0, ask_tick = 0, min_gap_ms = -1; unsigned changes = 0;
    };
    void Keep() {
        k_ = Kept{clip_, final_, pending_, cut_, t_set_, t_clip_, ask_tick_, min_gap_ms_, changes_};
        kept_ = true;
    }
    bool on_ = false, cut_ = false, late_flag_ = false, kept_ = false;
    Clip clip_ = Clip::None, final_ = Clip::None, pending_ = Clip::None;
    double t_set_ = 0, t_clip_ = 0, t_end_ = 0, ask_tick_ = 0, min_gap_ms_ = -1;
    unsigned changes_ = 0, loops_ = 0, late_ = 0, held_ = 0, undone_ = 0;
    Kept k_;
};

// ------------------------------------------------------------------ the lose carry
// The profile at game frame e (linear between frames, clamped to the table): mm along the clip's Z, negative = back.
inline double Travel(const float* table, unsigned n, double e) {
    if (!table || !n || !std::isfinite(e)) return 0;
    if (e <= 0) return table[0];
    if (e >= double(n - 1)) return table[n - 1];
    const unsigned i = unsigned(e);
    const double f = e - double(i);
    return double(table[i]) + (double(table[i + 1]) - double(table[i])) * f;
}
struct Frame {
    uint32_t frame = 0; double time = 0; float step = 1.f;
    Vec before, after;              // Link last frame (collided) and after this tick's MotionMove
    bool collision = true;          // +0x2CE bit 0x100 (the scenery check runs this frame)
};
enum class Stop : uint8_t {None, Done, Blocked, Height, BadInput, WriteFailed, Interrupted, NoCollision, Released};
inline const char* StopName(Stop s) {
    static const char* const n[] = {"none", "done", "blocked", "height", "bad_input", "write_failed", "interrupted",
                                    "no_collision", "released"};
    return n[unsigned(s)];
}
struct Summary {
    uint64_t lease = 0; unsigned ticks = 0, repeats = 0, lag_ticks = 0;
    float travelled = 0;            // mm commanded back along the facing (the last command; negative = forward)
    float deepest = 0;              // the furthest back commanded
    float planned = 0;              // the profile's end (mm back)
    float max_lag = 0, e = 0;
    Vec origin; float dir_x = 0, dir_z = 0, yaw = 0;
    bool away = false;              // dir: away from the owner (H1c's slide direction); false = the facing's back
    float turn_deg = 0;             // angle between dir and the facing's back (how far off his facing Link is carried)
    Stop stop = Stop::None;
};
inline constexpr double kCarryEnoughMm = 1000;   // a carry cut short this far back still stands in for the slide
// The lose's release skips H1c's slide only after a carry that did its job: run to the end, released by the probe,
// stopped by a wall (a slide would stop there too) or already kCarryEnoughMm back. Cut short otherwise (no scenery check,
// a ledge, bad input, a failed write): H1c's slide from where Link stands.
inline bool CarryReplacesSlide(const Summary& s) {
    return s.lease && (s.stop == Stop::Done || s.stop == Stop::Released || s.stop == Stop::Blocked ||
                       double(s.travelled) >= kCarryEnoughMm);
}
class Carry {
public:
    bool Active() const {return on_;}
    uint64_t lease() const {return on_ ? s_.lease : 0;}
    const Summary& summary() const {return s_;}
    // origin/yaw: Link at the lose clip's set (the yaw is kept on every write); start: game time then; table/n:
    // assets::lose_travel_mm. Carried along the facing's back (+0xA4).
    bool Begin(uint64_t lease, Vec origin, float yaw, double start, const float* table, unsigned n) {
        return Begin(lease, origin, yaw, -std::sin(yaw), -std::cos(yaw), false, start, table, n);
    }
    // The same along (back_x, back_z) (a unit XZ vector: away from the owner, ppb::Direction); away tags it in the summary.
    bool Begin(uint64_t lease, Vec origin, float yaw, float back_x, float back_z, bool away, double start, const float* table,
               unsigned n) {
        s_ = Summary{}; s_.lease = lease; on_ = false;
        const float len = std::hypot(back_x, back_z);
        if (!lease || !Finite(origin) || !std::isfinite(yaw) || !std::isfinite(start) || !table || n < 2 || !std::isfinite(len) ||
            std::fabs(len - 1.f) > 0.01f) {s_.lease = 0; return false;}
        table_ = table; n_ = n; start_ = start; known_ = have_cmd_ = false; lag_run_ = 0;
        s_.origin = origin; s_.yaw = yaw; s_.dir_x = back_x / len; s_.dir_z = back_z / len; s_.away = away;
        const double fx = -std::sin(yaw), fz = -std::cos(yaw), dot = fx * s_.dir_x + fz * s_.dir_z;
        s_.turn_deg = float(std::acos(dot > 1 ? 1.0 : dot < -1 ? -1.0 : dot) * 57.29577951308232);
        s_.planned = float(-double(table[n - 1]));
        on_ = true; return true;
    }
    Output Step(const Frame& f) {
        Output o;
        if (!on_) return o;
        if (known_ && f.frame == frame_) {++s_.repeats; return o;}
        known_ = true; frame_ = f.frame;
        const double e = f.time - start_;
        if (!Finite(f.before) || !Finite(f.after) || !std::isfinite(e) || e < -game_clock::kEpsilon ||
            !std::isfinite(f.step) || !(f.step > 0.f)) return Halt(Stop::BadInput), o;
        ++s_.ticks; s_.e = float(e);
        if (std::fabs(f.before.y - s_.origin.y) > back_slice::kHeight) return Halt(Stop::Height), o;
        if (!f.collision) return Halt(Stop::NoCollision), o;
        if (have_cmd_) {   // where the last command left Link vs where the collision put him
            const float lag = std::hypot(f.before.x - cmd_x_, f.before.z - cmd_z_);
            if (lag > s_.max_lag) s_.max_lag = lag;
            if (lag > back_slice::kBlockSlack) {++s_.lag_ticks; if (++lag_run_ >= 2) return Halt(Stop::Blocked), o;}
            else lag_run_ = 0;
        }
        const double at = e + double(f.step);   // one tick ahead, as backflip.h
        const float back = float(-Travel(table_, n_, at));
        cmd_x_ = s_.origin.x + s_.dir_x * back; cmd_z_ = s_.origin.z + s_.dir_z * back; have_cmd_ = true;
        s_.travelled = back;
        if (back > s_.deepest) s_.deepest = back;
        o.write = true; o.from = f.after; o.x = cmd_x_; o.z = cmd_z_; o.yaw = s_.yaw;
        if (at >= double(n_ - 1)) Halt(Stop::Done);   // this is the last write
        return o;
    }
    bool Abort(Stop why) {const bool was = on_; Halt(why); return was;}
private:
    void Halt(Stop why) {if (on_) {on_ = false; s_.stop = why;}}
    Summary s_; bool on_ = false, known_ = false, have_cmd_ = false;
    const float* table_ = nullptr; unsigned n_ = 0;
    double start_ = 0; float cmd_x_ = 0, cmd_z_ = 0; uint32_t frame_ = 0; unsigned lag_run_ = 0;
};
// The carry line's fields (link_runtime.cpp logs "sword_lock anim carry <these> tick=").
inline int FormatCarry(char* out, size_t n, const Summary& s) {
    return std::snprintf(out, n, "stop=%s lease=%llu ticks=%u repeats=%u travelled=%.0f deepest=%.0f planned=%.0f "
        "max_lag=%.0f lag_ticks=%u e=%.1f dir=%s turn_deg=%.0f replaces_slide=%u", StopName(s.stop),
        static_cast<unsigned long long>(s.lease), s.ticks, s.repeats, double(s.travelled), double(s.deepest), double(s.planned),
        double(s.max_lag), s.lag_ticks, double(s.e), s.away ? "owner" : "facing", double(s.turn_deg), CarryReplacesSlide(s) ? 1U : 0U);
}
}  // namespace link_runtime::sword_lock::anim

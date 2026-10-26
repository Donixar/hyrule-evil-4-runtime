#pragma once
// Ending Blow leap (16-oct-2026, design D:/link-in-re4-work/ending-blow-leap/SPEC.md): carries Link
// toward the down Ganado during the owned Ending Blow lease, like TP's finishing-blow jump, instead
// of jumping in place. Pure decisions plus small templated memory helpers; the takeoff lives in
// spin_attack_runtime.h (SwordDecide), the per-tick hook in link_runtime.cpp (CmbLeapOwns /
// CmbMoveDetour). Pinned bio4.exe SHA 19aed4af...; RVAs, base 0x400000. Off by default
// (EndingBlowLeap=0): nothing here runs and the in-place jump is unchanged.
//
// Where Link is moved (the Mortal Draw magnet's seam, mortal_draw_magnet.h header): right after the
// dispatch's own MotionMove (0x262B90) of our lease. cPlayer::move then runs the body overlap check
// (EmAtCheck, 0x3650F9), the scenery check (SatMgr, 0x36510E, only while +0x2CE has bit 0x100) and
// collision 0x1955B0 (0x36516E) on our position the same frame, exactly as on clip root travel.
// X (+0x94) and Z (+0x9C) are written; Y (+0x98) only on a planned descent (Descend, 24-oct); never oldPos (+0x110),
// +0x2CE or the yaw.
//
// Takeoff (Plan, same frame as the clip start, after the one-shot face yaw): u = unit XZ vector from
// Link P to the target root T; travel D = clamp(d - standoff, 0, range - standoff), shortened to stop
// body_clear short of any other Ganado on the path (pure reads, no native query). Under min_travel
// (or without the face write) Link jumps in place exactly as before.
// Each tick (Step): e = game time since the start (60 Hz frames), step = this tick (2*deltaTime).
// The command is C = D * Curve(tau), tau = clamp((e + step - e0) / (land - e0), 0, 1), land = strike_tick - 2,
// raised by at most CmdCap(D) per 60 Hz frame. Burst (17-oct, EndingBlowLeapBurst / 100 = a, default 2): Link
// holds through the clip's grounded crouch (e0 = takeoff, 4 game frames), then Curve(tau) = tau + ((a - 1) / 2)
// tau (1 - tau) (2 - tau) eases out from a times the mean rate D / (land - e0) right after takeoff to (3 - a) / 2
// times it at land (a = 2: 2x, then 0.5x; never a stall). The cap is max_cmd (40) or cmd_headroom (1.1) times
// that takeoff rate (248.5 at D = 6100); the curve is concave, so a steady deltaTime never asks for more than
// the takeoff rate and the cap never cuts a planned leap short. a = 1 is the linear leap of 16-oct bit for bit:
// e0 = 0, C = D * min(1, (e + step) / land), cap 1.1 x D / land (68.3 at D = 3600, 115.7 at D = 6100). The
// command looks one tick ahead, so a tick much longer than the last (a hitch, an uneven frame rate) can be
// clipped, and the shortfall is made up only within the 10% headroom before land, so a very uneven frame rate
// can end a leap short (counted in `capped`). The same total and landing frame at every steady frame rate,
// never more than planned. C counts commanded travel, not achieved travel: there is no catch-up after a block.
// The stab gets no travel.
//
// Height (17-oct, Donald: no leap with Link above the Ganado; 24-oct, bug 5: height should not matter): the plan
// takes a target down to 6000 below or 2500 above beyond the strike reach (ending_blow_height.h, the same table as
// the selection), the strike band within it. The native floor snap carries Link along slopes and steps (any floor
// < 1000 below, or up to 600 above, each frame). The height stop is relative to the planned descent: Link's Y must
// stay within `height` (300) of the span between his takeoff Y and the target's (a ledge or a step beyond the
// target's level, or an unexpected rise above both); with the target level it is the takeoff test of 16-oct.
// Descend (24-oct): a floor 1000 or more below is never snapped to, so on a descent deeper than descend_from (600)
// the leap also lowers Link's Y toward the planned line y0 + dy x Curve(tau), at most fall_rate per 60 Hz frame. The
// write happens before cPlayer::move's floor snap (one ray from the written Y + 600 down), so over any floor the snap
// puts him back on it the same frame and nothing changes on solid ground; over a drop he comes down along the line and
// lands on the target's level instead of hovering. Lowering by `drop` on a tick that also moves him forward shrinks the
// rise ahead the snap still catches from 600 to 600 - drop (review 24-oct M2), so the drop is gated, the same at every
// frame rate: only fall_probe (50) a tick until the previous tick's written Y survived the snap (no floor under him
// there: free), then fall_rate per 60 Hz frame but never more than fall_tick_max (150) a tick. On solid ground the
// snap undoes every probe and the catch window stays 550; over a drop it is 450 at least, at 15 FPS as at 240 (a slow
// frame rate only comes down in more ticks). When the leap stops with Link still above the target's level (contact,
// time, blocked..., and the lease ending: the clip finishing, or a hit or a grab taking Link; review 24-oct M1),
// Settle keeps lowering him toward it, Y only, until the snap holds him on a floor (3 ticks without going down), he
// reaches it, or settle_max game frames pass. After the lease, Settle goes on from the player's own MotionMove
// (link_runtime.cpp CmbLeapSettleOwns), for the same player in the same room only. Never with the scenery check off
// (no snap to catch him): that stops both.
// Fail closed: a stop (contact, blocked, lateral slide, height change, outside move, target lost or
// moving, time, bad input, write failure, lifecycle interrupt, scenery check off) ends the leap for
// the rest of the action; the clip plays out in place and the strike is rechecked at the strike
// reach (1200), so a short leap misses honestly. A target that starts getting up (r1 0x1F, Rising)
// during the leap is not lost (16-oct night, lethal on rise): the hook reports it with rising=1 and
// the leap carries on; dead, despawned, another actor or any other routine still stops it.
// The knife press refuses a leap whose body scan would land Link beyond the strike reach (Corridor),
// so the native knife slash plays instead of a stab that cannot connect.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "ending_blow_height.h"
#include "game_clock.h"

namespace link_runtime::ending_blow::leap {
namespace layout {
constexpr uint32_t pos_x = 0x94, pos_y = 0x98, pos_z = 0x9C;
constexpr uint32_t collision = 0x2CE;       // u16; SatMgr::check runs only with this bit (sat_check.asm.txt)
constexpr uint16_t collision_bit = 0x100;
}  // namespace layout

struct Vec2 { float x = 0, z = 0; };
struct Body { float x = 0, y = 0, z = 0; };
inline bool Finite(Vec2 a) { return std::isfinite(a.x) && std::isfinite(a.z); }
inline bool Finite(Body a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline float LenXZ(float dx, float dz) { return std::sqrt(dx * dx + dz * dz); }

struct Config {
    unsigned mode = 0;            // EndingBlowLeap: 0 off, 1 on, 2 observe (plan logged, nothing written)
    // 8500 with and without the lock (17-oct, Donald asked for 8.5 m; was 7000 / 4000, before that 3000 and 4500):
    // the native kick lands a Ganado 3.6-4.6 m away and he asked for a longer leap.
    float range = 8500.f;         // EndingBlowLeapRange: selection reach with a latched Z-lock (max travel 7600)
    float free_range = 8500.f;    // EndingBlowLeapFreeRange: selection reach without one (never above range)
    float standoff = 900.f;       // EndingBlowLeapStandoff: landing distance from the target root (XZ)
    float strike = 1200.f;        // EndingBlowStrikeRange: reach accepted at the strike
    bool prefer_lock = true;      // EndingBlowLeapPreferLock
    // Fixed by the design (not INI).
    float min_travel = 150.f;     // shorter travel: jump in place
    float body_clear = 650.f;     // stop this far before another Ganado on the path, and its lateral reach
    float body_dy = 400.f;        // other bodies counted only within this height of the path (Link -> target line)
    height::Band at_strike = height::kStrike;  // within the strike reach (in place) = the strike recheck's band (17-oct review)
    height::Band beyond = height::kFar; // beyond it (not `far`: a windows.h macro): 900 down, 600 up (17-oct, ending_blow_height.h)
    // Descend (24-oct): Y help on a planned descent deeper than descend_from (target dy < -descend_from).
    float descend_from = 600.f;   // above the snap's own reach upward; shallower descents are the snap's alone
    float fall_rate = 150.f;      // Y lowered at most this per 60 Hz frame (4000 in ~27 frames)
    // M2 (review 24-oct): a fixed cap a tick, not one scaled by the step, so the snap's ray (from y + 600) still sees a
    // rise of 450 ahead at every frame rate (was 400 a tick: 200 at <= 20 FPS).
    float fall_tick_max = 150.f;
    float fall_probe = 50.f;      // a tick's drop until the last written Y survived the snap (a 550 window on a floor)
    float fall_proof = 2.f;       // "survived": Link's collided Y this tick within this of the Y written last tick
    unsigned settle_hold = 3;     // Settle ends after this many ticks in a row that did not bring Link down (a floor)
    float settle_max = 180.f;     // and after this many game frames (3 s; 6000 at 15 FPS needs about 160)
    float max_cmd = 40.f;         // command cap per 60 Hz frame (linear: up to D = 2109, planned peak 36.2 at D = 2100)
    float cmd_headroom = 1.1f;    // beyond it the cap is 1.1 x the takeoff rate a x D / (land - e0) (linear: 62.1 at
                                  // D = 3600, cap 68.3; 105.2 and 115.7 at D = 6100. Burst 2: 225.9 and 248.5 at
                                  // D = 6100); a steady deltaTime never reaches it
    // Burst (17-oct, Donald: a fast start; arc research wf_f5b0b735): EndingBlowLeapBurst / 100, clamped to 1..3
    // (beyond 3 the curve would overshoot D and come back). 1 = the linear leap of 16-oct bit for bit.
    float burst = 2.f;
    float takeoff = 4.f;          // e0: no travel up to here (burst > 1): the clip keeps the feet down through TP
                                  // frames 0..2 (spin_ending_assets.py JUMP_TAKEOFF) = game frames 0..4
    // Blocked: commanded minus achieved over lag_ratio x the last command's rate per 60 Hz frame + lag_slack two
    // ticks running (the same tolerance at every frame rate since 16-oct night), or over lag_max at once. With a
    // burst the rate is the mean D / (land - e0) all through (Step).
    float lag_ratio = 0.5f, lag_slack = 10.f, lag_max = 120.f;
    float lateral = 300.f, height = 300.f;  // sideways slide off the line; Y outside the takeoff..target span by more
    float outside_slack = 100.f;  // moved by something else: farther than the last command plus this
    float target_step = 200.f;    // target moved more than this per 60 Hz frame
    unsigned land_margin = 2;     // land = strike_tick - land_margin (58 for the 60-frame strike)
    bool On() const { return mode == 1; }
    bool Observe() const { return mode == 2; }
    float MaxTravel() const { return range > standoff ? range - standoff : 0.f; }
    float Burst() const { return burst > 1.f ? (burst < 3.f ? burst : 3.f) : 1.f; }  // NaN: 1 (linear)
    // e0, the first game frame of travel: the takeoff with a burst, 0 for the linear leap or a window that ends first.
    float Start(float land) const { return Burst() > 1.f && takeoff > 0.f && land > takeoff ? takeoff : 0.f; }
    // Share of D at tau: the cubic Hermite with end slopes a and (3 - a) / 2. Exactly 0 and 1 at the ends, tau itself
    // at a = 1; monotone and concave for a in 1..3 (Curve'' = (a - 1) / 2 x (6 tau - 6) <= 0).
    double Curve(double tau) const {
        if (!(tau > 0.0)) return 0.0;
        if (tau >= 1.0) return 1.0;
        return tau + (double(Burst()) - 1.0) / 2.0 * tau * (1.0 - tau) * (2.0 - tau);
    }
    // Command cap per 60 Hz frame for a leap of `travel` landing at `land`: never below the takeoff rate a x D / (land - e0).
    float CmdCap(float travel, float land) const {
        const float span = land - Start(land);
        const float planned = span > 0.f && travel > 0.f ? cmd_headroom * Burst() * travel / span : 0.f;
        return planned > max_cmd ? planned : max_cmd;
    }
};

// ---- takeoff --------------------------------------------------------------------------------------
enum class Why : uint8_t { Ok, Off, Short, Bodies, NoFace, Height, BadInput, NoTime };
inline const char* WhyName(Why w) {
    static const char* const n[] = {"ok", "off", "short", "bodies", "no_face", "height", "bad_input", "no_time"};
    return n[unsigned(w)];
}
struct Course {
    Why why = Why::Off;
    float d = 0, dy = 0;          // target distance (XZ) and height difference at takeoff
    float raw = 0;                // clamp(d - standoff, 0, max travel), before the body scan
    float travel = 0;             // D; 0 = jump in place
    float land = 0, y0 = 0;       // end of the travel window (60 Hz frames); Link's Y at takeoff
    Vec2 p0, u, target;
    unsigned bodies = 0, scanned = 0;  // bodies that shortened the path / bodies considered
};
// P: Link now; T: the target root; others: every other Ganado (target_ending Others()).
// faced: the takeoff yaw was written (spin_attack_runtime.h); without it D = 0.
inline Course Plan(const Config& c, Body link, Body target, unsigned strike_tick, bool faced,
                   const Body* others, unsigned count) {
    Course k;
    if (!c.On() && !c.Observe()) return k;
    k.why = Why::BadInput;
    if (!Finite(link) || !Finite(target)) return k;
    const float dx = target.x - link.x, dz = target.z - link.z;
    k.d = LenXZ(dx, dz); k.dy = target.y - link.y;
    k.p0 = {link.x, link.z}; k.y0 = link.y; k.target = {target.x, target.z};
    if (!std::isfinite(k.d) || !(k.d >= 1.f)) return k;
    k.u = {dx / k.d, dz / k.d};
    const float max_travel = c.MaxTravel();
    k.raw = k.d - c.standoff;
    if (!(k.raw > 0.f)) k.raw = 0.f; else if (k.raw > max_travel) k.raw = max_travel;
    k.land = strike_tick > c.land_margin ? float(strike_tick - c.land_margin) : 0.f;
    if (!faced) { k.why = Why::NoFace; return k; }
    if (!(k.d > c.strike ? c.beyond : c.at_strike).Holds(k.dy)) { k.why = Why::Height; return k; }
    if (!(k.land > 0.f)) { k.why = Why::NoTime; return k; }
    float travel = k.raw;
    for (unsigned i = 0; others && i < count; ++i) {
        const Body& b = others[i];
        if (!Finite(b)) continue;
        ++k.scanned;
        const float bx = b.x - link.x, bz = b.z - link.z;
        const float along = bx * k.u.x + bz * k.u.z, side = std::fabs(bx * k.u.z - bz * k.u.x);
        // Height against the path at that point (17-oct, leaping down): Link's Y plus the target's dy times along / d.
        const float t = along > 0.f ? (along < k.d ? along / k.d : 1.f) : 0.f;
        if (!(std::fabs(b.y - (link.y + k.dy * t)) < c.body_dy)) continue;
        if (along > 0.f && along <= k.raw && side < c.body_clear) {
            ++k.bodies;
            if (along - c.body_clear < travel) travel = along - c.body_clear;
        }
    }
    if (!(travel >= c.min_travel)) { k.why = k.bodies && k.raw >= c.min_travel ? Why::Bodies : Why::Short; return k; }
    k.travel = travel; k.why = Why::Ok;
    return k;
}
// Corridor guard (open finding a, 16-oct night): run at the knife press (spin_attack_runtime.h SwordArbiter),
// before anything replaces the native knife slash, with the press's bodies and Plan's own arithmetic. Another
// Ganado counted on the path stops the leap body_clear short of it, so with the default standoff 900 and
// clear 650 the landing is >= 1550 from the target, beyond the 1200 strike; why=bodies leaves Link where he
// stands. When that landing is beyond the strike reach the press is refused (refuse) and the native slash
// plays (ending_none reason=bodies). A landing within the reach (an in-place jump at <= 1200, a wider
// EndingBlowStrikeRange) is kept. Only EndingBlowLeap=1: observe never moves Link.
struct Guard { bool refuse = false; Course k; float land = 0; };
inline Guard Corridor(const Config& c, Body link, Body target, unsigned strike_tick, const Body* others, unsigned count) {
    Guard g;
    if (!c.On()) return g;
    g.k = Plan(c, link, target, strike_tick, true, others, count);
    if (!g.k.bodies || !Finite(g.k.u)) return g;
    const float travel = g.k.why == Why::Ok ? g.k.travel : 0.f;
    g.land = LenXZ(target.x - (link.x + travel * g.k.u.x), target.z - (link.z + travel * g.k.u.z));
    // A kept landing must also leave the target within the strike band on the slope (dy x land / d, 17-oct review).
    g.refuse = !(g.land <= c.strike) || !c.at_strike.Holds(g.k.dy * (g.land / g.k.d));
    return g;
}

// ---- per tick ------------------------------------------------------------------------------------
struct Frame {
    uint32_t frame = 0;           // native logic frame (global+0x530C); a repeat is skipped
    double time = 0;              // game time in 60 Hz frames (game_clock Elapsed60)
    float step = 1.f;             // this tick in 60 Hz frames (2*deltaTime; 1 at 60 FPS)
    Body before, after;           // Link around the original MotionMove (before = last frame's collided position)
    bool collision = true;        // +0x2CE bit 0x100 (the scenery check runs this frame)
    bool target_ok = false;       // same actor/id/type, live slot, alive, still down (Grounded) or getting up (Rising)
    bool rising = false;          // target_ok because it is getting up (r1 0x1F): kept, counted in rise_e
    Body target;
};
// X/Z, plus Y only on a planned descent (write_y). `from` is the position the write expects.
struct Output { bool write = false, write_y = false; float x = 0, y = 0, z = 0; Body from; };
// Active: the leap moves Link. Settle: the leap stopped above the target's level; Y only (Descend).
enum class Phase : uint8_t { Off, Active, Settle };
enum class Stop : uint8_t { None, Contact, Blocked, Lateral, Height, Outside, TargetLost, TargetMoved, Time,
                            BadInput, WriteFailed, Interrupted, NoCollision };
inline const char* StopName(Stop s) {
    static const char* const n[] = {"none", "contact", "blocked", "lateral", "height", "outside", "target_lost",
                                    "target_moved", "time", "bad_input", "write_failed", "interrupted", "no_collision"};
    return n[unsigned(s)];
}
struct Summary {
    uint64_t lease = 0;
    Course course;
    unsigned ticks = 0, repeats = 0, capped = 0, lag_ticks = 0;
    double commanded = 0;         // sum of the commands written (C_prev)
    float achieved = 0;           // A = dot(before - P0, u) at the last tick
    float lateral = 0, dy = 0, max_lag = 0, d_end = 0, e = 0;  // dy: Link's Y now - at takeoff
    float off = 0;                // how far Link's Y left the takeoff..target span (0 inside it, signed: + above)
    float rise_e = -1;            // game frames since the start when the target was first seen getting up (-1 never)
    float peak = 0, peak_e = -1;  // fastest command per 60 Hz frame and the game frame of its tick (-1 none)
    float lowered = 0;            // Y the leap wrote down in all (Descend; the snap may have put some of it back)
    unsigned settle_ticks = 0;    // ticks of Settle after the stop
    unsigned free_ticks = 0;      // ticks that lowered at the full rate (the previous write survived: no floor)
    bool settle_after = false;    // Settle went on after the lease ended (M1)
    uint8_t settle_end = 0;       // SettleEnd: why the settle ended
    Stop stop = Stop::None;
};
enum class SettleEnd : uint8_t { None, Floor, Level, Time, NoCollision, BadInput, Aborted, Lost };
inline const char* SettleEndName(uint8_t e) {
    static const char* const n[] = {"none", "floor", "level", "time", "no_collision", "bad_input", "aborted", "lost"};
    return e < sizeof(n) / sizeof(n[0]) ? n[e] : "?";
}

class Leap {
public:
    explicit Leap(Config c = {}) : c_(c) {}
    const Config& config() const { return c_; }
    Phase phase() const { return phase_; }
    uint64_t lease() const { return phase_ == Phase::Off ? 0 : s_.lease; }
    const Summary& summary() const { return s_; }
    bool Moving() const { return phase_ == Phase::Active; }
    // Start frame (decide, after the clip started and the yaw was written). start: game time now.
    bool Begin(uint64_t lease, const Course& k, double start) {
        s_ = Summary{}; s_.lease = lease; s_.course = k; phase_ = Phase::Off;
        if (!c_.On() || !lease || k.why != Why::Ok || !(k.travel > 0.f) || !Finite(k.u) || !Finite(k.p0) ||
            !std::isfinite(k.y0) || !(k.land > 0.f) || !std::isfinite(start)) return false;
        start_ = start; frame_known_ = false; have_ref_ = false; have_target_ = false; last_cmd_ = 0; last_rate_ = 0; lag_run_ = 0;
        hold_ = 0; have_settle_ = false; settle_y_ = 0; wrote_y_ = false; last_y_ = 0; detached_ = false;
        owner_ = 0; room_ = 0; have_settle_start_ = false; settle_start_ = 0;
        phase_ = Phase::Active;
        return true;
    }
    // After the dispatch's own MotionMove of our lease. One call per MotionMove.
    Output Step(const Frame& f) {
        Output o;
        if (phase_ == Phase::Off) return o;
        if (frame_known_ && f.frame == frame_) { ++s_.repeats; return o; }
        frame_known_ = true; frame_ = f.frame;
        if (phase_ == Phase::Settle) return SettleStep(f);
        const double e = f.time - start_;
        if (!Finite(f.before) || !Finite(f.after) || !std::isfinite(e) || e < -game_clock::kEpsilon ||
            !std::isfinite(f.step) || !(f.step > 0.f)) return Halt(Stop::BadInput), o;
        ++s_.ticks; s_.e = float(e);
        const Course& k = s_.course;
        const float bx = f.before.x - k.p0.x, bz = f.before.z - k.p0.z;
        s_.achieved = bx * k.u.x + bz * k.u.z;
        s_.lateral = std::fabs(bx * k.u.z - bz * k.u.x);
        s_.dy = f.before.y - k.y0;
        if (!f.target_ok || !Finite(f.target)) return Halt(Stop::TargetLost), o;
        if (f.rising && s_.rise_e < 0) s_.rise_e = float(e);
        if (have_target_ && LenXZ(f.target.x - target_.x, f.target.z - target_.z) > c_.target_step * f.step)
            return Halt(Stop::TargetMoved), o;
        target_ = f.target; have_target_ = true;
        s_.d_end = LenXZ(f.target.x - f.before.x, f.target.z - f.before.z);
        if (s_.d_end <= c_.standoff) return Halt(Stop::Contact), o;
        if (game_clock::Exceeded(e, double(k.land))) return Halt(Stop::Time), o;
        if (!f.collision) return Halt(Stop::NoCollision), o;
        // Relative to the planned descent (17-oct): the span between the takeoff Y and the target's, not the takeoff alone.
        const float lo = k.dy < 0.f ? k.dy : 0.f, hi = k.dy > 0.f ? k.dy : 0.f;
        s_.off = s_.dy < lo ? s_.dy - lo : s_.dy > hi ? s_.dy - hi : 0.f;
        if (std::fabs(s_.off) > c_.height) return Halt(Stop::Height), o;
        if (have_ref_ && LenXZ(f.before.x - ref_.x, f.before.z - ref_.z) > last_cmd_ + c_.outside_slack)
            return Halt(Stop::Outside), o;
        if (s_.lateral > c_.lateral) return Halt(Stop::Lateral), o;
        const float lag = float(s_.commanded) - s_.achieved;
        if (lag > s_.max_lag) s_.max_lag = lag;
        if (lag > c_.lag_max) return Halt(Stop::Blocked), o;
        // Per 60 Hz frame (review 16-oct night): lag is a running total, so a threshold on one tick's command
        // tripped on a single lasting loss of ~29 at 165 FPS (6.1 m leap) against ~63 at 60 FPS. The last
        // command's rate per 60 Hz frame gives the same tolerance at every frame rate; a wall still stops it
        // within ~1.5 game frames (lag grows by a whole command per tick there).
        // Burst: one reference, the mean rate D / (land - e0), over the takeoff hold and the whole window (the Time
        // stop ends it at land). The eased rate falls from a x the mean to (3 - a) / 2 x: a tail that followed it
        // would shrink toward lag_slack and trip on an earlier small loss (arc research rule 1), and one that followed
        // the peak (fix-08 review) accepted a lasting loss there and tripped Blocked ~20 game frames later on open
        // floor, far from its cause (the lag never shrinks). With the mean a loss is judged when it happens, as on the
        // linear leap; a wall at the peak still trips lag_max or two ticks of lag (it grows by ~a x the mean per
        // frame). The linear leap keeps its last rate (16-oct, bit for bit).
        const bool eased = c_.Burst() > 1.f;
        const float e0 = c_.Start(k.land);
        const double span = double(k.land) - double(e0), mean = double(k.travel) / span;
        const float ref = eased ? float(mean) : last_rate_;
        if (lag > c_.lag_ratio * ref + c_.lag_slack) {
            ++s_.lag_ticks;
            if (++lag_run_ >= 2) return Halt(Stop::Blocked), o;
        } else lag_run_ = 0;
        // Nothing is commanded up to the takeoff (e + step <= e0; kEpsilon absorbs the periodic 165 FPS sum), then
        // D * Curve(tau) on the same absolute clock, one tick ahead as the linear leap (whose tau is (e + step) / land).
        const double ahead = e + double(f.step) - double(e0);
        const double tau = eased && ahead <= game_clock::kEpsilon ? 0.0 : ahead / span;
        const double wanted = double(k.travel) * c_.Curve(tau);
        double delta = wanted - s_.commanded;
        const double cap = double(c_.CmdCap(k.travel, k.land)) * double(f.step);
        if (delta > cap) { delta = cap; ++s_.capped; }
        if (!(delta > 0.0)) delta = 0.0;
        last_cmd_ = float(delta); last_rate_ = float(delta / double(f.step));
        if (last_rate_ > s_.peak) { s_.peak = last_rate_; s_.peak_e = float(e); }
        s_.commanded += delta;
        ref_ = {f.after.x, f.after.z};
        if (delta > 0.0) {
            o.write = true; o.from = f.after;
            o.x = f.after.x + float(delta) * k.u.x; o.z = f.after.z + float(delta) * k.u.z;
            ref_ = {o.x, o.z};
        }
        have_ref_ = true;
        Descend(f, o, float(double(k.y0) + double(k.dy) * c_.Curve(tau)));
        return o;
    }
    // A failed write, or the lease ended (Interrupted: finished, cancelled or abandoned). True when a moving leap stopped
    // (its summary is new). The lease ending on a descent keeps (or starts) Settle (M1, review 24-oct: a hit, a grab or
    // the clip's end part way down a deep drop left Link hovering out of the snap's reach); any other abort ends it.
    bool Abort(Stop why) {
        const bool was = phase_ == Phase::Active;
        Halt(why);
        if (why == Stop::Interrupted && phase_ == Phase::Settle) { detached_ = true; s_.settle_after = true; }
        else End(SettleEnd::Aborted);
        return was;
    }
    // After the lease: the player and room the Settle belongs to (the runtime's gate drops it for any other).
    void Bind(uint32_t player, uint32_t room) { owner_ = player; room_ = room; }
    bool Detached() const { return phase_ == Phase::Settle && detached_; }
    bool SettlesFor(uint32_t player, uint32_t room) const { return Detached() && player && player == owner_ && room == room_; }
    // Ends a Settle (the runtime gate: another player or room, no gameplay) and nothing else. Phase Off.
    void End(SettleEnd why) {
        if (phase_ == Phase::Settle && !s_.settle_end) s_.settle_end = uint8_t(why);
        phase_ = Phase::Off; detached_ = false;
    }
    // The planned descent is deep enough for Descend (dy below -descend_from).
    bool Descent() const { return s_.course.dy < -c_.descend_from; }

private:
    // A stop. Link may still be above the target's level after a safe stop: Settle brings him down (Y only).
    void Halt(Stop why) {
        if (phase_ != Phase::Active) return;
        s_.stop = why; phase_ = Phase::Off;
        // Interrupted (the lease ended, M1): the clip finished or the game took Link (a hit, a grab); the snap still runs.
        const bool safe = why == Stop::Contact || why == Stop::Time || why == Stop::Blocked || why == Stop::Lateral ||
                          why == Stop::Height || why == Stop::TargetLost || why == Stop::TargetMoved || why == Stop::Interrupted;
        if (safe && Descent()) { phase_ = Phase::Settle; hold_ = 0; have_settle_ = false; }
    }
    // Y toward `goal` (never up), with the scenery check on only: the snap that follows on the same frame puts Link
    // back on any floor, so this only takes him down where there is none within the snap's reach. Free: the Y written
    // last tick survived the snap (Link's collided Y is still it), so there was no floor under him; otherwise (the first
    // tick, a floor caught him, anything else moved him) only a probe (M2).
    void Descend(const Frame& f, Output& o, float goal) {
        const bool free = wrote_y_ && std::fabs(f.before.y - last_y_) <= c_.fall_proof;
        wrote_y_ = false;
        if (!Descent() || !f.collision || !std::isfinite(goal) || !(f.after.y > goal + 1.f)) return;
        float drop = c_.fall_rate * f.step;
        const float cap = free ? c_.fall_tick_max : c_.fall_probe;
        if (!(drop <= cap)) drop = cap;
        const float y = f.after.y - drop > goal ? f.after.y - drop : goal;
        if (!(y < f.after.y)) return;
        if (!o.write) { o.x = f.after.x; o.z = f.after.z; }
        o.write_y = true; o.y = y; o.from = f.after;
        s_.lowered += f.after.y - y;
        if (free) ++s_.free_ticks;
        wrote_y_ = true; last_y_ = y;
    }
    Output SettleStep(const Frame& f) {
        Output o;
        const float goal = s_.course.y0 + s_.course.dy;
        if (!Finite(f.before) || !Finite(f.after) || !std::isfinite(f.step) || !(f.step > 0.f) || !std::isfinite(f.time))
            return End(SettleEnd::BadInput), o;
        if (!f.collision) return End(SettleEnd::NoCollision), o;
        if (!(f.after.y > goal + 1.f)) return End(SettleEnd::Level), o;
        if (!have_settle_start_) { have_settle_start_ = true; settle_start_ = f.time; }
        if (game_clock::Exceeded(f.time - settle_start_, double(c_.settle_max))) return End(SettleEnd::Time), o;
        ++s_.settle_ticks;
        // Not lower than last tick: the snap holds him on a floor (or something else moves him); a few ticks in a row end it.
        if (have_settle_ && !(f.before.y < settle_y_ - 1.f)) { if (++hold_ >= c_.settle_hold) return End(SettleEnd::Floor), o; }
        else hold_ = 0;
        settle_y_ = f.before.y; have_settle_ = true;
        Descend(f, o, goal);
        return o;
    }
    Config c_;
    Phase phase_ = Phase::Off;
    Summary s_;
    double start_ = 0;
    uint32_t frame_ = 0;
    bool frame_known_ = false, have_ref_ = false, have_target_ = false;
    float last_cmd_ = 0, last_rate_ = 0;  // last command: this tick's units / per 60 Hz frame
    unsigned lag_run_ = 0;
    unsigned hold_ = 0;           // Settle: ticks in a row that did not bring Link down
    bool have_settle_ = false;
    float settle_y_ = 0;          // Settle: Link's collided Y at the last tick
    bool wrote_y_ = false;        // Descend wrote Y on the previous tick (last_y_): the next tick's proof of no floor
    float last_y_ = 0;
    bool detached_ = false;       // Settle goes on after the lease ended, for owner_ in room_
    uint32_t owner_ = 0, room_ = 0;
    bool have_settle_start_ = false;
    double settle_start_ = 0;     // game time of the first Settle tick (settle_max)
    Vec2 ref_;                    // where Link was left last tick (written or the clip's own `after`)
    Body target_;
};

// MotionMove ownership (link_runtime.cpp CmbLeapOwns): this call is the dispatch's own api.move of
// `lease` inside Runtime::Handle. Pure over the Runtime so the dispatch test drives the real one.
template <class Run> bool MoveOwner(const Run& run, const void* model, uint64_t lease) {
    return lease && run.busy && !run.observing && run.active && run.current && run.current == model &&
           run.lease.token == lease && run.Owns(run.current);
}

// ---- memory helpers (Memory: Read(addr, out, n) / Write(addr, in, n) / Writable(addr, n)) ----
template <class Memory> bool ReadPos(Memory& m, uint32_t player, Body& out) {
    float p[3]{};
    if (!player || !m.Read(player + layout::pos_x, p, sizeof(p))) return false;
    out = {p[0], p[1], p[2]};
    return Finite(out);
}
template <class Memory> bool ReadCollision(Memory& m, uint32_t player) {
    uint16_t flags = 0;
    return player && m.Read(player + layout::collision, &flags, sizeof(flags)) && (flags & layout::collision_bit);
}
// Strafe adapter pattern (ztarget_strafe_adapter.h Pose): read, preflight the whole committed
// writable span, read again; both reads must still be the position Step saw. Then X and Z (write), and Y (write_y).
template <class Memory> bool Apply(Memory& m, uint32_t player, const Output& o) {
    if (!o.write && !o.write_y) return true;
    float a[3]{}, b[3]{};
    if (!player || !std::isfinite(o.x) || !std::isfinite(o.z) || (o.write_y && !std::isfinite(o.y)) ||
        !m.Read(player + layout::pos_x, a, sizeof(a)) ||
        !m.Writable(player + layout::pos_x, sizeof(a)) || !m.Read(player + layout::pos_x, b, sizeof(b)) ||
        std::memcmp(a, b, sizeof(a)) || a[0] != o.from.x || a[1] != o.from.y || a[2] != o.from.z) return false;
    if (o.write && !(m.Write(player + layout::pos_x, &o.x, 4) && m.Write(player + layout::pos_z, &o.z, 4))) return false;
    return !o.write_y || m.Write(player + layout::pos_y, &o.y, 4);
}

// ---- log lines -----------------------------------------------------------------------------------
inline const char* ModeName(const Config& c) { return c.On() ? "on" : c.Observe() ? "observe" : "off"; }
// Burst as the INI percent (EndingBlowLeapBurst), after the 1..3 clamp.
inline unsigned BurstPercent(const Config& c) { return unsigned(c.Burst() * 100.f + 0.5f); }
// takeoff= is e0 for this plan's window (0: linear, or no window); live/regression.py reads the line up to d=.
// down= / up=: the height band Plan used for this d (17-oct).
inline int FormatPlan(char* out, size_t n, uint64_t lease, const Config& c, const Course& k, bool locked) {
    return std::snprintf(out, n,
        "ending_leap plan lease=%llu mode=%s prefer=%s why=%s d=%.0f dy=%.0f raw=%.0f travel=%.0f bodies=%u "
        "scanned=%u standoff=%.0f land=%.0f burst=%u takeoff=%.0f u=(%.3f,%.3f) p0=(%.0f,%.0f) target=(%.0f,%.0f) down=%.0f up=%.0f",
        static_cast<unsigned long long>(lease), ModeName(c), locked ? "lock" : "near", WhyName(k.why), k.d, k.dy,
        k.raw, k.travel, k.bodies, k.scanned, c.standoff, k.land, BurstPercent(c), double(c.Start(k.land)), k.u.x, k.u.z,
        k.p0.x, k.p0.z, k.target.x, k.target.z, double(k.d > c.strike ? c.beyond.down : c.at_strike.down),
        double(k.d > c.strike ? c.beyond.up : c.at_strike.up));
}
// peak= / peak_e=: the fastest command per 60 Hz frame and the game frame of its tick (the burst right after takeoff).
// off=: how far Link's Y left the takeoff..target span at the last tick (the height stop's measure, 17-oct).
// lowered=: Y the descent wrote down so far (24-oct; 0 on a level leap).
inline int Format(char* out, size_t n, const Summary& s) {
    return std::snprintf(out, n,
        "ending_leap stop=%s lease=%llu travelled=%.0f commanded=%.0f planned=%.0f ticks=%u repeats=%u capped=%u "
        "lag_ticks=%u max_lag=%.0f lateral=%.0f dy=%.0f d_end=%.0f e=%.1f land=%.0f rising=%u rise_e=%.1f peak=%.1f "
        "peak_e=%.2f off=%.0f lowered=%.0f",
        StopName(s.stop), static_cast<unsigned long long>(s.lease), s.achieved, s.commanded, s.course.travel, s.ticks,
        s.repeats, s.capped, s.lag_ticks, s.max_lag, s.lateral, s.dy, s.d_end, s.e, s.course.land, s.rise_e >= 0 ? 1U : 0U,
        double(s.rise_e), double(s.peak), double(s.peak_e), double(s.off), double(s.lowered));
}
// The Settle that followed a stop (24-oct): its ticks and the Y lowered in all (leap + settle); after_lease=1 when it
// went on after the lease ended (M1), free= the ticks at the full rate (M2), end= why it ended.
inline int FormatSettle(char* out, size_t n, const Summary& s) {
    return std::snprintf(out, n, "ending_leap settle lease=%llu ticks=%u lowered=%.0f target_dy=%.0f after_lease=%u free=%u end=%s",
        static_cast<unsigned long long>(s.lease), s.settle_ticks, double(s.lowered), double(s.course.dy),
        s.settle_after ? 1U : 0U, s.free_ticks, SettleEndName(s.settle_end));
}
}  // namespace link_runtime::ending_blow::leap

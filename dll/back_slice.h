#pragma once
// Back Slice (Tajo trasero, TP hidden skill; docs/dll-backslice-song-01-2026-10-18.md). Pure: no hooks, no process
// access. Runtime glue in spin_attack_runtime.h (BashArbiter / SwordDecide) and link_runtime.cpp (CmbBackMove).
//
// Control (no new key): with a Ganado Z-locked (hold the Z key: Left Ctrl, or the pad's ZTargetPadButton, LT by
// default) and strafing sideways (A or D held; the left stick sideways sets the same native RIGHT/LEFT bits), TAP the
// knife button (the Shield Bash's tap, shield_bash.h Tap: press and release within 250 ms). The side held at the
// press (else at the release) picks the arc: D / stick right (native bit 4, Side -1) carries Link toward his right
// (the sword hand, model -X), the strafe's own sense; A / stick left (bit 8, Side +1) the other way. Without A/D the tap stays the Shield Bash; holding the knife stays the spin charge,
// knife + attack the Ending Blow; the tap still opens the parry window as before. One per kCooldownFrames (1 s).
//
// Motion: the spin attack's clip (spin_attack_assets.h) is the slash; during its first arc_frames game frames Link
// is carried ~180 degrees round the target at its live position, radius = the distance at the press clamped to
// min_radius..max_radius (1..3 m), eased (smootherstep), facing the target every tick. Written as the Ending Blow
// leap writes (ending_blow_leap.h Apply): X/Z and the yaw only, right after the dispatch's own MotionMove of our lease,
// so cPlayer::move runs the body overlap, scenery and collision checks on that position the same frame. A tick
// whose collided position falls more than kBlockSlack short of the last command two ticks running (a wall, another
// body) ends the arc there (fail closed, like the leap); the slash still plays and the strike rechecks its reach.
//
// Strike (at max(clip strike tick, arc_frames + 2)): one native knife hit on the locked target only, Kind::Scaled
// BackSlicePermille (default 1500 = 1.5x the native knife amount; head/core part: ordinary, sword_actions::HitQueue).
// The record goes straight to a body part of the Ganado through the native publisher (0x1AEEF0) with `from` = Link
// behind it: the knife sweep, and with it a wooden shield's or a guard's frontal block, is never consulted. The
// native knife reaction (0x58B90) staggers the Ganado. The heavy-target damage cap stays as for every sword action.
// Keys in [LinkRuntime], read with SpinAttack=1 only (the clip): BackSlice missing or exact 1 = on (Donald 18-oct:
// default on), exact 0 = off; BackSlicePermille 1000..3000, BackSliceArcFrames 10..40, BackSliceMaxRadius 1500..3000.
//
// TP motion (backslice-tp-01, BackSliceMotion missing or 1 = on, 0 = the spin clip above): the clip is TP's own Back
// Slice (scripts/back_slice_anim.py): the side roll round the enemy (procTurnMove, mrolll/mrollr), the twirl jump slash
// (procCutFinishJumpUp, cutedst) and its landing (cuteded), one clip per side. The arc then lasts the roll (the clip's
// arc ticks, 40 = TP's 20 roll frames; BackSliceArcFrames is not used) and follows TP's turn curve (TpShare) instead of
// the smootherstep; the strike is the clip's (the twirl's sword sweep across the front, after the arc). Everything else
// (control, cooldown, radius, fail-closed arc, the hit) is the same.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "game_clock.h"

namespace link_runtime::back_slice {
inline constexpr double kCooldownFrames = 60.0;   // 1 s of game time
inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kBlockSlack = 180.f;       // mm short of the command (after collision) that counts as blocked
inline constexpr float kHeight = 400.f;           // Link's Y may leave the press Y by this much
inline constexpr float kTargetStep = 250.f;       // the target may move this far per 60 Hz frame
// TP's turn (dusklight d_a_alink_guard.inc procTurnMove, daAlinkHIO_turnMove_c0): the remaining angle +-0x7FF0 closes
// each 30 Hz TP frame by cLib_addCalcAngleS(rem, 0, 5, 2500, 200); the twirl starts once the roll passes frame 19.
inline constexpr int kTpTurn = 0x7FF0, kTpRate = 5, kTpMaxStep = 2500, kTpMinStep = 200;
inline constexpr unsigned kTpRollFrames = 20;
// Remaining turn after `frames` TP frames (C s16 arithmetic: the division truncates toward zero).
inline int TpTurnLeft(unsigned frames) {
    int rem = kTpTurn;
    for (unsigned i = 0; i < frames && rem; ++i) {
        int step = -rem / kTpRate;
        if (step > kTpMinStep || step < -kTpMinStep) rem += step > kTpMaxStep ? kTpMaxStep : step < -kTpMaxStep ? -kTpMaxStep : step;
        else rem = rem - kTpMinStep <= 0 ? 0 : rem - kTpMinStep;
    }
    return rem;
}
// Share of the arc at tau (0..1 of the roll): TP's turn done after tau * 20 TP frames over the turn done at the twirl
// (175 of 180 degrees), linear between TP frames. Full speed at once (13.7 degrees per TP frame for 9 frames), then
// slowing, as TP's roll.
inline double TpShare(double tau) {
    if (!(tau > 0.0)) return 0.0;
    if (tau >= 1.0) return 1.0;
    const double f = tau * kTpRollFrames;
    const unsigned i = unsigned(f);
    const double a = kTpTurn - TpTurnLeft(i), b = kTpTurn - TpTurnLeft(i + 1), end = kTpTurn - TpTurnLeft(kTpRollFrames);
    return (a + (b - a) * (f - double(i))) / end;
}
enum class Curve : uint8_t { Smooth, Tp };
struct Config {
    bool on = true;
    bool motion = true;             // BackSliceMotion: TP's clip (runtime: only when its files loaded)
    Curve curve = Curve::Smooth;
    unsigned permille = 1500, arc_frames = 20, min_radius = 1000, max_radius = 3000;
    unsigned reach = 3600;          // press: the locked target within max_radius + 600; strike: within this of Link
    float band = 700.f;             // |dy| allowed at the press
    unsigned StrikeTick(unsigned clip_tick) const { return clip_tick > arc_frames + 2 ? clip_tick : arc_frames + 2; }
};
// The arc on TP's clip: the roll's length (the clip's arc ticks) and TP's curve. A zero arc keeps the config as it is.
inline Config WithClip(Config c, unsigned arc_ticks) {
    if (arc_ticks) { c.arc_frames = arc_ticks; c.curve = Curve::Tp; }
    return c;
}
// Native RIGHT (4) / LEFT (8) bits of the input word (both set or neither: 0). re4-decomp pad.cpp Key_type_tbl: key bit
// 4 = 0x00020002 = the pad's right (JOY_RIGHT) or the stick pushed right (x > 0 sets the bit the decomp calls
// JOY_SLEFT; REP_RIGHT pairs them); bit 8 = left. Keyboard D is bit 4 (live log 19-oct: D held -> side=-1).
// Side: -1 = RIGHT input, +1 = LEFT input, the sign ztarget_strafe.h's pos += side * (uz, -ux) wants: with u the unit
// toward the target and Link's yaw facing it (RotMatrix Ry: model +Z -> (sin, cos), model -X -> (-cos, sin)), side -1
// moves along model -X, Link's right (the knife hand, part 10, is at bind X -704).
inline int Side(uint64_t keys) { return (keys & 12) == 4 ? -1 : (keys & 12) == 8 ? 1 : 0; }
// TP's roll clip for a side (backslice-tp-02): the clip that rolls the way the arc carries Link. back_slice_right
// (TP mrolll, mirrored) tips the body toward model -X (Link's right: head 402 BIN, spine 0.86 toward -X at game frame
// 8; scripts/back_slice_anim.py audit), back_slice_left (mrollr) toward +X. Side -1 (D) travels toward -X: the right
// clip. backslice-tp-01 had it the other way round (side > 0 -> right), so D rolled Link toward his left.
enum class Roll : uint8_t { Right, Left };
inline constexpr int kRightClipRollX = -1;   // model X sign the back_slice_right clip rolls toward (Link's right)
inline Roll ClipRoll(int side) { return side < 0 ? Roll::Right : Roll::Left; }

struct Vec { float x = 0, y = 0, z = 0; };
inline bool Finite(const Vec& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// ---- press -------------------------------------------------------------------------------------
enum class Why : uint8_t { Ok, Off, NoSide, NoLock, Far, Height, Cooldown, Busy, BadInput };
inline const char* WhyName(Why w) {
    static const char* const n[] = {"ok", "off", "no_side", "no_lock", "far", "height", "cooldown", "busy", "bad_input"};
    return n[unsigned(w)];
}
struct Plan {
    Why why = Why::Off;
    float d = 0, dy = 0, radius = 0, theta0 = 0, sweep = 0;   // sweep: signed, -side * pi
    int side = 0;
};
// link/target: positions at the press; locked: a Z-locked target was matched in the combat pool.
// last/now: game time of the last back slice (have_last) and now.
inline Plan Decide(const Config& c, int side, bool locked, Vec link, Vec target, bool have_last, double last, double now) {
    Plan p;
    if (!c.on) return p;
    p.side = side;
    if (!side) { p.why = Why::NoSide; return p; }
    if (!locked) { p.why = Why::NoLock; return p; }
    if (!Finite(link) || !Finite(target) || !std::isfinite(now)) { p.why = Why::BadInput; return p; }
    if (have_last && now >= last - game_clock::kEpsilon && !game_clock::Reached(now - last, kCooldownFrames)) { p.why = Why::Cooldown; return p; }
    const float dx = link.x - target.x, dz = link.z - target.z;
    p.d = std::sqrt(dx * dx + dz * dz); p.dy = target.y - link.y;
    if (!(p.d <= float(c.max_radius) + 600.f)) { p.why = Why::Far; return p; }
    if (!(std::fabs(p.dy) <= c.band)) { p.why = Why::Height; return p; }
    p.radius = p.d < float(c.min_radius) ? float(c.min_radius) : p.d > float(c.max_radius) ? float(c.max_radius) : p.d;
    p.theta0 = p.d >= 1.f ? std::atan2(dx, dz) : 0.f;   // Link = target + r (sin t, cos t)
    // The strafe's sense (ztarget_strafe.h: side +1, LEFT, moves along (uz, -ux), u towards the target) lowers theta;
    // side -1 (RIGHT, D) raises it: Link goes toward his right (model -X).
    p.sweep = -float(side) * kPi;
    p.why = Why::Ok;
    return p;
}
// Share of the sweep at tau (0..1): smootherstep, 0 and 1 at the ends with zero speed and acceleration there.
inline double Ease(double tau) {
    if (!(tau > 0.0)) return 0.0;
    if (tau >= 1.0) return 1.0;
    return tau * tau * tau * (tau * (tau * 6.0 - 15.0) + 10.0);
}
// The arc's share at tau for the config's curve: smootherstep (the spin clip) or TP's turn (the TP clip).
inline double Share(Curve c, double tau) { return c == Curve::Tp ? TpShare(tau) : Ease(tau); }

// ---- per tick ----------------------------------------------------------------------------------
struct Frame {
    uint32_t frame = 0; double time = 0; float step = 1.f;
    Vec before, after;              // Link last frame (collided) and after this tick's MotionMove
    bool collision = true;          // +0x2CE bit 0x100 (the scenery check runs this frame)
    bool target_ok = false; Vec target;
};
struct Output { bool write = false; float x = 0, z = 0, yaw = 0; Vec from; };
enum class Stop : uint8_t { None, Done, Blocked, Height, TargetLost, TargetMoved, BadInput, WriteFailed, Interrupted, NoCollision };
inline const char* StopName(Stop s) {
    static const char* const n[] = {"none", "done", "blocked", "height", "target_lost", "target_moved", "bad_input",
                                    "write_failed", "interrupted", "no_collision"};
    return n[unsigned(s)];
}
struct Summary {
    uint64_t lease = 0; Plan plan; unsigned ticks = 0, repeats = 0, lag_ticks = 0;
    float swept = 0;                // radians actually commanded (signed)
    float max_lag = 0, end_d = 0, end_angle = 0, e = 0;   // end_angle: Link's angle round the target vs theta0
    Stop stop = Stop::None;
};
class Circle {
public:
    explicit Circle(Config c = {}) : c_(c) {}
    const Config& config() const { return c_; }
    bool Active() const { return on_; }
    uint64_t lease() const { return on_ ? s_.lease : 0; }
    const Summary& summary() const { return s_; }
    bool Begin(uint64_t lease, const Plan& p, double start, float y0) {
        s_ = Summary{}; s_.lease = lease; s_.plan = p; on_ = false;
        if (!lease || p.why != Why::Ok || !std::isfinite(start) || !std::isfinite(y0) || !c_.arc_frames) return false;
        start_ = start; y0_ = y0; known_ = false; have_cmd_ = false; lag_run_ = 0; have_target_ = false;
        on_ = true; return true;
    }
    Output Step(const Frame& f) {
        Output o;
        if (!on_) return o;
        if (known_ && f.frame == frame_) { ++s_.repeats; return o; }
        known_ = true; frame_ = f.frame;
        const double e = f.time - start_;
        if (!Finite(f.before) || !Finite(f.after) || !std::isfinite(e) || e < -game_clock::kEpsilon ||
            !std::isfinite(f.step) || !(f.step > 0.f)) return Halt(Stop::BadInput), o;
        ++s_.ticks; s_.e = float(e);
        if (!f.target_ok || !Finite(f.target)) return Halt(Stop::TargetLost), o;
        if (have_target_ && std::hypot(f.target.x - target_.x, f.target.z - target_.z) > kTargetStep * f.step)
            return Halt(Stop::TargetMoved), o;
        target_ = f.target; have_target_ = true;
        const float rx = f.before.x - f.target.x, rz = f.before.z - f.target.z;
        s_.end_d = std::sqrt(rx * rx + rz * rz);
        s_.end_angle = std::atan2(rx, rz) - s_.plan.theta0;
        if (std::fabs(f.before.y - y0_) > kHeight) return Halt(Stop::Height), o;
        if (!f.collision) return Halt(Stop::NoCollision), o;
        if (have_cmd_) {   // where the last command left Link vs where the collision put him (target-relative)
            const float lag = std::hypot(f.before.x - (target_.x + cmd_x_), f.before.z - (target_.z + cmd_z_));
            if (lag > s_.max_lag) s_.max_lag = lag;
            if (lag > kBlockSlack) { ++s_.lag_ticks; if (++lag_run_ >= 2) return Halt(Stop::Blocked), o; }
            else lag_run_ = 0;
        }
        const double tau = (e + double(f.step)) / double(c_.arc_frames);   // one tick ahead, as the leap
        const float share = float(Share(c_.curve, tau));
        const float theta = s_.plan.theta0 + share * s_.plan.sweep;
        const float r = s_.plan.d + (s_.plan.radius - s_.plan.d) * share;   // no jump when d is clamped
        cmd_x_ = r * std::sin(theta); cmd_z_ = r * std::cos(theta); have_cmd_ = true;
        s_.swept = theta - s_.plan.theta0;
        o.write = true; o.from = f.after;
        o.x = f.target.x + cmd_x_; o.z = f.target.z + cmd_z_;
        o.yaw = std::atan2(f.target.x - o.x, f.target.z - o.z);   // face the target (cPlayer yaw at +0xA4)
        if (tau >= 1.0) Halt(Stop::Done);   // this is the last write
        return o;
    }
    bool Abort(Stop why) { const bool was = on_; Halt(why); return was; }
private:
    void Halt(Stop why) { if (on_) { on_ = false; s_.stop = why; } }
    Config c_; Summary s_; bool on_ = false, known_ = false, have_cmd_ = false, have_target_ = false;
    double start_ = 0; float y0_ = 0, cmd_x_ = 0, cmd_z_ = 0; uint32_t frame_ = 0; unsigned lag_run_ = 0; Vec target_;
};

// ---- memory (Memory: Read / Write / Writable, as ending_blow_leap.h Apply) -----------------------
// Re-reads Link's position (must still be o.from) before writing X, Z and the yaw (+0xA4).
template <class Memory> bool Apply(Memory& m, uint32_t player, const Output& o) {
    if (!o.write) return true;
    float a[3]{}, b[3]{};
    if (!player || !std::isfinite(o.x) || !std::isfinite(o.z) || !std::isfinite(o.yaw) || !m.Read(player + 0x94, a, sizeof(a)) ||
        !m.Writable(player + 0x94, 0x14) || !m.Read(player + 0x94, b, sizeof(b)) || std::memcmp(a, b, sizeof(a)) ||
        a[0] != o.from.x || a[1] != o.from.y || a[2] != o.from.z) return false;
    return m.Write(player + 0x94, &o.x, 4) && m.Write(player + 0x9C, &o.z, 4) && m.Write(player + 0xA4, &o.yaw, 4);
}

// ---- log ---------------------------------------------------------------------------------------
inline int FormatEnd(char* out, size_t n, const Summary& s) {
    return std::snprintf(out, n, "back_slice arc stop=%s lease=%llu ticks=%u repeats=%u swept_deg=%.0f planned_deg=%.0f radius=%.0f "
        "end_d=%.0f end_deg=%.0f max_lag=%.0f lag_ticks=%u e=%.1f", StopName(s.stop), static_cast<unsigned long long>(s.lease),
        s.ticks, s.repeats, double(s.swept) * 57.29578, double(s.plan.sweep) * 57.29578, double(s.plan.radius), double(s.end_d),
        double(s.end_angle) * 57.29578, double(s.max_lag), s.lag_ticks, double(s.e));
}
}  // namespace link_runtime::back_slice

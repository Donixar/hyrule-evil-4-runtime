#pragma once
// Backflip (backflip-01, 30-oct; docs/backflip-tp-2026-10-30.md). Pure: no hooks, no process access. Runtime glue in
// spin_attack_runtime.h (BashArbiter / FlipRequest / SwordDecide) and link_runtime.cpp (CmbFlipMove).
//
// Donald 30-oct: «al shieldear se siente como Link está begging por tener la opción de hacer un backflip tipo Link».
// TP: Z-targeting, back on the stick + jump (A) is procBackJumpInit(0) (dusklight d_a_alink.cpp:16896): ANM_BACKFLIP
// (bcks/rollbst.bck) at 0.7, mNormalSpeed = mBackflipSpeedH 24, speed.y = mBackflipSpeedV 10, voice Z2SE_AL_V_BACKTEN,
// current.angle.y = shape_angle.y + 0x8000 (straight back, still facing forward); procBackJump keeps the speed
// (backJumpSpeedDec only caps it to 24 * cos(slope) on the ground) until the animation ends, then procBackJumpLandInit
// (ANM_BACKFLIP_LAND, bcks/rollbed.bck from frame 2) stops him (mNormalSpeed 0). daAlinkHIO_backJump_c0::m:
// d_a_alink_HIO_data.inc:150.
//
// Control (no new key, Backflip=1): with the Z key held (Left Ctrl or ZTargetPadButton; a Z-locked Ganado or the neutral
// Z-target), S (native back bit 2, the stick pulled back) and TAP the knife button (the Shield Bash's tap,
// shield_bash.h Tap). The keys held at the press, else at the release, pick the move (Pick): A/D with a lock to circle
// = the Back Slice (as before; it keeps priority on a diagonal), S = the backflip, anything else (none, W) = the Shield
// Bash as before. Holding the knife stays the spin charge; the tap still opens the parry window. One per
// kCooldownFrames (1 s). With Backflip missing or 0 nothing here runs and the tap is exactly as before.
//
// Motion: TP's clip (scripts/backflip_anim.py: the flip, then the landing; one link_backflip.fcv/.seq + voice) plays
// through the dispatch; during its first travel_ticks game frames (26 = the flip's 9 / 0.7 TP frames) Link is carried
// straight back from his facing at the press (with a lock: straight away from the locked Ganado) at a constant speed,
// distance BackflipDistance mm (default kDefaultDistance; TP's own 24 x 12.86 TP units would be 3.9 m at the frozen
// calibration's scale, too far for RE4's rooms), as TP's mNormalSpeed. Written as the Back Slice writes (back_slice.h
// Apply): X/Z and the yaw only, right after the dispatch's own MotionMove of our lease, so cPlayer::move runs the body
// overlap, scenery and collision checks on that position the same frame. The yaw faces the locked Ganado while it lives
// (else stays as at the press). A tick whose collided position falls more than kBlockSlack short of the last command
// two ticks running (a wall, another body) ends the travel there (fail closed); so do a height change beyond kHeight
// (a ledge: the native fall takes over) and a frame without the scenery check.
// Landing (backflip-02, 30-oct; Donald: «hay un mini delay post caída de lag, quizás quitar ese lag»): TP lets the
// player act from mLandAnm's cancel frame (3 of rollbed, played from 2 at 1.0 = 1 TP frame = 2 game frames into the
// landing), not after its 15 TP frames. The lease ends there (Release: travel_ticks + BackflipLandFrames game frames,
// default kLandCancelFrames) as a normal finish, and the native idle / locomotion takes over from the landing pose.
// BackflipLandFrames >= land_ticks (the clip's whole landing, 30) = the clip plays to its end (backflip-01).
// No invulnerability: like the rest of the project, a dodge only works by moving away (i-frames could come later).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "back_slice.h"
#include "game_clock.h"

namespace link_runtime::backflip {
using back_slice::Vec;
using back_slice::Output;
using back_slice::Finite;
inline constexpr double kCooldownFrames = 60.0;   // 1 s of game time
inline constexpr float kBlockSlack = back_slice::kBlockSlack;
inline constexpr float kHeight = back_slice::kHeight;
inline constexpr unsigned kDefaultDistance = 2500, kMinDistance = 1000, kMaxDistance = 4000;   // BackflipDistance, mm
// BackflipLandFrames, game frames (60 Hz) of the landing kept after the flip: TP's cancel point by default; 30 = the
// clip's whole landing (rollbed 2..17 at 1.0 = 15 TP frames).
inline constexpr unsigned kLandCancelFrames = 2, kMaxLandFrames = 30;

struct Config {
    bool on = false;                // Backflip: exact 1 only (default off)
    unsigned distance = kDefaultDistance;
    unsigned land_frames = kLandCancelFrames;
    unsigned travel_ticks = 26;     // the clip's flip (sword_actions::assets::backflip_travel_ticks)
    unsigned land_ticks = 30;       // the clip's landing after the flip (sword_actions::assets::backflip_land_ticks)
};
inline Config WithClip(Config c, unsigned travel_ticks, unsigned land_ticks) {
    if (travel_ticks) c.travel_ticks = travel_ticks;
    if (land_ticks) c.land_ticks = land_ticks;
    return c;
}
// The lease ends early (before the clip's own end) only when fewer landing frames are kept than the clip has.
inline bool EarlyRelease(const Config& c) { return c.land_frames < c.land_ticks; }
inline double ReleaseTick(const Config& c) { return double(c.travel_ticks) + double(c.land_frames); }
// elapsed: game frames since the lease began (the dispatch's clock, as the strike tick).
inline bool Release(const Config& c, double elapsed) {
    return EarlyRelease(c) && std::isfinite(elapsed) && game_clock::Reached(elapsed, ReleaseTick(c));
}

// ---- input -------------------------------------------------------------------------------------
// Native UP (1) / DOWN (2) bits of the input word: re4-decomp pad.cpp Key_type_tbl key 0 = 0x00080008 (pad up or the
// stick pushed up), key 1 = 0x00040004 (down); the keyboard's W/S set them (ztarget_strafe_transport.h: "W/S held"
// = keys & 3). Back = DOWN alone.
inline bool Back(uint64_t keys) { return (keys & 3) == 2; }
enum class Tap : uint8_t { Bash, BackSlice, Backflip };
inline const char* TapName(Tap t) { static const char* const n[] = {"shield_bash", "back_slice", "backflip"}; return n[unsigned(t)]; }
// The knife tap's move. side: back_slice::Side at the press (else the release); back: Back() likewise;
// slice_target: the Back Slice is on and has a locked Ganado to circle. A/D + a target keeps the Back Slice (diagonals
// included), S the backflip (with or without a lock), the rest the Shield Bash.
inline Tap Pick(bool back_slice_on, int side, bool slice_target, bool backflip_on, bool back) {
    if (back_slice_on && side && slice_target) return Tap::BackSlice;
    if (backflip_on && back) return Tap::Backflip;
    return Tap::Bash;
}

// ---- press -------------------------------------------------------------------------------------
enum class Why : uint8_t { Ok, Off, Cooldown, BadInput };
inline const char* WhyName(Why w) { static const char* const n[] = {"ok", "off", "cooldown", "bad_input"}; return n[unsigned(w)]; }
struct Plan {
    Why why = Why::Off;
    float dir_x = 0, dir_z = 0;     // unit travel direction (horizontal)
    float yaw0 = 0;                 // Link's yaw for the flip (facing the target when locked)
    float distance = 0;
    bool locked = false;
};
// link: Link at the press; yaw: his yaw (+0xA4; model +Z -> (sin, cos)); locked/target: a Z-locked Ganado.
inline Plan Decide(const Config& c, Vec link, float yaw, bool locked, Vec target, bool have_last, double last, double now) {
    Plan p;
    if (!c.on) return p;
    if (!Finite(link) || !std::isfinite(yaw) || !std::isfinite(now) || (locked && !Finite(target))) { p.why = Why::BadInput; return p; }
    if (have_last && now >= last - game_clock::kEpsilon && !game_clock::Reached(now - last, kCooldownFrames)) { p.why = Why::Cooldown; return p; }
    const float dx = link.x - target.x, dz = link.z - target.z, d = std::sqrt(dx * dx + dz * dz);
    if (locked && d >= 1.f) {       // straight away from the target, facing it
        p.dir_x = dx / d; p.dir_z = dz / d; p.yaw0 = std::atan2(-dx, -dz); p.locked = true;
    } else {                        // straight back from his facing
        p.dir_x = -std::sin(yaw); p.dir_z = -std::cos(yaw); p.yaw0 = yaw;
    }
    p.distance = float(c.distance);
    p.why = Why::Ok;
    return p;
}
// TP's mNormalSpeed is constant through the flip: the travel is linear in time, all of it by the end of the flip.
inline double Share(double tau) { return !(tau > 0.0) ? 0.0 : tau >= 1.0 ? 1.0 : tau; }

// ---- per tick ----------------------------------------------------------------------------------
struct Frame {
    uint32_t frame = 0; double time = 0; float step = 1.f;
    Vec before, after;              // Link last frame (collided) and after this tick's MotionMove
    bool collision = true;          // +0x2CE bit 0x100 (the scenery check runs this frame)
    bool target_ok = false; Vec target;   // the locked Ganado alive this tick (yaw only)
};
enum class Stop : uint8_t { None, Done, Blocked, Height, BadInput, WriteFailed, Interrupted, NoCollision };
inline const char* StopName(Stop s) {
    static const char* const n[] = {"none", "done", "blocked", "height", "bad_input", "write_failed", "interrupted", "no_collision"};
    return n[unsigned(s)];
}
struct Summary {
    uint64_t lease = 0; Plan plan; unsigned ticks = 0, repeats = 0, lag_ticks = 0;
    float travelled = 0;            // mm commanded along the direction
    float max_lag = 0, e = 0;
    Stop stop = Stop::None;
};
class Move {
public:
    explicit Move(Config c = {}) : c_(c) {}
    const Config& config() const { return c_; }
    bool Active() const { return on_; }
    uint64_t lease() const { return on_ ? s_.lease : 0; }
    const Summary& summary() const { return s_; }
    bool Begin(uint64_t lease, const Plan& p, double start, Vec origin) {
        s_ = Summary{}; s_.lease = lease; s_.plan = p; on_ = false;
        if (!lease || p.why != Why::Ok || !std::isfinite(start) || !Finite(origin) || !c_.travel_ticks) return false;
        start_ = start; origin_ = origin; yaw_ = p.yaw0; known_ = false; have_cmd_ = false; lag_run_ = 0;
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
        if (std::fabs(f.before.y - origin_.y) > kHeight) return Halt(Stop::Height), o;
        if (!f.collision) return Halt(Stop::NoCollision), o;
        if (have_cmd_) {   // where the last command left Link vs where the collision put him
            const float lag = std::hypot(f.before.x - cmd_x_, f.before.z - cmd_z_);
            if (lag > s_.max_lag) s_.max_lag = lag;
            if (lag > kBlockSlack) { ++s_.lag_ticks; if (++lag_run_ >= 2) return Halt(Stop::Blocked), o; }
            else lag_run_ = 0;
        }
        const double tau = (e + double(f.step)) / double(c_.travel_ticks);   // one tick ahead, as the Back Slice
        const float along = float(Share(tau)) * s_.plan.distance;
        cmd_x_ = origin_.x + s_.plan.dir_x * along; cmd_z_ = origin_.z + s_.plan.dir_z * along; have_cmd_ = true;
        s_.travelled = along;
        if (s_.plan.locked && f.target_ok && Finite(f.target)) {
            const float tx = f.target.x - cmd_x_, tz = f.target.z - cmd_z_;
            if (tx * tx + tz * tz >= 1.f) yaw_ = std::atan2(tx, tz);   // keep facing the Ganado (cPlayer yaw at +0xA4)
        }
        o.write = true; o.from = f.after; o.x = cmd_x_; o.z = cmd_z_; o.yaw = yaw_;
        if (tau >= 1.0) Halt(Stop::Done);   // this is the last write
        return o;
    }
    bool Abort(Stop why) { const bool was = on_; Halt(why); return was; }
private:
    void Halt(Stop why) { if (on_) { on_ = false; s_.stop = why; } }
    Config c_; Summary s_; bool on_ = false, known_ = false, have_cmd_ = false;
    double start_ = 0; Vec origin_; float yaw_ = 0, cmd_x_ = 0, cmd_z_ = 0; uint32_t frame_ = 0; unsigned lag_run_ = 0;
};

// ---- log ---------------------------------------------------------------------------------------
inline int FormatEnd(char* out, size_t n, const Summary& s) {
    return std::snprintf(out, n, "backflip travel stop=%s lease=%llu ticks=%u repeats=%u travelled=%.0f planned=%.0f locked=%u "
        "max_lag=%.0f lag_ticks=%u e=%.1f", StopName(s.stop), static_cast<unsigned long long>(s.lease), s.ticks, s.repeats,
        double(s.travelled), double(s.plan.distance), s.plan.locked ? 1U : 0U, double(s.max_lag), s.lag_ticks, double(s.e));
}
}  // namespace link_runtime::backflip

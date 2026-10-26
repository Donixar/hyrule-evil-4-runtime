#pragma once
// Mortal Draw magnet (12-oct-2026, after build 07): keeps Link facing the target
// during the windup and stops the clip's root-motion lunge at a contact distance.
// Pure decisions plus small templated memory helpers; the hook lives in
// link_runtime.cpp. Pinned bio4.exe SHA 19aed4af...; RVAs, base 0x400000.
//
// Why (build 07 log): the clip's root motion carries Link ~1000-1200 units in the
// 16 frames before the cut, 14-32 deg off his facing. From 450 away he ends
// 743-905 beyond the Ganado, on its other side; the blade passes beside it.
//
// Where the clip moves Link (static, pinned image, scripts in the report):
//  - cPlayer::move 0x364D20 dispatches the player routine by R0 (+0xFC) through
//    table 0x81BCB4 at 0x364FF0. Entry 0 (normal 0x3632E0) and entry 5 (event
//    0x352CE0, via thunk 0x7E46) are the dispatch_hooks detours: Runtime::Handle
//    runs decide() and then, while it owns the lease, api.move = 0x25CFD0.
//  - 0x25CFD0: push 0; push ecx; call thunk 0x6BF9 -> MotionMove 0x262B90.
//  - MotionMove, root enabled (+0x218 & 1, set by our MotionSet): root delta from
//    0x262640 (thunk 0xE5B6) at 0x262BEA; 0x262D2C rotates it by the model 3x3
//    (+0x0C); 0x262D40 adds it to pos +0x94 (the only write to +0x94..+0x9C in
//    the function body); 0x262D54 adds the root rotation to ang +0xA0 (zero for
//    this clip: build 07 logged yaw_now == yaw_set 16 frames later).
//  - Back in cPlayer::move after the dispatch: parts world 0x25BED0 (0x3650CB),
//    part fix 0x25C680 (0x3650D2), position correction 0x25EDB0 (0x3650D8:
//    pos minus history +0x244 shifted into the model/part matrices), EmAtCheck
//    (0x3650F9), atari/collision 0x1955B0 on player+0x2B4 (0x36516E), position
//    correction again (0x365174).
// Hook point: the MotionMove 0x262B90 detour, right after the original returns,
// gated to the dispatch's own api.move of our lease (Runtime busy, current ==
// model, Owns, lease token). Nothing in the rest of MotionMove writes +0x94, and
// everything after it in cPlayer::move reads our corrected position: the matrix
// propagation and the native collision both run on it, so the correction is not
// overwritten and is not hidden from collision. Yaw is written at the same point,
// after the root rotation add, so the clip cannot overwrite it this frame.
//
// Collision reasoning (no attributed collision query; none is needed):
//  - The correction is a filter of this call's own displacement d = after - before
//    (read around the original MotionMove). Link ends at before + u*a + k*lat with
//    u the unit vector to the target, a <= the clip's own progress along u, lat the
//    sideways part and k = LateralKeep. |result - before| <= |d| always (proved in
//    the tests): he never moves farther than the clip moved him this frame and
//    never forward of the clip's own along-line progress (no push, no teleport).
//  - `before` is a collided position (previous frame). The new point is within
//    one clip step (~70-90 units) of it, and native collision (0x36516E) runs on
//    it afterwards in the same cPlayer::move, exactly as for the raw clip step.
//  - Y (+0x98) is never written: ground following stays native.
//  - Redirect (opt-in, default off) turns removed drift into along-line progress
//    with the same magnitude |d|; it is the only mode that moves Link somewhere
//    the clip did not head, still bounded by |d| and the contact clamp.
// Frame rate (re4_fps, docs/fps-coexistence-2026-10-12.md): the INI rates are per 60 Hz frame.
// Each Step carries `step` = 2*deltaTime (1.0 at 60): the turn rate, MaxStep and the target jump
// scale per tick, and the frame cap counts game time (Elapsed60). At 60 nothing changes.
// Fail closed: any unreadable/non-finite input, a lost or jumping target, a step
// that is not the clip's (> MaxStep), a far target or the frame cap turn the
// magnet off for the rest of the action (logged); the clip then runs untouched.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "game_clock.h"

namespace link_runtime::mortal_draw::magnet {
namespace layout {
// Player and Ganado share the cModel layout for these (mortal_draw_target.h layout,
// shield_reader convention): pos XYZ, ang.y; forward = (sin yaw, cos yaw).
constexpr uint32_t pos_x = 0x94, pos_y = 0x98, pos_z = 0x9C, yaw = 0xA4;
constexpr uint32_t em_be_flag = 0x004, em_id = 0x100, em_type = 0x101;  // (be_flag & 0x201) == 1: live slot
constexpr uint32_t motion_move_rva = 0x262B90, root_add_rva = 0x262D40, root_rot_rva = 0x262D54;
constexpr uint32_t move_wrapper_rva = 0x25CFD0, player_move_rva = 0x364D20, collision_call_rva = 0x36516E;
}  // namespace layout

struct Vec2 { float x = 0, z = 0; };
inline Vec2 Sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
inline Vec2 Add(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
inline Vec2 Mul(Vec2 a, float s) { return {a.x * s, a.z * s}; }
inline float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
inline float Len(Vec2 a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(Vec2 a) { return std::isfinite(a.x) && std::isfinite(a.z); }
// Signed shortest turn from `from` to `to`, in (-pi, pi] (same as target::Turn).
inline float Turn(float from, float to) {
    constexpr float pi = 3.14159265f, two_pi = 6.28318531f;
    float d = std::fmod(to - from, two_pi);
    if (d > pi) d -= two_pi; else if (d <= -pi) d += two_pi;
    return d;
}
// Native yaw that faces `to` from `from`: atan2(dx, dz). False under 1 unit.
inline bool FaceYaw(Vec2 from, Vec2 to, float& yaw) {
    const Vec2 d = Sub(to, from);
    if (!Finite(d) || !(Dot(d, d) >= 1.f)) return false;
    yaw = std::atan2(d.x, d.z);
    return std::isfinite(yaw);
}

struct Config {
    bool enabled = true;          // MortalDrawMagnet
    float contact = 600.f;        // MortalDrawContact: never closer than this to the target centre (XZ)
    float turn_rate = 0.2617994f; // MortalDrawTurnRate (deg/frame in the INI): 15 deg per frame
    float lateral_keep = 0.f;     // MortalDrawLateralKeep (percent): sideways drift kept
    bool redirect = false;        // MortalDrawMagnetRedirect: removed drift becomes along-line progress
    bool follow = true;           // MortalDrawMagnetFollow: keep the clamp after the cut until the lease ends
    float max_step = 400.f;       // a single MotionMove step longer than this is not the clip: stop
    float max_target_step = 600.f;// target moved more than this between steps: stop
    float max_track = 4000.f;     // target farther than this: stop
    unsigned max_frames = 120;    // 60 Hz frames of game time after Begin; then stop
    float min_face = 50.f;        // no yaw steering closer than this (atan2 unstable)
    unsigned problems = 0;
};
inline bool ExactOne(const wchar_t* v) { return v && v[0] == L'1' && v[1] == 0; }
inline bool Number(const wchar_t* v, unsigned& out) {
    if (!v || !*v) return false;
    unsigned n = 0;
    for (const wchar_t* p = v; *p; ++p) {
        if (*p < L'0' || *p > L'9' || n > 100000) return false;
        n = n * 10 + unsigned(*p - L'0');
    }
    out = n; return true;
}
// get(key, out, count) fills `out` with the raw [LinkRuntime] value ("" when missing).
// Invalid values keep the default and count a problem (logged by the caller).
template <class Get> Config ReadConfig(Get&& get) {
    Config c;
    wchar_t v[32]{};
    auto number = [&](const wchar_t* key, unsigned fallback, unsigned lo, unsigned hi) {
        v[0] = 0; get(key, v, sizeof(v) / sizeof(v[0]));
        unsigned n = 0;
        if (!v[0]) return fallback;
        if (!Number(v, n) || n < lo || n > hi) { ++c.problems; return fallback; }
        return n;
    };
    c.enabled = number(L"MortalDrawMagnet", 1, 0, 1) == 1;
    c.contact = float(number(L"MortalDrawContact", 600, 200, 1500));
    c.turn_rate = float(number(L"MortalDrawTurnRate", 15, 1, 180)) * 0.01745329252f;
    c.lateral_keep = float(number(L"MortalDrawLateralKeep", 0, 0, 100)) * 0.01f;
    c.redirect = number(L"MortalDrawMagnetRedirect", 0, 0, 1) == 1;
    c.follow = number(L"MortalDrawMagnetFollow", 1, 0, 1) == 1;
    return c;
}

// One MotionMove step through the filter. `before`/`after`: Link around the original
// call; `target`: the Ganado now. pos is where Link should be.
struct Filtered {
    Vec2 pos;
    float lateral_removed = 0, along_removed = 0, redirected = 0;
    float d_before = 0, d_after = 0;  // distance to the target before the step and after the filter
    bool clamped = false, changed = false;
};
inline bool Filter(const Config& c, Vec2 before, Vec2 after, Vec2 target, Filtered& out) {
    out = Filtered{}; out.pos = after;
    if (!Finite(before) || !Finite(after) || !Finite(target)) return false;
    const Vec2 d = Sub(after, before), tb = Sub(target, before);
    const float dist = Len(tb);
    out.d_before = dist;
    if (!(dist >= 1.f)) { out.d_after = Len(Sub(target, after)); return false; }
    const Vec2 u = Mul(tb, 1.f / dist);
    float along = Dot(d, u);
    const Vec2 lat = Sub(d, Mul(u, along));
    const float lat_len = Len(lat);
    const float keep = c.lateral_keep < 0.f ? 0.f : c.lateral_keep > 1.f ? 1.f : c.lateral_keep;
    const Vec2 kept = Mul(lat, keep);
    out.lateral_removed = lat_len * (1.f - keep);
    if (c.redirect && along > 0.f) {
        // Same magnitude as the clip step: |d|^2 = along'^2 + |kept|^2.
        const float rest = Dot(d, d) - Dot(kept, kept);
        const float redirected = rest > along * along ? std::sqrt(rest) : along;
        out.redirected = redirected - along; along = redirected;
    }
    // Never closer than the contact distance, or than where he already is when he
    // started inside it: only clip motion is removed, Link is never pushed back.
    const float floor_d = c.contact < dist ? c.contact : dist;
    const float max_along = dist - floor_d;
    if (along > max_along) { out.along_removed = along - max_along; along = max_along; out.clamped = true; }
    out.pos = Add(before, Add(Mul(u, along), kept));
    out.d_after = Len(Sub(target, out.pos));
    out.changed = std::fabs(out.pos.x - after.x) > 0.01f || std::fabs(out.pos.z - after.z) > 0.01f;
    return true;
}
// Yaw after one rate-limited step toward `to` from `from`. Keeps the game's own
// representation (no wrapping of the stored value). False: nothing to write.
inline bool StepYaw(float yaw_now, Vec2 from, Vec2 to, float rate, float min_face, float& out, float& step) {
    out = yaw_now; step = 0.f;
    float want = 0.f;
    if (!std::isfinite(yaw_now) || !(Len(Sub(to, from)) >= min_face) || !FaceYaw(from, to, want)) return false;
    const float turn = Turn(yaw_now, want);
    step = turn > rate ? rate : turn < -rate ? -rate : turn;
    if (std::fabs(step) < 1e-5f) { step = 0.f; return false; }
    out = yaw_now + step;
    return std::isfinite(out);
}

enum class Phase : uint8_t { Off, Windup, Follow };
enum class Stop : uint8_t { None, Disabled, Ended, Struck, TargetLost, TargetJump, FarTarget, BigStep, Frames, BadInput, WriteFailed };
inline const char* StopName(Stop s) {
    static const char* const n[] = {"none", "disabled", "ended", "struck", "target_lost", "target_jump", "far_target",
                                    "big_step", "frames", "bad_input", "write_failed"};
    return n[unsigned(s)];
}
struct Frame {
    uint32_t frame = 0;          // native logic frame (global+0x530C)
    double time = -1;            // game time in 60 Hz frames (game_clock Elapsed60); <0: use frame
    float step = 1.f;            // this tick in 60 Hz frames (2*deltaTime; 1 at 60 FPS)
    Vec2 before, after;          // Link around the original MotionMove
    float yaw = 0;               // +0xA4 after the original
    bool yaw_ok = false;
    bool target_ok = false;      // same actor/id/type, live slot, finite
    Vec2 target;
};
struct Output { bool write_pos = false, write_yaw = false; Vec2 pos; float yaw = 0; };
struct Summary {
    uint64_t lease = 0;
    unsigned steps = 0, repeats = 0, clamp_steps = 0, first_clamp = 0, strike_step = 0, follow_steps = 0;
    float start_d = 0, start_turn = 0;        // distance and wanted turn at Begin
    float clip_travel = 0, kept_travel = 0;   // sum |d| and sum |filtered step|
    float lateral_removed = 0, along_removed = 0, redirected = 0;
    float follow_lateral_removed = 0, follow_along_removed = 0;
    float turn_total = 0, max_turn_step = 0;
    float min_d = 0;
    bool struck = false;
    float strike_d = 0, strike_angle = 0, strike_offset = 0;  // at the cut: distance, facing error (rad), sideways miss
    Stop stop = Stop::None;
};

class Magnet {
public:
    explicit Magnet(Config c = {}) : c_(c) {}
    const Config& config() const { return c_; }
    Phase phase() const { return phase_; }
    uint64_t lease() const { return phase_ == Phase::Off ? 0 : s_.lease; }
    const Summary& summary() const { return s_; }
    // Start frame (decide, after the clip started). link/target: XZ now; yaw: +0xA4 now.
    bool Begin(uint64_t lease, uint32_t frame, float yaw, Vec2 link, Vec2 target, double time = -1) {
        s_ = Summary{}; s_.lease = lease; phase_ = Phase::Off;
        if (!c_.enabled) { s_.stop = Stop::Disabled; return false; }
        if (!lease || !std::isfinite(yaw) || !Finite(link) || !Finite(target)) { s_.stop = Stop::BadInput; return false; }
        float want = yaw;
        s_.start_d = Len(Sub(target, link)); s_.min_d = s_.start_d;
        if (FaceYaw(link, target, want)) s_.start_turn = Turn(yaw, want);
        start_frame_ = frame; last_frame_ = frame; yaw_frame_valid_ = false; last_target_ = target;
        start_time_ = time >= 0 ? time : double(frame);
        phase_ = Phase::Windup;
        return true;
    }
    // After the dispatch's own MotionMove of our lease. One call per MotionMove.
    Output Step(const Frame& f) {
        Output o;
        if (phase_ == Phase::Off) return o;
        if (!Finite(f.before) || !Finite(f.after)) return Halt(Stop::BadInput), o;
        if (!f.target_ok || !Finite(f.target)) return Halt(Stop::TargetLost), o;
        const double now = f.time >= 0 ? f.time : double(f.frame);
        const float tick = std::isfinite(f.step) && f.step > 0.f ? f.step : 1.f;
        if (now < start_time_ - game_clock::kEpsilon || game_clock::Exceeded(now - start_time_, double(c_.max_frames)))
            return Halt(Stop::Frames), o;
        if (Len(Sub(f.target, last_target_)) > c_.max_target_step * tick) return Halt(Stop::TargetJump), o;
        last_target_ = f.target;
        const float step_len = Len(Sub(f.after, f.before));
        if (step_len > c_.max_step * tick) return Halt(Stop::BigStep), o;
        if (Len(Sub(f.target, f.before)) > c_.max_track) return Halt(Stop::FarTarget), o;
        Filtered r;
        if (!Filter(c_, f.before, f.after, f.target, r)) {
            // Target on top of Link (< 1 unit): leave this step alone.
            ++s_.steps; s_.clip_travel += step_len; s_.kept_travel += step_len;
            return o;
        }
        ++s_.steps;
        s_.clip_travel += step_len; s_.kept_travel += Len(Sub(r.pos, f.before));
        if (phase_ == Phase::Windup) {
            s_.lateral_removed += r.lateral_removed; s_.along_removed += r.along_removed; s_.redirected += r.redirected;
        } else {
            ++s_.follow_steps; s_.follow_lateral_removed += r.lateral_removed; s_.follow_along_removed += r.along_removed;
        }
        if (r.clamped) { ++s_.clamp_steps; if (!s_.first_clamp) s_.first_clamp = s_.steps; }
        if (r.d_after < s_.min_d) s_.min_d = r.d_after;
        if (r.changed) { o.write_pos = true; o.pos = r.pos; }
        // Facing: once per native frame, windup only (the cut owns the follow-through).
        if (phase_ == Phase::Windup && f.yaw_ok) {
            if (yaw_frame_valid_ && f.frame == yaw_frame_) ++s_.repeats;
            else {
                float yaw = f.yaw, turn = 0.f;
                if (StepYaw(f.yaw, r.pos, f.target, c_.turn_rate * tick, c_.min_face, yaw, turn)) {
                    o.write_yaw = true; o.yaw = yaw;
                    s_.turn_total += turn;
                    if (std::fabs(turn) > s_.max_turn_step) s_.max_turn_step = std::fabs(turn);
                }
                yaw_frame_ = f.frame; yaw_frame_valid_ = true;
            }
        }
        last_frame_ = f.frame;
        return o;
    }
    // The cut (decide, before that frame's MotionMove). Records the geometry the blade sees.
    void NoteStrike(uint64_t lease, Vec2 link, float yaw, Vec2 target, bool target_ok) {
        if (!lease || lease != s_.lease || s_.struck) return;
        s_.struck = true; s_.strike_step = s_.steps;
        if (target_ok && Finite(link) && Finite(target) && std::isfinite(yaw)) {
            const Vec2 to = Sub(target, link);
            s_.strike_d = Len(to);
            float want = yaw;
            if (FaceYaw(link, target, want)) s_.strike_angle = Turn(yaw, want);
            s_.strike_offset = s_.strike_d * std::sin(std::fabs(s_.strike_angle));
        }
        if (phase_ == Phase::Windup) {
            if (c_.follow) phase_ = Phase::Follow;
            else Halt(Stop::Struck);
        }
    }
    // The caller could not apply an output (SEH-guarded write failed): stop for this action.
    void Abort(Stop why) { Halt(why); }
    // Lease end (finished or interrupted). True when this lease was ours.
    bool End(uint64_t lease) {
        if (!lease || lease != s_.lease) return false;
        if (phase_ != Phase::Off) Halt(Stop::Ended);
        return true;
    }

private:
    void Halt(Stop why) { if (phase_ != Phase::Off) { phase_ = Phase::Off; s_.stop = why; } }
    Config c_;
    Phase phase_ = Phase::Off;
    Summary s_;
    uint32_t start_frame_ = 0, last_frame_ = 0, yaw_frame_ = 0;
    double start_time_ = 0;
    bool yaw_frame_valid_ = false;
    Vec2 last_target_;
};

// ---- memory helpers (Memory: Read(addr, out, n) / Write(addr, in, n)) ----
template <class Memory> bool ReadPlayer(Memory& m, uint32_t player, Vec2& pos, float& yaw) {
    float p[3]{}; float y = 0;
    if (!player || !m.Read(player + layout::pos_x, p, sizeof(p)) || !m.Read(player + layout::yaw, &y, 4)) return false;
    pos = {p[0], p[2]}; yaw = y;
    return Finite(pos);
}
// The selected Ganado, still the same: live slot, same id/type, finite position.
template <class Memory> bool ReadTarget(Memory& m, uint32_t actor, uint8_t id, uint8_t type, Vec2& out) {
    uint32_t be = 0; uint8_t idt[2]{}; float p[3]{};
    if (!actor || !m.Read(actor + layout::em_be_flag, &be, 4) || (be & 0x201) != 1 ||
        !m.Read(actor + layout::em_id, idt, 2) || idt[0] != id || idt[1] != type ||
        !m.Read(actor + layout::pos_x, p, sizeof(p))) return false;
    out = {p[0], p[2]};
    return Finite(out);
}
// X and Z only (Y stays native), then the yaw.
template <class Memory> bool Apply(Memory& m, uint32_t player, const Output& o) {
    bool ok = true;
    if (o.write_pos) {
        ok = m.Write(player + layout::pos_x, &o.pos.x, 4) && m.Write(player + layout::pos_z, &o.pos.z, 4);
    }
    if (ok && o.write_yaw) ok = m.Write(player + layout::yaw, &o.yaw, 4);
    return ok;
}
// md_magnet log line (strike or end).
inline int Format(char* out, size_t n, const char* when, const Config& c, const Summary& s) {
    constexpr float deg = 57.29577951f;
    return std::snprintf(out, n,
        "md_magnet %s lease=%llu steps=%u follow_steps=%u repeats=%u start_d=%.0f start_turn=%.1f contact=%.0f "
        "clip_travel=%.0f kept_travel=%.0f lateral_removed=%.0f along_removed=%.0f redirected=%.0f clamp_steps=%u "
        "first_clamp=%u follow_lateral_removed=%.0f follow_along_removed=%.0f turn_total=%.1f max_turn_step=%.1f "
        "min_d=%.0f struck=%u strike_step=%u strike_d=%.0f strike_angle=%.1f strike_offset=%.0f stop=%s",
        when, static_cast<unsigned long long>(s.lease), s.steps, s.follow_steps, s.repeats, s.start_d, s.start_turn * deg,
        c.contact, s.clip_travel, s.kept_travel, s.lateral_removed, s.along_removed, s.redirected, s.clamp_steps,
        s.first_clamp, s.follow_lateral_removed, s.follow_along_removed, s.turn_total * deg, s.max_turn_step * deg,
        s.min_d, s.struck ? 1U : 0U, s.strike_step, s.strike_d, s.strike_angle * deg, s.strike_offset, StopName(s.stop));
}
}  // namespace link_runtime::mortal_draw::magnet

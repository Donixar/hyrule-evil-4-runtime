#pragma once
// Back Slice camera (backslice-cam-01, 19-oct-2026; docs/backslice-cam-01-2026-10-19.md). Pure: the falling-camera
// hold decision, TP's twirl-jump curve, the orbit springs, the turned / vertically followed offsets, the camera-state
// carry and the per-lease record. The hitCheck stage and the CameraControl::Check hook live in ztarget_runtime.h
// (PrepareOrbitWith, ProbeWall, FinishOrbitWith, FallHoldWith), the lease facts in spin_attack_runtime.h
// (SwordBackCameraFacts). Pinned bio4.exe SHA 19aed4af...; RVAs at base 0x400000. BackSliceCamera=0 runs none of it.
//
// Cause (live probe 19-oct 08:28, work/live/backslicecam-before-*.jsonl, and UHD CameraControl::Check 0x2F58A0 = GC
// cam_ctrl.cpp Check): every frame Check measures d = getPartsPtr(1)->world - pPL->pos; with r0 != 0xB and d.y <= 500
// it starts the falling camera (r0 0xB, CameraLookAt: the camera stays where it was and only turns to Link, collision
// against scenery; m_Inter.set(3)). TP's side roll lays Link on the ground: his hips fell from 1098 to 150 and r0 went
// 0xA -> 0xB 30 render frames (e 10.5) into the roll, so the camera froze while Link rolled round the Ganado (far
// behind him, then ground close-ups). When the twirl lifted the hips over 500 (e 33) Check set m_Inter (30 TP frames,
// 1 s) and m_system_flag 0x10, areaHitCheck put the shoulder camera (r0 0xA) back, and m_Inter dragged the camera in a
// straight line from the frozen spot to the shoulder, through Link (hood and shield close-ups, 1.3 m from his hips),
// while the 1.8 m jump took his hips 7 screen-heights above the frame. Two more faults stay once the fall is held: the
// shoulder camera follows Link's matrix (QFPS m_pl_mat) but CamSmth low-passes its world position, which cuts the
// orbit's chord (offline: 0.75 m from Link's axis instead of 1.27 m), and at that offset the roll (hips at 0.15 m)
// and the jump (hips at 2.9 m) leave the 51-degree-high view.
//
// Fix, during a clean Back Slice lease the dispatch owns (Engaged):
//  1. Fall hold: a mid-function hook at 0x2F5990 (fld [ebp-0xC]; fcomp [500.0]) hands the test d.y = 501 when it is
//     <= 500 and r0 is the shoulder camera 0xA, so the falling camera never starts. Nothing else of Check changes
//     (areaHitCheck, attach cameras and an event still switch cameras natively). The hook also keeps d.y (Link's
//     hips over his feet, the value the game itself tests) for the vertical follow.
//  2. Orbit: hitCheck gets a copy of the offsets rotated about Link's vertical axis by delta = q - n, n the native view
//     yaw (it turns with Link, who faces the Ganado every tick of the arc) and q a critically damped spring on it
//     (BackSliceCameraMs, 40 ms): the camera orbits round Link at the lock camera's own offset, easing in and out (TP's
//     roll starts at full speed: the camera trails by up to ~32 degrees, the Ganado stays within ~0.6 of the half
//     width), and never closer to Link than that offset.
//  3. Vertical follow (BackSliceCameraLift percent, 100 = these): H through a 35 ms spring is TP's twirl jump (Jump60,
//     aimed kCraneLead ahead) plus the roll's dip (the live hips under the lease's standing reference, the jump taken
//     out, never above 0). Up (the jump): the camera rises with H and backs off 0.4 H, the look point rises 0.7 H. Down
//     (the roll): the camera drops 0.3 |H|, the look point drops 1.2 |H|. Link (hips and head) and the Ganado's chest
//     stay in frame through the roll and at the apex (offline, the 19-oct view).
//  4. Carry: each fresh frame the camera chain's state (CamSmth m_effect, its backup, m_Inter.param; the Ending Blow
//     lift's addresses) is moved by this frame's base transform over the last one's (Link's matrix, delta and the
//     vertical follow), so the chain sees the orbit as a rigid move and CamSmth's world low-pass no longer cuts the
//     chord.
//  5. Walls (live 19-oct, r100 by the sheds): hitCheck pulls the asked camera in toward Link's shoulder, but CamSmth
//     low-passes that pull (and the carry keeps moving its state with the asked pose), so the smoothed camera stayed up
//     to 1 m beyond the wall: black frames inside the planks, then Link's legs and back filling the frame as it caught
//     up. On a frame whose hitCheck output lies more than kSnapPull from the camera it was asked for, the stage sets
//     CamSmth's own snap bit (m_flag bit 0, what the QFPS init uses): this CameraMove's smoothed camera is the collided
//     one, the near-head shove still runs. Out in the open nothing changes. hitCheck's camera ray (EmHitCheck,
//     ObjHitCheck, SatMgr 0x8000) also let the vertical follow carry the camera into the plank walls of those sheds and
//     the hillside, so before hitCheck a wall probe guards the follow's own move with the Ganados' sight test (EatMgr,
//     the walls, columns and doors bullets hit; ztarget_los.h): from the turned camera at the lock camera's offset, the
//     rise (or drop) first, then the back-off; a blocked leg is bisected for its farthest clear point, kProbeMargin
//     short of it (in at once, back out over kProbeOutTau), and the snap follows it. It never brings the camera closer
//     than that offset: a first probe cast from the shoulder (dev build, live 19-oct) pulled it into Link's pouch and
//     hood where the lock camera's own spot is inside the hill.
//  6. After the lease (or damage, an interruption): the springs ease delta and H back to 0 with the camera chain native
//     (no carry), at most kReleaseMax; a scene change, death, a lost frame, another camera mode, a bad matrix or an
//     unwritable state ends it at once (fail closed: native from that call).
#include "ending_blow_camera.h"
#include "game_clock.h"
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace link_runtime::back_slice::camera {
namespace zt = link_runtime::ztarget;
namespace ebc = link_runtime::ending_blow::camera;
using zt::Vec;
using zt::Matrix;
using zt::Offset;

// ---- the native falling camera ------------------------------------------------------------------------
namespace rva {
constexpr uint32_t check = 0x2F58A0;      // CameraControl::Check (thiscall, ebx = this after its prologue)
constexpr uint32_t fall_site = 0x2F5990;  // fld dword [ebp-0xC] (d.y); fcomp qword [500.0]: the hook site (9 bytes)
constexpr uint32_t check_thunk = 0xF330;  // JMP Check; CameraMove (0x19F410) calls it at 0x19F41E, then Move
constexpr int32_t fall_dy = -0xC;         // d.y, the local the test loads (PSVECSubtract wrote d at ebp-0x10)
constexpr uint32_t r0 = 0x34;             // CamCtrl r0 (byte): 0xA shoulder (QFPS), 0xB falling (CameraLookAt)
}  // namespace rva
constexpr float kFallLimit = 500.f;       // native: d.y <= 500 starts the falling camera (double 500.0 at 0xB0F988)
constexpr float kFallHeld = 501.f;        // what the test sees while held
constexpr uint8_t kModeShoulder = 0xA, kModeFall = 0xB;
// Relocation-normalized bytes (native_pins.h): Check's whole body (0x2F58A0..0x2F5A92; no re4_fps / re4_tweaks
// pattern touches it, fps-patterns.json 17-oct), its thunk, CameraMove's call, and the getPartsPtr / PSVECSubtract
// thunks the test's d comes from.
inline constexpr native_pins::Pin kCheckPins[] = {
    {0x2F58A0, "558bec83ec10a1105ec30033c58945fca13c6fc000f7801c50000000000400538bd90f85bc0100008a4328a8010f84b1010000a8040f85a9010000836330fdf6432c088a4b34884b3775078bcbe84f34d1ff56578bcbe8deddd0ff807b371175248a43343c0a72083c0b76193c117415d9ee8dbb540200008d7338d95f20b908"},
    {0x2F5920, "000000f3a58b4330a8400f85520100008b153c6fc000f7821c500000001000000f853c01000083bb50020000000f852f010000a8040f85270100008b0d5470c5008b81a402000085c0740a807805000f850d0100008d45f0508d9194000000526a01e862d2d0ff83c07050e8698ad1ffd945f4dc1d88f9b00083c40c807b340b"},
    {0x2F59A0, "dfe00f849d000000f6c4410f8ad1000000d905bcf8b0008dbb54020000d95f20a13c6fc000d940708db304010000d9e8b908000000dee1dc05b0f8b000d95f20f3a58b8b10070000c643340b85c974128b118b026a01ffd0c78310070000000000008d8b9004000085c974208d536052e8ec6dd1ff5f5e8983100700005b8b4d"},
    {0x2F5A20, "fc33cde8727337008be55dc35f33c05e8983100700005b8b4dfc33cde8597337008be55dc3f6c4417538d905a4fbb0008dbb54020000d95f20a13c6fc000d940708db304010000d9e8b908000000dee1dc0530f9b000d95f20f3a5c7432c100000005f5e8b4dfc33cd5be80b7337008be55dc3"},
    {0xF330, "e96b652e00"},          // Check's thunk
    {0x19F419, "b9d002c700e80dffe6ff"},  // CameraMove: mov ecx, CamCtrl; call Check's thunk
    {0x2BE9, "e9d28b2500"},          // getPartsPtr (0x25B7C0)
    {0xE3F9, "e9e27e5600"}};         // PSVECSubtract (0x5762E0)
// Hold the test: the shoulder camera in a clean Back Slice lease, and the hips at or under the limit. A NaN d.y or
// any other mode (0xB already, an area camera, an event) is left alone.
inline bool HoldFall(bool engaged, uint8_t r0, float dy) {
    return engaged && r0 == kModeShoulder && std::isfinite(dy) && dy <= kFallLimit;
}

// ---- TP's twirl jump (scripts/back_slice_anim.py lift_tp, the clip's root lift; pinned by
// tests/test_back_slice_camera.py) --------------------------------------------------------------------------------
constexpr double kTwirlStart = 40.0;   // game frames (60 Hz): the roll's 20 TP frames, ROLL_TP
constexpr double kSpeedV = 33.0, kGravity = 3.4;   // daAlinkHIO_cutFnJU_c0 speed.y, mAutoJump gravity (TP units)
constexpr double kRootScale = 12.617719209449547;  // basis calibration root_scale (TP unit -> BIN)
// The clip's root lift (BIN) at e game frames since the lease start: 0 before the twirl and after its landing
// (TP 18.41 frames later, game ~76.8), max(0, 33 t - 1.7 t (t + 1)) TP units at t = (e - 40) / 2 in between
// (apex 1818 at e ~58.4). NaN, inf and negative e give 0.
inline double Jump60(double e) {
    if (!(e > kTwirlStart) || !std::isfinite(e)) return 0.0;
    const double t = (e - kTwirlStart) / 2.0, h = kSpeedV * t - kGravity / 2.0 * t * (t + 1.0);
    return h > 0.0 ? kRootScale * h : 0.0;
}

// ---- configuration ([LinkRuntime], read with SpinAttack=1; combat_runtime.h) ------------------------------------
constexpr unsigned kDefaultMs = 40, kMinMs = 30, kMaxMs = 200;      // BackSliceCameraMs: the orbit spring
constexpr unsigned kDefaultLift = 100, kMaxLift = 150;              // BackSliceCameraLift: vertical follow percent (0 = none)
struct Config { unsigned ms = kDefaultMs, lift = kDefaultLift; };
// The vertical follow at percent 100, as fractions of the sprung height H: up (the twirl jump) and down (the roll).
// Live 19-oct (dev build, r100 by the sheds): backing off 1.0 H took the camera 3.1 m behind Link at the apex, inside a
// shed whose planks hitCheck does not collide with. The camera now rises with Link and backs off 0.4 H (~0.7 m).
constexpr float kUp = 1.0f, kBack = 0.4f, kLook = 0.7f;
// The roll tilts the camera down more than it lowers it (dev build, live 19-oct: lowered 0.6 m it went into the hill).
constexpr float kDipDown = 0.3f, kDipBack = 0.0f, kDipLook = 1.2f;
constexpr float kMaxDip = 1200.f;           // BIN: the dip never goes deeper (Link's hips rest ~1100 over his feet)
// The dip's reference: the highest hips (the jump taken out) seen in the lease, never under this (19-oct probe: standing
// and guard hips 1094..1111 over Link's feet), so a lease whose first frame already caught the roll going down still
// measures the dip from a standing Link.
constexpr float kStandHips = 1050.f;
constexpr float kCraneTau = 0.035f;         // seconds: the vertical spring
// The jump's part aims this far ahead on its curve (2 tau in game frames: a critically damped spring trails a ramp by
// 2 tau), so it follows the jump without lag and only rounds off the takeoff and the landing. The dip is live.
constexpr double kCraneLead = 2.0 * double(kCraneTau) * 60.0;
constexpr float kReleaseMax = 0.6f;         // seconds of easing back after the lease, at most
constexpr float kDoneYaw = 0.00872665f;     // 0.5 degree: released
constexpr float kDoneHeight = 5.f;          // BIN: released
constexpr float kMaxCarry = 3000.f;         // BIN a state point may move in one carry (more: fail closed)
constexpr float kSnapPull = 5.f;            // BIN hitCheck may move the asked camera before CamSmth snaps to it (a wall)

// ---- springs ----------------------------------------------------------------------------------------------
// Critically damped toward a target held over h seconds (exact solution): starts from rest (ease in), no overshoot
// (ease out). x' = target + (e + (v + w e) h) exp(-w h), v' = (v - w (v + w e) h) exp(-w h), e = x - target, w = 1/tau.
struct Spring {
    float x = 0.f, v = 0.f;
    void Reset(float at) { x = at; v = 0.f; }
    void Step(float target, float h, float tau) {
        if (!(h > 0.f) || !(tau > 0.f) || !std::isfinite(target)) return;
        const double w = 1.0 / double(tau), e = double(x) - double(target), k = std::exp(-w * double(h)),
                     a = double(v) + w * e;
        x = float(double(target) + (e + a * double(h)) * k);
        v = float((double(v) - w * a * double(h)) * k);
    }
};

// ---- geometry -----------------------------------------------------------------------------------------------
// The native view yaw of these offsets under M (RE4 yaw atan2(x, z) of M's rotation times target - campos[0], as
// Z-targeting's Adjust); NaN when degenerate.
inline float ViewYaw(const Matrix& m, const Offset& in) {
    const Vec w = zt::World(m, {in.target.x - in.campos[0].x, in.target.y - in.campos[0].y, in.target.z - in.campos[0].z});
    if (!zt::Finite(w) || !(w.x * w.x + w.z * w.z >= 1.f)) return NAN;
    return std::atan2(w.x, w.z);
}
// World vertical rotation adding `yaw` to an RE4 yaw (Z-targeting's RotateVector).
inline Vec Yaw(Vec w, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {c * w.x + s * w.z, w.y, -s * w.x + c * w.z};
}
struct M3 { float v[3][3]{}; };
inline M3 Rot(const Matrix& m) { M3 r; for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j) r.v[i][j] = m.v[i][j]; return r; }
inline Vec Mul(const M3& a, Vec x) {
    return {a.v[0][0] * x.x + a.v[0][1] * x.y + a.v[0][2] * x.z, a.v[1][0] * x.x + a.v[1][1] * x.y + a.v[1][2] * x.z,
            a.v[2][0] * x.x + a.v[2][1] * x.y + a.v[2][2] * x.z};
}
inline Vec Add(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec Sub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
// This frame's base transform: world = Yaw(R v, delta) + b (+ dc for the camera point, + dp for the look point).
struct Pose { M3 r; float delta = 0.f; Vec b{}, dc{}, dp{}; bool valid = false; };
// up / look: signed world Y added to the camera and the look point; back: how far the camera moves away from the look
// point (horizontally, never toward it).
struct Crane { float up = 0.f, back = 0.f, look = 0.f; };
inline Crane CraneAt(float height, unsigned percent) {
    if (!std::isfinite(height) || height == 0.f || !percent) return {};
    const float k = float(percent > kMaxLift ? kMaxLift : percent) / 100.f;
    if (height > 0.f) return {kUp * k * height, kBack * k * height, kLook * k * height};
    const float d = (-height > kMaxDip ? kMaxDip : -height) * k;
    return {-kDipDown * d, kDipBack * d, -kDipLook * d};
}
// The vertical target at e: TP's twirl jump kCraneLead ahead (TP's clip only) plus the roll's dip: the live hips
// (Check's d.y) with the jump taken out, under `ref` (Orbit's reference, kStandHips at least), clamped to
// [-kMaxDip, 0]. Unknown hips (NaN, no reference): no dip.
inline float VerticalTarget(double e, bool jump, float hips, float ref) {
    double h = jump ? Jump60(e + kCraneLead) : 0.0;
    if (std::isfinite(hips) && std::isfinite(ref)) {
        double dip = double(hips) - double(ref) - (jump ? Jump60(e) : 0.0);
        if (dip > 0.0) dip = 0.0;
        if (dip < -double(kMaxDip)) dip = -double(kMaxDip);
        h += dip;
    }
    return float(h);
}
// The offsets hitCheck gets: campos[0], campos[1] and target turned by delta about M's vertical through its origin
// (Link), the camera point moved up by `up` (signed) and back (away from the look point, horizontally) by `back`, the
// look point moved up by `look` (signed); the collision ray's start (campos[1]) is only turned; roll and fovy bit for
// bit. M's general inverse (calcBaseMatrix tilts the up axis on slopes:
// the turn stays about the world vertical, the crane straight up in the world). delta 0 and no crane: the input, bit
// for bit. False (out untouched) for a matrix outside the determinant band or a non-finite result.
inline bool Apply(const Matrix& m, const Offset& in, float delta, const Crane& c, Offset& out, Pose& pose) {
    pose = {};
    if (!std::isfinite(delta) || !std::isfinite(c.up) || !std::isfinite(c.back) || !std::isfinite(c.look) ||
        !zt::Finite(in.campos[0]) || !zt::Finite(in.campos[1]) || !zt::Finite(in.target)) return false;
    const ebc::Inverse n = ebc::Invert(m);
    if (!n.ok) return false;
    const M3 r = Rot(m);
    const Vec cw = Yaw(Mul(r, in.campos[0]), delta), sw = Yaw(Mul(r, in.campos[1]), delta), tw = Yaw(Mul(r, in.target), delta);
    Vec dc{0.f, c.up, 0.f};
    const float hx = cw.x - tw.x, hz = cw.z - tw.z, hl = std::sqrt(hx * hx + hz * hz);
    if (c.back != 0.f && hl >= 1.f) { dc.x = c.back * hx / hl; dc.z = c.back * hz / hl; }
    const Vec dp{0.f, c.look, 0.f};
    pose.r = r; pose.delta = delta; pose.b = {m.v[0][3], m.v[1][3], m.v[2][3]}; pose.dc = dc; pose.dp = dp;
    pose.valid = zt::Finite(pose.b) && zt::Finite(dc);
    if (!pose.valid) return false;
    Offset o = in;
    if (delta != 0.f || c.up != 0.f || c.back != 0.f || c.look != 0.f) {
        o.campos[0] = ebc::Mul(n, Add(cw, dc));
        o.campos[1] = ebc::Mul(n, sw);
        o.target = ebc::Mul(n, Add(tw, dp));
    }
    if (!zt::Finite(o.campos[0]) || !zt::Finite(o.campos[1]) || !zt::Finite(o.target)) { pose.valid = false; return false; }
    out = o;
    return true;
}
// The camera chain's state moved from the last frame's base transform to this one's, as the native chain would see a
// rigid move: x' = Yaw(R1 R0^-1 Yaw(x - b0 - d0, -delta0), delta1) + b1 + d1, with d the camera point's crane for a
// Campos and the look point's for a Target. False (x untouched) when R0 has no inverse or the result is not finite
// or moves the point more than kMaxCarry.
inline bool Carry(const Pose& from, const Pose& to, bool campos, Vec& x) {
    if (!from.valid || !to.valid || !zt::Finite(x)) return false;
    Matrix m0{};
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j) m0.v[i][j] = from.r.v[i][j];
    const ebc::Inverse n0 = ebc::Invert(m0);
    if (!n0.ok) return false;
    const Vec d0 = campos ? from.dc : from.dp, d1 = campos ? to.dc : to.dp;
    const Vec local = ebc::Mul(n0, Yaw(Sub(Sub(x, from.b), d0), -from.delta));
    const Vec y = Add(Add(Yaw(Mul(to.r, local), to.delta), to.b), d1);
    if (!zt::Finite(y) || ebc::Distance(x, y) > kMaxCarry) return false;
    x = y;
    return true;
}

// ---- the wall probe (EatMgr) --------------------------------------------------------------------------------
constexpr unsigned kProbeSteps = 6;         // bisection steps: the clear point within 1/64 of the ray
constexpr float kProbeMargin = 100.f;       // BIN kept in front of the wall
constexpr float kProbeOutTau = 0.12f;       // seconds: a released pull eases back out (a new pull applies at once)
enum class Probe : uint8_t { Clear, Pulled, Fault };
inline Vec Lerp(Vec a, Vec b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }
// blocked(a, b, hit): false = the native test faulted; hit = the segment a-b is blocked. t: the share of from->to the
// camera may use (1: clear; 0: blocked right at `from`). Fault: t stays 1 (no pull).
template <class Blocked> Probe ProbeRay(Blocked&& blocked, Vec from, Vec to, float& t) {
    t = 1.f;
    if (!zt::Finite(from) || !zt::Finite(to)) return Probe::Fault;
    bool hit = false;
    if (!blocked(from, to, hit)) return Probe::Fault;
    if (!hit) return Probe::Clear;
    float lo = 0.f, hi = 1.f;
    for (unsigned i = 0; i < kProbeSteps; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (!blocked(from, Lerp(from, to, mid), hit)) { t = 1.f; return Probe::Fault; }
        if (hit) hi = mid; else lo = mid;
    }
    const float len = ebc::Distance(from, to);
    t = len > 1.f ? lo - kProbeMargin / len : 0.f;
    if (t < 0.f) t = 0.f;
    return Probe::Pulled;
}
// The share used this frame: a tighter one at once, a looser one eased over kProbeOutTau (h seconds of this frame).
inline float EaseProbe(float prev, float now, float h) {
    if (!std::isfinite(prev) || now <= prev) return now;
    if (!(h > 0.f)) return prev;
    return prev + (now - prev) * float(1.0 - std::exp(-double(h) / double(kProbeOutTau)));
}

// ---- per lease ----------------------------------------------------------------------------------------------
enum class Phase : uint8_t { Off, Lease, Release };
// Why the lease phase ended (or the record was cut). finished/interrupted: the dispatch's end event; damaged: Link
// took a hit; lease: the lease left the dispatch or changed kind; dead/room: Link's health or the scene; camera:
// hitCheck stopped (another camera mode); matrix: a degenerate view or base matrix; smooth: the camera state could not
// be read or written; read: the frame could not be read. The first reason is kept; `released` says whether the easing
// back converged (0: cut, or it ran kReleaseMax and the camera went native at once).
enum class End : uint8_t { None, Finished, Interrupted, Damaged, Lease, Dead, Room, Camera, Matrix, Smooth, Read };
inline const char* EndName(End e) {
    static const char* const n[] = {"none", "finished", "interrupted", "damaged", "lease", "dead", "room", "camera",
                                    "matrix", "smooth", "read"};
    return n[unsigned(e)];
}
// From the sword runtime (spin_attack_runtime.h SwordBackCameraFacts) on the game thread.
struct Facts {
    uint64_t lease = 0;     // g_sword_action.lease (0: none)
    bool back = false;      // a Back Slice lease
    bool clean = false;     // no interruption flagged
    bool owned = false;     // the dispatch still runs this lease
    bool jump = false;      // TP's clip plays (its twirl jump; the spin clip has none)
    double e = 0;           // game frames since the lease start (60 Hz)
};
inline bool Engaged(const Facts& f) { return f.lease && f.back && f.clean && f.owned; }
// The stage's own fresh read (ztarget ReadFrame) of the same tick; hips: Check's d.y of this CameraMove (Link's hips
// over his feet; NaN when the fall hook did not see it).
struct Scene {
    bool read = false; uint32_t frame = 0, player = 0; uint16_t room = 0, damage = 0; bool healthy = false; float dt = 0.f;
    float hips = NAN;
};
struct Summary {
    uint64_t lease = 0;
    End end = End::None;
    bool released = false;            // the release converged (else: cut, or it ran kReleaseMax)
    unsigned ticks = 0, release_ticks = 0, carries = 0, holds = 0, snaps = 0, ms = 0, lift = 0;
    float max_pull = 0.f;             // BIN: the largest pull hitCheck put on the asked camera (a wall)
    unsigned probe_pulls = 0, probe_faults = 0;   // frames the wall probe pulled the camera in; native test faults
    float max_probe = 0.f;            // BIN: the largest probe pull
    float max_turn = 0.f, max_lift = 0.f, max_dip = 0.f, peak_e = -1.f, last_e = -1.f;   // degrees of delta; BIN of H
    float hips_ref = NAN;             // the dip's reference at the end of the lease (NaN: d.y never seen)
};
struct Tick {
    bool apply = false;   // hand hitCheck the turned / craned copy
    bool fresh = false;   // first call of this game frame
    bool carry = false;   // move the camera state (lease phase, after its first frame)
    bool first = false;   // the lease phase opened on this call
    float delta = 0.f, height = 0.f;
};
// Counts game frames, not calls: a second CameraMove in the same frame gets the same springs.
class Orbit {
public:
    Tick Step(const Facts& f, const Scene& s, float n, const Config& c) {
        Tick t;
        const bool engaged = Engaged(f);
        if (phase_ == Phase::Off) {
            if (!engaged || f.lease == closed_ || !s.read || !std::isfinite(n)) return t;
            Open(f, s, c);
            yaw_.Reset(n); height_.Reset(0.f);
            t.first = true;
        } else if (phase_ == Phase::Release && engaged && f.lease != s_.lease && f.lease != closed_) {
            Close(); Open(f, s, c);   // a new Back Slice while easing back: the springs go on from here
            t.first = true;
        }
        if (known_ && s.read && s.frame == frame_ && !t.first) {   // a second call in this game frame
            if (!std::isfinite(n)) { Abort(End::Matrix); return {}; }
            t = last_; t.fresh = false; t.carry = false; t.first = false; t.delta = zt::Turn(n, yaw_.x);
            return t;
        }
        if (!s.read) { Abort(End::Read); return {}; }
        if (s.player != player_ || s.room != room_) { Abort(End::Room); return {}; }
        if (!s.healthy) { Abort(End::Dead); return {}; }
        if (!std::isfinite(n)) { Abort(End::Matrix); return {}; }
        known_ = true; frame_ = s.frame;
        if (phase_ == Phase::Lease) {
            End why = End::None;
            if (f.lease != s_.lease || !f.back) why = End::Lease;
            else if (!f.clean) why = End::Interrupted;
            else if (!f.owned) why = End::Lease;
            else if (s.damage) why = End::Damaged;
            if (why != End::None) Release(why);
        }
        const float h = game_clock::ValidDt(s.dt) / 30.f;
        float target = 0.f;
        if (phase_ == Phase::Lease) {
            ++s_.ticks; s_.last_e = float(f.e);
            if (std::isfinite(s.hips)) {
                const double v = double(s.hips) - (f.jump ? Jump60(f.e) : 0.0);
                ref_ = float(std::fmax(std::isfinite(ref_) ? double(ref_) : double(kStandHips), v));
                s_.hips_ref = ref_;
            }
            if (c.lift) target = VerticalTarget(f.e, f.jump, s.hips, ref_);
        } else { ++s_.release_ticks; release_s_ += h; }
        yaw_.Step(yaw_.x + zt::Turn(yaw_.x, n), h, float(c.ms) / 1000.f);
        yaw_.x = zt::Turn(0.f, yaw_.x);
        height_.Step(target, h, kCraneTau);
        const float d = zt::Turn(n, yaw_.x);
        if (std::fabs(d) * 57.29578f > s_.max_turn) s_.max_turn = std::fabs(d) * 57.29578f;
        if (height_.x > s_.max_lift) { s_.max_lift = height_.x; s_.peak_e = float(f.e); }
        if (height_.x < s_.max_dip) s_.max_dip = height_.x;
        if (phase_ == Phase::Release) {
            const bool done = std::fabs(d) < kDoneYaw && std::fabs(height_.x) < kDoneHeight;
            if (done || release_s_ > kReleaseMax) { s_.released = done; Close(); return {}; }
        }
        t.apply = true; t.fresh = true; t.delta = d; t.height = height_.x;
        t.carry = phase_ == Phase::Lease && carry_ok_;
        carry_ok_ = phase_ == Phase::Lease;
        last_ = t;
        return t;
    }
    // Native from this call: the record closes (a scene change, a lost frame or matrix, another camera mode, an
    // unwritable state).
    void Abort(End why) { if (phase_ != Phase::Off) { if (s_.end == End::None) s_.end = why; s_.released = false; Close(); } }
    // The lease ended (SwordLifecycle): ease back from here.
    void Finish(uint64_t lease, End why) { if (phase_ == Phase::Lease && s_.lease == lease) Release(why); }
    void Carried() { if (phase_ != Phase::Off) ++s_.carries; }
    void Held() { if (phase_ != Phase::Off) ++s_.holds; }
    void Snapped(float pull) { if (phase_ != Phase::Off) { ++s_.snaps; if (pull > s_.max_pull) s_.max_pull = pull; } }
    void Probed(float pull) { if (phase_ != Phase::Off) { ++s_.probe_pulls; if (pull > s_.max_probe) s_.max_probe = pull; } }
    void ProbeFault() { if (phase_ != Phase::Off) ++s_.probe_faults; }
    // A closed record waiting for its log line (taken once).
    bool Take(Summary& out) { if (!pending_) return false; out = done_; pending_ = false; return true; }
    Phase phase() const { return phase_; }
    uint64_t lease() const { return phase_ == Phase::Off ? 0 : s_.lease; }
    const Summary& summary() const { return s_; }
    float Yaw() const { return yaw_.x; }
    float Height() const { return height_.x; }

private:
    void Open(const Facts& f, const Scene& s, const Config& c) {
        s_ = Summary{}; s_.lease = f.lease; s_.ms = c.ms; s_.lift = c.lift; ref_ = NAN;
        phase_ = Phase::Lease; player_ = s.player; room_ = s.room; release_s_ = 0.f; carry_ok_ = false; known_ = false;
    }
    void Release(End why) {
        if (phase_ != Phase::Lease) return;
        if (s_.end == End::None) s_.end = why;
        closed_ = s_.lease; phase_ = Phase::Release; release_s_ = 0.f; carry_ok_ = false;
    }
    void Close() {
        if (phase_ == Phase::Off) return;
        if (s_.end == End::None) s_.end = End::Lease;
        closed_ = s_.lease; done_ = s_; pending_ = true; phase_ = Phase::Off; known_ = false; carry_ok_ = false;
    }
    Phase phase_ = Phase::Off;
    Summary s_, done_;
    Spring yaw_, height_;
    Tick last_;
    bool pending_ = false, known_ = false, carry_ok_ = false;
    uint64_t closed_ = 0;
    uint32_t frame_ = 0, player_ = 0;
    uint16_t room_ = 0;
    float release_s_ = 0.f, ref_ = NAN;
};

// ---- log line ---------------------------------------------------------------------------------------------
// One line per Back Slice lease (LogLevel 1).
inline int Format(char* out, size_t n, const Summary& s) {
    return std::snprintf(out, n,
        "bs_cam lease=%llu end=%s released=%u ticks=%u release_ticks=%u carries=%u holds=%u snaps=%u max_pull=%.0f "
        "probe_pulls=%u max_probe=%.0f probe_faults=%u max_turn=%.1f max_lift=%.0f max_dip=%.0f peak_e=%.1f last_e=%.1f "
        "hips_ref=%.0f ms=%u lift=%u",
        static_cast<unsigned long long>(s.lease), EndName(s.end), s.released ? 1U : 0U, s.ticks, s.release_ticks,
        s.carries, s.holds, s.snaps, double(s.max_pull), s.probe_pulls, double(s.max_probe), s.probe_faults,
        double(s.max_turn), double(s.max_lift), double(s.max_dip), double(s.peak_e), double(s.last_e), double(s.hips_ref),
        s.ms, s.lift);
}
}  // namespace link_runtime::back_slice::camera

#pragma once
// Ending Blow camera lift (17-oct-2026; design wf_b42ad301 and its challenge, docs/ending-blow-leap-2026-10-16.md
// "Salto epico" section): the gameplay camera rises with the Ending Blow's 2 m visual jump, so Link's head stays in frame.
// Pure: the curve (shared with the clip; tests/test_ending_blow_camera_pins.py pins it to scripts/spin_ending_assets.py),
// the offset lift, the CameraSmooth bookkeeping, the ceiling cap and the per-lease tracker. The hitCheck stage lives in
// ztarget_runtime.h (PrepareLiftWith / FinishLiftWith / LiftAfterFrontWith), the lease facts in spin_attack_runtime.h
// (SwordCameraFacts). Pinned bio4.exe SHA 19aed4af...; RVAs at base 0x400000. EndingBlowCameraLift=0 runs none of it.
//
// Where (cam_ctrl.cpp CameraControl::move, UHD 0x2F6E..0x2F713A): mode 0xA runs CameraQuasiFPS::move (0x19E130). Its
// hitCheck (0x19C880, thunk 0x742D, called at 0x19E221) takes the base matrix M and the local QFPS_OFFSET and casts the
// collision ray from M*campos[1] (Link's shoulder) to M*campos[0] (the camera). We add d = M^-1 (0, L, 0) to campos[0]
// and target on a copy, never campos[1]: in the open the world camera and target rise by exactly L, under a low ceiling
// the native ray pulls the camera in. hitCheck's camera is local like its offsets (QFPS::move applies M afterwards).
// Then the water clamp (can only raise), m_Inter.move (CamCtrl+0x254: while its counter runs, param = (1 - 1/c) param
// + (1/c) cur, else param = cur), CamSmth.move (0x2F2DA0: m_effect = r (m_effect + quake) + (1 - r) m_Inter.param,
// r = m_ratio ^ deltaTime, ^ 1.5 deltaTime under re4_fps CameraImprovements on keyboard and mouse; its tail keeps
// m_effect in a backup at +0x130 and, when the camera comes within ~230 of Link's head, shoves m_effect aside and sets
// the UHD hold byte +0x12C, so the next move starts from the backup) and pG->Camera = m_effect (pG+0x118, +0x124).
// CamSmth (challenge 1): alone the smoothing reaches 0.27-0.75 of the lift and leaves the camera ~0.5 m high at the
// stab. So each tick the stage also adds L_now - L_applied to Campos.y and Target.y of m_effect, its backup and
// m_Inter.param (Smooth; fix-08: m_Inter low-passes cur while it interpolates, after the kick camera, and the hold
// restores the backup): with the lift already in every state the chain sees native + L on both sides, the final
// camera is the native one plus L exactly at any ratio, counter, deltaTime or factor, and from e = 58 and on the abort
// tick it is native again. A second CameraMove in a tick adds 0. The pins below hold the layout, the formula, the tail
// and the copy to the final camera.
// Ceiling (challenge 2, fix-08 review): hitCheck has no vertical probe and puts a pulled camera on the ceiling
// surface, target inside it. Only a pull that follows the lift counts (Tracker::Pull): one beyond the native-ray
// baseline by more than kPullThreshold (above ZNEAR 100: the side probes' own nudge) steps the lift down as a trial;
// if the pull dropped with it, a ceiling: the cap stays and only goes down for the rest of the lease; if not, a wall
// or a pillar the leap carried into the ray: new baseline, the step is undone.
// Accepted (challenge 8): an abort or a cap step drops the camera at once (a cut of up to 1.7 m with ratio 0), an
// undone trial is a dip of one step; Link's own model rises to ~3.8 m and clips through any lower ceiling whatever the
// camera does.
#include "ztarget_native.h"
#include "game_clock.h"
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace link_runtime::ending_blow::camera {
namespace zt = link_runtime::ztarget;
using zt::Vec;
using zt::Matrix;
using zt::Offset;
// scripts/spin_ending_assets.py JUMP_HEIGHT, JUMP_TAKEOFF, JUMP_APEX_FRACTION, LAND (TP frames, 30 Hz) and
// STRIKE['ending_blow'] (game frames, 60 Hz). Pinned by tests/test_ending_blow_camera_pins.py.
constexpr double kHeight = 2000.0, kTakeoffTp = 2.0, kApexFraction = 0.4, kLandTp = 29.0;
constexpr unsigned kStrikeTick = 60;
constexpr double kLand60 = 2.0 * kLandTp;  // 58 game frames: no lift from here on
// The 2 m clip (spin_attack_assets.h ending_blow.fcv_sha; D:/link-in-re4-work/ending-blow-arc/assets-01): startup
// refuses the lift with any other clip, whose arc this curve would not follow.
constexpr char kClipSha[] = "42f3e4e26704a50e71ba2f11df5173cb75c3ceee936d04270469fd795597c3a5";
// e (game frames since the lease start), the clip's root lift in BIN = jump_height(e / 2, LAND).
constexpr double kGolden[][2] = {{0, 0}, {4, 0}, {5, 180.898491}, {8, 672.153635}, {10, 956.790123}, {14.8, 1500},
                                 {20, 1865.569273}, {25.6, 2000}, {30, 1963.115379}, {41.8, 1500}, {50, 865.721689},
                                 {57, 121.551593}, {58, 0}, {60, 0}};
// EndingBlowCameraLift: percent of the clip's lift the camera follows (verify-01: the crown stays in frame with
// k 0.55-0.85, the whole body needs 0.7-1.05); 0 = off.
constexpr unsigned kDefaultPercent = 85, kMaxPercent = 120;
constexpr float kZNear = 100.f;           // cam_ctrl ZNEAR: hitCheck's side-probe nudge toward the shoulder
constexpr float kPullThreshold = 150.f;   // pull beyond the baseline that may be the lift hitting a ceiling
constexpr float kTrialStep = 150.f;       // the least a trial lowers the lift: a ceiling's pull then drops with it

// Clip root lift at game time e: 0 up to the takeoff (TP 2) and from LAND (TP 29), quadratic up to the apex
// (TP 12.8 = game 25.6) and quadratic down into the stab. NaN, inf and negative e give 0.
inline double Height60(double e) {
    const double f = e / 2.0;
    if (!(f > kTakeoffTp && f < kLandTp)) return 0.0;
    const double apex = kTakeoffTp + kApexFraction * (kLandTp - kTakeoffTp);
    const double u = f < apex ? (apex - f) / (apex - kTakeoffTp) : (f - apex) / (kLandTp - apex);
    return kHeight * (1.0 - u * u);
}
inline float Lift(double e, unsigned percent) {
    if (!percent) return 0.f;
    if (percent > kMaxPercent) percent = kMaxPercent;
    return float(double(percent) / 100.0 * Height60(e));
}

// ---- matrix ----------------------------------------------------------------------------------------
// General 3x3 inverse of M's rotation part (challenge 4: outside kamae calcBaseMatrix tilts the up axis toward the
// floor normal without renormalising, so the other axes shrink to cos theta; the transpose would be wrong there and
// Rigid() fails from ~5.7 degrees). The determinant must stay within 0.5..1.5: a rotation is 1, cos 30 deg 0.87.
struct Inverse { float v[3][3]{}; bool ok = false; };
inline Inverse Invert(const Matrix& m) {
    Inverse r;
    for (const auto& row : m.v) for (float x : row) if (!std::isfinite(x)) return r;
    const double a = m.v[0][0], b = m.v[0][1], c = m.v[0][2], d = m.v[1][0], e = m.v[1][1], f = m.v[1][2],
                 g = m.v[2][0], h = m.v[2][1], i = m.v[2][2];
    const double A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    if (!(det >= 0.5 && det <= 1.5)) return r;
    const double k = 1.0 / det;
    const double out[3][3] = {{A * k, (c * h - b * i) * k, (b * f - c * e) * k},
                              {B * k, (a * i - c * g) * k, (c * d - a * f) * k},
                              {C * k, (b * g - a * h) * k, (a * e - b * d) * k}};
    for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 3; ++x) r.v[y][x] = float(out[y][x]);
    r.ok = true;
    return r;
}
inline Vec Mul(const Inverse& n, Vec v) {
    return {n.v[0][0] * v.x + n.v[0][1] * v.y + n.v[0][2] * v.z, n.v[1][0] * v.x + n.v[1][1] * v.y + n.v[1][2] * v.z,
            n.v[2][0] * v.x + n.v[2][1] * v.y + n.v[2][2] * v.z};
}
// The lifted copy: campos[0] and target + M^-1 (0, L, 0); campos[1] (the ray start), roll and fovy bit for bit.
// False for no lift (L <= 0, NaN), a matrix outside the determinant band or a non-finite result.
inline bool Apply(const Matrix& m, const Offset& in, float lift, Offset& out) {
    if (!(lift > 0.f) || !std::isfinite(lift) || !zt::Finite(in.campos[0]) || !zt::Finite(in.target)) return false;
    const Inverse n = Invert(m);
    if (!n.ok) return false;
    const Vec d = Mul(n, {0.f, lift, 0.f});
    if (!zt::Finite(d)) return false;
    out = in;
    out.campos[0] = {in.campos[0].x + d.x, in.campos[0].y + d.y, in.campos[0].z + d.z};
    out.target = {in.target.x + d.x, in.target.y + d.y, in.target.z + d.z};
    return zt::Finite(out.campos[0]) && zt::Finite(out.target);
}
// ---- hitCheck's pull ------------------------------------------------------------------------------------
// hitCheck's output camera is in M's local space, like the offsets (fix-08: cam_qfps.cpp move applies M after it; UHD
// 0x19CDD2 copies ofs->campos[0] to out unchanged and the hit branches store M^-1 hit). Every branch leaves it on the
// ray from campos[1] (the shoulder) to the asked campos[0], pulled toward the shoulder (ZNEAR more on a side probe).
// pull = |out - asked|; ratio = |asked - shoulder| / |native - shoulder| (1 unlifted). The lift moves only the camera
// end, straight up in the world, so a vertical wall cuts the ray at the same fraction at any lift: pull / ratio is its
// native pull whatever the lift. A ceiling's grows with the lift. NaN when unknown.
struct Probe { float pull = NAN, ratio = NAN; };
inline float Distance(Vec a, Vec b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}
inline Probe Measure(Vec native, Vec asked, Vec shoulder, Vec out) {
    Probe p;
    if (!zt::Finite(native) || !zt::Finite(asked) || !zt::Finite(shoulder) || !zt::Finite(out)) return p;
    const float span0 = Distance(native, shoulder), span = Distance(asked, shoulder), pull = Distance(out, asked);
    if (!(span0 >= 1.f) || !std::isfinite(span) || !std::isfinite(pull)) return p;
    p.pull = pull; p.ratio = span / span0;
    return p;
}

// ---- CameraSmooth ------------------------------------------------------------------------------------
namespace rva {
constexpr uint32_t smooth = 0x870180;             // CameraSmooth CamSmth (ecx at 0x2F70F1)
constexpr uint32_t smooth_flag = smooth + 0xF8;   // m_flag: bit 0 snaps m_effect to the next input
constexpr uint32_t smooth_ratio = smooth + 0xFC;  // m_ratio (0.8 after QFPS init or walking, 0 for motion cameras)
constexpr uint32_t smooth_campos = smooth + 0x100, smooth_target = smooth + 0x10C;  // m_effect Campos, Target (world)
constexpr uint32_t smooth_hold = smooth + 0x12C;  // UHD byte: set by the tail's near-head shove; the next move restores
constexpr uint32_t smooth_backup = smooth + 0x130;  // m_effect from this backup (Campos, Target +0xC: CAMERA_POINT)
constexpr uint32_t inter = zt::rva::ctrl + 0x254;        // CamCtrl.m_Inter: its CAMERA_POINT param is CamSmth's input
constexpr uint32_t inter_frame = zt::rva::ctrl + 0x274;  // m_Inter.frame (float, ticks of interpolation left)
}  // namespace rva
// Relocation-normalized bytes (native_pins.h). re4_fps rewrites 0x2F2E4E..0x2F2E56 (the deltaTime load, x1.5 under
// CameraImprovements; CameraTweaks.cpp CSMoveCamDelta), so that span is not pinned; no re4_tweaks pattern touches
// the rest (fps-patterns.json, 17-oct; the tail too, fix-08).
inline constexpr native_pins::Pin kSmoothPins[] = {
    {0xB82A, "e971752e00"},  // CamSmth.move thunk -> 0x2F2DA0
    // Entry, m_flag bit 0 snap (m_effect = input, backup to +0x130, hold = 0), hold restore, quake add to Campos.
    {0x2F2DA0, "558bec83ec54a1105ec30033c58945fc538bd98b83f8000000568b7508578975bca801743583e0feb9080000008dbb000100008983f8000000f3a58dbb30010000b9080000008db300010000c6832c01000000f3a5e93401000080bb2c0100000074138db3300100008dbb00010000b908000000f3a5a13c6fc0008dbb000100"},
    {0x2F2E20, "005705744f00005057e83551d1ff8b0d3c6fc0008db30c0100005681c1744f00005156e81b51d1ffd983fc000000"},
    // pow, then m_effect.Campos = r m_effect + (1 - r) input and the same for Target (input + 0xC).
    {0x2F2E57, "83c418e851ba3700d95db8d945b851d95db4d945b4d91c245757e82a69d1ffd945b4d9e88b4dbcdee183c4088d45c0dd55acd95db8d945b8d91c245051e80769d1ff578d55c05257e8bf50d1ffd945b483c414d91c245656e8ec68d1ffd945b88b7dbcd95c240883c4088d45c0508d4f0c51e8d268d1ff568d55c05256e88a50"},
    {0x2F2ED7, "d1ffd98318010000d945b483c418d9c0decad9c9d95dbcd945bcd99318010000d94718dd45acdcc9d9c9dec2d9c9d99b18010000d9831c010000decad9c9d95dbcd945bcd9931c010000d9471cdecadec1d99b1c010000"},
    // Tail (fix-08): backup = m_effect, hold = 0; in mode 0xA (CamCtrl r0 [0xC70304]) 0x2F2C10 checks m_effect against
    // Link's head (matrix 0xC706C0) and, when it shoved it aside, hold = 1.
    {0x2F2F2E, "8dbb30010000b9080000008db300010000c6832c01000000f3a5803d0403c7000a752a8d45cc5068c006c700e8009ad1ff83c4088db3000100008d7dcce8a0fcffff84c07407c6832c01000001"},
    // CameraControl::move case 0xA: QFPS move (its hitCheck), cur = its camera.
    {0x2F6FEB, "8d8b78020000ddd8e8613cd1ff8db31c0300008d7b38b908000000f3a5"},
    // m_Inter.move(&cur); while m_Inter runs CamSmth's snap flag is cleared; CamSmth.move(m_Inter); camera.param = m_effect.
    {0x2F70C9, "8d4b388db354020000518bcee8da58d1ffd9eed89b74020000dfe0f6c4057a0783257802c700fe56b98001c700e82f47d1ff8d53608dbb04010000b908000000be8002c70052f3a5e85169d1ff"}};
// The lift already in the camera chain's state. Delta(want) is what to add to Campos.y and Target.y of m_effect, its
// backup and m_Inter.param this tick for a lift `want` (0 removes it); after Commit a second call in the tick gives 0.
// Drop forgets it without a write: hitCheck stopped (a new camera sets ratio 0 or the QFPS init snaps) or the state
// could not be read or written; whatever is left eases out natively.
class Smooth {
public:
    float Applied() const { return applied_; }
    float Delta(float want) const { return want - applied_; }
    void Commit(float want) { applied_ = want; }
    void Drop() { applied_ = 0.f; }
private:
    float applied_ = 0.f;
};
// The CamSmth factor f in r = ratio ^ (deltaTime f), from one move (log only; 1, or 1.5 under re4_fps CameraImprovements
// on keyboard and mouse): before/after = the smoothed Campos around the move (the backup while hold is set: the move
// starts from it, and the tail's shove is not smoothing), input = m_Inter's Campos. -1 when the step is too small to
// tell (under 20 BIN), not a contraction, or the ratio or deltaTime is unusable.
inline float Factor(Vec before, Vec after, Vec input, float ratio, float dt) {
    const Vec a{before.x - input.x, before.y - input.y, before.z - input.z}, b{after.x - input.x, after.y - input.y, after.z - input.z};
    const float aa = zt::Dot(a, a);
    if (!(aa >= 400.f) || !zt::Finite(b) || !(ratio > 0.f && ratio < 1.f) || !zt::ValidDt(dt)) return -1.f;
    const double r = double(zt::Dot(a, b)) / double(aa);
    if (!(r > 0.0 && r < 1.0)) return -1.f;
    const double f = std::log(r) / (double(dt) * std::log(double(ratio)));
    return std::isfinite(f) && f > 0.0 ? float(f) : -1.f;
}

// ---- per lease ---------------------------------------------------------------------------------------
// Why the lift stopped for a lease (latched: 0 for the rest of it). landed: e reached 58 (the normal end).
enum class End : uint8_t { None, Landed, Finished, Interrupted, Damaged, Dead, Room, Lease, Camera, NotRigid, Smooth, Read };
inline const char* EndName(End e) {
    static const char* const n[] = {"none", "landed", "finished", "interrupted", "damaged", "dead", "room", "lease",
                                    "camera", "not_rigid", "smooth", "read"};
    return n[unsigned(e)];
}
// From the sword runtime (spin_attack_runtime.h SwordCameraFacts) on the game thread, inside the camera's hitCheck.
struct Facts {
    uint64_t lease = 0;     // g_sword_action.lease (0: none)
    bool ending = false;    // an Ending Blow lease (a Spin lease never lifts)
    bool clean = false;     // no interruption flagged
    bool owned = false;     // the dispatch still runs this lease (run.active, same token)
    double e = 0;           // game frames since the lease start (game_clock, 60 Hz)
    float mot = NAN;        // the dispatch model's Mot_frame (cModel +0x1D8 +0x24), log only
};
// The camera stage's own fresh read (ztarget ReadFrame) of the same tick.
struct Scene { bool read = false; uint32_t frame = 0, player = 0; uint16_t room = 0, damage = 0; bool healthy = false; };
struct Summary {
    uint64_t lease = 0;
    End end = End::None;
    unsigned percent = 0, ticks = 0, writes = 0, dry = 0, pulled = 0, state = 0;
    float peak = 0, peak_e = -1, first_e = -1, last_e = -1;
    unsigned undone = 0;                             // trials whose pull did not follow the lift (a wall, a pillar)
    float cap = -1, max_pull = 0, base_pull = -1;   // ceiling cap (-1 none); the largest pull beyond the baseline
    float ratio = -1, inter = -1, f = -1, mot0 = NAN;  // CamSmth m_ratio and m_Inter.frame at the first tick, f estimate
};
struct Tick { float lift = 0; bool open = false, fresh = false, first = false; };
// Counts game frames, not calls: a second CameraMove in the same frame gets the same lift and counts nothing.
class Tracker {
public:
    Tick Step(const Facts& f, const Scene& s, unsigned percent) {
        Tick t;
        if (!f.lease || !f.ending) { if (open_) Close(End::Lease); return t; }
        if (open_ && s_.lease != f.lease) Close(End::Lease);
        if (!open_) {
            if (f.lease == closed_) return t;   // ended (Finish) already: never reopened
            s_ = Summary{}; s_.lease = f.lease; s_.percent = percent; open_ = true; latched_ = false; frame_known_ = false;
            player_ = s.player; room_ = s.room; cap_ = -1.f; clear_ = 0.f; base_ = 0.f; have_base_ = false; trial_ = false;
            confirmed_ = false; bucket_ = -1; last_ = 0.f;
            t.first = true;
        }
        t.open = true;
        if (frame_known_ && s.read && s.frame == frame_) { t.lift = last_; return t; }
        frame_known_ = s.read; frame_ = s.frame; t.fresh = true; ++s_.ticks;
        const float e = float(f.e);
        if (s_.first_e < 0.f) s_.first_e = e;
        s_.last_e = e;
        if (!latched_) {
            End why = End::None;
            if (!s.read || !std::isfinite(f.e)) why = End::Read;
            else if (!f.clean) why = End::Interrupted;
            else if (!f.owned) why = End::Lease;
            else if (s.player != player_ || s.room != room_) why = End::Room;
            else if (s.damage) why = End::Damaged;
            else if (!s.healthy) why = End::Dead;
            else if (game_clock::Reached(f.e, kLand60)) why = End::Landed;
            if (why != End::None) Latch(why);
        }
        float lift = latched_ ? 0.f : Lift(f.e, percent);
        if (cap_ >= 0.f && lift > cap_) lift = cap_;
        if (lift > s_.peak) { s_.peak = lift; s_.peak_e = e; }
        last_ = lift; t.lift = lift;
        return t;
    }
    // 0 for the rest of this lease (not rigid, the camera state unreadable or unwritable, camera mode changed).
    void Abort(End why) { if (open_ && !latched_) Latch(why); }
    // The lease ended (SwordLifecycle): close its record for the log. True when it was this tracker's lease.
    bool Finish(uint64_t lease, End why) {
        if (!open_ || s_.lease != lease) return false;
        Close(why); closed_ = lease;
        return true;
    }
    // After hitCheck, once per frame: Measure's pull and ratio, lift = what was applied (0: native offsets). The
    // ceiling cap counts only a pull that follows the lift (fix-08 review: a baseline taken once at e <= 4 called any
    // wall the leap met a ceiling, and the cap then ratcheted to 0 in steps of the wall's pull, each a camera cut).
    // n = pull / ratio is the pull on the native ray; the baseline is n with the lift at 0 and follows n down (the leap
    // carried Link away from a wall). extra = (n - base) ratio is what the lift added. Beyond kPullThreshold the lift
    // steps down as a trial: rising into it, to the last lift that was not pulled; under a ceiling already confirmed
    // that came lower, by the whole extra; otherwise (the lift falling or held) by kTrialStep, the least step either
    // way. The next frame decides: the pull dropped with the lift (by half the step, or of the extra when that is
    // less): a ceiling, the step stays and the cap only goes down for the rest of the lease. It did not: a wall or a
    // pillar, so the baseline takes it and the cap is restored (a dip of one step, not a ratchet).
    void Pull(float pull, float ratio, float lift) {
        if (!open_ || !std::isfinite(pull) || !(ratio > 0.f) || !std::isfinite(ratio)) return;
        const float n = pull / ratio;
        if (!(lift > 0.f)) { Base(n); trial_ = false; return; }
        if (have_base_ && n < base_) Base(n);
        const float extra = (n - (have_base_ ? base_ : 0.f)) * ratio;
        if (extra > s_.max_pull) s_.max_pull = extra;
        if (trial_) {
            trial_ = false;
            const float step = trial_lift_ - lift, drop = (trial_n_ - n) * trial_ratio_;
            if (!(step > 0.f) || !(drop >= 0.5f * (step < trial_extra_ ? step : trial_extra_))) {
                ++s_.undone; Base(n); cap_ = trial_cap_; s_.cap = cap_; clear_ = lift;
                return;
            }
            confirmed_ = true;
        }
        if (extra > kPullThreshold) {
            ++s_.pulled;
            float c = clear_ < lift ? clear_ : confirmed_ ? lift - extra : lift - kTrialStep;
            if (c > lift - kTrialStep) c = lift - kTrialStep;
            if (!(c > 0.f)) c = 0.f;
            if (cap_ < 0.f || c < cap_) {
                trial_ = true; trial_lift_ = lift; trial_n_ = n; trial_ratio_ = ratio; trial_extra_ = extra; trial_cap_ = cap_;
                cap_ = c;
            }
            s_.cap = cap_;
        } else clear_ = lift;
    }
    void Start(float ratio, float inter, float mot) { if (open_) { s_.ratio = ratio; s_.inter = inter; s_.mot0 = mot; } }
    void Factor(float f) { if (open_ && s_.f < 0.f && f > 0.f) s_.f = f; }
    void Wrote() { if (open_) ++s_.writes; }
    void Dry() { if (open_) ++s_.dry; }
    void Smoothed() { if (open_) ++s_.state; }
    // LogLevel 2 samples: true once per 4 game frames of the open lease.
    bool Sample(double e) {
        if (!open_ || !std::isfinite(e)) return false;
        const int b = int(std::floor(e / 4.0));
        if (b == bucket_) return false;
        bucket_ = b;
        return true;
    }
    // A closed record waiting for its log line (taken once).
    bool Take(Summary& out) { if (!pending_) return false; out = done_; pending_ = false; return true; }
    bool Open() const { return open_; }
    bool Latched() const { return latched_; }
    float Cap() const { return cap_; }
    bool Trial() const { return trial_; }
    bool Confirmed() const { return confirmed_; }
    const Summary& summary() const { return s_; }

private:
    void Latch(End why) { latched_ = true; if (s_.end == End::None) s_.end = why; }
    void Close(End why) {
        if (!open_) return;
        if (s_.end == End::None) s_.end = why;
        done_ = s_; pending_ = true; open_ = false;
    }
    void Base(float n) { base_ = n; have_base_ = true; s_.base_pull = n; }
    Summary s_, done_;
    bool open_ = false, latched_ = false, pending_ = false, frame_known_ = false, have_base_ = false, trial_ = false;
    bool confirmed_ = false;      // a trial's pull followed the lift down this lease: a ceiling
    uint64_t closed_ = 0;
    uint32_t frame_ = 0, player_ = 0;
    uint16_t room_ = 0;
    float cap_ = -1.f, clear_ = 0.f, base_ = 0.f, last_ = 0.f;
    float trial_lift_ = 0.f, trial_n_ = 0.f, trial_ratio_ = 1.f, trial_extra_ = 0.f, trial_cap_ = -1.f;  // the pending step
    int bucket_ = -1;
};

// ---- log lines -----------------------------------------------------------------------------------------
// One line per Ending Blow lease (LogLevel 1). first_e/mot0: e and the dispatch model's Mot_frame at the first tick
// (the clip's own frame units, not checked live yet); with the LogLevel 2 samples they fix the e-to-pose offset once
// (challenge 6: at most one tick).
inline int Format(char* out, size_t n, const Summary& s) {
    return std::snprintf(out, n,
        "ending_cam_lift lease=%llu end=%s percent=%u ticks=%u writes=%u dry=%u state=%u peak=%.0f peak_e=%.2f "
        "first_e=%.2f last_e=%.2f pulled=%u cap=%.0f undone=%u max_pull=%.0f base_pull=%.0f ratio=%.3f inter=%.1f f=%.2f "
        "mot0=%.2f",
        static_cast<unsigned long long>(s.lease), EndName(s.end), s.percent, s.ticks, s.writes, s.dry, s.state,
        double(s.peak), double(s.peak_e), double(s.first_e), double(s.last_e), s.pulled, double(s.cap), s.undone,
        double(s.max_pull), double(s.base_pull), double(s.ratio), double(s.inter), double(s.f), double(s.mot0));
}
// LogLevel 2, every 4 game frames: lift = the curve (capped), applied = what is in the camera (0 with observe), qfps_y =
// hitCheck's camera (M's local Y), smooth_y = CamSmth after its move, final_y = pG+0x118, native_y = final_y - applied.
struct Sample {
    uint64_t lease = 0;
    double e = 0;
    float mot = NAN, lift = 0, applied = 0, qfps_y = NAN, smooth_y = NAN, final_y = NAN, pull = NAN;
};
inline int FormatSample(char* out, size_t n, const Sample& s) {
    return std::snprintf(out, n,
        "ending_cam_sample lease=%llu e=%.2f mot=%.2f lift=%.0f applied=%.0f qfps_y=%.0f smooth_y=%.0f final_y=%.0f "
        "native_y=%.0f pull=%.0f",
        static_cast<unsigned long long>(s.lease), s.e, double(s.mot), double(s.lift), double(s.applied), double(s.qfps_y),
        double(s.smooth_y), double(s.final_y), double(s.final_y - s.applied), double(s.pull));
}
}  // namespace link_runtime::ending_blow::camera

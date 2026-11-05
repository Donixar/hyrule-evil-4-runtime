#pragma once
// Master Sword Blade Lock H2c: the duel's presentation (docs/swordlock-h2c-2026-11-04.md; design
// docs/idea-ambiciosa-2026-11-03.md, "Animacion y sonido", "Exito y fallo", "Camino al 100%" H2). Lab DLL only:
// link_runtime.cpp includes it inside #if LINK_SWORD_LOCK_LAB, so the public build never sees it. Pure: no hooks, no
// process access. Three pieces, each behind its own lab key ([Combat], missing = 1, never in a public or default INI):
//
//  * SwordLockCamera: the duel camera. A stage after the Back Slice orbit in the same hitCheck hook
//    (ztarget_runtime.h HitDetour, lab-guarded): while the duel holds the world, hitCheck gets a camera that frames Link
//    and the Dr. Salvador from the side of the blade contact (DuelView: the native camera's side, a little behind Link,
//    the distance growing with their separation), swung there round the contact point (Blend: polar about the look
//    point, so it never cuts through Link) by a critically damped weight (bsc::Spring, kInTau: ~90 % inside the Traba's
//    0.35 s). On top, three game-time envelopes (Director): a continuous low shake that grows with |PV| (the contention),
//    TP's demo-50 shake of up to kMissUnits on each missed break, and a short push of the camera toward the Salvador
//    on each hit break. Every exit (win, lose, draw, abandon) eases the weight back to 0 (kOutTau) with the camera chain
//    native underneath, so the hand-back is never a snap; a frame that cannot be read ends it at once (fail closed,
//    native from that call, logged). The wall care is the Back Slice's: hitCheck's own ray from the shoulder, the EatMgr
//    probe from the look point to the camera (bsc::ProbeRay / EaseProbe) and CamSmth's snap bit when a wall pulled it.
//  * SwordLockBars: the F-09 cinema bars (hud_tp.h, [Hud] ZTargetLetterbox's two units and their 0.27 s ease) in at the
//    lock and out at the end. One owner at a time (Bars): the duel while it holds the world, else the Z-target lock; a
//    hand-over between them keeps the bars up (no out-and-in). A pause hides them (the duel's own pause and the
//    painter's out-of-play drop).
//  * SwordLockSway: only the owner moves (Sway): along the Link-owner axis, PA x kSwayPerPa (the design's x1.3, see
//    kSwayGain), away from Link while Link wins, leaning in while the Salvador wins, plus a recoil slide on each hit
//    break; never closer than kMinSepMm to Link, never further back than the EatMgr probe found clear (unknown: no
//    move away at all), eased by a spring and written back to his exact start on every exit.
//
// Every time is game time (pG+0x70 or the duel's game_clock): the same at 60 and 165 FPS.
// Tests: tests/native/test_sword_lock_cinema.cpp (scripts/test_sword_lock_cinema.bat).
#include "back_slice_camera.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace link_runtime::sword_lock::cinema {
namespace bsc = link_runtime::back_slice::camera;
namespace zt = link_runtime::ztarget;
using zt::Vec;

// ---- [Combat] SwordLockCamera / SwordLockBars / SwordLockSway (lab DLL only): missing = 1, "0" = off, anything else
// counts a problem and stays on.
struct Config {
    bool camera = true, bars = true, sway = true;
    float fov = 1.f;   // candidate 7: [Combat] SwordLockCameraFov, the duel's fovy factor (ReadConfig: missing = 1.15)
};
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[16]{};
    const auto one = [&](const wchar_t* key, bool& out) {
        v[0] = 0; get(key, v, 16);
        if (!v[0] || (v[0] == L'1' && !v[1])) out = true;
        else if (v[0] == L'0' && !v[1]) out = false;
        else {out = true; ++problems;}
    };
    one(L"SwordLockCamera", c.camera);
    one(L"SwordLockBars", c.bars);
    one(L"SwordLockSway", c.sway);
    // candidate 7: SwordLockCameraFov, a plain decimal 1.0..1.4 (1 = the native fovy); missing = 1.15
    v[0] = 0; get(L"SwordLockCameraFov", v, 16);
    c.fov = 1.15f;
    if (v[0]) {
        double x = 0, frac = 0.1; bool dot = false, digit = false, bad = false;
        for (const wchar_t* p = v; *p && !bad; ++p) {
            if (*p == L'.' && !dot) {dot = true; continue;}
            if (*p < L'0' || *p > L'9') {bad = true; break;}
            digit = true;
            if (!dot) x = x * 10 + (*p - L'0'); else {x += (*p - L'0') * frac; frac *= 0.1;}
        }
        if (bad || !digit || x < 1.0 - 1e-9 || x > 1.4 + 1e-9) ++problems; else c.fov = static_cast<float>(x);
    }
    return c;
}

// ---- small helpers ------------------------------------------------------------------------------------------------
inline float Clamp(float x, float lo, float hi) {return x < lo ? lo : x > hi ? hi : x;}
inline float Smooth01(float t) {t = Clamp(t, 0.f, 1.f); return t * t * (3.f - 2.f * t);}
inline Vec Add(Vec a, Vec b) {return {a.x + b.x, a.y + b.y, a.z + b.z};}
inline Vec Sub(Vec a, Vec b) {return {a.x - b.x, a.y - b.y, a.z - b.z};}
inline Vec Scale(Vec a, float k) {return {a.x * k, a.y * k, a.z * k};}
inline float LenXZ(Vec a) {return std::sqrt(a.x * a.x + a.z * a.z);}
inline float Len(Vec a) {return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);}
inline bool Finite(Vec a) {return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);}
// A seconds value from a raw game step: NaN, inf, negative or more than a quarter second is no time (a hitch or a load
// is never animated through).
inline double ValidH(double h) {return std::isfinite(h) && h > 0.0 && h <= 0.25 ? h : 0.0;}

// Deterministic noise (Numerical Recipes LCG): one seed per duel, so a replayed duel shakes the same.
struct Rng {
    uint32_t s = 1;
    uint32_t Next() {s = s * 1664525u + 1013904223u; return s;}
    float Unit() {return float(Next() >> 8) * (1.f / 8388608.f) - 1.f;}   // [-1, 1)
    unsigned Between(unsigned lo, unsigned hi) {return hi <= lo ? lo : lo + Next() % (hi - lo + 1);}
};
// TP's demo-50 shake: hold a random offset and slide to the next every `frames` TP frames (30 Hz), the span drawn from
// [lo, hi] each time. Unit amplitude per axis; the caller scales it. Game time only.
class Shaker {
public:
    void Reset(uint32_t seed, unsigned lo, unsigned hi) {
        rng_.s = seed ? seed : 1u; lo_ = lo; hi_ = hi; a_ = {}; b_ = Pick(); t_ = 0; span_ = Span();
    }
    Vec Step(double h) {
        t_ += ValidH(h);
        while (t_ >= span_) {t_ -= span_; a_ = b_; b_ = Pick(); span_ = Span();}
        const float k = Smooth01(float(t_ / span_));
        return {a_.x + (b_.x - a_.x) * k, a_.y + (b_.y - a_.y) * k, a_.z + (b_.z - a_.z) * k};
    }
private:
    // Less vertical, as TP's; inside the unit ball, so the shake's length never passes its amplitude (a lerp of two
    // points in the ball stays in it).
    Vec Pick() {
        Vec v{rng_.Unit(), rng_.Unit() * 0.6f, rng_.Unit()};
        const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (l > 1.f) v = {v.x / l, v.y / l, v.z / l};
        return v;
    }
    double Span() {return double(rng_.Between(lo_, hi_)) / 30.0;}
    Rng rng_; unsigned lo_ = 3, hi_ = 13; Vec a_{}, b_{}; double t_ = 0, span_ = 0.1;
};

// ---- the camera director ------------------------------------------------------------------------------------------
constexpr float kInTau = 0.075f;        // s: the weight's spring in (critically damped; 0.9 at ~0.29 s, inside the Traba)
constexpr float kOutTau = 0.11f;        // s: the hand-back (0.95 at ~0.52 s)
constexpr float kDoneWeight = 0.004f;   // the hand-back converged: native from here
constexpr double kReleaseMax = 1.5;     // s: the hand-back never runs longer (then native at once; the weight is ~0 by then)
constexpr float kLowBase = 1.5f, kLowGain = 6.5f;   // world units: the continuous shake, kLowBase + kLowGain |PV|
constexpr unsigned kLowLo = 3, kLowHi = 13;         // TP frames between the shake's targets (demo 50)
constexpr float kMissUnits = 20.f;      // world units: a missed break's shake at its peak (the design's "hasta 20")
constexpr double kMissS = 0.45;         // s: its decay
constexpr unsigned kMissLo = 1, kMissHi = 2;        // TP frames: the miss shake is fast
constexpr float kPushMm = 140.f;        // a hit break's push of the camera toward the Salvador
constexpr double kPushInS = 0.07, kPushOutS = 0.26;
constexpr float kLookShake = -0.5f;     // the look point moves this share of the camera's shake (opposite: a small tilt)

enum class Phase : uint8_t {Off, Hold, Release};
inline const char* PhaseName(Phase p) {return p == Phase::Hold ? "hold" : p == Phase::Release ? "release" : "off";}
// Why the camera went native. finished: the hand-back converged after the duel; timeout: it ran kReleaseMax; read: the
// frame, the matrix or the offsets did not read; owner: the Salvador's position did not; room: another scene or
// player; camera: hitCheck stopped (another camera mode); matrix: a degenerate matrix or a non-finite result; abort:
// the runtime stopped it (a fault elsewhere).
enum class End : uint8_t {None, Finished, Timeout, Read, Owner, Room, Camera, Matrix, Abort};
inline const char* EndName(End e) {
    static const char* const n[] = {"none", "finished", "timeout", "read", "owner", "room", "camera", "matrix", "abort"};
    return n[unsigned(e)];
}
// From the duel (game thread), every hitCheck call.
struct Facts {
    bool on = false;          // the duel holds the world (stage Duel or Waiting, the probe holding) and the key is on
    uint32_t id = 0;          // the duel's number (a new one reopens; a closed one never reopens)
    double pv = 0;            // the push value, -1..1
    unsigned hits = 0, misses = 0;   // the duel's counters (each step up: a push / a shake)
};
struct Shot {
    bool apply = false;       // hand hitCheck the framed copy
    bool first = false;       // the hold opened on this call
    float weight = 0;         // 0 native .. 1 the duel's view
    Vec shake{};              // world offset of the camera point (the look point gets kLookShake of it)
    float push = 0;           // mm of the camera toward the Salvador
    float low = 0, miss = 0;  // the two shake amplitudes (log, preview)
};
struct Summary {
    uint32_t id = 0;
    End end = End::None;
    bool released = false;            // the hand-back converged
    unsigned ticks = 0, release_ticks = 0, pushes = 0, shakes = 0;
    float in_ms = -1, release_ms = 0, max_shake = 0, max_push = 0, max_weight = 0;
    unsigned probe_pulls = 0, snaps = 0;
    float side = 0, dist = 0;
};
class Director {
public:
    // h: seconds of game time since the last fresh frame (0 for a second call in the same frame: the same shot).
    Shot Step(const Facts& f, double h) {
        h = ValidH(h);
        if (phase_ == Phase::Off) {
            if (!f.on || f.id == closed_) return {};
            Open(f);
        } else if (f.on && f.id != s_.id) {   // a new duel while handing back: it goes on from this weight
            const float w = weight_.x;
            Finish(End::Finished, false); Open(f); weight_.x = w;
        }
        Shot t; t.first = first_; first_ = false;
        if (phase_ == Phase::Hold && !f.on) {phase_ = Phase::Release; release_s_ = 0;}
        if (f.hits > hits_) {++s_.pushes; push_t_ = 0;}
        if (f.misses > misses_) {++s_.shakes; miss_t_ = 0;}
        hits_ = f.hits; misses_ = f.misses;
        if (h > 0) {
            const bool hold = phase_ == Phase::Hold;
            weight_.Step(hold ? 1.f : 0.f, float(h), hold ? kInTau : kOutTau);
            weight_.x = Clamp(weight_.x, 0.f, 1.f);
            time_ += h; push_t_ += h; miss_t_ += h;
            if (hold) {++s_.ticks; if (s_.in_ms < 0 && weight_.x >= 0.9f) s_.in_ms = float(time_ * 1000.0);}
            else {++s_.release_ticks; release_s_ += h;}
            low_v_ = low_.Step(h); miss_v_ = miss_.Step(h);
            const float c = Clamp(float(std::fabs(f.pv)), 0.f, 1.f);
            low_amp_ = phase_ == Phase::Hold ? kLowBase + kLowGain * c : low_amp_ * float(std::exp(-h / double(kOutTau)));
        }
        t.weight = weight_.x;
        const float miss = miss_t_ < kMissS ? kMissUnits * float((1.0 - miss_t_ / kMissS) * (1.0 - miss_t_ / kMissS)) : 0.f;
        t.low = low_amp_; t.miss = miss;
        t.shake = Add(Scale(low_v_, low_amp_), Scale(miss_v_, miss));
        t.push = push_t_ < kPushInS ? kPushMm * Smooth01(float(push_t_ / kPushInS)) :
                 push_t_ < kPushInS + kPushOutS ? kPushMm * float((1.0 - (push_t_ - kPushInS) / kPushOutS) * (1.0 - (push_t_ - kPushInS) / kPushOutS)) : 0.f;
        const float shake = Len(t.shake) * t.weight;
        if (shake > s_.max_shake) s_.max_shake = shake;
        if (t.push * t.weight > s_.max_push) s_.max_push = t.push * t.weight;
        if (t.weight > s_.max_weight) s_.max_weight = t.weight;
        if (phase_ == Phase::Release) {
            s_.release_ms = float(release_s_ * 1000.0);
            const bool done = weight_.x < kDoneWeight && push_t_ >= kPushInS + kPushOutS && miss_t_ >= kMissS;
            if (done || release_s_ > kReleaseMax) {Finish(done ? End::Finished : End::Timeout, done); return {};}
        }
        t.apply = true;
        return t;
    }
    // Native from this call (a fault): the record closes; this duel never reopens.
    void Abort(End why) {if (phase_ != Phase::Off) Finish(why, false);}
    void Framed(float side, float dist) {s_.side = side; s_.dist = dist;}
    void Probed() {if (phase_ != Phase::Off) ++s_.probe_pulls;}
    void Snapped() {if (phase_ != Phase::Off) ++s_.snaps;}
    bool Take(Summary& out) {if (!pending_) return false; out = done_; pending_ = false; return true;}
    Phase phase() const {return phase_;}
    bool Active() const {return phase_ != Phase::Off;}
    float weight() const {return weight_.x;}
    const Summary& summary() const {return s_;}
    uint32_t closed() const {return closed_;}
    // The side chosen at the hold's start (+1 / -1; 0 = not yet).
    float side() const {return side_;}
    void SetSide(float s) {side_ = s;}
private:
    void Open(const Facts& f) {
        s_ = Summary{}; s_.id = f.id; phase_ = Phase::Hold; weight_.Reset(0.f); time_ = 0; release_s_ = 0;
        hits_ = f.hits; misses_ = f.misses; push_t_ = kPushInS + kPushOutS; miss_t_ = kMissS; first_ = true; side_ = 0;
        low_.Reset(f.id * 2654435761u + 1u, kLowLo, kLowHi); miss_.Reset(f.id * 2246822519u + 7u, kMissLo, kMissHi);
        low_v_ = miss_v_ = {}; low_amp_ = 0;
    }
    void Finish(End why, bool released) {
        if (s_.end == End::None) s_.end = why;
        s_.released = released; closed_ = s_.id; done_ = s_; pending_ = true; phase_ = Phase::Off;
    }
    Phase phase_ = Phase::Off;
    Summary s_, done_;
    bsc::Spring weight_;
    Shaker low_, miss_;
    Vec low_v_{}, miss_v_{};
    float low_amp_ = 0, side_ = 0;
    double time_ = 0, release_s_ = 0, push_t_ = 1, miss_t_ = 1;
    unsigned hits_ = 0, misses_ = 0;
    uint32_t closed_ = 0;
    bool pending_ = false, first_ = false;
};

// ---- the duel's view ----------------------------------------------------------------------------------------------
constexpr float kLookY = 1100.f;        // the look point over Link's feet (the blades' height)
constexpr float kCamUp = 250.f;         // the camera over the look point
constexpr float kDistBase = 2300.f, kDistGain = 0.6f, kDistMin = 2300.f, kDistMax = 3600.f;
constexpr float kBehindRad = 0.35f;     // ~20 degrees behind Link from the pure side (more of the Salvador's face)
constexpr float kMinSepXZ = 1.f;        // Link and the Salvador closer than this (XZ): no axis, no view
// The camera's side of the Link-owner axis: +1 or -1, the native camera's (so the swing is the short one).
inline float SideOf(Vec link, Vec owner, Vec native_cam) {
    const float ux = owner.x - link.x, uz = owner.z - link.z, l = std::sqrt(ux * ux + uz * uz);
    if (!(l >= kMinSepXZ)) return 1.f;
    const float px = uz / l, pz = -ux / l;   // perpendicular
    const float d = (native_cam.x - link.x) * px + (native_cam.z - link.z) * pz;
    return d < 0.f ? -1.f : 1.f;
}
struct View {Vec cam{}, look{}; float dist = 0, sep = 0; bool ok = false;};
// Link (feet) and the Salvador (feet): the look point between them at the blades' height, the camera to `side` of the
// axis, kBehindRad toward Link's back, at a distance that grows with their separation.
inline View DuelView(Vec link, Vec owner, float side) {
    View v;
    const float ux0 = owner.x - link.x, uz0 = owner.z - link.z, sep = std::sqrt(ux0 * ux0 + uz0 * uz0);
    if (!(sep >= kMinSepXZ) || !Finite(link) || !Finite(owner) || !(side == 1.f || side == -1.f)) return v;
    const float ux = ux0 / sep, uz = uz0 / sep, px = side * uz, pz = -side * ux;
    v.sep = sep; v.dist = Clamp(kDistBase + kDistGain * (sep - 1000.f), kDistMin, kDistMax);
    v.look = {link.x + ux0 * 0.5f, link.y + kLookY, link.z + uz0 * 0.5f};
    const float c = std::cos(kBehindRad), s = std::sin(kBehindRad);
    v.cam = {v.look.x + (px * c - ux * s) * v.dist, v.look.y + kCamUp, v.look.z + (pz * c - uz * s) * v.dist};
    v.ok = Finite(v.cam) && Finite(v.look);
    return v;
}
// The native camera swung toward the duel's: polar about the duel's look point (the angle by the short turn, the
// horizontal radius and the height lerped), the look point lerped. w 0: the native pair bit for bit.
inline bool Blend(Vec ncam, Vec nlook, const View& d, float w, Vec& cam, Vec& look) {
    if (!d.ok || !Finite(ncam) || !Finite(nlook) || !std::isfinite(w)) return false;
    w = Clamp(w, 0.f, 1.f);
    if (w == 0.f) {cam = ncam; look = nlook; return true;}
    const Vec rn = Sub(ncam, d.look), rd = Sub(d.cam, d.look);
    const float an = std::atan2(rn.x, rn.z), ad = std::atan2(rd.x, rd.z);
    float da = ad - an;
    while (da > 3.14159265f) da -= 6.2831853f;
    while (da < -3.14159265f) da += 6.2831853f;
    const float a = an + da * w, r = LenXZ(rn) + (LenXZ(rd) - LenXZ(rn)) * w, y = rn.y + (rd.y - rn.y) * w;
    cam = {d.look.x + std::sin(a) * r, d.look.y + y, d.look.z + std::cos(a) * r};
    look = {nlook.x + (d.look.x - nlook.x) * w, nlook.y + (d.look.y - nlook.y) * w, nlook.z + (d.look.z - nlook.z) * w};
    return Finite(cam) && Finite(look);
}
// The shot on the blended pair: the shake (the look point a kLookShake share of it) and the push toward the Salvador,
// both scaled by the weight (they fade with the hand-back).
inline void Compose(const Shot& s, Vec owner_chest, Vec& cam, Vec& look) {
    const float w = Clamp(s.weight, 0.f, 1.f);
    cam = Add(cam, Scale(s.shake, w));
    look = Add(look, Scale(s.shake, w * kLookShake));
    const Vec to = Sub(owner_chest, cam);
    const float l = Len(to);
    if (s.push > 0.f && l > s.push * w + 400.f) cam = Add(cam, Scale(to, s.push * w / l));   // never onto the Salvador
}
// The wall probe (the Back Slice's EatMgr test): the share of look->cam the camera may use, as bsc::ProbeRay, eased out
// with bsc::EaseProbe by the caller. blocked(a, b, hit) as bsc::ProbeRay's.
template <class Blocked> bsc::Probe ProbeView(Blocked&& blocked, Vec look, Vec cam, float& t) {
    return bsc::ProbeRay(blocked, look, cam, t);
}
inline Vec Pull(Vec look, Vec cam, float t) {return Add(look, Scale(Sub(cam, look), Clamp(t, 0.f, 1.f)));}
// Candidate 6 (Donald's duel against a wall, 4-nov: the camera right behind Link, his back filling the screen; the log:
// snaps=1293, hitCheck pulled the camera in every frame): the room the camera has from the look point decides
//  * the side at the start (PickSide): the native side unless its room is under kMinFrameMm and the other side has more;
//  * a rise while the room stays short (Raise): the camera goes up, looking down over Link's shoulder at both of them,
//    never closer than the wall lets it be (the rise is vertical: no new wall crossing), up to kRaiseMaxMm.
constexpr float kMinFrameMm = 1500.f;   // closer than this to the look point, Link's back fills the frame
constexpr float kRaiseMaxMm = 1100.f;   // the most the camera rises over a close wall
inline float PickSide(float native_side, float room_native, float room_other) {
    if (!std::isfinite(room_native) || !std::isfinite(room_other)) return native_side;
    return room_native < kMinFrameMm && room_other > room_native ? -native_side : native_side;
}
inline float Raise(float room) {
    if (!std::isfinite(room) || room >= kMinFrameMm) return 0.f;
    return kRaiseMaxMm * Clamp((kMinFrameMm - room) / kMinFrameMm, 0.f, 1.f);
}
// The share of look->cam a probe leaves, as room (mm).
template <class Blocked> float Room(Blocked&& blocked, Vec look, Vec cam) {
    float t = 1.f;
    if (bsc::ProbeRay(blocked, look, cam, t) == bsc::Probe::Fault) t = 1.f;
    return Len(Sub(cam, look)) * Clamp(t, 0.f, 1.f);
}

// ---- candidate 7: the framing solver ------------------------------------------------------------------------------
// Donald's tight spots (4-nov, cand6's log: hitCheck pulled the camera in by 230..1196 mm in most duels, cand6's rise
// never started while the pull left more than 1.5 m, and Link filled half the screen from his shoulder). The camera now
// takes a pose from a ladder, from H2c's (a little behind Link) toward the profile across the blade contact, then over
// it, then a high three-quarter view from above (the fallback): the first one that keeps both fighters in frame, Link
// no wider than kMaxLinkShare of the frame (LinkShare: his silhouette's width over the frame's width at his depth, with
// the camera's fovy), never nearer his head than kMinHeadMm, the probe's room (from the look point and from his
// shoulder, as hitCheck's ray) at least kMinRoomShare of the pose's distance. At the opening both sides are tried (the
// native one first); during the duel only the chosen side (a side swap would swing behind Link's back): when hitCheck
// keeps pulling the camera in (Repick), the pose in use is dropped for the rest of the duel and the next one that fits
// is taken, hitCheck's pull counted against it. The pose eases in game time; the hand-back is H2c's.
struct Pose {float behind, up, scale;};   // the angle behind the pure side (rad), the height over the look point, dist x
constexpr Pose kPoses[] = {
    {0.35f, 250.f, 1.00f},    // 0: H2c's (a little behind Link, the blades' height)
    {0.12f, 550.f, 1.05f},    // 1: nearer the profile, a little up
    {-0.10f, 850.f, 1.10f},   // 2: the profile across the contact, a touch on the Salvador's side
    {0.05f, 1350.f, 1.00f},   // 3: over the contact, looking down
    {0.35f, 2600.f, 0.45f},   // 4: the fallback, a high three-quarter view from above (steep: over walls on both sides)
};
constexpr unsigned kPoseCount = sizeof(kPoses) / sizeof(kPoses[0]), kFallbackPose = kPoseCount - 1;
constexpr float kLinkWidthMm = 900.f;     // Link's silhouette (shoulders, shield and sword arm) seen from the side
constexpr float kMaxLinkShare = 1.f / 3.f;
constexpr float kMinHeadMm = 1600.f;      // the camera never nearer his head than this
constexpr float kHeadY = 1550.f, kShoulderY = 1300.f;
constexpr float kMinRoomShare = 0.85f;    // the probe's room over the pose's distance
constexpr float kDefaultFovy = 45.f;      // used when the camera's fovy is unreadable or out of 10..120
constexpr float kAspect = 16.f / 9.f;
constexpr float kRepickShortMm = 300.f;   // hitCheck's pull that counts against the pose in use
constexpr double kRepickS = 0.4;          // ... for this long (game time) before the pose is dropped
constexpr float kFovMaxAdd = 10.f;        // the duel's fovy: at most +10 degrees over the native one
inline View PoseView(Vec link, Vec owner, float side, const Pose& p) {
    View v;
    const float ux0 = owner.x - link.x, uz0 = owner.z - link.z, sep = std::sqrt(ux0 * ux0 + uz0 * uz0);
    if (!(sep >= kMinSepXZ) || !Finite(link) || !Finite(owner) || !(side == 1.f || side == -1.f) || !std::isfinite(p.behind) ||
        !std::isfinite(p.up) || !(p.scale > 0.f)) return v;
    const float ux = ux0 / sep, uz = uz0 / sep, px = side * uz, pz = -side * ux;
    v.sep = sep; v.dist = Clamp(kDistBase + kDistGain * (sep - 1000.f), kDistMin, kDistMax) * p.scale;
    v.look = {link.x + ux0 * 0.5f, link.y + kLookY, link.z + uz0 * 0.5f};
    const float c = std::cos(p.behind), s = std::sin(p.behind);
    v.cam = {v.look.x + (px * c - ux * s) * v.dist, v.look.y + p.up, v.look.z + (pz * c - uz * s) * v.dist};
    v.ok = Finite(v.cam) && Finite(v.look);
    return v;
}
inline Pose Lerp(const Pose& a, const Pose& b, float k) {
    k = Clamp(k, 0.f, 1.f);
    return {a.behind + (b.behind - a.behind) * k, a.up + (b.up - a.up) * k, a.scale + (b.scale - a.scale) * k};
}
inline float SaneFovy(float fovy) {return std::isfinite(fovy) && fovy >= 10.f && fovy <= 120.f ? fovy : kDefaultFovy;}
// The duel's fovy for a native one: x scale, at most +kFovMaxAdd, eased in and out with the weight. An unreadable or
// odd native value is passed through untouched.
inline float DuelFovy(float native, float scale, float weight) {
    if (!std::isfinite(native) || native < 10.f || native > 120.f || !(scale >= 1.f) || !std::isfinite(weight)) return native;
    const float want = native * scale < native + kFovMaxAdd ? native * scale : native + kFovMaxAdd;
    return native + (want - native) * Clamp(weight, 0.f, 1.f);
}
// Link's share of the frame's width from `cam` looking at `look` (his chest's depth along the view).
inline float LinkShare(Vec cam, Vec look, Vec link, float fovy) {
    const Vec f = Sub(look, cam);
    const float lf = Len(f);
    if (!(lf > 1.f)) return 1.f;
    const Vec d = Sub(Vec{link.x, link.y + kShoulderY, link.z}, cam);
    const float depth = (d.x * f.x + d.y * f.y + d.z * f.z) / lf;
    if (!(depth > 1.f)) return 1.f;
    const float half = std::atan(std::tan(SaneFovy(fovy) * 0.5f * 0.017453293f) * kAspect);
    return kLinkWidthMm / (2.f * depth * std::tan(half));
}
struct Choice {
    unsigned pose = 0; float side = 1.f; bool fallback = false, ok = false;
    float room = 0, share = 1, head = 0;   // the pose's room, Link's share, the camera's distance to his head
    View view;
};
// One pose judged: its view pulled in to the probe's room (less hitCheck's last pull when it is the pose in use).
// Candidate 15: `ray` is hitCheck's real ray start (M * campos[1]); without a readable one, Link's shoulder over his feet.
template <class Blocked> Choice Judge(Blocked&& blocked, Vec link, Vec owner, float side, unsigned pose, float fovy, float pull,
                                      const Vec* ray = nullptr) {
    Choice c; c.pose = pose < kPoseCount ? pose : kFallbackPose; c.side = side; c.fallback = c.pose == kFallbackPose;
    c.view = PoseView(link, owner, side, kPoses[c.pose]);
    if (!c.view.ok) return c;
    const Vec shoulder = ray && Finite(*ray) ? *ray : Vec{link.x, link.y + kShoulderY, link.z};
    const float len = Len(Sub(c.view.cam, c.view.look));
    const float a = Room(blocked, c.view.look, c.view.cam);
    const float b = Room(blocked, shoulder, c.view.cam) + Len(Sub(c.view.look, shoulder));
    const float room = Clamp((a < b ? a : b) - (pull > 0.f ? pull : 0.f), 0.f, len);
    const Vec cam = Pull(c.view.look, c.view.cam, len > 0.f ? room / len : 0.f);
    c.room = room; c.share = LinkShare(cam, c.view.look, link, fovy);
    c.head = Len(Sub(cam, Vec{link.x, link.y + kHeadY, link.z}));
    c.ok = room >= kMinRoomShare * len && c.share <= kMaxLinkShare && c.head >= kMinHeadMm;
    return c;
}
// The first pose that fits on `side`, skipping the `rejected` ones (a bit per pose); none: the fallback (ok may be
// false: the best there is). `current` / `pull`: hitCheck's last pull counts against the pose in use only.
template <class Blocked> Choice ChooseOn(Blocked&& blocked, Vec link, Vec owner, float side, float fovy, uint32_t rejected,
                                         unsigned current = kPoseCount, float pull = 0.f, const Vec* ray = nullptr) {
    for (unsigned i = 0; i < kFallbackPose; ++i) {
        if (rejected & (1u << i)) continue;
        const Choice c = Judge(blocked, link, owner, side, i, fovy, i == current ? pull : 0.f, ray);
        if (c.ok) return c;
    }
    return Judge(blocked, link, owner, side, kFallbackPose, fovy, kFallbackPose == current ? pull : 0.f, ray);
}
// Candidate 15 (Donald's 3.6.0 play, 5-nov, duel 3: poses 0 and 1 did not fit on the native side, a wall there, and pose 2
// was taken without looking across): both low poses gone on the native side and a lower one free on the other.
constexpr unsigned kSwingFromPose = 2;
// The opening: the native side unless it has no pose that fits before the fallback, or only a high one against a lower
// one across (the blend in swings there; a tie, or pose 1 on the native side, is not worth the swing).
template <class Blocked> Choice Choose(Blocked&& blocked, Vec link, Vec owner, float native_side, float fovy, const Vec* ray = nullptr) {
    const Choice a = ChooseOn(blocked, link, owner, native_side, fovy, 0, kPoseCount, 0.f, ray);
    if (a.ok && !a.fallback && a.pose < kSwingFromPose) return a;
    const Choice b = ChooseOn(blocked, link, owner, -native_side, fovy, 0, kPoseCount, 0.f, ray);
    if (a.ok && !a.fallback) return b.ok && !b.fallback && b.pose < a.pose ? b : a;
    if (b.ok && !b.fallback) return b;
    if (a.ok) return a;
    return b.ok ? b : a;
}
// During the duel, a pose dropped (Repick): the ladder on this side, then the other side's (one cut at most: the caller
// says if it may still cut), the fallback last. Same duel 3: every pose left on the side in use was pulled to Link's back
// and the other side was never tried. `swapped`: the choice is on the other side, the camera cuts to it.
template <class Blocked> Choice ChooseNext(Blocked&& blocked, Vec link, Vec owner, float side, float fovy, uint32_t rejected,
                                           uint32_t other_rejected, bool may_swap, unsigned current, float pull, const Vec* ray,
                                           bool& swapped) {
    swapped = false;
    const Choice a = ChooseOn(blocked, link, owner, side, fovy, rejected, current, pull, ray);
    if (a.ok && !a.fallback) return a;
    if (may_swap) {
        const Choice b = ChooseOn(blocked, link, owner, -side, fovy, other_rejected, kPoseCount, 0.f, ray);
        if (b.ok && !b.fallback) {swapped = true; return b;}
    }
    return a;
}
// Candidate 8 (Donald's cand7 play, 4-nov 19:16: in 5 of 11 duels the camera sat on Link's back, the Salvador hidden,
// and repicks=0 in all 11). This DLL's own wall probe pulled the camera in (probe_pulls ~1200 a duel) before hitCheck
// saw it, so hitCheck's pull stayed small and Repick never ran; the opening's Judge had said the pose fit, but the
// fighters move during the lock. The framing is now judged on the camera actually used (after the probe and
// hitCheck): Link wider than kMaxLinkShare of the frame, or the camera nearer his head than kMinHeadMm, counts as a pull.
struct Live {float share = 1.f, head = 0.f; bool bad = true;};
inline Live LiveFraming(Vec cam, Vec look, Vec link, float fovy) {
    Live l;
    if (!Finite(cam) || !Finite(look) || !Finite(link)) return l;
    l.share = LinkShare(cam, look, link, fovy);
    l.head = Len(Sub(cam, Vec{link.x, link.y + kHeadY, link.z}));
    l.bad = !(l.share <= kMaxLinkShare) || !(l.head >= kMinHeadMm);
    return l;
}
// Candidate 11 (Donald's cand10 log: live_share=0.00 in all 11 duels, repicks=0 since cand7): the weight's critically
// damped float spring settles at 0.9999997 and never reaches 1.0f, so "weight >= 1" never held. The duel's view counts as
// full from this weight.
constexpr float kFullWeight = 0.999f;
// A re-picked pose eases in over about kPoseEaseTau of game time (cand7 used h x 0.2 with h in seconds: ~5 s).
constexpr double kPoseEaseTau = 0.25;
inline float PoseEase(double h) {h = ValidH(h); return h > 0.0 ? float(1.0 - std::exp(-h / kPoseEaseTau)) : 0.f;}
// During the duel: hitCheck's pull over kRepickShortMm, or a bad live framing, for kRepickS of game time drops the
// pose in use.
class Repick {
public:
    void Reset(unsigned pose) {pose_ = pose; rejected_ = 0; other_ = 0; over_ = 0; picks_ = 0; swapped_ = false;}
    // true: drop the pose in use now (the caller chooses again with rejected()).
    bool Step(float pull, double h, bool bad_frame = false) {
        h = ValidH(h);
        if (pose_ == kFallbackPose) return false;   // the last resort stays
        over_ = pull > kRepickShortMm || bad_frame ? over_ + h : 0.0;
        if (over_ < kRepickS) return false;
        rejected_ |= 1u << pose_; over_ = 0; ++picks_;
        return true;
    }
    void Took(unsigned pose) {pose_ = pose;}
    // Candidate 15: the camera cut to the other side: that side's list, the time starts over.
    void Swap() {const uint32_t r = rejected_; rejected_ = other_; other_ = r; over_ = 0; swapped_ = true;}
    unsigned pose() const {return pose_;}
    uint32_t rejected() const {return rejected_;}
    uint32_t other_rejected() const {return other_;}
    bool swapped() const {return swapped_;}
    unsigned picks() const {return picks_;}
private:
    unsigned pose_ = 0, picks_ = 0; uint32_t rejected_ = 0, other_ = 0; double over_ = 0; bool swapped_ = false;
};

inline int Format(char* out, size_t n, const Summary& s) {
    return std::snprintf(out, n, "id=%u end=%s released=%u ticks=%u release_ticks=%u release_ms=%.0f in_ms=%.0f pushes=%u shakes=%u "
        "max_shake=%.1f max_push=%.0f max_weight=%.3f probe_pulls=%u snaps=%u side=%.0f dist=%.0f", s.id, EndName(s.end),
        s.released ? 1U : 0U, s.ticks, s.release_ticks, double(s.release_ms), double(s.in_ms), s.pushes, s.shakes, double(s.max_shake),
        double(s.max_push), double(s.max_weight), s.probe_pulls, s.snaps, double(s.side), double(s.dist));
}

// ---- the cinema bars' owner ---------------------------------------------------------------------------------------
enum class BarOwner : uint8_t {None, ZTarget, Duel};
inline const char* BarOwnerName(BarOwner o) {return o == BarOwner::Duel ? "duel" : o == BarOwner::ZTarget ? "ztarget" : "none";}
// One owner at a time: the duel while it wants them, else the Z-target lock. The bars are up while anyone owns them,
// so a hand-over (a lock held through the duel, or taken right after) never drops them.
class Bars {
public:
    BarOwner Step(bool duel, bool ztarget) {
        const BarOwner o = duel ? BarOwner::Duel : ztarget ? BarOwner::ZTarget : BarOwner::None;
        if (o != owner_) {from_ = owner_; owner_ = o; ++changes_; changed_ = true;}
        return o;
    }
    bool on() const {return owner_ != BarOwner::None;}
    BarOwner owner() const {return owner_;}
    unsigned changes() const {return changes_;}
    bool TakeChange(BarOwner& from, BarOwner& to) {if (!changed_) return false; changed_ = false; from = from_; to = owner_; return true;}
private:
    BarOwner owner_ = BarOwner::None, from_ = BarOwner::None;
    unsigned changes_ = 0;
    bool changed_ = false;
};
// The duel wants the bars: the key, the world held by the duel and no pause.
inline bool DuelWantsBars(bool key, bool held, bool paused) {return key && held && !paused;}

// ---- the owner's sway ---------------------------------------------------------------------------------------------
// The design: "solo el dueno hace un vaiven leve siguiendo PA x1,3" and, on a hit break, "el Salvador retrocede
// (deslizamiento PA x1,3)". PA runs -50..50; read literally in RE4 units (mm) x1.3 is +-65 mm, too small to read at the
// duel camera's 3 m, and TP's own PA x1.3 in TP units (x12.6 to RE4) is +-820 mm, which would part the blades because
// Link does not move with him here. kSwayGain 2 keeps the x1.3 shape at +-130 mm, the chainsaw still at the blade.
constexpr float kSwayGain = 2.f;
constexpr float kSwayPerPa = 1.3f * kSwayGain;   // mm per PA unit, positive = away from Link (Link winning)
constexpr float kBackMaxMm = 200.f;     // never further back than this from his start
constexpr float kLeanMaxMm = 80.f;      // never more than this toward Link
constexpr float kMinSepMm = 450.f;      // and never closer than this to Link (XZ)
constexpr float kRecoilMm = 90.f;       // a hit break's extra slide back
constexpr double kRecoilInS = 0.08, kRecoilOutS = 0.32;
constexpr float kSwayTau = 0.09f;       // s: the position's spring while the duel runs
constexpr float kSwayReturnTau = 0.04f; // s: back to his start once it is over (5 tau in kSwayHoldMaxS: ~4 % left)
// A duel without a final clip (a draw) is released on the next decide: the probe's release waits for the sway to come
// back (under kSwayHoldMm) at most kSwayHoldMaxS of game time, as it waits up to 250 ms for H2b's win / lose clip, so
// the exact restore at the release is a few mm at most, never a pop of the whole sway.
constexpr float kSwayHoldMm = 2.f;
constexpr double kSwayHoldMaxS = 0.2;
constexpr float kWallMarginMm = 100.f;  // kept in front of a wall behind him (the probe's)
constexpr float kProbeChestMm = 1000.f; // the probe's height over his feet
constexpr float kSettledMm = 0.5f;
struct SwayFacts {
    bool on = false;          // the duel runs (Stage::Duel and running); off: back to the start
    double pa = 0;
    unsigned hits = 0;
};
struct SwaySummary {
    bool begun = false, restored = false, wall = false, foreign = false;
    float back_room = -1, max_back = 0, max_in = 0;
    unsigned recoils = 0, clamped = 0, writes = 0;
};
class Sway {
public:
    // The duel's start: the Salvador's start (feet), Link's, and the clear room behind him (mm, from the probe; < 0:
    // unknown, so he never moves away from Link). False: no axis (they stand on one spot, or a NaN).
    bool Begin(Vec origin, Vec link, float back_room, unsigned hits) {
        *this = Sway{};
        const float dx = origin.x - link.x, dz = origin.z - link.z, sep = std::sqrt(dx * dx + dz * dz);
        if (!Finite(origin) || !Finite(link) || !(sep >= kMinSepXZ)) return false;
        origin_ = origin; ux_ = dx / sep; uz_ = dz / sep; hits_ = hits; on_ = true;
        back_limit_ = back_room < 0.f || !std::isfinite(back_room) ? 0.f : Clamp(back_room, 0.f, kBackMaxMm);
        in_limit_ = Clamp(sep - kMinSepMm, 0.f, kLeanMaxMm);
        s_.begun = true; s_.back_room = back_room; s_.wall = back_room >= 0.f && back_room < kBackMaxMm;
        recoil_t_ = kRecoilInS + kRecoilOutS;
        return true;
    }
    // One step of h seconds of game time (0: no time, the same offset). Returns the offset (mm, + away from Link).
    float Step(const SwayFacts& f, double h) {
        if (!on_) return 0.f;
        h = ValidH(h);
        if (f.on && f.hits > hits_) {++s_.recoils; recoil_t_ = 0;}
        hits_ = f.hits;
        if (h > 0) recoil_t_ += h;
        const float recoil = recoil_t_ < kRecoilInS ? kRecoilMm * Smooth01(float(recoil_t_ / kRecoilInS)) :
            recoil_t_ < kRecoilInS + kRecoilOutS ? kRecoilMm * float(1.0 - (recoil_t_ - kRecoilInS) / kRecoilOutS) : 0.f;
        float target = f.on && std::isfinite(f.pa) ? float(f.pa) * kSwayPerPa + recoil : 0.f;
        const float clipped = Clamp(target, -in_limit_, back_limit_);
        if (clipped != target) ++s_.clamped;
        target = clipped;
        if (h > 0) {spring_.Step(target, float(h), f.on ? kSwayTau : kSwayReturnTau); spring_.x = Clamp(spring_.x, -in_limit_, back_limit_);}
        if (spring_.x > s_.max_back) s_.max_back = spring_.x;
        if (-spring_.x > s_.max_in) s_.max_in = -spring_.x;
        return spring_.x;
    }
    // Where he stands now (Y untouched).
    Vec Position() const {return {origin_.x + ux_ * spring_.x, origin_.y, origin_.z + uz_ * spring_.x};}
    Vec Origin() const {return origin_;}
    Vec Axis() const {return {ux_, 0.f, uz_};}
    float offset() const {return spring_.x;}
    bool Active() const {return on_;}
    bool Settled() const {return std::fabs(spring_.x) < kSettledMm;}
    // The release waits: still on its way back and the duel ended less than kSwayHoldMaxS ago (game time).
    bool HoldsRelease(double since_end_s) const {return on_ && std::fabs(spring_.x) > kSwayHoldMm && since_end_s < kSwayHoldMaxS;}
    // The exit: he goes back to his exact start (the caller writes Origin()); nothing more moves him.
    void Restored() {if (on_) {on_ = false; s_.restored = true; spring_.Reset(0.f);}}
    // Someone else moved him (the position read is not the one we wrote): we stop and leave him there.
    void Foreign() {if (on_) {on_ = false; s_.foreign = true;}}
    void Wrote() {++s_.writes;}
    const SwaySummary& summary() const {return s_;}
    float back_limit() const {return back_limit_;}
    float in_limit() const {return in_limit_;}
private:
    Vec origin_{};
    float ux_ = 0, uz_ = 0, back_limit_ = 0, in_limit_ = 0;
    bsc::Spring spring_;
    double recoil_t_ = 1;
    unsigned hits_ = 0;
    bool on_ = false;
    SwaySummary s_;
};
// The probe behind him: from his chest along the axis away from Link, kBackMaxMm + kWallMarginMm; the clear room
// (mm, the margin taken off), or -1 when the native test faulted (unknown: no move away).
template <class Blocked> float BackRoom(Blocked&& blocked, Vec origin, Vec axis) {
    const Vec from{origin.x, origin.y + kProbeChestMm, origin.z};
    const float len = kBackMaxMm + kWallMarginMm;
    const Vec to{from.x + axis.x * len, from.y, from.z + axis.z * len};
    float t = 1.f;
    const bsc::Probe p = bsc::ProbeRay(blocked, from, to, t);
    if (p == bsc::Probe::Fault) return -1.f;
    if (p == bsc::Probe::Clear) return kBackMaxMm;
    // ProbeRay already kept bsc::kProbeMargin (100) short of the wall: the room is that share of the ray.
    return Clamp(t * len, 0.f, kBackMaxMm);
}
inline int FormatSway(char* out, size_t n, const SwaySummary& s) {
    return std::snprintf(out, n, "begun=%u restored=%u foreign=%u wall=%u back_room=%.0f max_back=%.1f max_in=%.1f recoils=%u clamped=%u writes=%u",
        s.begun ? 1U : 0U, s.restored ? 1U : 0U, s.foreign ? 1U : 0U, s.wall ? 1U : 0U, double(s.back_room), double(s.max_back),
        double(s.max_in), s.recoils, s.clamped, s.writes);
}
}  // namespace link_runtime::sword_lock::cinema

#pragma once
// Spin bullets (Donald 22-oct: «que el giro dispare balas, para todos lados»): at the strike of the spin (8) and the
// Great Spin (12), free shots spread evenly around Link from his facing. Pure logic: ray assignment, the volley's
// tracers as trail quads, the INI keys; the glue (spin_attack_runtime.h) reads the Ganado pool, tests the walls with
// the EatMgr nearest hit (ztarget_los.h), adds the struck Ganados to the spin's own hit list (same damage, its own
// reach at revalidation), draws the tracers with the trail's table-17 node and plays the equipped weapon's shot once
// (SndCall(2, 0), the weapon bank: re4-decomp objRuger/objMachinegun). No ammo is used.
//
// INI [LinkRuntime] (read with SpinAttack=1):
//   SpinBullets           missing or 1 = on, 0 = off
//   SpinBulletsCount      shots of the spin, 1..32 (default 8)
//   GreatSpinBulletsCount shots of the Great Spin, 1..32 (default 12)
//   SpinBulletsRange      mm, 2000..30000 (default 12000)
#include "sword_trail.h"
#include "native_pins.h"
#include <cmath>
#include <cstdint>
#include <cwchar>

namespace link_runtime::spin_bullets {
namespace core = ::sword_trail;
constexpr unsigned kMaxShots = 32;
constexpr double kPi = 3.14159265358979323846;
constexpr float kMuzzleY = 1100.f;      // shots leave at Link's chest
constexpr float kChestY = 1100.f;       // and aim at a Ganado's chest
constexpr float kHalfWidth = 600.f;     // a Ganado within 60 cm of the line is in the shot's path
constexpr float kVertical = 1500.f;     // feet-to-feet band for a target (the spin's own is 400)
// The shots travel (Donald 22-oct: «que se vean las balas para ver su trayectoria»): a bright head kHeadMm long runs
// from the muzzle to where the shot stops at kSpeed, leaving a faint path behind it that fades over kFadeMs once the
// head has arrived. A 12 m shot takes 300 ms.
constexpr double kSpeed = 40.0;         // mm per game ms (40 m/s): slow enough to follow
constexpr double kHeadMm = 1200.0;      // the bullet's bright streak
constexpr double kFadeMs = 150.0;       // the path's fade after the head arrives
constexpr double kTracerWidth = 40.0;   // mm, half width of each of the two crossed planes (head)
constexpr double kPathWidth = 15.0;     // mm, the path behind it
constexpr uint8_t kTracerAlpha = 240, kPathAlpha = 90;
constexpr unsigned kShotSounds = 3;     // the equipped weapon's shot, three times...
constexpr double kShotGapMs = 60.0;     // ...60 game ms apart
struct Config {
    bool on = true;
    unsigned spin = 8, great = 12;
    float range = 12000.f;
    unsigned problems = 0;
    unsigned Shots(bool great_spin) const {return great_spin ? great : spin;}
};
// get(key, out, count) fills `out` with the raw value ("" when missing). Invalid values keep the default (counted).
template <class Get> Config ReadConfig(Get&& get) {
    Config c; wchar_t v[32]{};
    auto number = [&](const wchar_t* key, unsigned fallback, unsigned lo, unsigned hi) {
        v[0] = 0; get(key, v, 32);
        if (!v[0]) return fallback;
        unsigned n = 0;
        for (const wchar_t* p = v; *p; ++p) {
            if (*p < L'0' || *p > L'9') {++c.problems; return fallback;}
            n = n * 10 + unsigned(*p - L'0'); if (n > 1000000) {++c.problems; return fallback;}
        }
        if (n < lo || n > hi) {++c.problems; return fallback;}
        return n;
    };
    v[0] = 0; get(L"SpinBullets", v, 32);
    if (v[0]) {if ((v[0] == L'0' || v[0] == L'1') && !v[1]) c.on = v[0] == L'1'; else ++c.problems;}
    c.spin = number(L"SpinBulletsCount", 8, 1, kMaxShots);
    c.great = number(L"GreatSpinBulletsCount", 12, 1, kMaxShots);
    c.range = float(number(L"SpinBulletsRange", 12000, 2000, 30000));
    return c;
}
// The direction of shot i of n: the native yaw convention (sin, cos), from Link's facing, evenly around.
inline void Direction(float yaw, unsigned i, unsigned n, float& dx, float& dz) {
    const double a = double(yaw) + 2.0 * kPi * i / (n ? n : 1);
    dx = float(std::sin(a)); dz = float(std::cos(a));
}
struct Candidate {float x = 0, y = 0, z = 0; unsigned tag = 0;};   // feet position; tag = the caller's index
// For each of n shots from `origin` (Link's feet), the nearest candidate in its path: along the ray (0, range], within
// kHalfWidth of the line, within kVertical of Link's feet. target[i] = candidate index or -1; along[i] its distance.
inline void Assign(const float* origin, float yaw, unsigned n, float range, const Candidate* c, unsigned count, int* target, float* along) {
    if (n > kMaxShots) n = kMaxShots;
    for (unsigned i = 0; i < n; ++i) {target[i] = -1; along[i] = 0;}
    if (!origin || !std::isfinite(yaw) || !std::isfinite(range)) return;
    for (unsigned i = 0; i < n; ++i) {
        float dx, dz; Direction(yaw, i, n, dx, dz);
        for (unsigned k = 0; k < count; ++k) {
            const float ox = c[k].x - origin[0], oz = c[k].z - origin[2], oy = c[k].y - origin[1];
            const float a = ox * dx + oz * dz, side = std::fabs(ox * dz - oz * dx);
            if (!std::isfinite(a) || !std::isfinite(side) || !(a > 0) || a > range || side > kHalfWidth || std::fabs(oy) > kVertical) continue;
            if (target[i] < 0 || a < along[i]) {target[i] = int(k); along[i] = a;}
        }
    }
}
// One volley: each shot travels from the muzzle to where it stopped (a Ganado, a wall or its range). Drawn as two
// crossed thin planes (so it reads from any camera angle): the path behind the head (faint) and the head (bright).
struct Ray {core::Vec3 from{}, to{};};
inline double Length(const Ray& r) {
    const double dx = r.to.x - r.from.x, dy = r.to.y - r.from.y, dz = r.to.z - r.from.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
// A streak from a to b as two crossed planes (horizontal across the shot, then vertical), alpha a0 at a and a1 at b.
inline int Streak(const core::Vec3& p, const core::Vec3& q, double w, uint8_t a0, uint8_t a1, core::Quad* out, int at, int max) {
    const double dx = q.x - p.x, dz = q.z - p.z, len = std::sqrt(dx * dx + dz * dz);
    if (at + 2 > max || !(len > 1.0)) return at;
    const double sx = -dz / len * w, sz = dx / len * w;
    out[at++] = core::Quad{{{p.x - sx, p.y, p.z - sz}, {p.x + sx, p.y, p.z + sz}, {q.x + sx, q.y, q.z + sz}, {q.x - sx, q.y, q.z - sz}}, a0, a1};
    out[at++] = core::Quad{{{p.x, p.y - w, p.z}, {p.x, p.y + w, p.z}, {q.x, q.y + w, q.z}, {q.x, q.y - w, q.z}}, a0, a1};
    return at;
}
inline core::Vec3 Along(const Ray& r, double d) {
    const double len = Length(r), k = len > 0 ? (d < 0 ? 0 : d > len ? 1 : d / len) : 0;
    return {r.from.x + (r.to.x - r.from.x) * k, r.from.y + (r.to.y - r.from.y) * k, r.from.z + (r.to.z - r.from.z) * k};
}
class Volley {
    Ray rays_[kMaxShots]{}; unsigned n_ = 0; double t0_ = 0, life_ = 0; uint32_t room_ = 0, player_ = 0;
public:
    void Reset() {n_ = 0;}
    void Fire(double now, const Ray* r, unsigned n, uint32_t room, uint32_t player) {
        n_ = 0; t0_ = now; room_ = room; player_ = player; life_ = 0;
        for (unsigned i = 0; i < n && i < kMaxShots; ++i) {
            if (!core::Finite(r[i].from) || !core::Finite(r[i].to) || !(Length(r[i]) > 1.0)) continue;
            rays_[n_++] = r[i];
            const double l = Length(r[i]) / kSpeed + kFadeMs;
            if (l > life_) life_ = l;
        }
    }
    bool Live(double now) const {return n_ && now >= t0_ && now - t0_ < life_;}
    unsigned Count() const {return n_;}
    double Life() const {return life_;}
    // Appends up to 4 quads per shot (path, head) into out[at..max); returns the new count. A room or player change
    // ends the volley.
    int Frame(double now, uint32_t room, uint32_t player, core::Quad* out, int at, int max) {
        if (room != room_ || player != player_) n_ = 0;
        if (!Live(now) || !out) return at;
        const double age = now - t0_;
        for (unsigned i = 0; i < n_; ++i) {
            const Ray& r = rays_[i];
            const double len = Length(r), run = age * kSpeed;
            const double arrive = len / kSpeed, fade = age <= arrive ? 1.0 : 1.0 - (age - arrive) / kFadeMs;
            if (!(fade > 0)) continue;
            const double head = run < len ? run : len, tail = head - kHeadMm > 0 ? head - kHeadMm : 0;
            const core::Vec3 h = Along(r, head), t = Along(r, tail);
            at = Streak(r.from, t, kPathWidth, 0, uint8_t(kPathAlpha * fade), out, at, max);   // the path, fading toward the muzzle
            at = Streak(t, h, kTracerWidth, uint8_t(kPathAlpha * fade), uint8_t(kTracerAlpha * fade), out, at, max);   // the bullet
        }
        return at;
    }
};
// SndCall (cdecl u32 SndCall(u16 blk, u16 no, Vec* pos, int id, int vol, cUnit* obj)), called at its entry: live
// 22-oct its thunk 0xADC1 is redirected by another mod (re4_tweaks), so only the function itself is pinned.
inline constexpr uint32_t kSndCall = 0x56E230;
inline constexpr native_pins::Pin kSoundPins[] = {
    {kSndCall, "558bec83ec18538b5d08560fb7f38d46fe57c645fa00c645"}};
}  // namespace link_runtime::spin_bullets

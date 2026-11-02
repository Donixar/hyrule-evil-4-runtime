#pragma once
// Zelda's light ray (Donald 23-oct: «poderosa, diosa hermosa»; spec docs/superpowers/specs/2026-10-23-zelda-light-ray-design.md).
// In the playable Ashley segment (pl_type 1, Ashley reskinned as Zelda) the knife button, which Ashley's own controls
// ignore (re4-decomp pl_sub.cpp:855-869), fires a golden ray from Zelda's hand at the nearest enemy in front. Pure logic
// here: the INI keys, the target, one ray per press, the beam's quads (drawn with the trail's table-17 node) and the
// damage. The glue is zelda_light_runtime.h.
//
// Damage. Ganados go through the damage adapter like a spin bullet. The armor knights (cEm3c, vtable RVA 0x712AE4,
// found by RTTI .?AVcEm3c@@ in the pinned image) take the knife's cDmgInfo record written directly: em3cDmCk
// (re4-decomp em3c.cpp:98-339) dereferences the record's hit part, so it must be a real YARARE_INFO (the knight's own
// inline one at actor+0x344, UHD; GC cEm::hitInfo at 0x33C), the timer at least 2 (10, as the knife), weapon 0x10 (0x14 and
// 0x16 are ignored). A knight with Be_flg 0x400 (lying down, or waiting to ambush) is protected natively every frame and
// is left alone (Donald 23-oct: the ambush knights stay native). Before the native hit, Drain lowers the hp so the ray
// does its own amount; a lethal ray leaves 0 hp and the knight's own damage check runs the native death.
//
// INI [ZeldaLight]:
//   Enabled       missing or 1 = on, 0 = off
//   Probe         exact 1 = log the probe lines (zelda_probe ...)
//   Range         mm, 2000..30000 (default 15000)
//   CooldownMs    100..3000 (default 400)
//   GanadoDamage  1..5000 (default 1300; the game scales it by difficulty: live 23-oct 600 took 283 hp)
//   KnightDamage  1..1000 (default 350)
//   ZeldaCastClip exact 1 = the cast plays the TP clip (zelda_cast_clip.h) and eases the turn; missing or 0 = the 23-oct
//                 procedural pose and snap
//   ZeldaCompanionRay, ZeldaCompanionCooldownMin/Max: the partner's own rays (zelda_companion.h)
#include "spin_bullets.h"
#include "zelda_cast_clip.h"
#include "sword_trail.h"
#include <cmath>
#include <cstdint>
#include <cwchar>

namespace link_runtime::zelda_light {
namespace core = ::sword_trail;
constexpr uint32_t kKeyHeld = 0x862F08, kKeyPressed = 0x862F10, kKnifeBit = 0x800;   // RVAs; Key bit 0x800 = knife
constexpr uint32_t kKnightVtable = 0x712AE4;   // RVA, cEm3c (armor knight)
constexpr uint8_t kKnife = 0x10, kKnifeTimer = 10;
constexpr int kKnightNativeHit = 0;            // live 23-oct (r20e): the knife record takes 0 hp off a knight; a lethal
                                                // ray leaves 0 hp and em3cDmCk runs the native death (hp <= 0)
constexpr float kConeCos = 0.866f;             // +-30 degrees of Zelda's facing
constexpr float kVertical = 1500.f;            // feet-to-feet band
constexpr float kKnightMin = 900.f;            // a knight nearer than this may be grabbing Zelda: left to its script
constexpr double kSpeed = 120.0;               // mm per game ms: a 15 m ray lands in 125 ms
constexpr double kHeadMm = 2500.0;             // the bright head
constexpr double kFadeMs = 400.0;   // the beam lingers (Donald: a goddess, not a bullet)
constexpr double kCoreWidth = 110.0, kGlowWidth = 280.0;
constexpr uint8_t kCoreAlpha = 255, kGlowAlpha = 110;
constexpr uint32_t kGold = 0xF2C94C;
namespace layout {
constexpr uint32_t be_flag = 0x004, pos = 0x094, routine = 0x0FC, id = 0x100, hp = 0x324;   // routine: r_no_0
constexpr uint32_t dmg = 0x328, dmg_flag = 0x32C, dmg_timer = 0x32D, dmg_weapon = 0x32E, dmg_from = 0x330;
constexpr uint32_t dmg_distance = 0x33C, dmg_part = 0x340, hit_info = 0x344;
constexpr uint32_t work_flags = 0x408;         // the knight's Be_flg (UHD; StartWait 0x164D60 ORs 0x400 here, live 23-oct)
constexpr uint32_t wake = 0x3D0;               // cEm::flag (UHD): bit 0 wakes a dormant knight (StartWait tests it)
constexpr uint32_t protect = 0x400;            // Be_flg: em3cDmCk returns before LifeDownSet
}  // namespace layout

struct Config {
    bool on = true, probe = false, cast_clip = false;
    float range = 15000.f;
    unsigned cooldown_ms = 400, ganado = 1300, knight = 350, problems = 0;
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
    v[0] = 0; get(L"Enabled", v, 32);
    if (v[0]) {if ((v[0] == L'0' || v[0] == L'1') && !v[1]) c.on = v[0] == L'1'; else ++c.problems;}
    v[0] = 0; get(L"Probe", v, 32);
    c.probe = v[0] == L'1' && !v[1];
    v[0] = 0; get(L"ZeldaCastClip", v, 32);
    c.cast_clip = v[0] == L'1' && !v[1];
    c.range = float(number(L"Range", 15000, 2000, 30000));
    c.cooldown_ms = number(L"CooldownMs", 400, 100, 3000);
    c.ganado = number(L"GanadoDamage", 1300, 1, 5000);
    c.knight = number(L"KnightDamage", 350, 1, 1000);
    return c;
}

struct Candidate {float x = 0, y = 0, z = 0; unsigned tag = 0; bool knight = false;};   // feet position
// The nearest candidate in front of `origin` (Zelda's feet) facing `yaw` (native: direction (sin, cos)): ahead, within
// range, within the +-30 degree cone and the height band. -1 when none.
inline int Pick(const float* origin, float yaw, float range, const Candidate* c, unsigned count) {
    if (!origin || !c || !std::isfinite(yaw) || !std::isfinite(range)) return -1;
    const float fx = std::sin(yaw), fz = std::cos(yaw);
    int best = -1; float best_d = 0;
    for (unsigned k = 0; k < count; ++k) {
        const float ox = c[k].x - origin[0], oz = c[k].z - origin[2], oy = c[k].y - origin[1];
        const float d = std::sqrt(ox * ox + oz * oz), along = ox * fx + oz * fz;
        if (!std::isfinite(d) || !std::isfinite(oy) || !(d > 0) || !(along > 0) || d > range || std::fabs(oy) > kVertical) continue;
        if (along / d < kConeCos || (c[k].knight && d < kKnightMin)) continue;
        if (best < 0 || d < best_d) {best = int(k); best_d = d;}
    }
    return best;
}

// Zelda's facing: her model's angle (cModel +0xA4), the same convention as Link's (direction (sin, cos)). Live 23-oct
// (scripts/live/zelda_heading.py): walking freely, atan2 of her steps equals the angle (diff 0.00 at two facings).
inline float Facing(float rot_y) {return rot_y;}

// The native yaw (direction (sin, cos)) from `from` (feet) toward x, z: the cast turns her to the target.
inline float YawTo(const float* from, float x, float z) {return float(std::atan2(double(x) - from[0], double(z) - from[2]));}

// Where the ray leaves: Zelda's right hand, beside her body (live 23-oct: from the chest, the follow camera behind her saw
// the ray only through her head and hair). Facing +Z her right is -X (live 23-oct: a +X hand sent the beam from her left).
constexpr float kHandSide = 380.f, kHandAhead = 250.f, kHandHeight = 1050.f;
inline core::Vec3 Hand(const float* feet, float yaw) {
    const double fx = std::sin(yaw), fz = std::cos(yaw), rx = -fz, rz = fx;
    return {feet[0] + fx * kHandAhead + rx * kHandSide, feet[1] + kHandHeight, feet[2] + fz * kHandAhead + rz * kHandSide};
}

// One ray per press of the button: fires on the rising edge only, once the cooldown since the last ray has passed or game
// time ran back (a save load: the cooldown then counts from the new time, as back_slice.h and shield_bash.h).
class Press {
    bool down_ = false, fired_ = false; double last_ = 0;
public:
    bool Fire(bool pressed, double now, unsigned cooldown_ms) {
        const bool edge = pressed && !down_; down_ = pressed;
        if (!edge || (fired_ && now >= last_ && now - last_ < cooldown_ms)) return false;
        fired_ = true; last_ = now; return true;
    }
};

// The press edge reads the raw key every frame; the in-play gates only decide whether that press fires. A one-frame blink
// of the gates (focus, a status bit) never re-arms a held button, and a press held through a closing menu never fires.
inline bool Trigger(Press& p, bool raw, bool allowed, double now, unsigned cooldown_ms) {
    return p.Fire(raw, now, cooldown_ms) && allowed;
}

// The cast (Donald's goal 23-oct, docs/superpowers/specs/2026-10-23-zelda-cast-design.md): the press starts it, her hand
// glows for kChargeMs, the ray fires once, the glow fades by kGlowEndMs, and a new cast can start after kCastMs (the arm
// pose of zelda_cast_pose.h runs over the same 600 ms).
constexpr double kChargeMs = 150.0, kGlowEndMs = 450.0, kCastMs = 600.0;
// clip (zelda-companion-01): the TP clip's cast lasts zelda_cast_clip::kClipMs and fires when the caller says the clip
// reached its release (FireDue's clip_due), the star peaking there instead of at kChargeMs.
class Cast {
    double t0_ = 0, length_ = kCastMs; bool on_ = false, fired_ = false, clip_ = false;
public:
    void Begin(double now, bool clip = false) {
        t0_ = now; on_ = true; fired_ = false; clip_ = clip; length_ = clip ? zelda_cast_clip::kClipMs : kCastMs;
    }
    bool Busy(double now) const {return on_ && now >= t0_ && now - t0_ < length_;}
    bool Clip() const {return clip_;}
    double Age(double now) const {return now - t0_;}
    // The hand star's age: the clip's star peaks at its release.
    double GlowAge(double now) const {return clip_ ? now - t0_ - (zelda_cast_clip::kReleaseMs - kChargeMs) : now - t0_;}
    // True on the first frame at or past kChargeMs (once per cast); a clip cast waits for clip_due.
    bool FireDue(double now, bool clip_due = false) {
        if (!on_ || fired_ || (clip_ ? !clip_due : now - t0_ < kChargeMs)) return false;
        fired_ = true; return true;
    }
};
// The hand's star (sword_charge::draw's glint, additive) at a cast's age: it grows and brightens to full at the shot, then
// fades out by kGlowEndMs.
struct GlowLook {uint8_t star = 0; double scale = 0;};
inline GlowLook Glow(double age) {
    GlowLook g;
    if (!(age > 0) || age >= kGlowEndMs) return g;
    const double a = age < kChargeMs ? age / kChargeMs : 1.0 - (age - kChargeMs) / (kGlowEndMs - kChargeMs);
    g.star = uint8_t(std::lround(255.0 * a));
    g.scale = age < kChargeMs ? 0.6 + 0.6 * a : 1.2 + 0.6 * (1.0 - a);   // grows while charging, keeps blooming as it fades
    return g;
}
// Golden sparks along the ray: one every kSparkStep from the hand to the impact (the last at the impact), at most kMaxSparks.
constexpr double kSparkStep = 1200.0;
constexpr unsigned kMaxSparks = 8;
inline unsigned SparkPoints(const core::Vec3& from, const core::Vec3& to, core::Vec3* out) {
    const double dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z, len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!out || !std::isfinite(len)) return 0;
    unsigned n = unsigned(len / kSparkStep);
    if (n * kSparkStep < len) ++n;   // the impact
    if (n == 0) n = 1;
    if (n > kMaxSparks) n = kMaxSparks;
    for (unsigned i = 0; i < n; ++i) {
        const double k = i + 1 == n ? 1.0 : (i + 1) * kSparkStep / len;
        out[i] = {from.x + dx * k, from.y + dy * k, from.z + dz * k};
    }
    return n;
}

// The ray: a wide soft glow around a bright core, its head running from the hand to where it stops at kSpeed, the
// whole beam fading over kFadeMs after it lands. Two crossed planes each (spin_bullets::Streak).
class Beam {
    spin_bullets::Ray ray_{}; double t0_ = 0; bool on_ = false; uint32_t room_ = 0, player_ = 0;
public:
    void Fire(double now, core::Vec3 from, core::Vec3 to, uint32_t room, uint32_t player) {
        ray_ = {from, to}; t0_ = now; room_ = room; player_ = player;
        on_ = core::Finite(from) && core::Finite(to) && spin_bullets::Length(ray_) > 1.0;
    }
    bool Live(double now) const {
        return on_ && now >= t0_ && now - t0_ < spin_bullets::Length(ray_) / kSpeed + kFadeMs;
    }
    int Frame(double now, uint32_t room, uint32_t player, core::Quad* out, int at, int max) {
        if (room != room_ || player != player_) on_ = false;
        if (!Live(now) || !out) return at;
        const double len = spin_bullets::Length(ray_), age = now - t0_, arrive = len / kSpeed;
        const double fade = age <= arrive ? 1.0 : 1.0 - (age - arrive) / kFadeMs;
        if (!(fade > 0)) return at;
        const double head = age * kSpeed < len ? age * kSpeed : len, tail = head - kHeadMm > 0 ? head - kHeadMm : 0;
        const core::Vec3 h = spin_bullets::Along(ray_, head), t = spin_bullets::Along(ray_, tail);
        const uint8_t glow = uint8_t(kGlowAlpha * fade), core_a = uint8_t(kCoreAlpha * fade);
        at = spin_bullets::Streak(ray_.from, h, kGlowWidth, glow, glow, out, at, max);              // the glow, hand to head
        at = spin_bullets::Streak(ray_.from, t, kCoreWidth, uint8_t(core_a / 2), core_a, out, at, max);   // the core behind
        at = spin_bullets::Streak(t, h, kCoreWidth, core_a, core_a, out, at, max);                  // the bright head
        return at;
    }
};

// The hp to leave before the native hit, so that the ray takes `amount` in all: hp - amount, never below what the native
// hit itself removes (`keep`), so that hit is the lethal one; untouched when already at or below `keep`.
inline int Drain(int hp, int amount, int keep) {
    if (hp <= keep) return hp;
    return hp - amount > keep ? hp - amount : keep;
}

// r_no_1 of the knight's move routine (re4-decomp em3c.cpp Em3c_R1_move_tbl): 0 StartWait (lying or standing dormant until
// the room's flag; live 23-oct it took the hp but never died), 1 AtkWait (the ambush, left native: Donald 23-oct), from 2 on
// awake (Wait, Walk, Run, attacks).
constexpr uint8_t kKnightAwake = 2;
// The knife's damage record on an armor knight, flag byte last. Refused (nothing written): part 0, hp <= 0, Be_flg 0x400
// (natively protected), a record already pending, a non-finite `from`, and any routine but move (r_no_0 1): em3cDmCk
// does not run during the start, the attack waits or a damage routine, and the record would expire unread (review 23-oct).
template <class Mem> bool StrikeKnight(Mem& m, uint32_t actor, const float* from, uint32_t part) {
    uint8_t pending = 1, routine[2] = {}; int16_t hp = 0; uint32_t be = 0;
    if (!actor || !part || !from || !m.Read(actor + layout::dmg_flag, &pending, 1) || pending || !m.Read(actor + layout::hp, &hp, 2) ||
        hp <= 0 || !m.Read(actor + layout::work_flags, &be, 4) || (be & layout::protect) || !m.Read(actor + layout::routine, routine, 2) ||
        routine[0] != 1 || routine[1] < kKnightAwake)
        return false;
    for (unsigned i = 0; i < 3; ++i) if (!std::isfinite(from[i])) return false;
    const float zero = 0; const uint8_t timer = kKnifeTimer, weapon = kKnife, flag = 1;
    return m.Write(actor + layout::dmg, &zero, 4) && m.Write(actor + layout::dmg_from, from, 12) &&
           m.Write(actor + layout::dmg_distance, &zero, 4) && m.Write(actor + layout::dmg_part, &part, 4) &&
           m.Write(actor + layout::dmg_timer, &timer, 1) && m.Write(actor + layout::dmg_weapon, &weapon, 1) &&
           m.Write(actor + layout::dmg_flag, &flag, 1);
}
}  // namespace link_runtime::zelda_light

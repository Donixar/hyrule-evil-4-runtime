#pragma once
// Zelda as the AI partner casts her light ray (zelda-companion-01, Donald 30-oct: «que Ashley no sea 100% useless, que
// pueda usar su rayo de luz ... ideal que no lo spamee, que sea en algunas situaciones»). While Link is the player
// (pl_type 0) and Zelda follows him (cSubChar, partner slot 0x857060), she throws one ray in three situations only:
//   1. Threat: a Ganado in an attack or grab routine (mortal_draw_target.h AttackRoutine) within ThreatMm of her or of
//      Link;
//   2. Carried: she is being carried off (pG+0x5024 bit 0x20000000, STA_SUB_CATCHED): the ray goes to the carrier, the
//      Ganado in a grab routine nearest to her (the native code drops her when the carrier is hurt, em10.cpp:13296);
//   3. Danger: Link's life (pG+0x4FB4) at or below DangerHp with a Ganado within DangerMm of him.
// Only from her normal routine (r_no_0 0, r_no_1 footwork 0, move 1, behind 2/5, crouch 4); never hiding (0x10),
// on a ladder, falling, at a fence or window, in an action, an event or a damage routine. The one exception is being
// carried: the carrier puts her in the custom routine (r_no_0 4, em10.cpp SetSubDamage -> cSubChar::setEmFunc), which
// is allowed while the caught bit is set and only for the Carried situation. At most one ray per situation
// trigger (the same situation on the same Ganado never fires twice in a row), and a random cooldown of
// CompanionCooldownMin..Max seconds (game time) between rays. Pure logic; the glue is zelda_companion_runtime.h.
//
// cSubChar layout (UHD, static-verified on the pinned image bb59e550..., docs/zelda-companero-rayo-2026-10-30.md):
//   r_no_0 +0xFC  cSubChar::move 0x361A70: `mov al,[esi+0xFC]; cmp al,4` (the m_pFunc routine) then NpcFuncTbl[al]
//   r_no_1 +0xFD  cSubChar::moveCore 0x3619D0: `movzx eax,byte [esi+0xFD]` indexes funcTbl 0x81BAD8
//   hp     +0x324 cSubChar::move copies pG+0x4FB8 (ashley_life) there each frame
//   caught pG+0x5024 bit 0x20000000, cleared at the top of cSubChar::move (0x361AB9)
//   vtable 0x727FE4 (RTTI .?AVcSubChar@@); slot 4 -> thunk 0xAA60 -> cSubChar::move 0x361A70
// The id byte +0x100 == 3 (Ashley) is from the brief; the routine allow-list is the GC decomp's (pl_npc.cpp:203-231).
#include <cmath>
#include <cstdint>
#include <cwchar>

namespace link_runtime::zelda_companion {
constexpr uint32_t kPartnerSlot = 0x857060, kSubCharVtable = 0x727FE4;
constexpr uint8_t kAshleyId = 3;
namespace layout {
constexpr uint32_t pos = 0x94, rot_y = 0xA4, r0 = 0xFC, r1 = 0xFD, id = 0x100, hp = 0x324;
constexpr uint32_t status2 = 0x5024, pl_life = 0x4FB4, character = 0x4FC8;   // in pG
constexpr uint32_t caught = 0x20000000;
}

struct Config {
    bool on = false;                     // ZeldaCompanionRay=1
    unsigned cooldown_min_s = 8, cooldown_max_s = 15;
    float threat_mm = 3000.f, danger_mm = 5000.f, carrier_mm = 2500.f, range_mm = 12000.f, vertical = 1500.f;
    int danger_hp = 400;
};

// Her routines that may cast (r_no_0 0 = moveCore; r_no_1 per pl_npc.cpp funcTbl).
inline bool StateAllowed(unsigned r0, unsigned r1, bool carried = false) {
    if (carried) return r0 == 4;
    return r0 == 0 && (r1 == 0 || r1 == 1 || r1 == 2 || r1 == 4 || r1 == 5);
}
// Values a live cSubChar can hold (NpcFuncTbl has 7 routines, funcTbl 0x15): anything else means a wrong offset or a
// wrong object and disables the feature.
inline bool Plausible(unsigned r0, unsigned r1) { return r0 <= 6 && r1 <= 0x14; }

// mortal_draw_target.h AttackRoutine (em10 attack / grab R1), repeated here so this header stays standalone.
inline bool AttackRoutine(uint8_t r1) {
    return r1 == 0x0E || r1 == 0x0F || (r1 >= 0x20 && r1 <= 0x3A) || (r1 >= 0x3D && r1 <= 0x3F) ||
           r1 == 0x59 || r1 == 0x65 || r1 == 0x66 || r1 == 0x6D;
}
// The grab family (Catch, NeckHang*, Backhold, Bombhold, DashCatch, TakeAway; UFOCatch, R300TakeAshley).
inline bool GrabRoutine(uint8_t r1) { return (r1 >= 0x33 && r1 <= 0x3A) || r1 == 0x65 || r1 == 0x66; }

enum class Situation : uint8_t { None, Threat, Carried, Danger };
inline const char* SituationName(Situation s) {
    switch (s) {
    case Situation::Threat: return "threat"; case Situation::Carried: return "carried"; case Situation::Danger: return "danger";
    default: return "none";
    }
}
struct Enemy { float x = 0, y = 0, z = 0; uint8_t r0 = 0, r1 = 0; uint32_t tag = 0; };   // tag: the caller's index
struct World {
    float zelda[3]{}, link[3]{};
    bool caught = false;      // debounced caught bit
    int link_hp = 0;
    const Enemy* enemies = nullptr; unsigned count = 0;
};
struct Pick { Situation situation = Situation::None; int index = -1; };

inline bool Finite3(const float* p) { return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]); }
inline float Flat(const float* a, const Enemy& e) {
    const float dx = e.x - a[0], dz = e.z - a[2];
    return std::sqrt(dx * dx + dz * dz);
}
inline bool Band(const float* a, const Enemy& e, float vertical) { return std::fabs(e.y - a[1]) <= vertical; }

// The situation now and its target: Carried first (her own rescue), then a Threat (an attacking Ganado nearest to her or
// to Link), then Danger (the Ganado nearest to Link). Enemies beyond range_mm of her are ignored.
inline Pick Assess(const World& w, const Config& c) {
    Pick p;
    if (!w.enemies || !Finite3(w.zelda) || !Finite3(w.link)) return p;
    float best = 0;
    auto take = [&](Situation s, unsigned i, float d) {
        if (p.situation == Situation::None || d < best) { p = {s, int(i)}; best = d; }
    };
    if (w.caught) {
        for (unsigned i = 0; i < w.count; ++i) {
            const Enemy& e = w.enemies[i];
            const float d = Flat(w.zelda, e);
            if (e.r0 == 1 && GrabRoutine(e.r1) && d <= c.carrier_mm && Band(w.zelda, e, c.vertical)) take(Situation::Carried, i, d);
        }
        if (p.situation != Situation::None) return p;
    }
    for (unsigned i = 0; i < w.count; ++i) {
        const Enemy& e = w.enemies[i];
        if (e.r0 != 1 || !AttackRoutine(e.r1)) continue;
        const float dz = Flat(w.zelda, e), dl = Flat(w.link, e), d = dz < dl ? dz : dl;
        if (dz > c.range_mm || !Band(w.zelda, e, c.vertical)) continue;
        if (d <= c.threat_mm) take(Situation::Threat, i, d);
    }
    if (p.situation != Situation::None) return p;
    if (w.link_hp > 0 && w.link_hp <= c.danger_hp) {
        for (unsigned i = 0; i < w.count; ++i) {
            const Enemy& e = w.enemies[i];
            const float dl = Flat(w.link, e);
            if (Flat(w.zelda, e) > c.range_mm || !Band(w.zelda, e, c.vertical) || dl > c.danger_mm) continue;
            take(Situation::Danger, i, dl);
        }
    }
    return p;
}

// One ray per situation trigger: a trigger is (situation, target). It may fire once; it re-arms only after the
// assessments without it reach kRearmFrames (the situation ended; the glue assesses every kAssessMs), so a Ganado still swinging, or still carrying her, is not shot
// again and again (the cooldown alone would let a long carry draw a ray every 8-15 s).
constexpr unsigned kRearmFrames = 30;
constexpr double kAssessMs = 50.0;   // 30 assessments = 1.5 s without the situation
class Latch {
    Situation s_ = Situation::None; uint32_t target_ = 0; bool fired_ = false; unsigned absent_ = 0;
public:
    // Every assessed frame; returns whether this trigger may still fire.
    bool Observe(Situation s, uint32_t target) {
        if (s == Situation::None) {
            if (fired_ && ++absent_ >= kRearmFrames) { fired_ = false; s_ = Situation::None; target_ = 0; }
            return false;
        }
        if (s != s_ || target != target_) { s_ = s; target_ = target; fired_ = false; }   // a new trigger
        absent_ = 0;
        return !fired_;
    }
    void Fired() { fired_ = true; absent_ = 0; }
};

// Random cooldown between rays, in game ms. Time running back (a save load) re-arms from the new time.
class Cooldown {
    uint32_t state_; double ready_ = 0, last_ = 0; bool armed_ = false;
public:
    explicit Cooldown(uint32_t seed = 0x9E3779B9u) : state_(seed ? seed : 1u) {}
    uint32_t Next() { uint32_t x = state_; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return state_ = x; }
    // Draw in [min_s, max_s] seconds (max below min is taken as min), in ms.
    double Draw(unsigned min_s, unsigned max_s) {
        if (max_s < min_s) max_s = min_s;
        const double span = double(max_s - min_s) * 1000.0;
        return double(min_s) * 1000.0 + span * (double(Next() % 100001u) / 100000.0);
    }
    bool Ready(double now) {
        if (!armed_) return true;
        if (now < last_) { armed_ = false; return true; }
        return now >= ready_;
    }
    void Arm(double now, unsigned min_s, unsigned max_s) { last_ = now; ready_ = now + Draw(min_s, max_s); armed_ = true; }
    double ReadyAt() const { return ready_; }
};

// The partner's identity and sanity, read each frame. A run of implausible frames disables the feature (bad offsets or
// a foreign object behind the slot are never acted on).
constexpr unsigned kImplausibleLimit = 60;
class Sanity {
    unsigned bad_ = 0; bool dead_ = false;
public:
    bool Dead() const { return dead_; }
    // Returns true when the values may be used this frame.
    bool Check(bool vtable_ok, unsigned id, unsigned r0, unsigned r1) {
        if (dead_) return false;
        if (!vtable_ok || id != kAshleyId) return false;   // no partner Ashley now (Luis, nobody): not a fault
        if (Plausible(r0, r1)) { bad_ = 0; return true; }
        if (++bad_ >= kImplausibleLimit) dead_ = true;
        return false;
    }
};

// The caught bit is cleared at the top of cSubChar::move and set again by the carrier's routine, so a reader at the
// player's dispatch can see either value: seen set within the last kCaughtHoldMs counts as caught.
constexpr double kCaughtHoldMs = 400.0;
class CaughtDebounce {
    double seen_ = -1e18;
public:
    bool Update(bool bit, double now) {
        if (bit) seen_ = now;
        if (now < seen_) seen_ = now;   // time ran back
        return now - seen_ <= kCaughtHoldMs;
    }
};

// INI [ZeldaLight] keys of this feature. get(key, out, count) as zelda_light::ReadConfig.
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[32]{};
    auto number = [&](const wchar_t* key, unsigned fallback, unsigned lo, unsigned hi) {
        v[0] = 0; get(key, v, 32);
        if (!v[0]) return fallback;
        unsigned n = 0;
        for (const wchar_t* p = v; *p; ++p) {
            if (*p < L'0' || *p > L'9') { ++problems; return fallback; }
            n = n * 10 + unsigned(*p - L'0'); if (n > 1000000) { ++problems; return fallback; }
        }
        if (n < lo || n > hi) { ++problems; return fallback; }
        return n;
    };
    v[0] = 0; get(L"ZeldaCompanionRay", v, 32);
    c.on = v[0] == L'1' && !v[1];
    c.cooldown_min_s = number(L"ZeldaCompanionCooldownMin", 8, 1, 300);
    c.cooldown_max_s = number(L"ZeldaCompanionCooldownMax", 15, 1, 300);
    if (c.cooldown_max_s < c.cooldown_min_s) { ++problems; c.cooldown_max_s = c.cooldown_min_s; }
    return c;
}
}  // namespace link_runtime::zelda_companion

#pragma once
// Master Sword Blade Lock candidate 9: the Salvador takes part in the lock (Donald, 4-nov 19:40: "es muy fome que
// solamente Link esté struggling y moviéndose; el Dr. Salvador también está con la chainsaw prendida, el choque del metal
// de la chainsaw con la Master Sword debería sonar bien fuerte, metálico de espadas, y el Salvador estaría haciendo
// sonidos de struggle"). Lab DLL only: link_runtime.cpp includes it inside #if LINK_SWORD_LOCK_LAB. Pure: no hooks, no
// process access, no audio device.
//
// Why he looked dead (candidate 9 study, 4-nov): the duel's freeze sets SPF_EM, so cEmMgr::move skips his emMove: no
// AI, no motion, no partsWorldCalc (his bone matrices keep the last frame) and no cEm10::move sound triggers (his
// running engine SndCall(6, 0x4D) is re-fired every 60 frames from there: one 1.01 s play, then silence). H2c's sway
// wrote only cModel::pos (+0x94), which the renderer uses for ordering and culling, not for the skinned mesh.
//
// What candidate 9 does:
//  * Motion: the native cModel::setPos (re4-decomp model.cpp:494-517) moves a model by shifting every part's world
//    matrix translation (cParts +0x0C mat, column 3), world (+0x70) and world_old (+0x7C). The lab does the same to his
//    body parts AND to his chainsaw's (em10 +0x54C pWeapon, a cEm: its own parts chain) by the same offset: the sway's
//    (now visible) plus a struggle: a slow shudder across the blade axis and up / down that grows with the strain
//    (|PV|), a jolt on each of Link's hit breaks, a surge toward Link on a miss. The chainsaw alone also buzzes (the
//    engine's vibration, a few mm a frame). Every write is undone at the end (the release, an abandon, a fault); the
//    world's next partsWorldCalc rebuilds every matrix from his motion anyway, so a missed undo heals on the next frame.
//  * Sounds (scripts/sword_lock_salvador_sounds.py, SHA-pinned below; a missing or changed file mutes that sound only):
//    (candidate 16: no engine loop, kEngineLoop) a LOUD metal clash at the Traba, on each hit break and each miss;
//    his own struggle grunts (em15 waves 79, 75, 76, 81: Donald's picks), one on each hit break and one every 1.1..1.6 s
//    of game time, and the long one when Link wins. Against Dark Link none of this plays (he has his own voices).
// Every time is the duel's game time (60 and 165 FPS alike). Tests: tests/native/test_sword_lock_salvador.cpp.
#include "sword_lock_duel.h"
#include "sword_lock_darklink.h"
#include <cmath>
#include <cstdint>
#include <cwchar>

namespace link_runtime::sword_lock::salvador {
namespace sd = link_runtime::sword_lock::duel;

// ---- layout (re4-decomp include/model.h, em10.h; UHD = GC for these, as the rest of the mod reads them) ----------
constexpr uint32_t kWeaponPtr = 0x54C;   // cEm10::pWeapon (GC layout; NULL on UHD: Donald's cand10 log, why=weapon saw=0)
// Candidate 11: the chainsaw found the other way round, as the game itself links it (re4-decomp emwep.cpp setParent):
// a live cEmWep (the thrown-weapon vtable the parry already pins) in its Parent routine (r0 1, r1 3) whose pEm_oya
// (FREE_EMWEP +0x084 = em +0x464) is the Salvador.
constexpr uint32_t kWepOya = 0x464;
constexpr uint8_t kWepParentR0 = 1, kWepParentR1 = 3;
constexpr float kDiagMm = 3000.f;    // candidate 12: the ems logged around him at the duel's start
constexpr float kSawNearMm = 2000.f; // a held cEmWep this near him is his saw when no pEm_oya match exists
constexpr uint32_t kFirstPart = 0xF4;    // cModel::pList
constexpr uint32_t kNextPart = 0xF4;     // cParts::pList
constexpr uint32_t kNParts = 0x102;      // cModel::nParts (u8)
constexpr uint32_t kMatTx = 0x0C + 0x0C, kMatTy = 0x0C + 0x1C, kMatTz = 0x0C + 0x2C;   // cCoord::mat column 3
constexpr uint32_t kWorld = 0x70, kWorldOld = 0x7C;
constexpr uint32_t kPos = 0x94;
constexpr unsigned kMaxParts = 96;
constexpr float kMaxPartReachMm = 3500.f;   // a part's translation never farther than this from its model's pos
constexpr float kMaxWeaponMm = 2500.f;      // the chainsaw's pos within this of his

// [Combat] SwordLockSalvadorMotion / SwordLockSalvadorSound (lab DLL only): missing = 1, "0" = off, anything else
// counts a problem and stays on.
struct Config {bool motion = true, sound = true;};
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[16]{};
    const auto one = [&](const wchar_t* key, bool& out) {
        v[0] = 0; get(key, v, 16);
        if (!v[0] || (v[0] == L'1' && !v[1])) out = true;
        else if (v[0] == L'0' && !v[1]) out = false;
        else {out = true; ++problems;}
    };
    one(L"SwordLockSalvadorMotion", c.motion);
    one(L"SwordLockSalvadorSound", c.sound);
    return c;
}

struct V3 {float x = 0, y = 0, z = 0;};
inline V3 Add(V3 a, V3 b) {return {a.x + b.x, a.y + b.y, a.z + b.z};}
inline V3 Sub(V3 a, V3 b) {return {a.x - b.x, a.y - b.y, a.z - b.z};}
inline V3 Mul(V3 a, float k) {return {a.x * k, a.y * k, a.z * k};}
inline float Len(V3 a) {return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);}
inline bool Finite(V3 a) {return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);}
inline double ValidH(double h) {return std::isfinite(h) && h > 0.0 && h <= 0.25 ? h : 0.0;}

// ---- the struggle ---------------------------------------------------------------------------------------------
constexpr float kShudderSideMm = 60.f;    // across the blade axis, at full strain (cand14: x2, cand13's 30 hardly showed)
constexpr float kShudderUpMm = 36.f;      // up / down, at full strain
constexpr float kShudderBase = 0.55f;     // of the amplitude with no strain (he still pushes)
constexpr double kShudderHz = 4.5;        // new noise targets per second (smoothstep between them)
constexpr float kJoltUpMm = -60.f;        // a hit break: he sags under the blow
constexpr float kJoltBackMm = 90.f;       // ... and is thrown back a little more (on top of the sway's recoil)
constexpr float kSurgeMm = 120.f;         // a miss: he surges toward Link
constexpr double kJoltS = 0.30, kSurgeS = 0.40;
constexpr float kBuzzMm = 4.f;            // the chainsaw's engine vibration (each fresh frame, the saw only)
constexpr float kInTau = 0.08f, kOutTau = 0.05f;   // s: the struggle's weight in / out
constexpr float kDoneWeight = 0.01f;

struct Facts {
    bool on = false;      // the duel runs; off: the struggle eases out
    double pv = 0;        // PushValue -1..1 (+ = Link winning)
    unsigned hits = 0, misses = 0;
};
// A deterministic LCG (Numerical Recipes), one seed per duel.
struct Rng {
    uint32_t s = 1;
    float Unit() {s = s * 1664525u + 1013904223u; return float(s >> 8) * (1.f / 8388608.f) - 1.f;}   // [-1, 1)
};
inline float Smooth(float t) {t = t < 0.f ? 0.f : t > 1.f ? 1.f : t; return t * t * (3.f - 2.f * t);}
// Value noise in [-1, 1]: a new target kShudderHz times a second, smoothstep between.
class Noise {
public:
    void Seed(uint32_t s) {rng_.s = s ? s : 1; a_ = rng_.Unit(); b_ = rng_.Unit(); t_ = 0;}
    float Step(double h) {
        t_ += h * kShudderHz;
        while (t_ >= 1.0) {t_ -= 1.0; a_ = b_; b_ = rng_.Unit();}
        return a_ + (b_ - a_) * Smooth(float(t_));
    }
private:
    Rng rng_; float a_ = 0, b_ = 0; double t_ = 0;
};
class Struggle {
public:
    // The Traba: u = the XZ unit from Link to him (the blade axis); false: no axis.
    bool Begin(V3 u, uint32_t seed, unsigned hits, unsigned misses) {
        *this = Struggle{};
        const float l = std::sqrt(u.x * u.x + u.z * u.z);
        if (!(l > 0.5f) || !std::isfinite(l)) return false;
        u_ = {u.x / l, 0.f, u.z / l}; side_ = {-u_.z, 0.f, u_.x};
        side_n_.Seed(seed * 2654435761u + 1u); up_n_.Seed(seed * 40503u + 7u); buzz_.s = seed ^ 0x9E3779B9u;
        if (!buzz_.s) buzz_.s = 1;
        hits_ = hits; misses_ = misses; jolt_t_ = kJoltS; surge_t_ = kSurgeS; on_ = true;
        return true;
    }
    // One fresh step of h s of game time (0: no time, the same offsets).
    void Step(const Facts& f, double h) {
        if (!on_) return;
        h = ValidH(h);
        if (f.hits > hits_) {jolt_t_ = 0; ++jolts_;}
        if (f.misses > misses_) {surge_t_ = 0; ++surges_;}
        hits_ = f.hits; misses_ = f.misses;
        if (h <= 0.0) return;
        const float target = f.on ? 1.f : 0.f, tau = f.on ? kInTau : kOutTau;
        w_ += (target - w_) * float(1.0 - std::exp(-h / tau));
        if (!f.on && w_ < kDoneWeight) w_ = 0.f;
        jolt_t_ += h; surge_t_ += h;
        const float strain = std::fabs(float(f.pv)) > 1.f ? 1.f : std::fabs(float(f.pv));
        const float amp = kShudderBase + (1.f - kShudderBase) * strain;
        const float side = side_n_.Step(h) * kShudderSideMm * amp, up = up_n_.Step(h) * kShudderUpMm * amp;
        const float jk = jolt_t_ < kJoltS ? float(1.0 - jolt_t_ / kJoltS) : 0.f, sk = surge_t_ < kSurgeS ? float(1.0 - surge_t_ / kSurgeS) : 0.f;
        const float jolt = jk * jk * (3.f - 2.f * jk), surge = sk * sk * (3.f - 2.f * sk);
        body_ = Mul(Add(Add(Mul(side_, side), V3{0.f, up + kJoltUpMm * jolt, 0.f}),
                        Mul(u_, kJoltBackMm * jolt - kSurgeMm * surge)), w_);
        buzz_v_ = Mul(V3{buzz_.Unit(), buzz_.Unit(), buzz_.Unit()}, kBuzzMm * w_);
        ++steps_;
        const float b = Len(body_);
        if (b > max_body_) max_body_ = b;
    }
    V3 body() const {return body_;}                    // his body and the chainsaw
    V3 saw() const {return Add(body_, buzz_v_);}       // the chainsaw alone: plus the engine's buzz
    float weight() const {return w_;}
    bool on() const {return on_;}
    bool done() const {return on_ && w_ <= 0.f && steps_ > 0;}
    void Stop() {on_ = false; body_ = buzz_v_ = V3{}; w_ = 0.f;}
    unsigned jolts() const {return jolts_;}
    unsigned surges() const {return surges_;}
    unsigned steps() const {return steps_;}
    float max_body() const {return max_body_;}
private:
    V3 u_, side_, body_, buzz_v_;
    Noise side_n_, up_n_; Rng buzz_;
    float w_ = 0.f, max_body_ = 0.f;
    double jolt_t_ = 0, surge_t_ = 0;
    unsigned hits_ = 0, misses_ = 0, jolts_ = 0, surges_ = 0, steps_ = 0;
    bool on_ = false;
};

// ---- the parts' shift (setPos without pos): what is applied now, the next delta --------------------------------
// The runtime walks a model's parts (at most kMaxParts, the chain's length must match nParts) and adds the delta to
// mat's translation, world and world_old. Applied() is the offset the parts carry now; a foreign write (his translation
// no longer where we left it: the world moved him) ends the shift without an undo (partsWorldCalc rebuilt them).
class Shift {
public:
    V3 Delta(V3 want) const {return Sub(want, applied_);}
    void Applied(V3 now) {applied_ = now; ++writes_; const float l = Len(now); if (l > max_mm_) max_mm_ = l;}
    V3 applied() const {return applied_;}
    void Reset() {*this = Shift{};}
    unsigned writes() const {return writes_;}
    float max_mm() const {return max_mm_;}
private:
    V3 applied_; unsigned writes_ = 0; float max_mm_ = 0;
};
// A part's translation as the shift expects it: within half a mm of where it last left it.
inline bool Ours(V3 seen, V3 expected) {return std::fabs(seen.x - expected.x) <= 0.5f && std::fabs(seen.y - expected.y) <= 0.5f &&
                                              std::fabs(seen.z - expected.z) <= 0.5f;}
// The chainsaw the lab shifts with him: a readable pointer, its pos near his, finite.
inline bool WeaponNear(V3 his, V3 saw) {return Finite(his) && Finite(saw) && Len(Sub(his, saw)) <= kMaxWeaponMm;}
inline bool PartSane(V3 model_pos, V3 part) {return Finite(part) && Len(Sub(part, model_pos)) <= kMaxPartReachMm;}

// ---- the sounds -------------------------------------------------------------------------------------------------
enum Loop : uint8_t {kEngine = 0, kLoopCount};
// Candidate 12 (Donald, cand11: "la voz del dr era la misma spameada", "sonó Ganondorf cuando a mí me ganaron"): TP's
// Ganondorf strain (dl_lock_voice_a/b/c, the Dark Link files and pins) as ONE-SHOTS, spaced and varied by the band, his
// defeat groan when Link wins and his triumph when Link loses, instead of the band's voice looping on every frame.
enum Shot : uint8_t {kClash = 0, kGrunt1, kGrunt2, kGrunt3, kGrunt4, kStrainA, kStrainB, kStrainC, kDefeat, kTriumph, kShotCount};
enum class Voice : uint8_t {Em15, Ganondorf, None};
namespace dl = link_runtime::sword_lock::darklink;
inline const wchar_t* LoopPath(unsigned i) {return i == kEngine ? L"salvador_lock_engine.wav" : L"";}
inline const wchar_t* ShotPath(unsigned i) {
    static const wchar_t* const n[kStrainA] = {L"salvador_lock_clash.wav", L"salvador_lock_grunt_1.wav", L"salvador_lock_grunt_2.wav",
                                               L"salvador_lock_grunt_3.wav", L"salvador_lock_grunt_4.wav"};
    if (i < kStrainA) return n[i];
    if (i <= kStrainC) return dl::LoopPath(dl::kVoiceA + (i - kStrainA));
    return i == kDefeat ? dl::ShotPath(dl::kLose) : i == kTriumph ? dl::ShotPath(dl::kWin) : L"";
}
// SHA-256 of scripts/sword_lock_salvador_sounds.py's output (deterministic), in Bin32\sword_lock_lab\.
inline const char* LoopSha(unsigned i) {
    return i == kEngine ? "d8e39479123e43266077946635ac9d0e3e3dbd287c91c7d9f973ca365d138c8a" : "";   // salvador_lock_engine.wav (em15 wave 38, 0.77 s loop)
}
inline const char* ShotSha(unsigned i) {
    if (i >= kStrainA && i <= kStrainC) return dl::LoopSha(dl::kVoiceA + (i - kStrainA));
    if (i == kDefeat) return dl::ShotSha(dl::kLose);
    if (i == kTriumph) return dl::ShotSha(dl::kWin);
    static const char* const s[kStrainA] = {
        "a650be1e4a06cb09e8f9ad33ddc50a70088cfeef805a155835a22647cc941edf",   // salvador_lock_clash.wav
        "175ae646350c6a9d747fc72fc8b6a8d382afc595e84976e6c8b6fb599b56b657",   // salvador_lock_grunt_1.wav (em15 wave 79, the long one)
        "10fe4adfd3b36d2da285dc0663bc2d66e87279391a6a96b147e1b0451773a1f6",   // salvador_lock_grunt_2.wav (wave 75)
        "884eb5c3b82e122baee5423b1ae8a509d933a39e9cfb0fb0d769802817c16e0d",   // salvador_lock_grunt_3.wav (wave 76)
        "110d3b1bea821d9c13a0c6bc5d43d12484bea0d7e2049cec355b656873433fb7"};  // salvador_lock_grunt_4.wav (wave 81)
    return i < kStrainA ? s[i] : "";
}
inline const char* ShotName(unsigned i) {
    static const char* const n[kShotCount] = {"clash", "grunt_1", "grunt_2", "grunt_3", "grunt_4", "strain_a", "strain_b", "strain_c",
                                              "defeat", "triumph"};
    return i < kShotCount ? n[i] : "none";
}
constexpr uint8_t LoopBit(unsigned i) {return static_cast<uint8_t>(1u << i);}
// Candidate 16 (Donald's 3.6.0 play, 5-nov: "solamente grita"; alone, salvador_lock_engine.wav: "solo escuche el grito"):
// em15 wave 38 is a scream, not the chainsaw, and it looped for the whole lock since candidate 9. Off until a real motor
// is picked by ear; the file stays pinned and loaded, nothing plays it.
constexpr bool kEngineLoop = false;
constexpr double kFirstGruntMs = 650;          // after the Traba
constexpr double kGruntGapMs[] = {1150, 1500, 1300, 1600};
constexpr double kGruntMinMs = 380;            // never two grunts closer than this (a hit right after the timed one)
constexpr unsigned kGruntOrder[] = {kGrunt2, kGrunt4, kGrunt3, kGrunt2, kGrunt4, kGrunt3, kGrunt1};   // the long one now and then
// Ganondorf: the band's strain most of the time, the others in between, never the same one twice in a row
constexpr unsigned kStrainOrder[3][4] = {{kStrainA, kStrainB, kStrainA, kStrainC},    // band A (even)
                                         {kStrainB, kStrainA, kStrainB, kStrainC},    // band B (the Salvador winning)
                                         {kStrainC, kStrainB, kStrainC, kStrainA}};   // band C (Link winning)

struct Out {uint8_t loops = 0; uint32_t shots[kShotCount]{};};
struct SoundFacts {
    bool running = false, paused = false;
    sd::Band band = sd::Band::A;   // candidate 12: picks the Ganondorf strain
    uint32_t ev = 0;
    sd::Result result = sd::Result::None;
    double game_ms = 0;
};
class Director {
public:
    // grunts: candidate 9's em15 voices. Candidate 11 (Donald, 4-nov: "era un grito nomás, nada que ver a esfuerzo"; he
    // chose Ganondorf's TP strain instead): the runtime starts it without them and plays the V_TUBAZERI voices by band.
    void Start(Voice v = Voice::Em15) {
        const Out keep = out_;
        *this = Director();
        for (unsigned i = 0; i < kShotCount; ++i) out_.shots[i] = keep.shots[i];   // counters run for the whole session
        on_ = true; voice_ = v; Play(kClash); out_.loops = kEngineLoop ? LoopBit(kEngine) : 0; next_grunt_ = kFirstGruntMs;
    }
    void Tick(const SoundFacts& f) {
        if (!on_) return;
        if (f.ev & (sd::kEvHit | sd::kEvMiss | sd::kEvRound)) Play(kClash);   // candidate 10: an overtime round re-locks the blades
        band_ = f.band;
        if (!f.running) {
            if (voice_ == Voice::Ganondorf) {
                if (f.result == sd::Result::Win) Grunt(kDefeat, f.game_ms, true);         // Link won: his defeat groan
                else if (f.result == sd::Result::Lose) Grunt(kTriumph, f.game_ms, true);  // Link lost: his triumph
            } else if (f.result == sd::Result::Win) Grunt(kGrunt1, f.game_ms, true);   // Link won: his long one
            Stop();
            return;
        }
        if (f.ev & (sd::kEvHit | sd::kEvRound)) Grunt(NextGrunt(), f.game_ms, false);
        else if (!f.paused && f.game_ms >= next_grunt_) Grunt(NextGrunt(), f.game_ms, false);
        out_.loops = f.paused || !kEngineLoop ? 0 : LoopBit(kEngine);
    }
    void Stop() {on_ = false; out_.loops = 0;}
    const Out& out() const {return out_;}
    bool on() const {return on_;}
    unsigned grunts() const {return grunts_;}
    unsigned clashes() const {return out_.shots[kClash];}
private:
    Out out_;
    bool on_ = false;
    Voice voice_ = Voice::Em15;
    sd::Band band_ = sd::Band::A;
    unsigned grunts_ = 0, order_ = 0;
    double last_grunt_ = -1e9, next_grunt_ = kFirstGruntMs;
    void Play(unsigned s) {++out_.shots[s];}
    unsigned NextGrunt() const {
        if (voice_ == Voice::Ganondorf) {
            const unsigned b = band_ == sd::Band::B ? 1u : band_ == sd::Band::C ? 2u : 0u;
            return kStrainOrder[b][order_ % 4];
        }
        return kGruntOrder[order_ % (sizeof(kGruntOrder) / sizeof(kGruntOrder[0]))];
    }
    void Grunt(unsigned which, double t, bool force) {
        if (voice_ == Voice::None) return;
        if (!force && t - last_grunt_ < kGruntMinMs) return;
        Play(which); ++grunts_; last_grunt_ = t;
        if (!force) ++order_;
        next_grunt_ = t + kGruntGapMs[grunts_ % (sizeof(kGruntGapMs) / sizeof(kGruntGapMs[0]))];
    }
};

}  // namespace link_runtime::sword_lock::salvador

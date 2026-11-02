#pragma once
// Zelda's cast clip (zelda-companion-01, Donald 30-oct: «que la animación del rayo sea más natural, ahora parece
// robótico»). TP's possessed Zelda throwing her ball of light (Hzelda.arc bck/hzelda_attack_c.bck, release at TP frame
// 41, d_a_e_hzelda.cpp:499) retargeted offline onto Ashley's skeleton (scripts/zelda_cast_anim.py): parts 1-16 rotation
// only, played on the native secondary-motion slot cModel+0x2A8 (pMotionB, re4-decomp include/model.h:515) the way the
// parry reaction and the shield guard drive it for Link (parry_reaction.h). Pure logic: timing, layer weight, the slot
// decision and the eased facing turn. The process side is zelda_cast_clip_runtime.h.
//
// Native contract (re4-decomp src/game/motion.cpp MotionMove, model.cpp matBlend): MotionMove copies the primary's
// Seq_speed into pMotionB (+0xC0), runs it and blends it in by its Brate (+0xC8, quaternion slerp per part); weight 0
// skips the blend. Any primary MotionSetCore without keep flag 0x20000000 clears +0x2A8 (we never fight a foreign work:
// only a freed slot or the player's own neck work is taken back). Seq_frame (+0xB8, frames in sequence time, one SEQ
// entry per TP frame) tells where the clip is: the ray fires there, with a game-time fallback.
#include <cmath>
#include <cstdint>

namespace link_runtime::zelda_cast_clip {
// scripts/zelda_cast_anim.py manifest (D:/link-in-re4-work/zelda-companion-01/anim/manifest.json).
constexpr unsigned kEntries = 24;              // SEQ entries 0..24 at TP speed (30 per game second)
constexpr double kEntryMs = 1000.0 / 30.0;
constexpr double kClipMs = kEntries * 1000.0 / 30.0;   // 800 ms (exact)
constexpr double kReleaseEntry = 15.0;         // TP frame 41 after the warp (wind-up x2.7, the throw at TP speed)
constexpr double kReleaseMs = kReleaseEntry * 1000.0 / 30.0;   // 500 ms (exact)
constexpr double kReleaseLateMs = 120.0;       // fallback: no readable Seq_frame by then, fire on game time
constexpr double kRampInMs = 130.0, kRampOutMs = 220.0;
constexpr double kTurnMs = 180.0;              // the facing turn toward the target (was a one-frame snap)
inline constexpr wchar_t kFcvName[] = L"link_zelda_cast.fcv";
inline constexpr wchar_t kSeqName[] = L"link_zelda_cast.seq";
constexpr unsigned kFcvBytes = 226592, kSeqBytes = 128;
inline constexpr char kFcvSha[] = "0c4b02e2a1d7d6126ab1c93381ae82239d7c7f9d1b7d03aae65082905a04314b";
inline constexpr char kSeqSha[] = "16ef8be1c2a572ab498f133759d9f163842fb768fb7942a21fe7227a49e5a9bf";
namespace work {   // MOTION_INFO (re4-decomp include/model.h:249-289); 0x42/0x44/0xC0/0xC8 used live by parry_reaction
constexpr uint32_t status = 0x42, flags = 0x44, seq_frame = 0xB8, speed = 0xC0, weight = 0xC8, size = 0x200;
}
constexpr uint32_t kSecondary = 0x2A8;         // cModel::pMotionB
constexpr uint32_t kNeckWork = 0x474;          // cPlayer's neck work (parry_reaction.h kNeckWorkOffset)

inline double Smooth(double t) {
    if (!(t > 0)) return 0;
    if (t >= 1) return 1;
    return t * t * (3 - 2 * t);
}
// The clip's blend weight at `age` ms: eases in, holds, eases out to exactly 0 at the end (MotionMove then shows the
// native pose before the slot is given back).
inline float LayerWeight(double age_ms) {
    if (!std::isfinite(age_ms) || age_ms <= 0 || age_ms >= kClipMs) return 0.f;
    if (age_ms < kRampInMs) return float(Smooth(age_ms / kRampInMs));
    if (age_ms > kClipMs - kRampOutMs) return float(Smooth((kClipMs - age_ms) / kRampOutMs));
    return 1.f;
}
// The ray leaves at the clip's release: the secondary's Seq_frame (when it is ours and readable) or, failing that,
// game time past the release plus a small grace.
inline bool ReleaseDue(bool seq_known, double seq_frame, double age_ms) {
    if (seq_known && std::isfinite(seq_frame) && seq_frame >= kReleaseEntry && seq_frame <= kEntries + 1.0) return true;
    return std::isfinite(age_ms) && age_ms >= kReleaseMs + (seq_known ? kReleaseLateMs : 0.0);
}
// The slot +0x2A8 each tick of a running clip.
enum class Slot : uint8_t { Ours, Retake, Yield };
// current: the slot now; ours: our work; neck: the model's own neck work (0 for a model without one, the partner).
// A freed slot (a primary MotionSet) and the neck's takeover are taken back; any other work is a native layer that
// wins (the clip ends without a write).
inline Slot Decide(uint32_t current, uint32_t ours, uint32_t neck) {
    if (current == ours) return Slot::Ours;
    if (current == 0 || (neck && current == neck)) return Slot::Retake;
    return Slot::Yield;
}
// May the clip be installed over this slot value?
inline bool Installable(uint32_t current, uint32_t neck) { return current == 0 || (neck && current == neck); }

// Shortest signed angle from a to b (radians, in [-pi, pi]).
inline double AngleDelta(double a, double b) {
    constexpr double two_pi = 6.28318530717958647692, pi = 3.14159265358979323846;
    double d = std::fmod(b - a, two_pi);
    if (d > pi) d -= two_pi;
    if (d < -pi) d += two_pi;
    return d;
}
// The facing during a cast: from her own yaw `from` to `to` along the shortest arc, smoothstep over kTurnMs. NaN `to`
// keeps `from`.
inline float EaseYaw(float from, float to, double age_ms) {
    if (!std::isfinite(from)) return from;
    if (!std::isfinite(to)) return from;
    const double k = Smooth(std::isfinite(age_ms) ? age_ms / kTurnMs : 1.0);
    return float(from + AngleDelta(from, to) * k);
}
}  // namespace link_runtime::zelda_cast_clip

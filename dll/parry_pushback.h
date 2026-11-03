#pragma once
// Heavy parry pushback (parry-heavy-01, Donald 31-oct): a tier-2 block of a hit with power >= ParryHeavyPower (default
// 1000) still cancels the hit, and Link slides back TP-style, away from the hit point, ParryHeavyPushback units
// (default 1300; 700 until 2-nov, Donald found it too subtle) over ParryHeavyFrames 60 Hz frames of game time (default
// 20), easing out. 0 distance = off.
//
// Wiring (link_runtime.cpp): Start on the game thread when the general block cancels a heavy hit; Step around the
// player's MotionMove (CmbMoveDetour), after the original call, only when no sword action owns the move; Apply writes
// X/Z before cPlayer::move runs the native collision on them (as the backflip), so walls stop the slide. A new player,
// a damaged/dead routine or a failed write ends it. Pure logic plus a templated Apply: no process access of its own.
//
// Pause, menu, event (pending 14, 3-nov; review note on 3.4.0 "the heavy pushback can resume late"): the slide only
// advances by the game step of the player's own MotionMove, so a pause that freezes the game (frame counter still)
// just suspends it and it finishes the remaining game frames afterwards. When the game ran ticks without that
// MotionMove (an event, a menu that keeps the counter going, a sword action owning the move) or the counter went back
// (a load), the native frame counter given to Start/Step jumps: the slide is cancelled (Stop::Gap) instead of
// resuming late. Frame 0 = counter unknown: no gap check (the legacy wiring and tests).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace link_runtime::parry::pushback {
struct Config {
    unsigned heavy_power = 1000;   // ParryHeavyPower: hits at or above this push Link back (1..9998)
    float distance = 1300.f;       // ParryHeavyPushback: units of the slide (0 = off, max 3000)
    float frames = 20.f;           // ParryHeavyFrames: 60 Hz frames of game time (4..60)
};
template <class Get> Config ReadConfig(Get get, unsigned& problems) {
    Config c;
    auto num = [&](const wchar_t* key, long lo, long hi, long fallback) {
        wchar_t text[32]{}; get(key, text, 32);
        if (!text[0]) return fallback;
        wchar_t* end = nullptr; const long v = std::wcstol(text, &end, 10);
        if (end == text || *end || v < lo || v > hi) { ++problems; return fallback; }
        return v;
    };
    c.heavy_power = static_cast<unsigned>(num(L"ParryHeavyPower", 1, 9998, 1000));
    c.distance = static_cast<float>(num(L"ParryHeavyPushback", 0, 3000, 1300));
    c.frames = static_cast<float>(num(L"ParryHeavyFrames", 4, 60, 20));
    return c;
}
inline bool Heavy(const Config& c, uint16_t power) { return c.distance > 0.f && power >= c.heavy_power; }
inline float Ease(float t) { t = t < 0.f ? 0.f : t > 1.f ? 1.f : t; return 1.f - (1.f - t) * (1.f - t); }

// Unit XZ direction from the hit point to Link; Link's backward facing (-sin yaw, -cos yaw) when the two are closer
// than 1 unit or not finite.
inline bool Direction(const float hit[3], const float link[3], float yaw, float& ux, float& uz) {
    const float dx = link[0] - hit[0], dz = link[2] - hit[2], d = std::sqrt(dx * dx + dz * dz);
    if (std::isfinite(d) && d >= 1.f) { ux = dx / d; uz = dz / d; return true; }
    if (!std::isfinite(yaw)) return false;
    ux = -std::sin(yaw); uz = -std::cos(yaw);
    return true;
}

enum class Stop : uint8_t {None, Finished, Player, Damaged, Write, Gap};
inline const char* StopName(Stop s) {
    switch (s) {case Stop::Finished: return "finished"; case Stop::Player: return "player"; case Stop::Damaged: return "damaged";
    case Stop::Write: return "write"; case Stop::Gap: return "gap"; default: return "-";}
}

// Native ticks (frame counter +0x530C, one per logic tick at any FPS) the game may run between two of the slide's
// MotionMoves before the slide counts as interrupted: slack for a native hit-stop (0.18 s at 165 FPS, 0.5 s at 60),
// far below any menu or event.
inline constexpr int32_t kMaxGapTicks = 30;

class Slide {
public:
    // A new heavy block restarts the slide from where Link is (the remaining part of an older one is dropped).
    // frame: the native frame counter now (0 = unknown).
    bool Start(const Config& c, uint32_t player, float ux, float uz, uint32_t frame = 0) {
        if (!player || !(c.distance > 0.f) || !std::isfinite(ux) || !std::isfinite(uz)) return false;
        on_ = true; player_ = player; ux_ = ux; uz_ = uz; dist_ = c.distance; frames_ = c.frames < 1.f ? 1.f : c.frames;
        t_ = 0.f; moved_ = 0.f; stop_ = Stop::None; frame_ = frame;
        return true;
    }
    // One player MotionMove of `step` 60 Hz frames: the displacement for this tick (0 once finished). frame: the
    // native frame counter of this tick (0 = unknown); a jump past kMaxGapTicks, or back, since the last one cancels.
    bool Step(uint32_t player, float step, float& dx, float& dz, uint32_t frame = 0) {
        dx = dz = 0.f;
        if (!on_) return false;
        if (player != player_) { Halt(Stop::Player); return false; }
        if (frame && frame_) {
            const int32_t gap = static_cast<int32_t>(frame - frame_);   // wraps like the counter
            if (gap < 0 || gap > kMaxGapTicks) { Halt(Stop::Gap); return false; }
        }
        if (frame) frame_ = frame;
        if (!std::isfinite(step) || step <= 0.f) return false;
        const float t0 = t_;
        t_ = t_ + step > frames_ ? frames_ : t_ + step;
        const float d = dist_ * (Ease(t_ / frames_) - Ease(t0 / frames_));
        dx = ux_ * d; dz = uz_ * d; moved_ += d;
        if (t_ >= frames_) Halt(Stop::Finished);
        return d != 0.f;
    }
    void Abort(Stop why) { Halt(why); }
    bool Active() const { return on_; }
    float moved() const { return moved_; }
    Stop stop() const { return stop_; }
private:
    void Halt(Stop why) { if (on_) { on_ = false; stop_ = why; } }
    bool on_ = false; uint32_t player_ = 0, frame_ = 0; float ux_ = 0, uz_ = 0, dist_ = 0, frames_ = 1, t_ = 0, moved_ = 0; Stop stop_ = Stop::None;
};

// Memory: Read / Writable / Write. Adds (dx, dz) to Link's X/Z (+0x94/+0x9C) when both reads agree.
template <class Memory> bool Apply(Memory& m, uint32_t player, float dx, float dz) {
    if (!player || !std::isfinite(dx) || !std::isfinite(dz)) return false;
    if (dx == 0.f && dz == 0.f) return true;
    float a[3]{}, b[3]{};
    if (!m.Read(player + 0x94, a, sizeof(a)) || !m.Writable(player + 0x94, 0xC) || !m.Read(player + 0x94, b, sizeof(b)) ||
        std::memcmp(a, b, sizeof(a))) return false;
    const float x = a[0] + dx, z = a[2] + dz;
    return m.Write(player + 0x94, &x, 4) && m.Write(player + 0x9C, &z, 4);
}
}  // namespace link_runtime::parry::pushback

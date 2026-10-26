#pragma once
// Game time in 60 Hz frames, from the native deltaTime (docs/fps-coexistence-2026-10-12.md §3).
// Pure: no process access. The wiring reads the counter and deltaTime on the game thread.
//
// Native facts (bio4.exe 1.1.0, SHA 19aed4af...; re4_tweaks/re4_fps source):
//  - GLOBAL_WK (*0x806F3C, our global_slot) +0x70 is deltaTime_70 in 1/30 s units: 0.5 at
//    60 FPS, 30/165 at 165. re4_fps runs one fixed logic tick per presented frame; a PC that
//    cannot hold the target slows the game (deltaTime only changes with UseDynamicFrametime).
//  - +0x530C frameCounter_530C adds 1 per logic tick.
//  - variableframerate (config.ini) is the int at image RVA 0x82B7A0.
// Our frame-counted timers used ticks; at 165 they ran 2.75x short. They now count Elapsed60:
// at 60 FPS every tick adds exactly 1.0, so the clock equals the raw counter and every legacy
// integer comparison gives the same result (tests/native/test_game_clock.cpp).
#include <cmath>
#include <cstdint>

namespace link_runtime::game_clock {
constexpr uint32_t kDeltaTimeOffset = 0x70;      // GLOBAL_WK float deltaTime_70
constexpr uint32_t kFrameCounterOffset = 0x530C; // GLOBAL_WK uint32 frameCounter_530C
constexpr uint32_t kVariableFrameRateRva = 0x82B7A0;
constexpr float kDefaultDt = 0.5f, kMinDt = 0.03f, kMaxDt = 2.0f;
// 30/165 = 2/11 is periodic: sums are compared with this tolerance (60-frame units).
constexpr double kEpsilon = 1e-3;

inline float ValidDt(float raw) { return std::isfinite(raw) && raw >= kMinDt && raw <= kMaxDt ? raw : kDefaultDt; }
// One tick's length in 60 Hz frames (exactly 1.0 at 60 FPS).
inline double Step60(float dt) { return double(ValidDt(dt)) * 2.0; }
inline bool Reached(double elapsed, double limit) { return elapsed + kEpsilon >= limit; }
inline bool Exceeded(double elapsed, double limit) { return elapsed > limit + kEpsilon; }

// Game-thread clock. Observe() is called with the native counter wherever a timer needs "now";
// repeated reads in one tick do not advance it. The first observation anchors it to the counter,
// so at 60 FPS Elapsed60() == counter (as the legacy code compared).
class Clock {
    double elapsed_ = 0.0, step_ = 1.0;
    uint32_t frame_ = 0;
    bool known_ = false;
public:
    void Advance(float dt) { step_ = Step60(dt); elapsed_ += step_; }
    double Elapsed60() const { return elapsed_; }
    double LastStep() const { return step_; }
    bool Known() const { return known_; }
    double Observe(uint32_t frame, float dt) {
        step_ = Step60(dt);
        if (!known_) { known_ = true; frame_ = frame; elapsed_ = double(frame); return elapsed_; }
        const int32_t delta = static_cast<int32_t>(frame - frame_);  // wraps like the counter
        frame_ = frame;
        elapsed_ += double(delta) * step_;
        return elapsed_;
    }
};

// FpsProbe arithmetic (worker). Observe() on every poll; Sample() every few seconds.
struct RateSample {
    bool valid = false;
    uint32_t ticks = 0, invalid_dt = 0, resets = 0;
    double seconds = 0, ticks_per_s = 0, speed = 0;  // speed: game seconds per real second (1.00 = real time)
    float dt = 0;
};
class Rate {
    uint32_t frame_ = 0, ticks_ = 0, invalid_ = 0, resets_ = 0;
    double game_s_ = 0;
    uint64_t start_ms_ = 0, seen_ms_ = 0;
    float dt_ = 0;
    bool known_ = false, anchored_ = false;
public:
    // A real tick rate is never above kMaxTicksPerMs (2000 FPS) plus a small slack. Loading a save
    // rewrites the counter (12-oct: it went back, then jumped forward by 64675 within one 5 ms
    // poll and printed 12924.7 ticks/s): such a jump re-anchors instead of being counted.
    static constexpr uint64_t kMaxTicksPerMs = 2, kSlackTicks = 16;
    // `now_ms` 0 = unknown (no time bound, legacy callers/tests).
    void Observe(uint32_t frame, float raw_dt, uint64_t now_ms = 0) {
        const uint64_t elapsed = now_ms && seen_ms_ && now_ms >= seen_ms_ ? now_ms - seen_ms_ : 0;
        const bool timed = now_ms && seen_ms_ && now_ms >= seen_ms_;
        if (now_ms) seen_ms_ = now_ms;
        if (!known_) { known_ = true; frame_ = frame; return; }
        const int32_t delta = static_cast<int32_t>(frame - frame_);
        frame_ = frame;
        if (delta < 0 || delta > 100000 || (timed && uint64_t(delta) > elapsed * kMaxTicksPerMs + kSlackTicks)) {
            ++resets_; return;  // load/reset: re-anchor
        }
        if (!delta) return;
        ticks_ += uint32_t(delta);
        if (std::isfinite(raw_dt) && raw_dt >= kMinDt && raw_dt <= kMaxDt) { game_s_ += double(delta) * raw_dt / 30.0; dt_ = raw_dt; }
        else invalid_ += uint32_t(delta);
    }
    RateSample Sample(uint64_t now_ms) {
        RateSample s;
        if (!anchored_ || now_ms <= start_ms_) {
            if (!anchored_) { anchored_ = true; start_ms_ = now_ms; ticks_ = invalid_ = resets_ = 0; game_s_ = 0; }
            return s;
        }
        s.valid = true; s.ticks = ticks_; s.invalid_dt = invalid_; s.resets = resets_; s.dt = dt_;
        s.seconds = double(now_ms - start_ms_) / 1000.0;
        s.ticks_per_s = double(ticks_) / s.seconds;
        s.speed = game_s_ / s.seconds;
        start_ms_ = now_ms; ticks_ = invalid_ = resets_ = 0; game_s_ = 0;
        return s;
    }
};
}  // namespace link_runtime::game_clock

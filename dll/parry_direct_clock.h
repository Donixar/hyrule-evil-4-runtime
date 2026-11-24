#pragma once
#include "game_clock.h"
#include "case_key_debounce.h"

namespace link_runtime::parry::direct {
// Enemy contacts do not run in Options/the case, but the global counter advances there.
// PadRead runs on its own native thread even in menus. Enemy contact workers only read.
struct FollowFrame {
    uint32_t global = 0, frame = 0;
    uint16_t room = 0;
    uint8_t routine = 0;
    uint32_t status[4]{};
    float dt = 0;
};
inline constexpr double kUnknownFollowTime = -INFINITY;
struct FollowTime {
    double game = kUnknownFollowTime;
    uint64_t generation = 0, menu = 0;
    uint32_t pad_frame = 0;
};
inline bool FollowFrameValid(const FollowFrame& f) {
    namespace ck = case_key_debounce;
    return f.global && (f.routine == 3 || f.routine == 6) &&
           !(f.status[2] & ck::layout::kStaTitle) && !(f.status[3] & ck::layout::kCardTask) &&
           std::isfinite(f.dt) && f.dt >= game_clock::kMinDt && f.dt <= game_clock::kMaxDt;
}
inline bool FollowHeld(const FollowFrame& f) {
    return f.routine == 6 || (f.status[0] & case_key_debounce::layout::kStaSubScrn) != 0;
}
class FollowClock {
    bool known_ = false, held_ = false;
    uint32_t global_ = 0, frame_ = 0;
    uint16_t room_ = 0;
    double time_ = 0, step_ = 0;
    uint64_t generation_ = 0, menu_ = 0;
public:
    // Read before changing the clock to stamp the transition in a diagnostic.
    bool Held() const {return known_ && held_;}
    bool Available() const {return known_;}
    double Time() const {return time_;}
    bool Observe(const FollowFrame& f) {
        if (!FollowFrameValid(f)) {
            const bool retire = known_;
            known_ = false;
            return retire;
        }
        const bool held = FollowHeld(f);
        const bool reset = !known_ || f.global != global_ || f.room != room_ ||
                           f.frame < frame_ || f.frame - frame_ > 1;
        if (reset) {++generation_; menu_ = 0; time_ = 0;}
        // PadRead precedes the native counter increment. Its state/delta describe the
        // interval that starts here. Commit the last announced interval exactly once.
        else if (f.frame != frame_ && !held_) time_ += step_;
        if (held && (reset || !held_)) ++menu_;
        global_ = f.global;
        room_ = f.room;
        frame_ = f.frame;
        held_ = held;
        step_ = game_clock::Step60(f.dt);
        known_ = true;
        return reset;
    }
    FollowTime AtContact(const FollowFrame& live) const {
        if (!known_ || !FollowFrameValid(live) || FollowHeld(live) ||
            live.global != global_ || live.room != room_ || live.frame < frame_) return {};
        const uint32_t ahead = live.frame - frame_;
        // PadRead precedes the native +1 counter increment. A live playable frame may
        // be exactly one ahead. Use the interval already announced by the publisher.
        // Same-frame disagreement at a menu edge is ambiguous until the next PadRead.
        if (ahead > 1 || (ahead == 0 && held_)) return {};
        return {time_ + (ahead && !held_ ? step_ : 0.0), generation_, menu_, frame_};
    }
};
#if defined(_MSC_VER) && defined(_M_IX86)
class SharedFollowClock {
    SRWLOCK lock_ = SRWLOCK_INIT;
    FollowClock clock_;
public:
    void Publish(const FollowFrame& frame) {
        AcquireSRWLockExclusive(&lock_);
        clock_.Observe(frame);
        ReleaseSRWLockExclusive(&lock_);
    }
    FollowTime AtContact(const FollowFrame& live) {
        AcquireSRWLockShared(&lock_);
        const FollowTime time = clock_.AtContact(live);
        ReleaseSRWLockShared(&lock_);
        return time;
    }
};
#endif
template<class Memory> bool ReadFollowFrame(Memory& m, uint32_t base, FollowFrame& f) {
    namespace ck = case_key_debounce;
    uint32_t g = 0, before = 0, after_global = 0;
    if (!m.Read(base + ck::rva::kGlobalSlot, &g, 4) || !g ||
        !m.Read(g + game_clock::kFrameCounterOffset, &before, 4) ||
        !m.Read(g + 0x20, &f.routine, 1) || !m.Read(g + 0x4FAC, &f.room, 2) ||
        !m.Read(g + ck::layout::kStatus0, f.status, sizeof(f.status)) ||
        !m.Read(g + game_clock::kDeltaTimeOffset, &f.dt, 4) ||
        !m.Read(g + game_clock::kFrameCounterOffset, &f.frame, 4) || before != f.frame ||
        !m.Read(base + ck::rva::kGlobalSlot, &after_global, 4) || g != after_global) return false;
    f.global = g;
    return true;
}
} // namespace link_runtime::parry::direct

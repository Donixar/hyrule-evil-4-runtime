#pragma once
#include <cstdint>
#include <limits>

namespace link_runtime::mortal_draw {
enum class Phase { Idle, Ready, Windup, Recovery };
enum class Event { Tick, Press, StrikeFrame, AnimationEnd, Interrupt };
enum class Effect { None, Begin, OrdinaryStrike, LethalStrike, Cancel, Finish };
struct Snapshot {
    // Opaque identities must include actor/scene generation, not only an address.
    uint64_t scene = 0, target = 0;
    bool stable = false, gameplay = false, player_free = false, sword_sheathed = false;
    bool native_action = false, common_alive_active = false, standing = false;
    bool no_exposed_plaga = false, unaware = false, rear_close_clear = false;
    bool compatible_ground = false, actual_contact = false;
};
struct State {
    Phase phase = Phase::Idle;
    uint64_t scene = 0, target = 0, token = 0; // Session counter, preserved on clear.
    bool strike_consumed = false;
};
struct Decision {
    Effect effect = Effect::None;
    bool show_prompt = false;
    uint64_t target = 0, token = 0;
};
inline bool Eligible(const Snapshot& s) {
    return s.scene && s.target && s.stable && s.gameplay && s.player_free &&
        s.sword_sheathed && !s.native_action && s.common_alive_active && s.standing &&
        s.no_exposed_plaga && s.unaware && s.rear_close_clear && s.compatible_ground;
}
inline Decision Step(State& state, const Snapshot& s, Event event, uint64_t event_token) {
    const auto clear = [&state] {
        state.phase = Phase::Idle;
        state.scene = state.target = 0;
        state.strike_consumed = false;
    };
    const bool active = state.phase == Phase::Windup || state.phase == Phase::Recovery;
    // Entry-only player/equipment flags change because this animation occupies the
    // player and draws the sword. Surprise and contact are checked at impact.
    if (active && (event == Event::Interrupt || !s.stable || !s.gameplay ||
        !s.scene || !s.target || state.scene != s.scene || state.target != s.target ||
        s.native_action || !s.common_alive_active || !s.standing || !s.no_exposed_plaga)) {
        const Decision cancelled{Effect::Cancel, false, state.target, state.token};
        clear();
        return cancelled;
    }
    switch (state.phase) {
    case Phase::Idle:
    case Phase::Ready:
        if (event != Event::Tick && event != Event::Press) return {};
        if (!Eligible(s) || state.token == (std::numeric_limits<uint64_t>::max)()) {
            clear(); return {};
        }
        if (event == Event::Tick) {
            state.phase = Phase::Ready;
            state.scene = s.scene; state.target = s.target;
            return {Effect::None, true, s.target, 0};
        }
        if (state.phase != Phase::Ready) return {};
        if (state.scene != s.scene || state.target != s.target) { clear(); return {}; }
        ++state.token;
        state.phase = Phase::Windup;
        state.strike_consumed = false;
        return {Effect::Begin, false, state.target, state.token};
    case Phase::Windup:
        switch (event) {
        case Event::StrikeFrame:
            if (!event_token || event_token != state.token || state.strike_consumed) return {};
            state.strike_consumed = true;
            state.phase = Phase::Recovery;
            if (!s.actual_contact || !s.rear_close_clear || !s.compatible_ground) return {};
            return {s.unaware ? Effect::LethalStrike : Effect::OrdinaryStrike,
                    false, state.target, state.token};
        case Event::Tick:
        case Event::Press:
        case Event::AnimationEnd:
        case Event::Interrupt:
            // End without a strike marker cannot fabricate damage. The adapter
            // must emit Interrupt when an animation actually ended without one.
            return {};
        }
        return {};
    case Phase::Recovery:
        if (event == Event::AnimationEnd && event_token && event_token == state.token) {
            const Decision finished{Effect::Finish, false, state.target, state.token};
            clear();
            return finished;
        }
        return {};
    }
    return {};
}
}

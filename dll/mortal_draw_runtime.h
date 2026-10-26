#pragma once
// Contextual Mortal Draw controller: the glue between the arbiter sample
// (mortal_draw_context.h), the target selection (mortal_draw_target.h), the
// action policy (mortal_draw_policy.h), the player dispatch start and the shared
// native-hit adapter (mortal_draw_damage.h). Pure: memory reads, hooks and the
// native start live in link_runtime.cpp; this class only decides.
//
// Frame flow (all on the game thread; "frames" are 60 Hz frames of game time):
//  1. Arbiter exit (0x18AB40): Arbiter() steps the one-press trigger with the
//     pre+post sample (N-3) and the current selection. With Contextual on, a
//     press becomes a start Request and the caller masks KEY_A (MaskPress).
//     Observe-only never requests and never masks.
//  2. Player handler (dispatch decide): TakeRequest() -> the caller revalidates
//     the target and builds a Snapshot -> Admit() runs the policy Tick+Press ->
//     native start (TryStartClip from normal idle) -> Started() or StartFailed().
//  3. Every player frame while active: StrikeDue() -> RevalidateStrike (strike
//     tolerance + the start's PressLatch, see Latch()) -> Strike()
//     (policy StrikeFrame; with Damage on: Issue + NativeHit, Lethal falling back
//     to Ordinary with the same token on OverCap). Settle() reads the outcome.
//  4. Lease end (dispatch lifecycle): Ended() -> AnimationEnd or Interrupt; an
//     interrupt withdraws our override (adapter.Cancel): a published record then
//     lands as an ordinary native knife hit, never a late kill.
#include "mortal_draw_context.h"
#include "mortal_draw_damage.h"
#include "mortal_draw_policy.h"
#include "mortal_draw_target.h"
#include "game_clock.h"
#include <cstdint>

namespace link_runtime::mortal_draw::runtime {
struct Settings {
    bool contextual = false;     // MortalDrawContextual: requests a start (and masks the press)
    bool damage = false;         // MortalDrawDamage: the cut publishes through the adapter
    // Game time in 60 Hz frames (game_clock.h): `now` below is Elapsed60, which equals the raw
    // logic counter at 60 FPS; at 165 it advances 2*deltaTime per tick (docs/fps-coexistence).
    unsigned strike_tick = 16;   // 60 Hz frames from the start to the cut
    unsigned request_frames = 15;  // a request waits this long for normal idle, then expires
};
// Why the prompt is unavailable besides the trigger's own blocks (log).
enum class Gate : uint8_t { None, Ocarina, Reaction, Busy, Active };
inline const char* GateName(Gate g) {
    static const char* const n[] = {"none", "ocarina", "parry_reaction", "dispatch_busy", "mortal_active"};
    return n[unsigned(g)];
}
inline const char* BlockName(context::Block b) {
    static const char* const n[] = {"none", "unreadable", "wrong_sample_point", "stale_frame", "no_target", "native_offer",
                                    "actions_disabled", "player_busy", "sword_drawn", "gun_raised"};
    return n[unsigned(b)];
}
inline const char* RejectName(target::Reject r) {
    static const char* const n[] = {"none", "no_scene", "bad_manager", "unreadable", "unstable", "invalid_slot", "not_em10",
                                    "id_not_allowed", "type_excluded", "dead", "inactive", "not_move_routine", "armored",
                                    "chainsaw", "plaga_exposed", "found", "pending_damage", "forced_find", "not_idle_routine",
                                    "too_close", "too_far", "vertical_gap", "not_rear", "no_line_check", "line_blocked",
                                    "not_finite", "attacking"};
    return n[unsigned(r)];
}
inline const char* EffectName(Effect e) {
    static const char* const n[] = {"none", "begin", "ordinary", "lethal", "cancel", "finish"};
    return n[unsigned(e)];
}
inline const char* StatusName(damage::Status s) {
    static const char* const n[] = {"published", "disabled", "bad_request", "token_used", "busy", "identity_lost", "dead",
                                    "pending_native", "no_hitpart", "over_cap", "publish_failed"};
    return n[unsigned(s)];
}
inline const char* OutcomeName(damage::Outcome o) {
    static const char* const n[] = {"none", "pending", "applied", "cancelled", "expired", "consumed_natively", "superseded",
                                    "lost", "passthrough"};
    return n[unsigned(o)];
}
// Snapshot for the policy from a revalidation plus the player-side facts.
// actual_contact: no blade contact is attributed; in reach (rear_close_clear) stands in.
struct PlayerFacts { bool stable = false, gameplay = false, player_free = false, sword_sheathed = false; };
inline Snapshot MakeSnapshot(const target::Selection& s, const target::Recheck& v, const target::Tracker& t, const PlayerFacts& p) {
    Snapshot out;
    target::Apply(s, v, t, out);
    out.stable = p.stable; out.gameplay = p.gameplay; out.player_free = p.player_free; out.sword_sheathed = p.sword_sheathed;
    out.native_action = false; out.compatible_ground = true;
    out.actual_contact = out.rear_close_clear;
    return out;
}
struct Request { uint64_t frame = 0; double time = 0; target::Selection sel; };
struct Stats {
    unsigned presses = 0, requests = 0, expired = 0, refused = 0, starts = 0, start_failed = 0;
    unsigned strikes = 0, lethal = 0, ordinary = 0, no_effect = 0, published = 0, applied = 0, unconfirmed = 0;
    unsigned finished = 0, interrupted = 0, early_end = 0;
    damage::Status last_status = damage::Status::Disabled;
    damage::Outcome last_outcome = damage::Outcome::None;
    Effect last_effect = Effect::None;
    uint64_t last_token = 0;
};
class Controller {
public:
    explicit Controller(Settings s) : s_(s) {}
    const Settings& settings() const { return s_; }
    const Stats& stats() const { return stats_; }
    bool Active() const { return act_.active; }
    bool Requested() const { return request_; }
    uint64_t Lease() const { return act_.active ? act_.lease : 0; }
    uint64_t HitToken() const { return stats_.last_token; }  // last adapter token of a cut
    const target::Selection& Target() const { return act_.sel; }
    const target::PressLatch& Latch() const { return act_.latch; }

    // Arbiter exit. `sample` carries both halves (pre_ok/after_arbiter); `gate`
    // closes the prompt for reasons the trigger does not see.
    context::Decision Arbiter(const context::Sample& sample, const target::Selection& sel, Gate gate) {
        return Arbiter(sample, sel, gate, double(sample.frame));
    }
    // `now`: game time (Elapsed60) of this sample.
    context::Decision Arbiter(const context::Sample& sample, const target::Selection& sel, Gate gate, double now) {
        const bool ready = sel.found && gate == Gate::None && !act_.active && !request_;
        context::Decision d = trigger_.Step(sample, ready, now);
        if (act_.active || request_) { trigger_.DropBuffer(); d.buffered = false; }
        if (d.press) {
            ++stats_.presses;
            if (s_.contextual) { request_ = true; req_ = Request{sample.frame, now, sel}; ++stats_.requests; }
            else d.mask_trg = false;  // observe-only: the press stays with the game
        }
        return d;
    }
    // Player handler: the pending request, while still fresh.
    bool TakeRequest(double now, Request& out) {
        if (!request_) return false;
        if (game_clock::Exceeded(now - req_.time, double(s_.request_frames)) || now < req_.time - game_clock::kEpsilon) {
            request_ = false; ++stats_.expired; return false;
        }
        out = req_; return true;
    }
    void DropRequest() { if (request_) { request_ = false; ++stats_.refused; } }
    // Policy admission right before the native start (Tick then Press -> Begin).
    bool Admit(const Snapshot& snap) {
        if (act_.active) return false;
        Step(policy_, snap, Event::Tick, 0);
        const Decision d = Step(policy_, snap, Event::Press, 0);
        if (d.effect != Effect::Begin) { request_ = false; ++stats_.refused; return false; }
        return true;
    }
    // `latch`: target::Latch of the start revalidation, used by RevalidateStrike at the cut.
    void Started(uint64_t lease, double now, const target::Selection& sel, const target::PressLatch& latch = {}) {
        request_ = false; ++stats_.starts;
        act_ = Action{}; act_.active = true; act_.lease = lease; act_.start_time = now; act_.sel = sel; act_.latch = latch;
    }
    void StartFailed() { request_ = false; ++stats_.start_failed; Clear(); }
    bool StrikeDue(double now) const {
        return act_.active && !act_.struck && game_clock::Reached(now - act_.start_time, double(s_.strike_tick));
    }
    // The cut, once per action. `target` is filled by the caller (identity,
    // actor, from); its facts callback decides same_identity. `lethal_cap`:
    // target::LethalCap of the revalidated target (0 = none), the HP limit a
    // lethal cut on a heavy target keeps (16-oct); an ordinary cut ignores it.
    Effect Strike(const Snapshot& snap, damage::Adapter& adapter, const damage::HitTarget& target, int lethal_cap = 0) {
        if (!act_.active || act_.struck) return Effect::None;
        act_.struck = true; ++stats_.strikes;
        const Decision d = Step(policy_, snap, Event::StrikeFrame, policy_.token);
        stats_.last_effect = d.effect;
        if (d.effect == Effect::LethalStrike) ++stats_.lethal;
        else if (d.effect == Effect::OrdinaryStrike) ++stats_.ordinary;
        else ++stats_.no_effect;
        if (!s_.damage || (d.effect != Effect::LethalStrike && d.effect != Effect::OrdinaryStrike)) return d.effect;
        if (!act_.hit) act_.hit = adapter.Issue();
        stats_.last_token = act_.hit;
        const damage::Kind kind = damage::KindFor(d.effect);
        damage::Status st = adapter.NativeHit(target, kind == damage::Kind::Lethal ? lethal_cap : 0, kind, act_.hit);
        if (st == damage::Status::OverCap) st = adapter.NativeHit(target, 0, damage::Kind::Ordinary, act_.hit);
        stats_.last_status = st;
        if (st == damage::Status::Published) { ++stats_.published; act_.published = true; }
        return d.effect;
    }
    // Once per frame: confirms the published hit (Applied or anything else).
    void Settle(damage::Adapter& adapter) {
        if (!act_.published || act_.settled) return;
        const damage::Outcome o = adapter.OutcomeOf(act_.hit);
        if (o == damage::Outcome::Pending) return;
        act_.settled = true; stats_.last_outcome = o;
        if (o == damage::Outcome::Applied) ++stats_.applied; else ++stats_.unconfirmed;
    }
    // Our lease ended. finished=true: the clip reached its end (AnimationEnd);
    // otherwise an interrupt (damage, grab, event, F8/cancel): our override is withdrawn.
    void Ended(uint64_t lease, bool finished, damage::Adapter& adapter) {
        if (!act_.active || lease != act_.lease) return;
        if (finished && act_.struck) { ++stats_.finished; Step(policy_, Snapshot{}, Event::AnimationEnd, policy_.token); }
        else {
            if (finished) ++stats_.early_end; else ++stats_.interrupted;
            if (act_.published && !act_.settled) adapter.Cancel(act_.hit);
        }
        Settle(adapter);
        const Action keep = act_;
        Clear();
        // A published hit stays tracked until it settles (Settle() is driven by the caller).
        if (keep.published && !keep.settled) { act_.hit = keep.hit; act_.published = true; }
    }

private:
    struct Action {
        bool active = false, struck = false, published = false, settled = false;
        uint64_t lease = 0, hit = 0;
        double start_time = 0;
        target::Selection sel;
        target::PressLatch latch;
    };
    void Clear() {
        // Ends the policy action whatever its phase, keeping its token counter.
        policy_.phase = Phase::Idle; policy_.scene = policy_.target = 0; policy_.strike_consumed = false;
        act_ = Action{};
    }
    Settings s_;
    context::Trigger trigger_;
    State policy_;
    bool request_ = false;
    Request req_;
    Action act_;
    Stats stats_;
};
}  // namespace link_runtime::mortal_draw::runtime

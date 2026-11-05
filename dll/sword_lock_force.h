#pragma once
// Master Sword Blade Lock H1c, the lab force trigger: [Combat] SwordLockLabForce / SwordLockLabForceMm. Lab DLL only:
// link_runtime.cpp includes this header inside #if LINK_SWORD_LOCK_LAB (as sword_lock_icons.h), so the public build never
// sees it and stays byte-identical. Pure: no hooks, no process access; link_runtime.cpp SwkForce reads the game and queues
// the probe's request. Tests: tests/native/test_sword_lock_lab.cpp (ForceTest).
#include "sword_lock_lab.h"
#include "game_clock.h"
#include <cmath>
#include <cstdint>

namespace link_runtime::sword_lock::lab {
// [Combat] SwordLockLabForce (0/1, missing = 0) and SwordLockLabForceMm (500..10000, missing = 2500): a bot test of what
// comes after the parry (the mask, the offers and icons with the world frozen, the exits, the slide) without waiting for
// a level-2 block of the chainsaw (3-nov: Dr. Salvador swung 1-2 times every 15 minutes and the bot never got one).
// Read only by the lab DLL (LINK_SWORD_LOCK_LAB=1, with the duel's keys); never in a layer or a shipped INI. With it on,
// on the player's core frame, the live chainsaw owner (em10 id 0x15 with +0x7F0 == 4, the owner ChainsawOwner resolves)
// within ForceMm of Link (XZ, at most kForceMaxDyMm apart in height) and in front of him (parry_general_block.h
// Parryable: within 120 degrees of his forward) queues the probe's request as the tier-2 block would, when the
// admission would take it; the request then goes through the same Admit, freeze, lease and duel. Once per cooldown:
// max(SwordLockCooldownMs, kForceRetryMs) of game time after a forced request; kForceRetryMs after a crowd. A forced duel
// has no blocked swing to arm again: the no_swing rule is waived for it alone (its start line says force=1 follow=0).
inline constexpr double kDefaultForceMm = 2500, kMinForceMm = 500, kMaxForceMm = 10000;
inline constexpr double kForceRetryMs = 2000, kForceMaxDyMm = 2000;
struct ForceConfig {bool on = false; double mm = kDefaultForceMm;};
template <class Get> ForceConfig ReadForceConfig(Get&& get, unsigned& problems) {
    ForceConfig c; wchar_t v[128]{};
    get(L"SwordLockLabForce", v, 128);
    if (!detail::Switch(v, c.on)) ++problems;
    v[0] = 0; get(L"SwordLockLabForceMm", v, 128);
    if (v[0] && !detail::Number(v, kMinForceMm, kMaxForceMm, c.mm)) {++problems; c.mm = kDefaultForceMm;}
    return c;
}
// Why the trigger did not fire on this frame (Fire: it queues now). In the order the runtime reads them: the cheap
// facts first (Gate), then the owner, then the crowd.
enum class ForceWhy : uint8_t {Off, Busy, Wait, Ocarina, Gameplay, LinkBusy, NoOwner, Cooldown, Far, Behind, Crowd, Fire};
inline const char* ForceWhyName(ForceWhy w) {
    switch (w) {
    case ForceWhy::Busy: return "busy"; case ForceWhy::Wait: return "wait"; case ForceWhy::Ocarina: return "ocarina";
    case ForceWhy::Gameplay: return "gameplay"; case ForceWhy::LinkBusy: return "link_busy"; case ForceWhy::NoOwner: return "no_owner";
    case ForceWhy::Cooldown: return "cooldown"; case ForceWhy::Far: return "far"; case ForceWhy::Behind: return "behind";
    case ForceWhy::Crowd: return "crowd"; case ForceWhy::Fire: return "fire"; default: return "off";
    }
}
struct ForceFacts {
    bool free = false;        // the probe Free (nothing queued, held or watched) and the duel Off or Done
    bool ocarina = false;     // the probe's Gate::ocarina
    bool gameplay = false;    // the probe's Gate::gameplay (ReadHealth eligible)
    bool link_free = false;   // Link in a routine the hold lease starts from (knife hold, normal idle) and no lease of the probe
    bool owner = false;       // a live chainsaw owner was found
    bool cooling = false;     // the probe's own cooldown of that owner (Probe::Cooling)
    double dist_mm = -1, dy_mm = 0;
    bool front = false;       // Parryable with the owner's position as the sample
    bool crowd = false;       // the lab's crowd rule (SwkCrowd), read last
};
class Force {
public:
    void Configure(const ForceConfig& c, double cooldown_ms) {cfg_ = c; cooldown_ms_ = cooldown_ms; at_ = -1; wait_ms_ = 0;}
    const ForceConfig& config() const {return cfg_;}
    double cooldown_ms() const {return cooldown_ms_ > kForceRetryMs ? cooldown_ms_ : kForceRetryMs;}
    // A clock that went back (a load) clears the wait.
    bool Waiting(double now) const {
        return at_ >= 0 && now + game_clock::kEpsilon >= at_ && !game_clock::Reached(now - at_, Frames(wait_ms_));
    }
    ForceWhy Gate(const ForceFacts& f, double now) const {
        if (!cfg_.on) return ForceWhy::Off;
        if (!f.free) return ForceWhy::Busy;
        if (Waiting(now)) return ForceWhy::Wait;
        if (f.ocarina) return ForceWhy::Ocarina;
        if (!f.gameplay) return ForceWhy::Gameplay;
        if (!f.link_free) return ForceWhy::LinkBusy;
        return ForceWhy::Fire;
    }
    ForceWhy Judge(const ForceFacts& f, double now) const {
        const ForceWhy g = Gate(f, now);
        if (g != ForceWhy::Fire) return g;
        if (!f.owner) return ForceWhy::NoOwner;
        if (f.cooling) return ForceWhy::Cooldown;
        if (!(f.dist_mm >= 0) || f.dist_mm > cfg_.mm || !(std::fabs(f.dy_mm) <= kForceMaxDyMm)) return ForceWhy::Far;
        if (!f.front) return ForceWhy::Behind;
        if (f.crowd) return ForceWhy::Crowd;
        return ForceWhy::Fire;
    }
    void Fired(double now) {at_ = now; wait_ms_ = cooldown_ms(); ++fired_;}
    void Crowded(double now) {at_ = now; wait_ms_ = kForceRetryMs; ++crowded_;}   // the crowd may move: asked again soon
    unsigned fired() const {return fired_;}
    unsigned crowded() const {return crowded_;}
private:
    ForceConfig cfg_;
    double cooldown_ms_ = kDefaultCooldownMs, at_ = -1, wait_ms_ = 0;
    unsigned fired_ = 0, crowded_ = 0;
};
}  // namespace link_runtime::sword_lock::lab

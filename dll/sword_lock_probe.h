#pragma once
// Master Sword Blade Lock, experiment H0 (docs/idea-ambiciosa-2026-11-03.md, "Experimento inicial" and "Experimento
// tecnico de A-Z1"). Lab DLL only: [Combat] SwordLockProbe=1, never published. It measures the round trip of the lock,
// without clips or effects:
//  1. a tier-2 block of the chainsaw (parry_general_block.h Chainsaw, owner from the runtime's Owner(): em10AtkCk EDI or
//     em10CsawHitCk ESI) only QUEUES a request (owner, serial) inside the EmAtkHitCk resolver: nothing is written there;
//  2. on the player's next decide frame the runtime begins its own ocarina::WorldFreeze (Stop_flg SPF_EM|SPF_SUBCHAR,
//     ocarina_freeze.h) and then leases the dispatch slot (mortal_draw_dispatch.h) with a resident hold clip from the
//     knife hold (StartFrom::KnifeHoldTrial) or idle (NormalIdle); a refused Begin is logged and the parry goes on as today;
//  3. after kHoldMs of game time it publishes the native flash record 0x17 on the owner through 0x1AEEF0 (variant B,
//     SwordLockProbeExit=kick: the kick 0x14), then ends the freeze, then hands Link back;
//  4. it samples the owner (r0/r1, Seq_frame, HP), Link (HP, routine, 2A8), Stop_flg at -1, 0, 1000, 2000, 2016 and 3000 ms.
// Safety rules: released on the SAME frame on a QTE (Stop_flg SPF_ACTBTN), a grab or hit (Link's R0 neither 0 nor our
// lease's 5), a cutscene, a room load (identity, counter gap or the freeze's bits gone), Link's or the owner's death;
// one lock per owner every kCooldownMs; never with the ocarina out. Stop_flg goes back through the freeze's own bit
// ownership (End clears only the bits Begin set).
//
// This header is the pure part: the state machine Idle -> Queued -> Frozen -> Releasing -> Done/Abandoned, its game
// clock timers (60 Hz frames from game_clock.h: the same at 60 and 165 FPS), the cooldown and the refusal/abandon rules,
// over injected snapshots. No process access, hooks or native calls (link_runtime.cpp does the reads and the writes).
// Tests: tests/native/test_sword_lock_probe.cpp (scripts/test_sword_lock_probe.bat).
//
// H1c lab duel ([Combat] SwordLock=1 and SwordLockLab=1, sword_lock_lab.h) reuses this machine with options that keep
// the H0 behaviour when left at their defaults: Config::external_release (the duel, not kHoldMs, decides the release:
// RequestRelease), Config::cooldown_ms (SwordLockCooldownMs), Config::menu_abandons (false in the lab: a sub-screen
// request pauses the duel instead of abandoning at once, plan "Cableado" 9), Gate::crowd (Refuse::Crowd, the lab's 8 m
// rule) and Gate::no_swing (Refuse::NoSwing: no Follow to arm again at the release). Their tests:
// tests/native/test_sword_lock_lab.cpp.
#include "game_clock.h"
#include <cstdint>
#include <cwctype>

namespace link_runtime::sword_lock::probe {
enum class Exit : uint8_t {Flash, Kick};
inline const char* ExitName(Exit e) {return e == Exit::Kick ? "kick" : "flash";}
inline uint8_t ExitWeapon(Exit e) {return e == Exit::Kick ? 0x14 : 0x17;}

struct Config {
    bool enabled = false;      // [Combat] SwordLockProbe: missing or 0 = off (3.5.0 behaviour: nothing installed or run)
    Exit exit = Exit::Flash;   // [Combat] SwordLockProbeExit: flash (default) or kick
    // H1c lab duel only (never read from the INI here): the release waits for RequestRelease instead of kHoldMs, and the
    // per-owner cooldown length. The H0 defaults: false and kCooldownMs.
    bool external_release = false;
    double cooldown_ms = 15000;
    // H1c lab only: false = Status_flg[2] kSscrnRequest is not an abandon (the lab duel pauses on it). H0 default: true.
    bool menu_abandons = true;
};
namespace detail {
inline bool Same(const wchar_t* a, const wchar_t* b) {
    for (; *a && *b; ++a, ++b) if (std::towlower(*a) != *b) return false;
    return !*a && !*b;
}
}  // namespace detail
// get(key, out, count) fills `out` with the raw value ("" when missing). A lab key: any other value counts a problem and
// leaves the probe off (a typo never runs a half-configured experiment).
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[16]{};
    get(L"SwordLockProbe", v, 16);
    if (v[0] == L'1' && !v[1]) c.enabled = true;
    else if (v[0] && !(v[0] == L'0' && !v[1])) {++problems; return Config{};}
    v[0] = 0; get(L"SwordLockProbeExit", v, 16);
    if (!v[0] || detail::Same(v, L"flash")) c.exit = Exit::Flash;
    else if (detail::Same(v, L"kick")) c.exit = Exit::Kick;
    else {++problems; return Config{};}
    return c;
}

// Game time. Every timer is in 60 Hz frames of game_clock (1.0 per tick at 60 FPS, 60/165 at 165).
inline constexpr double kHoldMs = 2000, kAfterMs = 16, kWatchMs = 1000, kCooldownMs = 15000;
static_assert(Config{}.cooldown_ms == kCooldownMs && !Config{}.external_release && Config{}.menu_abandons, "the H0 defaults");
inline constexpr double kQueueMs = 250;      // a request older than this at its decide frame is stale (Link was busy)
inline constexpr double kLeaseWaitMs = 1000; // the lease is retried on decide frames this long after Begin, then given up
inline constexpr double Frames(double ms) {return ms * 0.06;}
inline constexpr double Ms(double frames) {return frames / 0.06;}
inline constexpr int32_t kMaxGapTicks = 30;   // native ticks between two probe ticks (parry_pushback.h): more = a load or a menu

// Sample labels, in ms of the lock: -1 right before Begin, 0 the Begin frame (after the lease try), 1000, 2000 the
// release frame (before the exit blow), 2016 the first frame >= 16 ms after the release, 3000 the end of the watch.
inline constexpr int kLabels[] = {-1, 0, 1000, 2000, 2016, 3000};
inline constexpr unsigned kLabelCount = 6;
inline constexpr unsigned Bit(unsigned index) {return 1U << index;}
enum Label : unsigned {kBefore = 0, kBegin, kMid, kRelease, kAfter, kEnd};

// Native words the rules read (bio4.exe 1.1.0; the same constants as ocarina_freeze.h / save_anywhere.h).
namespace native {
constexpr uint32_t kSpfActBtn = 0x100;                                   // Stop_flg: QTE / action-button lock
constexpr uint32_t kStaEvent = 0x00001000, kStaCut = 0x08000000, kStaMovie = 0x10000000;   // Status_flg[0]
constexpr uint32_t kStaDiedemo = 0x00100000;                             // Status_flg[0]: death demo
constexpr uint32_t kSscrnRequest = 0x04000000;                           // Status_flg[2]: inventory/map/shop
constexpr uint32_t kKnifeHold = 0x01010B00;                              // Link's steady knife hold (StartFrom::KnifeHoldTrial)
constexpr uint8_t kLeaseR0 = 5;                                          // dispatch lease: EventNormal
}  // namespace native

enum class State : uint8_t {Idle, Queued, Frozen, Releasing, Done, Abandoned};
inline const char* StateName(State s) {
    switch (s) {
    case State::Queued: return "queued"; case State::Frozen: return "frozen"; case State::Releasing: return "releasing";
    case State::Done: return "done"; case State::Abandoned: return "abandoned"; default: return "idle";
    }
}
// Why a request did not become a lock (the parry goes on as today).
enum class Refuse : uint8_t {None, Busy, NoOwner, Stale, Ocarina, Cooldown, Gameplay, Owner, Freeze, Crowd, NoSwing};
inline const char* RefuseName(Refuse r) {
    switch (r) {
    case Refuse::Busy: return "busy"; case Refuse::NoOwner: return "no_owner"; case Refuse::Stale: return "stale";
    case Refuse::Ocarina: return "ocarina"; case Refuse::Cooldown: return "cooldown"; case Refuse::Gameplay: return "gameplay";
    case Refuse::Owner: return "owner"; case Refuse::Freeze: return "freeze"; case Refuse::Crowd: return "crowd";
    case Refuse::NoSwing: return "no_swing"; default: return "none";
    }
}
// Why a held lock was let go before its time (released the same frame, no exit blow).
enum class Abandon : uint8_t {None, Unreadable, Load, Gap, LinkDead, Qte, Grab, Cutscene, Menu, OwnerDead, Ocarina, FreezeLost, LeaseLost};
inline const char* AbandonName(Abandon a) {
    switch (a) {
    case Abandon::Unreadable: return "unreadable"; case Abandon::Load: return "load"; case Abandon::Gap: return "gap";
    case Abandon::LinkDead: return "link_dead"; case Abandon::Qte: return "qte"; case Abandon::Grab: return "grab";
    case Abandon::Cutscene: return "cutscene"; case Abandon::Menu: return "menu"; case Abandon::OwnerDead: return "owner_dead";
    case Abandon::Ocarina: return "ocarina"; case Abandon::FreezeLost: return "freeze_lost"; case Abandon::LeaseLost: return "lease_lost";
    default: return "none";
    }
}
// Review 3-nov: an abandon in a world that goes on (same player, global and room; the owner frozen mid-swing with the
// parry's Follow long expired) still publishes the exit blow before the freeze End, or the chainsaw swing resumes at
// point blank. Not after a load, an unreadable frame or a death (no world to hit), nor inside a native event or QTE
// (the game drives that scene), nor freeze_lost (it can be a load zeroing Stop_flg before the room id changes). Those
// rely on the lab guard (Probe::Guarding, link_runtime.cpp SwordLockGuard); the H1c lab duel arms the parry's Follow
// again on the same swing instead (sword_lock_lab.h RearmOnAbandon).
inline bool AbandonFires(Abandon a) {
    return a == Abandon::Gap || a == Abandon::Grab || a == Abandon::Menu || a == Abandon::LeaseLost || a == Abandon::Ocarina;
}

// Admission facts on the decide frame after the block.
struct Gate {
    bool ocarina = false;      // the ocarina is out, or its freeze, lease or release work is still held
    bool gameplay = false;     // ReadHealth eligible: Link in play
    bool owner_alive = false;  // the queued owner reads as a live em10 Ganado
    bool crowd = false;        // H1c lab: another hostile Ganado or a weapon in flight too close (H0 never sets it)
    bool no_swing = false;     // H1c lab: the block's Follow has no owner routine to arm again at the release (H0: never)
};
// One probe tick (decide frame or the player's core frame). The runtime reads it on the game thread.
struct World {
    bool readable = false;
    uint32_t frame = 0;          // native frame counter (pG+0x530C)
    double now = 0;              // game time, 60 Hz frames (GameNow)
    uint32_t player = 0, global = 0, room = 0;
    int link_hp = 0;             // pl_life (pG+0x4FB4)
    uint32_t link_routine = 0;   // pl+0xFC
    uint32_t stop = 0, status0 = 0, status2 = 0;
    bool owner_alive = false;    // the admitted owner (same id/type/max HP), alive, not dying
    bool ocarina = false;
    bool lease = false;          // our dispatch lease owns Link this frame
    uint32_t freeze_owned = 0;   // the freeze's bits still owned (WorldFreeze::Owned after its Poll)
};
// What the runtime does on this tick, in this order: abandon; lease try; samples; release; first move; finish.
struct Action {
    Abandon abandon = Abandon::None;
    bool try_lease = false;      // decide frames only: lease the dispatch slot now
    bool lease_gave_up = false;  // the wait window ran out without a lease (once)
    unsigned samples = 0;        // Bit(Label) of the samples due now (kRelease is taken by the release itself)
    bool release = false;        // exit blow, then freeze End, then lease cancel
    bool first_move = false;     // first frame after the release frame: the owner's r0/r1 after its first move
    bool finished = false;       // the watch is over (kEnd sample), or it was cut by a load: the end line
};
struct Stats {unsigned queued = 0, busy = 0, no_owner = 0, refused = 0, locks = 0, released = 0, abandoned = 0;};

class Probe {
public:
    static constexpr unsigned kOwners = 8;   // cooldown table
    explicit Probe(Config c = {}) : cfg_(c) {}
    void Configure(const Config& c) {cfg_ = c;}
    const Config& config() const {return cfg_;}
    State state() const {return state_;}
    bool Free() const {return state_ == State::Idle || state_ == State::Done || state_ == State::Abandoned;}
    bool Holding() const {return state_ == State::Frozen;}   // the world is ours and the release has not run
    uint32_t owner() const {return owner_;}
    uint32_t serial() const {return serial_;}
    uint32_t owned() const {return owned_;}
    double start() const {return t0_;}
    double Elapsed(double now) const {return now - t0_;}               // frames since Begin
    double ElapsedMs(double now) const {return Ms(now - t0_);}
    double SinceReleaseMs(double now) const {return Ms(now - release_t_);}
    bool lease_started() const {return lease_started_;}
    double lease_ms() const {return lease_ms_;}
    unsigned lease_tries() const {return lease_tries_;}
    bool watch_cut() const {return watch_cut_;}
    int hp_release() const {return hp_release_;}
    int hp_min() const {return hp_min_;}
    bool taken(unsigned label) const {return (taken_ & Bit(label)) != 0;}
    Refuse last_refuse() const {return refuse_;}
    Abandon last_abandon() const {return abandon_;}
    const Stats& stats() const {return stats_;}

    // Resolver (game thread, inside the EmAtkHitCk hook): a press-blocked chainsaw swing with a known owner. Records
    // the request only; a second block while one is queued or held is ignored (one lock at a time).
    bool Queue(uint32_t owner, uint32_t serial, double now) {
        if (!cfg_.enabled) return false;
        if (!Free()) {++stats_.busy; return false;}
        if (!owner) {++stats_.no_owner; return false;}
        Clear(); state_ = State::Queued; owner_ = owner; serial_ = serial; queued_ = now; ++stats_.queued;
        return true;
    }
    // Decide frame: the request against the rules. None = the runtime samples -1, begins the freeze and calls Began.
    // Anything else drops the request (Idle) and the parry stays as it is.
    Refuse Admit(const Gate& g, double now) {
        if (state_ != State::Queued) return Refuse::Busy;
        Refuse r = Refuse::None;
        if (now + game_clock::kEpsilon < queued_ || game_clock::Exceeded(now - queued_, Frames(kQueueMs))) r = Refuse::Stale;
        else if (g.ocarina) r = Refuse::Ocarina;
        else if (Cooling(owner_, now)) r = Refuse::Cooldown;
        else if (!g.gameplay) r = Refuse::Gameplay;
        else if (!g.owner_alive) r = Refuse::Owner;
        else if (g.crowd) r = Refuse::Crowd;
        else if (g.no_swing) r = Refuse::NoSwing;
        if (r != Refuse::None) Drop(r);
        return r;
    }
    // The freeze's Begin result. frozen: FreezeResult::Frozen; owned: the bits it set. A refused Begin, or one that
    // owns no bit (the game already held both: nothing of ours to release), drops the request as Refuse::Freeze.
    bool Began(bool frozen, uint32_t owned, const World& w) {
        if (state_ != State::Queued) return false;
        if (!frozen || !owned || !w.readable) {Drop(Refuse::Freeze); return false;}
        state_ = State::Frozen; owned_ = owned; t0_ = w.now; frame_ = w.frame;
        player_ = w.player; global_ = w.global; room_ = w.room & 0xFFFF;
        taken_ = Bit(kBefore) | Bit(kBegin);   // the runtime logs both on this frame
        hp_release_ = hp_min_ = w.link_hp;
        Remember(owner_, w.now); ++stats_.locks;
        return true;
    }
    // Lease outcome on a decide frame (try_lease).
    void LeaseTried(bool started, double now) {
        ++lease_tries_;
        if (started && !lease_started_) {lease_started_ = true; lease_ms_ = Ms(now - t0_);}
    }
    // The dispatch reported our lease gone without our own cancel (Abandoned, or Finished: the hold did not loop).
    void LeaseLost() {if (state_ == State::Frozen && lease_started_) lease_lost_ = true;}
    // H1c lab (Config::external_release): the duel is over; the release runs on the next tick that may cancel the lease.
    void RequestRelease() {if (state_ == State::Frozen) release_due_ = true;}
    bool release_requested() const {return release_due_;}

    // Every probe tick while Frozen or Releasing. in_decide: the dispatch decide (the lease can start or be cancelled).
    Action Step(const World& w, bool in_decide) {
        Action a;
        if (state_ == State::Frozen) {
            a.abandon = Judge(w);
            if (a.abandon != Abandon::None) {
                abandon_ = a.abandon; state_ = State::Abandoned; abandon_t_ = w.readable ? w.now : -1; ++stats_.abandoned; return a;
            }
            frame_ = w.frame;
            const double t = w.now - t0_;
            if (!lease_started_ && !lease_given_up_) {
                if (game_clock::Reached(t, Frames(kLeaseWaitMs))) {lease_given_up_ = true; a.lease_gave_up = true;}
                else if (in_decide) a.try_lease = true;
            }
            if (!taken(kMid) && game_clock::Reached(t, Frames(1000))) {a.samples |= Bit(kMid); taken_ |= Bit(kMid);}
            // The release cancels our lease, which only the decide can do: with the lease held it waits for that frame.
            const bool due = cfg_.external_release ? release_due_ : game_clock::Reached(t, Frames(kHoldMs));
            if (due && (in_decide || !w.lease)) {
                a.release = true; taken_ |= Bit(kRelease);
                state_ = State::Releasing; release_t_ = w.now; release_frame_ = w.frame;
                hp_release_ = hp_min_ = w.link_hp; ++stats_.released;
            }
            return a;
        }
        if (state_ != State::Releasing) return a;
        // The watch only observes: a load, a gap or another player ends it early (watch_cut); nothing is released twice.
        if (!w.readable || w.player != player_ || w.global != global_ || (w.room & 0xFFFF) != room_ || Gap(w.frame)) {
            watch_cut_ = true; a.finished = true; state_ = State::Done; return a;
        }
        frame_ = w.frame;
        if (w.link_hp < hp_min_) hp_min_ = w.link_hp;
        if (!first_taken_ && w.frame != release_frame_) {first_taken_ = true; a.first_move = true;}
        const double since = w.now - release_t_;
        if (!taken(kAfter) && game_clock::Reached(since, Frames(kAfterMs))) {a.samples |= Bit(kAfter); taken_ |= Bit(kAfter);}
        if (game_clock::Reached(since, Frames(kWatchMs))) {a.samples |= Bit(kEnd); taken_ |= Bit(kEnd); a.finished = true; state_ = State::Done;}
        return a;
    }
    // The held lock's rules, in priority order (the first that holds is the reason logged).
    Abandon Judge(const World& w) const {
        using namespace native;
        if (!w.readable) return Abandon::Unreadable;
        if (w.player != player_ || w.global != global_ || (w.room & 0xFFFF) != room_) return Abandon::Load;
        if (Gap(w.frame)) return Abandon::Gap;
        if (w.link_hp <= 0 || (w.status0 & kStaDiedemo)) return Abandon::LinkDead;
        if (w.stop & kSpfActBtn) return Abandon::Qte;
        const uint8_t r0 = static_cast<uint8_t>(w.link_routine & 0xFF);
        if (r0 != 0 && r0 != kLeaseR0) return Abandon::Grab;
        if (w.status0 & (kStaEvent | kStaCut | kStaMovie)) return Abandon::Cutscene;
        if (lease_lost_ || (lease_started_ && !w.lease)) return Abandon::LeaseLost;
        if (r0 == kLeaseR0 && !w.lease) return Abandon::Cutscene;   // a native event owns Link (not our lease)
        if (cfg_.menu_abandons && (w.status2 & kSscrnRequest)) return Abandon::Menu;
        if (!w.owner_alive) return Abandon::OwnerDead;
        if (w.ocarina) return Abandon::Ocarina;
        if (w.freeze_owned != owned_) return Abandon::FreezeLost;   // game code cleared a bit, or a load zeroed Stop_flg
        return Abandon::None;
    }
    // Lab guard (review 3-nov): the owner's chainsaw contacts that no tier blocked are cancelled and logged while the
    // lock holds, during its watch, and for kWatchMs after an abandon (game time; a clock that went back ends it). A
    // FAIL in the live verdict instead of a decapitation; nothing written (the resolver's cancel return).
    bool Guarding(double now) const {
        if (state_ == State::Frozen || state_ == State::Releasing) return true;
        return state_ == State::Abandoned && abandon_t_ >= 0 && now + game_clock::kEpsilon >= abandon_t_ &&
               !game_clock::Reached(now - abandon_t_, Frames(kWatchMs));
    }
    bool Cooling(uint32_t owner, double now) const {
        for (const auto& c : cool_)
            if (c.owner == owner && now + game_clock::kEpsilon >= c.at && !game_clock::Reached(now - c.at, Frames(cfg_.cooldown_ms))) return true;
        return false;
    }

private:
    struct Cool {uint32_t owner = 0; double at = 0;};
    Config cfg_;
    State state_ = State::Idle;
    uint32_t owner_ = 0, serial_ = 0, owned_ = 0, player_ = 0, global_ = 0, room_ = 0, frame_ = 0, release_frame_ = 0;
    double queued_ = 0, t0_ = 0, release_t_ = 0, lease_ms_ = 0, abandon_t_ = -1;
    unsigned taken_ = 0, lease_tries_ = 0;
    bool lease_started_ = false, lease_given_up_ = false, lease_lost_ = false, first_taken_ = false, watch_cut_ = false;
    bool release_due_ = false;
    int hp_release_ = 0, hp_min_ = 0;
    Refuse refuse_ = Refuse::None;
    Abandon abandon_ = Abandon::None;
    Cool cool_[kOwners]{};
    Stats stats_;

    bool Gap(uint32_t frame) const {
        const int32_t d = static_cast<int32_t>(frame - frame_);   // wraps like the counter
        return d < 0 || d > kMaxGapTicks;
    }
    void Clear() {
        owner_ = serial_ = owned_ = player_ = global_ = room_ = frame_ = release_frame_ = 0;
        queued_ = t0_ = release_t_ = lease_ms_ = 0; abandon_t_ = -1; taken_ = lease_tries_ = 0;
        lease_started_ = lease_given_up_ = lease_lost_ = first_taken_ = watch_cut_ = release_due_ = false;
        hp_release_ = hp_min_ = 0; refuse_ = Refuse::None; abandon_ = Abandon::None;
    }
    void Drop(Refuse r) {refuse_ = r; state_ = State::Idle; ++stats_.refused;}
    void Remember(uint32_t owner, double now) {   // the owner's slot, else a free one, else the oldest
        unsigned slot = kOwners;
        for (unsigned i = 0; i < kOwners && slot == kOwners; ++i) if (cool_[i].owner == owner) slot = i;
        for (unsigned i = 0; i < kOwners && slot == kOwners; ++i) if (!cool_[i].owner) slot = i;
        if (slot == kOwners) {slot = 0; for (unsigned i = 1; i < kOwners; ++i) if (cool_[i].at < cool_[slot].at) slot = i;}
        cool_[slot] = {owner, now};
    }
};
}  // namespace link_runtime::sword_lock::probe

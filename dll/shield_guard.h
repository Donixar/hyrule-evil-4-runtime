// dll/shield_guard.h
#pragma once
// Shield guard with durability (shield-guard-01, Donald 18-oct night, docs/sesion-2026-10-18.md "escudo en guardia
// con desgaste"; design and evidence in docs/shield-guard-01-2026-10-18.md). Pure logic: no hooks and no process
// access; the wiring in link_runtime.cpp supplies Memory/Port/Sources objects. INI ([LinkRuntime]):
//   ShieldGuard=1          on (default off; needs ZTarget=1 and the parry transport, ParryActive or ParryGeneralBlock)
//   ShieldGuardBlocks=5    blocked hits before the shield breaks, 1..99
//   ShieldRegenSeconds=45  game seconds until a broken shield comes back, 1..600
//
// 1. Guard pose. While the Z-lock is on (Ctrl held / pad R3 toggle) and the knife is not held, Link plays TP's
//    shield guard (atdefs, then held) as an upper-body layer on the native secondary-motion route (player+2A8), the
//    route the parry reaction uses (parry_reaction.h; ParryReaction is installed): parts 1-16 rotation only, so the
//    legs keep the native walk/strafe. A blocked hit plays TP's atdefr (the shield takes the hit) on the same work,
//    a break plays atdefng. The neck controller is suppressed while the layer owns 2A8 (same contract as the
//    reaction). A primary MotionSet (damage, knife attack, sword clips) detaches the layer natively: it ends without
//    writes and comes back when the lock still wants it (after a short cooldown).
// 2. Shield in hand. Link's shield is geometry of the body BIN (pl00/pl08/pl09/pl10 member 000, rigid on bone 2; the
//    back piece 005 only carries the sheathed sword). The body info (first node of player+0x15C) is swapped to a body
//    variant built offline from the installed body (scripts/shield_guard_assets.py): "hand" = same body with the
//    shield faces rigid on bone 16 (left hand) placed for the TP guard, "none" = without the shield. Same skeleton,
//    materials and TPL; only info+0xC (model) and the bound box words change, compare-exchanged and put back exactly
//    (route (a) of the ocarina mount, docs/ocarina-mount-probe-2026-10-11.md, on the body info). A variant is shown
//    only when the live body BIN carries the fingerprint of the installed body it was derived from.
// 3. Block. Tier 3 of the pre-effects resolver (after tier-1 parry and tier-2 general block): with the guard up and
//    the shield intact, a sample of an allowed producer (general::Classify: Ganado melee, bosses and thrown weapons
//    as configured for tier 2) whose source (the attacker when known, else the weapon sample) lies within 60
//    degrees of Link's facing is cancelled like a tier-2 block: the sphere test reports no hit (0 damage, no native
//    hurt reaction). Sparks (parry sparks), shield sound (wood/metal, the rear shield's TP sounds), a thrown weapon
//    is deflected, and Link recoils with the hit clip. Later samples of the same swing are followed silently and
//    cost nothing.
// 4. Durability. Every block costs 1 (follow-ups free). At 0 the shield breaks: hidden (body "none"), break sound,
//    no frontal block and no rear shield (RearShield) until ShieldRegenSeconds of game time later, when it reappears
//    on the back with a chime. Same rule for the Ordon (wood), Hylian and Master Sword costume shields (metal sound).
//    Memory only: the state survives room loads inside one game session (the game clock is our own accumulated
//    game time, paused with the game) and resets when the game restarts.
#include "shield_guard_config.h"
#include "parry_general_block.h"
#include "game_clock.h"
#include "link_runtime_core.h"
#include "secondary_slot.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace link_runtime::shield_guard {

// ------------------------------------------------------------------------------------------------ durability
enum class Material : uint8_t { Wood, Metal };
// Costume 0 = pl00 Ordon (wooden), 1 = pl08 Hylian, 2 = pl09, 3 = pl10 Master Sword costume (Hylian shield).
inline bool CostumeSupported(unsigned costume) { return costume <= 3; }
inline Material CostumeMaterial(unsigned costume) { return costume == 0 ? Material::Wood : Material::Metal; }

enum class Hit : uint8_t { Ignored, Blocked, Broke };
class Durability {
public:
    void Configure(unsigned blocks, unsigned seconds) {
        max_ = left_ = blocks < kMinBlocks ? kMinBlocks : blocks > kMaxBlocks ? kMaxBlocks : blocks;
        const unsigned s = seconds < kMinRegen ? kMinRegen : seconds > kMaxRegen ? kMaxRegen : seconds;
        regen_ = s * 60.0; broken_ = false; broken_at_ = 0;
    }
    unsigned Max() const { return max_; }
    unsigned Left() const { return left_; }
    bool Broken() const { return broken_; }
    double RegenFrames() const { return regen_; }
    // now: game time in 60 Hz frames (monotonic, our own clock).
    double Remaining(double now) const { return broken_ ? (std::max)(0.0, broken_at_ + regen_ - now) : 0.0; }
    Hit Block(double now) {
        if (broken_ || !left_) return Hit::Ignored;
        if (--left_) return Hit::Blocked;
        broken_ = true; broken_at_ = now;
        return Hit::Broke;
    }
    // True once, when a broken shield has waited the regeneration time.
    bool Regen(double now) {
        if (!broken_ || !game_clock::Reached(now - broken_at_, regen_)) return false;
        broken_ = false; left_ = max_;
        return true;
    }
private:
    unsigned max_ = 5, left_ = 5;
    double regen_ = 45 * 60.0, broken_at_ = 0;
    bool broken_ = false;
};

// Our own game clock: the sum of every tick's Step60 (pauses when the game does not tick; never goes back).
class Clock {
public:
    void Advance(double step) { if (std::isfinite(step) && step > 0 && step < 10) now_ += step; }
    double Now() const { return now_; }
private:
    double now_ = 0;
};

// ------------------------------------------------------------------------------------------------ frontal cone
// Source within 60 degrees of Link's facing in XZ: ahead > 0 and lateral^2 <= 3 ahead^2 (tan 60 = sqrt 3).
// cos_out = ahead/|d| (for the log). False with a degenerate or non-finite input.
inline bool InCone(const float* player, const float* forward, const float* source, float& cos_out) {
    cos_out = 0;
    if (!player || !forward || !source) return false;
    const double fx = forward[0], fz = forward[2], dx = double(source[0]) - player[0], dz = double(source[2]) - player[2];
    const double ahead = dx * fx + dz * fz, lateral = dx * fz - dz * fx, n = (dx * dx + dz * dz) * (fx * fx + fz * fz);
    if (!std::isfinite(ahead) || !std::isfinite(lateral) || !std::isfinite(n) || n <= 1e-6) return false;
    cos_out = static_cast<float>(ahead / std::sqrt(n));
    return ahead > 0 && lateral * lateral <= 3.0 * ahead * ahead;
}

// ------------------------------------------------------------------------------------------------ tier-3 decision
enum class Reason : uint8_t { Blocked, Off, Structure, Descriptor, Producer, Lowered, Front, Cone, Broken, Fault, Reentry };
inline const char* ReasonName(Reason r) {
    switch (r) {
    case Reason::Blocked: return "blocked"; case Reason::Off: return "off"; case Reason::Structure: return "structure";
    case Reason::Descriptor: return "descriptor"; case Reason::Producer: return "producer"; case Reason::Lowered: return "lowered";
    case Reason::Front: return "front"; case Reason::Cone: return "cone"; case Reason::Broken: return "broken";
    case Reason::Fault: return "fault"; default: return "reentry";
    }
}
struct Outcome {
    bool cancel = false, follow = false, broke = false;
    Reason reason = Reason::Off;
    parry::general::Verdict verdict = parry::general::Verdict::Unlisted;
    uint32_t ret_rva = 0, function = 0, owner = 0;
    char cls = '?';
    unsigned left = 0;                   // blocks left after this one
    float cos = 0;                       // source bearing cosine
    bool owner_source = false;           // the cone used the attacker's position (else the weapon sample)
    parry::general::Descriptor descriptor{};
    parry::general::Front front{};
};
// Sources: bool Invocation(c); uint32_t ImageBase(); bool Live(size_t); bool Descriptor(uint32_t, Descriptor&);
// bool Front(c, Front&, float forward[3]); uint32_t Owner(c); bool OwnerPosition(uint32_t owner, float out[3]);
// bool Routine(uint32_t owner, uint8_t (&)[2]) (cEm r0/r1, general::Follow chain/flail bounds); uint64_t Now() (ms, follow-up timing).
// up: the guard pose is up (GuardLayer::Up); game_now: Clock::Now. A cancel with !follow consumed one block.
template <class S> Outcome Decide(S& s, const parry::Candidate& c, const parry::general::Settings& set,
                                  parry::general::Follow& follow, Durability& durability, bool enabled, bool up, double game_now) {
    namespace g = parry::general;
    Outcome o;
    if (!enabled) return o;
    o.reason = Reason::Structure;
    if (!c.body_part || !s.Invocation(c)) return o;
    const uint32_t base = s.ImageBase();
    o.ret_rva = c.outer_caller >= base ? c.outer_caller - base : 0;
    const size_t index = g::Find(o.ret_rva);
    if (index < g::kProducerCount) { o.function = g::kProducers[index].function; o.cls = g::kProducers[index].cls; }
    g::Descriptor again{};
    o.reason = Reason::Descriptor;
    if (!s.Descriptor(c.attack, o.descriptor) || !s.Descriptor(c.attack, again) || !g::Same(o.descriptor, again)) return o;
    o.verdict = g::Classify(index, index < g::kProducerCount && s.Live(index), o.descriptor, set);
    o.reason = Reason::Producer;
    if (o.verdict != g::Verdict::Allowed) return o;
    float forward[3]{};
    o.reason = Reason::Front;
    if (!s.Front(c, o.front, forward)) return o;
    o.owner = s.Owner(c);
    g::Swing swing{};   // the attacker's routine: chain/flail returns keep the longer follow-up (as tier 2)
    if (o.owner) swing.known = s.Routine(o.owner, swing.routine);
    const uint64_t now = s.Now();
    follow.Expire(now);
    // A later sample of a swing this guard already stopped: cancelled silently, free, even if the guard dropped or
    // the shield broke on that swing (it must not hit Link a frame later).
    if (follow.Matches(o.function, o.owner, o.front.sample, now, false, &swing)) {
        follow.Touch(o.front.sample, now);
        o.follow = o.cancel = true; o.reason = Reason::Blocked; o.left = durability.Left();
        return o;
    }
    o.reason = Reason::Lowered;
    if (!up) return o;
    o.reason = Reason::Broken;
    if (durability.Broken()) return o;
    float source[3]{};
    o.owner_source = o.ret_rva == 0x55CE2 && o.owner && s.OwnerPosition(o.owner, source);
    if (!o.owner_source) std::memcpy(source, o.front.sample, sizeof(source));
    o.reason = Reason::Cone;
    if (!InCone(o.front.player, forward, source, o.cos)) return o;
    const Hit hit = durability.Block(game_now);
    if (hit == Hit::Ignored) { o.reason = Reason::Broken; return o; }
    follow.Arm(o.function, o.owner, o.front.sample, now, swing);
    o.broke = hit == Hit::Broke; o.left = durability.Left();
    o.cancel = true; o.reason = Reason::Blocked;
    return o;
}
// SEH and reentry boundary, like general::Run: a fault or a nested resolver leaves native damage.
inline __declspec(thread) bool deciding = false;
template <class S> bool Run(S& s, const parry::Candidate& c, const parry::general::Settings& set, parry::general::Follow& follow,
                            Durability& durability, bool enabled, bool up, double game_now, Outcome& out) {
    const DWORD error = GetLastError();
    out = Outcome{};
    if (deciding) { out.reason = Reason::Reentry; SetLastError(error); return false; }
    deciding = true;
    // The durability copy is committed only after a clean decision: a fault never costs a block.
    Durability trial = durability;
    parry::general::Follow trial_follow = follow;
    bool ok = false;
    __try {
        out = Decide(s, c, set, trial_follow, trial, enabled, up, game_now);
        ok = out.cancel;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = Outcome{}; out.reason = Reason::Fault; ok = false;
    }
    if (out.reason != Reason::Fault) { durability = trial; follow = trial_follow; }
    deciding = false;
    SetLastError(error);
    return ok;
}

// ------------------------------------------------------------------------------------------------ guard layer
// Secondary-motion layer (player+2A8), the parry reaction's route held for as long as the guard is wanted.
enum class Clip : uint8_t { Guard, Hit, Break };
inline const char* ClipName(Clip c) { return c == Clip::Guard ? "guard" : c == Clip::Hit ? "hit" : "break"; }
constexpr uint32_t kNeckWorkOffset = 0x474;   // parry_reaction.h: the neck controller's own work
constexpr uint32_t kStanceWorkRva = 0x87A800; // parry_reaction.h: the knife stance's static work
constexpr uint16_t kSeqEnded = 4;
struct LayerConfig {
    unsigned ramp_in = 6, ramp_out = 6;   // 60 Hz frames
    unsigned cooldown = 20;               // after a native detach, before installing again
    unsigned break_hold = 75;             // break clip cap (35 TP frames = 70 game frames at TP speed)
    unsigned contest_limit = 3;           // native detaches within contest_window: give up until the lock is released
    unsigned contest_window = 120;
    unsigned resume_limit = 12;           // freed-2A8 re-installs within contest_window before the old lost path
};
struct LayerWorld {
    uint32_t player = 0;
    bool wanted = false;     // Z-lock on, knife not held, supported costume, gameplay
    bool blocked = false;    // damage, grab, death, scene/room change, Mortal Draw/sword clip, ocarina: give back now
    bool lent = false;       // 2A8 is the parry reaction's work while the reaction is active (it gives it back)
    uint32_t frame = 0;      // game frame counter (a repeated frame only reasserts the weight)
    float step = 1.f;        // this tick in 60 Hz frames
    uint32_t image_base = 0; // for the stance work
    bool stance_live = false; // slot-guard-01: the stance work is still the player's live blend (secondary_slot::Check);
                              // the exit gives 2A8 back to a stance only then, else 0
};
struct LayerStats {
    unsigned installs = 0, completed = 0, cancelled = 0, lost_owner = 0, dropped = 0, refused = 0, install_failed = 0;
    unsigned hits = 0, breaks = 0, lent = 0, repeated_frame = 0, neck_suppressed = 0, contested = 0, resumed = 0;
    unsigned cleared = 0;    // slot-guard-01: exits that gave 2A8 back as 0 (the previous owner was stale)
};
// slot-guard-01: what the last exit that wrote 2A8 found at install and gave back (for the log).
struct LayerHandBack { uint32_t previous = 0, restored = 0; bool wrote = false, cleared = false; };
enum class Phase : uint8_t { Idle, In, Hold, Out, Zero, Breaking };
inline const char* PhaseName(Phase p) {
    switch (p) {
    case Phase::Idle: return "idle"; case Phase::In: return "in"; case Phase::Hold: return "hold"; case Phase::Out: return "out";
    case Phase::Zero: return "zero"; default: return "breaking";
    }
}
// Port (game thread): bool Install(player, Clip, out work) inits OUR work (parry_reaction.h port contract) and
// writes 2A8=work; uint32_t Secondary(player); bool ReplaceSecondary(player, expected, value); float Weight(work);
// void SetWeight(work, w); uint16_t Status(work).
class GuardLayer {
public:
    explicit GuardLayer(LayerConfig c = {}) : config_(c) {}
    Phase phase() const { return phase_; }
    bool Active() const { return phase_ != Phase::Idle; }
    float Weight() const { return Active() ? last_ : 0.f; }
    uint32_t Work() const { return work_; }
    Clip clip() const { return clip_; }
    const LayerStats& stats() const { return stats_; }
    const LayerHandBack& last_exit() const { return exit_; }
    // The guard pose is up: blocks count. The ramp-in counts from half weight; the break clip never does.
    bool Up() const { return (phase_ == Phase::Hold || (phase_ == Phase::In && last_ >= 0.5f)) && !lent_now_; }
    void OnBlocked() { if (Active()) hit_pending_ = true; }
    void OnBroken() { if (Active()) break_pending_ = true; hit_pending_ = false; }
    bool SuppressNeck(uint32_t player) {
        if (phase_ == Phase::Idle || player != player_) return false;
        ++stats_.neck_suppressed; return true;
    }
    template <class Port> void Tick(const LayerWorld& w, Port& port) {
        namespace gc = link_runtime::game_clock;
        const double step = std::isfinite(w.step) && w.step > 0.f ? double(w.step) : 1.0;
        if (!w.wanted && phase_ == Phase::Idle) { contest_ = 0; given_up_ = false; }
        if (cooldown_ > 0) cooldown_ -= step;
        if (contest_window_ > 0) contest_window_ -= step; else contest_ = 0;
        if (resume_window_ > 0) resume_window_ -= step; else resumes_ = 0;
        if (phase_ != Phase::Idle && w.frame && w.frame == frame_) {
            ++stats_.repeated_frame;
            if (w.player == player_ && port.Secondary(player_) == work_ && phase_ != Phase::Zero) port.SetWeight(work_, last_);
            return;
        }
        frame_ = w.frame;
        lent_now_ = false;
        if (phase_ == Phase::Idle) {
            hit_pending_ = break_pending_ = false;
            if (!w.wanted || w.blocked || given_up_ || cooldown_ > 0 || !w.player) return;
            const uint32_t current = port.Secondary(w.player);
            const bool stance = w.image_base && current == w.image_base + kStanceWorkRva;
            if (current && current != w.player + kNeckWorkOffset && !stance) { ++stats_.refused; cooldown_ = config_.cooldown; return; }
            uint32_t work = 0;
            if (!port.Install(w.player, Clip::Guard, work) || !work || port.Secondary(w.player) != work) {
                ++stats_.install_failed; cooldown_ = config_.cooldown; return;
            }
            ++stats_.installs; player_ = w.player; work_ = work; previous_ = current; clip_ = Clip::Guard;
            last_ = 0.f; port.SetWeight(work_, 0.f); phase_ = Phase::In; ramp_ = 0;
        }
        if (w.player != player_) { ++stats_.dropped; Idle(); return; }     // never write through an old player
        if (port.Secondary(player_) != work_) {
            if (w.lent) { ++stats_.lent; lent_now_ = true; return; }     // the reaction borrowed it and gives it back
            // A known action took Link (damage, a sword clip such as the Shield Bash, Mortal Draw, ocarina): its
            // primary MotionSet detached us. Not a contest: the guard comes back after it, while still wanted.
            if (w.blocked) { ++stats_.cancelled; Idle(); cooldown_ = config_.cooldown; return; }
            // shield-guard-resume (30-oct): a locomotion MotionSet (walk/stop/turn, most often over the grenade and
            // Striker primaries) clears 2A8 or hands it back to the neck. While still wanted, re-install at once at
            // the weight in effect instead of dropping the shield for the cooldown; a foreign owner or too many
            // resumes in the window still take the old path.
            const uint32_t cur = port.Secondary(player_);
            const bool freed = !cur || cur == player_ + kNeckWorkOffset || (w.image_base && cur == w.image_base + kStanceWorkRva);
            if (freed && w.wanted && clip_ == Clip::Guard && (phase_ == Phase::In || phase_ == Phase::Hold) &&
                resumes_ < config_.resume_limit) {
                uint32_t work = 0;
                if (port.Install(player_, Clip::Guard, work) && work && port.Secondary(player_) == work) {
                    if (resume_window_ <= 0) resume_window_ = config_.contest_window;
                    ++resumes_; ++stats_.resumed; work_ = work; previous_ = cur; Write(port, last_); return;
                }
            }
            ++stats_.lost_owner; Lost(); return;                          // unexplained native detach or the neck
        }
        if (w.blocked) { ++stats_.cancelled; Give(port, w); Idle(); cooldown_ = config_.cooldown; return; }
        if (break_pending_) {
            break_pending_ = false;
            uint32_t work = 0;
            if (port.Install(player_, Clip::Break, work) && work == work_ && port.Secondary(player_) == work_) {
                ++stats_.breaks; clip_ = Clip::Break; phase_ = Phase::Breaking; ramp_ = 0; Write(port, 1.f);
            } else { ++stats_.install_failed; Lost(); return; }
            return;
        }
        if (hit_pending_ && (phase_ == Phase::In || phase_ == Phase::Hold)) {
            hit_pending_ = false;
            uint32_t work = 0;
            if (port.Install(player_, Clip::Hit, work) && work == work_ && port.Secondary(player_) == work_) {
                ++stats_.hits; clip_ = Clip::Hit; Write(port, last_ > 0.f ? last_ : 1.f);
            } else { ++stats_.install_failed; Lost(); return; }
        }
        hit_pending_ = false;
        if (phase_ == Phase::Breaking) {
            ramp_ += step;
            if (port.Status(work_) == kSeqEnded || gc::Reached(ramp_, double(config_.break_hold))) { phase_ = Phase::Out; from_ = last_; ramp_ = 0; }
            else { Write(port, 1.f); return; }
        }
        if (!w.wanted && (phase_ == Phase::In || phase_ == Phase::Hold)) { phase_ = Phase::Out; from_ = last_; ramp_ = 0; }
        else if (w.wanted && phase_ == Phase::Out && clip_ != Clip::Break) {
            // Wanted again during the ramp out: ramp back in from the weight in effect.
            phase_ = Phase::In; ramp_ = double(last_) * config_.ramp_in;
        }
        switch (phase_) {
        case Phase::In: {
            ramp_ += step;
            const bool full = gc::Reached(ramp_, double(config_.ramp_in));
            Write(port, full ? 1.f : float(ramp_ / config_.ramp_in));
            if (full) phase_ = Phase::Hold;
            break;
        }
        case Phase::Hold: Write(port, 1.f); break;
        case Phase::Out: {
            ramp_ += step;
            const float v = gc::Reached(ramp_, double(config_.ramp_out)) ? 0.f
                : from_ * float((double(config_.ramp_out) - ramp_) / config_.ramp_out);
            Write(port, v);
            if (v == 0.f) phase_ = Phase::Zero;
            break;
        }
        case Phase::Zero:   // the previous tick ran MotionMove at weight 0 (exact primary pose)
            Give(port, w); ++stats_.completed; Idle(); return;
        default: break;
        }
    }
private:
    void Idle() { phase_ = Phase::Idle; player_ = work_ = previous_ = frame_ = 0; ramp_ = 0; last_ = from_ = 0.f; lent_now_ = false; }
    void Lost() {
        Idle(); cooldown_ = config_.cooldown;
        if (contest_window_ <= 0) contest_window_ = config_.contest_window;
        if (++contest_ >= config_.contest_limit) { given_up_ = true; ++stats_.contested; }
    }
    // slot-guard-01: previous_ (0, the neck or the stance) only while it is still a live owner, else 0.
    template <class Port> void Give(Port& port, const LayerWorld& w) {
        bool cleared = false;
        const uint32_t back = secondary_slot::HandBack(previous_, player_, w.image_base, w.stance_live, 0, cleared);
        exit_ = LayerHandBack{previous_, back, false, cleared};
        if (port.ReplaceSecondary(player_, work_, back)) { exit_.wrote = true; if (cleared) ++stats_.cleared; return; }
        ++stats_.lost_owner;
    }
    template <class Port> void Write(Port& port, float v) { port.SetWeight(work_, v); last_ = v; }
    LayerConfig config_;
    Phase phase_ = Phase::Idle;
    Clip clip_ = Clip::Guard;
    uint32_t player_ = 0, work_ = 0, previous_ = 0, frame_ = 0;
    double ramp_ = 0, cooldown_ = 0, contest_window_ = 0, resume_window_ = 0;
    float last_ = 0.f, from_ = 0.f;
    unsigned contest_ = 0, resumes_ = 0;
    bool hit_pending_ = false, break_pending_ = false, given_up_ = false, lent_now_ = false;
    LayerStats stats_;
    LayerHandBack exit_;
};

// ------------------------------------------------------------------------------------------------ body variants
namespace body_layout {
constexpr uint32_t kModelList = 0x15C, kNext = 0x14, kModel = 0xC, kFlags = 0x4, kShape = 0x2;
constexpr uint32_t kBox = 0x44, kBoxWords = 6;
constexpr uint32_t kBinFlags = 0x20, kBinRelocated = 0x100000, kBinShape = 0x2C, kBinVersion = 0x3C, kBinPositions = 0x30;
constexpr uint32_t kVersionA = 0x20010801, kVersionB = 0x20030818;
constexpr uint32_t kAssetAlign = 32;
constexpr uint32_t kBinPointers[] = {0x00, 0x0C, 0x10, 0x14, 0x1C, 0x30, 0x34, 0x40, 0x48, 0x4C};
// Fingerprint: header words 0x18/0x28/0x38/0x3C (counts, version) and 0x40, + the first kPrintWords words of the
// vertex positions (never relocated). scripts/shield_guard_assets.py computes the same on the installed file.
// shield-guard-02: 0x40 is a POINTER that calcModelAddr relocates (live pl08 0x40 = bin + 0xB70, file 0xB70); -01
// hashed it raw, so every live body was refused (`result=fingerprint`). It is now hashed as an offset from the BIN.
constexpr uint32_t kPrintHeader[] = {0x18, 0x28, 0x38, 0x3C, 0x40};
constexpr bool kPrintIsPointer[] = {false, false, false, false, true};
constexpr unsigned kPrintHeaderCount = 5;
constexpr uint32_t kPrintWords = 128;
constexpr unsigned kMaxList = 16;
}  // namespace body_layout

inline uint32_t Fnv(uint32_t h, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i) { h ^= (word >> (8 * i)) & 0xFF; h *= 16777619u; }
    return h;
}
// Memory: bool Read32(addr, out). bin must be relocated (positions word is an address) or a file image whose
// positions word is an offset: `positions_base` is added (0 for a live relocated BIN, the file address otherwise).
// Pointer header words are hashed as offsets (value + positions_base - bin): the same number live and on disk.
struct PrintParts {
    uint32_t header[body_layout::kPrintHeaderCount] = {};
    uint32_t positions = 2166136261u;   // FNV-1a of the first kPrintWords position words alone
    uint32_t print = 0;                 // the combined fingerprint
};
template <class Memory> bool FingerprintParts(Memory& m, uint32_t bin, uint32_t positions_base, PrintParts& out) {
    namespace L = body_layout;
    uint32_t h = 2166136261u, w = 0, positions = 0;
    for (unsigned k = 0; k < L::kPrintHeaderCount; ++k) {
        if (!m.Read32(bin + L::kPrintHeader[k], w)) return false;
        if (L::kPrintIsPointer[k] && w) w = w + positions_base - bin;
        out.header[k] = w; h = Fnv(h, w);
    }
    if (!m.Read32(bin + L::kBinPositions, positions)) return false;
    positions += positions_base;
    uint32_t p = 2166136261u;
    for (uint32_t i = 0; i < L::kPrintWords; ++i) { if (!m.Read32(positions + 4 * i, w)) return false; h = Fnv(h, w); p = Fnv(p, w); }
    out.positions = p; out.print = h;
    return true;
}
template <class Memory> bool Fingerprint(Memory& m, uint32_t bin, uint32_t positions_base, uint32_t& out) {
    PrintParts parts;
    if (!FingerprintParts(m, bin, positions_base, parts)) return false;
    out = parts.print;
    return true;
}
// First field that differs between a live print and the expected one: the header offset, 0xFFFF for the positions.
inline uint32_t PrintMismatch(const PrintParts& live, const uint32_t (&header)[body_layout::kPrintHeaderCount], uint32_t positions) {
    for (unsigned k = 0; k < body_layout::kPrintHeaderCount; ++k) if (live.header[k] != header[k]) return body_layout::kPrintHeader[k];
    return live.positions != positions ? 0xFFFFu : 0u;
}

struct Variant {
    uint32_t bin = 0, size = 0;
    uint32_t box[body_layout::kBoxWords] = {};
    bool ready = false;
};
inline bool FiniteWord(uint32_t w) { return (w & 0x7F800000u) != 0x7F800000u; }
// Native: bool CalcModelAddr(bin), bool BoundingBox(bin, uint32_t (&out)[6]) (ocarina_mount.h contract).
template <class Memory, class Native> Variant PrepareVariant(Memory& m, Native& native, uint32_t bin, uint32_t size) {
    namespace L = body_layout;
    Variant out;
    if (!IsPointer(bin) || bin % L::kAssetAlign || size < 0x60 || size > 0x100000 || bin + size < bin) return out;
    if (!native.CalcModelAddr(bin)) return out;
    uint32_t flags = 0, shape = 0, version = 0, bones = 0;
    if (!m.Read32(bin + L::kBinFlags, flags) || !(flags & L::kBinRelocated) || !m.Read32(bin + L::kBinShape, shape) || shape ||
        !m.Read32(bin + L::kBinVersion, version) || (version != L::kVersionA && version != L::kVersionB) ||
        !m.Read32(bin + 0x18, bones) || ((bones >> 8) & 0xFF) != 119) return out;
    for (uint32_t offset : L::kBinPointers) {
        uint32_t p = 0;
        if (!m.Read32(bin + offset, p)) return out;
        if (p && (p < bin || p >= bin + size)) return out;
    }
    uint32_t box[L::kBoxWords] = {};
    if (!native.BoundingBox(bin, box)) return out;
    for (uint32_t w : box) if (!FiniteWord(w)) return out;
    for (uint32_t i = 0; i < L::kBoxWords; ++i) out.box[i] = box[i];
    out.bin = bin; out.size = size; out.ready = true;
    return out;
}

enum class SwapResult : uint8_t {
    Shown, Restored, Unchanged, NoPlayer, NoBody, ShapeModel, NotReady, Fingerprint, Foreign, IdentityChanged, InvalidMemory, RaceLost
};
inline const char* SwapName(SwapResult r) {
    switch (r) {
    case SwapResult::Shown: return "shown"; case SwapResult::Restored: return "restored"; case SwapResult::Unchanged: return "unchanged";
    case SwapResult::NoPlayer: return "no_player"; case SwapResult::NoBody: return "no_body"; case SwapResult::ShapeModel: return "shape";
    case SwapResult::NotReady: return "not_ready"; case SwapResult::Fingerprint: return "fingerprint"; case SwapResult::Foreign: return "foreign";
    case SwapResult::IdentityChanged: return "identity_changed"; case SwapResult::InvalidMemory: return "invalid_memory";
    default: return "race_lost";
    }
}
// Body info swap. Memory: Read32 / Compare32. Show(variant) from native or from another variant; Restore puts back
// exactly the model pointer and box words it found. Every write is a compare-exchange; nothing is written through
// a player/info that changed (room load, costume change): the lease is dropped instead.
class BodySwap {
public:
    bool Holding() const { return active_; }
    uint32_t Shown() const { return active_ ? shown_ : 0; }
    uint32_t Info() const { return info_; }
    // The live body's print parts at the last Fingerprint refusal (for the log: which field differs).
    const PrintParts& Refused() const { return refused_; }
    template <class Memory> SwapResult Show(Memory& m, uint32_t player_slot, const Variant& v, uint32_t expected_print) {
        namespace L = body_layout;
        if (!v.ready) return SwapResult::NotReady;
        uint32_t player = 0, info = 0;
        if (!m.Read32(player_slot, player)) return SwapResult::InvalidMemory;
        if (!IsPointer(player)) return SwapResult::NoPlayer;
        if (!m.Read32(player + L::kModelList, info)) return SwapResult::InvalidMemory;
        if (!IsPointer(info)) return SwapResult::NoBody;
        if (active_ && (player != player_ || info != info_)) { Drop(); }
        uint32_t flags = 0, model = 0, box[L::kBoxWords] = {};
        if (!m.Read32(info + L::kFlags, flags) || !m.Read32(info + L::kModel, model)) return SwapResult::InvalidMemory;
        for (uint32_t i = 0; i < L::kBoxWords; ++i) if (!m.Read32(info + L::kBox + 4 * i, box[i])) return SwapResult::InvalidMemory;
        if (active_) {
            if (model != shown_) { Drop(); return SwapResult::Foreign; }
            if (model == v.bin) return SwapResult::Unchanged;
            // Variant to variant: model first, box after (both ours, the original stays recorded).
            if (!m.Compare32(info + L::kModel, shown_, v.bin)) return SwapResult::RaceLost;
            for (uint32_t i = 0; i < L::kBoxWords; ++i)
                if (box[i] != v.box[i] && !m.Compare32(info + L::kBox + 4 * i, box[i], v.box[i])) {
                    m.Compare32(info + L::kModel, v.bin, shown_); return SwapResult::RaceLost;
                }
            shown_ = v.bin; for (uint32_t i = 0; i < L::kBoxWords; ++i) shown_box_[i] = v.box[i];
            return SwapResult::Shown;
        }
        if (flags & L::kShape) return SwapResult::ShapeModel;
        if (!IsPointer(model) || model == v.bin) return SwapResult::InvalidMemory;
        PrintParts parts;
        if (!FingerprintParts(m, model, 0, parts)) return SwapResult::InvalidMemory;
        if (parts.print != expected_print) { refused_ = parts; return SwapResult::Fingerprint; }
        uint32_t again = 0;
        if (!m.Read32(player_slot, again) || again != player) return SwapResult::InvalidMemory;
        if (!m.Compare32(info + L::kModel, model, v.bin)) return SwapResult::RaceLost;
        uint32_t written = 0;
        for (; written < L::kBoxWords; ++written)
            if (box[written] != v.box[written] && !m.Compare32(info + L::kBox + 4 * written, box[written], v.box[written])) break;
        if (written < L::kBoxWords) {
            for (uint32_t i = 0; i < written; ++i) if (box[i] != v.box[i]) m.Compare32(info + L::kBox + 4 * i, v.box[i], box[i]);
            m.Compare32(info + L::kModel, v.bin, model);
            return SwapResult::RaceLost;
        }
        active_ = true; player_ = player; info_ = info; model_ = model; shown_ = v.bin;
        for (uint32_t i = 0; i < L::kBoxWords; ++i) { box_[i] = box[i]; shown_box_[i] = v.box[i]; }
        return SwapResult::Shown;
    }
    template <class Memory> SwapResult Restore(Memory& m, uint32_t player_slot) {
        namespace L = body_layout;
        if (!active_) return SwapResult::Unchanged;
        uint32_t player = 0, info = 0;
        if (!m.Read32(player_slot, player)) return SwapResult::InvalidMemory;
        if (player != player_ || !m.Read32(player + L::kModelList, info) || info != info_) { Drop(); return SwapResult::IdentityChanged; }
        uint32_t model = 0, box[L::kBoxWords] = {};
        if (!m.Read32(info + L::kModel, model)) return SwapResult::InvalidMemory;
        for (uint32_t i = 0; i < L::kBoxWords; ++i) if (!m.Read32(info + L::kBox + 4 * i, box[i])) return SwapResult::InvalidMemory;
        bool foreign = model != shown_;
        for (uint32_t i = 0; i < L::kBoxWords; ++i) foreign = foreign || (box[i] != shown_box_[i] && box[i] != box_[i]);
        if (foreign) { Drop(); return SwapResult::Foreign; }
        // Box first, model last: every state this object can leave keeps model == shown_ until the end.
        for (uint32_t i = 0; i < L::kBoxWords; ++i)
            if (box[i] != box_[i] && !m.Compare32(info + L::kBox + 4 * i, shown_box_[i], box_[i])) return SwapResult::RaceLost;
        if (!m.Compare32(info + L::kModel, shown_, model_)) return SwapResult::RaceLost;
        Drop();
        return SwapResult::Restored;
    }
    // The lease is forgotten without writing (the info it held is gone or changed by someone else).
    void Drop() { active_ = false; player_ = info_ = model_ = shown_ = 0; for (auto& b : box_) b = 0; for (auto& b : shown_box_) b = 0; }
private:
    bool active_ = false;
    PrintParts refused_;
    uint32_t player_ = 0, info_ = 0, model_ = 0, shown_ = 0;
    uint32_t box_[body_layout::kBoxWords] = {}, shown_box_[body_layout::kBoxWords] = {};
};

// ------------------------------------------------------------------------------------------------ placement policy
enum class Placement : uint8_t { Back, Hand, None };
inline const char* PlacementName(Placement p) { return p == Placement::Back ? "back" : p == Placement::Hand ? "hand" : "none"; }
// gameplay false (cutscene, loading, death) always shows the native body: the shield on the back.
// shield_action (shield-guard-02): a TP clip that holds the shield in the left hand is playing (Shield Bash, Back
// Slice), so the shield rides the hand for its whole duration even with the guard layer released.
inline Placement Want(bool gameplay, bool broken, bool lock_wanted, float layer_weight, bool shield_action = false) {
    if (!gameplay) return Placement::Back;
    if (broken) return Placement::None;
    return lock_wanted || shield_action || layer_weight >= 0.5f ? Placement::Hand : Placement::Back;
}
// The rear shield (RearShield, rear_shield_band.h) counts only with the shield on the back and intact.
inline bool RearAllowed(bool enabled, Placement shown, bool broken) { return !enabled || (shown == Placement::Back && !broken); }

// ------------------------------------------------------------------------------------------------ primary arm IK
// sword-hand-01 (19-oct): the guard clips carry the left hand (part 16) as the native arm IK effector target
// (model-space rotation + position, scripts/shield_guard_anim.py EFFECTOR), which is how InverseKinematics reads it
// while the PRIMARY motion declares the arm chain (a joint on part 13 with kind & 0x30: 50 of the 66 pl08 motions,
// kind 0xA0). Over a primary without that chain, part 16 would be parent-relative and the hand would land a metre off,
// so the layer is only installed (and kept) over such a primary. The primary FCV is model+1D8 (the dispatch's Owns reads
// the same word); MotionSetCore relocates only its key-offset table, the header (u16 maxFrame, u8 nJoints, u16 kind[n],
// u8 partsNo[n]) is the file's.
constexpr uint32_t kPrimaryMotion = 0x1D8;
constexpr unsigned kArmIkRoot = 13;
// header: the bytes from the FCV start, at least 3 + 3n of them.
inline bool HeaderHasArmIk(const uint8_t* header, size_t size) {
    if (!header || size < 3) return false;
    const unsigned n = header[2];
    if (size < 3 + 3 * size_t(n)) return false;
    for (unsigned j = 0; j < n; ++j) {
        const unsigned kind = header[3 + 2 * j];   // low byte of the little-endian kind word
        if (header[3 + 2 * n + j] == kArmIkRoot && (kind & 0x30)) return true;
    }
    return false;
}
// 1 the primary declares the left-arm IK chain, 0 not (or no primary), -1 unreadable. Memory: bool Read(addr, out, n).
template <class Memory> int PrimaryArmIk(Memory& m, uint32_t model) {
    uint32_t motion = 0;
    if (!m.Read(model + kPrimaryMotion, &motion, 4)) return -1;
    if (!motion) return 0;
    uint8_t header[3 + 3 * 255] = {};
    if (!m.Read(motion, header, 3)) return -1;
    const size_t need = 3 + 3 * size_t(header[2]);
    if (!m.Read(motion, header, need)) return -1;
    return HeaderHasArmIk(header, need) ? 1 : 0;
}
// shield-fk-guard-01 (29-oct): primaries without that chain (grenades 0x08, Striker 0x1C: arm_ik=0) play the FK
// variant of the clips instead (link_shield_guard*_fk.fcv: part 16 parent-relative, the hand where the forward
// kinematics of the clip puts it). The variant is chosen when the layer installs; if the primary's verdict flips while
// the layer is up, the layer ends cleanly (blocked) and comes back with the other variant after the cooldown.
enum class ClipVariant : uint8_t { None, Effector, Fk };
inline const char* VariantName(ClipVariant v) { return v == ClipVariant::Effector ? "ik" : v == ClipVariant::Fk ? "fk" : "none"; }
// arm_ik: PrimaryArmIk (1 chain, 0 no chain, -1 unreadable); fk_ready: the FK clips are resident.
inline ClipVariant VariantFor(int arm_ik, bool fk_ready) {
    if (arm_ik == 1) return ClipVariant::Effector;
    return arm_ik == 0 && fk_ready ? ClipVariant::Fk : ClipVariant::None;
}
inline bool VariantMismatch(bool layer_active, ClipVariant running, ClipVariant now) {
    return layer_active && now != ClipVariant::None && running != now;
}
}  // namespace link_runtime::shield_guard

#pragma once
// slot-guard-01 (1-nov, docs/combate-2026-11-01.md): the player's secondary motion slot, player+0x2A8 (cModel::pMotionB,
// the work MotionMove blends over the primary). Who may hold it, whether a hand-back target is still its owner's live
// clip, a watchdog for a slot no system owns, and a read-only probe of the arm bones. Pure logic: the wiring
// (link_runtime.cpp) supplies the memory and does the one write (a compare-exchange of 2A8 to 0).
//
// 31-oct (Donald): right after a tier-2 parry against Krauser (power 800), with the parry reaction on 2A8 over the knife
// stance, Link lost both arms and they stayed gone with any weapon until he died; the draw bits were intact. The log of
// that session shows the reaction completing and handing 2A8 back to the stance work. What broke the bones is not proven
// offline; this removes every stale hand-back the layers can construct and logs the slot and the arm bones, so a live
// repro says which one it was.
//
// Native facts (bio4.exe 1.1.0; re4-decomp game/motion.cpp, pl_class.cpp, pl_knife.cpp; PC bytes read on the pinned
// image):
//  - MotionSetCore (2611A0) clears model+2A8 unless the work being set carries Mot_flag bit 29 (2611BC..2611D6). The
//    player's primary never carries it, so every primary MotionSet drops the secondary.
//  - Natively the player's 2A8 is written only by cPlNeck::motSet (its own work, player+0x474) and by the global cMot3
//    `mot3` (bio4+0x87A7E8; knife stance and aim blends). cMot3::set (27E190) stores the model at +0, makes mot0 (+8)
//    the primary and inits its work (+0x18, i.e. bio4+0x87A800) with mot1 (+0xC); cMot3::move (27CE00) swaps the work
//    between mot1 and mot2 (+0x10) and writes |rate| into [model+2A8]+C8, whoever owns 2A8 at that moment.
//  - So the stance work is the player's live blend only while mot3.model is the player, its clip (work+0) is mot1 or
//    mot2, and the player's primary clip (model+0x1D8) is still mot0. A stance work handed back after the knife or the
//    aim ended (or after anything replaced the primary without a MotionSet) is a stale clip blended over a pose it was
//    never made for.
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace link_runtime::secondary_slot {
constexpr uint32_t kSlot = 0x2A8, kNeckWork = 0x474, kPrimaryMotion = 0x1D8, kWeight = 0xC8;
constexpr uint32_t kMot3Rva = 0x87A7E8, kStanceWorkRva = 0x87A800;
namespace mot3 {
constexpr uint32_t model = 0x0, mot0 = 0x8, mot1 = 0xC, mot2 = 0x10, work = 0x18;
}
static_assert(kMot3Rva + mot3::work == kStanceWorkRva, "cMot3::work is the stance work");

// ------------------------------------------------------------------------------------------------ the stance work
struct Stance {
    bool read = false;
    uint32_t model = 0, mot0 = 0, mot1 = 0, mot2 = 0;
    uint32_t clip = 0;      // work+0 (MOTION_INFO::pMot)
    uint32_t primary = 0;   // player+0x1D8
};
// Memory: bool Read32(uint32_t address, uint32_t& out).
template <class Memory> Stance ReadStance(Memory& m, uint32_t image_base, uint32_t player) {
    Stance s;
    if (!image_base || !player) return s;
    const uint32_t at = image_base + kMot3Rva;
    s.read = m.Read32(at + mot3::model, s.model) && m.Read32(at + mot3::mot0, s.mot0) && m.Read32(at + mot3::mot1, s.mot1) &&
             m.Read32(at + mot3::mot2, s.mot2) && m.Read32(at + mot3::work, s.clip) && m.Read32(player + kPrimaryMotion, s.primary);
    return s;
}
enum class StanceState : uint8_t { Live, Unread, OtherModel, NoClip, ForeignClip, PrimaryChanged };
inline const char* Name(StanceState s) {
    switch (s) {
    case StanceState::Live: return "live"; case StanceState::Unread: return "unread"; case StanceState::OtherModel: return "other_model";
    case StanceState::NoClip: return "no_clip"; case StanceState::ForeignClip: return "foreign_clip"; default: return "primary_changed";
    }
}
inline StanceState Check(const Stance& s, uint32_t player) {
    if (!s.read) return StanceState::Unread;
    if (!player || s.model != player) return StanceState::OtherModel;
    if (!s.clip) return StanceState::NoClip;
    if (s.clip != s.mot1 && s.clip != s.mot2) return StanceState::ForeignClip;
    if (s.primary != s.mot0) return StanceState::PrimaryChanged;
    return StanceState::Live;
}

// ------------------------------------------------------------------------------------------------ who holds 2A8
enum class Kind : uint8_t { Empty, Neck, Stance, Reaction, Guard, Foreign };
inline const char* Name(Kind k) {
    switch (k) {
    case Kind::Empty: return "none"; case Kind::Neck: return "neck"; case Kind::Stance: return "stance";
    case Kind::Reaction: return "reaction"; case Kind::Guard: return "guard"; default: return "foreign";
    }
}
// Our own works (static buffers: their addresses are known whether their layer is active or not).
struct Works { uint32_t reaction = 0, guard = 0; };
inline Kind Classify(uint32_t value, uint32_t player, uint32_t image_base, const Works& works) {
    if (!value) return Kind::Empty;
    if (player && value == player + kNeckWork) return Kind::Neck;
    if (image_base && value == image_base + kStanceWorkRva) return Kind::Stance;
    if (works.reaction && value == works.reaction) return Kind::Reaction;
    if (works.guard && value == works.guard) return Kind::Guard;
    return Kind::Foreign;
}

// ------------------------------------------------------------------------------------------------ hand-back
// What a layer that borrowed 2A8 gives back: `previous` (what it found there at install) only while that is still a
// live owner, else 0 (MotionBlendOff, the native idle: MotionMove blends nothing, the neck takes the slot again by
// itself, cMot3::move does nothing until the next cMot3::set). The neck's own work is always live (a member of the same
// player); the stance work only while Check() says Live; guest is the guard layer's work while that layer is active
// (0 when it is idle). cleared: previous was not 0 and is not given back.
inline uint32_t HandBack(uint32_t previous, uint32_t player, uint32_t image_base, bool stance_live, uint32_t guest, bool& cleared) {
    cleared = false;
    if (!previous) return 0;
    if (player && previous == player + kNeckWork) return previous;
    if (image_base && previous == image_base + kStanceWorkRva && stance_live) return previous;
    if (guest && previous == guest) return previous;
    cleared = true;
    return 0;
}

// ------------------------------------------------------------------------------------------------ watchdog
// Run once per player MotionMove, after our layers ticked and before the original MotionMove. It acts only on a slot
// it can attribute: a stance work that is no longer the player's live blend, or one of our works whose layer is idle
// (an orphan). The neck, an active layer of ours and any work it cannot attribute (Zelda's cast clip, a native layer
// it does not know) are never touched.
struct Watch {
    uint32_t player = 0, slot = 0, image_base = 0;
    uint32_t frame = 0;                         // game frame counter (a stale stance must be seen on two different frames)
    Works works;
    bool reaction_active = false, guard_active = false;
    StanceState stance = StanceState::Unread;   // Check() of the stance for this player (read when the slot is the stance)
};
enum class Verdict : uint8_t { Owned, StaleStance, OrphanReaction, OrphanGuard, Foreign };
inline const char* Name(Verdict v) {
    switch (v) {
    case Verdict::Owned: return "owned"; case Verdict::StaleStance: return "stale_stance";
    case Verdict::OrphanReaction: return "orphan_reaction"; case Verdict::OrphanGuard: return "orphan_guard"; default: return "foreign";
    }
}
inline Verdict Inspect(const Watch& w) {
    switch (Classify(w.slot, w.player, w.image_base, w.works)) {
    case Kind::Empty: case Kind::Neck: return Verdict::Owned;
    case Kind::Stance: return w.stance == StanceState::Live ? Verdict::Owned : Verdict::StaleStance;
    case Kind::Reaction: return w.reaction_active ? Verdict::Owned : Verdict::OrphanReaction;
    case Kind::Guard: return w.guard_active ? Verdict::Owned : Verdict::OrphanGuard;
    default: return Verdict::Foreign;
    }
}
// A stale stance is released when the same slot value is still stale on a later frame than its first sighting (never
// on one transient frame); an orphan of ours at once. Feed every inspection; Release() true = compare-exchange 2A8 from
// `slot` to 0.
class Watchdog {
public:
    struct Stats { unsigned inspections = 0, releases = 0, stale_stance = 0, orphan_reaction = 0, orphan_guard = 0, foreign = 0; };
    bool Release(const Watch& w, Verdict& verdict) {
        ++stats_.inspections;
        verdict = Inspect(w);
        if (verdict == Verdict::Foreign) ++stats_.foreign;
        if (verdict != Verdict::StaleStance) stale_ = 0;
        if (verdict == Verdict::Owned || verdict == Verdict::Foreign) return false;
        if (verdict == Verdict::StaleStance) {
            if (stale_ != w.slot || stale_player_ != w.player) { stale_ = w.slot; stale_player_ = w.player; stale_frame_ = w.frame; return false; }
            if (w.frame == stale_frame_) return false;   // a second MotionMove of the same frame
        }
        stale_ = 0;
        return true;
    }
    // The wiring reports the outcome of a release it attempted (the compare-exchange may lose to a native write).
    void Released(Verdict v) {
        ++stats_.releases;
        if (v == Verdict::StaleStance) ++stats_.stale_stance;
        else if (v == Verdict::OrphanReaction) ++stats_.orphan_reaction;
        else if (v == Verdict::OrphanGuard) ++stats_.orphan_guard;
    }
    const Stats& stats() const { return stats_; }
private:
    uint32_t stale_ = 0, stale_player_ = 0, stale_frame_ = 0;
    Stats stats_;
};

// ------------------------------------------------------------------------------------------------ arm probe
// Read-only diagnostics of Link's arm bones. Skeleton (mortal_draw_baseline_data.h parents): chest 2; right arm 5
// (clavicle), 7, 8, 9, 10 (hand); left arm 11 (clavicle), 13, 14, 15, 16 (hand). cParts (re4-decomp model.h): world
// matrix +0x0C (3 rows of 4 floats, translation in column 3), scale +0xAC, motParts.flags +0x1C0, next part +0xF4; the
// model's first part at model+0xF4.
namespace arm {
constexpr uint32_t kFirstPart = 0xF4, kNextPart = 0xF4, kMatrix = 0x0C, kScale = 0xAC, kFlags = 0x1C0;
constexpr unsigned kChest = 2, kLast = 16;
constexpr unsigned kParts[] = {5, 7, 8, 9, 10, 11, 13, 14, 15, 16};
constexpr float kMinAxis = 0.05f, kMaxAxis = 20.f, kMaxReach = 3000.f;   // world units (Link's hand is < 1000 from his chest)
struct Part { float mat[12]{}; float scale[3]{}; uint32_t flags = 0; };
enum class Health : uint8_t { Ok, Unread, NonFinite, Collapsed, Huge, Detached };
inline const char* Name(Health h) {
    switch (h) {
    case Health::Ok: return "ok"; case Health::Unread: return "unread"; case Health::NonFinite: return "non_finite";
    case Health::Collapsed: return "collapsed"; case Health::Huge: return "huge"; default: return "detached";
    }
}
struct Report {
    Health health = Health::Unread;
    unsigned part = 0;         // the first bad arm part
    float value = 0;           // the smallest/largest axis length or the distance from the chest
    uint32_t flags = 0;        // that part's motParts.flags
    float scale[3]{};          // and its scale
};
inline bool Finite(const float* v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) if (!std::isfinite(v[i])) return false;
    return true;
}
inline Report Judge(const Part (&parts)[kLast + 1]) {
    Report r;
    const Part& chest = parts[kChest];
    if (!Finite(chest.mat, 12)) { r.health = Health::NonFinite; r.part = kChest; r.flags = chest.flags; return r; }
    for (unsigned i : kParts) {
        const Part& p = parts[i];
        r.part = i; r.flags = p.flags;
        for (unsigned k = 0; k < 3; ++k) r.scale[k] = p.scale[k];
        if (!Finite(p.mat, 12) || !Finite(p.scale, 3)) { r.health = Health::NonFinite; return r; }
        float low = 1e30f, high = 0;
        for (unsigned c = 0; c < 3; ++c) {
            const float len = std::sqrt(p.mat[c] * p.mat[c] + p.mat[4 + c] * p.mat[4 + c] + p.mat[8 + c] * p.mat[8 + c]);
            low = len < low ? len : low; high = len > high ? len : high;
        }
        if (low < kMinAxis) { r.health = Health::Collapsed; r.value = low; return r; }
        if (high > kMaxAxis) { r.health = Health::Huge; r.value = high; return r; }
        const float dx = p.mat[3] - chest.mat[3], dy = p.mat[7] - chest.mat[7], dz = p.mat[11] - chest.mat[11];
        const float reach = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(reach <= kMaxReach)) { r.health = Health::Detached; r.value = reach; return r; }
    }
    Report ok; ok.health = Health::Ok;
    return ok;
}
// Memory: bool Read(uint32_t address, void* out, size_t n). False when the chain is shorter or unreadable.
template <class Memory> bool ReadParts(Memory& m, uint32_t model, Part (&out)[kLast + 1]) {
    uint32_t node = 0;
    if (!model || !m.Read(model + kFirstPart, &node, 4)) return false;
    for (unsigned i = 0; i <= kLast; ++i) {
        if (node < 0x10000) return false;
        if (!m.Read(node + kMatrix, out[i].mat, sizeof(out[i].mat)) || !m.Read(node + kScale, out[i].scale, sizeof(out[i].scale)) ||
            !m.Read(node + kFlags, &out[i].flags, 4) || !m.Read(node + kNextPart, &node, 4)) return false;
    }
    return true;
}
}  // namespace arm
}  // namespace link_runtime::secondary_slot

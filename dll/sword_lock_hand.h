// dll/sword_lock_hand.h
#pragma once
// Blade Lock candidate 5 (docs/swordlock-cand5-2026-11-04.md; Donald, 4-nov: "hay ocasiones que aparece en el duelo con
// la pistola"): his duels ran with the handgun equipped (weapon=2), the hold lease started from idle (no native knife
// draw), so the TP blade-lock clips played with the pistol in his right hand and the Master Sword hidden.
//
// The duel reuses sword-hand-01's swap (sword_hand.h Swap, the one the Back Slice, the spin and the Ending Blow use):
// the knife slot's model info (cPlBody m_pKnife, the Master Sword) gets its draw bit 8 and the equipped weapon is hidden
// at cObjWep display level 0 (ocarina::WeaponHide, both weapon objects, any weapon; none equipped = only the sword).
// Nothing else: the equipped weapon id, the ammo and the hand models are never written.
//
// DuelHand is the duel's lease on it:
//   Begin  (the duel opens, SwkStart): Swap::Begin. Native (a lease from the knife hold: the native knife already shows
//          the sword and hides the gun) writes nothing; while the duel holds, a native knife-down seen later (bit 8
//          gone) is answered with a Begin then (late=1), so the sword shows whatever started the lock.
//   Tick   (every probe tick, decide and core frames, damage and death frames included): a pending restore retried
//          first; then EndWhy: a load (the ocarina worker's epoch moved) forgets without a write, a death, a cutscene or
//          the duel no longer holding Link hand everything back; otherwise Swap::Hold re-asserts the gun's level 0.
//   End    (the release, an abandon, the duel's end lines, the win spin's hand-over): Swap::End, which clears only the
//          bits this swap set and only while they are still exactly as it left them. Pending: Tick retries next frame.
// The win's Great Spin: the release keeps the sword through the win spin's hold; the spin's own sword_hand Begin, in the
// same Started event, comes right after this End (no frame without the sword), and a hold closed without a spin ends it.
// Memory: bool Read32(addr, out), bool Compare32(addr, expected, desired). Game thread only.
#include <cstdint>
#include <cstring>
#include "sword_hand.h"

namespace link_runtime::sword_lock::hand {
namespace shand = link_runtime::sword_hand;

struct Facts {
    bool locked = false;      // the duel holds Link: the probe Frozen with the lab's Duel stage, or the win spin's hold
    bool link_alive = true;   // life > 0 and no death demo
    bool gameplay = true;     // no event / cut / movie
    bool same_epoch = true;   // the ocarina worker's load epoch unchanged since Begin (0 = 0 with the ocarina off)
};
// Why a held sword goes back now, or nullptr to keep it. "load" forgets without a write (the models may be rebuilt).
inline bool IsLoad(const char* why) { return why && std::strcmp(why, "load") == 0; }
inline const char* EndWhy(const Facts& f) {
    return !f.same_epoch ? "load" : !f.link_alive ? "death" : !f.gameplay ? "gameplay" : !f.locked ? "unlocked" : nullptr;
}

struct Step {
    shand::Result result = shand::Result::NotHeld;
    const char* why = nullptr;   // the end reason ("retry" for a pending restore), nullptr while kept
    bool ended = false;          // the lease is over (nothing of ours left)
    bool late = false;           // a native knife-down answered: the sword shown now
};

class DuelHand {
public:
    static constexpr unsigned kMaxLate = 8;   // candidate 11: re-shows of the sword a duel answers before native wins
    bool Active() const { return active_; }
    bool Holds() const { return swap_.Held(); }
    bool Pending() const { return swap_.Pending(); }
    shand::Result begin() const { return begin_; }
    uint32_t weapon() const { return weapon_; }
    uint32_t knife_info() const { return swap_.KnifeInfo(); }
    unsigned guns_hidden() const { return swap_.GunsHidden(); }
    unsigned reasserts() const { return swap_.Reasserts(); }
    unsigned late() const { return late_; }

    // weapon: the equipped id (GLOBAL+0x4FC0) for the log; foreign_model: the ocarina's model in the slot (0 = none).
    template <class Memory>
    shand::Result Begin(Memory& m, uint32_t player_slot, uint64_t lease, uint32_t weapon, uint32_t foreign_model = 0) {
        if (active_) return shand::Result::AlreadyHeld;
        active_ = true; lease_ = lease; weapon_ = weapon; foreign_ = foreign_model; late_ = 0;
        begin_ = swap_.Begin(m, player_slot, lease, foreign_model);
        watch_native_ = begin_ == shand::Result::Native;
        // nothing of ours to hold or to retry: the lease stays only to answer a native knife-down
        if (!swap_.Held() && !watch_native_) active_ = false;
        return begin_;
    }

    template <class Memory>
    Step Tick(Memory& m, uint32_t player_slot, const Facts& f, bool native_knife) {
        Step s;
        if (!active_) return s;
        const char* why = EndWhy(f);
        if (IsLoad(why)) return End(m, player_slot, why, native_knife);
        if (swap_.Pending()) {
            s.result = swap_.End(m, player_slot, native_knife); s.why = "retry";
            if (s.result != shand::Result::Pending) Finish(s);
            return s;
        }
        if (why) return End(m, player_slot, why, native_knife);
        if (swap_.Held()) {
            s.result = swap_.Hold(m, player_slot);
            // Candidate 11 (Donald's cand10 duels 7 and 10: the sword shown from idle, a native knife-down cleared bit 8 on the
            // next tick, "native wins" gave the pistol back, and he fought the whole lock with the handgun): while the duel
            // still holds Link, a foreign knife-down is answered like one after a native Begin (the late path shows the sword
            // again on the next tick), at most kMaxLate times a duel; a Lost (rebuilt player or models) still ends it.
            if (s.result == shand::Result::Foreign && f.locked && late_ < kMaxLate) watch_native_ = true;
            else if (s.result == shand::Result::Foreign || s.result == shand::Result::Lost) watch_native_ = false;   // native wins
            if (!swap_.Held() && !watch_native_) Finish(s);
            return s;
        }
        if (watch_native_) {   // the native knife showed it at Begin: answer a knife-down while the duel holds
            const shand::Result r = swap_.Begin(m, player_slot, lease_, foreign_);
            if (r == shand::Result::Shown) { s.result = r; s.late = true; ++late_; watch_native_ = false; }
            else if (r == shand::Result::Native || r == shand::Result::InvalidMemory) s.result = shand::Result::Native;
            else { s.result = r; watch_native_ = false; if (!swap_.Held()) Finish(s); }
        }
        return s;
    }

    template <class Memory>
    Step End(Memory& m, uint32_t player_slot, const char* why, bool native_knife) {
        Step s; s.why = why;
        if (!active_) return s;
        if (IsLoad(why)) {   // forgotten, never a write into rebuilt models
            const bool had = swap_.Held();
            swap_.Abandon(); s.result = had ? shand::Result::Lost : shand::Result::NotHeld; Finish(s);
            return s;
        }
        if (!swap_.Held()) { Finish(s); return s; }   // nothing of ours (native at Begin, or native took it back)
        s.result = swap_.End(m, player_slot, native_knife);
        if (s.result != shand::Result::Pending) Finish(s);
        return s;
    }

    void Abandon() { swap_.Abandon(); active_ = watch_native_ = false; }

private:
    shand::Swap swap_;
    bool active_ = false, watch_native_ = false;
    uint64_t lease_ = 0;
    uint32_t weapon_ = 0, foreign_ = 0;
    unsigned late_ = 0;
    shand::Result begin_ = shand::Result::NotHeld;
    void Finish(Step& s) { active_ = watch_native_ = false; s.ended = true; }
};
}  // namespace link_runtime::sword_lock::hand

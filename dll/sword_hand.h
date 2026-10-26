// dll/sword_hand.h
#pragma once
// Sword in the hand during the mod's sword actions (sword-hand-01, 19-oct; Donald's video: the TP Back Slice with the
// handgun equipped kept the HANDGUN in Link's right hand through the roll and the twirl slash).
//
// Since shield-guard-01 the Back Slice, the Shield Bash, the spin / Great Spin and the Ending Blow (and its leap) can
// start from locomotion (dispatch StartFrom::NormalIdle / NormalMove), outside the knife routine, so nothing native
// draws the knife: the right hand keeps the equipped gun and the Master Sword (the knife slot) stays hidden.
//
// What the native knife draw does (re4-decomp src/game/pl_knife.cpp knife_r3_ready10 frame 4, and its UHD bytes):
//   setWepTrans(pl, 0)    cObjWep::setDisp(1, 0) on Wep->m_pWep (RVA 353902..353914): hides the gun at level 1;
//   Body->setKnife(true)  inlined in UHD as `or dword [Body(+7E4)->m_pKnife(+2C) + 4], 8` (RVA 353914..353927):
//                         bit 3 of the knife model info's flags is what the draw tests (RVA 2EAA3F `test cl,8`).
// The knife-down undoes both (setKnife(false) = `and [..+4], ~8` at RVA 353DC2, then setDisp(1, 1)). The right hand
// model is NOT swapped: with a gun equipped the hand stays the weapon's grip hand (setRightHand(1)), exactly as when the
// knife is drawn natively, and SwordStep (link_runtime_core.h, worker thread) hides the sheathed sword while bit 8 is on.
//
// This swap does the same two things, without touching level 1 (the knife's and interrupt()'s) or the hand models:
//   Begin  (clip start, the lease's Started event): knife info bit 8 already on (a clip from the knife hold) = the
//          native knife already shows the sword and hides the gun: nothing is written (Native). Otherwise the gun is
//          hidden at level 0 with ocarina::WeaponHide (bit 0x04 of cObjWep::flag, the ocarina's proven route, both
//          weapon objects, any weapon; none equipped = nothing to hide) and then bit 8 is set on the knife info with a
//          compare-exchange. A failure rolls back what this call wrote (fail closed).
//   Hold   (every game frame of the clip): identity (player, Body, knife info, its model pointer) unchanged; the gun's
//          level 0 re-asserted. If something native cleared bit 8 meanwhile (setKnife(false): interrupt(), a knife
//          routine, death), the native state wins: the gun is shown again now and the swap ends (Foreign).
//   End    (clip end, any reason, or a frame where the action is no longer valid: damage, death, scene, lease lost):
//          bit 8 is cleared only where this object set it, only while it is still set and the identity is unchanged,
//          and not while the native knife routine owns the slot (its own knife-down clears it); then the gun's level 0
//          is set back where WeaponHide cleared it. A failed read or compare-exchange keeps the lease (Pending) and the
//          caller retries next frame. A rebuilt player / Body / knife info / weapon object is forgotten without a write.
// Memory: bool Read32(addr, out), bool Compare32(addr, expected, desired). Game thread only.
#include <cstddef>
#include <cstdint>
#include "link_runtime_core.h"
#include "ocarina_weapon.h"

namespace link_runtime::sword_hand {
namespace layout {
constexpr uint32_t kBody = 0x7E4, kKnifeInfo = 0x2C;         // player -> cPlBody -> m_pKnife (cModelInfo)
constexpr uint32_t kList = 0x15C, kNext = 0x14;              // player's model infos, cModelInfo::pList
constexpr uint32_t kFlags = 0x4, kModel = 0xC;               // cModelInfo be_flag, model_addr
constexpr uint32_t kVisible = 0x8;                           // be_flag bit 3: drawn (setKnife)
constexpr unsigned kMaxList = 16;
constexpr uint32_t kRoutine = 0xFC;                          // cPlayer r_no_0..3 bytes
}  // namespace layout

enum class Result : uint8_t {
    Shown,          // Begin: bit 8 set by us (and the gun hidden at level 0 where one was shown)
    Native,         // Begin: the knife info already shows the sword (knife routine): nothing written or held
    NoKnife,        // Begin: Body / m_pKnife missing, not in the model list, or the slot shows a foreign model
    AlreadyHeld,    // Begin while holding: nothing written
    InvalidMemory,  // Begin: a read failed; nothing written
    RaceLost,       // Begin: a compare-exchange failed; this call's writes were rolled back
    Held,           // Hold: still ours
    Foreign,        // Hold: bit 8 cleared by native code; the gun was shown again and the swap ended (or is pending)
    Lost,           // Hold / End: player, Body, knife info or its model changed; forgotten without a write
    Restored,       // End: everything ours handed back
    Pending,        // End / Hold: a read or compare-exchange failed; call End again next frame
    NotHeld,        // End without a lease
};
inline const char* Name(Result r) {
    switch (r) {
    case Result::Shown: return "shown"; case Result::Native: return "native"; case Result::NoKnife: return "no_knife";
    case Result::AlreadyHeld: return "already_held"; case Result::InvalidMemory: return "invalid_memory";
    case Result::RaceLost: return "race_lost"; case Result::Held: return "held"; case Result::Foreign: return "foreign";
    case Result::Lost: return "lost"; case Result::Restored: return "restored"; case Result::Pending: return "pending";
    default: return "not_held";
    }
}
// The knife routine (r_no_0 0, r_no_1 0x0B) owns the knife slot: its knife-down clears bit 8 itself.
inline bool KnifeRoutine(uint32_t routine) { return (routine & 0xFF) == 0 && ((routine >> 8) & 0xFF) == 0x0B; }
// Per player frame (the MotionMove detour, every routine): why a held swap must end now, or nullptr to keep it.
// The sword action stops being valid with the player unreadable, out of gameplay (death, load), pending damage, the
// action's lease gone, or the dispatch no longer owning that lease (a native damage / event routine took the player).
inline const char* EndReason(bool player, bool gameplay, bool damaged, bool action_lease, bool dispatch_owns) {
    return !player ? "player" : !gameplay ? "gameplay" : damaged ? "damage" : !action_lease ? "lease" : !dispatch_owns ? "dispatch" : nullptr;
}

class Swap {
public:
    bool Held() const { return held_ || pending_; }
    bool Pending() const { return pending_; }
    uint64_t Lease() const { return lease_; }
    unsigned GunsHidden() const { return static_cast<unsigned>(weapon_.Count()); }
    unsigned Reasserts() const { return weapon_.Reasserts(); }
    uint32_t KnifeInfo() const { return info_; }

    // foreign_model: a model the knife slot may be lent to (the ocarina's, 0 = none); with it there, nothing is done.
    template <class Memory>
    Result Begin(Memory& m, uint32_t player_slot, uint64_t lease, uint32_t foreign_model = 0) {
        namespace L = layout;
        if (held_ || pending_) return Result::AlreadyHeld;
        uint32_t player = 0, body = 0, info = 0, flags = 0, model = 0;
        if (!m.Read32(player_slot, player) || !IsPointer(player) || !m.Read32(player + L::kBody, body))
            return Result::InvalidMemory;
        if (!IsPointer(body) || !m.Read32(body + L::kKnifeInfo, info) || !IsPointer(info)) return Result::NoKnife;
        const int listed = Listed(m, player, info);
        if (listed < 0) return Result::InvalidMemory;
        if (!listed) return Result::NoKnife;
        if (!m.Read32(info + L::kFlags, flags) || !m.Read32(info + L::kModel, model)) return Result::InvalidMemory;
        if (!IsPointer(model) || (foreign_model && model == foreign_model)) return Result::NoKnife;
        if (flags & L::kVisible) return Result::Native;
        // Gun first (the native draw's order), then the sword; the sword write rolls the gun back on a lost race.
        const auto hidden = weapon_.Hide(m, player_slot);
        if (hidden == ocarina::WeaponResult::InvalidMemory) return Result::InvalidMemory;
        if (hidden == ocarina::WeaponResult::RaceLost) return Result::RaceLost;
        player_ = player; body_ = body; info_ = info; model_ = model; lease_ = lease;
        uint32_t again = 0;
        if (!m.Read32(info + L::kFlags, again) || again != flags || !m.Compare32(info + L::kFlags, flags, flags | L::kVisible)) {
            // Roll the gun back now; if that cannot complete this frame the lease stays pending (End retries it), so
            // a hidden gun is never abandoned.
            held_ = true; knife_ours_ = false;
            if (End(m, player_slot, false) != Result::Pending) Drop();
            return Result::RaceLost;
        }
        held_ = true; knife_ours_ = true; pending_ = false;
        return Result::Shown;
    }

    // Every game frame while Held(). Pending: the caller should call End.
    template <class Memory>
    Result Hold(Memory& m, uint32_t player_slot) {
        namespace L = layout;
        if (pending_) return Result::Pending;
        if (!held_) return Result::NotHeld;
        const int same = Same(m, player_slot);
        if (same < 0) return Result::Held;                       // unreadable this frame: wait
        if (!same) {
            // Rebuilt player: forgotten without a write. Same player, other Body / knife info / model: the knife is not
            // ours any more, but the gun (its own identity checks) is still shown again.
            const Result r = End(m, player_slot, false);
            return r == Result::Pending ? Result::Pending : Result::Lost;
        }
        uint32_t flags = 0;
        if (!m.Read32(info_ + L::kFlags, flags)) return Result::Held;
        if (knife_ours_ && !(flags & L::kVisible)) {
            knife_ours_ = false;                                 // native setKnife(false): it owns the slot now
            const Result r = End(m, player_slot, false);
            return r == Result::Pending ? Result::Pending : Result::Foreign;
        }
        weapon_.Hold(m, player_slot);
        return Result::Held;
    }

    // native_knife: the knife routine owns the slot now (bit 8 is then left to its knife-down).
    template <class Memory>
    Result End(Memory& m, uint32_t player_slot, bool native_knife) {
        namespace L = layout;
        if (!held_ && !pending_) return Result::NotHeld;
        uint32_t player = 0;
        if (!m.Read32(player_slot, player) || !player) { pending_ = true; held_ = false; return Result::Pending; }
        if (player != player_) { weapon_.Abandon(); Drop(); return Result::Lost; }
        const int same = Same(m, player_slot);
        if (same < 0) { pending_ = true; held_ = false; return Result::Pending; }
        if (!same) {
            // The knife info or its model changed; the weapon objects keep their own identity checks.
            knife_ours_ = false;
        }
        if (knife_ours_ && !native_knife) {
            bool done = false;
            for (int i = 0; i < 4 && !done; ++i) {
                uint32_t flags = 0;
                if (!m.Read32(info_ + L::kFlags, flags)) break;
                if (!(flags & L::kVisible)) { done = true; break; } // cleared already (native)
                done = m.Compare32(info_ + L::kFlags, flags, flags & ~L::kVisible);
            }
            if (!done) { pending_ = true; held_ = false; return Result::Pending; }
        }
        knife_ours_ = false;
        bool foreign = !same;
        if (weapon_.Held()) {
            const auto w = weapon_.End(m, player_slot);
            if (w == ocarina::WeaponResult::Pending) { pending_ = true; held_ = false; return Result::Pending; }
            if (w == ocarina::WeaponResult::PartlyForeign) foreign = true;
        }
        Drop();
        return foreign ? Result::Lost : Result::Restored;
    }

    void Abandon() { weapon_.Abandon(); Drop(); }

private:
    ocarina::WeaponHide weapon_;
    bool held_ = false, pending_ = false, knife_ours_ = false;
    uint32_t player_ = 0, body_ = 0, info_ = 0, model_ = 0;
    uint64_t lease_ = 0;

    void Drop() { held_ = pending_ = knife_ours_ = false; player_ = body_ = info_ = model_ = 0; lease_ = 0; }
    // 1 the info is in the player's model list, 0 not, -1 unreadable / looped.
    template <class Memory> static int Listed(Memory& m, uint32_t player, uint32_t info) {
        uint32_t node = 0;
        if (!m.Read32(player + layout::kList, node)) return -1;
        for (unsigned i = 0; IsPointer(node); ++i) {
            if (i == layout::kMaxList) return -1;
            if (node == info) return 1;
            if (!m.Read32(node + layout::kNext, node)) return -1;
        }
        return 0;
    }
    // 1 player, Body, knife info and its model are the ones Begin saw; 0 changed; -1 unreadable.
    template <class Memory> int Same(Memory& m, uint32_t player_slot) const {
        uint32_t player = 0, body = 0, info = 0, model = 0;
        if (!m.Read32(player_slot, player)) return -1;
        if (player != player_) return 0;
        if (!m.Read32(player + layout::kBody, body)) return -1;
        if (body != body_) return 0;
        if (!m.Read32(body + layout::kKnifeInfo, info)) return -1;
        if (info != info_) return 0;
        if (!m.Read32(info + layout::kModel, model)) return -1;
        return model == model_ ? 1 : 0;
    }
};
}  // namespace link_runtime::sword_hand

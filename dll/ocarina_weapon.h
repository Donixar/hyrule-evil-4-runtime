// dll/ocarina_weapon.h
#pragma once
#include <cstddef>
#include <cstdint>
#include "link_runtime_core.h"
// Hides the equipped weapon in Link's hands while the ocarina pose owns him (12-oct in-game
// finding: the pose raised the equipped handgun to his mouth). Offline attribution only; not
// tested in game. Pins: link_runtime::native_pins::kOcarinaWeapon and
// scripts/ocarina_weapon_pins.py; doc: docs/ocarina-runtime-2026-10-12.md.
//
// UHD facts (GC names from re4-decomp include/pl_wep.h, src/game/objWep.cpp, pl_wep.cpp):
//  - cPlayer::Wep (cPlWep) is player+0x7D8; m_pWep +0x34 and m_pWepHand +0x38 are the weapon
//    cObjWep objects (the second is the rifle/launcher/grenade hand part). Both may be 0.
//    cPlayer::setNoSuspend (RVA 27BF10) walks exactly these two.
//  - cObjWep::flag is the byte at +0x354 (GC 0x350). F_DISP_0/1/2 are bits 0x04/0x08/0x10;
//    setDisp(level, on) (RVA 33E9B0, thiscall) sets or clears one of them.
//  - cObjWep::move (RVA 33F320; every weapon vtable shares it, none overrides it) recomputes
//    the model's be_flag bit 2 (displayed) EVERY frame at RVA 33F36A: on only if all three
//    F_DISP bits are on. ModelTrans (RVA 2EB60B) does not draw a model without be_flag bit 2.
//    So writing be_flag bit 2 directly would be undone next frame; an F_DISP bit is persistent.
//  - Level 1 is the knife's (knife ready hides the gun with setDisp(1,0) and shows it back),
//    level 2 is PlSetHand's (cPlWep::setTrans, RVA 283940) and interrupt() (RVA 33F0B0, event,
//    damage, weapon change) turns 1 and 2 back on. Level 0 (bit 0x04) is written only through
//    setDisp by grenade/launcher weapon routines and scene scripts, and by the constructor
//    (RVA 33F1A3: flag = 0x1C). None of those run while the ocarina clip owns the player.
// So this takes level 0: clears bit 0x04 with a compare-exchange where it was set, re-asserts
// it each frame while held (defensive: no per-frame writer was found), and on End sets it back
// only where this object cleared it and only while it is still clear. Nothing else is written.
//
// Identity: a weapon change destroys the old objects and creates new ones (constructor:
// flag 0x1C, all shown), so End writes a slot only while player, Wep, the object pointer and
// its vtable are the ones Hide saw; otherwise that slot is forgotten without a write (a new
// object is already shown). A failed read keeps the lease and asks for a retry (Pending).
// Memory: bool Read32(addr, out), bool Compare32(addr, expected, desired). Game-thread only.
namespace link_runtime::ocarina {
namespace weapon_layout {
constexpr uint32_t kPlayerWep = 0x7D8, kWepModel = 0x34, kWepHand = 0x38;
constexpr uint32_t kFlagWord = 0x354;   // cObjWep::flag (byte 0 of this aligned dword)
constexpr uint32_t kDisp0 = 0x04;       // F_DISP_0
}  // namespace weapon_layout

enum class WeaponResult {
    Hidden,         // Hide: every equipped weapon object with bit 0x04 set now has it clear (0..2 slots)
    NoWeapon,       // Hide: Wep or both objects are 0, or both already hidden by the game; holds nothing
    Restored,       // End: every slot this object cleared is shown again (or was shown by the game)
    PartlyForeign,  // End: some slot changed identity and was forgotten without a write
    Pending,        // End: a read or compare-exchange failed; nothing lost, call End again
    AlreadyHeld,    // Hide while holding: nothing written
    NotHeld,        // End without a lease
    InvalidMemory,  // Hide: a read failed or a pointer was invalid; nothing left written
    RaceLost,       // Hide: a compare-exchange failed; this call's writes were rolled back
};

class WeaponHide {
public:
    static constexpr size_t kSlots = 2;
    bool Held() const { return held_; }
    size_t Count() const { size_t n = 0; for (const auto& s : slots_) n += s.ours ? 1 : 0; return n; }
    unsigned Reasserts() const { return reasserts_; }

    template <class Memory>
    WeaponResult Hide(Memory& memory, uint32_t player_slot) {
        namespace W = weapon_layout;
        if (held_) return WeaponResult::AlreadyHeld;
        uint32_t player = 0, wep = 0;
        if (!memory.Read32(player_slot, player) || !IsPointer(player) ||
            !memory.Read32(player + W::kPlayerWep, wep)) return WeaponResult::InvalidMemory;
        if (!wep) return WeaponResult::NoWeapon;
        if (!IsPointer(wep)) return WeaponResult::InvalidMemory;
        Slot found[kSlots] = {};
        const uint32_t fields[kSlots] = {W::kWepModel, W::kWepHand};
        for (size_t i = 0; i < kSlots; ++i) {
            uint32_t object = 0, vtable = 0, word = 0;
            if (!memory.Read32(wep + fields[i], object)) return WeaponResult::InvalidMemory;
            if (!object || (i == 1 && object == found[0].object)) continue;
            if (!IsPointer(object) || !memory.Read32(object, vtable) || !IsPointer(vtable) ||
                !memory.Read32(object + W::kFlagWord, word)) return WeaponResult::InvalidMemory;
            if (!(word & W::kDisp0)) continue;   // hidden at level 0 by the game: not ours
            found[i] = Slot{fields[i], object, vtable, true};
        }
        if (!found[0].ours && !found[1].ours) return WeaponResult::NoWeapon;
        uint32_t again = 0;
        if (!memory.Read32(player_slot, again) || again != player) return WeaponResult::InvalidMemory;
        for (size_t i = 0; i < kSlots; ++i) {
            if (!found[i].ours) continue;
            if (!Clear(memory, found[i].object)) {
                for (size_t j = 0; j < i; ++j) if (found[j].ours) Set(memory, found[j].object);
                return WeaponResult::RaceLost;
            }
        }
        held_ = true; player_ = player; wep_ = wep; reasserts_ = 0;
        for (size_t i = 0; i < kSlots; ++i) slots_[i] = found[i];
        return WeaponResult::Hidden;
    }

    // Every frame while Held(): a slot whose identity changed is forgotten (no write); a slot
    // whose bit 0x04 came back on is cleared again. A failed read waits for the next frame.
    template <class Memory>
    void Hold(Memory& memory, uint32_t player_slot) {
        namespace W = weapon_layout;
        if (!held_) return;
        uint32_t wep = 0;
        if (!Same(memory, player_slot, wep)) return;
        for (auto& s : slots_) {
            if (!s.ours) continue;
            const int id = Identity(memory, wep, s);
            if (id < 0) continue;
            if (id == 0) { s = Slot{}; continue; }
            uint32_t word = 0;
            if (memory.Read32(s.object + W::kFlagWord, word) && (word & W::kDisp0) && Clear(memory, s.object)) ++reasserts_;
        }
    }

    template <class Memory>
    WeaponResult End(Memory& memory, uint32_t player_slot) {
        namespace W = weapon_layout;
        if (!held_) return WeaponResult::NotHeld;
        uint32_t player = 0, wep = 0;
        // Player slot empty (a load in progress): wait. Player or Wep rebuilt (load, continue):
        // their weapon objects are new and shown, so nothing is written.
        if (!memory.Read32(player_slot, player) || !player) return WeaponResult::Pending;
        if (player != player_) { Drop(); return WeaponResult::PartlyForeign; }
        if (!memory.Read32(player + W::kPlayerWep, wep)) return WeaponResult::Pending;
        if (wep != wep_) { Drop(); return WeaponResult::PartlyForeign; }
        bool foreign = false;
        for (auto& s : slots_) {
            if (!s.ours) continue;
            const int id = Identity(memory, wep, s);
            if (id < 0) return WeaponResult::Pending;
            if (id == 0) { s = Slot{}; foreign = true; continue; }
            if (!Set(memory, s.object)) return WeaponResult::Pending;
            s = Slot{};
        }
        Drop();
        return foreign ? WeaponResult::PartlyForeign : WeaponResult::Restored;
    }

    void Abandon() { Drop(); }

private:
    struct Slot { uint32_t field = 0, object = 0, vtable = 0; bool ours = false; };
    Slot slots_[kSlots]{};
    bool held_ = false;
    uint32_t player_ = 0, wep_ = 0;
    unsigned reasserts_ = 0;

    void Drop() { held_ = false; player_ = wep_ = 0; for (auto& s : slots_) s = Slot{}; }
    template <class Memory> bool Same(Memory& memory, uint32_t player_slot, uint32_t& wep) {
        uint32_t player = 0;
        return memory.Read32(player_slot, player) && player == player_ &&
               memory.Read32(player + weapon_layout::kPlayerWep, wep) && wep == wep_;
    }
    // 1 same object (pointer and vtable), 0 changed, -1 unreadable.
    template <class Memory> static int Identity(Memory& memory, uint32_t wep, const Slot& s) {
        uint32_t object = 0, vtable = 0;
        if (!memory.Read32(wep + s.field, object)) return -1;
        if (object != s.object) return 0;
        if (!memory.Read32(object, vtable)) return -1;
        return vtable == s.vtable ? 1 : 0;
    }
    // Bit 0x04 of the flag byte only; other bits and bytes keep what the game wrote. A
    // concurrent change of another bit retries a bounded number of times.
    template <class Memory> static bool Clear(Memory& memory, uint32_t object) {
        for (int i = 0; i < 4; ++i) {
            uint32_t word = 0;
            if (!memory.Read32(object + weapon_layout::kFlagWord, word)) return false;
            if (!(word & weapon_layout::kDisp0)) return true;
            if (memory.Compare32(object + weapon_layout::kFlagWord, word, word & ~weapon_layout::kDisp0)) return true;
        }
        return false;
    }
    template <class Memory> static bool Set(Memory& memory, uint32_t object) {
        for (int i = 0; i < 4; ++i) {
            uint32_t word = 0;
            if (!memory.Read32(object + weapon_layout::kFlagWord, word)) return false;
            if (word & weapon_layout::kDisp0) return true;   // the game showed it already
            if (memory.Compare32(object + weapon_layout::kFlagWord, word, word | weapon_layout::kDisp0)) return true;
        }
        return false;
    }
};
}  // namespace link_runtime::ocarina

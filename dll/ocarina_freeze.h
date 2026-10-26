// dll/ocarina_freeze.h
#pragma once
#include <cstddef>
#include <cstdint>
#include "link_runtime_core.h"
// Freezes the world while Link plays the ocarina, OoT style: every work of the em list (Ganados,
// animals, bosses, enemy weapons, the em set objects) and Ashley stop where they are and STAY
// DRAWN; Link, his weapons and effects keep running. Offline attribution only, not tested in game.
// Evidence: docs/ocarina-runtime-2026-10-12.md ("Congelacion por Stop_flg") and
// scripts/ocarina_stop_pins.py.
//
// Mechanism (12-oct, replaces the STA_SUSPEND recipe, which hid the Ganados in game because
// ModelTrans 2EB5E0 does not register suspended models for drawing):
// pG->Stop_flg (pG+0x170) bits SPF_EM 0x20000000 and SPF_SUBCHAR 0x00001000, the same bits the
// game's own STOP MODE sets (gameStopMove, game.cpp:1569) and that every Stop_flg saver covers.
//  - cEmMgr::move (RVA 1B01B0) skips emMove for every work while SPF_EM is set and then moves only
//    the partner, unless SPF_SUBCHAR is set too (1B01F2). emMove is a work's whole frame: AI,
//    damage, motion, skeleton (ShapeMove), its sounds and its yarare display.
//  - Nothing else reads those bits: in all of .text the only test of 0x20000000 against pG+0x170
//    is 1B01C8, and of 0x1000 only 1AF7DE (emMove, partner) and 1B01F2. The draw paths (ModelTrans,
//    372740, 3751B0), shadows and lights test STA_SUSPEND and be_flag, never Stop_flg: the works
//    keep their last pose on screen.
//  - The player is not frozen: emMove returns for pPL and gameMainLoop moves him under SPF_PL and
//    STA_SUSPEND only (23E71F). His weapons are ObjMgr objects and SPF_OBJ is not touched.
// This header hooks nothing and calls no game code: it reads and compare-exchanges one word
// (Stop_flg) through the injected Memory: bool Read32(addr, out), bool Compare32(addr, exp, des).
// It never writes Status_flg, be_flag or any model, so visibility cannot change through it.
// Game-thread only.
//
// Ownership: a bit already set at Begin is not ours (neither set nor cleared). Every native writer
// that stops the game saves Stop_flg, writes 0xFFFFFFFF (or all but 0x4000) and restores the saved
// word later (sscrn, events, up cut, messages, doors, movies, block loads, option screen); all of
// those values include SPF_PL, so End waits (Pending) while SPF_PL is set and clears our bits after
// the restore. A room load zeroes Stop_flg (gameRoomMemInit memclr, 23AEC9), so a load seen while
// we hold the lease with none of our bits left drops it without writing (Abandoned).
namespace link_runtime::ocarina {
namespace freeze_layout {
constexpr uint32_t kRoutine = 0x20;                  // low byte: Rno0, 3 = MainLoop
constexpr uint32_t kRoomId = 0x4FAC;                 // u16 room_id (GC 0x4F9C + 0x10), RVA 23ACD7
constexpr uint32_t kStatus0 = 0x501C, kStatus1 = 0x5020, kStatus2 = 0x5024, kStopFlg = 0x170;
constexpr uint32_t kSpfEm = 0x20000000;              // SPF_EM, bit 2: cEmMgr::move skips every work
constexpr uint32_t kSpfSubchar = 0x00001000;         // SPF_SUBCHAR, bit 19: the partner too
constexpr uint32_t kSpfPl = 0x10000000;              // SPF_PL, bit 3: someone stopped the player / saved Stop_flg
constexpr uint32_t kWorldStop = kSpfEm | kSpfSubchar;
constexpr uint32_t kSuspend = 0x10000000;            // STA_SUSPEND in Status_flg[1]: guard only, never written
constexpr uint32_t kSscrnRequest = 0x04000000;       // STA_SSCRN_REQUEST, bit 69: inventory/map/shop open
constexpr uint32_t kStaEvent = 0x00001000, kStaDiedemo = 0x00100000;  // Status_flg[0]
}  // namespace freeze_layout

enum class FreezeResult {
    Frozen,          // Begin: our bits set (possibly none, when the game had already set both)
    Restored,        // End cleared every bit that Begin set
    PartlyForeign,   // End done; some of our bits had been cleared by game code and were left alone
    Pending,         // End not possible yet (saver/event/death demo/inventory/loading/player 0): no writes, lease kept
    Abandoned,       // room/player identity changed or a load wiped our bits: nothing written, lease dropped
    AlreadyActive,   // Begin while a lease is held: nothing written
    NotActive,       // End without a lease
    Refused,         // Begin: a guard was set; RefusedAt() says which
    InvalidMemory,   // a read or pointer check failed; nothing written
    RaceLost,        // Begin: Stop_flg changed under the compare-exchange; nothing written
};
enum class EndPhase { Done, Pending, Abandoned };
inline EndPhase PhaseOf(FreezeResult r) {
    return r == FreezeResult::Pending ? EndPhase::Pending :
           r == FreezeResult::Abandoned ? EndPhase::Abandoned : EndPhase::Done;
}
// RefusedAt() codes.
enum RefuseReason : int { kRefuseNone = -1, kRefuseRoutine = 100, kRefuseEvent, kRefuseDiedemo,
                          kRefuseInventory, kRefuseStopPl, kRefuseSuspend };

class WorldFreeze {
public:
    bool Active() const { return active_; }
    uint32_t Owned() const { return owned_; }        // bits this lease set and still owns
    uint32_t Lost() const { return lost_; }          // bits game code cleared while we held them
    size_t TargetCount() const { return Bits(owned_); }
    int RefusedAt() const { return refused_at_; }

    // Sets SPF_EM and SPF_SUBCHAR where they are clear. Refuses with no writes outside MainLoop,
    // during an event, death demo, open inventory, a native suspension, or with SPF_PL (a saver or
    // the game holds Stop_flg). epoch: the runtime's load epoch (0 = not tracked).
    template <class Memory>
    FreezeResult Begin(Memory& memory, uint32_t player_slot, uint32_t global_slot, uint32_t epoch = 0) {
        using namespace freeze_layout;
        if (active_) return FreezeResult::AlreadyActive;
        refused_at_ = kRefuseNone;
        uint32_t player = 0, global = 0, routine = 0, room = 0, status0 = 0, status1 = 0, status2 = 0, stop = 0;
        if (!memory.Read32(player_slot, player) || !IsPointer(player) ||
            !memory.Read32(global_slot, global) || !IsPointer(global) ||
            !memory.Read32(global + kRoutine, routine) || !memory.Read32(global + kRoomId, room) ||
            !memory.Read32(global + kStatus0, status0) || !memory.Read32(global + kStatus1, status1) ||
            !memory.Read32(global + kStatus2, status2) || !memory.Read32(global + kStopFlg, stop))
            return FreezeResult::InvalidMemory;
        if ((routine & 0xFF) != 3) return Refuse(kRefuseRoutine);
        if (status0 & kStaEvent) return Refuse(kRefuseEvent);
        if (status0 & kStaDiedemo) return Refuse(kRefuseDiedemo);
        if (status2 & kSscrnRequest) return Refuse(kRefuseInventory);
        if (stop & kSpfPl) return Refuse(kRefuseStopPl);
        if (status1 & kSuspend) return Refuse(kRefuseSuspend);
        uint32_t now_player = 0, now_global = 0;
        if (!memory.Read32(player_slot, now_player) || now_player != player ||
            !memory.Read32(global_slot, now_global) || now_global != global) return FreezeResult::InvalidMemory;
        const uint32_t owned = kWorldStop & ~stop;
        if (owned && !memory.Compare32(global + kStopFlg, stop, stop | owned)) return FreezeResult::RaceLost;
        player_slot_ = player_slot; global_slot_ = global_slot; player_ = player; global_ = global;
        room_ = room & 0xFFFF; epoch_ = epoch; owned_ = owned; lost_ = 0;
        in_transition_ = identity_lost_ = false; active_ = true;
        return FreezeResult::Frozen;
    }

    // Every game frame while Active(). Detects a room load the caller might not see: a different
    // global, player or room id, or a trip outside MainLoop (seen here, or as a new load epoch)
    // that comes back with none of our bits left (the load zeroed Stop_flg). A trip that comes back
    // with our bits (the option screen restores its saved word) keeps the lease. A bit game code
    // cleared outside a saver window is forgotten (never cleared later: it may be the game's again).
    template <class Memory>
    void Poll(Memory& memory, uint32_t epoch = 0) {
        using namespace freeze_layout;
        if (!active_ || identity_lost_) return;
        uint32_t player = 0, global = 0, routine = 0, room = 0, stop = 0;
        if (!memory.Read32(global_slot_, global) || global != global_) { identity_lost_ = true; return; }
        if (!memory.Read32(player_slot_, player) || !memory.Read32(global + kRoutine, routine) ||
            !memory.Read32(global + kRoomId, room)) return;  // unreadable this frame: judge later
        if ((player && player != player_) || (room & 0xFFFF) != room_) { identity_lost_ = true; return; }
        if (epoch && epoch_ && epoch != epoch_) in_transition_ = true;
        if (epoch) epoch_ = epoch;
        if ((routine & 0xFF) != 3 || !player) { in_transition_ = true; return; }
        if (!memory.Read32(global + kStopFlg, stop) || (stop & kSpfPl)) return;  // a saver holds the word
        if (in_transition_) {
            in_transition_ = false;
            if (owned_ && !(stop & owned_)) { identity_lost_ = true; return; }
        }
        const uint32_t gone = owned_ & ~stop;
        owned_ &= ~gone; lost_ |= gone;
    }

    // Clears only bits Begin set, only while they are still set; every other bit keeps the value
    // the game gave it. Pending (no writes, lease kept) while a saver holds Stop_flg (SPF_PL), an
    // event, death demo, inventory, a non-MainLoop routine or a 0 player pointer is seen, or the
    // compare-exchange keeps losing: call again later. Abandoned (no writes) after a load.
    template <class Memory>
    FreezeResult End(Memory& memory, uint32_t epoch = 0) {
        using namespace freeze_layout;
        if (!active_) return FreezeResult::NotActive;
        Poll(memory, epoch);
        if (identity_lost_) { Drop(); return FreezeResult::Abandoned; }
        uint32_t player = 0, routine = 0, status0 = 0, status2 = 0, stop = 0;
        if (!memory.Read32(player_slot_, player) || !memory.Read32(global_ + kRoutine, routine) ||
            !memory.Read32(global_ + kStatus0, status0) || !memory.Read32(global_ + kStatus2, status2) ||
            !memory.Read32(global_ + kStopFlg, stop))
            return FreezeResult::Pending;
        if (!player || (routine & 0xFF) != 3 || in_transition_ || (stop & kSpfPl) ||
            (status0 & (kStaEvent | kStaDiedemo)) || (status2 & kSscrnRequest)) return FreezeResult::Pending;
        bool foreign = lost_ != 0;
        for (int attempt = 0;; ++attempt) {
            const uint32_t ours = stop & owned_;
            if (ours != owned_) foreign = true;
            if (!ours) break;
            if (memory.Compare32(global_ + kStopFlg, stop, stop & ~ours)) break;
            if (attempt == 3 || !memory.Read32(global_ + kStopFlg, stop)) return FreezeResult::Pending;
            if (stop & kSpfPl) return FreezeResult::Pending;
        }
        Drop();
        return foreign ? FreezeResult::PartlyForeign : FreezeResult::Restored;
    }

    // Lease forgotten without writing (process teardown when memory is gone).
    void Abandon() { Drop(); }

private:
    bool active_ = false, in_transition_ = false, identity_lost_ = false;
    int refused_at_ = kRefuseNone;
    uint32_t player_slot_ = 0, global_slot_ = 0, player_ = 0, global_ = 0, room_ = 0, epoch_ = 0;
    uint32_t owned_ = 0, lost_ = 0;

    static size_t Bits(uint32_t v) { size_t n = 0; for (; v; v &= v - 1) ++n; return n; }
    FreezeResult Refuse(int at) { refused_at_ = at; return FreezeResult::Refused; }
    void Drop() {
        active_ = in_transition_ = identity_lost_ = false;
        player_ = global_ = player_slot_ = global_slot_ = room_ = epoch_ = owned_ = 0;
    }
};

// The freeze lease as the runtime holds it: one Release entry for every way a session can end
// (session release, the dispatch lease end, teardown) and one Service call per game frame on
// either handler path, which retries a pending release or polls a held freeze. Release is
// idempotent: a second call with nothing held returns NotActive and writes nothing.
// Bug 2 of 12-oct (exit clip ended, freeze never released) is closed by calling Release from the
// lease end itself, not only from a later session tick.
class FreezeOwner {
public:
    bool Active() const { return freeze_.Active(); }
    bool Pending() const { return pending_; }
    const WorldFreeze& Freeze() const { return freeze_; }
    // True when a Release/Service result should be logged as "freeze end" (finished now).
    static bool Finished(FreezeResult r) { return r != FreezeResult::NotActive && PhaseOf(r) != EndPhase::Pending; }

    template <class Memory>
    FreezeResult Begin(Memory& memory, uint32_t player_slot, uint32_t global_slot, uint32_t epoch) {
        if (freeze_.Active()) return FreezeResult::AlreadyActive;   // includes a pending release
        pending_ = false;
        return freeze_.Begin(memory, player_slot, global_slot, epoch);
    }
    template <class Memory>
    FreezeResult Release(Memory& memory, uint32_t epoch) {
        if (!freeze_.Active()) { pending_ = false; return FreezeResult::NotActive; }
        const FreezeResult r = freeze_.End(memory, epoch);
        pending_ = PhaseOf(r) == EndPhase::Pending;
        return r;
    }
    template <class Memory>
    FreezeResult Service(Memory& memory, uint32_t epoch) {
        if (pending_) return Release(memory, epoch);
        if (freeze_.Active()) freeze_.Poll(memory, epoch);
        return FreezeResult::NotActive;
    }
    void Abandon() { freeze_.Abandon(); pending_ = false; }

private:
    WorldFreeze freeze_;
    bool pending_ = false;
};
}  // namespace link_runtime::ocarina

#pragma once
// Sword actions vs the native action-button prompts (offer-fix-01, 3-nov-2026). Pure: the works queued at the
// arbiter's entry, classified, and the native-priority rule the sword actions ask. No hooks, no writes.
//
// The bug (Donald, 3-nov: "with an item on the floor for E TAKE the Back Slice never comes out, nor the Shield
// Bash"). Through 3.5.0 every sword action except the Ending Blow gave way to ANY queued offer (+0x40) and to ANY
// prompt shown (+0x42), every frame (spin_attack_runtime.h SwordPriority). Pinned bio4.exe, RVAs:
//   * cActionButton::move 0x18AB40 clears +0x42 (0x18AB51), walks ot[15..0] and sets +0x42 = 1 (0x18ABC3) on the
//     first work checkPLStatus 0x18A930 accepts, BEFORE checkButton (thunk 0x1F96 -> 0x18A300) tests the key: +0x42
//     is "a prompt is up this frame" (GC act_btn.h m_active_flag), not "an offer fired". With "E TAKE" on screen
//     both bytes are set every frame Link stands there.
//   * A DISP_A_NORMAL (btn 1) work fires only on KEY_A 0x80000 (TRG, ON with NO_TRG, or STA_PL_ACTION, which
//     cPlayer::checkCtrl raises on that press, or a fire press while a WEP_SET_IGNORE work set STA_ACT_DONT_FIRE).
//     The knife key 0x800 is never read or masked by the arbiter: the offer does not take the sword's input.
//     The fire path (3-nov review): STA_ACT_DONT_FIRE is +0x501C 0x200000, set when a WEP_SET_IGNORE work is queued
//     (0x18AAD6) or accepted with a weapon up (0x18A985, which only such works pass); the knife's fire checks
//     (0x35451B -> 0x2818A0, 0x354512 -> 0x2819D0) then raise STA_PL_ACTION (0x2818E0, 0x281A0E) instead of slashing.
//     In room 0x20D a fire press may fire a btn-1 work too (0x18A4C8). None of the actions this rule lets through
//     reads a fire press (the tap aborts on one, shield_bash.h Tap; the charge is unsafe with one, SwordArbiter), so
//     no fire press is shared; the Ending Blow, a fire press, keeps its own rule (SwordEndingPriority).
//   * During a sword lease Link's routine is the dispatch's Event routine (r0 5): checkPLStatus's actCheck
//     (thunk 0x54A2 -> 0x27D800, r0 +0xFC must be 0) refuses every work without ENFORCE_EXEC, so a plain prompt can
//     neither show nor fire until the clip ends; it cannot cancel the lease either.
//   * +0x41 is the tail's latch of the prompt lock (0x18A0E1: GLOBAL+0x170 bit 0x100), not an offer.
// So a queued E prompt (pickup 0x28 from the scene areas 0x2BB4F2, flags 0/0x80/0x100; kick 7, flags 1; door 0x10;
// ladder, window, talk...) only ever needs the E key. The pickup's flags were checked (3-nov review): the scene
// areas' one btn-1 set 0x2BB4F2 takes the area's act_type as kind and edi as flags, which 0x2BB350..0x2BB4CC only
// ever sets to 0, 0x80 or 0x100 (GC sce_at.cpp, SCEAT_ID_ITEM: act_color and DOOR_COLOR, never ENFORCE_EXEC); its
// other set (0x2BB450, DISP_X hide, btn 6) binds as before. The rule below, SwordOverNativeOffer=1 (missing = 1), lets
// the sword actions start and run through such prompts; E (or aiming) still wins as before (the runtime's `keys`),
// and E still takes the item or kicks. Binding works keep the 3.5.0 priority:
//   * any other button (L+R / A+B dodges, A or B rapid, gacha, stick, X: the QTEs and escapes, GC em2b/em2c/emrock),
//   * ENFORCE_EXEC (shown outside the player's normal state: scripted jumps of r117/r204, mash prompts),
//   * NO_EXEC (a work that stops the selection);
// and so do the prompt lock (+0x41), the arbiter's early-outs and unreadable works (fail closed). A scan of the 155
// set() call sites (3-nov) found the dodges (0x25 GUARD, 0x13 STOOP, 0x1A JUMP) on buttons 3/4 and the other timed
// prompts on other buttons or with ENFORCE_EXEC; the btn-1 works without ENFORCE_EXEC / NO_EXEC are the player's own
// choices (talk, check, jump out/down/over, push, kick, go up, stand, open, operation, rescue, ride, climb, catch, get
// off, pick up, execute, get down).
// SwordOverNativeOffer=0 is 3.5.0 bit for bit: any queued offer or prompt up is native priority.
#include <cstdint>

#include "mortal_draw_context.h"

namespace link_runtime::sword_offer {
namespace context = link_runtime::mortal_draw::context;
namespace layout {
// ACT_WORK (0x18 bytes) at manager +0x44, 8 of them (set 0x18AA10 refuses a ninth): +0xC kind, +0xF btn, +0x14 flags.
constexpr uint32_t works = 0x44, work_size = 0x18, w_kind = 0x0C, w_btn = 0x0F, w_flags = 0x14;
constexpr unsigned kMaxWorks = 8;
constexpr uint8_t btn_action = 1;                     // DISP_A_NORMAL: the KEY_A prompt ("E TAKE", "E KICK", ...)
constexpr uint32_t flag_enforce_exec = 0x02, flag_no_exec = 0x20;
}  // namespace layout

struct Work { uint8_t kind = 0, btn = 0; uint32_t flags = 0; };
// A work the sword actions may play through: a plain action-button prompt.
inline bool Yields(const Work& w) {
    return w.btn == layout::btn_action && !(w.flags & (layout::flag_enforce_exec | layout::flag_no_exec));
}
struct Facts {
    bool readable = false;
    uint8_t queued = 0, yielding = 0, binding = 0;
    uint8_t first_kind = 0xFF;                            // the first queued work (log)
    uint8_t binding_kind = 0xFF, binding_btn = 0;         // the first binding work (log)
    uint32_t binding_flags = 0;
};
inline void Add(Facts& f, const Work& w) {
    if (!f.queued) f.first_kind = w.kind;
    ++f.queued;
    if (Yields(w)) { ++f.yielding; return; }
    if (!f.binding) { f.binding_kind = w.kind; f.binding_btn = w.btn; f.binding_flags = w.flags; }
    ++f.binding;
}
// The `count` works queued now (count = +0x40 read with them). Arbiter entry: before the mod queues its own NULL-func
// prompts (Mortal Draw, the lab's), so every work read is native. A failed read leaves readable = false.
template <class Reader> bool Read(Reader& reader, uint32_t image_base, uint8_t count, Facts& f) {
    f = Facts{};
    const uint32_t manager = image_base + context::layout::action_manager_rva;
    for (uint32_t i = 0; i < count && i < layout::kMaxWorks; ++i) {
        uint8_t raw[layout::work_size]{};
        if (!reader.Read(manager + layout::works + i * layout::work_size, raw, sizeof(raw))) { f = Facts{}; return false; }
        Work w;
        w.kind = raw[layout::w_kind]; w.btn = raw[layout::w_btn];
        w.flags = uint32_t(raw[layout::w_flags]) | uint32_t(raw[layout::w_flags + 1]) << 8 |
                  uint32_t(raw[layout::w_flags + 2]) << 16 | uint32_t(raw[layout::w_flags + 3]) << 24;
        Add(f, w);
    }
    f.readable = true;
    return true;
}
// Native priority of a sword action this frame (the arbiter's sample `s`, its works `f`). `over`: SwordOverNativeOffer.
// The caller adds its keys (KEY_A or the aim key held) on top, as before.
inline bool Priority(const context::Sample& s, const Facts& f, bool over) {
    if (!s.readable || !s.after_arbiter || s.lock || s.actions_disabled) return true;
    if (!s.offers && !s.active) return false;
    // 3.5.0, or works not read for this very count: fail closed. A prompt up with nothing native queued is the mod's
    // own NULL-func offer (the runtime's ClaimOwnOffer already clears it), never binding.
    if (!over || !f.readable || f.queued != (s.offers < layout::kMaxWorks ? s.offers : layout::kMaxWorks)) return true;
    return f.binding != 0;
}
// Offers queued after the arbiter, read again at the clip's start (SwordDecide): `count` = +0x40 then, `f` its works.
inline bool LateBlocks(uint8_t count, const Facts& f, bool over) {
    if (!count) return false;
    if (!over || !f.readable || f.queued != (count < layout::kMaxWorks ? count : layout::kMaxWorks)) return true;
    return f.binding != 0;
}
// The Blade Lock lab's win spin (lab DLL, its keys included): the rule above whatever SwordOverNativeOffer says, and
// of the keys only aiming. A held E (the duel's mash key, often still down after the flash) does not cut it: no plain
// prompt can fire while the lease's Event routine runs, so the kick waits for the clip's end.
inline bool LabWinPriority(const context::Sample& s, const Facts& f) {
    return Priority(s, f, true) || (s.on & context::layout::key_ready) != 0;
}
// 3.5.0 would have given way here and the rule above did not: the log's "sword_offer pass" line (spin_attack_runtime.h
// SwordOfferPass, over = the key, or 1 for the lab win spin; a held E or aim key then filters it out).
inline bool PlayedThrough(const context::Sample& s, const Facts& f, bool over) {
    return over && Priority(s, f, false) && !Priority(s, f, true);
}
}  // namespace link_runtime::sword_offer

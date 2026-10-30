#pragma once
// Mortal Draw contextual trigger: pure decision over an injected sample.
// Native action-button offers (kick, door, pickup, jump, ladder, Zelda/Ashley
// interactions) always win. Pinned bio4.exe SHA 19aed4af...; RVAs, base 0x400000.
//
// Sample point (enforced): the cActionButton arbiter 0x18AB40 (thiscall, ECX =
// manager 0x8502C0). Its entry sees this frame's queued offers (+0x40) and the
// lock (+0x41) before they are cleared; its exit (tail 0x18A0A0 clears the
// list and +0x40, re-latches +0x41) sees +0x42 = an offer fired with this
// frame's press. Read both halves (ReadPreArbiter / ReadPostArbiter) and decide
// after the arbiter. On the activation frame the caller masks KEY_A out of
// Key_btn_trg (MaskPress), so a reader later in the frame (player routine, a
// late offer) cannot consume the same edge. Readers that run before the
// arbiter in the frame are not covered: their order must be checked in the log.
#include <cstddef>
#include <cstdint>

namespace link_runtime::mortal_draw::context {
namespace layout {
// Key_btn_on / Key_btn_trg (64-bit each). checkButton 0x18A300 reads ON at
// 0x862F08; helpers 0x565CE0 (thunk 0x65C3) and 0x189F70 (thunk 0xEC0F) read TRG.
constexpr uint32_t input_on_rva = 0x862F08, input_trg_rva = 0x862F10;
// checkButton jump table 0x18A7A4: button 1 (kick offer 0x6148A, door offer
// 0x1CD01A use button 1) tests 0x80000 via TRG (flag 2) or ON (flag 0x10).
// re4_tweaks pad.h names 0x80000 KEY_A: the logical action button, whatever
// physical key/pad button the player mapped to it.
constexpr uint64_t key_action = 0x80000ULL;
// Button 1 TRG path 0x18A4A7 pushes a second 64-bit mask (bit 48, 0x10000 in the
// high dword) after 0x80000, but both callees (0x565CE0 TRG, 0x565D10 ON) only
// AND [ebp+8..+0xF]: the second mask is dead in UHD, no alias of KEY_A.
constexpr uint64_t button1_second_mask = 0x0001000000000000ULL;
// Must not be held: KEY_KAMAE (aim), KEY_FIRE, KEY_KAMAE_KNIFE (knife ready,
// ON 0x800 in knife set 0x354410). Donald's Space is the knife hold, not this.
constexpr uint64_t key_ready = 0x10ULL, key_fire = 0x80ULL, key_knife_ready = 0x800ULL;
// cActionButton manager: offers queued +0x40 (set 0x18AA10), lock +0x41,
// fired-this-frame +0x42 (cleared at arbiter entry 0x18AB51, set 0x18ABC3).
constexpr uint32_t action_manager_rva = 0x8502C0, offers = 0x40, lock = 0x41, active = 0x42;
constexpr uint32_t arbiter_rva = 0x18AB40, arbiter_tail_rva = 0x18A0A0;
// Arbiter early-outs (no offer can fire): GLOBAL [0x806F3C] +0x170 & 0x100 and
// +0x501C & 0x100000 (0x18AB5A, 0x18AB6A).
constexpr uint32_t global_slot_rva = 0x806F3C, global_flags_a = 0x170, global_flags_b = 0x501C;
constexpr uint32_t global_mask_a = 0x100, global_mask_b = 0x100000;
}  // namespace layout

struct Sample {
    bool readable = false;
    bool after_arbiter = false;  // both halves read around 0x18AB40 this frame
    bool pre_ok = false;         // N-3: ReadPreArbiter succeeded for this sample (required by the post half)
    uint64_t frame = 0;          // monotonic game-frame counter supplied by the caller (0 allowed)
    uint64_t on = 0, trg = 0;
    uint8_t offers = 0, lock = 0;  // arbiter entry
    uint8_t active = 0;            // arbiter exit
    bool actions_disabled = false; // arbiter early-out flags
    bool player_free = false;    // injected: player routine free (no ladder/jump/damage/grab/event)
    bool sword_sheathed = false; // injected: sheath/hand model state (knife info bit 8 clear)
};
enum class Block { None, Unreadable, WrongSamplePoint, StaleFrame, NoTarget, NativeOffer, ActionsDisabled, PlayerBusy, SwordDrawn, GunRaised };
// mask_trg: on this frame the caller must clear KEY_A from Key_btn_trg (MaskPress).
struct Decision {
    bool available = false, press = false, mask_trg = false;
    bool buffered = false;  // the press is (or came from) the buffer below
    Block block = Block::None;
};

// One activation per physical press: after any action-button press, whether it
// started Mortal Draw or belonged to a native offer, the button must be seen
// released before another activation.
// Press buffer (12-oct, build 08 in game: parry -> step behind -> press gave 0 md_press
// because Link was still in parry recovery): a press blocked ONLY by our own gates
// (PlayerBusy, NoTarget) is kept for kBufferFrames of game time (60 Hz frames, 300 ms) and
// fires once on the first available frame. A native offer/active/lock, the arbiter
// early-outs, a drawn sword or a raised gun cancel it: native actions still win. The
// physical press is spent either way (release required, as before).
class Trigger {
    uint64_t last_frame_ = 0;
    bool started_ = false, need_release_ = false, buffered_ = false;
    double buffer_start_ = 0;
public:
    static constexpr double kBufferFrames = 18.0;  // 300 ms at 60 Hz
    // A Mortal Draw already running or requested: a press then is not buffered into a second one.
    void DropBuffer() { buffered_ = false; }
    Decision Step(const Sample& s, bool target_ready) { return Step(s, target_ready, double(s.frame)); }
    // `now`: game time in 60 Hz frames (game_clock Elapsed60; equals the counter at 60 FPS).
    Decision Step(const Sample& s, bool target_ready, double now) {
        Decision d;
        if (!s.readable) { d.block = Block::Unreadable; return d; }
        if (!s.after_arbiter) { d.block = Block::WrongSamplePoint; return d; }
        // A save load rewinds the counter (zt frame_gap delta=-3576): that frame still fails closed, then the
        // trigger follows the new timeline instead of staying stale until the old value comes back.
        if (started_ && s.frame <= last_frame_) { if (s.frame < last_frame_) last_frame_ = s.frame; d.block = Block::StaleFrame; return d; }
        started_ = true; last_frame_ = s.frame;
        const bool held = (s.on & layout::key_action) != 0;
        const bool pressed = (s.trg & layout::key_action) != 0;
        if (!held && !pressed) need_release_ = false;
        if (s.offers || s.active || s.lock) d.block = Block::NativeOffer;
        else if (s.actions_disabled) d.block = Block::ActionsDisabled;
        else if (!s.player_free) d.block = Block::PlayerBusy;
        else if (!s.sword_sheathed || (s.on & layout::key_knife_ready)) d.block = Block::SwordDrawn;
        else if (s.on & (layout::key_ready | layout::key_fire)) d.block = Block::GunRaised;
        else if (!target_ready) d.block = Block::NoTarget;
        d.available = d.block == Block::None;
        const bool ours = d.block == Block::PlayerBusy || d.block == Block::NoTarget;
        if (buffered_ && (!ours && !d.available)) buffered_ = false;                      // native/weapon wins
        if (buffered_ && now - buffer_start_ > kBufferFrames + 1e-3) buffered_ = false;   // expired
        if (buffered_ && now < buffer_start_ - 1e-3) buffered_ = false;                    // clock went back
        if (pressed && !need_release_) {
            d.press = d.mask_trg = d.available;
            need_release_ = true;  // consumed here or by the native action
            buffered_ = !d.available && ours;
            if (buffered_) { buffer_start_ = now; d.buffered = true; }
        } else if (buffered_ && d.available) {
            buffered_ = false; d.press = d.buffered = true;
            d.mask_trg = pressed;  // no edge this frame: nothing to mask
        }
        return d;
    }
};

// Arbiter entry: queued offers, lock and early-out flags (cleared by the tail).
template <class Reader> bool ReadPreArbiter(Reader& reader, uint32_t image_base, Sample& s) {
    s = Sample{};
    uint8_t manager[2]{}; uint32_t global = 0, a = 0, b = 0;
    if (!reader.Read(image_base + layout::action_manager_rva + layout::offers, manager, 2) ||
        !reader.Read(image_base + layout::global_slot_rva, &global, 4) || !global ||
        !reader.Read(global + layout::global_flags_a, &a, 4) || !reader.Read(global + layout::global_flags_b, &b, 4)) return false;
    s.offers = manager[0]; s.lock = manager[1];
    s.actions_disabled = (a & layout::global_mask_a) || (b & layout::global_mask_b);
    s.pre_ok = true;
    return true;
}
// Arbiter exit: input and whether a native offer fired with this frame's press.
// Only valid after ReadPreArbiter succeeded for the same frame: without the
// entry facts (offers, lock, early-outs) native priority would fail open (N-3).
template <class Reader> bool ReadPostArbiter(Reader& reader, uint32_t image_base, Sample& s) {
    s.readable = s.after_arbiter = false;
    if (!s.pre_ok) return false;
    uint64_t on = 0, trg = 0; uint8_t active = 0;
    if (!reader.Read(image_base + layout::input_on_rva, &on, 8) || !reader.Read(image_base + layout::input_trg_rva, &trg, 8) ||
        !reader.Read(image_base + layout::action_manager_rva + layout::active, &active, 1)) return false;
    s.on = on; s.trg = trg; s.active = active;
    s.readable = s.after_arbiter = true;
    return true;
}
// Activation frame only: removes KEY_A from Key_btn_trg so no later reader in
// this frame sees the edge. Key_btn_on stays (held state is not an edge).
template <class Memory> bool MaskPress(Memory& memory, uint32_t image_base) {
    uint32_t low = 0;
    if (!memory.Read(image_base + layout::input_trg_rva, &low, 4)) return false;
    low &= ~uint32_t(layout::key_action);
    return memory.Write(image_base + layout::input_trg_rva, &low, 4);
}
}  // namespace link_runtime::mortal_draw::context

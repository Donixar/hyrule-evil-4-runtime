// dll/sword_parts.h
#pragma once
// Part-visibility guard for the sword clips (spin-fix-01, Donald 18-oct night: "perdí mi brazo ... haciendo el giro",
// with the HANDGUN equipped, a spin started from the knife hold, from_knife=1; the arm came back after the spin).
//
// What is known (re4-decomp src/game/pl_leon.cpp, pl_knife.cpp, pl_wep.cpp; the live list read 18-oct night):
//  - Link's parts hang from player+0x15C, linked through +0x14 (cPlLeon::setModel order: body, extras, knife info,
//    face, head, hair/back sword (be_flag 0x40), then the right and left hand models). A part is drawn while bit 3
//    (0x8) of its flags word (+0x4) is set; SwordStep (link_runtime_core.h) keeps the back sword's bit the opposite
//    of the knife's, and nothing else of ours writes these words.
//  - The right hand is its own model and is REPLACED with the weapon's grip hand (setRightHand(1), Body->pWepHand)
//    when a gun is equipped; the left hand model is replaced by setLeftHand for the grips. The knife stance only
//    hides the gun object (setWepTrans(pl, 0) = cObjWep disp level 1) and shows the knife info.
//  - A sword clip started from the knife hold bypasses the native knife-down (mortal_draw_dispatch.h EndOwned writes
//    0x00030B00 at the end so the native code restores the gun): whatever the native code hides on the way, nothing
//    shows again until that knife-down.
// The exact part that vanished was not observed (no live sample of that spin), so this guard is generic and logged:
//   Begin (clip start): snapshot every part (node pointer, flags). Visible ones, except the knife info and the back
//                       sword (their visibility is the knife's and SwordStep's business), are protected.
//   Step  (every game frame of the clip): a protected part whose node is still in the list but whose draw bit went
//                       off gets it back (compare-exchange); each restore is reported once per part (`sword_parts
//                       restore index=.. node=.. flags=..`). A part that left the list (a hand model replaced) is
//                       reported (`gone`) and never written.
//   End   (clip end):   every word this guard wrote and that is still exactly what it wrote gets the draw bit cleared
//                       again (the native state it found); anything changed since is left alone. Only nodes in the
//                       player's list at that moment (bughunt-01: a part restored and then replaced, or a list rebuilt
//                       by a reload, kept its old word, and End wrote into a node that was no longer Link's).
// Memory: bool Read32(addr, out), bool Compare32(addr, expected, desired). Game thread only.
#include "link_runtime_core.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace link_runtime::sword_parts {
namespace layout {
constexpr uint32_t kList = 0x15C, kNext = 0x14, kFlags = 0x4, kModel = 0xC;
constexpr uint32_t kDraw = 0x8, kHairMark = 0x40;
constexpr unsigned kMaxParts = 16;
}  // namespace layout

struct Snapshot {
    uint32_t node[layout::kMaxParts] = {}, flags[layout::kMaxParts] = {};
    unsigned count = 0;
};
// The player's part list (false: unreadable, a loop or more than kMaxParts).
template <class Memory> bool Read(Memory& m, uint32_t player, Snapshot& out) {
    out = Snapshot{};
    if (!IsPointer(player)) return false;
    uint32_t node = 0;
    if (!m.Read32(player + layout::kList, node)) return false;
    while (IsPointer(node)) {
        if (out.count == layout::kMaxParts) return false;
        for (unsigned i = 0; i < out.count; ++i) if (out.node[i] == node) return false;
        if (!m.Read32(node + layout::kFlags, out.flags[out.count])) return false;
        out.node[out.count++] = node;
        if (!m.Read32(node + layout::kNext, node)) return false;
    }
    return true;
}
// Knife info and back sword positions (SwordStep's: costume 4 is the special list).
inline unsigned KnifeIndex(unsigned costume) { return costume == 4 ? 1 : 2; }
inline unsigned BackIndex(unsigned costume) { return costume == 4 ? 4 : 5; }

enum class Change : uint8_t { Restored, Gone, RaceLost };
struct Report { Change change = Change::Restored; unsigned index = 0; uint32_t node = 0, flags = 0; };

class Guard {
public:
    static constexpr unsigned kReports = layout::kMaxParts;
    bool Active() const { return active_; }
    unsigned Restores() const { return restores_; }
    // Clip start. False (inactive) when the list cannot be read.
    template <class Memory> bool Begin(Memory& m, uint32_t player, unsigned costume) {
        *this = Guard{};
        if (!Read(m, player, start_)) return false;
        player_ = player;
        for (unsigned i = 0; i < start_.count; ++i) {
            const bool special = i == KnifeIndex(costume) || i == BackIndex(costume) || (start_.flags[i] & layout::kHairMark);
            protect_[i] = !special && (start_.flags[i] & layout::kDraw);
        }
        active_ = true;
        return true;
    }
    // Every game frame of the clip. Fills up to kReports changes (each part reported once); returns how many.
    template <class Memory> unsigned Step(Memory& m, uint32_t player, Report* out) {
        unsigned n = 0;
        if (!active_ || player != player_) return 0;
        Snapshot now;
        if (!Read(m, player, now)) return 0;
        for (unsigned i = 0; i < start_.count; ++i) {
            if (!protect_[i]) continue;
            unsigned at = layout::kMaxParts;
            for (unsigned j = 0; j < now.count; ++j) if (now.node[j] == start_.node[i]) { at = j; break; }
            if (at == layout::kMaxParts) {
                protect_[i] = false; written_[i] = 0;   // not Link's part any more: never written again (End)
                if (out && n < kReports) out[n++] = Report{Change::Gone, i, start_.node[i], 0};
                continue;
            }
            const uint32_t f = now.flags[at];
            if (f & layout::kDraw) continue;
            const uint32_t want = f | layout::kDraw;
            if (!m.Compare32(start_.node[i] + layout::kFlags, f, want)) {
                if (out && n < kReports && !reported_[i]) out[n++] = Report{Change::RaceLost, i, start_.node[i], f};
                reported_[i] = true;
                continue;
            }
            written_[i] = want; ++restores_;
            if (out && n < kReports && !reported_[i]) out[n++] = Report{Change::Restored, i, start_.node[i], f};
            reported_[i] = true;
        }
        return n;
    }
    // Clip end (any reason). Returns how many words were handed back. Only nodes in the player's list now (an
    // unreadable list hands nothing back: the part stays shown rather than a foreign word being written).
    template <class Memory> unsigned End(Memory& m) {
        unsigned n = 0;
        Snapshot now;
        if (active_ && Read(m, player_, now)) {
            for (unsigned i = 0; i < start_.count; ++i) {
                if (!written_[i]) continue;
                bool listed = false;
                for (unsigned j = 0; j < now.count && !listed; ++j) listed = now.node[j] == start_.node[i];
                if (!listed) continue;
                uint32_t f = 0;
                if (m.Read32(start_.node[i] + layout::kFlags, f) && f == written_[i] &&
                    m.Compare32(start_.node[i] + layout::kFlags, f, f & ~layout::kDraw)) ++n;
            }
        }
        *this = Guard{};
        return n;
    }
    const Snapshot& Start() const { return start_; }

private:
    Snapshot start_;
    bool protect_[layout::kMaxParts] = {}, reported_[layout::kMaxParts] = {};
    uint32_t written_[layout::kMaxParts] = {};
    uint32_t player_ = 0;
    unsigned restores_ = 0;
    bool active_ = false;
};
// "0:9 1:9 2:1 ..." (index:flags) for the log.
inline int Format(char* buf, size_t size, const Snapshot& s) {
    if (!buf || !size) return 0;
    int n = 0; buf[0] = 0;
    for (unsigned i = 0; i < s.count; ++i) {
        const int k = std::snprintf(buf + n, size - size_t(n), "%s%u:%X", i ? " " : "", i, s.flags[i]);
        if (k < 0 || size_t(n + k) >= size) break;
        n += k;
    }
    return n;
}
}  // namespace link_runtime::sword_parts

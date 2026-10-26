// dll/ocarina_mount.h
#pragma once
#include <cstddef>
#include <cstdint>
#include "link_runtime_core.h"
// Puts the ocarina in Link's right hand by borrowing the knife slot, route (a) of
// docs/ocarina-mount-probe-2026-10-11.md (offline attribution; not tested in game).
//
// UHD facts this relies on (pins: scripts/ocarina_mount_pins.py):
//  - cPlBody is player+0x7E4 (GC 0x794 + 0x50); m_pKnife is Body+0x2C, a cModelInfo
//    that cPlLeon::setModel (RVA 354960) creates from pl00 member 0xD and appends to the
//    player's model list (player+0x15C, next +0x14).
//  - setKnife is inlined: or/and of bit 8 in info+4 (e.g. RVA 353914 on, 353DC2 off);
//    the GC matrix-diagonal version does not exist in UHD.
//  - ModInfoMgr.create (RVA 25E780) only sets info+0xC (model), +0x10 (TPL), be_flag
//    bit 2 when the BIN has shape data, and the bound box: getBoundingBox (RVA 25D120,
//    cdecl (bin, info+0x38)) reads the BIN's own positions (+0x30, count +0x38) and
//    writes six floats at info+0x44..0x5B (its dst+0x0C..+0x23). The draw (RVA 2EAA3F)
//    skips infos without bit 8 and re-reads +0xC and +0x10 every frame.
//  - The draw takes the material count and table from the model's own header (RVA
//    2EAEDF mov eax,[ebp-E4] = info+0xC; 2EAEE5 movzx ecx,word [eax+1A]; 2EAEE9 mov ebx,
//    [eax+1C]); nothing in the info is sized by the knife's material count.
// So the mount writes the words the game owns (model, TPL, box, bit 8) on the info the
// game already uses, and puts back exactly what it found.
//
// Order: mount writes model, TPL and box while bit 8 is still clear (nothing drawn),
// then sets bit 8; unmount clears bit 8 first, then puts TPL, box and model back. Every
// write is a compare-exchange; a lost race rolls back what this call wrote.
//
// Hooks nothing. Memory supplies bool Read32(addr, out) and bool Compare32(addr,
// expected, desired); Native supplies bool CalcModelAddr(bin), bool CalcTplAddr(tpl)
// (UHD calcModelAddr RVA 25CB50 / calcTplAddr RVA 25CCF0, cdecl, one argument; both are
// idempotent in-place relocations) and bool BoundingBox(bin, uint32_t out[6]) (UHD
// getBoundingBox RVA 25D120 into a 0x24-byte buffer; out = its words +0x0C..+0x23).
// Game-thread only.
//
// Integration duties (Task 10), not done here:
//  - SwordStep (link_runtime_core.h) hides the sheathed sword (back info) while the
//    knife info has bit 8. While Mounted() it must not run, or it hides the back sword:
//    skip it when KnifeSlotHolds(asset) is true.
//  - Mount only after the session sheathed the sword (bit 8 clear) and while the freeze
//    holds; unmount before the freeze ends. The player's cModel carries be_flag 0x800
//    during the freeze, and this info is drawn as part of it, so it needs no exemption.
namespace link_runtime::ocarina {
namespace mount_layout {
constexpr uint32_t kBody = 0x7E4, kKnifeInfo = 0x2C;      // player -> cPlBody -> m_pKnife
constexpr uint32_t kModelList = 0x15C, kNext = 0x14;      // cModel::pModelInfo, cModelInfo::pList
constexpr uint32_t kFlags = 0x4, kModel = 0xC, kTpl = 0x10;
constexpr uint32_t kVisible = 0x8, kShape = 0x2;          // info+4: shown (setKnife), shape data
constexpr uint32_t kBinFlags = 0x20, kBinRelocated = 0x100000, kBinShape = 0x2C, kBinVersion = 0x3C;
constexpr uint32_t kVersionA = 0x20010801, kVersionB = 0x20030818;
constexpr uint32_t kSkeletonWord = 0x18;                  // bytes 18..19 compared (weight records, 119 bones);
                                                          // 1A..1B = material count, the BIN's own
constexpr uint32_t kGroupWord = 0x28;                     // bytes 2A..2B, knife: 01 00
constexpr uint32_t kBox = 0x44, kBoxWords = 6;            // info+0x44..0x5B: getBoundingBox output
constexpr uint32_t kAssetAlign = 32;                      // calcModelAddr wants 32-byte aligned parts
constexpr unsigned kMaxList = 16;
// BIN header words calcModelAddr turns into pointers (0 allowed).
constexpr uint32_t kBinPointers[] = {0x00, 0x0C, 0x10, 0x14, 0x1C, 0x30, 0x34, 0x48, 0x4C};
}  // namespace mount_layout

enum class MountResult {
    Mounted,         // Mount wrote model, TPL, box and bit 8
    Restored,        // Unmount put every word back
    AlreadyMounted,  // Mount while this object holds a mount: nothing written
    NotMounted,      // Unmount without a mount
    NoPlayer,        // player slot empty or not a pointer
    NoKnife,         // Body or m_pKnife missing, or m_pKnife not in the player's model list
    SwordReady,      // knife info has bit 8 (sword in hand): nothing written
    ShapeModel,      // knife info has shape data (bit 2): not the slot this was attributed on
    AssetNotReady,   // asset not prepared, or the slot already shows this asset
    HeaderMismatch,  // ocarina BIN bytes 18..19 or 2A..2B differ from the knife's
    Foreign,         // Unmount: model/TPL/box not ours and not the original; nothing written, lease dropped
    IdentityChanged, // Unmount: player, Body or m_pKnife changed; nothing written, lease dropped
    InvalidMemory,   // a read failed or a pointer was invalid; nothing left written
    RaceLost,        // a compare-exchange failed; this call's writes were rolled back
};

struct OcarinaAsset {
    uint32_t bin = 0, tpl = 0;
    uint32_t box[mount_layout::kBoxWords] = {};   // the ocarina's own getBoundingBox output
    bool ready = false;
};

inline bool FiniteFloatWord(uint32_t w) { return (w & 0x7F800000u) != 0x7F800000u; }

inline bool MountPointer(uint32_t v) { return IsPointer(v); }

// Relocates the ocarina BIN/TPL (already copied into game-reachable memory that lives as
// long as the DLL) with the game's own routines, then checks what the draw will read.
template <class Memory, class Native>
OcarinaAsset PrepareAsset(Memory& memory, Native& native, uint32_t bin, uint32_t bin_size, uint32_t tpl) {
    namespace L = mount_layout;
    OcarinaAsset out;
    if (!MountPointer(bin) || !MountPointer(tpl) || bin % L::kAssetAlign || bin_size < 0x50 ||
        bin_size > 0x100000 || bin + bin_size < bin) return out;
    if (!native.CalcModelAddr(bin) || !native.CalcTplAddr(tpl)) return out;
    uint32_t flags = 0, shape = 0, version = 0, marker = 0, count = 0;
    if (!memory.Read32(bin + L::kBinFlags, flags) || !(flags & L::kBinRelocated) ||
        !memory.Read32(bin + L::kBinShape, shape) || shape != 0 ||
        !memory.Read32(bin + L::kBinVersion, version) || (version != L::kVersionA && version != L::kVersionB) ||
        !memory.Read32(tpl, marker) || marker != 0 || !memory.Read32(tpl + 4, count) || count == 0 || count > 64)
        return out;
    for (uint32_t offset : L::kBinPointers) {
        uint32_t p = 0;
        if (!memory.Read32(bin + offset, p)) return out;
        if (p && (p < bin || p >= bin + bin_size)) return out;
    }
    uint32_t box[L::kBoxWords] = {};
    if (!native.BoundingBox(bin, box)) return out;   // reads relocated pointers: after CalcModelAddr
    for (uint32_t w : box) if (!FiniteFloatWord(w)) return out;
    for (uint32_t i = 0; i < L::kBoxWords; ++i) out.box[i] = box[i];
    out.bin = bin; out.tpl = tpl; out.ready = true;
    return out;
}

// True while the knife info shows this asset. Task 10 skips SwordStep while it holds.
template <class Memory>
bool KnifeSlotHolds(Memory& memory, uint32_t player_slot, const OcarinaAsset& asset) {
    namespace L = mount_layout;
    uint32_t player = 0, body = 0, info = 0, model = 0;
    return asset.ready && memory.Read32(player_slot, player) && MountPointer(player) &&
           memory.Read32(player + L::kBody, body) && MountPointer(body) &&
           memory.Read32(body + L::kKnifeInfo, info) && MountPointer(info) &&
           memory.Read32(info + L::kModel, model) && model == asset.bin;
}

class HandMount {
public:
    bool Mounted() const { return active_; }
    uint32_t Info() const { return info_; }

    template <class Memory>
    MountResult Mount(Memory& memory, uint32_t player_slot, const OcarinaAsset& asset) {
        namespace L = mount_layout;
        if (active_) return MountResult::AlreadyMounted;
        if (!asset.ready) return MountResult::AssetNotReady;
        uint32_t player = 0, body = 0, info = 0;
        if (!memory.Read32(player_slot, player)) return MountResult::InvalidMemory;
        if (!MountPointer(player)) return MountResult::NoPlayer;
        if (!memory.Read32(player + L::kBody, body)) return MountResult::InvalidMemory;
        if (!MountPointer(body)) return MountResult::NoKnife;
        if (!memory.Read32(body + L::kKnifeInfo, info)) return MountResult::InvalidMemory;
        if (!MountPointer(info)) return MountResult::NoKnife;
        const MountResult listed = InList(memory, player, info);
        if (listed != MountResult::Mounted) return listed;
        uint32_t flags = 0, model = 0, tpl = 0;
        if (!memory.Read32(info + L::kFlags, flags) || !memory.Read32(info + L::kModel, model) ||
            !memory.Read32(info + L::kTpl, tpl)) return MountResult::InvalidMemory;
        if (flags & L::kVisible) return MountResult::SwordReady;
        if (flags & L::kShape) return MountResult::ShapeModel;
        if (model == asset.bin || tpl == asset.tpl) return MountResult::AssetNotReady;
        if (!MountPointer(model) || !MountPointer(tpl)) return MountResult::InvalidMemory;
        uint32_t knife_skeleton = 0, knife_group = 0, ocarina_skeleton = 0, ocarina_group = 0;
        if (!memory.Read32(model + L::kSkeletonWord, knife_skeleton) || !memory.Read32(model + L::kGroupWord, knife_group) ||
            !memory.Read32(asset.bin + L::kSkeletonWord, ocarina_skeleton) ||
            !memory.Read32(asset.bin + L::kGroupWord, ocarina_group)) return MountResult::InvalidMemory;
        // Bytes 18..19 (weight records, bone count) and 2A..2B (upper half of the word at
        // 0x28). Bytes 1A..1B are the material count, which the draw reads from the BIN itself.
        if ((knife_skeleton & 0xFFFFu) != (ocarina_skeleton & 0xFFFFu) || (knife_group >> 16) != (ocarina_group >> 16))
            return MountResult::HeaderMismatch;
        uint32_t box[L::kBoxWords] = {};
        for (uint32_t i = 0; i < L::kBoxWords; ++i)
            if (!memory.Read32(info + L::kBox + 4 * i, box[i])) return MountResult::InvalidMemory;
        uint32_t again = 0;
        if (!memory.Read32(player_slot, again) || again != player) return MountResult::InvalidMemory;
        // Hidden while the pointers and box change; shown last.
        if (!memory.Compare32(info + L::kModel, model, asset.bin)) return MountResult::RaceLost;
        if (!memory.Compare32(info + L::kTpl, tpl, asset.tpl)) {
            memory.Compare32(info + L::kModel, asset.bin, model);
            return MountResult::RaceLost;
        }
        uint32_t written = 0;
        for (; written < L::kBoxWords; ++written)
            if (box[written] != asset.box[written] &&
                !memory.Compare32(info + L::kBox + 4 * written, box[written], asset.box[written])) break;
        if (written < L::kBoxWords || !memory.Compare32(info + L::kFlags, flags, flags | L::kVisible)) {
            for (uint32_t i = 0; i < written; ++i)
                if (box[i] != asset.box[i]) memory.Compare32(info + L::kBox + 4 * i, asset.box[i], box[i]);
            memory.Compare32(info + L::kTpl, asset.tpl, tpl);
            memory.Compare32(info + L::kModel, asset.bin, model);
            return MountResult::RaceLost;
        }
        active_ = true; player_ = player; body_ = body; info_ = info;
        model_ = model; tpl_ = tpl; bin_ = asset.bin; ocarina_tpl_ = asset.tpl;
        for (uint32_t i = 0; i < L::kBoxWords; ++i) { box_[i] = box[i]; ocarina_box_[i] = asset.box[i]; }
        return MountResult::Mounted;
    }

    template <class Memory>
    MountResult Unmount(Memory& memory, uint32_t player_slot) {
        namespace L = mount_layout;
        if (!active_) return MountResult::NotMounted;
        uint32_t player = 0, body = 0, info = 0;
        if (!memory.Read32(player_slot, player)) return MountResult::InvalidMemory;
        if (player != player_ || !memory.Read32(player + L::kBody, body) || body != body_ ||
            !memory.Read32(body + L::kKnifeInfo, info) || info != info_) {
            Drop();
            return MountResult::IdentityChanged;
        }
        uint32_t flags = 0, model = 0, tpl = 0, box[L::kBoxWords] = {};
        if (!memory.Read32(info + L::kFlags, flags) || !memory.Read32(info + L::kModel, model) ||
            !memory.Read32(info + L::kTpl, tpl)) return MountResult::InvalidMemory;
        for (uint32_t i = 0; i < L::kBoxWords; ++i)
            if (!memory.Read32(info + L::kBox + 4 * i, box[i])) return MountResult::InvalidMemory;
        // The model goes back last, so every state this object can leave has model == bin_
        // (a retry after RaceLost may find bit 8, the TPL or box words already restored).
        bool foreign = model != bin_ || (tpl != ocarina_tpl_ && tpl != tpl_);
        for (uint32_t i = 0; i < L::kBoxWords; ++i)
            foreign = foreign || (box[i] != ocarina_box_[i] && box[i] != box_[i]);
        if (foreign) {
            Drop();
            return MountResult::Foreign;
        }
        // Bit 8 was clear before Mount (SwordReady refuses otherwise); other bits are the game's.
        if ((flags & L::kVisible) && !memory.Compare32(info + L::kFlags, flags, flags & ~L::kVisible))
            return MountResult::RaceLost;
        if (tpl != tpl_ && !memory.Compare32(info + L::kTpl, ocarina_tpl_, tpl_)) return MountResult::RaceLost;
        for (uint32_t i = 0; i < L::kBoxWords; ++i)
            if (box[i] != box_[i] && !memory.Compare32(info + L::kBox + 4 * i, ocarina_box_[i], box_[i]))
                return MountResult::RaceLost;
        if (!memory.Compare32(info + L::kModel, bin_, model_)) return MountResult::RaceLost;
        Drop();
        return MountResult::Restored;
    }

private:
    template <class Memory>
    static MountResult InList(Memory& memory, uint32_t player, uint32_t info) {
        namespace L = mount_layout;
        uint32_t node = 0, seen[L::kMaxList] = {};
        if (!memory.Read32(player + L::kModelList, node)) return MountResult::InvalidMemory;
        for (unsigned count = 0; MountPointer(node); ++count) {
            if (count == L::kMaxList) return MountResult::InvalidMemory;
            for (unsigned i = 0; i < count; ++i)
                if (seen[i] == node) return MountResult::InvalidMemory;
            if (node == info) return MountResult::Mounted;
            seen[count] = node;
            if (!memory.Read32(node + L::kNext, node)) return MountResult::InvalidMemory;
        }
        return MountResult::NoKnife;
    }
    void Drop() {
        active_ = false; player_ = body_ = info_ = model_ = tpl_ = bin_ = ocarina_tpl_ = 0;
        for (uint32_t i = 0; i < mount_layout::kBoxWords; ++i) box_[i] = ocarina_box_[i] = 0;
    }

    bool active_ = false;
    uint32_t player_ = 0, body_ = 0, info_ = 0, model_ = 0, tpl_ = 0, bin_ = 0, ocarina_tpl_ = 0;
    uint32_t box_[mount_layout::kBoxWords] = {}, ocarina_box_[mount_layout::kBoxWords] = {};
};
}  // namespace link_runtime::ocarina

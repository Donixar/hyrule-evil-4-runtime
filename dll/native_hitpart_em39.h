#pragma once
// Dark Link's (em39, Krauser's class) hit part for the Blade Lock's win exit, fila 16 (14-nov). native_hitpart.h knows
// em10's list only and refuses an em39 (not_em10), so the exit flash on Dark Link never published (`flash=no_hitpart`).
// Read-only, like native_hitpart.h: an injected `bool Read(uint32_t address, void* out, size_t n)`, no writes, no calls.
//
// Attribution (bio4.exe SHA 19aed4af..., UHD bytes; N:/link-in-re4-work/night-20261114/a2/em39-hitpart.md):
//   em39 R0 Init 0x148720 (R0 table 0x7F8590[0], dispatched from the vtable's move, slot 4 0x1574E0, at 0x1576A5) is the
//   only builder: YarareInit 0x18B570 at 0x148C55 (inline head actor+0x344, parts 5, flag 1), then YarareAdd 0x18B660
//   for hit[0..8] at actor+0x43C+i*0x34 (parts 2/1, 0x14/1, 0x18/1, 9/3, 0xF/3, 0x13/1, 0x17/1, 8/3, 0xE/3) and, for
//   type +0x101 2 or 3, YarareAddCube 0x18B6E0 for hit[9..18] (parts 0x62..0x66 and 0x7B..0x7F, flag 0xB: AddCube ors 8).
//   List end: actor+0x610 (type 0/1, 9 nodes) or actor+0x818 (type 2/3, 19 nodes); both inside the manager block 0xE08.
//   Damage record: the same cDmgInfo layout as em10 (+0x328 / +0x32C pending / +0x340 node); em39DmCk 0x148200 reads the
//   node's parts_no. Type 2/3 (r31c, the duel's Dark Link): the head (parts 5) gives Dm_T_Head (2,3) whatever the weapon;
//   any other part needs Total_damage > 200 for Dm_T_Down, and the guard parts only clink. So the exit takes the head.
#include <cmath>
#include <cstdint>

namespace link_runtime::native_hitpart::em39 {
constexpr uint32_t kVtableRva = 0x7127CC;        // em39 (parry_direct_routes.h kSpecs' Krauser; sword_lock_darklink.h)
constexpr uint8_t kId = 0x39;
constexpr uint32_t kVtable = 0x000, kBeFlag = 0x004, kIdOff = 0x100, kTypeOff = 0x101;
constexpr uint32_t kHead = 0x344, kHit = 0x43C, kNodeSize = 0x34;
constexpr uint32_t kNext = 0x30, kFlag = 0x24, kParts = 0x26, kLen = 0x28;
constexpr unsigned kNodesType01 = 9, kNodesType23 = 19, kMaxNodes = 1 + kNodesType23;
constexpr uint32_t kEnd23 = kHit + kNodesType23 * kNodeSize;   // 0x818
constexpr uint16_t kFlagOn = 1, kFlagHandgunSkip = 0x10, kFlagKnown = 0x40FF;
constexpr int16_t kPartHead = 5;
constexpr uint32_t kImageSize = 0x137D000, kMaxPoolCount = 256, kMaxPoolBlock = 0x4000;

inline bool KnownPart(int16_t p) {
    switch (p) {
    case 2: case 5: case 8: case 9: case 0xE: case 0xF: case 0x13: case 0x14: case 0x17: case 0x18: return true;
    default: return (p >= 0x62 && p <= 0x66) || (p >= 0x7B && p <= 0x7F);
    }
}
inline unsigned NodesFor(uint8_t type) {return type == 2 || type == 3 ? kNodesType23 : kNodesType01;}

enum class Verdict : uint8_t {Ok, NoManager, BadActor, ReadFailed, NotLive, NotEm39, BadPointer, Cycle, BadFlags, UnknownPart, NoHead, Disabled};
inline const char* Name(Verdict v) {
    static const char* const n[] = {"ok", "no_manager", "bad_actor", "read_failed", "not_live", "not_em39", "bad_pointer", "cycle",
                                    "bad_flags", "unknown_part", "no_head", "disabled"};
    return n[unsigned(v)];
}
struct Ranges {
    uint32_t image_base = 0x400000, image_size = kImageSize;
    uint32_t heap_lo = 0x10000, heap_hi = 0x7FFF0000;
    uint32_t pool_base = 0, pool_count = 0, pool_block = 0;   // the enemy manager (native_hitpart::ReadPool fills the same)
};
struct Node {uint32_t address = 0; uint8_t slot = 0xFF; uint16_t flags = 0; int16_t parts_no = 0; float len = 0; bool len_repaired = false;};
struct List {Node nodes[kMaxNodes]; unsigned count = 0; uint8_t type = 0;};

// Slot of `node` for this type: 0 = the inline head, 1..n = hit[0..n-1]; 0xFF = not one of this em39's nodes.
inline uint8_t SlotOf(uint32_t actor, uint32_t node, uint8_t type) {
    if (node == actor + kHead) return 0;
    const uint32_t lo = actor + kHit, hi = lo + NodesFor(type) * kNodeSize;
    if (node < lo || node >= hi) return 0xFF;
    const uint32_t off = node - lo;
    return off % kNodeSize ? 0xFF : uint8_t(1 + off / kNodeSize);
}
inline Verdict Address(const Ranges& r, uint32_t actor) {
    if (!r.pool_base || !r.pool_count || r.pool_count > kMaxPoolCount || r.pool_block < kEnd23 || r.pool_block > kMaxPoolBlock)
        return Verdict::NoManager;
    if (!actor || (actor & 3) || actor < r.heap_lo || actor >= r.heap_hi || r.heap_hi - actor < kEnd23) return Verdict::BadActor;
    if (actor < r.image_base + r.image_size && actor + kEnd23 > r.image_base) return Verdict::BadActor;
    if (actor < r.pool_base) return Verdict::BadActor;
    const uint32_t off = actor - r.pool_base;
    if (off % r.pool_block || off / r.pool_block >= r.pool_count) return Verdict::BadActor;
    return Verdict::Ok;
}
// The whole list, each pointer checked against this type's slots BEFORE it is read; reads stay inside [actor, +0x818).
template <class Mem> Verdict Walk(Mem& m, const Ranges& r, uint32_t actor, List& out) {
    out = List{};
    Verdict v = Address(r, actor);
    if (v != Verdict::Ok) return v;
    uint32_t vt = 0, be = 0; uint8_t id = 0, type = 0;
    if (!m.Read(actor + kVtable, &vt, 4) || !m.Read(actor + kBeFlag, &be, 4) || !m.Read(actor + kIdOff, &id, 1) ||
        !m.Read(actor + kTypeOff, &type, 1)) return Verdict::ReadFailed;
    if ((be & 0x201) != 1) return Verdict::NotLive;
    if (vt != r.image_base + kVtableRva || id != kId) return Verdict::NotEm39;
    out.type = type;
    uint32_t node = actor + kHead, seen = 0;
    while (node) {
        const uint8_t slot = SlotOf(actor, node, type);
        if (slot == 0xFF) return Verdict::BadPointer;
        if (seen & (1u << slot)) return Verdict::Cycle;
        seen |= 1u << slot;
        Node n; n.address = node; n.slot = slot; uint32_t nx = 0;
        if (!m.Read(node + kFlag, &n.flags, 2) || !m.Read(node + kParts, &n.parts_no, 2) || !m.Read(node + kLen, &n.len, 4) ||
            !m.Read(node + kNext, &nx, 4)) return Verdict::ReadFailed;
        if (n.flags & ~kFlagKnown) return Verdict::BadFlags;
        if (!KnownPart(n.parts_no)) return Verdict::UnknownPart;
        out.nodes[out.count++] = n;
        node = nx;
    }
    return Verdict::Ok;
}
// The head node (parts 5), ON and not handgun-skip, from a validated list. A len that is not finite or negative becomes
// 0 with len_repaired (the caller publishes its own distance, as ocarina_song_effects.h does for 0x14/0x17).
template <class Mem> Verdict Head(Mem& m, const Ranges& r, uint32_t actor, Node& out) {
    out = Node{};
    List l; const Verdict v = Walk(m, r, actor, l);
    if (v != Verdict::Ok) return v;
    for (unsigned i = 0; i < l.count; ++i) {
        const Node& n = l.nodes[i];
        if (n.parts_no != kPartHead) continue;
        if (!(n.flags & kFlagOn) || (n.flags & kFlagHandgunSkip)) return Verdict::Disabled;
        out = n;
        if (!std::isfinite(n.len) || n.len < 0) {out.len = 0; out.len_repaired = true;}
        return Verdict::Ok;
    }
    return Verdict::NoHead;
}
}  // namespace link_runtime::native_hitpart::em39

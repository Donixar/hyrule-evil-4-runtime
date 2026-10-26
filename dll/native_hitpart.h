#pragma once
// Qualified hit-part source for the shared native-hit adapter (Mortal Draw
// lethal and parry riposte). Read-only: an injected accessor
// `bool Read(uint32_t address, void* out, size_t n)`, no writes, no native calls.
//
// Attribution (pinned bio4.exe SHA 19aed4af..., image SHA bb59e550..., RVAs):
//   knife sweep 0x286C40 (thunk 0x7865) -> list builder 0x1BB3C0 (thunk 0x23A6,
//   weapons outside 0x12..0x2E, so the knife 0x10) -> per-actor knife test
//   0x1BA110 (thunk 0x40E8, cdecl actor, p0, p1, float max_len2, word weapon;
//   returns the closest ON node or 0) -> pairs (actor, node) -> 0x286DA6 publishes
//   node with distance = node+0x28 through 0x1AEEF0.
//   The hit list is YARARE_INFO (0x34 bytes) with the inline head at actor+0x344
//   and `next` at +0x30; every walker (0x1BA110, 0x1BA3E0, 0x1BA880, 0x1B9DB8,
//   0x1B560C) starts with `lea esi,[actor+0x344]` and follows [esi+0x30].
//   Population: YarareInit 0x18B570 (thunk 0x6271) writes the head and clears
//   +0x374; YarareAdd 0x18B660 (thunk 0x87BF) initialises a node (next 0) and
//   appends it unless already linked. em10 R0 Init 0x61580 (R0 table 0x7F39B0[0])
//   builds: head parts 5 (flag 1) [types 0xA/0xD: parts 3, flag 5], then
//   hit[0..9] at actor+0x834 + i*0x34 with parts 2, 0x14, 0x18, 9, 0xF, 0x13,
//   0x17, 8, 0xE and hit[9] = parts 5 flag 0 [0xA/0xD: parts 0x25 flag 1].
//   No em10 code writes a `next` pointer after Init (writers of +0x374 are only
//   0x18B5A7/0x18B617 and player code); em10 only toggles flag bits
//   (0x5AE57/0x5AE86 head ON, 0x64A07 head OFF, 0x5BA97..0x5BC04 hit[] bits).
// Consumers of the published node (cDmgInfo+0x18 = actor+0x340, never null
// checked): calc 0x4AD40 reads +0x28 (< 36e6 = "close", only used by
// GetWepDmVal for weapons 7/8/0x20) and +0x26 (armour 0x39CA0, head 5, core
// 0x25); knife reaction 0x58B90 (table 0x7F3DB0[0x10]) reads +0x26 only.
// So a valid record is one node of THIS actor's list, with a known parts_no.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace link_runtime::native_hitpart {
namespace layout {
constexpr uint32_t head = 0x344, node_size = 0x34;
constexpr uint32_t next = 0x30, flag = 0x24, parts_no = 0x26, len = 0x28, c_dis = 0x2C;
constexpr uint32_t em10_hit = 0x834;   // FREE_EM10 hit[10] (GC work 0x1C4)
constexpr unsigned em10_hit_count = 10, max_nodes = 1 + em10_hit_count;
constexpr uint32_t em10_end = em10_hit + em10_hit_count * node_size;  // 0xA3C
constexpr uint16_t flag_on = 1, flag_handgun_skip = 0x10;
constexpr uint16_t flag_known = 0x40FF;  // YAT_FLAG_* bits plus DMPOS 0x4000 (set/cleared by 0x1BB610/0x1BAFCA)
constexpr int16_t part_head = 5, part_core = 0x25;
// Enemy manager slots (mortal_draw_target.h): array, count, block size.
constexpr uint32_t manager_array_rva = 0x7FDB08, manager_count_rva = 0x7FDB0C, manager_block_rva = 0x7FDB10;
constexpr uint32_t image_size = 0x137D000;  // pinned mapped image (20434944 bytes)
// Native functions this source mirrors (fixture: tests/native/test_native_hitpart.cpp).
constexpr uint32_t knife_test_rva = 0x1BA110, point_test_rva = 0x1BA880, builder_rva = 0x1BB3C0;
constexpr uint32_t yarare_init_rva = 0x18B570, yarare_add_rva = 0x18B660, em10_init_rva = 0x61580;
constexpr float close_len = 36000000.f;   // 0xB10354
}  // namespace layout

// Every parts_no em10 R0 Init 0x61580 can link (all types).
inline bool KnownPart(int16_t p) {
    switch (p) { case 2: case 3: case 5: case 8: case 9: case 0xE: case 0xF: case 0x13: case 0x14: case 0x17: case 0x18: case 0x25: return true; }
    return false;
}

// Mandatory manager data (fix round 1, I1): without a readable enemy manager
// every query is refused (NoManager); use ReadPool / ManagedSource to fill these.
struct Ranges {
    uint32_t image_base = 0x400000, image_size = layout::image_size;
    uint32_t heap_lo = 0x10000, heap_hi = 0x7FFF0000;  // user-mode, outside the module
    uint32_t pool_base = 0, pool_count = 0, pool_block = 0;  // enemy manager: the actor must be a slot
};
// Optional identity the caller already qualified (riposte owner id +0x100, type +0x101).
struct Expect { bool check = false; uint8_t id = 0, type = 0; };
// len_repaired (shield-guard-02): the node's len was not finite or negative and the caller's rule took it anyway.
struct Node { uint32_t address = 0; uint8_t slot = 0xFF; uint16_t flags = 0; int16_t parts_no = 0; float len = 0; bool len_repaired = false; };
struct List { Node nodes[layout::max_nodes]; unsigned count = 0; };
// Head/core are refused unless allowed (head 5 takes the LostHead branch,
// core 0x25 doubles the calc): the default for both Lethal and Scaled.
// repair_len (shield-guard-02, Donald 18-oct, pid 10308): a wading Ganado (r1 0x26/0x10 in shallow water) keeps a
// selected node whose len is not finite or negative, so every Back Slice / Shield Bash on it was refused
// (no_hitpart verdict=bad_len, 31 times). The node's len is read by the native calc only for weapons 7/8/0x20
// (GetWepDmVal); a caller publishing another weapon (knife 0x10, kick 0x14, flash 0x17) may take the node and pass
// its own distance (Facts::hitpart_len). The memory is never written.
struct Rule { bool allow_head = false, allow_core = false, repair_len = false; };
enum class Verdict : uint8_t { Ok, BadActor, ReadFailed, BadPointer, Cycle, TooLong, BadFlags, UnknownPart, BadLen, Empty, NotListed, Disabled, Excluded,
                               NoManager, NotLive, NotEm10, Identity };
inline const char* Name(Verdict v) {
    static const char* const n[] = {"ok", "bad_actor", "read_failed", "bad_pointer", "cycle", "too_long", "bad_flags",
                                    "unknown_part", "bad_len", "empty", "not_listed", "disabled", "excluded",
                                    "no_manager", "not_live", "not_em10", "identity"};
    return n[unsigned(v)];
}
namespace layout {
// Liveness and class, as the knife builder 0x1BB52A and the enumerator 0x491CF
// test them: be_flag +4 (&0x201)==1; em10 base vtable 0x71035C at +0 (derived
// variants refused, like mortal_draw_target.h); id +0x100, type +0x101.
constexpr uint32_t vtable = 0x000, be_flag = 0x004, id = 0x100, type = 0x101, em10_vtable_rva = 0x71035C;
constexpr uint32_t max_pool_count = 256, max_pool_block = 0x4000;  // target::ReadManager bounds
}  // namespace layout
// Slot of `node` in the attributed em10 layout: 0 = inline head, 1..10 = hit[0..9].
inline uint8_t SlotOf(uint32_t actor, uint32_t node) {
    if (node == actor + layout::head) return 0;
    if (node < actor + layout::em10_hit || node >= actor + layout::em10_end) return 0xFF;
    const uint32_t off = node - (actor + layout::em10_hit);
    return off % layout::node_size ? 0xFF : uint8_t(1 + off / layout::node_size);
}

class Source {
public:
    explicit Source(const Ranges& r) : r_(r) {}
    const Ranges& ranges() const { return r_; }
    bool HasManager() const {
        return r_.pool_base && r_.pool_count && r_.pool_count <= layout::max_pool_count && r_.pool_block >= layout::em10_end &&
               r_.pool_block <= layout::max_pool_block;
    }
    // Address checks only (no read): heap range, outside the module, 4-aligned,
    // and an exact enemy-manager slot.
    Verdict ActorAddress(uint32_t actor) const {
        if (!HasManager()) return Verdict::NoManager;
        if (!actor || (actor & 3) || actor < r_.heap_lo || actor >= r_.heap_hi || r_.heap_hi - actor < layout::em10_end) return Verdict::BadActor;
        if (actor < r_.image_base + r_.image_size && actor + layout::em10_end > r_.image_base) return Verdict::BadActor;
        if (actor < r_.pool_base) return Verdict::BadActor;
        const uint32_t off = actor - r_.pool_base;
        if (off % r_.pool_block || off / r_.pool_block >= r_.pool_count) return Verdict::BadActor;
        return Verdict::Ok;
    }
    // Slot is live (be_flag), holds an em10 (vtable) and, if asked, the expected owner.
    template <class Mem> Verdict ActorLive(Mem& m, uint32_t actor, const Expect* expect) const {
        uint32_t vt = 0, be = 0; uint8_t id = 0, type = 0;
        if (!m.Read(actor + layout::vtable, &vt, 4) || !m.Read(actor + layout::be_flag, &be, 4) ||
            !m.Read(actor + layout::id, &id, 1) || !m.Read(actor + layout::type, &type, 1)) return Verdict::ReadFailed;
        if ((be & 0x201) != 1) return Verdict::NotLive;
        if (vt != r_.image_base + layout::em10_vtable_rva) return Verdict::NotEm10;
        if (expect && expect->check && (id != expect->id || type != expect->type)) return Verdict::Identity;
        return Verdict::Ok;
    }
    // Whole list, every pointer checked against the attributed slots BEFORE it
    // is dereferenced; reads stay inside [actor, actor+0xA3C). `len` is not
    // judged here (round 1, M4): only the node a caller takes must have a sane len.
    template <class Mem> Verdict Walk(Mem& m, uint32_t actor, List& out, const Expect* expect = nullptr) const {
        out = List{};
        Verdict v = ActorAddress(actor);
        if (v != Verdict::Ok) return v;
        if ((v = ActorLive(m, actor, expect)) != Verdict::Ok) return v;
        uint32_t node = actor + layout::head;
        unsigned seen = 0;
        while (node) {
            const uint8_t slot = SlotOf(actor, node);
            if (slot == 0xFF) return Verdict::BadPointer;
            if (seen & (1u << slot)) return Verdict::Cycle;
            if (out.count == layout::max_nodes) return Verdict::TooLong;  // unreachable with unique slots
            seen |= 1u << slot;
            Node n; n.address = node; n.slot = slot; uint32_t nx = 0;
            if (!m.Read(node + layout::flag, &n.flags, 2) || !m.Read(node + layout::parts_no, &n.parts_no, 2) ||
                !m.Read(node + layout::len, &n.len, 4) || !m.Read(node + layout::next, &nx, 4)) return Verdict::ReadFailed;
            if (n.flags & ~layout::flag_known) return Verdict::BadFlags;
            if (!KnownPart(n.parts_no)) return Verdict::UnknownPart;
            out.nodes[out.count++] = n;
            node = nx;
        }
        return Verdict::Ok;
    }
    // First node in list order that the native knife test could return (ON, not
    // handgun-skip) and that `rule` admits. Empty when nothing is ON.
    template <class Mem> Verdict Select(Mem& m, uint32_t actor, Rule rule, Node& out, const Expect* expect = nullptr) const {
        out = Node{};
        List l; const Verdict v = Walk(m, actor, l, expect);
        if (v != Verdict::Ok) return v;
        bool any_on = false;
        for (unsigned i = 0; i < l.count; ++i) {
            const Node& n = l.nodes[i];
            if (!Selectable(n)) continue;
            any_on = true;
            if (Admits(rule, n)) return Take(rule, n, out);
        }
        return any_on ? Verdict::Excluded : Verdict::Empty;
    }
    // A node handed in by a caller (e.g. a native hit test result): it must be a
    // listed, selectable node of this actor's validated list.
    template <class Mem> Verdict Validate(Mem& m, uint32_t actor, uint32_t node, Rule rule, Node& out, const Expect* expect = nullptr) const {
        out = Node{};
        List l; const Verdict v = Walk(m, actor, l, expect);
        if (v != Verdict::Ok) return v;
        for (unsigned i = 0; i < l.count; ++i) {
            if (l.nodes[i].address != node) continue;
            if (!Selectable(l.nodes[i])) return Verdict::Disabled;
            if (!Admits(rule, l.nodes[i])) return Verdict::Excluded;
            return Take(rule, l.nodes[i], out);
        }
        return Verdict::NotListed;
    }
    static bool Selectable(const Node& n) { return (n.flags & layout::flag_on) && !(n.flags & layout::flag_handgun_skip); }
    static bool Admits(Rule r, const Node& n) {
        return (r.allow_head || n.parts_no != layout::part_head) && (r.allow_core || n.parts_no != layout::part_core);
    }

private:
    // The taken node's len becomes the published distance: finite and >= 0.
    static Verdict Take(Rule rule, const Node& n, Node& out) {
        if (!std::isfinite(n.len) || n.len < 0) {
            if (!rule.repair_len) return Verdict::BadLen;
            out = n; out.len = 0; out.len_repaired = true; return Verdict::Ok;
        }
        out = n; return Verdict::Ok;
    }
    Ranges r_;
};

// Manager slots from the image globals (game thread), bounded like
// target::ReadManager. False: no manager, so every Source query is NoManager.
template <class Mem> bool ReadPool(Mem& m, uint32_t image_base, Ranges& r) {
    uint32_t array = 0, count = 0, block = 0;
    if (!m.Read(image_base + layout::manager_array_rva, &array, 4) || !m.Read(image_base + layout::manager_count_rva, &count, 4) ||
        !m.Read(image_base + layout::manager_block_rva, &block, 4) || !array || !count || count > layout::max_pool_count ||
        block < layout::em10_end || block > layout::max_pool_block) return false;
    r.pool_base = array; r.pool_count = count; r.pool_block = block;
    return true;
}
// Source for `image_base` with the live manager (NoManager on every query when unreadable).
template <class Mem> Source ManagedSource(Mem& m, uint32_t image_base) {
    Ranges r; r.image_base = image_base;
    ReadPool(m, image_base, r);
    return Source(r);
}

// FIRST in-game check (observer only, no publish, no write): one log line per
// observed target, with `s = ManagedSource(mem, image_base)` (no manager:
// verdict=no_manager). `actor` is the Ganado Mortal Draw selected (or the parry
// owner); format:
//   hitpart_observe actor=0x0A1B2C3D verdict=ok nodes=11 on=10 pick=1:0x02 list=0:05:0001:0,1:02:0001:12.5,...
// list items are slot:parts_no:flags:len (len rounded); pick = slot:parts of the
// default (no head/core) selection, or '-' with the refusal in verdict.
template <class Mem> int FormatObservation(char* buf, size_t size, const Source& s, Mem& m, uint32_t actor) {
    if (!buf || !size) return 0;
    List l; const Verdict walk = s.Walk(m, actor, l);
    Node pick; const Verdict sel = walk == Verdict::Ok ? s.Select(m, actor, Rule{}, pick) : walk;
    unsigned on = 0;
    for (unsigned i = 0; i < l.count; ++i) on += Source::Selectable(l.nodes[i]) ? 1 : 0;
    int n = std::snprintf(buf, size, "hitpart_observe actor=0x%08X verdict=%s nodes=%u on=%u pick=", actor, Name(sel), l.count, on);
    if (n < 0 || size_t(n) >= size) return n;
    int k = sel == Verdict::Ok ? std::snprintf(buf + n, size - n, "%u:0x%02X list=", pick.slot, unsigned(uint16_t(pick.parts_no)))
                               : std::snprintf(buf + n, size - n, "- list=");
    if (k < 0 || size_t(n + k) >= size) return n + (k > 0 ? k : 0);
    n += k;
    for (unsigned i = 0; i < l.count; ++i) {
        const Node& x = l.nodes[i];
        k = std::snprintf(buf + n, size - n, "%s%u:%02X:%04X:%.0f", i ? "," : "", x.slot, unsigned(uint16_t(x.parts_no)), x.flags, double(x.len));
        if (k < 0 || size_t(n + k) >= size) return n + (k > 0 ? k : 0);
        n += k;
    }
    return n;
}
}  // namespace link_runtime::native_hitpart

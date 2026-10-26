#pragma once
// Spin and Great Spin break boxes and boards (Donald 22-oct: «el giro y el gran giro también rompan barriles, cajas»).
// Pure logic over an injected memory (Read/Write with SEH in the runtime); the glue (spin_attack_runtime.h) calls Fire
// once at a spin's strike. INI [LinkRuntime] SpinBreakables: missing or 1 = on, 0 = off.
//
// The breakables are enemies of the same cEmMgr pool as the Ganados (re4-decomp src/game/embox.cpp, emBar.cpp). The
// UHD ids are the GC ones + 0x10 (live 22-oct, room with boxes: cEmBox at id 0x53; the GC 0x51 is cEmDoor here), so
// each is identified by its id AND its class vtable (RTTI .?AVcEmBox@@ / .?AVcEmBar@@ in the pinned image):
//   0x53 cEmBox  vtable 0x716F00  boxes, wooden barrels, vases and cabinets (1000 hp);
//   0x61 cEmBar  vtable 0x717170  wooden boards on windows and doors (1000 hp).
// Their per-frame damage check (emBoxDmCk / emBarDmCk) consumes the cDmgInfo at actor+0x328 (UHD; GC 0x324): any
// weapon but 0x14/0x16/0x17/0x2A/0xE breaks them, and the knife 0x10 takes 1000 = their whole hp, as a native
// slash does. So a spin writes the same record the knife leaves (thrown::Deflect's layout, parry_general_block.h):
// float 0, from = Link, distance 0, part 0, timer 10, weapon 0x10, flag 1 last. A record already pending is left alone.
// Never: cEmBarrel 0x58 / vtable 0x716FC0 (the red explosive barrel: it would blow up beside Link), cEmDoor 0x51 /
// 0x716EA0, cEmWindow 0x56 / 0x716F60, racks, gates and hit receivers (events and puzzles).
#include <cmath>
#include <cstdint>

namespace link_runtime::sword_breakables {
namespace layout {
constexpr uint32_t be_flag = 0x004, pos = 0x094, id = 0x100, hp = 0x324;   // cModel / cEm, as mortal_draw_target.h
constexpr uint32_t dmg = 0x328, dmg_flag = 0x32C, dmg_timer = 0x32D, dmg_weapon = 0x32E, dmg_from = 0x330;
constexpr uint32_t dmg_distance = 0x33C, dmg_part = 0x340;
}  // namespace layout
constexpr uint8_t kBox = 0x53, kBar = 0x61, kKnife = 0x10, kKnifeTimer = 10;
constexpr uint32_t kBoxVtable = 0x716F00, kBarVtable = 0x717170;   // RVAs
constexpr unsigned kMaxBreaks = 32;
// Height band around Link's feet: wider than the Ganados' 400 mm so vases and jars on tables and shelves (~0.8 m)
// break too (Donald 22-oct: everything that holds items).
constexpr float kVertical = 1200.f;
inline bool Breakable(uint8_t id) {return id == kBox || id == kBar;}
// The class vtable that goes with the id (image_base + RVA); anything else is not ours.
inline bool Breakable(uint8_t id, uint32_t vtable, uint32_t image_base) {
    return (id == kBox && vtable == image_base + kBoxVtable) || (id == kBar && vtable == image_base + kBarVtable);
}
struct Object {uint32_t pointer = 0; uint8_t id = 0; int16_t hp = 0; float pos[3]{};};
// A live breakable: valid slot (be_flag & 0x201 == 1), id 0x53 / 0x61 with its class vtable, hp > 0, finite position,
// no record pending.
template <class Mem> bool Read(Mem& m, uint32_t pointer, uint32_t image_base, Object& out) {
    out = Object{};
    uint32_t be = 0, vtable = 0; uint8_t id = 0, pending = 1; int16_t hp = 0; float p[3]{};
    if (!pointer || !m.Read(pointer, &vtable, 4) || !m.Read(pointer + layout::be_flag, &be, 4) || (be & 0x201) != 1 ||
        !m.Read(pointer + layout::id, &id, 1) || !Breakable(id, vtable, image_base) || !m.Read(pointer + layout::hp, &hp, 2) || hp <= 0 || !m.Read(pointer + layout::pos, p, 12) ||
        !m.Read(pointer + layout::dmg_flag, &pending, 1) || pending)
        return false;
    for (const float v : p) if (!std::isfinite(v)) return false;
    out.pointer = pointer; out.id = id; out.hp = hp; out.pos[0] = p[0]; out.pos[1] = p[1]; out.pos[2] = p[2];
    return true;
}
// Within the spin's reach: horizontal radius and vertical band around Link's feet (the Ganado targets' own limits).
inline bool InReach(const float* link, const float* at, float radius, float vertical) {
    const float dx = at[0] - link[0], dz = at[2] - link[2], dy = std::fabs(at[1] - link[1]);
    const float h = std::sqrt(dx * dx + dz * dz);
    return std::isfinite(h) && std::isfinite(dy) && h <= radius && dy <= vertical;
}
// The knife's damage record, flag byte last; refused when one is already pending.
template <class Mem> bool Strike(Mem& m, uint32_t pointer, const float* from) {
    uint8_t pending = 1;
    if (!pointer || !from || !m.Read(pointer + layout::dmg_flag, &pending, 1) || pending) return false;
    for (unsigned i = 0; i < 3; ++i) if (!std::isfinite(from[i])) return false;
    const float zero = 0; const uint32_t part = 0; const uint8_t timer = kKnifeTimer, weapon = kKnife, flag = 1;
    return m.Write(pointer + layout::dmg, &zero, 4) && m.Write(pointer + layout::dmg_from, from, 12) &&
           m.Write(pointer + layout::dmg_distance, &zero, 4) && m.Write(pointer + layout::dmg_part, &part, 4) &&
           m.Write(pointer + layout::dmg_timer, &timer, 1) && m.Write(pointer + layout::dmg_weapon, &weapon, 1) &&
           m.Write(pointer + layout::dmg_flag, &flag, 1);
}
struct Result {unsigned seen = 0, struck = 0, failed = 0, boxes = 0, bars = 0;};
// Every live breakable of the pool (array, count, block) within reach gets the knife record, at most kMaxBreaks.
template <class Mem> Result Fire(Mem& m, uint32_t image_base, uint32_t array, uint32_t count, uint32_t block, const float* link,
                                  float radius, float vertical) {
    Result r;
    if (!array || !block || !link || count > 1024) return r;
    for (uint32_t i = 0; i < count && r.struck < kMaxBreaks; ++i) {
        Object o;
        if (!Read(m, array + i * block, image_base, o)) continue;
        ++r.seen;
        if (!InReach(link, o.pos, radius, vertical)) continue;
        if (Strike(m, o.pointer, link)) {++r.struck; ++(o.id == kBox ? r.boxes : r.bars);} else ++r.failed;
    }
    return r;
}
}  // namespace link_runtime::sword_breakables

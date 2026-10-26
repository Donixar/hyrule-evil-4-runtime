// dll/rupee_colors.h  (docs/rupees-colors-2026-10-12.md; pins in native_pins.h kRupeeColours, wiring in link_runtime.cpp)
#pragma once
#include <cstdint>
namespace link_runtime::rupees {
// Floor/popup model of a money pickup chosen by its value, not only by its id. RE4 pays money with three
// ids (cItemMgr::get, UHD 0x307A80): 0x78 num*10 (300 if 0), 0x79 num*100 (3000), 0x71 num*1000 (5000).
// The model comes from ItemGetBinTplAddr(u8 id,void**bin,void**tpl) (cdecl, body 0x24C5B0, thunk 0x5A92),
// which reads cItmSys work[id] (0x100 x {bin,tpl}, base pointer at VA 0xC59B9C). Two generic callers
// carry the pickup record: we redirect their call rel32 to a stub that remembers the record, then ask
// for the alias slot of the value's colour and fall back to the original id when the room lacks it.
// Every RVA is relative to 0x400000 and was read from the pinned bio4.exe (sha 19aed4af...c8ee4a).
namespace rva {
constexpr uint32_t kLookupThunk = 0x5A92;     // e9 196b2400 -> 0x24C5B0
constexpr uint32_t kLookupBody = 0x24C5B0;    // 558bec0fb645088b0d<9c9bc500>8b550c8d0cc1... (reloc at +9)
// SceAtCreateItemAt (drops, barrels, saved respawns): edi = SCE_AT_DATA*, item_id u16 +0x78 and
// item_num u16 +0x7C already stored (0x2BA070, 0x2BA078). Pin 0x2BA107..0x2BA118:
// 51 8d55f8 d95f64 52 56 e87db9d4ff 83c40c
constexpr uint32_t kCreateCall = 0x2BA110;
// Room item set (sce_at.cpp:4110 in GC): esi = &w->item (SCE_AT_DATA+0x5C), item_id +0x1C, item_num +0x20.
// Pin 0x2BAF2D..0x2BAF42: 0fb64e1c 8d55f4 52 8d45f8 50 51 e853abd4ff 83c40c
constexpr uint32_t kRoomCall = 0x2BAF3A;
}
enum class Colour : uint8_t { Green, Blue, Yellow, Red, Purple, Orange, Silver };
// Slots: green/blue/red keep the stock ids (installed today); the rest are u8 ids no room ITM registers
// and nothing grants (item census 11-oct + ITM census): 0xA0 Item_A0, 0xAC File_01, 0xAD File_02, 0xFC File_30.
constexpr uint8_t kSlot[7] = {0x78, 0x79, 0xA0, 0x71, 0xAC, 0xAD, 0xFC};
// 1 rupee = 100 pesetas: TP's own values 1/5/10/20/50/100/200 (scripts/rupee_colors.py TIERS_B).
constexpr uint32_t kLow[7] = {0, 500, 1000, 2000, 5000, 10000, 20000};

inline bool Value(uint16_t id, uint16_t num, uint32_t& ptas) {
    switch (id) {
    case 0x78: ptas = num ? num * 10u : 300u; return true;
    case 0x79: ptas = num ? num * 100u : 3000u; return true;
    case 0x71: ptas = num ? num * 1000u : 5000u; return true;
    default: return false;
    }
}
inline Colour ColourOf(uint32_t ptas) {
    unsigned c = 0;
    while (c < 6 && ptas >= kLow[c + 1]) ++c;
    return static_cast<Colour>(c);
}
// Pure decision: the slot to look up first for (id, num), or id itself for anything that is not money.
inline uint8_t SlotFor(uint16_t id, uint16_t num) {
    uint32_t ptas = 0;
    if (!Value(id, num, ptas)) return static_cast<uint8_t>(id);
    return kSlot[static_cast<unsigned>(ColourOf(ptas))];
}
// The detour's decision around the native ItemGetBinTplAddr (cdecl: u8 id, void** bin, void** tpl; returns 1 when
// the room's ITM has both pointers for the id, else 0 with *bin = 0). `have` = the pickup record was read; its id must
// be the pushed id (a record that disagrees is not trusted). The colour's slot is asked first; a room without it (or
// any failure) gets exactly the native lookup of the original id. Not money: one native lookup, nothing else.
template <class L> int Lookup(L&& lookup, uint32_t id, bool have, uint16_t rec_id, uint16_t rec_num, void** bin, void** tpl) {
    const uint8_t own = static_cast<uint8_t>(id);
    const uint8_t slot = have && static_cast<uint8_t>(rec_id) == own ? SlotFor(rec_id, rec_num) : own;
    if (slot != own && lookup(static_cast<uint32_t>(slot), bin, tpl)) return 1;
    return lookup(id, bin, tpl);
}
}  // namespace link_runtime::rupees

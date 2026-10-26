// dll/event_sheath.h
#pragma once
// EventSheathHide (18-oct-2026, docs/dll-sheath-events-01-2026-10-18.md): in the Krauser knife-fight events Link
// holds the Master Sword (the event knife "evm7700", duel-master-swords-01) and ALSO shows the Master Sword in its
// sheath on his back. That second sword is not the gameplay player and not SwordStep: the event builds its own Link,
// an obj18 "pl0000" (Event::PPl +0x80), and hangs the part "pl000d" on it, which Event::ExeBeginEvt maps to the
// player's archive entry 9 = pl00_005, the whole sheathed sword (blade + hilt, 192 faces). The empty sheath and the
// shield are in the body pl00_000 ("pl0000" itself) and are not touched.
//
// Rule, run on the game thread right after the native Event::ControlTransFlag (which sets each event model's
// be_flag bit 1 for the current cut): for that Event, find in its ModTbl the in-use entries "evm7700" (type 2, an
// obj) and "pl000d" (type 3, a parts info) and check that pl000d hangs on PPl's parts list. While evm7700 is drawn
// (be_flag bits 1|2, the ModelTrans test) pl000d's draw bit (+4 bit 3, the commonModelTrans test) is cleared; when
// evm7700 stops being drawn in the same event, the bit is set again, but only if the word is still exactly the one
// written here (an owner record per part). Nothing else is written: events without evm7700, without pl000d on
// PPl, or with an unreadable table do nothing, and gameplay (no Event::Run) never reaches this code.
//
// Memory: bool Read32(addr, out), bool Read(addr, void*, n), bool Compare32(addr, expected, desired).
#include "link_runtime_core.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace link_runtime::event_sheath {
namespace layout {
constexpr uint32_t kControlTransRva = 0x2218A0;  // Event::ControlTransFlag (thiscall Event*), MinHook target
constexpr uint32_t kEventFlags = 0x04;           // alive = (flags & 0x201) == 1 (EventMgr::IsAliveEvt)
constexpr uint32_t kEventName = 0x18;            // char[0x20], log only
constexpr uint32_t kModTblNum = 0x48, kModTblWork = 0x4C, kPpl = 0x80;
constexpr uint32_t kEntryBytes = 0x3C, kEntryName = 0x00, kEntryNameBytes = 0x30;
constexpr uint32_t kEntryFlag = 0x30, kEntryType = 0x31, kEntryDat = 0x34;
constexpr uint32_t kParts = 0x15C, kNext = 0x14, kBeFlag = 0x04;
constexpr uint32_t kModelDrawn = 0x6;            // ModelTrans draws a model only with be_flag bits 1 and 2
constexpr uint32_t kPartDraw = 0x8;              // commonModelTrans skips a parts info without bit 3
constexpr uint32_t kMaxEntries = 256, kMaxParts = 32;
}  // namespace layout
constexpr char kKnife[] = "evm7700";
constexpr char kSheath[] = "pl000d";

enum class Kind : uint8_t {
    None,        // nothing to do (no knife/part in this event, or the state already matches)
    Hide,        // knife drawn: pl000d draw bit cleared (owner recorded)
    Restore,     // knife hidden again: pl000d draw bit set (word was ours)
    Foreign,     // knife drawn but pl000d already hidden by someone else: left alone
    Drop,        // owned word changed under us or the part left the table: owner dropped, nothing written
    RaceLost,    // the compare-exchange failed: nothing written
    Invalid,     // unreadable/inconsistent table or list: nothing written
    NotOnBody,   // pl000d is not on PPl's parts list: nothing written
};
inline const char* Name(Kind k) {
    switch (k) {
    case Kind::None: return "none"; case Kind::Hide: return "hide"; case Kind::Restore: return "restore";
    case Kind::Foreign: return "foreign"; case Kind::Drop: return "drop"; case Kind::RaceLost: return "race_lost";
    case Kind::Invalid: return "invalid"; case Kind::NotOnBody: return "not_on_body";
    }
    return "?";
}

struct Result {
    Kind kind = Kind::None;
    uint32_t event = 0, part = 0, knife = 0;
    uint32_t knife_flags = 0, before = 0, after = 0;
};

// Owner records: which parts this rule hid, and the exact word it wrote. Game thread only.
struct Owners {
    struct Row { uint32_t event = 0, part = 0, written = 0; bool used = false; };
    Row rows[4];
    Row* Find(uint32_t event, uint32_t part) {
        for (auto& r : rows) if (r.used && r.event == event && r.part == part) return &r;
        return nullptr;
    }
    Row* ForEvent(uint32_t event) {
        for (auto& r : rows) if (r.used && r.event == event) return &r;
        return nullptr;
    }
    void Put(uint32_t event, uint32_t part, uint32_t written) {
        Row* slot = Find(event, part);
        if (!slot) for (auto& r : rows) if (!r.used) { slot = &r; break; }
        if (!slot) slot = &rows[0];   // more than 4 live events never happens; the oldest record is forgotten
        *slot = {event, part, written, true};
    }
    unsigned Count() const { unsigned n = 0; for (const auto& r : rows) n += r.used; return n; }
};

namespace detail {
template <class Memory> bool NameIs(Memory& m, uint32_t entry, const char* want) {
    char name[layout::kEntryNameBytes]{};
    if (!m.Read(entry + layout::kEntryName, name, sizeof(name))) return false;
    if (!std::memchr(name, 0, sizeof(name))) return false;
    return std::strcmp(name, want) == 0;
}
template <class Memory> bool OnList(Memory& m, uint32_t owner, uint32_t part, bool& found) {
    found = false;
    uint32_t node = 0;
    if (!m.Read32(owner + layout::kParts, node)) return false;
    for (uint32_t n = 0; IsPointer(node); ++n) {
        if (n == layout::kMaxParts) return false;
        if (node == part) { found = true; return true; }
        if (!m.Read32(node + layout::kNext, node)) return false;
    }
    return true;
}
}  // namespace detail

// One Event after its ControlTransFlag. `event` is the thiscall `this`.
template <class Memory>
Result Step(Memory& m, uint32_t event, Owners& owners) {
    using namespace layout;
    Result r; r.event = event;
    uint32_t flags = 0, num = 0, work = 0, ppl = 0;
    if (!IsPointer(event) || !m.Read32(event + kEventFlags, flags) || !m.Read32(event + kModTblNum, num) ||
        !m.Read32(event + kModTblWork, work) || !m.Read32(event + kPpl, ppl)) { r.kind = Kind::Invalid; return r; }
    if ((flags & 0x201) != 1 || num > kMaxEntries || (num && !IsPointer(work))) { r.kind = Kind::Invalid; return r; }
    uint32_t knife = 0, part = 0;
    for (uint32_t i = 0; i < num; ++i) {
        const uint32_t e = work + i * kEntryBytes;
        uint32_t head = 0, dat = 0;   // byte +0x30 flag, +0x31 type
        if (!m.Read32(e + kEntryFlag, head) || !m.Read32(e + kEntryDat, dat)) { r.kind = Kind::Invalid; return r; }
        if (!(head & 1) || !IsPointer(dat)) continue;
        const uint32_t type = (head >> 8) & 0xFF;
        if (type == 2 && !knife && detail::NameIs(m, e, kKnife)) knife = dat;
        else if (type == 3 && !part && detail::NameIs(m, e, kSheath)) part = dat;
    }
    r.knife = knife; r.part = part;
    Owners::Row* own = part ? owners.Find(event, part) : nullptr;
    if (!part) {
        // The part left this event's table (EndParts / end of event): forget it, never write to freed memory.
        if (Owners::Row* stale = owners.ForEvent(event)) { stale->used = false; r.kind = Kind::Drop; r.part = stale->part; }
        return r;
    }
    if (!IsPointer(ppl)) { r.kind = Kind::NotOnBody; return r; }
    bool on_body = false;
    if (!detail::OnList(m, ppl, part, on_body)) { r.kind = Kind::Invalid; return r; }
    if (!on_body) { r.kind = Kind::NotOnBody; return r; }
    uint32_t word = 0;
    if (!m.Read32(part + kBeFlag, word)) { r.kind = Kind::Invalid; return r; }
    r.before = r.after = word;
    bool drawn = false;
    if (knife) {
        if (!m.Read32(knife + kBeFlag, r.knife_flags)) { r.kind = Kind::Invalid; return r; }
        drawn = (r.knife_flags & kModelDrawn) == kModelDrawn;
    }
    if (drawn) {
        if (!(word & kPartDraw)) {
            r.kind = own && own->written == word ? Kind::None : Kind::Foreign;
            return r;
        }
        const uint32_t want = word & ~kPartDraw;
        if (!m.Compare32(part + kBeFlag, word, want)) { r.kind = Kind::RaceLost; return r; }
        owners.Put(event, part, want);
        r.after = want; r.kind = Kind::Hide;
        return r;
    }
    if (!own) return r;   // knife not drawn and the part is not ours: the game's state stays as it is
    if (word != own->written) { own->used = false; r.kind = Kind::Drop; return r; }
    const uint32_t want = word | kPartDraw;
    if (!m.Compare32(part + kBeFlag, word, want)) { r.kind = Kind::RaceLost; return r; }
    own->used = false;
    r.after = want; r.kind = Kind::Restore;
    return r;
}
}  // namespace link_runtime::event_sheath

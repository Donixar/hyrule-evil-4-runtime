// dll/case_items.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace link_runtime::case_items {
// The Piece of Heart (0x1C) and its three mixes lie 2 x 1 in the attache case, each with its own model
// (docs/heart-items-case-2026-10-12.md; the Piece of Heart's own spec is docs/piece-of-heart-case-2026-10-12.md;
// scripts/case_heart_items.py builds the data and checks these numbers against the pinned image).
//  1. piece_info (bio4.exe .data, read by pzlPlayer before pieceTblInit, shared by the case, Mercenaries' case and
//     the Merchant): each item's row 1 x 2 -> 2 x 1, once at startup (Qualify + ApplyRows).
//  2. After pieceTblInit (cdecl, SUB_SCREEN* wk; the case DAT at wk+0x1E0) each row's model pointer (+0x50) is
//     set to DAT member 2 * id when that member is a case BIN (DecideModels): what pieceTblInit's default case
//     (model = id) would do, without the .text switch byte the first spec patched. A DAT without the models
//     (vanilla, or another mod's) keeps the native jar / herb models: no member is read as the wrong type.
//  3. When the case opens (pzlPlayer init, thiscall, before it reads the records) every saved record of these
//     items laid out 1 x 2 gets a quarter turn (MigrateRecord): the same cells under the 2 x 1 shape.
//  4. A mix (2-nov-2026): cItemMgr::combine only changes the id of the piece it lands on, so a heart made on a herb
//     or jar kept its 1 x 2 row: footprint and cursor upright, the 2 x 1 model drawn half a cell off, until the case
//     was reopened (MigrateRecord). Both mixes (the drop at 0x38C826 and the Combine menu at 0x38D3BD) then call
//     pieceModelSet (cdecl, piece) for the piece whose id changed; before it a heart piece on another row gets its
//     heart row and a quarter turn: the same cells (the init, new-item and Merchant calls find the heart row already).
// No hook is installed here: the functions are pure over injected memory (see the wiring in the doc).
constexpr uint8_t kDefaultCase = 0x07;     // pieceTblInit switch: model = id
struct Item {
    uint16_t id;
    uint32_t row_rva;        // piece_info row of the item
    uint16_t model_member;   // 2 * id: the BIN the default case would read
    uint32_t switch_rva;     // pieceTblInit's byte index for the id (checked, never written)
    uint8_t native_case;     // its native case: 2 green herb's model 6, 4 the herbs' jar (model 0x12)
};
constexpr Item kItems[4] = {
    {0x1C, 0x80C2E8, 0x38, 0x388B48, 0x02},   // Piece of Heart (Yellow Herb)
    {0x15, 0x80C4F8, 0x2A, 0x388B41, 0x04},   // G+R+Y: TP's Heart Container
    {0x16, 0x80C398, 0x2C, 0x388B42, 0x04},   // G+Y: the container with a small green jar
    {0xA8, 0x80C3F0, 0x150, 0x388BD4, 0x04},  // R+Y: the TotK-style container
};
namespace rva {
constexpr uint32_t kPieceInfo = 0x80B1B8;     // 64 rows of 0x58, then id 0xFFFF
constexpr uint32_t kRowSize = 0x58, kRowCount = 64;
constexpr uint32_t kRowModel = 0x50;          // BIN pointer (pieceTblInit fills it)
constexpr uint32_t kPieceTblInit = 0x388A10;  // void __cdecl pieceTblInit(SUB_SCREEN* wk)
constexpr uint32_t kTblInitDatLoad = 0x388ABF;// mov edi, [edx+0x1E0]: the case DAT
constexpr uint32_t kSwitchIndex = 0x388B2C;   // byte per id 0..0xA8
constexpr uint32_t kJumpTable = 0x388B0C;     // 8 cases; [7] = the default case
constexpr uint32_t kDefaultTarget = 0x388A89; // mov ecx, ebx (model = id)
constexpr uint32_t kPzlPlayerInit = 0x28A9B0; // thiscall (ecx = pzlPlayer, int kind), ret 4: reads ItemMgr records
constexpr uint32_t kItemMgr = 0x870FD4;       // ItemMgr global (preferred VA 0xC70FD4)
constexpr uint32_t kItemArray = 0x14, kItemCount = 0x1C;   // cItem* and int, offsets in ItemMgr
constexpr uint32_t kWkDat = 0x1E0;            // SUB_SCREEN::pSwitchDat
constexpr uint32_t kPreferredBase = 0x400000;
constexpr uint32_t kModelSet = 0x389AE0;      // void __cdecl pieceModelSet(piece): the model of piece+0x24's id
constexpr uint32_t kSetDir = 0x289AA0;        // thiscall (ecx = piece, s8 turn), ret 4: centre from the row, rotated
}
// pzlPlayer piece (0x2C bytes): +4 the row's size field (row + 4), +0x20 turn, +0x24 the cItem.
constexpr uint32_t kPieceRowLink = 4, kPieceRow = 0x04, kPieceTurn = 0x20, kPieceItem = 0x24;
// Bytes that do not move under relocation (absolute operands left out).
constexpr uint8_t kTblInitHead[16] = {0x55, 0x8B, 0xEC, 0x51, 0x33, 0xC0, 0xB9, 0xFF,
                                      0xFF, 0x00, 0x00, 0x89, 0x45, 0xFC, 0x66, 0x39};
constexpr uint8_t kDatLoad[6] = {0x8B, 0xBA, 0xE0, 0x01, 0x00, 0x00};
constexpr uint8_t kDefaultCode[2] = {0x8B, 0xCB};
constexpr uint8_t kPzlInitHead[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C};
constexpr uint8_t kModelSetHead[7] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C, 0xA1};   // then the cookie's address
constexpr uint8_t kModelSetTail[12] = {0x33, 0xC5, 0x89, 0x45, 0xFC, 0x56, 0x8B, 0x75, 0x08, 0x8B, 0x46, 0x24};
constexpr uint32_t kModelSetTailAt = 0x0B;
constexpr uint8_t kSetDirHead[16] = {0x55, 0x8B, 0xEC, 0x8B, 0x41, 0x04, 0xD9, 0x40,
                                     0x04, 0x53, 0x8A, 0x5D, 0x08, 0xD9, 0x59, 0x0C};
constexpr size_t kRowCheck = 18;           // id, size, centre, shape "11"
constexpr size_t kRowWrite = 12;           // +4: size_x, size_y, pad, centre_x, centre_y
constexpr uint32_t kRowWriteAt = 4;
constexpr size_t kItemSize = 14;           // cItem

inline void RowBefore(uint16_t id, uint8_t out[kRowCheck]) {
    const uint8_t tail[16] = {0x01, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3F, '1', '1'};
    out[0] = uint8_t(id); out[1] = uint8_t(id >> 8); out[2] = out[3] = 0;
    std::memcpy(out + 4, tail, 14);
}
inline void RowAfter(uint16_t id, uint8_t out[kRowCheck]) {
    const uint8_t tail[16] = {0x02, 0x01, 0, 0, 0, 0, 0, 0x3F, 0, 0, 0, 0, '1', '1'};
    out[0] = uint8_t(id); out[1] = uint8_t(id >> 8); out[2] = out[3] = 0;
    std::memcpy(out + 4, tail, 14);
}
enum class RowState : uint8_t { Before, After, Other };
inline RowState Classify(const uint8_t* row, uint16_t id) {
    uint8_t want[kRowCheck];
    RowBefore(id, want);
    if (std::memcmp(row, want, kRowCheck) == 0) return RowState::Before;
    RowAfter(id, want);
    if (std::memcmp(row, want, kRowCheck) == 0) return RowState::After;
    return RowState::Other;
}

// --- 1. the image: everything checked before anything is written ---------------------------------------------
// Mem: bool Read(uint32_t rva, void* out, size_t n) const; bool Write(uint32_t rva, const void* in, size_t n).
// `base` is the module's actual base: the jump table holds relocated absolute addresses.
struct Plan {
    bool ok = false;
    const char* reason = "not qualified";
    uint8_t write_mask = 0;      // bit i: kItems[i]'s row still 1 x 2
};
template<class Mem> Plan Qualify(const Mem& m, uint32_t base) {
    Plan p;
    uint8_t buf[32];
    if (!m.Read(rva::kPieceTblInit, buf, sizeof kTblInitHead) ||
        std::memcmp(buf, kTblInitHead, sizeof kTblInitHead)) { p.reason = "pieceTblInit head"; return p; }
    if (!m.Read(rva::kTblInitDatLoad, buf, sizeof kDatLoad) ||
        std::memcmp(buf, kDatLoad, sizeof kDatLoad)) { p.reason = "pieceTblInit DAT load"; return p; }
    if (!m.Read(rva::kPzlPlayerInit, buf, sizeof kPzlInitHead) ||
        std::memcmp(buf, kPzlInitHead, sizeof kPzlInitHead)) { p.reason = "pzlPlayer init head"; return p; }
    uint32_t target = 0;
    if (!m.Read(rva::kJumpTable + 4 * kDefaultCase, &target, 4) || target != base + rva::kDefaultTarget) {
        p.reason = "default case jump"; return p;
    }
    if (!m.Read(rva::kDefaultTarget, buf, 2) || std::memcmp(buf, kDefaultCode, 2)) {
        p.reason = "default case code"; return p;
    }
    uint16_t end = 0;
    if (!m.Read(rva::kPieceInfo + rva::kRowSize * rva::kRowCount, &end, 2) || end != 0xFFFF) {
        p.reason = "piece_info end"; return p;
    }
    for (unsigned i = 0; i < 4; ++i) {
        const Item& it = kItems[i];
        if ((it.row_rva - rva::kPieceInfo) % rva::kRowSize ||
            it.row_rva >= rva::kPieceInfo + rva::kRowSize * rva::kRowCount) { p.reason = "row place"; return p; }
        if (it.switch_rva != rva::kSwitchIndex + it.id) { p.reason = "switch place"; return p; }
        uint8_t sw = 0;
        if (!m.Read(it.switch_rva, &sw, 1) || (sw != it.native_case && sw != kDefaultCase)) {
            p.reason = "switch byte"; return p;
        }
        if (!m.Read(it.row_rva, buf, kRowCheck)) { p.reason = "row read"; return p; }
        const RowState s = Classify(buf, it.id);
        if (s == RowState::Other) { p.reason = "row bytes"; return p; }
        if (s == RowState::Before) p.write_mask |= uint8_t(1u << i);
    }
    for (uint32_t r = 0; r < rva::kRowCount; ++r) {           // each item has exactly one row
        uint16_t id = 0;
        if (!m.Read(rva::kPieceInfo + r * rva::kRowSize, &id, 2)) { p.reason = "row id"; return p; }
        for (const Item& it : kItems)
            if (id == it.id && rva::kPieceInfo + r * rva::kRowSize != it.row_rva) { p.reason = "second row"; return p; }
    }
    p.ok = true; p.reason = "ok";
    return p;
}
template<class Mem> bool ApplyRows(Mem& m, const Plan& p) {
    if (!p.ok) return false;
    for (unsigned i = 0; i < 4; ++i) {
        if (!(p.write_mask & (1u << i))) continue;
        uint8_t row[kRowCheck];
        RowAfter(kItems[i].id, row);
        if (!m.Write(kItems[i].row_rva + kRowWriteAt, row + kRowWriteAt, kRowWrite)) return false;
    }
    return true;
}

// --- 2. the models, after pieceTblInit --------------------------------------------------------------------------
// The case DAT: u32 count, 12 bytes, count u32 offsets from 0x10, count 4-byte tags. A member is the item's model
// when tagged "BIN\0", not empty (the next member starts later), 0x20-aligned and starting like member 0x0C (the
// herb sprig, a case BIN every case DAT has). `size` bounds every read (the DAT the game read, or the header part
// the host has copied out); 0 offset = keep the native model.
struct Models { uint32_t offset[4] = {0, 0, 0, 0}; };
inline uint32_t U32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline bool MemberBin(const uint8_t* dat, size_t size, uint32_t member, uint32_t& offset) {
    offset = 0;
    if (!dat || size < 0x10) return false;
    const uint32_t n = U32(dat);
    if (n < 0x10 || n > 0x1000 || member + 1 >= n || 0x10 + 8ull * n > size) return false;
    const uint8_t* offs = dat + 0x10;
    const uint8_t* tags = offs + 4 * n;
    if (std::memcmp(tags + 4 * member, "BIN\0", 4)) return false;
    const uint32_t at = U32(offs + 4 * member), next = U32(offs + 4 * (member + 1)), ref = U32(offs + 4 * 0x0C);
    if (at % 0x20 || next <= at || uint64_t(at) + 4 > size || uint64_t(ref) + 4 > size) return false;
    if (std::memcmp(tags + 4 * 0x0C, "BIN\0", 4) || std::memcmp(dat + at, dat + ref, 4)) return false;
    offset = at;
    return true;
}
// Bytes of a case DAT in memory up to its last member's start + 4: the bound DecideModels reads within (the game
// keeps no size next to wk+0x1E0). 0 when the header is not a case DAT's.
inline size_t DatExtent(const uint8_t* dat) {
    if (!dat) return 0;
    const uint32_t n = U32(dat);
    if (n < 0x10 || n > 0x1000) return 0;
    uint64_t end = 0x10 + 8ull * n;
    for (uint32_t i = 0; i < n; ++i) {
        const uint64_t at = U32(dat + 0x10 + 4 * i);
        if (at < 0x10 + 8ull * n || at > 0x4000000) return 0;     // offsets past the header, below 64 MB
        if (at + 4 > end) end = at + 4;
    }
    return size_t(end);
}
inline Models DecideModels(const uint8_t* dat, size_t size) {
    Models out;
    for (unsigned i = 0; i < 4; ++i) MemberBin(dat, size, kItems[i].model_member, out.offset[i]);
    return out;
}

// --- 3. the saves -----------------------------------------------------------------------------------------------
// cItem: +0 u16 id, +4 flags (bit 0 alive), +0xA x, +0xB y (centre in half cells), +0xC orientation, +0xD board
// (1 = in the case). A 1 x 2 at an even orientation has its centre on (even, odd) half cells, a 2 x 1 on (odd,
// even); odd orientations swap them. A record laid out for the other shape gets orientation ^ 1 (a quarter turn
// in the same group, 0-3 or 4-7): the same cells. Idempotent; parities no 2-cell piece has are left alone.
// Same rule as scripts/case_heart_piece.py migrate_orient.
inline int MigrateOrient(int x, int y, int orient, bool wide) {
    const bool lying = (x & 1) == 1 && (y & 1) == 0;
    const bool standing = (x & 1) == 0 && (y & 1) == 1;
    const bool turned = (orient & 1) != 0;
    const bool covers_wide = wide ? !turned : turned;
    if ((lying && !covers_wide) || (standing && covers_wide)) return orient ^ 1;
    return orient;
}
inline bool IsCaseItem(uint16_t id) {
    for (const Item& it : kItems) if (it.id == id) return true;
    return false;
}
inline bool MigrateRecord(uint8_t* rec) {
    uint16_t id;
    std::memcpy(&id, rec, 2);
    if (!IsCaseItem(id) || !(rec[4] & 1) || rec[0xD] != 1) return false;
    const int8_t x = int8_t(rec[0xA]), y = int8_t(rec[0xB]), o = int8_t(rec[0xC]);
    if (o < 0 || o > 7) return false;
    const int now = MigrateOrient(x, y, o, true);
    if (now == o) return false;
    rec[0xC] = uint8_t(now);
    return true;
}
// Every record of the ItemMgr array (count bounded by the caller's own check, here 1..1024).
inline unsigned MigrateAll(uint8_t* array, int count) {
    if (!array || count <= 0 || count > 1024) return 0;
    unsigned changed = 0;
    for (int i = 0; i < count; ++i) changed += MigrateRecord(array + kItemSize * size_t(i)) ? 1 : 0;
    return changed;
}

// --- 4. a mix ----------------------------------------------------------------------------------------------------
template<class Mem> bool QualifyCombine(const Mem& m) {
    uint8_t buf[16];
    return m.Read(rva::kModelSet, buf, sizeof kModelSetHead) && !std::memcmp(buf, kModelSetHead, sizeof kModelSetHead) &&
           m.Read(rva::kModelSet + kModelSetTailAt, buf, sizeof kModelSetTail) &&
           !std::memcmp(buf, kModelSetTail, sizeof kModelSetTail) &&
           m.Read(rva::kSetDir, buf, sizeof kSetDirHead) && !std::memcmp(buf, kSetDirHead, sizeof kSetDirHead);
}
struct CombineFix {
    bool row = false;        // write row_ptr into the piece (+4)
    bool turn = false;       // then setDir(dir): a 1 x 2 at turn d and a 2 x 1 at turn d ^ 1 cover the same cells
    uint32_t row_ptr = 0;
    int8_t dir = 0;
};
// id: the result's item; row_ptr: the piece's +4 (row + 4); size: the bytes it points at; dir: the piece's turn.
inline CombineFix DecideCombineFix(uint16_t id, uint32_t row_ptr, int8_t size_x, int8_t size_y, int dir, uint32_t base) {
    CombineFix f;
    if (!row_ptr || dir < 0 || dir > 7) return f;
    for (const Item& it : kItems) {
        if (it.id != id) continue;
        const uint32_t want = base + it.row_rva + kPieceRowLink;
        if (row_ptr == want) return f;
        f.row = true; f.row_ptr = want;
        if (size_x == 1 && size_y == 2) { f.turn = true; f.dir = int8_t(dir ^ 1); }
        return f;
    }
    return f;
}
}

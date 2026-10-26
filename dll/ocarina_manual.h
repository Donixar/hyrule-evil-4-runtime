// dll/ocarina_manual.h
#pragma once
// "Ocarina Manual" in the Files menu (17-oct-2026, docs/manual-ocarina-2026-10-17.md). File_31 (item 0xFD,
// file number 31) is never given in the campaign and UHD never lists it. With [Ocarina] Manual=1 (default)
// the DLL (1) adds 0xFD as row 13 of the first Files category, (2) points file 31's rows of the four file
// tables at four pages (messages 0xA4..0xA7 of ss_file.dat, pictures SS/eng/f31a..d.tpl; data layer
// ocarina-manual/data-01) and (3) gives 0xFD on the first gameplay tick of each load where Link owns the
// ocarina (0x0D) and lacks the manual. Pure decisions and byte tables here; link_runtime.cpp writes them.
//
// UHD bio4.exe (SHA 19aed4af...), preferred base 0x400000 (RVAs below):
//  - 0x36B050 category size: case 0 `mov eax,0xC` (eight more copies: kCategoryZeroSizeSites); 0x36B150 builds the per-category id lists on the stack
//    (category 0 = 0xAC..0xB7 at [ebp-0x84], zeroing [ebp-0x6C] after them) and sorts each by the same
//    size (case 0 `mov eax,0xC` at 0x36B2A6). Row 13 = word [ebp-0x6C]: the two zeroing stores (6 bytes)
//    become `mov word [ebp-0x6C],0xFD`; slots past the size are never read.
//  - fileId2No (0x36B760) maps 0xFD -> 31 and Mission Directives 2 (0x107, Separate Ways) -> 31 as well,
//    so file 31's rows (index 30) of fileInfo 0x81BFC0 (u8[4]), fileMsgNum 0x81C048 (u8[8] per language)
//    and fileTplJpn/Eng 0x81C158/0x81C268 (count + first page of each picture) are shared with Ada's
//    directive. The rows hold the manual only outside Separate Ways: GLOBAL_WK (*0x806F3C) +0x54 bit 0x1000,
//    the bit 0x36B7F0 tests to give Ada her own category. In Separate Ways the native rows are put back.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "native_pins.h"

namespace link_runtime::ocarina::manual {
constexpr uint16_t kManualItem = 0xFD;
constexpr uint16_t kOcarinaItem = 0x0D;
constexpr uint8_t kMessageBase = 0xA4;
constexpr uint8_t kPages = 4;
constexpr uint32_t kGlobalSlotRva = 0x806F3C;
constexpr uint32_t kSeparateWaysOffset = 0x54, kSeparateWaysBit = 0x1000;

struct Patch { uint32_t rva; uint8_t size; uint8_t native[8]; uint8_t manual[8]; };
// List: category 0 gets a 13th row holding 0xFD (code, written once at start).
// files-menu-fix-01 (20-oct-2026, docs/archivos-desfase-2026-10-20.md): the game holds the category-0 size
// (`mov reg,0xC`, case 0 of a 4-way switch on the category) in NINE places, and every one must agree. The
// menu decides "this category has the chapter-memo row on top" by comparing two copies: the plain size
// and the size +1 for the current part's category (byte 0xC7AE84 - 1, outside Separate Ways). Equal = rows
// map 1:1 to the id list; different = row 0 is the memo and row r shows/opens list[r-1]. The first build
// patched only the list builder (0x36B05D) and its sort (0x36B2A7), so the selection (0x36D4F5 fn
// 0x36B050 vs 0x36D4FC fn 0x36B0B0) and the drawing (inline pairs in 0x36B8C0) disagreed by one row:
// the cursor opened the neighbouring file and the end row hit an empty (0xFFFF) slot. All nine now say 13.
inline constexpr uint32_t kCategoryZeroSizeSites[] = {
    0x36B05D, 0x36B0C3, 0x36B2A7, 0x36BC74, 0x36BCAB, 0x36BD1D, 0x36BD51, 0x36BE79, 0x36BEB0};
inline constexpr Patch kList[] = {
    {0x36B05D, 1, {0x0C}, {0x0D}},   // 0x36B050 size (list builder loop, selection)
    {0x36B2A7, 1, {0x0C}, {0x0D}},   // sort count in the builder 0x36B150
    {0x36B19A, 6, {0x89, 0x45, 0x94, 0x89, 0x45, 0x98}, {0x66, 0xC7, 0x45, 0x94, 0xFD, 0x00}},
    {0x36B0C3, 1, {0x0C}, {0x0D}},   // 0x36B0B0 size + memo row (selection, cursor clamp, scroll bar)
    {0x36BC74, 1, {0x0C}, {0x0D}},   // row draw 0x36B8C0, row 0: plain size
    {0x36BCAB, 1, {0x0C}, {0x0D}},   //   row 0: size + memo row
    {0x36BD1D, 1, {0x0C}, {0x0D}},   //   rows 1..: plain size
    {0x36BD51, 1, {0x0C}, {0x0D}},   //   rows 1..: size + memo row
    {0x36BE79, 1, {0x0C}, {0x0D}},   //   cursor highlight: plain size
    {0x36BEB0, 1, {0x0C}, {0x0D}}};  //   cursor highlight: size + memo row
// File 31 rows (data): msgBase 0xA4, colour 0, text type 2, manual layout 1 (like the Playing Manuals);
// four pages in every language; pictures a..d start on pages 1..4.
inline constexpr Patch kRows[] = {
    {0x81C038, 4, {0x9C, 0x00, 0x01, 0x00}, {kMessageBase, 0x00, 0x02, 0x01}},
    {0x81C138, 8, {1, 1, 1, 1, 1, 1, 1, 1}, {kPages, kPages, kPages, kPages, kPages, kPages, kPages, kPages}},
    {0x81C248, 8, {1, 1, 0, 0, 0, 0, 0, 0}, {kPages, 1, 2, 3, 4, 0, 0, 0}},
    {0x81C358, 8, {1, 1, 0, 0, 0, 0, 0, 0}, {kPages, 1, 2, 3, 4, 0, 0, 0}}};
// Context pins (native bytes around every patch site, the Separate Ways test and fileId2No's 0xF4..0xFD
// and 0x106..0x10A ranges). Checked before anything is written; the patch sites themselves are checked
// against native or manual bytes (a reload of the DLL finds its own patch).
inline constexpr native_pins::Pin kPins[] = {
    {0x36B050, "83f8037732ff24858cb07600"},
    {0x36B062, "8b0d3c6fc000b8030000003a817c8400001bc0f7d883c009c3b807000000c3b805000000c333c0c3"},
    {0x36B16D, "c7857cffffffac00ad00c74580ae00af00c74584b000b100c74588b200b300c7458cb400b500c74590b600b700"},
    {0x36B1A0, "c7459c48004900c745a04a004b00c745a44c004d00c745a84e005000c745acf4004f008945b08945b48945b8"},
    {0x36B29A, "83ff037733ff24bd0cb37600"},
    {0x36B2AB, "eb278b153c6fc000b1033a8a7c8400001bc0f7d883c009eb10b807000000eb09b805000000eb0233c0"},
    {0x36B7F0, "8b0d3c6fc000f7415400100000750e85c07e1283f8037e0b83f8057508b803000000c348c333c0c3"},
    {0x36B760, "8d8854ffffff6683f90b77090fb7c02dab000000c38d50b86683fa0877070fb7c083e83bc38d880cffffff6683f90977090fb7c02dde000000c38d90fafeffff6683fa0477090fb7c02de8000000c333c0c3"},
    // files-menu-fix-01: the other seven size copies (sites excluded) and the selection that compares them.
    {0x36B0B0, "8b0d3c6fc00083fa037730ff249504b17600b8"},
    {0x36B0C4, "000000eb24b8030000003a817c8400001bc0f7d883c009eb10b807000000eb09b805000000eb0233c0f7415400100000750d0fbe0d84aec700493bd1750140c3c2b07600c9b07600ddb07600e4b07600"},
    {0x36BC60, "8b45f80fbe780183ff037730ff24bd50bf7600b9"},
    {0x36BC75, "000000eb24b9030000003a8a7c8400001bc9f7d983c109eb10b907000000eb09b905000000eb0233c983ff037730ff24bd60bf7600b8"},
    {0x36BCAC, "000000eb24b8030000003a827c8400001bc0f7d883c009eb10b807000000eb09b805000000eb0233c0f74254001000008bf0750f0fbe1d84aec7004b3bfb75038d70013bce7416e8ec0ecaff83f8180f94c180c108884df0e9c10000008b45f80fbe480183f903772dff248d70bf7600be"},
    {0x36BD1E, "000000eb21b0033a827c8400001bf6f7de83c609eb10be07000000eb09be05000000eb0233f683f9037730ff248d80bf7600b8"},
    {0x36BD52, "000000eb24b8030000003a827c8400001bc0f7d883c009eb10b807000000eb09b805000000eb0233c0f74254001000008bf8750f0fbe1d84aec7004b3bcb75038d7801c1e104034dfc3bf7740d0fb70c8d44b1c700894d08eb0b0fb7048d48b1c700894508"},
    {0x36BE5C, "8b45f80fbe40018b1d3c6fc0008945e883f8037730ff248590bf7600b9"},
    {0x36BE7A, "000000eb24b9030000003a8b7c8400001bc9f7d983c109eb10b907000000eb09b905000000eb0233c983f803772dff2485a0bf7600b8"},
    {0x36BEB1, "000000eb21b2033a937c8400001bc0f7d883c009eb10b807000000eb09b805000000eb0233c0f74354001000008bd08955e475120fbe1584aec7004a3955e88d500174028bd03bca"},
    {0x36BF50, "73bc76007abc76008ebc760095bc7600aabc7600b1bc7600c5bc7600ccbc76001cbd760023bd760034bd76003bbd760050bd760057bd76006bbd760072bd760078be76007fbe760093be76009abe7600afbe7600b6be7600c7be7600cebe7600"},
    {0x36D4EF, "0fbe56018bc2e856dbffff8bf8e8afdbffff3bf8747c"},
    {0x36D581, "0fbe56018bc2e8c4daffff8bf8e81ddbffffc1e2043bf8740f0fbec303d00fb71c9544b1c700eb0d0fbecb03d10fb71c9548b1c700"},
    {0x81C030, "960001009c000100"},
    {0x81C03C, "9c0001009c0001009c000100"}};

// Which bytes a patch site holds: native, manual or neither (unknown: never touched).
enum class Site : uint8_t { Native, Manual, Unknown };
inline Site Classify(const uint8_t* bytes, const Patch& p) {
    if (std::memcmp(bytes, p.native, p.size) == 0) return Site::Native;
    if (std::memcmp(bytes, p.manual, p.size) == 0) return Site::Manual;
    return Site::Unknown;
}
// Every site of a table must be native or manual before any is written (no half-known state).
template<class Mem, size_t N> bool SitesKnown(Mem& m, uint32_t base, const Patch (&table)[N]) {
    for (const auto& p : table) {
        uint8_t now[8]{};
        if (!m.Read(base + p.rva, now, p.size) || Classify(now, p) == Site::Unknown) return false;
    }
    return true;
}
// Writes the manual (or native) bytes where they are not already; returns the number of sites written,
// or -1 when a site is unknown or a write fails (sites written before the failure stay written).
template<class Mem, size_t N> int Apply(Mem& m, uint32_t base, const Patch (&table)[N], bool manual) {
    if (!SitesKnown(m, base, table)) return -1;
    int written = 0;
    for (const auto& p : table) {
        uint8_t now[8]{};
        if (!m.Read(base + p.rva, now, p.size)) return -1;
        const uint8_t* want = manual ? p.manual : p.native;
        if (std::memcmp(now, want, p.size) == 0) continue;
        if (!m.Write(base + p.rva, want, p.size)) return -1;
        ++written;
    }
    return written;
}

// Rows follow the game mode: manual rows in the main game, native rows in Separate Ways.
class RowSync {
    int8_t state = -1;   // -1 unknown, 0 native, 1 manual
public:
    void Reset() { state = -1; }
    // Returns true when the host must write the rows for `separate_ways` (state changed or unknown).
    bool Want(bool separate_ways, bool& manual) {
        manual = !separate_ways;
        const int8_t next = manual ? 1 : 0;
        if (state == next) return false;
        return true;
    }
    void Done(bool manual) { state = manual ? 1 : 0; }
};

// Host contract (game thread): LoadEpoch(), GameplayValid(), HasItem(id), GetItem(id,num), Log(text);
// the same accessors as ocarina_item.h OcarinaGrant (ItemMgr search/get thunks).
enum class Give : uint8_t { Waiting, NoOcarina, AlreadyOwned, Granted, GetFailed, Done };
// At most one native get(0xFD,1) per load epoch, only in gameplay, only when Link owns the ocarina and
// search() does not find the manual. The decision is taken once per epoch, so an ocarina granted later in
// the same load waits for the next load (the ocarina grant runs first on the same tick, so a fresh game
// gets both on its first gameplay tick).
template<class Host> class ManualGrant {
    bool decided = false; uint32_t epoch = 0;
public:
    void Reset() { decided = false; epoch = 0; }
    Give Tick(Host& h) {
        if (!h.GameplayValid()) return Give::Waiting;
        const uint32_t now = h.LoadEpoch();
        if (decided && now == epoch) return Give::Done;
        decided = true; epoch = now;
        if (!h.HasItem(kOcarinaItem)) { h.Log("ocarina manual waits for the ocarina"); return Give::NoOcarina; }
        if (h.HasItem(kManualItem)) { h.Log("ocarina manual present"); return Give::AlreadyOwned; }
        if (!h.GetItem(kManualItem, 1)) { h.Log("ocarina manual get failed"); return Give::GetFailed; }
        h.Log("ocarina manual granted"); return Give::Granted;
    }
};
}  // namespace link_runtime::ocarina::manual

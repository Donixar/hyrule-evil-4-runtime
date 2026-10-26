#pragma once
// Krauser's bow for Link, pilot without art (Donald 22-oct; docs/research/arco-krauser-2026-10-22.md). Krauser's native
// bow (item 0x52, weapon_no 0x1C, arrows 0x72, Em/wep28) has the left-hand hold and the string pull the Hero's Bow on
// the Mine Thrower lacks. Leon (pl_type 0) cannot equip it natively; two things stand in the way:
//  1. ReadWepData's file table per character: Leon's row 0x1C (VA 0xC11F10, RVA 0x811F10) is empty, Krauser's
//     (RVA 0x812530) is {0xC3 wep28.das, 0xC4 wep28.rel}. The pilot copies Krauser's 8 bytes into Leon's row, only
//     when Leon's is still all zero and Krauser's is the expected one (.data, written once at startup).
//  2. wep28 calls the player's setLeftHand(7). Leon's (0x754CB0, thiscall, ret 4) knows 0..5 and 0x63 and takes any
//     other value as a pointer (crash); Krauser's maps 7 to its member 0x11, Leon's type 1. A detour on Leon's turns
//     7 into 1 (every other value untouched; Krauser's own function is not hooked).
// The arrow model and its hand motion come from the player archive (members 133..136, only in pl0a): the pilot layer
// appends them to Link's archives (scripts/krauser_bow_pilot.py). INI [LinkRuntime] KrauserBowPilot: 1 on, else off.
#include "native_pins.h"
#include <cstdint>
#include <cstring>

namespace link_runtime::krauser_bow_pilot {
namespace rva {
constexpr uint32_t kLeonRow = 0x811F10, kKrauserRow = 0x812530, kSetLeftHand = 0x354CB0;
// piece_info (the case's piece shapes, UHD entries 0x58 bytes: u16 id, pad, size_x, size_y, pad, f32 centre x/y,
// shape[0x40], 8 bytes of model pointers that pieceTblInit fills): Krauser's bow 0x52 is 7x3, the Hero's Bow on the
// Mine Thrower 0x36 is 5x2 (Donald 23-oct: 7x3 is too much).
constexpr uint32_t kPieceBow = 0x80B948, kPieceHeroBow = 0x80B6E0;
// weaponFilename (cdecl void(char* name, int no), Sscrn/ss_model.cpp): the case's character view loads
// SS/cmn/ss_wepNN.dat for the equipped weapon. For Leon (pl_type 0, pG+0x4FC8) weapon 0x1C falls to ss_wep02 (the
// handgun), while the view's init for 0x1C is wep28Init, which reads ss_wep28's members: opening the case with the
// bow equipped crashed (23-oct, bio4+0x242158 on a garbage motion pointer). Giving it Krauser's ss_wep28 crashed too
// (bio4+0x25D170): Leon's view init leonModelInit (cdecl void(u16 no, u16 type), 0x387880) has no case for 0x1C, and
// ss_wep28's hand model pairs with Krauser's player archive. So for Leon the bow's view is the Mine Thrower's: the
// init is called with 0x0E and the file is ss_wep14.dat, which already carries the Hero's Bow (bow_rescale.py, the
// case view Donald approved on 22-oct).
constexpr uint32_t kWeaponFilename = 0x3810C0, kGlobalSlot = 0x806F3C, kPlType = 0x4FC8, kLeonModelInit = 0x387880;
}  // namespace rva
constexpr int kBowWeapon = 0x1C, kMineThrower = 0x0E;
inline constexpr char kBowCaseFile[] = "SS/cmn/ss_wep14.dat";
// The file the case should load: the Mine Thrower's (Hero's Bow) for Leon's weapon 0x1C, else what the game chose.
inline bool BowCaseFile(int pl_type, int weapon_no) {return pl_type == 0 && weapon_no == kBowWeapon;}
// leonModelInit's weapon: the bow's 0x1C becomes the Mine Thrower's 0x0E (the low 16 bits are the u16 argument).
inline uint32_t LeonViewWeapon(uint32_t no) {return (no & 0xFFFF) == uint32_t(kBowWeapon) ? (no & 0xFFFF0000u) | kMineThrower : no;}
constexpr uint32_t kPieceShape = 0x04, kPieceShapeEnd = 0x50;   // size, centre and shape: the bytes copied
constexpr int kBowHand = 7, kLeonHand = 1;
inline int RemapLeftHand(int type) {return type == kBowHand ? kLeonHand : type;}
inline constexpr unsigned char kKrauserRowBytes[8] = {0xC3, 0x00, 0xC4, 0x00, 0x00, 0x00, 0x00, 0x00};
inline constexpr native_pins::Pin kPins[] = {
    // Leon's setLeftHand: prologue, the 0x7E4 work, the 0x63 restore, the 0..5 jump table, ret 4 at the end.
    {rva::kSetLeftHand, "558bec5356578bf98b87e40700008b402033f63bc6741850"},
    {rva::kSetLeftHand + 0x2F, "8b4d0883f9637509"},
    {rva::kSetLeftHand + 0x40, "83f9057761"},
    // weaponFilename: prologue, then movzx eax, byte [ecx+0x4FC8] (pl_type); cmp eax, 5.
    {rva::kWeaponFilename, "558bec"},
    {rva::kWeaponFilename + 0x09, "0fb681c84f000083f805"},
    // leonModelInit: prologue; mov ebx, [ebp+8]; movzx esi, bx; cmp esi, 0x2E (the weapon switch).
    {rva::kLeonModelInit, "558bec83ec1c"},
    {rva::kLeonModelInit + 0x1CC, "8b5d080fb7f383fe2e"},
};
enum class Verdict : uint8_t {Installed, Off, Pins, Table, Write, Hook};
inline const char* Name(Verdict v) {
    switch (v) {
    case Verdict::Installed: return "installed"; case Verdict::Off: return "off"; case Verdict::Pins: return "pins";
    case Verdict::Table: return "table"; case Verdict::Write: return "write"; default: return "hook";
    }
}
// The table row: Leon's must be empty and Krauser's the expected one; then Leon's gets Krauser's. Already patched
// (Leon's == Krauser's) counts as done. Mem: Read/Write(address, buffer, n).
template <class Mem> Verdict PatchRow(Mem& m, uint32_t base) {
    unsigned char leon[8]{}, krauser[8]{};
    if (!m.Read(base + rva::kLeonRow, leon, 8) || !m.Read(base + rva::kKrauserRow, krauser, 8)) return Verdict::Table;
    if (std::memcmp(krauser, kKrauserRowBytes, 8)) return Verdict::Table;
    if (!std::memcmp(leon, kKrauserRowBytes, 8)) return Verdict::Installed;
    static const unsigned char zero[8]{};
    if (std::memcmp(leon, zero, 8)) return Verdict::Table;
    return m.Write(base + rva::kLeonRow, kKrauserRowBytes, 8) ? Verdict::Installed : Verdict::Write;
}
// The bow's case piece takes the Hero's Bow's 5x2 shape (size, centre, shape; never the model pointers). Both entries
// must be the expected ones (ids, 7x3 / 5x2), or the bow is already 5x2 (done).
template <class Mem> Verdict PatchPiece(Mem& m, uint32_t base) {
    unsigned char bow[kPieceShapeEnd]{}, hero[kPieceShapeEnd]{};
    if (!m.Read(base + rva::kPieceBow, bow, sizeof(bow)) || !m.Read(base + rva::kPieceHeroBow, hero, sizeof(hero)))
        return Verdict::Table;
    if (bow[0] != 0x52 || bow[1] != 0 || hero[0] != 0x36 || hero[1] != 0 || hero[4] != 5 || hero[5] != 2) return Verdict::Table;
    if (!std::memcmp(bow + kPieceShape, hero + kPieceShape, kPieceShapeEnd - kPieceShape)) return Verdict::Installed;
    if (bow[4] != 7 || bow[5] != 3) return Verdict::Table;
    return m.Write(base + rva::kPieceBow + kPieceShape, hero + kPieceShape, kPieceShapeEnd - kPieceShape) ? Verdict::Installed
                                                                                                         : Verdict::Write;
}
}  // namespace link_runtime::krauser_bow_pilot

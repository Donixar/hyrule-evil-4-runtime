#pragma once
// Master Sword Blade Lock, candidate 1 (docs/swordlock-cand1-2026-11-04.md; plan steps 3 and 4 of
// docs/debates/2026-11-04-forcejeo/acta.md). Lab DLL only: link_runtime.cpp includes it inside #if LINK_SWORD_LOCK_LAB.
// Pure: no hooks, no process access.
//
// Step 3, the duel's difficulty from the game MODE, never from the adaptive rank:
//   pG+0x847C (UHD; GC GLOBAL_WK::game_mode 0x8354, re4-decomp include/global.h:230) is the difficulty the player chose.
//   Values, from the UHD code itself (bio4.exe SHA 19aed4af..., RVAs at base 0x400000):
//     * the title's difficulty choice writes exactly 1, 3, 5 and 6 (0x2D63E2 mov [pG+0x847C],1; 0x2D63F3 / 0x2D643C ,5;
//       0x2D6403 / 0x2D645E ,6; 0x2D63D1 / 0x2D644D ,3; 0x2D6422 5 or 6 by a flag); the image's 13 immediate byte stores
//       to [reg+0x847C] hold only 1, 3, 5 and 6 (the others: 0x23E1F3 / 0x23E205's defaults, 0x23E30A, 0x252D83,
//       0x2E08F6 and 0x2E0936, all 5);
//     * game init (GC game.cpp:247-256, UHD 0x23E1C9..0x23E205): mode 6 sets SYS_HARD_MODE (pG+0x54 |= 0x20, the
//       Professional flag) and a mode 0 becomes 5 (3 with pG+0x4FA3 set);
//     * LifeDownSet2 takes an extra damage factor only with mode <= 1 (0x1B5BB2 cmp [pG+0x847C],1;
//       docs/defense-native-qualification.md:51-53), and ten ammo counts depend on it with 5 as Normal
//       (docs/ocarina-item-census-2026-10-11.md:38);
//     * GC's own names (title.cpp:1545 game_mode_tbl): 1 VERY_EASY, 3 EASY, 5 NORMAL, 6 HARD.
//   So 1 Amateur, 3 Easy, 5 Normal, 6 Professional. Donald's 3-nov log (mode=5 on every line, level 1..5): he plays
//   Normal; the rank alone gave him Easy duels (level <= 3). The duel: 1 and 3 Easy, 5 Normal, 6 Pro; any other value
//   or an unread byte (-1) Normal. The rank pG+0x4F98 is still logged (level=), never used.
//
// Step 4, the admission closed to the design's owners:
//   * a chainsaw block opens the duel only for Dr. Salvador (em10 id 0x15); the Bella sisters (0x16) and any other
//     chainsaw owner keep the 3.5.0 parry, refused why=owner (an unread owner too: fail closed);
//   * Dark Link stays behind [Combat] SwordLockDarkLink (sword_lock_darklink.h);
//   * with a pad (the device word 0x806F44 = 2 XInput / 3 DirectInput, or the pad column selector 0xE224B0 = 1, as the
//     duel's icons read them, sword_lock_icons.h KeyboardGlyphs) or either word unread, the duel does not open
//     (why=device) and the parry is 3.5.0's, unless [Combat] SwordLockPad=1 (missing = 0 = closed: the team's
//     recommendation, pending Donald's decision in step 12).
// Tests: tests/native/test_sword_lock_gate.cpp (scripts/test_sword_lock_gate.bat).
#include "sword_lock_duel.h"
#include "sword_lock_icons.h"
#include <cstdint>
#include <cwchar>

namespace link_runtime::sword_lock::gate {
namespace sd = link_runtime::sword_lock::duel;

// ---- step 3: the mode -----------------------------------------------------------------------------------------------
constexpr uint32_t kModeOffset = 0x847C;   // pG+0x847C, one byte
constexpr int kModeAmateur = 1, kModeEasy = 3, kModeNormal = 5, kModePro = 6;
inline const char* ModeName(int mode) {
    switch (mode) {
    case kModeAmateur: return "amateur"; case kModeEasy: return "easy"; case kModeNormal: return "normal"; case kModePro: return "pro";
    default: return mode < 0 ? "unread" : "unknown";
    }
}
// The duel's difficulty: the mode only (the rank never). Unread (-1) or unknown: Normal.
inline sd::Difficulty DifficultyFromMode(int mode) {
    switch (mode) {
    case kModeAmateur: case kModeEasy: return sd::Difficulty::Easy;
    case kModePro: return sd::Difficulty::Pro;
    default: return sd::Difficulty::Normal;
    }
}

// ---- step 4: the admission --------------------------------------------------------------------------------------------
constexpr uint8_t kSalvadorId = 0x15;      // em10 Dr. Salvador (nightcheck ENCOUNTERS['salvador'] ids='15'); Bella 0x16
// [Combat] SwordLockPad (lab DLL only): missing = 0 (closed); "1" opens the duel on a pad; anything else counts a problem
// and stays closed.
struct Config {bool pad = false;};
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[16]{};
    get(L"SwordLockPad", v, 16);
    if (!v[0] || (v[0] == L'0' && !v[1])) c.pad = false;
    else if (v[0] == L'1' && !v[1]) c.pad = true;
    else {c.pad = false; ++problems;}
    return c;
}
enum class Why : uint8_t {Ok, Owner, Device};
inline const char* WhyName(Why w) {return w == Why::Owner ? "owner" : w == Why::Device ? "device" : "ok";}
struct Device {bool read = false; int32_t device = -1, selector = -1;};
// The device rule (both owners): the keyboard / mouse glyphs only, unless SwordLockPad=1; an unread word is a pad.
inline bool KeyboardDevice(const Device& d) {return d.read && icons::KeyboardGlyphs(d.device, d.selector);}
inline Why DeviceRule(const Config& c, const Device& d) {return c.pad || KeyboardDevice(d) ? Why::Ok : Why::Device;}
// A chainsaw block: Dr. Salvador only (owner read with id 0x15), then the device rule.
inline Why Chainsaw(const Config& c, bool owner_read, uint8_t owner_id, const Device& d) {
    if (!owner_read || owner_id != kSalvadorId) return Why::Owner;
    return DeviceRule(c, d);
}
}  // namespace link_runtime::sword_lock::gate

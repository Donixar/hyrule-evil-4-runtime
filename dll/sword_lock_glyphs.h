#pragma once
// Master Sword Blade Lock, milestone H1a: the glyph census of the mixed-button duel (docs/idea-ambiciosa-2026-11-03.md,
// "Mecanica del duelo: botones mezclados", hito H1a). Pure: no hooks, no process access. The lab census that confirms
// what is still open runs in link_runtime.cpp ([Combat] SwordLockGlyphCensus=1, never published); its bot case is
// scripts/live/cases_swordlock.py swordlock_glyphs. Tests: tests/native/test_sword_lock_glyphs.cpp
// (scripts/test_sword_lock_glyphs.bat; with the mapped image every pin below is checked against it).
//
// Pinned bio4.exe 1.1.0 (SHA 19aed4af...), mapped image bb59e550...; every address is an RVA (base 0x400000). Offline
// facts, each with its bytes in kCensusPins / kBitPins:
//  * Logical key bits: the key-name map is built at 0x294E10 (`mov [ebp-0xC], lo; mov [ebp-8], hi; call insert;
//    mov [eax], name`, ESI = 0 from 0x294E2C). KEY_RIGHT 4 (0x294E73), KEY_LEFT 8 (0x294E92), KEY_KAMAE 0x10, KEY_FIRE
//    0x80, KEY_ASHLEY 0x200, KEY_CK 0x400, KEY_KAMAE_KNIFE 0x800, KEY_Y 0x10000, KEY_X 0x20000, KEY_B 0x40000, KEY_A
//    0x80000, KEY_R 0x4000000, KEY_L 0x8000000; high dword KEY_KNIFE 1<<48, KEY_KNIFE2 1<<49, KEY_CRANK 1<<50 (kKeys).
//    Key_btn_on / Key_btn_trg are 64-bit at 0x862F08 / 0x862F10 (edge detector 0x56A4F0). BIO4/input.ini GAMEPLAY
//    KEYBOARD (vanilla and Donald's): D = KEY_RIGHT, A = KEY_LEFT, SPACE = KEY_KAMAE_KNIFE and KEY_B, E = KEY_A and
//    KEY_CK, MB_RIGHT = KEY_KAMAE, MB_LEFT = KEY_FIRE (and KEY_OK), LSHIFT = KEY_RUN, Z = KEY_ASHLEY, KEY_X / KEY_Y /
//    KEY_KNIFE / KEY_CRANK = 00000, KEY_R / KEY_L = MA_RIGHT / MA_LEFT (mouse axes). Live already: D = bit 4
//    (back_slice.h, 19-oct log side=-1).
//  * Device selector [0xE224B0] (never written by an absolute store in .text: set through a pointer, a confirmar en el
//    censo). Value 1 = the pad: cActionButton::set (0x18AA7D) and ActionButton::move (0x1A2FF6) take the pad column, the
//    cdecl wrapper 0x18AD40 passes its 7th argument (the pad btn) instead of the 6th (the keyboard btn), and the aim code
//    at 0x197CA8 reads Key_btn_on & KEY_KAMAE and the byte 0x863011 (by its shape the mouse path) only when it is not 1.
//    Which values the keyboard and the mouse give it is not known offline (the census logs it: selector=).
//  * Remap in set() (0x18AA7D..0x18AAAF): selector 1: btn 3 (L_R) -> 0x12 and 0xD (B_RAPID) -> 2 (A_RAPID, the Modder's
//    collapse); otherwise 0x11 -> 0xB (GACHA) and 0x12 -> 4 (A_B). Native calls of the wrapper show the pairs, e.g. the
//    grab SHAKE OFF 0x280D9E (kind 0x2B, kb GACHA 0xB, pad 0x11) and the dodges (kb 3, pad 0x12 or 4).
//  * Icon: move() picks the IdSys id from 0x7FBFF0[btn*2 + (selector != 1)] (kGlyphTable, 0x14 btns x 2 columns).
//    btn 7 (Y) and 8 (Z) share A_NORMAL's ids 00/20: no glyph of their own.
//  * checkButton 0x18A300 (jump table 0x18A7A4, btn-1): btn 1/2/0xE test KEY_A, 4 KEY_A+KEY_B, 5 KEY_B, 6 KEY_ASHLEY (not
//    KEY_X: the ficha's "X de mando" glyph is Ashley's button, Z on keyboard, RB on the pad), 7 KEY_Y, 9 KEY_L, 0xA KEY_R,
//    0x11/0x12 KEY_KNIFE, 0x13 KEY_CRANK; 3 tests bits 0x10/0x80 of the words 0x86301C/0x863020; 8, 0xB, 0xC, 0xD, 0xF,
//    0x10 test nothing (their QTE code reads the stick itself). btn 2, 3, 4, 9, 0xA and 0xD also set the per-frame QTE
//    byte 0x863A58 (cleared at 0x23E636 before the arbiter; PadRead and the glyph renderer read it). The KEY_A tests
//    of btn 1/2/0xE hold for a work whose flags carry 2, 0x10 or 0x40 (the census offers flags 2); with none of them
//    (flags 0: the Mortal Draw prompt and most native offers) checkButton calls 0x4085D5(0x11) instead (0x18A393), a
//    bit of the player's +0x501C state, not Key_btn. CheckBits(btn, flags) says so.
//  * The arbiter 0x18AB40 clears the cockpit (btn 0x8510B5 = 0, kind 0x8510B7 = 0x51); disp writes the drawn work's
//    btn/kind there (0x18A25D..0x18A26C) and move() reads that cockpit (ECX = 0x8510B5). checkPLStatus 0x18A930 skips
//    actCheck with flags & 2 (ENFORCE_EXEC); func NULL never runs (0x18ABF3).
//  * Live evidence so far (H0 recording, 3-nov, keyboard): btn 1 draws the E key ("E KICK"; also "[E] MORTAL DRAW",
//    12-oct); GACHA (SHAKE OFF, kb 0xB) draws a mouse icon on a pink burst, not A/D. Everything else is "a confirmar en
//    el censo".
#include "native_pins.h"
#include "game_clock.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace link_runtime::sword_lock::glyphs {
namespace rva {
constexpr uint32_t kSet = 0x18AA10, kSetWrapper = 0x18AD40, kArbiter = 0x18AB40, kDisp = 0x18A100, kCheckButton = 0x18A300;
constexpr uint32_t kCheckPlStatus = 0x18A930, kMove = 0x1A2FD0, kGlyphTable = 0x7FBFF0, kSelector = 0xE224B0;
constexpr uint32_t kRendererDevice = 0x806F44;   // a second device word (written 0..3 at 0x5644B3..0x5645FA), log only
constexpr uint32_t kQteFlag = 0x863A58, kCockpit = 0x8510B5, kCockpitKind = 0x8510B7;
constexpr uint32_t kManager = 0x8502C0, kManagerActive = 0x42, kKeyOn = 0x862F08, kKeyTrg = 0x862F10;
}  // namespace rva

// ---------------------------------------------------------------- logical keys (Key_btn_on / Key_btn_trg bits)
namespace key {
constexpr uint64_t kForward = 0x1, kBack = 0x2, kRight = 0x4, kLeft = 0x8, kKamae = 0x10, kRelockon = 0x20, kRun = 0x40;
constexpr uint64_t kFire = 0x80, kTurn180 = 0x100, kAshley = 0x200, kCheck = 0x400, kKamaeKnife = 0x800, kSt = 0x1000;
constexpr uint64_t kOption = 0x2000, kCamUp = 0x4000, kCamDown = 0x8000, kY = 0x10000, kX = 0x20000, kB = 0x40000;
constexpr uint64_t kA = 0x80000, kSscrn = 0x100000, kMap = 0x200000, kLb = 0x400000, kRb = 0x800000, kU = 0x1000000;
constexpr uint64_t kD = 0x2000000, kR = 0x4000000, kL = 0x8000000, kZ = 0x10000000, kEvCancel = 0x20000000;
constexpr uint64_t kCancel = 0x40000000, kOk = 0x80000000ULL;
constexpr uint64_t kKnife = 1ULL << 48, kKnife2 = 1ULL << 49, kCrank = 1ULL << 50;
// Mouse movement and wheel (KEYBOARD: KEY_C_U/C_D = MA_WU/MA_WD, KEY_U/D/R/L = MA_*, KEY_C_R/C_L = MA_RIGHT/LEFT):
// the census does not log a line for a change of these alone.
constexpr uint64_t kMouseAxes = kCamUp | kCamDown | kU | kD | kR | kL | (0x30ULL << 32);
}  // namespace key
struct KeyName { const char* name; uint64_t bit; uint32_t ctor; };   // ctor: RVA of its first `mov [ebp-0xC]` of the entry
inline constexpr KeyName kKeys[] = {
    {"KEY_FORWARD", 0x1, 0x294E35}, {"KEY_BACK", 0x2, 0x294E54}, {"KEY_RIGHT", 0x4, 0x294E73}, {"KEY_LEFT", 0x8, 0x294E92},
    {"KEY_KAMAE", 0x10, 0x294EB1}, {"KEY_RELOCKON", 0x20, 0x294ED0}, {"KEY_RUN", 0x40, 0x294EEF}, {"KEY_FIRE", 0x80, 0x294F0E},
    {"KEY_180TURN", 0x100, 0x294F2D}, {"KEY_ASHLEY", 0x200, 0x294F4C}, {"KEY_CK", 0x400, 0x294F61},
    {"KEY_KAMAE_KNIFE", 0x800, 0x294F8A}, {"KEY_ST", 0x1000, 0x294FA9}, {"KEY_OPTION", 0x2000, 0x294FC8},
    {"KEY_C_U", 0x4000, 0x294FE7}, {"KEY_C_D", 0x8000, 0x295006}, {"KEY_Y", 0x10000, 0x295025}, {"KEY_X", 0x20000, 0x295044},
    {"KEY_B", 0x40000, 0x295063}, {"KEY_A", 0x80000, 0x295082}, {"KEY_SSCRN", 0x100000, 0x2950A1},
    {"KEY_MAP", 0x200000, 0x2950C0}, {"KEY_LB", 0x400000, 0x2950DF}, {"KEY_RB", 0x800000, 0x2950FE},
    {"KEY_U", 0x1000000, 0x29511D}, {"KEY_D", 0x2000000, 0x29513C}, {"KEY_R", 0x4000000, 0x29515B}, {"KEY_L", 0x8000000, 0x29517A},
    {"KEY_Z", 0x10000000, 0x295199}, {"KEY_EV_CANCEL", 0x20000000, 0x2951B8}, {"KEY_CANCEL", 0x40000000, 0x2951D7},
    {"KEY_OK", 0x80000000ULL, 0x2951F6}, {"KEY_LL", 1ULL << 32, 0x295215}, {"KEY_LR", 1ULL << 33, 0x29522D},
    {"KEY_LD", 1ULL << 34, 0x295253}, {"KEY_LU", 1ULL << 35, 0x295272}, {"KEY_C_R", 1ULL << 36, 0x295291},
    {"KEY_C_L", 1ULL << 37, 0x2952B0}, {"KEY_PICK_UP", 1ULL << 38, 0x2952CF}, {"KEY_MAP_MENU", 1ULL << 39, 0x2952EE},
    {"KEY_ACT_A", 1ULL << 40, 0x29530D}, {"KEY_QUICK", 1ULL << 41, 0x29532C}, {"KEY_ACT_B", 1ULL << 42, 0x29534B},
    {"KEY_MAP_ZOOM", 1ULL << 43, 0x29536A}, {"KEY_PLUS", 1ULL << 44, 0x295389}, {"KEY_MINUS", 1ULL << 45, 0x29539E},
    {"KEY_1", 1ULL << 46, 0x2953C7}, {"KEY_2", 1ULL << 47, 0x2953E6}, {"KEY_KNIFE", 1ULL << 48, 0x295405},
    {"KEY_KNIFE2", 1ULL << 49, 0x295424}, {"KEY_CRANK", 1ULL << 50, 0x295443}};
inline bool SameName(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = char(x - 32);
        if (y >= 'a' && y <= 'z') y = char(y - 32);
        if (x != y) return false;
    }
    return !*a && !*b;
}
inline uint64_t KeyBit(const char* name) {
    if (!name) return 0;
    for (const auto& k : kKeys) if (SameName(k.name, name)) return k.bit;
    return 0;
}
inline const char* KeyNameOf(uint64_t bit) {
    for (const auto& k : kKeys) if (k.bit == bit) return k.name;
    return nullptr;
}

// ---------------------------------------------------------------- cActionButton btn (DISP_FLAG), remap and glyph
namespace btn {
constexpr uint8_t kOff = 0, kANormal = 1, kARapid = 2, kLR = 3, kAB = 4, kB = 5, kX = 6, kY = 7, kZ = 8, kL = 9, kR = 0xA;
constexpr uint8_t kGacha = 0xB, kRotate = 0xC, kBRapid = 0xD, kAAccent = 0xE, kStickA = 0xF, kStickUp = 0x10;
constexpr uint8_t kPad11 = 0x11, kPad12 = 0x12, kPad13 = 0x13;   // UHD: pad-side btns of the wrapper (no GC name)
constexpr uint8_t kFirst = 1, kLast = 0x12, kCount = kLast - kFirst + 1, kTable = 0x14;
}  // namespace btn
inline const char* BtnName(uint8_t b) {
    static const char* const n[] = {"OFF", "A_NORMAL", "A_RAPID", "L_R", "A_B", "B", "X", "Y", "Z", "L", "R", "GACHA", "ROTATE",
                                    "B_RAPID", "A_ACCENT", "STICK_A", "STICK_UP", "PAD_11", "PAD_12", "PAD_13"};
    return b < btn::kTable ? n[b] : "?";
}
// 0x7FBFF0, two bytes per btn: [0] with the selector at 1 (pad), [1] otherwise. Pinned in kCensusPins.
inline constexpr uint8_t kGlyphTable[btn::kTable][2] = {
    {0x00, 0x20}, {0x00, 0x20}, {0x17, 0x21}, {0x19, 0x03}, {0x04, 0x24}, {0x07, 0x07}, {0x09, 0x11}, {0x00, 0x20},
    {0x00, 0x20}, {0x0A, 0x0A}, {0x0B, 0x0B}, {0x06, 0x06}, {0x05, 0x05}, {0x17, 0x0C}, {0x08, 0x22}, {0x0D, 0x25},
    {0x0E, 0x0E}, {0x17, 0x21}, {0x19, 0x03}, {0x18, 0x21}};
inline bool PadColumn(int32_t selector) { return selector == 1; }
// cActionButton::set's remap of the work's btn (0x18AA7D..0x18AAAF), with the selector at the time of the call.
inline uint8_t Remap(uint8_t b, int32_t selector) {
    if (PadColumn(selector)) return b == btn::kLR ? btn::kPad12 : b == btn::kBRapid ? btn::kARapid : b;
    return b == btn::kPad11 ? btn::kGacha : b == btn::kPad12 ? btn::kAB : b;
}
// ActionButton::move's icon id for a remapped btn (0x1A2FF6..0x1A3018). 0xFF: outside the table.
inline uint8_t Glyph(uint8_t remapped, int32_t selector) {
    return remapped < btn::kTable ? kGlyphTable[remapped][PadColumn(selector) ? 0 : 1] : 0xFF;
}
// The wrapper 0x18AD40: the btn a native call shows on this device.
inline uint8_t WrapperBtn(uint8_t keyboard_btn, uint8_t pad_btn, int32_t selector) { return PadColumn(selector) ? pad_btn : keyboard_btn; }
// The logical Key_btn bits checkButton tests for a btn (0 = none: the QTE reads its input itself; btn 3 reads other words),
// for a work with the census' flags (2, ENFORCE_EXEC). For other flags use CheckBits(btn, flags).
inline uint64_t CheckBits(uint8_t b) {
    switch (b) {
    case btn::kANormal: case btn::kARapid: case btn::kAAccent: return key::kA;
    case btn::kAB: return key::kA | key::kB;
    case btn::kB: return key::kB;
    case btn::kX: return key::kAshley;
    case btn::kY: return key::kY;
    case btn::kL: return key::kL;
    case btn::kR: return key::kR;
    case btn::kPad11: case btn::kPad12: return key::kKnife;
    case btn::kPad13: return key::kCrank;
    default: return 0;
    }
}
// The same for a work's flags: btn 1/2/0xE test KEY_A only with flags & (2 | 0x10 | 0x40) (0x18A35D, 0x18A498); with
// none of them they read 0x4085D5(0x11) (0x18A393), not Key_btn: 0 here.
constexpr uint32_t kKeyBtnFlags = 0x2 | 0x10 | 0x40;
inline uint64_t CheckBits(uint8_t b, uint32_t flags) {
    if ((b == btn::kANormal || b == btn::kARapid || b == btn::kAAccent) && !(flags & kKeyBtnFlags)) return 0;
    return CheckBits(b);
}
// checkButton also raises the per-frame QTE byte 0x863A58 for these.
inline bool SetsQte(uint8_t b) {
    return b == btn::kARapid || b == btn::kLR || b == btn::kAB || b == btn::kL || b == btn::kR || b == btn::kBRapid;
}

// ---------------------------------------------------------------- the duel's controls
enum class Device : uint8_t { Keyboard, Pad };
enum class Use : uint8_t { Mash, Break };
enum class Gesture : uint8_t { Tap, Hold, Mash, Alternate, Sway, DirTap };
// Live: confirmed in game. Census: offered by the lab census, pending its capture. Mismatch: the native glyph shows
// another physical input. NoGlyph: no native btn draws this input. Excluded: unbound on this device or a design rule.
enum class Status : uint8_t { Live, Census, Mismatch, NoGlyph, Excluded };
inline const char* StatusName(Status s) {
    static const char* const n[] = {"live", "census", "mismatch", "no_glyph", "excluded"};
    return n[unsigned(s)];
}
struct Side { uint8_t btn; Status status; const char* note; };
enum class Control : uint8_t { KnifeMash, ActionMash, Sway, Alternate, Twist, QuickCut, HoldAction, SideCut, Guard, PadX, Pommel, kCount };
struct ControlInfo {
    Control id; const char* key; const char* label; Use use; Gesture gesture;
    uint64_t bits;     // the logical input the duel reads
    uint64_t shared;   // also counts where it is the same physical key as `bits` (the knife's KEY_B on SPACE)
    uint64_t second;   // second input of the gesture (Alternado: Accion; Corte lateral: the knife tap)
    uint16_t hold_ms;
    Side keyboard, pad;
};
inline constexpr ControlInfo kControls[] = {
    {Control::KnifeMash, "CuchilloRapido", "Cuchillo rapido", Use::Mash, Gesture::Mash, key::kKamaeKnife, key::kB, 0, 0,
     {btn::kBRapid, Status::Census, "B_RAPID 0xD, glifo 0x0C"},
     {btn::kB, Status::Mismatch, "set() colapsa 0xD en A_RAPID con selector 1; DISP_B prueba KEY_B (0x18A677), que en Tipo II es X "
                                 "(correr), no LB: ningun btn prueba KEY_KAMAE_KNIFE"}},
    {Control::ActionMash, "AccionRapida", "Accion rapida", Use::Mash, Gesture::Mash, key::kA, 0, 0, 0,
     {btn::kARapid, Status::Census, "A_RAPID, glifo 0x21"}, {btn::kARapid, Status::Census, "A_RAPID, glifo 0x17"}},
    {Control::Sway, "Vaiven", "Vaiven izquierda-derecha", Use::Mash, Gesture::Sway, key::kRight | key::kLeft, 0, 0, 0,
     {btn::kGacha, Status::Mismatch, "GACHA en teclado dibuja el mouse (SHAKE OFF de H0, 0x280D9E kb=0xB): KEY_L/KEY_R = MA_LEFT/MA_RIGHT, no A/D"},
     {btn::kGacha, Status::Census, "GACHA 06; el nativo usa 0x11 en mando"}},
    {Control::Alternate, "Alternado", "Alternado cuchillo/Accion", Use::Mash, Gesture::Alternate, key::kKamaeKnife, key::kB, key::kA, 0,
     {btn::kOff, Status::Census, "un btn por flanco: el del cuchillo y A_NORMAL"},
     {btn::kOff, Status::Mismatch, "el flanco del cuchillo no tiene glifo: DISP_B prueba KEY_B (X en Tipo II), 0x11/0x12 prueban KEY_KNIFE"}},
    {Control::Twist, "Accion", "Giro de hoja", Use::Break, Gesture::Tap, key::kA, 0, 0, 0,
     {btn::kANormal, Status::Live, "E KICK (H0 3-nov) y [E] MORTAL DRAW (12-oct)"}, {btn::kANormal, Status::Census, "A_NORMAL 00"}},
    {Control::QuickCut, "Cuchillo", "Corte rapido", Use::Break, Gesture::Tap, key::kB, key::kKamaeKnife, 0, 0,
     {btn::kB, Status::Census, "DISP_B 07; checkButton prueba KEY_B (0x18A677)"},
     {btn::kB, Status::Census, "DISP_B 07; KEY_B = X (correr) en Tipo II, el cuchillo es LB"}},
    {Control::HoldAction, "Aguante", "Aguante", Use::Break, Gesture::Hold, key::kA, 0, 0, 500,
     {btn::kAAccent, Status::Census, "A_ACCENT, glifo 0x22"}, {btn::kAAccent, Status::Census, "A_ACCENT, glifo 0x08"}},
    {Control::SideCut, "Lateral", "Corte lateral", Use::Break, Gesture::DirTap, key::kRight | key::kLeft, 0, key::kKamaeKnife, 0,
     {btn::kOff, Status::NoGlyph, "ningun btn nativo junta direccion y cuchillo"}, {btn::kOff, Status::NoGlyph, "idem"}},
    {Control::Guard, "Guardia", "Guardia", Use::Break, Gesture::Hold, key::kKamae, 0, 0, 400,
     {btn::kOff, Status::NoGlyph, "ningun btn prueba KEY_KAMAE solo (3 = esquive); el censo mira Z/L/R"}, {btn::kOff, Status::NoGlyph, "idem"}},
    {Control::PadX, "X", "X de mando", Use::Break, Gesture::Tap, key::kAshley, 0, 0, 0,
     {btn::kX, Status::Excluded, "DISP_X prueba KEY_ASHLEY (0x18A6A3), no KEY_X: en teclado seria Z; la ficha la deja solo en mando, "
                                 "requiere decision (opt_in)"},
     {btn::kX, Status::Census, "DISP_X 09 = KEY_ASHLEY (RB en Tipo II)"}},
    {Control::Pommel, "Pomo", "Pomo", Use::Break, Gesture::Tap, key::kFire, 0, 0, 0,
     {btn::kOff, Status::NoGlyph, "solo btn 1 en r20d prueba KEY_FIRE"}, {btn::kOff, Status::NoGlyph, "idem"}}};
static_assert(sizeof(kControls) / sizeof(kControls[0]) == size_t(Control::kCount), "one row per control");
inline const ControlInfo& Info(Control c) { return kControls[unsigned(c)]; }
inline const Side& SideOf(const ControlInfo& c, Device d) { return d == Device::Pad ? c.pad : c.keyboard; }

// ---------------------------------------------------------------- input.ini bindings, physical keys, the gate
// Profiles of the GAMEPLAY block: KEYBOARD, the three XInput profiles (Type I/II/III), the plain GAME_CONTROLLER and the
// GAME_CONTROLLER block headed by a device GUID (C216046D... by default).
enum class Profile : uint8_t { Keyboard, XInputA, XInputB, XInputC, DInput, DInputGuid, kCount };
inline const char* ProfileName(Profile p) {
    static const char* const n[] = {"keyboard", "xinput_a", "xinput_b", "xinput_c", "dinput", "dinput_guid"};
    return n[unsigned(p)];
}
struct Bind { uint64_t bit; std::string token; };
struct ProfileBinds { bool present = false; std::vector<Bind> binds; };
struct InputBinds { ProfileBinds profile[size_t(Profile::kCount)]; bool gameplay = false; };
inline constexpr const char* kDefaultGuid = "C216046D-0000-0000-0000-504944564944";
namespace detail {
inline std::string Trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    return a == std::string::npos ? std::string{} : s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}
inline std::string Upper(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 32);
    return s;
}
}  // namespace detail
// Parses the GAMEPLAY block of an input.ini text (CRLF or LF). Unknown names and 00000 are skipped; a name bound twice
// keeps both tokens (KEY_RUN on X and L3 in Type II with pad-type2-01).
inline InputBinds ParseInput(const std::string& text, const char* guid = kDefaultGuid) {
    InputBinds out;
    bool gameplay = false, after_controller = false;
    int current = -1;
    const std::string want = detail::Upper(std::string("###") + (guid ? guid : ""));
    size_t at = 0;
    while (at < text.size()) {
        const size_t end = text.find('\n', at);
        std::string line = detail::Trim(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? text.size() : end + 1;
        if (line.empty()) continue;
        if (line.rfind("###", 0) == 0) {
            const std::string tag = detail::Upper(line);
            if (tag == "###GAMEPLAY") { gameplay = true; current = -1; after_controller = false; out.gameplay = true; continue; }
            if (tag == "###MENUS") { gameplay = false; current = -1; after_controller = false; continue; }
            if (!gameplay) continue;
            if (after_controller) {   // the line right after ###GAME_CONTROLLER may name a device
                after_controller = false;
                const bool named = tag.size() > 3 && tag.find('-') != std::string::npos;
                if (named) { current = tag == want ? int(Profile::DInputGuid) : -2; if (current >= 0) out.profile[current].present = true; continue; }
            }
            if (tag == "###KEYBOARD") current = int(Profile::Keyboard);
            else if (tag == "###XINPUT_CONTROLLER_A") current = int(Profile::XInputA);
            else if (tag == "###XINPUT_CONTROLLER_B") current = int(Profile::XInputB);
            else if (tag == "###XINPUT_CONTROLLER_C") current = int(Profile::XInputC);
            else if (tag == "###GAME_CONTROLLER") { current = int(Profile::DInput); after_controller = true; }
            else current = -2;
            if (current >= 0) out.profile[current].present = true;
            continue;
        }
        after_controller = false;
        if (current < 0 || line[0] == ';' || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = detail::Trim(line.substr(0, eq));
        std::string token = detail::Trim(line.substr(eq + 1));
        const auto comment = token.find_first_of(";#");
        if (comment != std::string::npos) token = detail::Trim(token.substr(0, comment));
        const uint64_t bit = KeyBit(name.c_str());
        if (!bit || token.empty() || token == "00000") continue;
        out.profile[current].binds.push_back({bit, detail::Upper(token)});
    }
    return out;
}
// Physical tokens bound to any logical bit of `bits`, sorted, without repeats.
inline std::vector<std::string> Tokens(const ProfileBinds& p, uint64_t bits) {
    std::vector<std::string> out;
    for (const auto& b : p.binds) {
        if (!(b.bit & bits)) continue;
        bool seen = false;
        for (const auto& t : out) if (t == b.token) { seen = true; break; }
        if (!seen) out.push_back(b.token);
    }
    for (size_t i = 1; i < out.size(); ++i)
        for (size_t j = i; j > 0 && out[j] < out[j - 1]; --j) std::swap(out[j], out[j - 1]);
    return out;
}
inline bool Overlap(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    for (const auto& x : a) for (const auto& y : b) if (x == y) return true;
    return false;
}
// The logical bits a control reads on this profile: `bits`, plus `shared` only where it is the same physical key (SPACE
// carries KEY_KAMAE_KNIFE and KEY_B on the keyboard; on the pad they are LB and X), plus `second`.
inline uint64_t ReadBits(const ControlInfo& c, const ProfileBinds& p) {
    uint64_t out = c.bits | c.second;
    if (c.shared && Overlap(Tokens(p, c.bits), Tokens(p, c.shared))) out |= c.shared;
    return out;
}
inline std::vector<std::string> PhysicalKeys(const ControlInfo& c, const ProfileBinds& p) { return Tokens(p, ReadBits(c, p)); }
// Bound on this profile: its main input (and its second one) reach a physical key.
inline bool Bound(const ControlInfo& c, const ProfileBinds& p) {
    return !Tokens(p, c.bits).empty() && (!c.second || !Tokens(p, c.second).empty());
}
// Two controls share a physical key: never neighbours, the same key in the signature, never a fail when pressed.
inline bool SharePhysical(Control a, Control b, const ProfileBinds& p) {
    return Overlap(PhysicalKeys(Info(a), p), PhysicalKeys(Info(b), p));
}
// The glyph shows what the duel reads: when the side's btn has a Key_btn test (census flags), every physical key of that
// test is one the control reads on this profile (else the player presses the drawn button and never counts). A btn
// without a test (B_RAPID, GACHA) cannot be checked here: its census crop decides.
inline bool GlyphAgrees(const ControlInfo& c, Device d, const ProfileBinds& p) {
    const Side& s = SideOf(c, d);
    const uint64_t test = s.btn ? CheckBits(s.btn) : 0;
    if (!test) return true;
    const auto shown = Tokens(p, test), read = PhysicalKeys(c, p);
    if (shown.empty()) return false;
    for (const auto& t : shown) { bool hit = false; for (const auto& r : read) hit |= r == t; if (!hit) return false; }
    return true;
}
// A side the design keeps out (Excluded) that names a btn counts as Census only when its control is in `opt_in` (bit per
// Control): an explicit design decision, never the default (the keyboard X de mando = Z, Ashley's key).
inline Status EffectiveStatus(const ControlInfo& c, Device d, uint32_t opt_in = 0) {
    const Side& s = SideOf(c, d);
    return s.status == Status::Excluded && s.btn && ((opt_in >> unsigned(c.id)) & 1u) ? Status::Census : s.status;
}
// Eligible for the bag on a device: bound, its glyph at least `floor` (Live only, or Live + Census), and that glyph tests
// the input the control reads.
inline bool Eligible(const ControlInfo& c, Device d, const ProfileBinds& p, bool census_counts, uint32_t opt_in = 0) {
    const Status s = EffectiveStatus(c, d, opt_in);
    return Bound(c, p) && (s == Status::Live || (census_counts && s == Status::Census)) && GlyphAgrees(c, d, p);
}
// The delivery gate (design: at least 3 distinct physical keys in the keyboard bag). `exclude` drops controls (bit per
// Control) a design decision keeps out; `opt_in` lets in an Excluded side a design decision accepts.
struct Gate { unsigned distinct = 0; std::vector<std::string> keys; bool pass = false; };
inline Gate DeviceGate(Device d, const ProfileBinds& p, bool census_counts, uint32_t exclude = 0, uint32_t opt_in = 0) {
    Gate g;
    for (const auto& c : kControls) {
        if ((exclude >> unsigned(c.id)) & 1u) continue;
        if (!Eligible(c, d, p, census_counts, opt_in)) continue;
        for (const auto& t : PhysicalKeys(c, p)) {
            bool seen = false;
            for (const auto& k : g.keys) if (k == t) { seen = true; break; }
            if (!seen) g.keys.push_back(t);
        }
    }
    g.distinct = unsigned(g.keys.size());
    g.pass = g.distinct >= 3;
    return g;
}

// ---------------------------------------------------------------- the lab census ([Combat] SwordLockGlyphCensus=1)
// Offer of our own through cActionButton::set at the arbiter entry (mortal_draw_prompt.h's path): kind 0x44 (the mod's
// message 0x5A, "MORTAL DRAW "), slot 0, func NULL (drawn, never run), flags ENFORCE_EXEC (no actCheck: drawn whatever
// Link's routine), only with the native queue empty and no Mortal Draw prompt offered that frame (it keeps priority:
// OfferStats::md counts those frames); the runtime subtracts our offer from the post-arbiter sample (ClaimOwnOffer), so
// the Mortal Draw and sword arbiters never take it for a native prompt. Every btn 1..0x12, one at a time, `offer_ms` each with `gap_ms`
// without an offer between them (the cockpit sees btn 0, so the next offer always rebuilds its icon). Pass 1 uses the
// game's own selector; pass 2 (pad) writes the selector to 1 only inside our set() call and inside move() while the
// cockpit holds our kind, so no other reader ever sees it: "glifo de mando (selector forzado, sin mando real)".
// A cycle starts after `settle_ms` of continuous gameplay following a load (player, global, room or a frame gap) and
// runs once; the next load starts another.
constexpr uint8_t kCensusKind = 0x44;
constexpr uint32_t kCensusFlags = 2;   // ACTCTR_ENFORCE_EXEC
struct CensusConfig {
    bool enabled = false;     // SwordLockGlyphCensus: exact 1; missing or 0 = off (nothing installed or run)
    bool pad = true;          // SwordLockGlyphCensusPad: missing or 1 = the forced pad pass, 0 = keyboard pass only
    uint32_t offer_ms = 1500; // SwordLockGlyphCensusMs: 500..5000
    uint32_t gap_ms = 400, settle_ms = 3000;
};
namespace detail {
inline bool WideEq(const wchar_t* a, const wchar_t* b) {
    for (; *a && *b; ++a, ++b) if (std::towlower(*a) != std::towlower(*b)) return false;
    return !*a && !*b;
}
inline bool WideNumber(const wchar_t* v, uint32_t& out) {
    if (!v || !*v) return false;
    uint64_t n = 0;
    for (const wchar_t* p = v; *p; ++p) {
        if (*p < L'0' || *p > L'9') return false;
        n = n * 10 + unsigned(*p - L'0');
        if (n > 1000000) return false;
    }
    out = uint32_t(n);
    return true;
}
}  // namespace detail
// get(key, out, count) fills `out` with the raw value ("" when missing). A lab key: a typo never runs a half-configured
// census (SwordLockGlyphCensus other than 0/1 counts a problem and stays off).
template <class Get> CensusConfig ReadCensusConfig(Get&& get, unsigned& problems) {
    CensusConfig c; wchar_t v[16]{};
    get(L"SwordLockGlyphCensus", v, 16);
    if (v[0] == L'1' && !v[1]) c.enabled = true;
    else if (v[0] && !(v[0] == L'0' && !v[1])) { ++problems; return CensusConfig{}; }
    v[0] = 0; get(L"SwordLockGlyphCensusPad", v, 16);
    if (!v[0] || detail::WideEq(v, L"1")) c.pad = true;
    else if (detail::WideEq(v, L"0")) c.pad = false;
    else { ++problems; c.pad = false; }
    v[0] = 0; get(L"SwordLockGlyphCensusMs", v, 16);
    uint32_t ms = 0;
    if (v[0]) {
        if (detail::WideNumber(v, ms) && ms >= 500 && ms <= 5000) c.offer_ms = ms;
        else ++problems;
    }
    return c;
}

enum class Pass : uint8_t { Native, PadForced };
inline const char* PassName(Pass p) { return p == Pass::PadForced ? "pad_forced" : "native"; }
// One census tick: the arbiter entry. gameplay: Link in play (ReadHealth eligible).
struct CensusWorld {
    bool readable = false, gameplay = false;
    uint32_t player = 0, global = 0, room = 0, frame = 0;
    double now = 0;   // game time, 60 Hz frames
};
struct CensusPlan {
    bool inject = false;   // offer `btn` this frame (pass `pass`)
    uint8_t btn = 0; Pass pass = Pass::Native;
    bool restart = false, cycle_begin = false, offer_begin = false, offer_end = false, cycle_end = false;
    const char* restart_reason = "";
};
// What one offer saw, logged when it closes (`sword_lock glyph census btn=...`).
struct OfferStats {
    uint8_t btn = 0; Pass pass = Pass::Native; unsigned slot = 0;
    uint32_t frames = 0, paused = 0, injected = 0, own = 0, displayed = 0, native = 0, unreadable = 0, set_failed = 0;
    uint32_t remap_bad = 0, qte = 0, forced_moves = 0, selector_changes = 0;
    uint32_t md = 0;   // frames the Mortal Draw prompt offered first (it keeps priority: the census does not inject then)
    int32_t selector = -1, renderer = -1;   // at the first injection (the game's own selector, before any forcing)
    uint8_t remapped = 0, glyph = 0xFF, work_count = 0, work_kind = 0, work_btn = 0, cockpit_btn = 0, cockpit_kind = 0;
    uint32_t work_func = 0xFFFFFFFF, work_flags = 0;
    bool aborted = false;
    double t_ms = 0;
    bool Shown() const { return displayed > 0; }
    int32_t Used() const { return pass == Pass::PadForced ? 1 : selector; }   // the selector set() and move() saw
};
// The work set() filled (manager +0x44, mortal_draw_prompt.h layout), read right after our call.
struct WorkFacts { bool readable = false; uint8_t count = 0, kind = 0, btn = 0; uint32_t func = 0, flags = 0; };
template <class Reader> bool ReadWork(Reader& r, uint32_t image_base, WorkFacts& w) {
    w = WorkFacts{};
    const uint32_t m = image_base + rva::kManager;
    if (!r.Read(m + 0x40, &w.count, 1) || !r.Read(m + 0x44 + 0x0C, &w.kind, 1) || !r.Read(m + 0x44 + 0x0F, &w.btn, 1) ||
        !r.Read(m + 0x44 + 0x04, &w.func, 4) || !r.Read(m + 0x44 + 0x14, &w.flags, 4)) return false;
    w.readable = true;
    return true;
}
// Our offer was the one drawn: the arbiter fired a work (+0x42) and disp wrote our kind and remapped btn to the cockpit.
inline bool Displayed(uint8_t active, uint8_t cockpit_btn, uint8_t cockpit_kind, uint8_t remapped) {
    return active == 1 && cockpit_kind == kCensusKind && cockpit_btn == remapped;
}

class Census {
public:
    enum class State : uint8_t { Off, Settling, Running, Done };
    static constexpr int32_t kMaxGap = 30;      // native ticks between two arbiter calls: more = a load, a menu, a scene
    static constexpr double kMaxStep = 10.0;    // 60 Hz frames one tick may add
    explicit Census(CensusConfig c = {}) { Configure(c); }
    void Configure(const CensusConfig& c) { cfg_ = c; state_ = c.enabled ? State::Settling : State::Off; known_ = false; }
    const CensusConfig& config() const { return cfg_; }
    State state() const { return state_; }
    unsigned Passes() const { return cfg_.pad ? 2u : 1u; }
    unsigned Slots() const { return Passes() * btn::kCount; }
    unsigned cycles() const { return cycles_; }
    bool Offering() const { return open_; }
    OfferStats& current() { return cur_; }
    const OfferStats& last() const { return last_; }
    unsigned cycle_offers() const { return cycle_offers_; }
    unsigned cycle_shown() const { return cycle_shown_; }
    // The pass and btn of a slot of the cycle.
    static uint8_t SlotBtn(unsigned slot) { return uint8_t(btn::kFirst + slot % btn::kCount); }
    static Pass SlotPass(unsigned slot) { return slot >= btn::kCount ? Pass::PadForced : Pass::Native; }

    CensusPlan Step(const CensusWorld& w) {
        CensusPlan p;
        if (state_ == State::Off || !w.readable) { if (open_) ++cur_.unreadable; return p; }
        const int32_t gap = int32_t(w.frame - frame_);
        const bool moved = known_ && (w.player != player_ || w.global != global_ || (w.room & 0xFFFF) != room_);
        const bool jump = known_ && (gap < 0 || gap > kMaxGap);
        if (!known_ || moved || jump) {
            if (open_) { cur_.aborted = true; Close(); p.offer_end = true; }
            if (known_) { p.restart = true; p.restart_reason = moved ? "load" : "gap"; }
            state_ = State::Settling; settle_ = 0; known_ = true; last_now_ = w.now;
            player_ = w.player; global_ = w.global; room_ = w.room & 0xFFFF;
        }
        frame_ = w.frame;
        double dt = w.now - last_now_;
        if (!(dt > 0)) dt = 0;
        if (dt > kMaxStep) dt = kMaxStep;
        last_now_ = w.now;
        if (state_ == State::Settling) {
            settle_ = w.gameplay ? settle_ + dt : 0;
            if (!game_clock::Reached(settle_, Frames(cfg_.settle_ms))) return p;
            state_ = State::Running; slot_ = 0; t_ = 0; ++cycles_; cycle_offers_ = cycle_shown_ = 0;
            p.cycle_begin = true;
            Open(); p.offer_begin = true;
        } else if (state_ == State::Running) {
            if (w.gameplay) t_ += dt;
            const double offer = Frames(cfg_.offer_ms), slot_len = offer + Frames(cfg_.gap_ms);
            if (open_ && game_clock::Reached(t_, offer)) { Close(); p.offer_end = true; }
            if (game_clock::Reached(t_, slot_len)) {
                t_ -= slot_len; if (t_ < 0) t_ = 0;
                if (++slot_ >= Slots()) { state_ = State::Done; p.cycle_end = true; return p; }
                Open(); p.offer_begin = true;
            }
        } else return p;   // Done: until the next load
        if (!open_) return p;
        ++cur_.frames;
        cur_.t_ms = Ms(t_);
        if (!w.gameplay) { ++cur_.paused; return p; }
        p.inject = true; p.btn = cur_.btn; p.pass = cur_.pass;
        return p;
    }
    // After our set(): the selector the game had, the renderer word and the work set() filled.
    void Injected(int32_t selector, int32_t renderer, const WorkFacts& w) {
        if (!open_) return;
        if (!w.readable) { ++cur_.unreadable; return; }   // a failed read is no remap evidence either way
        if (cur_.injected && selector != cur_.selector) ++cur_.selector_changes;
        if (!cur_.injected) { cur_.selector = selector; cur_.renderer = renderer; }
        ++cur_.injected;
        cur_.work_count = w.count; cur_.work_kind = w.kind; cur_.work_btn = w.btn; cur_.work_func = w.func; cur_.work_flags = w.flags;
        cur_.remapped = w.btn;
        const int32_t used = cur_.pass == Pass::PadForced ? 1 : selector;
        cur_.glyph = Glyph(w.btn, used);
        if (w.btn != Remap(cur_.btn, used)) ++cur_.remap_bad;
        if (w.readable && w.count == 1 && w.kind == kCensusKind && w.func == 0 && w.flags == kCensusFlags) ++cur_.own;
    }
    // After the arbiter: was it ours that was drawn, and the QTE byte.
    void Exit(uint8_t active, uint8_t cockpit_btn, uint8_t cockpit_kind, uint8_t qte) {
        if (!open_) return;
        cur_.cockpit_btn = cockpit_btn; cur_.cockpit_kind = cockpit_kind;
        if (Displayed(active, cockpit_btn, cockpit_kind, cur_.remapped)) ++cur_.displayed;
        if (qte) ++cur_.qte;
    }
    static constexpr double Frames(double ms) { return ms * 0.06; }
    static constexpr double Ms(double frames) { return frames / 0.06; }

private:
    void Open() {
        cur_ = OfferStats{}; cur_.slot = slot_; cur_.btn = SlotBtn(slot_); cur_.pass = SlotPass(slot_); open_ = true;
    }
    void Close() {
        open_ = false; last_ = cur_; ++cycle_offers_;
        if (cur_.Shown()) ++cycle_shown_;
    }
    CensusConfig cfg_;
    State state_ = State::Off;
    bool known_ = false, open_ = false;
    uint32_t player_ = 0, global_ = 0, room_ = 0, frame_ = 0;
    double last_now_ = 0, settle_ = 0, t_ = 0;
    unsigned slot_ = 0, cycles_ = 0, cycle_offers_ = 0, cycle_shown_ = 0;
    OfferStats cur_, last_;
};

// Key bits: one line per change of Key_btn_on outside the mouse axes, at most kKeyLinesPerSecond (real time).
struct KeyChange { bool log = false; uint64_t rise = 0, fall = 0; uint32_t skipped = 0; };
class KeyWatch {
public:
    static constexpr uint32_t kKeyLinesPerSecond = 20;
    KeyChange Step(uint64_t on, uint64_t now_ms) {
        KeyChange k;
        if (!known_) { known_ = true; last_ = on; return k; }
        const uint64_t diff = (on ^ last_) & ~key::kMouseAxes;
        k.rise = on & ~last_; k.fall = last_ & ~on;
        last_ = on;
        if (!diff) return k;
        if (now_ms - second_ >= 1000) { second_ = now_ms; lines_ = 0; }
        if (lines_ >= kKeyLinesPerSecond) { ++skipped_; return k; }
        ++lines_; k.log = true; k.skipped = skipped_; skipped_ = 0;
        return k;
    }
private:
    bool known_ = false; uint64_t last_ = 0, second_ = 0; uint32_t lines_ = 0, skipped_ = 0;
};

// ---------------------------------------------------------------- pins (all or nothing)
// What the census needs: set()'s remap and its switch (from 0x18AA6D, where kMortalDrawPrompt's window ends), the
// glyph table, disp's cockpit writes, the QTE byte reset, the renderer word's writer, the edge detector of Key_btn.
inline constexpr native_pins::Pin kCensusPins[] = {
    {0x18AA6D, "8d0481565088560fe8ef65e7ff83c408833db02422010175178a460f3c03740a3c0d7522c6460f02eb1cc6460f12eb160fb6460f83e811740948750ac6"
               "460f04eb04c6460f0b0fb6460f4883f80d77230fb688f0aa5800ff248de8aa5800f6461401740fa13c6fc00081881c500000000020005e5dc2"
               "2000"},
    {0x18AAE8, "cbaa5800e0aa58000000010001010101010101010100"},
    {0x7FBFF0, "0020002017211903042407070911002000200a0a0b0b06060505170c08220d250e0e172119031821"},
    {0x18A254, "8a45fc8b0d406fc000881db710c500a2b510c5008a510b5e8815b910c500"},
    {0x23E636, "881d583ac600"},
    {0x5644F2, "c705446fc00001000000"},
    {0x56A4F0, "8b1d002fc6008b15042fc6008bc333c18bf023f133d78935102fc6008bf22315042fc60023f723c333db8935142fc600a3182fc60089"}};
// ActionButton::move (hooked only for the pad pass): prologue, the cockpit compare, the selector and the table reads.
inline constexpr native_pins::Pin kMovePins[] = {
    {0x1A2FD0, "558bec51568bf18a46043a46067404c64601008a063a4601750c8a4e023a4e030f8405010000833db024220101750f0fb6d08a0455f0bf"
               "bf008845fceb0d0fb6c88a144df1bfbf008855fc"}};
// Evidence of the bits and btns (logged as bits_pinned=; the census does not depend on them): the per-device wrapper,
// checkButton's dispatch, table and handlers, and the key-name map entries the duel uses.
inline constexpr native_pins::Pin kBitPins[] = {
    {0x18AD40, "558bec833db0242201018b452074038b451c8b4d288b5524518b4d14528b5510508b4518508b450c518b4d08525051b9c002c500e86222e8ff"
               "5dc22400"},
    {0x18A34D, "83f9120f8742040000ff248da4a75800"},
    {0x18A7A4, "7ea4580077a4580041a55800cfa5580077a65800a3a65800cfa6580098a75800fba658004fa7580098a7580098a7580065a458005da35800"
               "98a7580098a758000ea558000ea558003aa55800"},
    {0x18A35D, "8b4314a8407577a8027548a8107427526800000800526800000800"},
    {0x18A393, "6a11e83be2e7ff83c40485c00f84f3030000"},   // flags without 2/0x10/0x40: 0x4085D5(0x11), not Key_btn
    {0x18A465, "5f5ec605583ac6000132c0"},
    {0x18A477, "c605583ac600018b0d3c6fc000b80d020000663981ac4f0000742e891510cfc7008b4314a8407554a8020f84c1feffff6800000100525268"
               "00000800"},
    {0x18A50E, "680000010052e8f646e8ff83c40884c0"},
    {0x18A53A, "6800000400ebd2"},
    {0x18A5CF, "6a006800000800c605583ac60001e82d46e8ff8ad883c40884db74136a006800000400e81846e8ff83c40884c07545"},
    {0x18A677, "526800000400e88d45e8ff83c40884c0"},
    {0x18A6A3, "526800020000e86145e8ff83c40884c0"},
    {0x18A6CF, "526800000100e83545e8ff83c40884c0"},
    {0x18A6FB, "526800000008c605583ac60001e80245e8ff83c40884c0"},
    {0x18A74F, "526800000004c605583ac60001e8ae44e8ff83c40884c0"},
    {0x294E29, "8d45f433f6508d8bc0000000c745f401000000"},
    {0x294E73, "c745f4040000008975f8e89a1dd7ffc700ec03b200"},
    {0x294E92, "c745f4080000008975f8e87b1dd7ff8d4df4518d8bc0000000c700e003b200"},
    {0x294EB1, "c745f4100000008975f8e85c1dd7ff8d55f4528d8bc0000000c700d403b200"},
    {0x294F0E, "c745f4800000008975f8e8ff1cd7ff8d55f4528d8bc0000000c700ac03b200"},
    {0x294F4C, "c745f4000200008975f8e8c11cd7ffc7008c03b200"},
    {0x294F61, "c745f4000400008975f88d4df4518d8bc0000000e8a21cd7ff8d55f4528d8bc0000000c7008403b200"},
    {0x294F8A, "c745f4000800008975f8e8831cd7ffc7007003b200"},
    {0x295025, "c745f4000001008975f8e8e81bd7ff8d55f4528d8bc0000000c7003803b200"},
    {0x295044, "c745f4000002008975f8e8c91bd7ffc7003003b200"},
    {0x295063, "c745f4000004008975f8e8aa1bd7ff8d4df4518d8bc0000000c7002803b200"},
    {0x295082, "c745f4000008008975f8e88b1bd7ff8d55f4528d8bc0000000c7002003b200"},
    {0x29515B, "c745f4000000048975f8e8b21ad7ffc700e002b200"},
    {0x29517A, "c745f4000000088975f8e8931ad7ff8d4df4518d8bc0000000c700d802b200"},
    {0x295405, "8975f4c745f800000100e80818d7ff8d4df4518d8bc0000000c700e801b200"},
    {0x295443, "8975f4c745f800000400e8ca17d7ffc700cc01b200"},
    {0x7203EC, "4b45595f524947485400"}, {0x7203E0, "4b45595f4c45465400"}, {0x7203D4, "4b45595f4b414d414500"},
    {0x7203AC, "4b45595f4649524500"}, {0x72038C, "4b45595f4153484c455900"}, {0x720384, "4b45595f434b00"},
    {0x720370, "4b45595f4b414d41455f4b4e49464500"}, {0x720338, "4b45595f5900"}, {0x720330, "4b45595f5800"},
    {0x720328, "4b45595f4200"}, {0x720320, "4b45595f4100"}, {0x7202E0, "4b45595f5200"}, {0x7202D8, "4b45595f4c00"},
    {0x7201E8, "4b45595f4b4e49464500"}, {0x7201CC, "4b45595f4352414e4b00"}};
}  // namespace link_runtime::sword_lock::glyphs

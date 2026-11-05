#pragma once
// Master Sword Blade Lock H1c, the duel's own icons (route B1 of D:/link-in-re4-work/swordlock-icons/ruta.md, 3-nov-2026).
// Lab DLL only: link_runtime.cpp includes and wires this header inside #if LINK_SWORD_LOCK_LAB, with the H1c duel
// ([Combat] SwordLock=1 and SwordLockLab=1). Pure: no hooks, no process access; the runtime gives the reads and the four
// native calls (Native contracts below, SEH around each) and draws through the IDSystem path of ocarina_staff.h.
//
// What it draws: the key the player really has bound, taken from the game's own PC key atlas (cockpit tex id 0xA5, image
// #332 of 07000000.pack, rectangles from BIO4/uvdata*.t3d, so it follows the game's language: "Space", "Espacio",
// "Espace"...), for KEY_KAMAE_KNIFE in the mashes (Push 1 and the Finish: Space) and KEY_A in the break (Twist: E), at the
// native QTE prompt's place (0,-89), animated in sync with the duel: the key hammers at 8 Hz (up/down, scale 1.0/0.94, a
// darker shadow) with the grey arrow of the native B_RAPID bouncing over it, a ring (tex 0xF2) grows 1.0 -> 1.4 and fades
// in 150 ms on every counted flank, in the break a ring closes with the window (1.9 -> 1.05), a hit flashes gold (200 ms
// flash, 250 ms tint), a miss tints red and shakes 2 units for 200 ms (the mash's lock after a miss tints red 120 ms).
// Every time is game time (the duel's game ms: game_clock Step60), the same at 60 and 165 FPS. At most kUnits (6) units.
// Hidden in a pause (0x806F39, the MENUS set, a sub-screen request: sword_lock_lab.h Paused), at the end or abandon of the
// duel, outside the lab, on a pad (device word 0x806F44 2/3 or the pad column of the selector 0xE224B0: the XInput glyphs
// are other cockpit textures, out of scope) and for a key the atlas has not (the default "0" rect).
//
// The key, by the native chain set() uses for KEY_CK in its mode 5 (0x247206..0x24728A):
//   [0x866CE0] the input manager (read only: its getter 0x2A5810 creates it when null, so it is never called),
//   0x2900A0 (thiscall mgr): the GAMEPLAY binding set PadRead builds Key_btn from outside menus (0x56868F..0x56869D)
//            and set() asks (0x24720D): the override mgr+0x80 when [mgr+0x88] != 0, else the default set mgr+0x70. The
//            menus set is mgr+0x00 (0x28E500, PadRead 0x56866E): any other map is "menus_map", tried again later,
//   0x245150 (thiscall map, Node** out, const uint64_t* logical, RET 8): std::map::find; the head is [map+4] (set() does
//            not check it: here a head is "unbound"), the node's key at +0x10/+0x14 and its value, the physical code, at
//            +0x18 (lo) / +0x1C (hi) (0x24723B),
//   0x1E600 (cdecl Record* out, lo, hi): a compare tree (Space 0x39 -> 99, mouse 0x200/0x400/0x800 -> 175..177, axes and
//            wheels 0x400001.. and hi 0x100000 -> 185..189 / 276..279) and, for the rest, the letter table 0x84B940
//            (byte [lo*4 + 0x84B941], filled at boot from the user's keyboard layout); it copies the 0x1C-byte record
//            0x84BD40 + idx*0x1C (0x1EF07..0x1EF45). Called only for a code whose table read stays inside the table
//            (SafeCode). The copy must equal the table's own entry at its idx, which the uvdata reader 0x23780 writes.
// The record: idx, u0, v0, u1, v1 (normalized; V over the atlas' 2015), the rectangle's width and height in texels.
// set() writes unit +0x11C = u0, +0x120 = u1, +0x124 = v0, +0x128 = v1 (0x247260) and, in mode 5 (the centred QTE glyph
// 0x22), the size width * 0.5 x height * 0.5 (double 0.5 at 0x70FB50; 0x247290, 0x2472D4): Space 131x64 -> 65.5x32,
// E 52x64 -> 26x32. The IdSys draw reads those four UV floats of every unit, whatever its class (0x2430B2..0x24324E); a
// new unit gets 0/1 (0x2417C0..0x2417E3) and only set() writes them again: with [0x859818] at 0 (cleared on every exit
// of set()) and our class 0x3E (the remap runs for class 0x20 only), our set() remaps nothing.
// The textures are the cockpit's (core.udas EFF 0x19): 0xA5 the key atlas, 0xE4 the soft glow, 0xF2 the white disc, 0x0D
// the grey arrow of B_RAPID. Each of the four TPLs holds one image (tools/eff_tex.py over core.udas EFF 0x19, 3-nov:
// 0x0D #238 64x64, 0xA5 #332, 0xE4 #350 96x96, 0xF2 #356 48x48; ruta-evidence/native_prompt_textures.png shows a single
// arrow, glow and disc), so tex_no 0 and the full UV 0/1 (a new unit's) are the whole picture for the frame pieces; only
// the key pieces take a sub-rectangle of 0xA5. None is ours to load or release: each one must be registered, TEX_WK[id] (g_pIdTexSys
// 0x859B8C, 0x54 bytes each) with its owner +0x50 non-zero (GetTexWk 0x2D4ED0 returns null otherwise and the draw skips
// the unit) and its TPL +0x48 non-zero; any missing one hides the icon (no_tex) and the offer falls back.
//
// The native offer: with the icon live, the mash and the break offers of sword_lock_lab.h go out with btn 0, so move()
// skips its third set() (the glyph, 0x1A3079) and the kind (messages 0x5C/0x5E, emptied) and the arbiter's rules stay;
// the per-press gap of the mash (rebuilding the native icon) is dropped: the ring is ours. The break uses our icon too,
// by evidence: btn 1 draws E live (census H1a, "E KICK"), but its glyph 0x20 sits at (-60,-150) next to the text while
// the push icon is at (0,-89), so the prompt would jump 61 left and 61 down inside the 250 ms warning of a 1000 ms window;
// only our painter can close a ring with that window; and set() puts both on the same atlas (0xA5), so the key is the
// same. Fallback (vuelta atras): without the icon (pins, a pad, a key without face, no_tex, set_failed, class_taken) the
// offers are exactly H1c's (btn 5 with its gaps, btn 1). Plan B of the report (A1, the 4-byte immediate at 0x247228
// patched only inside our move()) is not built: B1 needs no write in .text and gives every control of H1d a face.
// Tests: tests/native/test_sword_lock_icons.cpp (scripts/test_sword_lock_icons.bat; with the mapped image every pin).
#include "sword_lock_lab.h"
#include "native_pins.h"
#include "ocarina_staff.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace link_runtime::sword_lock::icons {
namespace sd = link_runtime::sword_lock::duel;
namespace lab = link_runtime::sword_lock::lab;
namespace sg = link_runtime::sword_lock::glyphs;
namespace staff = link_runtime::ocarina::staff;

namespace rva {
constexpr uint32_t kInputSlot = 0x866CE0;   // the input manager (its getter 0x2A5810 creates it: never called)
constexpr uint32_t kActiveMap = 0x2900A0;   // thiscall(mgr) -> map
constexpr uint32_t kFind = 0x245150;        // thiscall(map, Node** out, const uint64_t* logical), RET 8
constexpr uint32_t kKeyGlyph = 0x1E600;     // cdecl(Record* out, lo, hi) -> out
constexpr uint32_t kRecords = 0x84BD40;     // Record[kMaxRecord + 1]
constexpr uint32_t kDevice = 0x806F44;      // 0 keyboard, 1 mouse, 2 XInput, 3 DirectInput (sword_lock_glyphs.h kRendererDevice)
constexpr uint32_t kSelector = 0xE224B0;    // 1 = the pad column (sword_lock_glyphs.h kSelector)
}  // namespace rva
namespace layout {
constexpr uint32_t kMgrGameMap = 0x88, kMgrMapGame = 0x80, kMgrMapDefault = 0x70, kMgrMapMenus = 0x00;   // 0x2900A0, 0x28E500
constexpr uint32_t kMapHead = 0x04;                                                 // 0x245161
constexpr uint32_t kNodeKey = 0x10, kNodeValue = 0x18;                              // 0x24516B, 0x24723B
constexpr uint32_t kRecordSize = 0x1C;
// ID_UNIT UV (0x247260): u0, u1, v0, v1, contiguous.
constexpr uint32_t u_uv = 0x11C;
// ID_UNIT rot0.z (UHD; GC 0x100, decomp include/id_sys.h rot0 at 0xF8): idSysMove03 0x2429B0 copies rot0 (+0x104..+0x10C)
// into rot (+0x110..+0x118) every frame, then adds a curve only when the unit has one (ours none) and builds the matrix
// with rot * PI/180: degrees about the screen axis (cockpit.cpp writes the life needle's rot0.z the same way). H1d's
// Lateral turns the B_RAPID arrow with it; every other unit keeps 0.
constexpr uint32_t u_rot0_z = 0x10C;
// TEX_WK (GetTexWk 0x2D4ED0; IdTexDataLoad -> 0x2D4AE0 stores the TPL at +0x48 and the owner at +0x50).
constexpr uint32_t tex_wk_tpl = 0x48;
using namespace staff::layout;   // IdSys / ID_UNIT offsets, be_flag bits, tex_sys_ptr_rva, tex_wk_size / tex_wk_owner
// Ours. Class 0x3E: no set/kill/unitPtr call of the EXE uses an immediate class in 0x36..0x3F (the staff's census); the mod
// uses 0x3B (reticle), 0x3C (staff), 0x3D (HUD). No texture of our own in this phase (owner 0xCA3 stays reserved).
constexpr uint8_t id_class = 0x3E;
}  // namespace layout
constexpr int kMaxRecord = 279;   // the largest index 0x1E600 gives (0x117); the letter table's are signed bytes
constexpr uint8_t kTexKeys = 0xA5, kTexGlow = 0xE4, kTexDisc = 0xF2, kTexArrow = 0x0D;
constexpr double kScaleW = 0.5, kScaleH = 0.5;   // mode 5 (the centred QTE glyph 0x22): 0x247290 and 0x2472D4
constexpr uint32_t kDikZero = 0x0B;              // the '0' key: its glyph is the default rect (index 58 in the tree)
constexpr uint64_t kLogicalKnife = sg::key::kKamaeKnife, kLogicalAction = sg::key::kA;
// H1d's keyboard bag (sword_lock_draw.h): D, A and the right mouse button of the gameplay set (BIO4/input.ini GAMEPLAY
// KEYBOARD: D = KEY_RIGHT, A = KEY_LEFT, MB_RIGHT = KEY_KAMAE), through the same chain. The mouse buttons are named by the
// tree of 0x1E600 (0x200/0x400/0x800 -> 175..177); a code the atlas has not gives Unmapped and that step stays out.
constexpr uint64_t kLogicalRight = sg::key::kRight, kLogicalLeft = sg::key::kLeft, kLogicalAim = sg::key::kKamae;

// ---------------------------------------------------------------- pins (all or nothing, with kOcarinaStaff's IdSys)
inline constexpr native_pins::Pin kPins[] = {
    {0x2A5810, "a1e06cc60085c07525"},                                   // the getter reads [0x866CE0] (never called)
    {0x2900A0, "83b988000000008d818000000075038d4170c3"},               // active map: +0x80 with [+0x88], else +0x70
    {0x245150, "558bec5156578b7d0c578bf1e81debdbff8b760489450c3bc674238b4f043b4814721b77078b173b501072128d450c8b088b45085f"
               "89085e8be55dc20800"},                                    // find: head [+4], key +0x10/+0x14, RET 8
    {0x24723B, "8b008b481c8b5018"},                                     // set() mode 5: the value +0x18 lo / +0x1C hi
    {0x1E600, "558bec8b45108b4d0c85c0"},                                // KeyGlyph: hi [ebp+0x10], lo [ebp+0xC]
    {0x1EF07, "0fbe048d41b9c400eb13b817010000eb0cb816010000eb05b815010000568d34c5000000002bf08b4508578d34b540bdc400b907"
              "0000008bf8f3a55f5e5dc3"},                                // letter table 0x84B941, records 0x84BD40, 7 dwords
    {0x247260, "d98558ffffffd99b1c010000d98560ffffffd99b20010000d9855cffffffd99b24010000d98564ffffffd99b28010000"},
    {0x247290, "d98568ffffffdd0550fbb000dcc9"},                         // mode 5 width * [0x70FB50]
    {0x2472D4, "d9856cffffffd8c9"},                                     // mode 5 height * 0.5
    {0x70FB50, "000000000000e03f"},                                     // 0.5
    {0x2417C0, "d9eed9961c01000083c408d99e24010000c6060dd9e88bc6d99620010000d99e28010000"},   // a new unit: UV 0/1
    {0x2430B2, "d9862401000083c410d95c2404d9861c010000"},               // the draw reads v0, u0 of each unit
    {0x242E8F, "0fb64e7a0fb65678"},                                     // the draw binds tex id +0x78, tex no +0x7A
    {0x2D4ED0, "558bec0fb655088bc26bc05403c181fa000100007d0683785000751a"},   // GetTexWk: owner +0x50 != 0
    {0x242F05, "0fb6567b8b4048"},                                       // TEX_WK +0x48: the TPL
    {0x5644F2, "c705446fc00001000000"},                                 // the device word's writer (1 = mouse)
    {0x2429C3, "8b86040100008b8e080100008b960c010000898610010000"},     // idSysMove03: rot = rot0 (+0x104..+0x10C -> +0x110)
};

// ---------------------------------------------------------------- the key: physical code -> atlas record -> face
#pragma pack(push, 4)
struct Record {int32_t idx = 0; float u0 = 0, v0 = 0, u1 = 0, v1 = 0, w = 0, h = 0;};
#pragma pack(pop)
static_assert(sizeof(Record) == layout::kRecordSize, "the native record is 7 dwords");
// 0x1E600 reads the letter table at [lo*4 + 0x84B941] for every code its tree does not name: only codes that keep that
// read inside the 256-entry table, or that the tree names, are asked (a huge code would read past it).
inline bool SafeCode(uint32_t lo, uint32_t hi) {
    if (lo <= 0xFF) return true;
    return hi == 0 && (lo == 0x200 || lo == 0x400 || lo == 0x800 || (lo >= 0x400001 && lo <= 0x400005));
}
inline bool SameRect(const Record& a, const Record& b) {return a.u0 == b.u0 && a.v0 == b.v0 && a.u1 == b.u1 && a.v1 == b.v1;}
inline bool SameRecord(const Record& a, const Record& b) {return std::memcmp(&a, &b, sizeof(Record)) == 0;}
// What the chain gave for one logical key (filled by Query or by hand in the tests).
struct Lookup {
    uint64_t logical = 0;
    bool manager = false;    // [0x866CE0] read and non-null
    bool game_map = false;   // 0x2900A0 returned a gameplay set (mgr+0x80 or mgr+0x70)
    bool fault = false;      // a native call or a read faulted
    bool found = false;      // the node is not the head (and its key is the one asked)
    uint32_t lo = 0, hi = 0; // the physical code
    bool asked = false;      // 0x1E600 was called (SafeCode)
    Record rec;              // its copy
    bool table = false;      // the table's entry at rec.idx was read
    Record at;
    bool zero = false;       // the table's entry 0 (the default rect) was read
    Record rec0;
};
enum class Why : uint8_t {Ok, NoManager, MenusMap, Fault, Unbound, Unsafe, BadRecord, Mismatch, Unmapped, Device, Off};
inline const char* WhyName(Why w) {
    switch (w) {
    case Why::Ok: return "ok"; case Why::NoManager: return "no_manager"; case Why::MenusMap: return "menus_map";
    case Why::Fault: return "fault"; case Why::Unbound: return "unbound"; case Why::Unsafe: return "unsafe_code";
    case Why::BadRecord: return "bad_record"; case Why::Mismatch: return "table_mismatch"; case Why::Unmapped: return "unmapped";
    case Why::Device: return "device"; default: return "off";
    }
}
// A transient reason: the chain is tried again later in the same duel (a menus frame, the manager not built yet).
inline bool Transient(Why w) {return w == Why::NoManager || w == Why::MenusMap || w == Why::Fault;}
struct Face {
    bool ok = false;
    Why why = Why::Off;
    uint64_t logical = 0;
    uint32_t lo = 0, hi = 0;
    int idx = 0;
    float uv[4] = {0, 1, 0, 1};   // as the unit stores them: u0, u1, v0, v1 (+0x11C..+0x128)
    float px_w = 0, px_h = 0;     // the rectangle in texels
    float w = 0, h = 0;           // on screen (IdSys units): mode 5's size
};
inline bool Plausible(const Record& r) {
    const float v[6] = {r.u0, r.v0, r.u1, r.v1, r.w, r.h};
    for (float x : v) if (!std::isfinite(x)) return false;
    return r.idx >= 1 && r.idx <= kMaxRecord && r.u0 >= 0 && r.u1 <= 1 && r.u0 < r.u1 && r.v0 >= 0 && r.v1 <= 1 && r.v0 < r.v1 &&
           r.w > 0 && r.w <= 512 && r.h > 0 && r.h <= 512;
}
inline Face Resolve(const Lookup& k) {
    Face f; f.logical = k.logical; f.lo = k.lo; f.hi = k.hi; f.idx = k.rec.idx;
    if (k.fault) {f.why = Why::Fault; return f;}
    if (!k.manager) {f.why = Why::NoManager; return f;}
    if (!k.game_map) {f.why = Why::MenusMap; return f;}
    if (!k.found) {f.why = Why::Unbound; return f;}
    if (!SafeCode(k.lo, k.hi)) {f.why = Why::Unsafe; return f;}
    if (!k.asked || !Plausible(k.rec)) {f.why = Why::BadRecord; return f;}
    if (!k.table || !SameRecord(k.rec, k.at)) {f.why = Why::Mismatch; return f;}
    // Index 0 and every index without a character carry the default rect (the '0' glyph): a key the atlas has not.
    if (!k.zero || (SameRect(k.rec, k.rec0) && !(k.lo == kDikZero && k.hi == 0))) {f.why = Why::Unmapped; return f;}
    f.ok = true; f.why = Why::Ok;
    f.uv[0] = k.rec.u0; f.uv[1] = k.rec.u1; f.uv[2] = k.rec.v0; f.uv[3] = k.rec.v1;
    f.px_w = k.rec.w; f.px_h = k.rec.h;
    f.w = float(k.rec.w * kScaleW); f.h = float(k.rec.h * kScaleH);
    return f;
}
// The chain on the game thread. Native contract (the runtime wraps every call and read in SEH):
//   uint32_t base;  bool Read(uint32_t addr, void* out, size_t n);
//   bool ActiveMap(uint32_t mgr, uint32_t& map);                     // thiscall base+0x2900A0
//   bool Find(uint32_t map, uint64_t logical, uint32_t& node);       // thiscall base+0x245150(&node, &logical)
//   bool KeyGlyph(uint32_t lo, uint32_t hi, Record& out);            // cdecl base+0x1E600(&out, lo, hi)
template <class Native> Lookup Query(Native& n, uint64_t logical) {
    Lookup k; k.logical = logical;
    uint32_t mgr = 0, map = 0, head = 0, node = 0;
    if (!n.Read(n.base + rva::kInputSlot, &mgr, 4)) {k.fault = true; return k;}
    if (!mgr) return k;
    k.manager = true;
    if (!n.ActiveMap(mgr, map)) {k.fault = true; return k;}
    // 3-nov live: the override +0x80 is empty in a normal game ([mgr+0x88] == 0), so 0x2900A0 returns the default
    // gameplay set +0x70; only the menus set (the manager itself) is refused.
    if (map != mgr + layout::kMgrMapGame && map != mgr + layout::kMgrMapDefault) return k;
    k.game_map = true;
    if (!n.Read(map + layout::kMapHead, &head, 4) || !n.Find(map, logical, node)) {k.fault = true; return k;}
    if (!node || node == head) return k;
    uint32_t kv[4] = {};
    if (!n.Read(node + layout::kNodeKey, kv, sizeof(kv))) {k.fault = true; return k;}
    if (kv[0] != uint32_t(logical) || kv[1] != uint32_t(logical >> 32)) return k;   // not the key asked: unbound
    k.found = true; k.lo = kv[2]; k.hi = kv[3];
    if (!SafeCode(k.lo, k.hi)) return k;
    if (!n.KeyGlyph(k.lo, k.hi, k.rec)) {k.fault = true; return k;}
    k.asked = true;
    if (k.rec.idx >= 0 && k.rec.idx <= kMaxRecord)
        k.table = n.Read(n.base + rva::kRecords + uint32_t(k.rec.idx) * layout::kRecordSize, &k.at, sizeof(Record));
    k.zero = n.Read(n.base + rva::kRecords, &k.rec0, sizeof(Record));
    return k;
}
// The glyphs set() draws on this device are the PC atlas: the keyboard or the mouse (0x806F44 0/1) with the selector on the
// PC column. A pad (2 XInput, 3 DirectInput: set() takes 0x1F1A0 for 3) or the pad column draws other textures.
inline bool KeyboardGlyphs(int32_t device, int32_t selector) {return (device == 0 || device == 1) && selector != 1;}
struct Faces {
    Face knife, action;   // KEY_KAMAE_KNIFE (the mashes) and KEY_A (the break, Twist)
    Face right, left, aim;   // H1d: KEY_RIGHT, KEY_LEFT, KEY_KAMAE (resolved only by the lab's H1d wiring; Off otherwise)
    const Face* For(uint8_t ctl) const {
        switch (ctl) {
        case sd::kKnife: return &knife; case sd::kAction: return &action; case sd::kRight: return &right;
        case sd::kLeft: return &left; case sd::kAim: return &aim; default: return nullptr;
        }
    }
    bool Any() const {return knife.ok || action.ok;}
    bool Transient() const {
        const Face* all[] = {&knife, &action, &right, &left, &aim};
        for (const Face* f : all) if (!f->ok && icons::Transient(f->why)) return true;
        return false;
    }
};
// A `sword_lock icon key` line is due on the duel's first resolve and then only when that face really changed (its ok,
// why, code or index): a device flip keyboard <-> mouse (0x806F44 0/1) draws the same key and logs nothing.
inline bool SameFace(const Face& a, const Face& b) {return a.ok == b.ok && a.why == b.why && a.lo == b.lo && a.hi == b.hi && a.idx == b.idx;}
inline bool KeyLineDue(bool first, const Face& was, const Face& now) {return first || !SameFace(was, now);}
// The control the icon paints for a prompt (Icon::Update's rule): a mash (gesture Mash) or the tap of a break; any other
// gesture (H1d's Alternate, Brace, Hold, Lateral...) has no icon and keeps H1c's offer.
inline uint8_t PaintCtl(const sd::Prompt& p) {
    if (!p.show) return sd::kNoCtl;
    if (p.gesture == sd::Gesture::Mash || (p.phase == sd::Phase::Break && p.gesture == sd::Gesture::Tap)) return p.ctl;
    return sd::kNoCtl;
}
// H1d's rule (the drawn duel, sword_lock_draw.h): every step paints its key. Mash, Swing (the next direction) and Alternate
// (only the next expected key, switching on each valid flank) in the mash stretches; the tap, the hold (Brace: E, Guard: the
// right mouse button) and the Lateral in the breaks. The Lateral shows its drawn direction until that key is held
// (`held`, the duel's last input), then the knife (Space) the gesture asks for.
inline uint8_t PaintCtlWide(const sd::Prompt& p, uint16_t held) {
    if (!p.show) return sd::kNoCtl;
    if (p.phase == sd::Phase::Break) {
        if (p.gesture == sd::Gesture::Lateral)
            return p.ctl != sd::kNoCtl && (held & sd::CtlBit(p.ctl)) ? p.ctl2 : p.ctl;
        if (p.gesture == sd::Gesture::Tap || p.gesture == sd::Gesture::Hold) return p.ctl;
        return sd::kNoCtl;
    }
    if (p.gesture == sd::Gesture::Mash || p.gesture == sd::Gesture::Swing || p.gesture == sd::Gesture::Alternate) return p.ctl;
    return sd::kNoCtl;
}
// The control behind one offer: the resolved break's feedback shows that break's key (Icon::break_ctl), the rest the prompt's.
inline uint8_t OfferCtl(const lab::Offer& o, const sd::Prompt& p, uint8_t break_ctl) {return o.feedback ? break_ctl : PaintCtl(p);}
inline uint8_t OfferCtlWide(const lab::Offer& o, const sd::Prompt& p, uint8_t break_ctl, uint16_t held) {
    return o.feedback ? break_ctl : PaintCtlWide(p, held);
}
// The offer of the lab (sword_lock_lab.h Offers) has its key in our icon: a push or break offer whose control (OfferCtl)
// the icon paints with a face; otherwise the native offer stays H1c's and the prompt is never left without a key.
inline bool Covers(const lab::Offer& o, const Faces& f, uint8_t ctl) {
    if (!o.show && !o.gap) return false;
    if (o.kind < lab::kKindPush || o.kind > lab::kKindPush + 7) return false;   // 0x46..0x4D (cand2/3: per gesture, HOLD AIM, WAIT)
    const Face* face = f.For(ctl);
    return face && face->ok;
}
// What goes to cActionButton::set: with the icon, btn 0 (move() skips the glyph set(); the kind stays) and no gap (no native
// icon to rebuild); without it, H1c's offer unchanged (the fallback: btn 5 with its gaps, btn 1).
inline lab::Offer NativeOffer(lab::Offer o, bool icon) {
    if (!icon) return o;
    if (o.gap) {o.gap = false; o.show = true;}
    if (o.show) o.btn = sg::btn::kOff;
    return o;
}

// ---------------------------------------------------------------- the icon (units, animation)
constexpr int kUnits = 6;
static_assert(kUnits <= 6, "at most 6 IdSys units (the HUD shares 0x80)");
enum Unit : uint8_t {kGlow = 0, kRing, kShadow, kKey, kFlash, kArrow};
// Table order = the native glyph tree's (0xE4 burst, 0xF2, the 0xA5 key, 0xF2, ...): glow, ring, shadow, key, flash, arrow.
inline constexpr uint8_t kTex[kUnits] = {kTexGlow, kTexDisc, kTexKeys, kTexKeys, kTexDisc, kTexArrow};
constexpr double kHalfBeatMs = 62.5;     // 8 Hz: up 62.5 ms, down 62.5 ms
constexpr double kRingMs = 150, kRingGrow = 0.4;
constexpr double kPopMs = 120, kPopScale = 0.25;
constexpr double kRingOpen = 1.9, kRingClose = 1.05;
constexpr double kFlashMs = 200, kShakeMs = 200, kShakeAmp = 2, kShakePeriodMs = 50, kLockTintMs = 120;
constexpr double kFeedbackMs = lab::kFeedbackMs;   // 250: the resolved break keeps its icon, as H1c's offer
constexpr float kDownScale = 0.94f, kDownDrop = 1.5f, kShadowDrop = 2.5f, kRingPad = 14.f, kArrowSize = 20.f, kArrowGap = 12.f;
struct Geometry {float x = 0.f, y = -89.f;};   // the native QTE glyph's root (glyph 0x22, UWF core.udas 0x1C)
struct View {
    bool visible = false;
    uint8_t tex_id = 0, tex_no = 0;
    float uv[4] = {0, 1, 0, 1};   // u0, u1, v0, v1 (+0x11C..+0x128)
    float x = 0, y = 0, w = 0, h = 0;
    uint8_t rgba[4] = {255, 255, 255, 255};
    float rot = 0;                // degrees (ID_UNIT rot0.z): only the Lateral's arrow turns
};
// Hold and Lateral: H1d's wide rule only (a hold break, a Lateral break); H1c never sees them.
enum class Mode : uint8_t {None, Mash, Break, Hit, Miss, Hold, Lateral, Timing};
inline const char* ModeName(Mode m) {
    switch (m) {
    case Mode::Mash: return "mash"; case Mode::Break: return "break"; case Mode::Hit: return "hit"; case Mode::Miss: return "miss";
    case Mode::Hold: return "hold"; case Mode::Lateral: return "lateral"; case Mode::Timing: return "timing";
    default: return "none";
    }
}
// Candidate 4 (Donald, 4-nov: "demarcar la zona donde uno no tiene que apretar y donde uno si"): the track is the disc
// 0xF2's flat centre stretched to a crisp bar tinted dim red over its whole length (kRing: "don't press" before and after
// the zone), the press zone the same over it, bright gold (kFlash, drawn after the key; green while the needle is inside
// it), the great part the glow 0xE4's flat centre in white, its exact width at the zone's start and twice the zone's
// height (kGlow, drawn first: it shows as white tabs above and below the zone), the needle
// the arrow 0x0D drawn last, on top. Six units still (the HUD shares IdSys 0x80: kUnits stays 6). A timing hit tints the
// zone and the key gold instead of the round flash (kFlash is the zone here).
// Candidate 2's timing check (sword_lock_duel.h Prompt::timing; Donald 4-nov: "tipo Dead by Daylight"): a straight track
// under the key with its zone and a needle, not a ring. The cockpit's textures have no arc (0xF2 and 0xE4 are whole
// discs, 0x0D one arrow), so an arc zone cannot be drawn with them: a track reads the same at a glance and the needle's
// place is unambiguous. Within the same 6 units: the soft glow 0xE4 stretched to the track (kGlow), the disc 0xF2
// stretched to the zone (kRing, gold) and to its great part (kFlash, white; a hit's flash as before), the arrow 0x0D
// pointing down onto the track as the needle (kArrow; the Lateral's direction arrow until the direction is held). All
// of it from the warning (needle at the start), the needle at the press through the feedback (where it was pressed).
// Candidate 5 (Donald's 1920x1080 recording, 4-nov: the track read as a thin line, its centre on the native text's top):
// the bar and the zone thicker, the needle larger (the gap grows with it: the needle stands between the key and the
// bar, never over the key), before the prompt scale below (7 -> 9, 11 -> 13, 15 -> 18, gap 22 -> 25).
constexpr float kTrackLen = 150.f, kTrackH = 9.f, kTrackGap = 25.f, kZoneH = 13.f, kNeedleSize = 18.f;
// Hold (cand2): the empty track brighter (the H1d mock-up showed it too faint) and visible from the warning's start.
constexpr double kHoldTrackAlpha = 235, kHoldWarnFloor = 0.6;
// Candidate 3: the right mouse button's face drawn larger (the atlas' 48x64 px record, scaled), and the WAIT key's shade.
constexpr float kAimScale = 1.4f;
// Candidate 5: the Swing mash's pair: the gap between A and D, and the waiting key's shade.
constexpr float kPairGap = 8.f;
constexpr uint8_t kPairDim = 125, kPairDimAlpha = 215;
// Candidate 5: the whole prompt (key, rings, track, needle, arrows) scaled about the QTE root and lifted so its lowest
// point (the track's great tabs, kPromptLow x scale below the root) sits at kPromptBottom, just above the native text
// (its top measured at about HUD y -128 on the 1920x1080 recording: 925 px, 3 px per unit around 540); [Combat]
// SwordLockPromptScale (lab, missing = kPromptScale). The units' positions and sizes only: the unit budget is the same.
constexpr float kPromptScale = 1.6f, kPromptScaleMin = 1.f, kPromptScaleMax = 2.2f;
constexpr float kPromptLow = 16.f + kTrackGap + kZoneH;   // the E key's half height + the gap + the great's half height
constexpr float kPromptBottom = -124.f;
inline float PromptLift(float s) {return kPromptBottom + kPromptLow * s - Geometry{}.y;}
// Candidate 6 (two Slash taps at 106 / 142 ms after an A+D mash): WAIT more prominent, the key darker and the red bar
// pulsing at kWaitPulseMs (was 120 / 200, no pulse).
constexpr uint8_t kWaitShade = 75, kWaitAlpha = 170;
constexpr double kWaitPulseMs = 250;
// H1d visuals (Donald, 4-nov: "Si, hazlas"). HOLD: the key sits pressed (the mash's down scale and dark shadow) inside a
// fixed track (the soft glow 0xE4, blue) from the warning on, so it never reads as a tap's closing ring; while the hold
// runs the disc (0xF2, gold) fills the track from nothing to its full size at the hold's length (Brace 500 ms, Guard
// 400 ms); a release empties it; full is the hit's flash as before. LATERAL: the drawn side's key (A or D) with the B_RAPID arrow (0x0D, it points down) turned
// 90 degrees to that side next to it, nudging toward it; once the direction is held the key turns to Space and the arrow
// stays, so "direction + Space" reads as one gesture. Mash, Alternate, Swing and the taps keep their look.
constexpr double kHoldTrack = 1.9, kHoldFill0 = 1.0;   // the track around the key and the fill's start (x the key's side)
constexpr double kNudgeMs = 250, kNudge = 3;   // the Lateral arrow's push toward its side
// The arrow's turn for a side (the texture points down; rot0.z degrees, counter-clockwise on a y-up HUD). A sign the
// live test has not confirmed yet: if the arrow shows the other side, this is the one constant to flip.
constexpr float kArrowTurnRight = 90.f;
inline float ArrowTurn(uint8_t side) {return side == sd::kSideLeft ? -kArrowTurnRight : kArrowTurnRight;}
// A hold's length by the control the prompt asks (sword_lock_duel.h Info: Brace on the action, Guard on the aim).
inline double HoldMsFor(uint8_t ctl) {return ctl == sd::kAim ? sd::Info(sd::Break::Guard).hold_ms : sd::Info(sd::Break::Brace).hold_ms;}
enum class Hide : uint8_t {None, Off, Pause, Device, Idle, NoKey};
inline const char* HideName(Hide h) {
    switch (h) {
    case Hide::None: return "none"; case Hide::Off: return "off"; case Hide::Pause: return "pause"; case Hide::Device: return "device";
    case Hide::Idle: return "idle"; default: return "no_key";
    }
}
// One logical tick, after the duel's (the same frame of the player).
struct Input {
    bool on = false;          // the lab duel holds the world and runs (Stage::Duel, Duel::Running)
    bool paused = false;      // sword_lock_lab.h Paused: 0x806F39 / the MENUS set / a sub-screen request / unreadable
    bool keyboard = true;     // KeyboardGlyphs(device, selector)
    sd::Prompt prompt;        // the duel's prompt after the tick
    uint32_t ev = 0;          // the tick's events (kEvFlank, kEvBeat, kEvLocked, kEvHit, kEvMiss)
    bool break_ok = false;    // the resolved break's ok (with kEvHit / kEvMiss)
    double now_ms = 0;        // the duel's game ms (game_clock): the clock of every animation
    double window_ms = 1000;  // the break window (Duel::window_ms)
    double needle = -1;       // cand2: the resolved timing break's needle at the press (BreakRecord::needle; -1 none)
    bool wide = false;        // H1d's drawn duel: PaintCtlWide (every step), else H1c's PaintCtl
    uint16_t held = 0;        // the duel's last held controls (the Lateral's direction), with wide
};
struct TrackView {bool on = false; double needle = 0, zone0 = 0, zone1 = 1, great1 = 0;};
inline float Clamp01(double v) {return float(v < 0 ? 0 : v > 1 ? 1 : v);}
inline uint8_t Byte(double v) {return uint8_t(v <= 0 ? 0 : v >= 255 ? 255 : v + 0.5);}
class Icon {
public:
    void Reset() {const float s = scale_; const bool p = pair_on_; *this = Icon(); scale_ = s; pair_on_ = p;}   // cand5's settings survive
    void SetScale(float s) {scale_ = s < kPromptScaleMin ? kPromptScaleMin : s > kPromptScaleMax ? kPromptScaleMax : s;}
    void SetSwingPair(bool on) {pair_on_ = on;}   // candidate 5: the A/D mash as two keys (off: H1d's one key + arrow)
    bool pair() const {return pair_;}
    float scale() const {return scale_;}
    // Candidate 5: every unit scaled about the root and lifted by PromptLift (1 = H1d's sizes exactly).
    View Get(int unit, const Geometry& g = Geometry{}) const {
        View v = Raw(unit, g);
        if (!v.visible || scale_ == 1.f) return v;
        const float ay = g.y + PromptLift(scale_);
        v.x = g.x + (v.x - g.x) * scale_; v.y = ay + (v.y - g.y) * scale_; v.w *= scale_; v.h *= scale_;
        return v;
    }
    void Update(const Input& in, const Faces& f) {
        if (!in.on) {const unsigned r = rings_, fl = flashes_, mi = misses_, lo = locked_; Reset(); rings_ = r; flashes_ = fl; misses_ = mi;
                     locked_ = lo; return;}
        now_ = in.now_ms; window_ = in.window_ms > 0 ? in.window_ms : 1000;
        const sd::Prompt& p = in.prompt;
        if (in.ev & (sd::kEvFlank | sd::kEvBeat)) {ring_t0_ = now_; ++rings_;}
        if (in.ev & sd::kEvLocked) {lock_t0_ = now_; ++locked_;}
        if (in.ev & (sd::kEvHit | sd::kEvMiss)) {
            fb_t0_ = now_; fb_ok_ = (in.ev & sd::kEvHit) != 0 && in.break_ok; fb_ctl_ = brk_ctl_;
            if (fb_ok_) ++flashes_; else ++misses_;
        }
        if (p.phase != phase_) {phase_ = p.phase; phase_t0_ = now_;}
        const uint8_t paint = in.wide ? PaintCtlWide(p, in.held) : PaintCtl(p);
        if (p.phase == sd::Phase::Break && p.show) {
            brk_ctl_ = paint; remaining_ = p.remaining_ms; warning_ = p.warning; warn_ms_ = p.warn_ms > 0 ? p.warn_ms : sd::kWarnMs;
            track_.on = p.timing; track_.needle = p.needle; track_.zone0 = p.zone0; track_.zone1 = p.zone1; track_.great1 = p.great1;
        }
        if (in.ev & (sd::kEvHit | sd::kEvMiss)) {fb_track_ = track_; if (in.needle >= 0) fb_track_.needle = in.needle;}
        const bool feedback = fb_t0_ >= 0 && now_ - fb_t0_ < kFeedbackMs && fb_ctl_ != sd::kNoCtl;
        uint8_t ctl = sd::kNoCtl;
        mode_ = Mode::None;
        if (feedback) {ctl = fb_ctl_; mode_ = fb_ok_ ? Mode::Hit : Mode::Miss; shown_ = fb_track_;}
        else if (paint != sd::kNoCtl) {
            ctl = paint;
            if (in.wide) mode_ = p.phase != sd::Phase::Break ? Mode::Mash : p.gesture == sd::Gesture::Hold ? Mode::Hold :
                                 p.gesture == sd::Gesture::Lateral ? Mode::Lateral : p.timing ? Mode::Timing : Mode::Break;
            else mode_ = p.gesture == sd::Gesture::Mash ? Mode::Mash : Mode::Break;
            shown_ = p.phase == sd::Phase::Break ? track_ : TrackView{};
        }
        if (!feedback && paint == sd::kNoCtl) shown_ = TrackView{};
        hold_ms_ = mode_ == Mode::Hold ? HoldMsFor(p.ctl) : 0;
        fill_ = mode_ == Mode::Hold && p.executing && hold_ms_ > 0 ? Clamp01(p.gesture_ms / hold_ms_) : 0.0;
        side_ = mode_ == Mode::Lateral ? p.side : sd::kSideNone;
        swing_ = mode_ == Mode::Mash && p.gesture == sd::Gesture::Swing && (ctl == sd::kLeft || ctl == sd::kRight);
        ctl_ = ctl;
        const Face* face = f.For(ctl);
        if (in.paused) hide_ = Hide::Pause;
        else if (!in.keyboard) hide_ = Hide::Device;
        else if (mode_ == Mode::None) hide_ = Hide::Idle;
        else if (!face || !face->ok) hide_ = Hide::NoKey;
        else hide_ = Hide::None;
        if (face && face->ok) face_ = *face; else face_ = Face{};
        // candidate 5: the Swing mash shows both keys (A and D) side by side, the one asked next lit
        const Face* other = swing_ ? f.For(ctl == sd::kLeft ? sd::kRight : sd::kLeft) : nullptr;
        pair_ = pair_on_ && swing_ && other && other->ok;
        other_ = pair_ ? *other : Face{};
        if (hide_ == Hide::None) ++frames_;
    }
    bool Visible() const {return hide_ == Hide::None;}
    Hide hidden() const {return hide_;}
    Mode mode() const {return mode_;}
    uint8_t ctl() const {return ctl_;}
    uint8_t break_ctl() const {return brk_ctl_;}   // the last break's painted control (kNoCtl for a gesture without icon)
    const Face& face() const {return face_;}
    double now() const {return now_;}
    unsigned rings() const {return rings_;}
    unsigned flashes() const {return flashes_;}
    unsigned misses() const {return misses_;}
    unsigned locked() const {return locked_;}
    unsigned frames() const {return frames_;}
    double fill() const {return fill_;}           // the hold's progress 0..1 (0 outside a hold or before the press)
    uint8_t side() const {return side_;}          // the Lateral's side (kSideNone otherwise)
    const TrackView& track() const {return shown_;} // cand2: the timing check drawn now (on = false: none)
    // cand3: the timing check's needle has not reached its zone (the key dimmed, the offer's text WAIT)
    bool Waiting() const {
        return shown_.on && shown_.needle < shown_.zone0 && (mode_ == Mode::Timing || (mode_ == Mode::Lateral && ctl_ == sd::kKnife));
    }
    // 8 Hz from the stretch's start: down on the odd half beats.
    bool Down() const {return mode_ == Mode::Mash && std::fmod(now_ - phase_t0_ < 0 ? 0 : now_ - phase_t0_, 2 * kHalfBeatMs) >= kHalfBeatMs;}
    // The break ring's scale (1.9 in the warning, closing to 1.05 with the window); 0 outside the break.
    double RingScale() const {
        if (mode_ != Mode::Break && mode_ != Mode::Lateral) return 0;
        if (warning_) return kRingOpen;
        const double frac = window_ > 0 ? remaining_ / window_ : 0;
        return kRingClose + (kRingOpen - kRingClose) * Clamp01(frac);
    }
    View Raw(int unit, const Geometry& g = Geometry{}) const {   // the unscaled unit (H1d..cand4 geometry)
        View v;
        if (unit < 0 || unit >= kUnits || hide_ != Hide::None) return v;
        v.tex_id = kTex[unit];
        const float big = ctl_ == sd::kAim ? kAimScale : 1.f;   // cand3: the mouse face larger (Guard read as "right")
        const float kw = face_.w * big, kh = face_.h * big, side = (kw > kh ? kw : kh) + kRingPad;
        const bool hold = mode_ == Mode::Hold, lateral = mode_ == Mode::Lateral;
        const bool down = Down() || hold;   // a hold's key sits pressed from the start
        float dx = 0;
        if (mode_ == Mode::Miss && now_ - fb_t0_ < kShakeMs)
            dx = float(kShakeAmp * std::sin(2 * 3.14159265358979323846 * (now_ - fb_t0_) / kShakePeriodMs));
        double key_s = 1.0, key_dy = 0;
        if ((mode_ == Mode::Mash || hold) && down) {key_s = kDownScale; key_dy = -kDownDrop;}
        if ((mode_ == Mode::Break || lateral) && now_ - phase_t0_ < kPopMs) key_s = 1.0 + kPopScale * (1.0 - (now_ - phase_t0_) / kPopMs);
        const float kx = g.x + dx, ky = float(g.y + key_dy);
        if (pair_ && mode_ == Mode::Mash) return PairUnit(unit, g, v, dx, key_s, key_dy, down);
        if (shown_.on && (mode_ == Mode::Timing || mode_ == Mode::Hit || mode_ == Mode::Miss || lateral)) {
            const bool own = unit == kGlow || unit == kRing || unit == kFlash || (unit == kArrow && !(lateral && ctl_ != sd::kKnife));
            if (own) return TrackUnit(unit, g, kh);   // the key, its shadow, a hit's flash and the Lateral's direction arrow: below
        }
        switch (unit) {
        case kGlow: {
            if (hold) {   // the empty track: the soft glow at a fixed size (never a tap's closing ring), red near the end
                v.visible = true; v.x = g.x; v.y = g.y; v.w = v.h = float(side * kHoldTrack);
                v.rgba[0] = 170; v.rgba[1] = 210; v.rgba[2] = 255;
                const double frac = window_ > 0 ? remaining_ / window_ : 0;
                if (!warning_ && fill_ <= 0 && frac < 0.3) {const double k = 1.0 - frac / 0.3; v.rgba[0] = 255; v.rgba[1] = Byte(210 - 130 * k); v.rgba[2] = Byte(255 - 195 * k);}
                const double ramp = kHoldWarnFloor + (1.0 - kHoldWarnFloor) * Clamp01((now_ - phase_t0_) / warn_ms_);
                v.rgba[3] = Byte(warning_ ? kHoldTrackAlpha * ramp : kHoldTrackAlpha);
                return v;
            }
            v.visible = true; v.x = g.x; v.y = g.y; v.w = kw * 1.35f + 24.f; v.h = kh * 1.9f;
            const uint8_t gold[3] = {255, 210, 110}, blue[3] = {170, 210, 255}, red[3] = {255, 60, 50};
            const uint8_t* c = mode_ == Mode::Miss ? red : mode_ == Mode::Break || lateral ? blue : gold;
            v.rgba[0] = c[0]; v.rgba[1] = c[1]; v.rgba[2] = c[2];
            double a = mode_ == Mode::Mash ? (down ? 150 : 110) : mode_ == Mode::Hit ? 160 : 120;
            if ((mode_ == Mode::Break || lateral) && warning_) a *= Clamp01((now_ - phase_t0_) / warn_ms_);
            v.rgba[3] = Byte(a);
            return v;
        }
        case kRing: {
            double s = 0, a = 0;
            uint8_t c[3] = {255, 255, 255};
            if (mode_ == Mode::Mash && ring_t0_ >= 0 && now_ - ring_t0_ < kRingMs) {
                const double t = (now_ - ring_t0_) / kRingMs;
                s = 1.0 + kRingGrow * t; a = 190 * (1.0 - t);
                v.w = float((kw + kRingPad) * s); v.h = float((kh + kRingPad) * s);
            } else if (hold) {   // the fill: the disc grows gold over the track, nothing before the press, full at the hold's length
                s = kHoldFill0 + (kHoldTrack - kHoldFill0) * fill_; a = fill_ > 0 ? 150 + 90 * fill_ : 0;
                c[0] = 255; c[1] = 214; c[2] = 120;
                v.w = v.h = float(side * s);
            } else if (mode_ == Mode::Break || lateral) {
                s = RingScale();
                a = warning_ ? 170 * Clamp01((now_ - phase_t0_) / warn_ms_) : 190;
                const double frac = window_ > 0 ? remaining_ / window_ : 0;
                if (!warning_ && frac < 0.3) {const double k = 1.0 - frac / 0.3; c[1] = Byte(255 - 135 * k); c[2] = Byte(255 - 175 * k);}
                v.w = v.h = float(side * s);
            }
            if (s <= 0 || a < 1) return View{};
            v.visible = true; v.x = g.x; v.y = g.y; v.rgba[0] = c[0]; v.rgba[1] = c[1]; v.rgba[2] = c[2]; v.rgba[3] = Byte(a);
            return v;
        }
        case kShadow:
            v.visible = true; std::memcpy(v.uv, face_.uv, sizeof(v.uv));
            v.x = kx; v.y = g.y - kShadowDrop; v.w = kw; v.h = kh;
            v.rgba[0] = v.rgba[1] = v.rgba[2] = 0; v.rgba[3] = Byte(down ? 170 : 110);
            return v;
        case kKey: {
            v.visible = true; std::memcpy(v.uv, face_.uv, sizeof(v.uv));
            v.x = kx; v.y = ky; v.w = float(kw * key_s); v.h = float(kh * key_s);
            const bool locked = mode_ == Mode::Mash && lock_t0_ >= 0 && now_ - lock_t0_ < kLockTintMs;
            if (mode_ == Mode::Hit) {v.rgba[0] = 255; v.rgba[1] = 225; v.rgba[2] = 120;}
            else if (mode_ == Mode::Miss) {v.rgba[0] = 255; v.rgba[1] = 70; v.rgba[2] = 60;}
            else if (Waiting()) {v.rgba[0] = v.rgba[1] = v.rgba[2] = kWaitShade; v.rgba[3] = kWaitAlpha;}   // cand3: WAIT, not yet
            else if (locked) {v.rgba[0] = 255; v.rgba[1] = 90; v.rgba[2] = 80;}
            return v;
        }
        case kFlash: {
            if (mode_ != Mode::Hit || now_ - fb_t0_ >= kFlashMs) return View{};
            const double t = (now_ - fb_t0_) / kFlashMs;
            v.visible = true; v.x = g.x; v.y = g.y; v.w = v.h = float(side * (1.2 + 0.8 * t));
            v.rgba[0] = 255; v.rgba[1] = 240; v.rgba[2] = 190; v.rgba[3] = Byte(255 * (1.0 - t));
            return v;
        }
        default:   // kArrow: the mash's, bouncing over the key with the beat; the Lateral's, turned to its side
            if (lateral && (side_ == sd::kSideLeft || side_ == sd::kSideRight)) {
                const float dir = side_ == sd::kSideLeft ? -1.f : 1.f;
                const double ph = std::fmod(now_ - phase_t0_ < 0 ? 0 : now_ - phase_t0_, kNudgeMs) / kNudgeMs;
                v.visible = true; v.rot = ArrowTurn(side_);
                v.x = g.x + dir * float(kw * 0.5f + kArrowGap + kArrowSize * 0.5f + kNudge * ph); v.y = g.y;
                v.w = v.h = kArrowSize; v.rgba[3] = 230;
                return v;
            }
            if (mode_ != Mode::Mash) return View{};
            if (swing_) {   // cand2: the swing's arrow points to the side of the key asked now (A left, D right), so it reads
                            // as "left / right" before any press, never as the knife's hammering
                const float dir = ctl_ == sd::kLeft ? -1.f : 1.f;
                v.visible = true; v.rot = ArrowTurn(ctl_ == sd::kLeft ? sd::kSideLeft : sd::kSideRight);
                v.x = g.x + dir * float(kw * 0.5f + kArrowGap + kArrowSize * 0.5f + (down ? 3.f : 0.f)); v.y = g.y;
                v.w = v.h = kArrowSize; v.rgba[3] = 230;
                return v;
            }
            v.visible = true; v.x = g.x; v.y = g.y + kh * 0.5f + kArrowGap + (down ? 0.f : 3.f); v.w = v.h = kArrowSize;
            v.rgba[3] = 230;
            return v;
        }
    }
private:
    // The timing check's units (kGlow track, kRing zone, kFlash great part, kArrow needle; the key and its shadow are the
    // usual ones, returned not visible here so Get draws them).
    // Candidate 5 (Donald, 4-nov: "spamear a y d constantemente, y no solamente 1 tecla"): the Swing mash's two keys side by
    // side (A left, D right): the one asked next is the key unit, lit, pressed with the beat like a mash; the other one is
    // the shadow unit with the other face, dimmed; the arrow points outward from the lit key; the ring pulses around it on
    // each counted flank; the glow spans both keys.
    View PairUnit(int unit, const Geometry& g, View v, float dx, double key_s, double key_dy, bool down) const {
        const bool left_lit = ctl_ == sd::kLeft;
        const Face& lf = left_lit ? face_ : other_;
        const Face& rf = left_lit ? other_ : face_;
        const float xl = g.x - (lf.w * 0.5f + kPairGap * 0.5f), xr = g.x + (rf.w * 0.5f + kPairGap * 0.5f);
        const float lit_x = (left_lit ? xl : xr) + dx, lit_w = left_lit ? lf.w : rf.w, kh = face_.h > other_.h ? face_.h : other_.h;
        const float dir = left_lit ? -1.f : 1.f;
        switch (unit) {
        case kGlow:
            v.visible = true; v.x = g.x; v.y = g.y; v.w = (lf.w + rf.w + kPairGap) * 1.15f + 24.f; v.h = kh * 1.9f;
            v.rgba[0] = 255; v.rgba[1] = 210; v.rgba[2] = 110; v.rgba[3] = Byte(down ? 150 : 110);
            return v;
        case kRing:
            if (ring_t0_ < 0 || now_ - ring_t0_ >= kRingMs) return View{};
            {   const double t = (now_ - ring_t0_) / kRingMs, s = 1.0 + kRingGrow * t;
                v.visible = true; v.x = lit_x; v.y = g.y; v.w = float((lit_w + kRingPad) * s); v.h = float((kh + kRingPad) * s);
                v.rgba[3] = Byte(190 * (1.0 - t)); }
            return v;
        case kShadow:   // the other key, waiting its turn
            v.visible = true; std::memcpy(v.uv, (left_lit ? rf : lf).uv, sizeof(v.uv));
            v.x = left_lit ? xr : xl; v.y = g.y; v.w = left_lit ? rf.w : lf.w; v.h = left_lit ? rf.h : lf.h;
            v.rgba[0] = v.rgba[1] = v.rgba[2] = kPairDim; v.rgba[3] = kPairDimAlpha;
            return v;
        case kKey: {
            v.visible = true; std::memcpy(v.uv, face_.uv, sizeof(v.uv));
            v.x = lit_x; v.y = float(g.y + key_dy); v.w = float(face_.w * key_s); v.h = float(face_.h * key_s);
            const bool locked = lock_t0_ >= 0 && now_ - lock_t0_ < kLockTintMs;
            if (locked) {v.rgba[0] = 255; v.rgba[1] = 90; v.rgba[2] = 80;}
            return v;
        }
        case kArrow:
            v.visible = true; v.rot = ArrowTurn(left_lit ? sd::kSideLeft : sd::kSideRight);
            v.x = lit_x + dir * float(lit_w * 0.5f + kArrowGap + kArrowSize * 0.5f + (down ? 3.f : 0.f)); v.y = g.y;
            v.w = v.h = kArrowSize; v.rgba[3] = 230;
            return v;
        default: return View{};   // kFlash: a mash has none
        }
    }
    View TrackUnit(int unit, const Geometry& g, float kh) const {
        View v; v.tex_id = kTex[unit];
        const float ty = g.y - kh * 0.5f - kTrackGap, x0 = g.x - kTrackLen * 0.5f;
        const bool fb = mode_ == Mode::Hit || mode_ == Mode::Miss;
        const double ramp = warning_ && !fb ? 0.6 + 0.4 * Clamp01((now_ - phase_t0_) / warn_ms_) : 1.0;
        const double n = shown_.needle < 0 ? 0 : shown_.needle > 1 ? 1 : shown_.needle;
        const bool inside = n >= shown_.zone0 && n <= shown_.zone1;
        // the track's rectangles sample only the flat centre of their soft textures (the disc 0xF2 is 255 over its middle
        // 40%, the glow 0xE4 over its middle 6%): crisp edges, so the red ends and the zone's borders read at a glance
        if (unit == kRing || unit == kFlash) {v.uv[0] = v.uv[2] = 0.40f; v.uv[1] = v.uv[3] = 0.60f;}
        if (unit == kGlow) {v.uv[0] = v.uv[2] = 0.47f; v.uv[1] = v.uv[3] = 0.53f;}
        switch (unit) {
        case kRing:   // the whole bar, dim red: pressing here (before or after the zone) is a miss
            v.visible = true; v.x = g.x; v.y = ty; v.w = kTrackLen; v.h = kTrackH;
            v.rgba[0] = 150; v.rgba[1] = 40; v.rgba[2] = 40; v.rgba[3] = Byte(225 * ramp);
            if (Waiting() && !warning_) {   // cand6: WAIT, not yet: the red pulses (4 Hz) so an early press reads as wrong
                const double t = std::fmod(now_ - phase_t0_ < 0 ? 0 : now_ - phase_t0_, kWaitPulseMs) / kWaitPulseMs;
                const double k = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * t);
                v.rgba[0] = Byte(150 + 105 * k); v.rgba[1] = Byte(40 + 15 * k); v.rgba[2] = Byte(40 + 15 * k); v.rgba[3] = Byte(225 + 30 * k);
            }
            return v;
        case kFlash: {   // the press zone, bright over the red bar (green with the needle in it, gold on a hit, red on a miss)
            const double z0 = shown_.zone0, z1 = shown_.zone1;
            v.visible = z1 > z0; v.x = float(x0 + kTrackLen * (z0 + z1) * 0.5); v.y = ty;
            v.w = float(kTrackLen * (z1 - z0)); v.h = kZoneH;
            if (mode_ == Mode::Miss) {v.rgba[0] = 255; v.rgba[1] = 110; v.rgba[2] = 80;}
            else if (mode_ == Mode::Hit) {v.rgba[0] = 255; v.rgba[1] = 225; v.rgba[2] = 120;}
            else if (inside && !warning_) {v.rgba[0] = 120; v.rgba[1] = 255; v.rgba[2] = 120;}
            else {v.rgba[0] = 255; v.rgba[1] = 205; v.rgba[2] = 70;}
            v.rgba[3] = Byte(250 * ramp);
            return v;
        }
        case kGlow: {   // the great part: white, exactly its width at the zone's start, taller than the zone (tabs above, below)
            const double z0 = shown_.zone0, z1 = shown_.great1 > z0 ? shown_.great1 : z0;
            v.visible = z1 > z0; v.x = float(x0 + kTrackLen * (z0 + z1) * 0.5); v.y = ty;
            v.w = float(kTrackLen * (z1 - z0)); v.h = kZoneH * 2.f;
            v.rgba[0] = 255; v.rgba[1] = 255; v.rgba[2] = 235; v.rgba[3] = Byte(255 * ramp);
            return v;
        }
        case kArrow: {   // the needle, pointing down onto the bar, on top of everything
            v.visible = true; v.x = float(x0 + kTrackLen * n); v.y = ty + kZoneH * 0.5f + kNeedleSize * 0.5f;
            v.w = v.h = kNeedleSize; v.rgba[3] = 255;
            if (mode_ == Mode::Miss) {v.rgba[1] = 120; v.rgba[2] = 110;}
            return v;
        }
        default: return View{};
        }
    }
    Hide hide_ = Hide::Off;
    float scale_ = 1.f;   // candidate 5: SetScale
    bool pair_on_ = false, pair_ = false;   // candidate 5: SetSwingPair; the A/D pair drawn now
    Face other_;          // candidate 5: the pair's other key (A when D is asked, D when A is)
    Mode mode_ = Mode::None;
    sd::Phase phase_ = sd::Phase::Idle;
    uint8_t ctl_ = sd::kNoCtl, brk_ctl_ = sd::kNoCtl, fb_ctl_ = sd::kNoCtl;
    bool fb_ok_ = false, warning_ = false;
    double now_ = 0, window_ = 1000, remaining_ = 0, phase_t0_ = 0, ring_t0_ = -1, lock_t0_ = -1, fb_t0_ = -1;
    Face face_;
    double hold_ms_ = 0, fill_ = 0, warn_ms_ = sd::kWarnMs;
    uint8_t side_ = sd::kSideNone;
    TrackView track_, fb_track_, shown_;
    bool swing_ = false;
    unsigned rings_ = 0, flashes_ = 0, misses_ = 0, locked_ = 0, frames_ = 0;
};

// ---------------------------------------------------------------- IDSystem::set v2 table (ocarina_staff.h BuildTable)
constexpr size_t kEntry = staff::kEntry, kTableSize = 8 + kUnits * kEntry;
inline void BuildTable(uint8_t* t, const Geometry& g = Geometry{}) {
    std::memset(t, 0, kTableSize);
    std::memcpy(t, "2.00", 4);
    t[5] = uint8_t(kUnits);
    for (int i = 0; i < kUnits; ++i) {
        uint8_t* e = t + 8 + i * kEntry;
        e[0x03] = 0;                  // flags: set() ORs alive|move|visible itself
        e[0x04] = uint8_t(i);         // markNo
        e[0x05] = uint8_t(0x40 + i);  // unitNo
        e[0x07] = 0xFF;               // no parent
        e[0x0B] = kTex[i];            // the cockpit texture of this piece (set() copies it to +0x78, 0x24535C)
        staff::PutF(e + 0x14, g.x);
        staff::PutF(e + 0x18, g.y);
        staff::PutF(e + 0x50, 16.f);
        staff::PutF(e + 0x54, 16.f);
        e[0x58] = e[0x59] = e[0x5A] = 0xFF; e[0x5B] = 0;   // white, transparent until the first view
        e[0x6F] = layout::tex_flag_hold;                    // texNo is ours
    }
}

// ---------------------------------------------------------------- native painter (no texture of its own)
// Game thread. Native contract (the runtime: base + RVA, SEH around calls; the same calls as the staff's painter):
//   uint32_t base;  bool Read(uint32_t, void*, size_t);  bool Write(uint32_t, const void*, size_t);
//   void Set(const void* table);   // thiscall base+0x4449 on base+idsys(table, 0xFF, layout::id_class, 0x13, 5, 0)
//   void Kill();                   // thiscall base+0xCE9B on base+idsys(0xFF, layout::id_class)
//   uint32_t UnitPtr(uint8_t mark);// thiscall base+0x95F7 on base+idsys(mark, layout::id_class)
// Tick(arm): the units are created (hidden) as soon as the duel wants an icon, so the first prompt shows at once; they are
// shown only while the icon is visible and every cockpit texture is registered. Hide keeps them (a pause); Reset kills them
// (the end, an abandon). A set() that fails (the pool full) or a class left by someone else latches until Reset.
enum class PaintWhy : uint8_t {Idle, Hidden, Armed, Shown, NoTex, ClassTaken, SetFailed, Lost, Unreadable};
inline const char* PaintWhyName(PaintWhy w) {
    switch (w) {
    case PaintWhy::Idle: return "idle"; case PaintWhy::Hidden: return "hidden"; case PaintWhy::Armed: return "armed";
    case PaintWhy::Shown: return "shown"; case PaintWhy::NoTex: return "no_tex"; case PaintWhy::ClassTaken: return "class_taken";
    case PaintWhy::SetFailed: return "set_failed"; case PaintWhy::Lost: return "lost"; default: return "unreadable";
    }
}
inline bool Failure(PaintWhy w) {return w == PaintWhy::NoTex || w == PaintWhy::ClassTaken || w == PaintWhy::SetFailed;}
template <class Native> class Painter {
public:
    PaintWhy Tick(Native& n, const Icon& icon, bool arm, const Geometry& g = Geometry{}) {
        if (failed_) return PaintWhy::Idle;
        if (!arm) {if (live_) Kill(n); return PaintWhy::Idle;}
        for (uint8_t t : kTex) if (!Registered(n, t)) {missing_ = t; if (live_) Hide(n); return PaintWhy::NoTex;}
        missing_ = 0;
        if (live_) {
            for (int i = 0; i < kUnits; ++i) if (!Ours(n, unit_[i], uint8_t(i))) {   // some freed: the rest hidden, re-armed next tick
                HideUnits(n); live_ = false; visible_ = 0; return PaintWhy::Lost;
            }
        } else {
            bool taken = false;
            if (!ClassBit(n, taken)) return PaintWhy::Unreadable;
            if (taken) {   // our own class left by an earlier duel of this process is ours to clear
                for (int i = 0; i < kUnits && taken; ++i) if (Ours(n, n.UnitPtr(uint8_t(i)), uint8_t(i))) taken = false;
                if (taken) {failed_ = true; return PaintWhy::ClassTaken;}
                n.Kill();
            }
            BuildTable(table_, g);
            n.Set(table_);
            live_ = true; visible_ = 0;
            for (int i = 0; i < kUnits; ++i) {
                unit_[i] = n.UnitPtr(uint8_t(i));
                if (!Ours(n, unit_[i], uint8_t(i))) {Kill(n); failed_ = true; return PaintWhy::SetFailed;}
                uint8_t be = 0; n.Read(unit_[i] + layout::u_be_flag, &be, 1);
                be = uint8_t(be & ~layout::be_visible); n.Write(unit_[i] + layout::u_be_flag, &be, 1);
            }
            return PaintWhy::Armed;   // shown from the next tick, after one native IdSys.move built their matrices
        }
        if (!icon.Visible()) {Hide(n); return PaintWhy::Hidden;}
        unsigned shown = 0;
        for (int i = 0; i < kUnits; ++i) {
            const View v = icon.Get(i, g);
            const uint32_t u = unit_[i];
            uint8_t be = 0;
            if (!n.Read(u + layout::u_be_flag, &be, 1)) return PaintWhy::Unreadable;
            if (v.visible) {
                const float pos[3] = {v.x, v.y, 0.0f};
                n.Write(u + layout::u_tex_no, &v.tex_no, 1);
                n.Write(u + layout::u_pos0, pos, sizeof(pos));
                n.Write(u + layout::u_size_w, &v.w, 4);
                n.Write(u + layout::u_size_h, &v.h, 4);
                n.Write(u + layout::u_col0, v.rgba, 4);
                n.Write(u + layout::u_uv, v.uv, sizeof(v.uv));
                n.Write(u + layout::u_rot0_z, &v.rot, 4);
                be = uint8_t(be | layout::be_visible); ++shown;
            } else {
                be = uint8_t(be & ~layout::be_visible);
            }
            n.Write(u + layout::u_be_flag, &be, 1);
        }
        visible_ = shown;
        return PaintWhy::Shown;
    }
    // Visible bits off, units kept (a pause, a texture gone).
    void Hide(Native& n) {
        if (!live_) return;
        HideUnits(n);
        visible_ = 0;
    }
    // The end of the duel: the units go back to the pool, also the survivors of a partial loss (Lost leaves live_ false);
    // a class 0x3E held by someone else (no unit of ours in it) is never killed. The next duel retries a failure.
    void Reset(Native& n) {
        if (live_) Kill(n);
        else if (Leftover(n)) Kill(n);
        failed_ = false; missing_ = 0;
    }
    bool Live() const {return live_;}
    bool failed() const {return failed_;}
    unsigned visible() const {return visible_;}
    uint8_t missing() const {return missing_;}
    const uint8_t* Table() const {return table_;}
    uint32_t unit(int i) const {return i >= 0 && i < kUnits ? unit_[i] : 0;}
    // TEX_WK[id]: owner +0x50 and TPL +0x48 non-zero (GetTexWk's test, then what the draw reads).
    static bool Registered(Native& n, uint8_t tex) {
        uint32_t sys = 0, owner = 0, tpl = 0;
        if (!n.Read(n.base + layout::tex_sys_ptr_rva, &sys, 4) || !sys) return false;
        const uint32_t wk = sys + uint32_t(tex) * layout::tex_wk_size;
        return n.Read(wk + layout::tex_wk_owner, &owner, 4) && owner && n.Read(wk + layout::tex_wk_tpl, &tpl, 4) && tpl;
    }
private:
    uint8_t table_[kTableSize] = {};
    uint32_t unit_[kUnits] = {};
    bool live_ = false, failed_ = false;
    unsigned visible_ = 0;
    uint8_t missing_ = 0;
    void Kill(Native& n) {n.Kill(); live_ = false; visible_ = 0; std::memset(unit_, 0, sizeof(unit_));}
    void HideUnits(Native& n) {   // only the units still ours: a unit of the class someone else took is never written
        for (int i = 0; i < kUnits; ++i) {
            if (!Ours(n, unit_[i], uint8_t(i))) continue;
            uint8_t be = 0;
            if (n.Read(unit_[i] + layout::u_be_flag, &be, 1)) {be = uint8_t(be & ~layout::be_visible); n.Write(unit_[i] + layout::u_be_flag, &be, 1);}
        }
    }
    bool Leftover(Native& n) {   // Tick's rule for a class left by an earlier duel: the bit set and one of our marks in it
        bool taken = false;
        if (!ClassBit(n, taken) || !taken) return false;
        for (int i = 0; i < kUnits; ++i) if (Ours(n, n.UnitPtr(uint8_t(i)), uint8_t(i))) return true;
        return false;
    }
    bool ClassBit(Native& n, bool& on) {
        uint32_t w = 0;
        if (!n.Read(n.base + layout::idsys_rva + layout::idsys_set_bits + (layout::id_class >> 5) * 4, &w, 4)) return false;
        on = (w & (0x80000000u >> (layout::id_class & 31))) != 0; return true;
    }
    bool Ours(Native& n, uint32_t u, uint8_t mark) {
        uint8_t h[4] = {};
        return u && u != n.base + layout::unit_dummy_rva && n.Read(u, h, 4) && h[0] != layout::be_free &&
               h[2] == layout::id_class && h[3] == mark;
    }
};
}  // namespace link_runtime::sword_lock::icons

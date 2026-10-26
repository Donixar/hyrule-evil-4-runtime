// dll/save_anywhere.h
#pragma once
// "Serenity" effect: open the native SAVE screen from gameplay (Donald, 12-oct-2026;
// docs/ocarina-serenity-2026-10-12.md). Attribution and every pin: scripts/save_anywhere_pins.py.
//
// The typewriter (SCE_AT area type 8, sceAtFunc_save 0x2B3D00) only calls CardSave(term_no, 1)
// (thunk 0x279D -> 0x5641C0, cdecl) from ActBtn.move inside gameMainLoop, on the game task.
// CardSave writes pG->terminal_no, starts CardMainTask in task slot 1 and TaskSleep(1)s the caller.
// The card task suspends the game task, backs up Disp_flg/Stop_flg, runs the save screen, restores
// both, signals the game task and fades in: play continues in the same room and frame. No ink ribbon,
// item or difficulty test exists on that path; its only refusal is "Ashley caught".
// cGameSave::save copies pPL->pos/ang.y into pG->pl_pos/pl_ang_y when Rno0 == 3, and player init
// spawns at pl_pos: a load (and a continue) resumes where Link stood, not at the room entry.
// We call the same thunk with (0, 1): terminal 0 is what the chapter-end and ending saves pass.
//
// Rules (Allowed), fail closed on any unreadable word:
//  - pins ok (checked once at start-up by the runtime, relocation-aware);
//  - ordinary gameplay: link_runtime::ReadHealth eligible (MainLoop Rno0 3, Link, no movie/cut/
//    death/case/event/blackout/suspend/load, no SPF_KEY/PL/CTRL) and character byte 0;
//  - no action-button lock (Stop_flg SPF_ACTBTN 0x100), partner not caught (Status2 0x20000000),
//    sub-screen not requested (Status2 0x04000000), card not running (Status3 0x800);
//  - Link idle: R0 == 0 and R1 == 0 (pl+0xFC/+0xFD), not using binoculars (pl+0x468 & 4);
//  - task slot 1 free (death demo and card share it) and the current task is the game task (slot 0);
//  - enemies (relaxed default, Donald decides later): no live ACTIVE enemy (hp > 0, status +0x3CC
//    bit 5) within Rules::radius of Link, and no Ganado (ids 0x10-0x17, 0x19-0x20, the em10 library)
//    with em10 Be_flg (+0x4B0) bit 0x100 "found Link" (cEm10::setFindPL 0x33B70). Ids below 0x10
//    (partners) and 0x18 (Merchant) are not enemies. Ganado ids/Be_flg come from the GC decomp
//    (include/em10.h) plus the UHD setFindPL pin; the 10 m radius is a proposal.
// Open must be the last thing the caller does on that game frame: CardSave returns after the menu
// closes (the game task resumes from TaskSleep), as the typewriter's ActBtn.move does.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "link_runtime_health.h"
#include "native_pins.h"

namespace link_runtime::save_anywhere {
namespace rva {
constexpr uint32_t kCardSaveThunk = 0x279D;
constexpr uint32_t kGlobalSlot = 0x806F3C, kPlayerSlot = 0x857054, kPartnerSlot = 0x857060;
constexpr uint32_t kEmMgr = 0x7FDB04;           // cEmMgr object: active head [+0x14], next [em+0x08]
constexpr uint32_t kTaskArray = 0x867D40, kCurrentTask = 0x867D3C;
constexpr uint32_t kTaskStride = 0x348, kTaskStatus = 0x04;
}
namespace off {
constexpr uint32_t kRno0 = 0x20, kStopFlg = 0x170, kCharacter = 0x4FC8;
constexpr uint32_t kStatus2 = 0x5024, kStatus3 = 0x5028;
constexpr uint32_t kPlRoutine = 0xFC, kPlFlags468 = 0x468, kPos = 0x94;
constexpr uint32_t kEmHead = 0x14, kEmNext = 0x08, kEmId = 0x100, kEmHp = 0x324, kEmStatus = 0x3CC, kEm10BeFlg = 0x4B0;
}
constexpr uint32_t kSpfActBtn = 0x100, kPartnerCaught = 0x20000000, kSscrnRequest = 0x04000000, kCardRunning = 0x800;
constexpr uint32_t kEmActive = 0x20, kFoundPl = 0x100;
constexpr unsigned kMaxEnemies = 256;

// The 48 relocation-aware pins live in native_pins.h (kSaveAnywhere; equal to scripts/save_anywhere_pins.py PINS,
// tests/test_save_anywhere_header.py).
inline constexpr const auto& kPins = native_pins::kSaveAnywhere;
template <class Mem> bool PinsOk(Mem& m, uint32_t base) { return native_pins::All(m, base, kPins); }

enum class Block { None, Unreadable, NotGameplay, ActionLock, Partner, SubScreen, CardRunning, PlayerBusy,
                   Binoculars, TaskBusy, NotGameTask, EnemyNear, EnemyAware };
inline const char* Why(Block b) {
    switch (b) {
    case Block::None: return "save allowed";
    case Block::Unreadable: return "serenity blocked: unreadable";
    case Block::NotGameplay: return "serenity blocked: not gameplay";
    case Block::ActionLock: return "serenity blocked: action lock";
    case Block::Partner: return "serenity blocked: partner caught";
    case Block::SubScreen: return "serenity blocked: sub screen";
    case Block::CardRunning: return "serenity blocked: card running";
    case Block::PlayerBusy: return "serenity blocked: Link not idle";
    case Block::Binoculars: return "serenity blocked: binoculars";
    case Block::TaskBusy: return "serenity blocked: task slot 1";
    case Block::NotGameTask: return "serenity blocked: not game task";
    case Block::EnemyNear: return "serenity blocked: enemy near";
    case Block::EnemyAware: return "serenity blocked: enemy aware";
    }
    return "serenity blocked";
}
// The same blocks for the combat songs (Sheik, Epona; ocarina_song_effects.h), which never count enemies.
inline const char* WhySong(Block b) {
    switch (b) {
    case Block::None: return "song allowed";
    case Block::Unreadable: return "song blocked: unreadable";
    case Block::NotGameplay: return "song blocked: not gameplay";
    case Block::ActionLock: return "song blocked: action lock";
    case Block::Partner: return "song blocked: partner caught";
    case Block::SubScreen: return "song blocked: sub screen";
    case Block::CardRunning: return "song blocked: card running";
    case Block::PlayerBusy: return "song blocked: Link busy";
    case Block::Binoculars: return "song blocked: binoculars";
    case Block::TaskBusy: return "song blocked: task slot 1";
    case Block::NotGameTask: return "song blocked: not game task";
    case Block::EnemyNear: case Block::EnemyAware: return "song blocked: enemy";
    }
    return "song blocked";
}
// waive_player: skip only the routine check (Serenity while the ocarina pose is still out); every other
// rule, enemies included, still applies.
// Combat songs (17-oct): enemies=false skips the enemy scan (they are the point of the song) and
// player_r0_only relaxes the routine check to R0 == 0 (Link standing, walking or running, not hit,
// grabbed or in an action); every other gameplay rule still applies.
struct Rules { float radius = 10000.0f; bool aware = true; bool waive_player = false; bool enemies = true; bool player_r0_only = false; };   // 10 m (RE4 units are mm)
struct Result { Block block = Block::Unreadable; uint32_t enemy = 0; uint8_t enemy_id = 0; float distance2 = 0; };

inline bool IsGanado(uint8_t id) { return (id >= 0x10 && id <= 0x17) || (id >= 0x19 && id <= 0x20); }
// 0x51: a live, moving object near the r105 church path that blocked Serenity (PID 36944); the GC decomp
// files 0x40-0x51 mostly under scenery. Donald chose to stop counting it as an enemy (15-oct).
// Non-hostile ems (GC decomp module headers, re4-decomp/src/emXX): 0x21 the village dog (Wolf Link), 0x23 crows,
// 0x26 cows, 0x27 lake fish, 0x28 chickens, 0x2A traps (bear trap, tripwire bombs: static, they never chase).
// 18-oct, New Game 1-1: a chicken (code=40 = 0x28) at 8.67 m (b=0x21E1 mm) blocked Serenity. Snakes (0x24),
// bats (0x29), the head parasite (0x25) and the wolves (0x22, Wolfos) bite or dash at Link: still enemies.
inline bool IsHarmless(uint8_t id) { return id == 0x21 || id == 0x23 || id == 0x26 || id == 0x27 || id == 0x28 || id == 0x2A; }
inline bool IsEnemy(uint8_t id) { return id >= 0x10 && id != 0x18 && id != 0x51 && !IsHarmless(id); }

template <class Mem> struct Words {
    Mem& m;
    bool Read32(uint32_t a, uint32_t& v) { return a && m.Read(a, &v, 4); }
    bool Pos(uint32_t a, float (&p)[3]) { return a && m.Read(a, p, sizeof(p)); }
};

// Mem: bool Read(uint32_t address, void* out, size_t n) (SEH-guarded in the runtime). base: bio4.exe.
template <class Mem> Result Allowed(Mem& mem, uint32_t base, const Rules& rules = {}) {
    Result r; Words<Mem> w{mem};
    const uint32_t gslot = base + rva::kGlobalSlot, pslot = base + rva::kPlayerSlot;
    uint32_t g = 0, pl = 0, rno = 0, ch = 0, stop = 0, st2 = 0, st3 = 0, routine = 0, f468 = 0, task = 0, cur = 0;
    if (!w.Read32(gslot, g) || !w.Read32(pslot, pl) || !IsPointer(g) || !IsPointer(pl)) return r;
    if (!w.Read32(g + off::kRno0, rno) || !w.Read32(g + off::kCharacter, ch) || !w.Read32(g + off::kStopFlg, stop) ||
        !w.Read32(g + off::kStatus2, st2) || !w.Read32(g + off::kStatus3, st3) || !w.Read32(pl + off::kPlRoutine, routine) ||
        !w.Read32(pl + off::kPlFlags468, f468) ||
        !w.Read32(base + rva::kTaskArray + rva::kTaskStride + rva::kTaskStatus, task) ||
        !w.Read32(base + rva::kCurrentTask, cur)) return r;
    HealthState health;
    if (!ReadHealth(w, pslot, gslot, health) || !health.eligible || health.player != pl || (rno & 0xFF) != 3 || (ch & 0xFF) != 0)
        { r.block = Block::NotGameplay; return r; }
    if (stop & kSpfActBtn) { r.block = Block::ActionLock; return r; }
    if (st2 & kPartnerCaught) { r.block = Block::Partner; return r; }
    if (st2 & kSscrnRequest) { r.block = Block::SubScreen; return r; }
    if (st3 & kCardRunning) { r.block = Block::CardRunning; return r; }
    if (!rules.waive_player && (routine & (rules.player_r0_only ? 0xFFu : 0xFFFFu)) != 0) { r.block = Block::PlayerBusy; return r; }
    if (f468 & 4) { r.block = Block::Binoculars; return r; }
    if (task & 0xFF) { r.block = Block::TaskBusy; return r; }
    if (cur != base + rva::kTaskArray) { r.block = Block::NotGameTask; return r; }
    if (!rules.enemies) { r = {}; r.block = Block::None; return r; }
    float lp[3] = {};
    uint32_t partner = 0, em = 0;
    if (!w.Pos(pl + off::kPos, lp) || !w.Read32(base + rva::kPartnerSlot, partner) ||
        !w.Read32(base + rva::kEmMgr + off::kEmHead, em)) return r;
    const float r2 = rules.radius * rules.radius;
    for (unsigned n = 0; em; ++n) {
        if (n >= kMaxEnemies || !IsPointer(em)) { r.block = Block::Unreadable; return r; }
        uint32_t id = 0, hp = 0, status = 0, next = 0;
        if (!w.Read32(em + off::kEmId, id) || !w.Read32(em + off::kEmHp, hp) || !w.Read32(em + off::kEmStatus, status) ||
            !w.Read32(em + off::kEmNext, next)) { r.block = Block::Unreadable; return r; }
        const uint8_t eid = uint8_t(id);
        if (em != partner && IsEnemy(eid) && int16_t(hp & 0xFFFF) > 0 && (status & kEmActive)) {
            float ep[3] = {};
            if (!w.Pos(em + off::kPos, ep)) { r.block = Block::Unreadable; return r; }
            const float dx = ep[0] - lp[0], dy = ep[1] - lp[1], dz = ep[2] - lp[2], d2 = dx * dx + dy * dy + dz * dz;
            r.enemy = em; r.enemy_id = eid; r.distance2 = d2;
            if (!(d2 >= r2)) { r.block = Block::EnemyNear; return r; }   // NaN blocks too
            if (rules.aware && IsGanado(eid)) {
                uint32_t be = 0;
                if (!w.Read32(em + off::kEm10BeFlg, be)) { r.block = Block::Unreadable; return r; }
                if (be & kFoundPl) { r.block = Block::EnemyAware; return r; }
            }
        }
        em = next;
    }
    r = {}; r.block = Block::None;
    return r;
}

// The native call; the runtime wraps it in SEH. Returns after the save screen closes.
using CardSaveFn = void(__cdecl*)(int terminal_no, int flags);
inline CardSaveFn CardSave(uint32_t base) { return reinterpret_cast<CardSaveFn>(uintptr_t(base + rva::kCardSaveThunk)); }
constexpr int kTerminal = 0, kFlags = 1;
}  // namespace link_runtime::save_anywhere

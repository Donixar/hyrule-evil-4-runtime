// dll/fairy_revive.h
#pragma once
// The fairy revives Link (Donald, 12-oct-2026; docs/fairy-revive-2026-10-12.md). Item 0x05 (ex First
// Aid Spray, the fairy in a bottle, scripts/text_rename.py) saves Link from a lethal HP loss once:
// one fairy is consumed through the native cItemMgr::use path (its own case: healing(2400), num-1,
// slot freed at 0) and HP ends at the maximum.
//
// Where: a MinHook detour on the entry of LifeDownSet2 (body RVA 0x1B5AF0, thunk 0x71C1, 91 calls,
// all through the thunk), the only routine that subtracts the player's HP for ordinary damage. The
// detour calls the original and then looks at the result: the HP word was > 0 before and is 0 after,
// the native return value is <= 0. Only that case is touched, and only by returning the revived HP,
// so every caller takes its own "survived" branch (PlSetDamage plays a normal damage reaction instead
// of the 6/7 death types or routine 2; grab routines such as 0x92A7A/0xBC318/em36 choose their
// non-lethal motion). It is the point where the game itself keeps the player alive (UHD's
// "restore from max" tail at 0x1B5CBA; GC's DBG_NO_DEATH at em_sub.cpp:1931).
//
// What the fairy does NOT save (native death, no fairy spent), by design:
//  - deaths that never pass a lethal LifeDownSet2: direct pl_life = 0 writes (chainsaw decapitation,
//    QTE failures, boulders, trolley/gondola, falls, drowning, keep-1-then-zero grabs like 0x9D525)
//    and every Ashley/partner death or game over (other LifeDownSet2 branch, or not LifeDownSet2);
//  - damage >= 9999 (kInstantKill), the game's instant-kill sentinel: El Gigante's stamp
//    (em2b.cpp:5847 -> SetPlDamage(stamp) scripted crush), 0x176962, 0x35EFAD, 0x35F101;
//  - attacks whose ATK_INFO has flag bit 3: EmAtkHitCk (return 0x1BDE62) and EmAtkSetDamagePL
//    (return 0x1B62A9) zero HP right after LifeDownSet2 returns, so a revive would only waste a fairy;
//    the same for the one caller that zeroes HP after the call whatever HP is left (return 0x11A359,
//    a scripted kill: LifeDownSet2(10) then HP = 0 when its em flag is set);
//  - anything outside ordinary play, by the fairy's own gate before the call (FairyGate below:
//    MainLoop, Link as player (character 0), HP > 0, no loading/transition/continue/screen stop,
//    no SPF_PL/SPF_CTRL stop, no movie/cut/die-demo/sub screen/event/blackout, no suspend/item get,
//    no screen request/event system/title, no divided event/now loading/typewriter). Unlike
//    shared ReadHealth (unchanged; LowHealth now has its own read-only view) it lets through STA_INTO_SHOP, which
//    stays set through r11c's fights (two fairies refused on 19-oct), and SPF_KEY (key-locked
//    player states such as grabs); damage, knockdown and grab states set none of the bits kept.
// After a revive Link gets a short grace, as TP's fairy (docs/fairy-revive-fix-2026-10-19.md): for
// kGraceFrames60 game frames (60 Hz units from the tick counter pG+0x530C and deltaTime pG+0x70, so the
// same game time at 30, 60 or 165 FPS) every ordinary hit that lowers his HP is undone (HP back to its
// entry value, no fairy spent). Why: native damage windows are tested once per logic tick, so at a high
// framerate one blow lands several times (El Gigante's grab, em2b r1 0x1A -> routine 0xB5B40 state 9:
// LifeDownSet2(800) for every tick with the motion frame in (86.7, 87.3], 3-4 ticks at 165 FPS; r119,
// 19-oct: 1380 -> 180 -> revive -> 180 -> death within 4 ticks). The exclusions below stay native in
// the grace too; outside it everything is as before.
// Hits the parry cancels never reach LifeDownSet2 (tier 1/2 cancel at 0x1BC4A6), so they never
// spend a fairy.
//
// Pure: every native access goes through the injected Host (game thread only):
//   bool Enabled();
//   bool Read(uint32_t address, void* out, size_t n);         // SEH-guarded copy
//   bool Write(uint32_t address, const void* in, size_t n);   // SEH-guarded store
//   bool Search(uint16_t id, uint32_t& item);                 // ItemMgr.search via thunk 0xE791; item 0 = none
//   bool Use(uint32_t item, bool& used);                      // ItemMgr.use via thunk 0x519B (al)
//   void Note(const Event& event);                            // copy for the worker (log, sound)
// Reviver itself keeps only the grace state; it is used from the LifeDownSet2 detour only.
#include "game_clock.h"
#include "link_runtime_health.h"
#include "native_pins.h"
#include <cstddef>
#include <cstdint>

namespace link_runtime::fairy {
constexpr uint16_t kFairyItem = 0x05;
constexpr int32_t kInstantKill = 9999;
constexpr double kGraceFrames60 = 60.0;   // 1 s of game time after a revive
namespace rva {
constexpr uint32_t kLifeDownBody = 0x1B5AF0;   // MinHook target: 55 8B EC 53 56 (5 whole bytes)
constexpr uint32_t kLifeDownThunk = 0x71C1;    // e9 -> body; every caller uses it
constexpr uint32_t kSphereReturn = 0x1BDE62;   // EmAtkHitCk after call LifeDownSet2; then test [esi+0A],8
constexpr uint32_t kLineReturn = 0x1B62A9;     // EmAtkSetDamagePL after call LifeDownSet2; same flag test
constexpr uint32_t kZeroingReturn = 0x11A359;  // LifeDownSet2(pPL,10,0,ebx); then HP = 0 if [esi+28B]&bl
constexpr uint32_t kPlayerSlot = 0x857054;     // VA 0xC57054, pPL
constexpr uint32_t kGlobalSlot = 0x806F3C;     // VA 0xC06F3C, pG
constexpr uint32_t kItemMgr = 0x870FD4;        // VA 0xC70FD4 (ocarina_item.h kItemMgrVa)
constexpr uint32_t kToWhom = 0x870FE6;         // ItemMgr+0x12, read by healing() 0x3056F0: 0 Link, 1 Ashley
constexpr uint32_t kSearchThunk = 0xE791;      // cItemMgr::search(u16) thiscall ret 4
constexpr uint32_t kUseThunk = 0x519B;         // cItemMgr::use(cItem*) thiscall ret 4, al
}  // namespace rva
namespace off {
constexpr uint32_t kTargetId = 0x100;          // cEm id byte: 0 player branch of LifeDownSet2
constexpr uint32_t kHp = 0x4FB4, kHpMax = 0x4FB6, kCharacter = 0x4FC8, kTick = 0x530C;
constexpr uint32_t kDeltaTime = game_clock::kDeltaTimeOffset;   // float, 1/30 s units (0.5 at 60 FPS)
constexpr uint32_t kAtkPower = 0x08, kAtkFlag = 0x0A;   // ATK_INFO (emhit.h): u16 power, u16 flag
constexpr uint32_t kItemNum = 0x02, kItemFlags = 0x04;  // cItem: u16 num, u8 flags (bit0 alive)
// ATK_INFO* relative to the detour's return-address slot (MSVC _AddressOfReturnAddress()):
// EmAtkHitCk: ebp, ebx/esi/edi, 4 args, ret -> [ebp+8] = slot+0x28.
// EmAtkSetDamagePL: ebp, 0x20 locals, esi/edi, 4 args, ret -> [ebp+0xC] = slot+0x48.
constexpr uint32_t kSphereAtkSlot = 0x28, kLineAtkSlot = 0x48;
}  // namespace off

// Exact bytes at the preferred base (scripts/fairy_revive_pins.py regenerates and re-verifies them
// against bio4.exe SHA 19aed4af... and the mapped image bb59e550...). Windows skip, on purpose:
// LifeDownSet2 0x1B5B40 (re4_tweaks DBG_NO_DEATH2 FlagPatch) and 0x1B5B8C..0x1B5B92 (re4_tweaks
// NTSC armor patch) and 0x1B5C57..0x1B5C5D (ShieldPreClampObserve MinHook); EmAtkHitCk 0x1BDE20..25
// (shield MinHook / re4_tweaks lazy C3); EmAtkSetDamagePL 0x1B61A0..A2 (re4_tweaks lazy C3).
// Checked once, before this hook is created. cItemMgr::use's entry is included: any
// foreign hook there keeps the fairy off.
// The pins themselves live in native_pins.h as native_pins::kFairyRevive (with the other runtimes' pins).

enum class Caller : uint8_t { Other, Sphere, Line };
inline Caller ClassifyReturn(uint32_t return_rva) {
    return return_rva == rva::kSphereReturn ? Caller::Sphere : return_rva == rva::kLineReturn ? Caller::Line : Caller::Other;
}
enum class Verdict : uint8_t {
    Pass,           // not the player branch, disabled, or after-state unreadable: native result untouched
    NotLethal,      // HP still > 0 (or keep-1, or a trainer that skipped the subtraction)
    Gates,          // lethal, but not ordinary play before the hit (event, case, Ashley, ...): native death
    InstantKill,    // damage >= 9999: native death
    InstantAttack,  // ATK flag bit 3, or the zeroing caller: HP is set to 0 anyway: native death
    Unverified,     // sphere/line caller whose ATK could not be cross-checked: native death (fail closed)
    Grace,          // a hit inside the post-revive grace: HP back to its entry value, no fairy spent
    NoFairy,        // no item 0x05 in Link's case: native death
    UseFailed,      // native use() refused/faulted or HP still 0: native death
    Revived};       // one fairy used, HP = max
inline const char* Name(Verdict v) {
    static const char* const names[] = {"pass","not_lethal","gates","instant_kill","instant_attack",
                                        "unverified","grace","no_fairy","use_failed","revived"};
    return names[static_cast<unsigned>(v)];
}
struct Attack { bool read = false; uint32_t pointer = 0; uint16_t power = 0, flag = 0; };
// Why the fairy's gate refused (logged as gate=<name> bits=0x...). Masks are MSB-first GLOBAL_WK flags
// (re4_tweaks GameFlags.h): Flg_SYSTEM +0x54, Stop_flg +0x170, Status_flg[0..3] +0x501C.
enum class Gate : uint8_t { None, Unread, Routine, Character, Hp, System, Stop, Status0, Status1, Status2, Status3, Max };
inline const char* Name(Gate g) {
    static const char* const names[] = {"none","unread","routine","character","hp","system","stop",
                                        "status0","status1","status2","status3","max"};
    return names[static_cast<unsigned>(g)];
}
namespace gate {
// SYS_EXCEPTION, SOFT_RESET, DATA_READ, DOORDEMO, TRANS_STOP, CONTINUE, SSCRN_EXEC, SCREEN_STOP, LOAD_GAME.
constexpr uint32_t kSystem = 0x20000000U | 0x04000000U | 0x02000000U | 0x00200000U | 0x00100000U |
                             0x00080000U | 0x00008000U | 0x00000400U | 0x00000100U;
constexpr uint32_t kStop = 0x10000000U | 0x02000000U;   // SPF_PL, SPF_CTRL (SPF_KEY 0x80000000 allowed)
// STA_MOVIE_ON, CUTCHG, MOVIE2_ON, DIEDEMO, SUB_SCRN, EVENT, BLACKOUT.
constexpr uint32_t kStatus0 = 0x10000000U | 0x08000000U | 0x04000000U | 0x00100000U | 0x00040000U |
                              0x00001000U | 0x00000080U;
constexpr uint32_t kStatus1 = 0x10000000U | 0x00000002U;   // STA_SUSPEND, ITEM_GET
constexpr uint32_t kStatus2 = 0x04000000U | 0x00080000U | 0x00008000U;   // SSCRN_REQUEST, EVENT_SYSYTEM, TITLE
constexpr uint32_t kIntoShop = 0x00040000U;   // STA_INTO_SHOP (77): ReadHealth blocks it, the fairy does not
// STA_DIV_EVENT_SET, DIV_EVENT_ING, NOW_LOADING, SCREEN_STOP_RESERVE, TYPEWRITER.
constexpr uint32_t kStatus3 = 0x00008000U | 0x00004000U | 0x00002000U | 0x00001000U | 0x00000800U;
}  // namespace gate
struct GateResult { bool read = false; Gate gate = Gate::Unread; uint32_t bits = 0; uint32_t player = 0; int hp = 0, max_hp = 0; };
// The fairy's eligibility, read like ReadHealth (same fields, same consistency checks) but with its own masks.
template <class Memory>
GateResult FairyGate(Memory& memory, uint32_t player_slot, uint32_t global_slot) {
    GateResult r;
    uint32_t player = 0, global = 0;
    if (!memory.Read32(player_slot, player) || !IsPointer(player) ||
        !memory.Read32(global_slot, global) || !IsPointer(global)) return r;
    uint32_t routine = 0, system = 0, stop = 0, health = 0, character = 0, status[4] = {};
    if (!memory.Read32(global + 0x20, routine) || !memory.Read32(global + 0x54, system) ||
        !memory.Read32(global + 0x170, stop) || !memory.Read32(global + 0x4FB4, health) ||
        !memory.Read32(global + 0x4FC8, character)) return r;
    for (unsigned i = 0; i < 4; ++i)
        if (!memory.Read32(global + 0x501C + i * 4, status[i])) return r;
    uint32_t current_player = 0, current_global = 0;
    if (!memory.Read32(player_slot, current_player) || current_player != player ||
        !memory.Read32(global_slot, current_global) || current_global != global) return r;
    const int hp = static_cast<int16_t>(health & 0xFFFF), maximum = static_cast<int16_t>(health >> 16);
    if (maximum <= 0 || hp < 0 || hp > maximum) return r;
    r.read = true; r.player = player; r.hp = hp; r.max_hp = maximum;
    const auto refuse = [&r](Gate g, uint32_t bits) { r.gate = g; r.bits = bits; return r; };
    if ((routine & 0xFF) != 3) return refuse(Gate::Routine, routine);
    if ((character & 0xFF) != 0) return refuse(Gate::Character, character & 0xFF);
    if (hp <= 0) return refuse(Gate::Hp, 0);
    if (system & gate::kSystem) return refuse(Gate::System, system & gate::kSystem);
    if (stop & gate::kStop) return refuse(Gate::Stop, stop & gate::kStop);
    if (status[0] & gate::kStatus0) return refuse(Gate::Status0, status[0] & gate::kStatus0);
    if (status[1] & gate::kStatus1) return refuse(Gate::Status1, status[1] & gate::kStatus1);
    if (status[2] & gate::kStatus2) return refuse(Gate::Status2, status[2] & gate::kStatus2);
    if (status[3] & gate::kStatus3) return refuse(Gate::Status3, status[3] & gate::kStatus3);
    r.gate = Gate::None; return r;
}
struct Entry {
    bool player = false;     // em == *pPL and em+0x100 == 0
    bool gameplay = false;   // FairyGate eligible just before the call
    Gate gate = Gate::Unread; uint32_t gate_bits = 0;
    uint32_t em = 0, global = 0, tick = 0, return_rva = 0;
    int hp = 0, max_hp = 0;
    int32_t damage = 0, rnd = 0; uint32_t flag = 0;
    Caller caller = Caller::Other; Attack attack;
};
struct Exit { bool read = false; uint32_t global = 0, tick = 0; int hp = 0, max_hp = 0; float dt = game_clock::kDefaultDt; };
// The grace a revive opens: who, where and when (tick of the revive).
struct Grace { bool active = false; uint32_t tick = 0, em = 0, global = 0; };
// Game time since the revive, in 60 Hz frames; negative (counter reset or wrapped) ends the grace.
inline bool GraceTime(const Grace& g, const Exit& x) {
    const int32_t ticks = static_cast<int32_t>(x.tick - g.tick);
    return ticks >= 0 && !game_clock::Exceeded(double(ticks) * game_clock::Step60(x.dt), kGraceFrames60);
}
inline bool GraceOpen(const Grace& g, const Entry& e, const Exit& x) {
    return g.active && x.read && e.em == g.em && e.global == g.global && x.global == g.global && GraceTime(g, x);
}
struct Event {
    Verdict verdict = Verdict::Pass;
    int hp_before = 0, hp_after = 0, max_hp = 0;
    int32_t damage = 0;
    uint32_t return_rva = 0, tick = 0, item = 0;
    uint16_t atk_flag = 0, num_before = 0, num_after = 0;
    uint8_t to_whom = 0;
    bool consume_verified = false;
    Gate gate = Gate::None; uint32_t gate_bits = 0;   // verdict gates: which gate and the bits that blocked
};
// The decision without side effects (before looking for a fairy). A lethal result goes through every
// exclusion; a non-lethal HP loss is only considered inside the grace, and then any exclusion leaves
// it native (NotLethal).
inline Verdict Judge(const Entry& e, int32_t ret, const Exit& x, const Grace& grace) {
    if (!e.player) return Verdict::Pass;
    if (!x.read || x.global != e.global) return Verdict::Pass;
    const bool lethal = e.hp > 0 && x.hp <= 0 && ret <= 0;
    const bool open = GraceOpen(grace, e, x);
    if (!lethal && !(open && e.hp > 0 && x.hp < e.hp)) return Verdict::NotLethal;
    const auto native = [lethal](Verdict v) { return lethal ? v : Verdict::NotLethal; };
    if (!e.gameplay || x.max_hp <= 0 || x.max_hp != e.max_hp) return native(Verdict::Gates);   // see GateOf
    if (e.damage >= kInstantKill) return native(Verdict::InstantKill);
    if (e.return_rva == rva::kZeroingReturn) return native(Verdict::InstantAttack);
    if (e.caller != Caller::Other) {
        const Attack& a = e.attack;
        if (!a.read || a.power != static_cast<uint16_t>(e.damage) || e.rnd != 0 ||
            e.flag != ((a.flag & 4) ? 1U : 0U)) return native(Verdict::Unverified);
        if (a.flag & 8) return native(Verdict::InstantAttack);
    }
    if (open) return Verdict::Grace;
    return Verdict::Revived;   // candidate: Reviver::Leave still needs a fairy and a successful use()
}

// The gate a Gates verdict reports: the entry gate, or Max when max HP changed or vanished across the call.
inline Gate GateOf(const Entry& e) { return e.gameplay ? Gate::Max : e.gate; }

template <class Host> class Reviver {
    uint32_t base_ = 0;
    Grace grace_;
    template <class T> static bool Get(Host& h, uint32_t address, T& out) { return address && h.Read(address, &out, sizeof(T)); }
    struct Memory {   // FairyGate adapter
        Host& h;
        bool Read32(uint32_t address, uint32_t& value) { return Get(h, address, value); }
    };
    bool ReadExit(Host& h, Exit& x) const {
        uint32_t global = 0, health = 0, tick = 0;
        if (!Get(h, base_ + rva::kGlobalSlot, global) || !IsPointer(global) ||
            !Get(h, global + off::kHp, health) || !Get(h, global + off::kTick, tick)) return false;
        x.read = true; x.global = global; x.tick = tick;
        float dt = 0.f;   // optional: an unreadable deltaTime counts as 60 FPS (0.5)
        if (Get(h, global + off::kDeltaTime, dt)) x.dt = game_clock::ValidDt(dt);
        x.hp = static_cast<int16_t>(health & 0xFFFF); x.max_hp = static_cast<int16_t>(health >> 16);
        return true;
    }
    bool SetHp(Host& h, uint32_t global, int hp) {
        const int16_t value = static_cast<int16_t>(hp);
        return h.Write(global + off::kHp, &value, sizeof(value));
    }
public:
    // image_base: the mapped bio4.exe (any ASLR base); the RVAs above are added to it.
    void Reset(uint32_t image_base) { base_ = image_base; grace_ = {}; }
    uint32_t Base() const { return base_; }
    // Before the original. return_rva = return address - image base; ret_slot = the address of the
    // return address on the stack (the caller's frame starts right above it).
    Entry Enter(Host& h, uint32_t em, int32_t damage, int32_t rnd, uint32_t flag, uint32_t return_rva, uint32_t ret_slot) {
        Entry e; e.em = em; e.damage = damage; e.rnd = rnd; e.flag = flag; e.return_rva = return_rva;
        if (!base_ || !h.Enabled() || !IsPointer(em)) return e;
        uint8_t id = 0xFF; uint32_t player = 0, global = 0, tick = 0;
        if (!Get(h, em + off::kTargetId, id) || id != 0) return e;   // enemies and Ashley: untouched
        if (!Get(h, base_ + rva::kPlayerSlot, player) || player != em) return e;
        if (!Get(h, base_ + rva::kGlobalSlot, global) || !IsPointer(global)) return e;
        e.player = true; e.global = global;
        Memory memory{h};
        const GateResult g = FairyGate(memory, base_ + rva::kPlayerSlot, base_ + rva::kGlobalSlot);
        if (g.read && g.player == em) {
            e.gameplay = g.gate == Gate::None; e.gate = g.gate; e.gate_bits = g.bits; e.hp = g.hp; e.max_hp = g.max_hp;
        }
        if (Get(h, global + off::kTick, tick)) e.tick = tick;
        e.caller = ClassifyReturn(return_rva);
        if (e.caller != Caller::Other && ret_slot) {
            uint32_t atk = 0;
            const uint32_t slot = ret_slot + (e.caller == Caller::Sphere ? off::kSphereAtkSlot : off::kLineAtkSlot);
            if (Get(h, slot, atk) && IsPointer(atk) && Get(h, atk + off::kAtkPower, e.attack.power) &&
                Get(h, atk + off::kAtkFlag, e.attack.flag)) { e.attack.read = true; e.attack.pointer = atk; }
        }
        return e;
    }
    // After the original returned `ret` (remaining HP, sign-extended). Returns the value the caller sees.
    int32_t Leave(Host& h, const Entry& e, int32_t ret, Verdict* out = nullptr) {
        if (out) *out = Verdict::Pass;
        if (!e.player) return ret;
        Exit x; ReadExit(h, x);
        if (grace_.active && x.read && (x.global != grace_.global || !GraceTime(grace_, x))) grace_ = {};
        Verdict v = Judge(e, ret, x, grace_);
        Event ev; ev.damage = e.damage; ev.return_rva = e.return_rva; ev.tick = x.tick;
        ev.hp_before = e.hp; ev.hp_after = x.hp; ev.max_hp = x.max_hp; ev.atk_flag = e.attack.flag;
        if (v == Verdict::Gates) { ev.gate = GateOf(e); ev.gate_bits = e.gameplay ? 0 : e.gate_bits; }
        int32_t result = ret;
        if (v == Verdict::Grace) {   // the hit is undone: HP as it was when LifeDownSet2 was entered
            if (SetHp(h, e.global, e.hp)) { result = e.hp; ev.hp_after = e.hp; }
            else v = Verdict::UseFailed;
        } else if (v == Verdict::Revived) {
            v = Revive(h, e, x, ev, result);
            if (v == Verdict::Revived) grace_ = {true, x.tick, e.em, e.global};
        }
        ev.verdict = v;
        if (out) *out = v;
        if (v != Verdict::Pass && v != Verdict::NotLethal) h.Note(ev);
        return result;
    }
private:
    Verdict Revive(Host& h, const Entry& e, const Exit& x, Event& ev, int32_t& result) {
        uint32_t item = 0;
        if (!h.Search(kFairyItem, item) || !item) return Verdict::NoFairy;
        uint16_t id = 0, num = 0;
        if (!Get(h, item, id) || id != kFairyItem || !Get(h, item + off::kItemNum, num) || !num) return Verdict::NoFairy;
        ev.item = item; ev.num_before = num;
        // healing() serves whoever the case last targeted; the fairy is Link's. Force 0, restore after.
        uint8_t to_whom = 0;
        if (!Get(h, base_ + rva::kToWhom, to_whom)) return Verdict::UseFailed;
        ev.to_whom = to_whom;
        const uint8_t link = 0;
        if (to_whom != 0 && !h.Write(base_ + rva::kToWhom, &link, 1)) return Verdict::UseFailed;
        bool used = false;
        const bool called = h.Use(item, used);
        if (to_whom != 0) h.Write(base_ + rva::kToWhom, &to_whom, 1);
        Exit now;
        if (!called || !used || !ReadExit(h, now) || now.global != e.global || now.hp <= 0) return Verdict::UseFailed;
        int hp = now.hp;
        if (hp < now.max_hp && SetHp(h, e.global, now.max_hp)) hp = now.max_hp;   // max above 2400
        uint16_t after = 0; uint8_t flags = 0;
        if (Get(h, item + off::kItemNum, after) && Get(h, item + off::kItemFlags, flags)) {
            ev.num_after = after;
            ev.consume_verified = after == static_cast<uint16_t>(num - 1) && (after != 0 || (flags & 1) == 0);
        }
        ev.hp_after = hp; ev.max_hp = now.max_hp; (void)x;
        result = hp;
        return Verdict::Revived;
    }
};
}  // namespace link_runtime::fairy

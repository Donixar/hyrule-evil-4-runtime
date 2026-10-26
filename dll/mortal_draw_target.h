#pragma once
// Mortal Draw target eligibility: pure logic over an injected reader.
// No process access, hooks or writes live here. The reader supplies bytes;
// this header decides whether one enemy is a valid Mortal Draw target: back
// turned to Link (found or not) in a safe routine, or the parried owner while
// staggered (StaggerWindow). Rule of 12-oct: docs/mortal-draw-real-2026-10-12.md.
//
// Attribution (pinned bio4.exe SHA 19aed4af..., image SHA bb59e550...; all RVAs,
// base 0x400000). Evidence: D:/link-in-re4-work/mortal-draw-real-01 and
// docs/mortal-draw-real-2026-10-12.md. GC names are structural comparators only.
#include "mortal_draw_policy.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace link_runtime::mortal_draw::target {
namespace layout {
// cEmMgr: enumerator 0x491A0 reads base/count/block from these slots and
// requires (unit+4 & 0x201)==1. The cEm10 move 0x5B238 loads ECX=VA 0xBFDB04.
constexpr uint32_t manager_rva = 0x7FDB04;
constexpr uint32_t manager_array_rva = 0x7FDB08, manager_count_rva = 0x7FDB0C, manager_block_rva = 0x7FDB10;
// RTTI .?AVcEm10@@, ctor 0x7C7D8. Several constructors store it (0x7C7DA, 0x7C804,
// 0x7C834, 0x7D514, ...): it is the base-class vtable. Derived variants that
// overwrite it are rejected as NotEm10 (conservative; logged by the caller).
constexpr uint32_t em10_vtable_rva = 0x71035C;
constexpr uint32_t em_block_hint = 0xC70;        // fixtures only; runtime reads the manager
constexpr uint32_t be_flag = 0x004;              // valid when (&0x201)==1 (0x491CF)
constexpr uint32_t pos = 0x094, ang = 0x0A0;     // pos XYZ; ang.y at +0xA4 (0x492BC)
constexpr uint32_t routine = 0x0FC;              // r_no_0..3; R0 table 0x7F39B0 (0x5B220), R1 table 0x7F39C8 (0x2912E)
constexpr uint32_t id = 0x100, type = 0x101;     // ID classifier 0x1AF000; type6 in ckFindPL 0x33B30
constexpr uint32_t hp = 0x324, hp_max = 0x326;   // signed int16, ckFindPL / LifeDownSet2 0x1B5E91
constexpr uint32_t dmg_info = 0x328;             // cDmgInfo published by 0x1AEEF0
constexpr uint32_t dmg_flag = 0x32C, dmg_timer = 0x32D;  // nonzero forces find in 0x78050
constexpr uint32_t status = 0x3CC;               // checkStatus(5) 0x1AEE10 -> mask 0x20
constexpr uint32_t flag_3d0 = 0x3D0;             // &0x80 forces find in 0x78050
constexpr uint32_t be_flg = 0x4B0;               // &0x100 found player (setFindPL 0x33B70)
constexpr uint32_t p_shield = 0x618;             // ckShiled slot +0x70 -> 0x33C50
// Plaga state. +0x6BC is non-null only once the Plaga has emerged: its only
// non-null em10 writer is 0x552C7 (spawn 0x40C5C7 result) inside the emergence
// routine 0x55100 (HP>0, sets +0x3D0|0x20), called from the cEm10 move 0x5AE20
// at 0x5B851 when the countdown +0x74C runs out, and from 0x62350 (0x623C1).
// +0x6D4 is cleared and set by 0x4B270 (0x4B27B/0x4B2F0), called from the same
// routine (0x5515A). Other em10 writers store 0 / release (0x28ED6, 0x39DC3,
// 0x4B701, 0x4BAE1, 0x509CE, 0x5010A). A Ganado carrying a hidden Plaga has both
// pointers null and stays eligible: native Plaga emergence is preserved.
constexpr uint32_t p_core = 0x6BC;               // ckParasite slot +0x38 -> 0x33C20
constexpr uint32_t p_parasite = 0x6D4;           // 0x58D05, paired with core/chainsaw
constexpr uint32_t plaga_timer = 0x74C;          // float countdown to emergence (0x5B825); log only
// Connected-attack latch: AxeAtk R2=0 init clears it (0x6E79E), a native hit sets it
// (0x564D9/0x5699E) and the Committed parry CAS-writes 1 (parry_commit.h). Nonzero
// makes producer 0x55C10 skip the swing's remaining collision (0x55C17).
constexpr uint32_t atk_latch = 0x7CC;
constexpr uint32_t wep_type = 0x7F0;             // ==4 chainsaw (0x58CF3, 43 sites); ==8 bowgun (0x33C62)
constexpr uint32_t span = 0x7F4;                 // bytes read per enemy snapshot
}  // namespace layout

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Line {  // Injected obstacle test; null fails closed unless explicitly waived.
    void* context = nullptr;
    bool (*clear)(void* context, const Vec3& from, const Vec3& to) = nullptr;
};
struct Config {
    // Target IDs: GanadoFamily() (below Raw), not a list. cModel::id (+0x100,
    // decomp include/model.h) is the ESL construct id: cEmMgr::construct
    // (src/game/em.cpp) ends with `pEm->id = id` after the 0x10..0x20 Ganado
    // block, and UHD has a single em10 class (RTTI .?AVcEm10@@ only, no cEm11..16),
    // so every Ganado module keeps the em10 base vtable. The 12-oct list
    // {0x12, 0x10} (r100 ESL, docs/test-spots-2026-10-12.md) refused Donald's
    // 16-oct rooms: r101/r105 Ganados are id 0x15, r108 id 0x17
    // (docs/objetivos-todos-los-ganados-2026-10-16.md). Since the second 16-oct
    // decision the Mortal Draw takes the Ending Blow's set (Classify): no type,
    // shield, chainsaw or Plaga exclusion, only the Merchant; heavy targets get
    // the damage limit (LethalCap). The routine rules below are unchanged.
    // R1 sub-routines whose body calls em10FindCk (0x78050) to leave idle:
    // Wait 0x00, Keeper 0x01, R101 Bucket/Suki/Cart 0x07-0x09, Walk 0x10, Work 0x64.
    // Excluded: Dash/Goto (route/scripted), Prayer (kneeling), Gatling (turret).
    uint8_t idle[8] = {0x00, 0x01, 0x07, 0x08, 0x09, 0x10, 0x64};
    unsigned idle_count = 7;
    // Donald, 12-oct (in game every Ganado had found Link almost at once): a
    // Ganado with its BACK to Link is a valid target even when it has found him.
    // The rear sector (Measure, NotRear) is the gate; found / forced-find no longer
    // reject. require_unaware=true restores the strict rule (idle set, Found,
    // ForcedFind). `aware`: R1 sub-routines admitted besides `idle` (R0 must be 1).
    // Index order = GC Em10_R1_move_tbl (re4-decomp src/em10/em10.cpp); the UHD
    // table 0x7F39C8 matches it where pinned: AxeAtk 0x26 at 0x7F3AFC, Crash 0x3B
    // at 0x7F3BA4, and the em10FindCk callers 0x00/0x01/0x07-0x09/0x10/0x11/0x13/
    // 0x51/0x64 (mortal-draw-real-01 findck.py). Each one moves, stands or turns
    // and only reaches an attack through em10AtkRtnCk, which switches R1 to the
    // attack routine (then Attacking rejects):
    //   0x0D Find (spotted reaction: shout + notify, then Dash/Walk)
    //   0x11 Dash (runs the route to the player; AtkRtnCk/Turn180 exits)
    //   0x12 Back (back-step, then AtkRtnCk mode 1 or Stay)
    //   0x14 GuardWalk (arms up, walks; back to Walk)
    //   0x15 Turn180 (turns on the spot; the geometry decides)
    //   0x16 Threat (threat shout, then walk or attack)
    //   0x17 SideStep (dodge, then attack/hide/walk)
    //   0x1B Stay (stands facing the player when it must not approach; r100 census)
    //   0x1D Guard (covers the head against the aimed gun)
    //   0x4C R100WalkStay, 0x4E StayWalk (r100 post walk / idle)
    //   0x4F AttackWait (ambush wait; finds when the player is >90 deg off)
    //   0x5D FindLost (lost sight: idle, then wander)
    // Kept out (NotIdleRoutine): 0x13 Goto (scripted/route goto), 0x0C Pickup,
    // 0x18/0x19 Hide/AppearSide, 0x1A SitDown, 0x1C RoofWait, 0x1E/0x1F
    // DownWakeWait/DownWake (getting up), 0x3B Crash, 0x3C ClimbOver, 0x40-0x45
    // ladders and jumps, 0x46-0x49 Trade/Drive/Catapult/RockPush, room events.
    bool require_unaware = false;
    uint8_t aware[16] = {0x0D, 0x11, 0x12, 0x14, 0x15, 0x16, 0x17, 0x1B, 0x1D, 0x4C, 0x4E, 0x4F, 0x5D};
    unsigned aware_count = 13;
    // Parry combo (TP style: parry -> step behind -> Mortal Draw), StaggerWindow.
    // The approved Committed parry does NOT change routines: parry_commit.h Confirm
    // requires +0xFC unchanged (R0=1, R1=0x26 AxeAtk, R2=1) and +0x7CC == 1, and only
    // swaps the motion to the em10 front "Crash" FCV/SEQ; AxeAtk goes to walk when
    // the motion ends (0x6ED96). So the stagger is R0=1 / R1 in stagger_r1 with the
    // latch set, for the exact parried owner, at most stagger_ms after the commit.
    // 0x29 SukiAtk: the hoe/pitchfork route of the same tier-1 parry (parry-coverage section 3).
    // 3000 (12-oct, build 08 in game: the combo step-behind outlived 2000 with Link still in
    // parry recovery). The window still closes as soon as the owner leaves the stagger routine
    // (End::Left), so a longer cap never admits a recovered Ganado.
    unsigned stagger_ms = 3000;
    uint8_t stagger_r1[4] = {0x26, 0x29};
    unsigned stagger_r1_count = 2;
    bool stagger_needs_latch = true;
    // R0=2 (damage) sub-routines admitted for the owner (e.g. a riposte knife
    // flinch). Empty by default: the UHD R1 damage table is not attributed per
    // index and holds knock-down / blow states. Entering any R0=2 then ends the
    // window; back in R0=1 the Ganado is judged by the normal rule again.
    uint8_t stagger_damage_r1[8] = {};
    unsigned stagger_damage_r1_count = 0;
    // Default: the rear sector is required for the staggered owner too (Donald:
    // move to its back). true admits any angle within reach while staggered.
    bool stagger_any_angle = false;
    // No type list since 16-oct (Donald: every skill on the Ending Blow's targets,
    // with a damage limit). The old conservative list was {2, 6, 0xA, 0xD, 0x16,
    // 0x18}; type 6 stays out as the Merchant (calc 0x4B02E zeroes its damage).
    // The others take the cut through the native pipeline like any knife hit:
    // type 2 x0.667 (0x4ADFD), armoured parts of 0xA/0xD/0x18 (0x58C38, 0x39CE6,
    // 0x39CEE, /4 at 0x39CA0) and 0x16 (GC only) only scale the amount down.
    // Provisional geometry (game units, mm-like). Measure against the real
    // animation before widening; the runtime must log measured values.
    float min_horizontal = 100.f, max_horizontal = 1200.f, max_vertical = 400.f;
    float rear_min_angle = 2.0943952f;  // player within +-60 deg of the enemy's back
    // Strike-time tolerance (RevalidateStrike), separate from press-time eligibility.
    // In-game 12-oct (build 05): every cut failed revalidation 16 frames after the
    // press (too_far after pressing at d=703..1034, not_rear once). The clip's root
    // motion carries Link ~1200-1500 units in those frames (shield pose: 1220, 1460,
    // 1526), up to ~30 deg off his facing, and the parried owner's Crash recoil pushes
    // it back. Facing the target at the start (link_runtime.cpp) aims that lunge; what
    // is left is |d - lunge| plus up to ~750 lateral drift plus the recoil: 2000 covers
    // it. Vertical gap, identity and actor checks are unchanged.
    float strike_max_horizontal = 2000.f, strike_min_horizontal = 0.f;
    // A target rear-valid at the start keeps its rear verdict through the windup
    // (same identity, pointer, id and type): a turn during the windup is not a
    // downgrade to ordinary. The surprise (routine) is still judged at the cut.
    bool latch_rear = true;
    // The parried owner admitted at the start stays the owner at the cut while it is
    // still InStagger, even if the 2000 ms window ran out during the windup.
    bool latch_stagger = true;
    bool require_line = true;
    unsigned max_slots = 256;
};
// Armored, Chainsaw and PlagaExposed are no longer produced (16-oct); they keep
// their places so earlier log indices stay. TypeExcluded now means the Merchant.
enum class Reject {
    None, NoScene, BadManager, Unreadable, Unstable, InvalidSlot, NotEm10, IdNotAllowed, TypeExcluded,
    Dead, Inactive, NotMoveRoutine, Armored, Chainsaw, PlagaExposed, Found, PendingDamage, ForcedFind,
    NotIdleRoutine, TooClose, TooFar, VerticalGap, NotRear, NoLineCheck, LineBlocked, NotFinite,
    Attacking,  // attack or grab R1 (AttackRoutine); appended so earlier log indices stay
};
// em10 attack / grab sub-routines (GC Em10_R1_move_tbl order): 0x0E C_SawStart,
// 0x0F BombIgnition, 0x20..0x32 (ParasiteAtk, Shot*, Throw*, AxeAtk 0x26,
// ShieldAtk, TorchFrame, SukiAtk 0x29, ScytheAtk, Claw* and C_Saw* attacks/hits),
// 0x33..0x3A (Catch, NeckHang*, Backhold, Bombhold, DashCatch, TakeAway),
// 0x3D..0x3F (Door/Rack/WindowAtk), 0x59 StickClaw, 0x65 UFOCatch,
// 0x66 R300TakeAshley, 0x6D CSawWalkAtk. r100 census 12-oct: found Ganados ran
// 0x26 / 0x29 (attacks) and 0x1B (Stay).
inline bool AttackRoutine(uint8_t r1) {
    return r1 == 0x0E || r1 == 0x0F || (r1 >= 0x20 && r1 <= 0x3A) || (r1 >= 0x3D && r1 <= 0x3F) ||
           r1 == 0x59 || r1 == 0x65 || r1 == 0x66 || r1 == 0x6D;
}
struct Raw {
    uint32_t vtable = 0, be_flag = 0, status = 0, flag_3d0 = 0, be_flg = 0;
    uint32_t p_shield = 0, p_core = 0, p_parasite = 0;
    float plaga_timer = 0;  // log only
    uint8_t id = 0, type = 0, r0 = 0, r1 = 0, dmg_flag = 0, dmg_timer = 0, wep_type = 0, atk_latch = 0;
    int16_t hp = 0, hp_max = 0;
    Vec3 pos; float ang_y = 0;
};
// Target family shared by the Mortal Draw (Classify), the Ending Blow and spin
// attack (ending_blow_target.h Actor) and Z-targeting (ztarget.h Actor).
// Donald, 16-oct: "a todos los que podemos botar al piso". Census of the
// md_census lines in link_runtime.log / link_runtime.old.log (127,052 lines,
// rooms r100..r330; docs/objetivos-todos-los-ganados-2026-10-16.md): every
// em10-vtable actor was id 0x11, 0x12, 0x13, 0x15, 0x17 or 0x1F, and the ones
// outside 0x12 lay on the floor (R0=1, R1=0x1E) in r101, r104, r105, r108 and
// r117 while refused as id_not_allowed; one of them is Dr. Salvador (r105, id
// 0x15, type 4, 5000 HP, chainsaw). The decomp builds 0x10..0x20 in one Ganado
// block (src/game/em.cpp) and shares one library across em10..em17, em19..em1f
// and em20 (src/em10/em10.cpp); 0x18 is cEm18, the Merchant (UHD vtable
// 0x710AD8, ctor 0x834F0). The classifier 0x1AF000 is not reused: it also
// answers 1 for 0x40..0x4F. Callers still require the cEm10 base vtable, so
// dogs, animals, bosses and allies (own classes) never get here.
inline bool GanadoFamily(uint8_t id) { return id >= 0x10 && id <= 0x20 && id != 0x18; }
// The Merchant in any of his forms: cEm18 (id 0x18), an em10 built with type 6
// (r104 shows him as id 0x13 type 6 in the census; ckFindPL 0x33B30, calc
// 0x4B02E zeroes damage on type 6), or an em10 in the Trade routine (R0=1,
// R1=0x46, which opens the shop). Never a sword or camera target.
inline bool Merchant(const Raw& r) { return r.id == 0x18 || r.type == 6 || (r.r0 == 1 && r.r1 == 0x46); }
// The id/type half of Merchant(), for callers that only hold the id and type
// (the parry counter-hit's owner just attacked, so it is not in Trade).
inline bool MerchantKind(uint8_t id, uint8_t type) { return id == 0x18 || type == 6; }
// Damage limit (Donald, 16-oct: every skill on the Ending Blow's targets, but
// with a damage limit). The two lethal paths, the Ending Blow on the floor and
// the Mortal Draw from behind or in the parry stagger, stay lethal on ordinary
// Ganados. On a heavy one their hit removes at most heavy_cap HP and kills only
// when the HP left is already <= heavy_cap (damage::CappedLethalAmount).
// Heavy = max HP above heavy_hp_max, a chainsaw (+0x7F0 == 4: Dr. Salvador and
// the Bella sisters; the calc already caps a knife hit on them at 1000, 0x4B061),
// or a claw type 0xA/0xD (Garrador-like: the calc sends them to 0x4AFD6, where a
// knife hit on any non-core part is /8+1, so a lethal blow can never kill them).
// Census (docs/objetivos-todos-los-ganados-2026-10-16.md): every em10 row has
// 500..1200 max HP except r10a id 0x12 types 0/1/3/4 (1500), r330 id 0x1F type
// 0x18 (2000) and the r101/r105 chainsaw carriers (id 0x15 type 4, 5000). The
// line sits above 1500 so the r10a villagers, common Ganados, stay lethal (the
// first build drew it at 1200 and capped them). Spin and the parry counter-hit
// are ordinary hits and never use it.
constexpr int16_t heavy_hp_max = 1500;
constexpr int heavy_cap = 1000;
inline bool Heavy(const Raw& r) {
    return r.hp_max > heavy_hp_max || r.wep_type == 4 || r.type == 0x0A || r.type == 0x0D;
}
// damage::Kind::Lethal's amount for this target: 0 = no limit, else the HP cap.
inline int LethalCap(const Raw& r) { return Heavy(r) ? heavy_cap : 0; }
namespace detail {
template <class T> inline T At(const unsigned char* block, uint32_t offset) {
    T value{}; std::memcpy(&value, block + offset, sizeof(T)); return value;
}
inline Raw Decode(const unsigned char* b) {
    Raw r;
    r.vtable = At<uint32_t>(b, 0); r.be_flag = At<uint32_t>(b, layout::be_flag);
    r.status = At<uint32_t>(b, layout::status); r.flag_3d0 = At<uint32_t>(b, layout::flag_3d0);
    r.be_flg = At<uint32_t>(b, layout::be_flg); r.p_shield = At<uint32_t>(b, layout::p_shield);
    r.p_core = At<uint32_t>(b, layout::p_core); r.p_parasite = At<uint32_t>(b, layout::p_parasite);
    r.plaga_timer = At<float>(b, layout::plaga_timer);
    r.id = b[layout::id]; r.type = b[layout::type]; r.r0 = b[layout::routine]; r.r1 = b[layout::routine + 1];
    r.dmg_flag = b[layout::dmg_flag]; r.dmg_timer = b[layout::dmg_timer]; r.wep_type = b[layout::wep_type];
    r.atk_latch = b[layout::atk_latch];
    r.hp = At<int16_t>(b, layout::hp); r.hp_max = At<int16_t>(b, layout::hp_max);
    r.pos = {At<float>(b, layout::pos), At<float>(b, layout::pos + 4), At<float>(b, layout::pos + 8)};
    r.ang_y = At<float>(b, layout::ang + 4);
    return r;
}
template <size_t N> inline bool In(const uint8_t (&list)[N], unsigned count, uint8_t value) {
    for (unsigned i = 0; i < count && i < N; ++i) if (list[i] == value) return true;
    return false;
}
inline bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}  // namespace detail

// The parried owner's stagger state (StaggerWindow): the Crash swing with the latch
// set (R0=1, R1 in stagger_r1) or a configured R0=2 flinch.
inline bool InStagger(const Raw& r, const Config& c) {
    const bool swing = r.r0 == 1 && detail::In(c.stagger_r1, c.stagger_r1_count, r.r1) &&
                       (!c.stagger_needs_latch || r.atk_latch == 1);
    const bool flinch = r.r0 == 2 && detail::In(c.stagger_damage_r1, c.stagger_damage_r1_count, r.r1);
    return swing || flinch;
}
// Magnetism: the native yaw (+0xA4) that faces `to` from `from`. Same convention as
// Measure and shield_reader FlatYawBasis: forward = (sin yaw, cos yaw), so
// yaw = atan2(dx, dz) in [-pi, pi]. False when not finite or closer than 1 unit.
inline bool FaceYaw(const Vec3& from, const Vec3& to, float& yaw) {
    if (!detail::Finite(from) || !detail::Finite(to)) return false;
    const float dx = to.x - from.x, dz = to.z - from.z;
    if (!(dx * dx + dz * dz >= 1.f)) return false;
    yaw = std::atan2(dx, dz);
    return std::isfinite(yaw);
}
// Signed shortest turn from `from` to `to`, in (-pi, pi].
inline float Turn(float from, float to) {
    constexpr float pi = 3.14159265f, two_pi = 6.28318531f;
    float d = std::fmod(to - from, two_pi);
    if (d > pi) d -= two_pi; else if (d <= -pi) d += two_pi;
    return d;
}

// Two identical full reads; a torn snapshot is never classified.
template <class Reader> Reject ReadRaw(Reader& reader, uint32_t pointer, Raw& out) {
    static thread_local unsigned char a[layout::span], b[layout::span];
    if (!pointer || !reader.Read(pointer, a, sizeof(a)) || !reader.Read(pointer, b, sizeof(b))) return Reject::Unreadable;
    if (std::memcmp(a, b, sizeof(a))) return Reject::Unstable;
    out = detail::Decode(a);
    return Reject::None;
}
struct Manager { uint32_t array = 0, count = 0, block = 0; };
template <class Reader> Reject ReadManager(Reader& reader, uint32_t image_base, const Config& c, Manager& m) {
    m = {};
    if (!reader.Read(image_base + layout::manager_array_rva, &m.array, 4) ||
        !reader.Read(image_base + layout::manager_count_rva, &m.count, 4) ||
        !reader.Read(image_base + layout::manager_block_rva, &m.block, 4)) return Reject::Unreadable;
    if (!m.array || m.count > c.max_slots || m.block < layout::span || m.block > 0x4000) return Reject::BadManager;
    return Reject::None;
}

// Actor-only checks, split so revalidation can report surprise separately.
// `stagger_owner`: StaggerWindow::Admit confirmed the parried owner in its stagger.
// "surprise" keeps its name (log, Recheck::unaware) but now means "a valid Mortal
// Draw state": with the geometry also passing, the cut is lethal.
struct Unit { Reject actor = Reject::None, surprise = Reject::None; };
inline Unit Classify(const Raw& r, uint32_t image_base, const Config& c, bool stagger_owner = false) {
    Unit u;
    if ((r.be_flag & 0x201) != 1) u.actor = Reject::InvalidSlot;
    else if (r.vtable != image_base + layout::em10_vtable_rva) u.actor = Reject::NotEm10;
    else if (!GanadoFamily(r.id)) u.actor = Reject::IdNotAllowed;
    else if (Merchant(r)) u.actor = Reject::TypeExcluded;
    else if (r.hp <= 0 || r.hp_max <= 0 || r.hp > r.hp_max) u.actor = Reject::Dead;
    else if (!(r.status & 0x20)) u.actor = Reject::Inactive;
    else if (r.r0 != 1 && !(stagger_owner && r.r0 == 2 && detail::In(c.stagger_damage_r1, c.stagger_damage_r1_count, r.r1)))
        u.actor = Reject::NotMoveRoutine;                        // 2 damage/down, 3 die, 4 scenario
    // 16-oct: no shield, chainsaw or emerged-Plaga exclusion (the Ending Blow's set).
    // The cut stays one native knife record on a body part (damage::ReadFacts: the
    // first ON node of the hit list that is neither head 5 nor core 0x25), published
    // without the blade's collision and consumed by the Ganado's own block 0x57D30 ->
    // calc 0x4AD40 -> LifeDownSet2: the chainsaw's 1000 cap (0x4B061), armoured
    // parts (/4, 0x39CA0) and the Plaga scale it there, death, Plaga emergence and
    // rewards stay native. A pending record still refuses below (PendingDamage).
    if (c.require_unaware) {  // strict rule (before 12-oct): never seen, idle routine
        if (r.be_flg & 0x100) u.surprise = Reject::Found;
        else if (r.dmg_flag || r.dmg_timer) u.surprise = Reject::PendingDamage;
        else if (r.flag_3d0 & 0x80) u.surprise = Reject::ForcedFind;
        else if (!detail::In(c.idle, c.idle_count, r.r1)) u.surprise = Reject::NotIdleRoutine;
        return u;
    }
    // A pending record (+0x32C bit0) from any source always rejects: the hit
    // belongs to someone else and NativeHit would answer PendingNative.
    if (r.dmg_flag & 1) u.surprise = Reject::PendingDamage;
    else if (stagger_owner) {}  // residual damage bytes of the parried owner tolerated
    else if (r.dmg_flag || r.dmg_timer) u.surprise = Reject::PendingDamage;
    else if (detail::In(c.idle, c.idle_count, r.r1) || detail::In(c.aware, c.aware_count, r.r1)) {}
    else if (AttackRoutine(r.r1)) u.surprise = Reject::Attacking;
    else u.surprise = Reject::NotIdleRoutine;
    return u;
}
struct Geometry { Reject reject = Reject::None; float horizontal = 0, vertical = 0, cosine = 0; };
inline Geometry Measure(const Vec3& player, const Raw& r, const Config& c, const Line& line, bool any_angle = false) {
    Geometry g;
    if (!detail::Finite(player) || !detail::Finite(r.pos) || !std::isfinite(r.ang_y)) { g.reject = Reject::NotFinite; return g; }
    const float dx = player.x - r.pos.x, dz = player.z - r.pos.z;
    g.horizontal = std::sqrt(dx * dx + dz * dz);
    g.vertical = std::fabs(player.y - r.pos.y);
    // Facing as GC GetXZAngle/Muku: forward = (sin ang.y, cos ang.y).
    if (g.horizontal > 0.f) g.cosine = (dx * std::sin(r.ang_y) + dz * std::cos(r.ang_y)) / g.horizontal;
    if (g.horizontal < c.min_horizontal) g.reject = Reject::TooClose;
    else if (g.horizontal > c.max_horizontal) g.reject = Reject::TooFar;
    else if (g.vertical > c.max_vertical) g.reject = Reject::VerticalGap;
    // N-4: the line is judged before the rear sector, so a turned enemy (NotRear,
    // still an ordinary strike in reach) never skips the obstacle test.
    else if (c.require_line && !line.clear) g.reject = Reject::NoLineCheck;
    else if (line.clear && !line.clear(line.context, player, r.pos)) g.reject = Reject::LineBlocked;
    else if (!any_angle && !(g.cosine <= std::cos(c.rear_min_angle))) g.reject = Reject::NotRear;
    return g;
}

// Mod-owned identity: a session-unique number per observed actor lifetime.
// A new identity is issued on a new scene, slot reuse (invalid->valid),
// pointer/ID/max-HP change, HP rising, or a return to the Init routine.
class Tracker {
    struct Slot { uint64_t identity = 0; uint32_t pointer = 0; int16_t hp = 0, hp_max = 0; uint8_t id = 0, r0 = 0; bool valid = false; };
    Slot slots_[256];
    uint64_t next_ = 0;
    uint32_t scene_ = 0;
public:
    void BeginScene(uint32_t epoch) { scene_ = epoch; for (auto& s : slots_) s = Slot{}; }
    uint32_t Scene() const { return scene_; }
    uint64_t Observe(uint32_t index, uint32_t pointer, bool valid, const Raw& r) {
        if (index >= 256 || !scene_) return 0;
        Slot& s = slots_[index];
        if (!valid) { s.valid = false; s.identity = 0; return 0; }
        const bool fresh = !s.valid || !s.identity || s.pointer != pointer || s.id != r.id || s.hp_max != r.hp_max ||
            r.hp > s.hp || (s.r0 != 0 && r.r0 == 0);
        if (fresh) s.identity = (uint64_t(scene_ & 0xFFFF) << 48) | (++next_ & 0xFFFFFFFFFFFFULL);
        s.valid = true; s.pointer = pointer; s.id = r.id; s.hp = r.hp; s.hp_max = r.hp_max; s.r0 = r.r0;
        return s.identity;
    }
};

// Parry stagger window: after a Committed parry, the exact parried owner is a
// valid target while it stays in its stagger (Config::stagger_*), even though it
// is in an attack routine. Owner = the actor/id/type the commit observation
// qualified (the same triple parry_riposte admits), bound on first sight to the
// tracker identity (a recycled slot or a new scene never inherits it). The window
// ends on: stagger_ms elapsed, leaving the stagger state once (it never re-opens
// for that commit), identity/id/type change, death/invalid slot, Clear() (scene)
// or a newer commit. Game thread only: OnCommitted (ResolveParry), Tick + Select
// (arbiter exit), Revalidate (player handler).
class StaggerWindow {
public:
    enum class End : uint8_t { None, Expired, Left, Identity, Replaced, Cleared };
    struct Stats { unsigned armed = 0, admitted = 0, expired = 0, left = 0, identity = 0, replaced = 0, ignored = 0; End last = End::None; };
    void OnCommitted(uint32_t serial, uint32_t actor, uint8_t id, uint8_t type, uint64_t now_ms, const Config& c) {
        if (!serial || !actor || serial == serial_) { ++stats_.ignored; return; }
        if (armed_) Finish(End::Replaced);
        serial_ = serial; actor_ = actor; id_ = id; type_ = type; identity_ = 0;
        now_ = now_ms; until_ = now_ms + c.stagger_ms; armed_ = true; ++stats_.armed;
    }
    void Tick(uint64_t now_ms) { now_ = now_ms; if (armed_ && now_ > until_) Finish(End::Expired); }
    void Clear() { if (armed_) Finish(End::Cleared); }
    bool Armed() const { return armed_; }
    uint32_t Actor() const { return armed_ ? actor_ : 0; }
    uint32_t Serial() const { return serial_; }
    // Log only: milliseconds left at the last Tick (0 when not armed).
    unsigned RemainingMs() const { return armed_ && until_ > now_ ? unsigned(until_ - now_) : 0; }
    const Stats& stats() const { return stats_; }
    // Called for every observed slot: true only for the owner in its stagger now.
    bool Admit(uint32_t pointer, uint64_t identity, const Raw& r, const Config& c) {
        if (!armed_ || pointer != actor_) return false;
        if (now_ > until_) { Finish(End::Expired); return false; }
        if (!identity || (identity_ && identity != identity_) || r.id != id_ || r.type != type_ ||
            (r.be_flag & 0x201) != 1 || r.hp <= 0) { Finish(End::Identity); return false; }
        identity_ = identity;
        if (!InStagger(r, c)) { Finish(End::Left); return false; }
        ++stats_.admitted;
        return true;
    }
private:
    void Finish(End e) {
        armed_ = false; stats_.last = e;
        switch (e) {
        case End::Expired: ++stats_.expired; break;
        case End::Left: ++stats_.left; break;
        case End::Identity: ++stats_.identity; break;
        case End::Replaced: ++stats_.replaced; break;
        default: break;
        }
    }
    bool armed_ = false;
    uint32_t serial_ = 0, actor_ = 0;
    uint8_t id_ = 0, type_ = 0;
    uint64_t identity_ = 0, now_ = 0, until_ = 0;
    Stats stats_;
};

struct Selection {
    bool found = false;
    uint32_t index = 0, pointer = 0;
    uint64_t identity = 0;
    float horizontal = 0, vertical = 0, cosine = 0;
    unsigned eligible = 0, examined = 0;
    Reject reject = Reject::None;  // reason for the nearest rejected em10 slot when not found
    Raw raw;
    bool stagger = false;          // selected as the parried owner in its stagger window
};
// Picks at most one eligible target: nearest in the rear sector, ties to the lower index.
// `stagger` (optional): the parry stagger window; null = no parry combo.
template <class Reader>
Selection Select(Reader& reader, uint32_t image_base, Tracker& tracker, const Vec3& player, const Config& c, const Line& line,
                 StaggerWindow* stagger = nullptr) {
    Selection best;
    if (!tracker.Scene()) { best.reject = Reject::NoScene; return best; }
    Manager m;
    if ((best.reject = ReadManager(reader, image_base, c, m)) != Reject::None) return best;
    best.reject = Reject::None;
    float nearest_rejected = 1e30f;
    for (uint32_t i = 0; i < m.count; ++i) {
        const uint32_t pointer = m.array + i * m.block;
        Raw r;
        const Reject read = ReadRaw(reader, pointer, r);
        if (read == Reject::Unreadable) continue;
        ++best.examined;
        if (read == Reject::Unstable) { if (!best.found && best.reject == Reject::None) best.reject = read; continue; }
        const bool valid = (r.be_flag & 0x201) == 1;
        const uint64_t identity = tracker.Observe(i, pointer, valid, r);
        const bool owner = stagger && stagger->Admit(pointer, identity, r, c);
        const Unit u = Classify(r, image_base, c, owner);
        Reject why = u.actor != Reject::None ? u.actor : u.surprise;
        Geometry g;
        if (why == Reject::None) { g = Measure(player, r, c, line, owner && c.stagger_any_angle); why = g.reject; }
        if (why == Reject::None && !identity) why = Reject::NoScene;
        if (why != Reject::None) {
            const float dx = player.x - r.pos.x, dz = player.z - r.pos.z, d = dx * dx + dz * dz;
            if (!best.found && (best.reject == Reject::None || (std::isfinite(d) && d < nearest_rejected))) {
                best.reject = why; if (std::isfinite(d)) nearest_rejected = d;
            }
            continue;
        }
        ++best.eligible;
        if (!best.found || g.horizontal < best.horizontal) {
            const unsigned eligible = best.eligible, examined = best.examined;
            best = Selection{true, i, pointer, identity, g.horizontal, g.vertical, g.cosine, eligible, examined, Reject::None, r, owner};
        }
    }
    return best;
}

// Impact-frame revalidation. `same` covers identity; actor_ok keeps the action
// alive; `unaware` decides lethal vs ordinary; geometry covers reach/sector/line.
struct Recheck {
    bool same = false, actor_ok = false, unaware = false, geometry_ok = false;
    bool stagger = false;  // judged as the parried owner in its stagger window
    // RevalidateStrike only: the start latch kept the owner / the rear verdict.
    bool stagger_latched = false, rear_latched = false;
    Reject actor = Reject::None, surprise = Reject::None, geometry = Reject::None;
    float horizontal = 0, vertical = 0, cosine = 0;  // measured now (log)
    Raw raw;
};
template <class Reader>
Recheck Revalidate(Reader& reader, uint32_t image_base, Tracker& tracker, const Selection& s, const Vec3& player,
                 const Config& c, const Line& line, StaggerWindow* stagger = nullptr) {
    Recheck v;
    Manager m;
    if (!s.found || !s.identity || ReadManager(reader, image_base, c, m) != Reject::None || s.index >= m.count ||
        m.array + s.index * m.block != s.pointer) return v;
    Raw r;
    if (ReadRaw(reader, s.pointer, r) != Reject::None) return v;
    const bool valid = (r.be_flag & 0x201) == 1;
    if (tracker.Observe(s.index, s.pointer, valid, r) != s.identity) return v;
    v.same = true; v.raw = r;
    v.stagger = stagger && stagger->Admit(s.pointer, s.identity, r, c);
    const Unit u = Classify(r, image_base, c, v.stagger);
    v.actor = u.actor; v.surprise = u.surprise;
    v.actor_ok = u.actor == Reject::None;
    v.unaware = v.actor_ok && u.surprise == Reject::None;
    const Geometry g = Measure(player, r, c, line, v.stagger && c.stagger_any_angle);
    v.geometry = g.reject; v.horizontal = g.horizontal; v.vertical = g.vertical; v.cosine = g.cosine;
    v.geometry_ok = v.geometry == Reject::None;
    return v;
}
// What the start (press-time rules, Revalidate) established about the target, kept
// for the cut. Valid only for a fully eligible start: same identity, actor and
// surprise ok, geometry ok (so in the rear sector unless the stagger any-angle
// alternative admitted it).
struct PressLatch {
    bool valid = false, rear = false, stagger = false;
    uint64_t identity = 0;
    uint32_t pointer = 0;
    uint8_t id = 0, type = 0;
    float horizontal = 0, cosine = 0;
};
inline PressLatch Latch(const Selection& s, const Recheck& v, const Config& c) {
    PressLatch l;
    if (!s.found || !s.identity || !v.same || !v.actor_ok || !v.unaware || !v.geometry_ok) return l;
    l.valid = true; l.identity = s.identity; l.pointer = s.pointer; l.id = v.raw.id; l.type = v.raw.type;
    l.rear = v.cosine <= std::cos(c.rear_min_angle); l.stagger = v.stagger;
    l.horizontal = v.horizontal; l.cosine = v.cosine;
    return l;
}
// Strike-frame revalidation: Revalidate with the strike tolerance. Identity, slot,
// id, type (Merchant), life, activity, R0, pending damage,
// surprise (routine) and the vertical gap are judged now, as at the start. Changed:
// reach is [strike_min_horizontal, max(max_horizontal, strike_max_horizontal)];
// with latch_rear a target rear-valid at the start skips the rear sector; with
// latch_stagger the owner admitted at the start stays the owner while InStagger.
// A latch for another identity, pointer, id or type is ignored.
template <class Reader>
Recheck RevalidateStrike(Reader& reader, uint32_t image_base, Tracker& tracker, const Selection& s, const PressLatch& latch,
                         const Vec3& player, const Config& c, const Line& line, StaggerWindow* stagger = nullptr) {
    Recheck v;
    Manager m;
    if (!s.found || !s.identity || ReadManager(reader, image_base, c, m) != Reject::None || s.index >= m.count ||
        m.array + s.index * m.block != s.pointer) return v;
    Raw r;
    if (ReadRaw(reader, s.pointer, r) != Reject::None) return v;
    const bool valid = (r.be_flag & 0x201) == 1;
    if (tracker.Observe(s.index, s.pointer, valid, r) != s.identity) return v;
    v.same = true; v.raw = r;
    const bool latched = latch.valid && latch.identity == s.identity && latch.pointer == s.pointer &&
                         latch.id == r.id && latch.type == r.type;
    v.stagger = stagger && stagger->Admit(s.pointer, s.identity, r, c);
    if (!v.stagger && c.latch_stagger && latched && latch.stagger && valid && r.hp > 0 && InStagger(r, c))
        v.stagger = v.stagger_latched = true;
    const Unit u = Classify(r, image_base, c, v.stagger);
    v.actor = u.actor; v.surprise = u.surprise;
    v.actor_ok = u.actor == Reject::None;
    v.unaware = v.actor_ok && u.surprise == Reject::None;
    Config sc = c;
    sc.max_horizontal = c.strike_max_horizontal > c.max_horizontal ? c.strike_max_horizontal : c.max_horizontal;
    sc.min_horizontal = c.strike_min_horizontal < c.min_horizontal ? c.strike_min_horizontal : c.min_horizontal;
    const bool rear_latch = c.latch_rear && latched && latch.rear;
    const Geometry g = Measure(player, r, sc, line, (v.stagger && c.stagger_any_angle) || rear_latch);
    v.geometry = g.reject; v.horizontal = g.horizontal; v.vertical = g.vertical; v.cosine = g.cosine;
    v.geometry_ok = v.geometry == Reject::None;
    v.rear_latched = rear_latch && v.geometry_ok && !(g.cosine <= std::cos(c.rear_min_angle));
    return v;
}
// Bridge to the pure action policy; player/input/ground fields come from elsewhere.
// An enemy that turned (NotRear) is still in reach: the strike stays ordinary if it
// connects (actual_contact), but leaving the rear sector also drops the surprise.
inline void Apply(const Selection& s, const Recheck& v, const Tracker& t, Snapshot& out) {
    out.scene = t.Scene();
    out.target = v.same ? s.identity : 0;
    // The policy's field names predate 16-oct: Classify no longer refuses an emerged Plaga.
    out.common_alive_active = out.standing = out.no_exposed_plaga = v.same && v.actor_ok;
    out.unaware = v.same && v.unaware && v.geometry_ok;
    out.rear_close_clear = v.same && (v.geometry_ok || v.geometry == Reject::NotRear);
}
}  // namespace link_runtime::mortal_draw::target

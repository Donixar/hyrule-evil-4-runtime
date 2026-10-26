// dll/ocarina_song_effects.h
#pragma once
// Combat effects of the ocarina songs (Donald, 17-oct-2026; docs/ocarina-canciones-sheik-epona-2026-10-17.md).
// Pure logic over an injected reader and publisher: no hooks, no process access, never writes HP.
//
//   Sheik  (Blow::Flash):  every Ganado within the radius gets one native flash-grenade record (weapon 0x17),
//                          like Sheik's Deku nut. Their own em10DmCk then runs the grenade's reaction
//                          (em10DmSetWep23, re4-decomp src/em10/em10.cpp:1846): Dm_Flash stun (the kick prompt
//                          opens), an emerged Plaga dies, armoured types 0xA/0xD, shields, gatlings and set 0x19
//                          ignore it. Dmg_tbl_em10[0x17] is 0 (src/game/em_dm_val.cpp): the calc leaves 1 HP at most.
//   Epona  (Blow::Charge): the nearest standing Ganado within the radius gets one native kick record (weapon
//                          0x14, Leon's kick in the live hit traces, the 14-oct kick camera report;
//                          the decomp's running debug hit, player.cpp:629, publishes the same weapon through
//                          PlWepHitCheck2's blast list), as if Wolf Link charged it: Em10DmSetWep_tbl[0x14] is
//                          em10DmSetWep00 (em10.cpp:1061), the kick reaction that knocks it down (setRno(2,4,0,1)),
//                          with the kick's ordinary native damage (Dmg_tbl_em10[0x14] = 200 before factors).
//
// Both write the record exactly as the native PlWepHitCheck2 (src/game/pl_wep.cpp:349) does for these weapons:
// dmg->set(flags 0, timer 10, weapon, from, part->len, part), only when the record is not pending (+0x32C bit0).
// The publisher is the one the Mortal Draw adapter uses (mortal_draw_damage.h: 0x1AEEF0, thiscall ECX =
// actor+0x328), the part is the first ON body node that is neither head nor core (damage::ReadFacts; the
// calc dereferences it, so a target without one is skipped). The adapter is not involved: it owns only knife
// records (weapon 0x10), so Resolve never claims these and the GetWepDmVal detour leaves them native.
//
// Targets: the Ending Blow / Z-target family (ending_blow_target.h Actor): live em10-vtable slot, Ganado id
// 0x10..0x20 except the Merchant (cEm18, type 6, Trade routine), HP in range, active, R0 1 or 2. Flash also
// skips what the native reaction ignores (types 0xA/0xD, shield); Charge takes only a standing Ganado (R0 1,
// not down or getting up) and skips heavy ones (mortal_draw_target.h Heavy: > 1500 HP, chainsaw, claw types).
#include "mortal_draw_damage.h"
#include "mortal_draw_target.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace link_runtime::ocarina::song_effects {
namespace tg = link_runtime::mortal_draw::target;
namespace dmg = link_runtime::mortal_draw::damage;

enum class Blow : uint8_t { Flash, Charge };
constexpr uint8_t kFlashWeapon = 0x17, kChargeWeapon = 0x14;
constexpr uint8_t kNativeTimer = 0x0A;   // PlWepHitCheck2's dmg->set(0, 10, ...)
inline uint8_t Weapon(Blow b) { return b == Blow::Flash ? kFlashWeapon : kChargeWeapon; }
inline const char* BlowName(Blow b) { return b == Blow::Flash ? "flash" : "charge"; }
// Vertical band from Link's feet (mm): the flash reaches a balcony or a ditch, the charge stays on the level.
constexpr float kFlashVertical = 3000.f, kChargeVertical = 1500.f;
constexpr unsigned kMaxFlash = 16;   // PlWepHitCheck2's TARGET_WK list holds 20

enum class Skip : uint8_t { None, Slot, NotEm10, NotGanado, Merchant, Dead, Inactive, Routine, Down, Pending, Armoured, Shielded,
                            Heavy, NotFinite, Far, Vertical, Unreadable, Count };
inline const char* SkipName(Skip s) {
    static const char* const n[] = {"none", "slot", "not_em10", "not_ganado", "merchant", "dead", "inactive", "routine", "down",
                                    "pending", "armoured", "shielded", "heavy", "not_finite", "far", "vertical", "unreadable"};
    return unsigned(s) < unsigned(Skip::Count) ? n[unsigned(s)] : "?";
}

// One enemy against one blow. `distance`: horizontal distance from Link (filled once the position is finite).
inline Skip Judge(const tg::Raw& r, uint32_t base, const tg::Vec3& link, Blow b, float radius, float& distance) {
    distance = 0;
    if ((r.be_flag & 0x201) != 1) return Skip::Slot;
    if (r.vtable != base + tg::layout::em10_vtable_rva) return Skip::NotEm10;
    if (!tg::GanadoFamily(r.id)) return Skip::NotGanado;
    if (tg::Merchant(r)) return Skip::Merchant;
    if (r.hp <= 0 || r.hp_max <= 0 || r.hp > r.hp_max) return Skip::Dead;
    if (!(r.status & 0x20)) return Skip::Inactive;
    if (r.r0 != 1 && r.r0 != 2) return Skip::Routine;   // 3 dying, 4 scenario
    if (b == Blow::Charge && (r.r0 != 1 || r.r1 == 0x1E || r.r1 == 0x1F)) return Skip::Down;
    if (r.dmg_flag & 1) return Skip::Pending;          // someone else's hit is waiting: the native rule too
    if (b == Blow::Flash && (r.type == 0x0A || r.type == 0x0D)) return Skip::Armoured;
    if (b == Blow::Flash && r.p_shield) return Skip::Shielded;
    if (b == Blow::Charge && tg::Heavy(r)) return Skip::Heavy;
    if (!tg::detail::Finite(r.pos) || !tg::detail::Finite(link)) return Skip::NotFinite;
    const float dx = r.pos.x - link.x, dz = r.pos.z - link.z;
    distance = std::sqrt(dx * dx + dz * dz);
    if (!(distance <= radius)) return Skip::Far;
    if (!(std::fabs(r.pos.y - link.y) <= (b == Blow::Flash ? kFlashVertical : kChargeVertical))) return Skip::Vertical;
    return Skip::None;
}

struct Target { uint32_t actor = 0; uint8_t id = 0, type = 0; float distance = 0; };
struct Pick {
    std::array<Target, kMaxFlash> items{}; unsigned count = 0, scanned = 0;
    unsigned skips[unsigned(Skip::Count)] = {};
};
inline unsigned Cap(Blow b) { return b == Blow::Flash ? kMaxFlash : 1; }
// Nearest first (ties keep manager order); the farthest drops when the list is full.
inline void Offer(Pick& p, const Target& t, unsigned cap) {
    if (!cap || cap > kMaxFlash) return;
    unsigned at = p.count;
    while (at && t.distance < p.items[at - 1].distance) --at;
    if (at >= cap) return;
    const unsigned last = p.count < cap ? p.count++ : cap - 1;
    for (unsigned j = last; j > at; --j) p.items[j] = p.items[j - 1];
    p.items[at] = t;
}
// Every manager slot, two identical reads each (tg::ReadRaw). An unreadable manager returns an empty pick.
template <class Reader> Pick Scan(Reader& m, uint32_t base, const tg::Vec3& link, Blow b, float radius) {
    Pick p; tg::Manager pool;
    if (tg::ReadManager(m, base, tg::Config{}, pool) != tg::Reject::None) return p;
    for (unsigned i = 0; i < pool.count; ++i) {
        const uint32_t ptr = pool.array + i * pool.block; tg::Raw r;
        ++p.scanned;
        if (tg::ReadRaw(m, ptr, r) != tg::Reject::None) { ++p.skips[unsigned(Skip::Unreadable)]; continue; }
        float d = 0; const Skip s = Judge(r, base, link, b, radius, d);
        if (s != Skip::None) { ++p.skips[unsigned(s)]; continue; }
        Offer(p, Target{ptr, r.id, r.type, d}, Cap(b));
    }
    return p;
}

// The record PlWepHitCheck2 writes for this weapon (flags 0, timer 10), on `actor`'s cDmgInfo.
inline dmg::PublishArgs Args(uint32_t actor, Blow b, const dmg::Facts& f) {
    return dmg::PublishArgs{actor + dmg::layout::dmg_info, 0, kNativeTimer, Weapon(b), f.from, f.hitpart_len, f.hitpart};
}
enum class Shot : uint8_t { Published, Unreadable, Dead, Pending, NoHitpart, Failed, Mismatch };
inline const char* ShotName(Shot s) {
    static const char* const n[] = {"published", "unreadable", "dead", "pending", "no_hitpart", "failed", "mismatch"};
    return n[unsigned(s)];
}
// Re-read right before the call (same frame as Scan): life, pending record and the body part. Publish:
// bool(const dmg::PublishArgs&), the SEH-guarded CallPublisher in the runtime. Afterwards the live record must
// be the one written (Mismatch otherwise: logged, the native hit stays whatever the game made of it).
template <class Reader, class Publish>
Shot Fire(Reader& m, uint32_t base, uint32_t actor, Blow b, const float (&from)[3], Publish&& publish,
          const native_hitpart::Source* source = nullptr) {
    dmg::HitTarget t; t.identity = actor; t.actor = actor;
    t.repair_len = true;   // shield-guard-02: weapons 0x14/0x17 never read the node len (a wading Ganado's is bad)
    dmg::Facts f;
    if (!dmg::ReadFacts(m, base, t, true, from, f, source)) return Shot::Unreadable;
    if (f.hp <= 0 || f.hp_max <= 0 || f.hp > f.hp_max) return Shot::Dead;
    if (f.pending_native) return Shot::Pending;
    if (!f.hitpart || f.parts_no < 0) return Shot::NoHitpart;
    const dmg::PublishArgs a = Args(actor, b, f);
    if (!publish(a)) return Shot::Failed;
    dmg::Record live;
    if (!dmg::ReadRecord(m, actor, live) || dmg::Mismatch(dmg::Expected(a), live)) return Shot::Mismatch;
    return Shot::Published;
}

struct Report {
    Blow blow = Blow::Flash; Pick pick; unsigned published = 0;
    unsigned shots[7] = {};                       // by Shot
    Shot result[kMaxFlash] = {};                  // per pick item
};
// Scan, then one shot per picked target, nearest first. `from` is Link's position (the flash's source, the
// charge's origin: the kick reaction knocks the Ganado away from it).
template <class Reader, class Publish>
Report Run(Reader& m, uint32_t base, const tg::Vec3& link, Blow b, float radius, Publish&& publish,
           const native_hitpart::Source* source = nullptr) {
    Report out; out.blow = b; out.pick = Scan(m, base, link, b, radius);
    const float from[3] = {link.x, link.y, link.z};
    for (unsigned i = 0; i < out.pick.count; ++i) {
        const Shot s = Fire(m, base, out.pick.items[i].actor, b, from, publish, source);
        out.result[i] = s; ++out.shots[unsigned(s)];
        if (s == Shot::Published || s == Shot::Mismatch) ++out.published;   // the native call ran
    }
    return out;
}
}  // namespace link_runtime::ocarina::song_effects

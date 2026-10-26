#pragma once
// Shared native-hit adapter (Mortal Draw and parry riposte): one native hit per
// adapter-issued token through the attributed knife route. Never writes HP.
//
//   uint64_t t = adapter.Issue();                     // one shared monotonic counter
//   adapter.NativeHit(target, amount, kind, t)        // impact frame, publishes once
//   Resolution r = adapter.Resolve(evaluation)        // GetWepDmVal detour, caller 0x4AD87
//   int dmg = Compose(r, sword)                       // ours XOR the caller's pass-through, never both
//   adapter.Applied(t) / adapter.OutcomeOf(t)         // confirmation for the caller
//   adapter.Tick() each frame; adapter.Cancel(t) on interrupt
//
// Kinds (amount meaning): Ordinary (ignored; native amount, passed through once),
// Lethal (0 or a heavy target's HP cap, mortal_draw_target.h
// LethalCap; an amount that survives the calc factors, the 9999 cap and the
// difficulty scale, or that removes at most the cap: CappedLethalAmount), Fixed
// (exact amount before native factors, 0..9999), Scaled (permille 100..3000 of
// the native amount, e.g. the riposte).
// Non-Ordinary kinds require Native::record (else Disabled, N-2); Overrides() tells.
// Hit-part rules are per kind (SetPartRule): Lethal and Scaled refuse head 5
// (LostHead branch) and core 0x25 by default; Ordinary and Fixed accept any
// valid part. The part itself comes from native_hitpart::Source (ReadFacts).
//
// Route (pinned bio4.exe SHA 19aed4af..., RVAs): publisher 0x1AEEF0 (thunk 0x3224,
// thiscall ECX=actor+0x328, 6 args, RET 0x18) -> per-actor damage block 0x57D30
// -> calc 0x4AD40 calling GetWepDmVal 0x1B1A90 (return 0x4AD87) -> LifeDownSet2
// 0x1B5AF0 -> weapon10 reaction 0x58B90 (table 0x7F3DB0): native death, Plaga
// and rewards stay with the game.
//
// The block can also consume or defer the record without reaching the calc:
// 0x5811E (+0x6A8 path) and 0x5817D (+0x6B0 path) clear +0x32C and kill, 0x581CD
// (R0=1/R1=0x5E with +0x4B0&8) clears +0x32C and returns, and 0x57D33 skips the
// whole block while 0x4352B0 is nonzero (record stays pending). Hence the
// override is bound to the exact record we published (Resolve), and Tick drops
// it as soon as the pending bit clears without a Resolve.
#include "mortal_draw_policy.h"
#include "native_hitpart.h"
#include <cstdint>
#include <cstring>

namespace link_runtime::mortal_draw::damage {
namespace layout {
constexpr uint32_t publisher_rva = 0x1AEEF0, publisher_thunk_rva = 0x3224;
constexpr uint32_t calc_return_rva = 0x4AD87, getwepdmval_rva = 0x1B1A90, lifedown_rva = 0x1B5AF0;
constexpr uint8_t knife_weapon = 0x10, knife_timer = 0x0A;  // knife sweep call 0x286DB9..0x286DBF
// cDmgInfo at actor+0x328 as written by 0x1AEEF0: +0 float 0, +4 flags|1
// (pending, caller test 0x286DA0), +5 timer, +6 weapon (>=0x31 becomes 2),
// +8..+0x10 from XYZ, +0x14 distance, +0x18 hit part.
constexpr uint32_t dmg_info = 0x328, dmg_flag = 0x32C, dmg_timer = 0x32D, dmg_weapon = 0x32E;
constexpr uint32_t dmg_from = 0x330, dmg_distance = 0x33C, dmg_hitpart = 0x340;
constexpr uint32_t hp = 0x324, hp_max = 0x326;
constexpr uint32_t actor_pos = 0x94;   // cModel position (x, y, z), as parry/owner reads it
// YARARE_INFO hit list, qualified in native_hitpart.h (2026-10-12): inline head
// +0x344, next +0x30, parts_no +0x26, len +0x28 (knife passes len as distance, 0x286DA6).
constexpr uint32_t hit_head = native_hitpart::layout::head, hit_next = native_hitpart::layout::next;
constexpr uint32_t hit_parts = native_hitpart::layout::parts_no, hit_len = native_hitpart::layout::len;
// Calc 0x4AD40 clamps its result to 9999 (0x4B085..0x4B092); chainsaw +0x7F0==4
// to 1000 for the knife weapon 0x10 (0x4B061). A chainsaw carrier is heavy
// (mortal_draw_target.h LethalCap): both lethal paths send it the capped amount,
// which this native cap can only lower further.
constexpr int calc_cap = 9999;
// Difficulty byte read by LifeDownSet2 0x1B5E25: [[0x806F3C] + 0x4F98].
constexpr uint32_t global_slot_rva = 0x806F3C, difficulty = 0x4F98;
}  // namespace layout

enum class Kind : uint8_t { Ordinary, Lethal, Fixed, Scaled };
constexpr unsigned kind_count = 4;
// `hitpart` optional: a caller that already resolved Link's blade part (parry)
// passes it with `from`/`distance`; the facts reader must then use that record.
// allow_head (spin-fix-01): with no `hitpart`, the default selection may take the head node when no other
// part is ON (an Ordinary knife hit, as the native knife would land on a standing Ganado's head). The kind's
// PartRule still decides: Lethal and Scaled refuse it (NoHitpart). Core 0x25 is never selected by default.
// repair_len (shield-guard-02): a selected node with a bad len (wading Ganado) is taken and the published distance
// is the squared distance from `from` to the actor (native_hitpart::Rule::repair_len; only for weapons whose calc
// never reads the node len: the sword actions' knife 0x10 and the songs/bash 0x14/0x17).
struct HitTarget {
    uint64_t identity = 0; uint32_t actor = 0;
    uint32_t hitpart = 0; float from[3]{}; float distance = 0;
    bool allow_head = false, repair_len = false;
};
struct Facts {  // read at the impact frame, before publishing
    bool same_identity = false, pending_native = false;
    int16_t hp = 0, hp_max = 0;
    uint8_t level = 0xFF;  // difficulty 0..10; 0xFF unknown -> worst case (10)
    uint32_t hitpart = 0; int16_t parts_no = -1; float hitpart_len = 0;
    bool len_repaired = false;  // shield-guard-02: the node len was bad; hitpart_len is the squared actor distance
    float from[3]{};  // attacker position copied by the publisher
    native_hitpart::Verdict part_verdict = native_hitpart::Verdict::Empty;  // log: why there is no part
};
struct PublishArgs {
    uint32_t dmg_info = 0; uint8_t flags = 0, timer = 0, weapon = 0;
    const float* from = nullptr; float distance = 0; uint32_t hitpart = 0;
};
// The live cDmgInfo as the publisher leaves it; floats compared as bit patterns.
struct Record {
    uint8_t flags = 0, timer = 0, weapon = 0;
    uint32_t from[3]{}, distance = 0, hitpart = 0;
};
enum RecordField : unsigned { kPending = 1, kTimer = 2, kWeapon = 4, kFrom = 8, kDistance = 16, kHitpart = 32 };
inline unsigned Mismatch(const Record& expect, const Record& live) {
    unsigned m = 0;
    if (!(live.flags & 1)) m |= kPending;
    if (live.timer != expect.timer) m |= kTimer;
    if (live.weapon != expect.weapon) m |= kWeapon;
    if (std::memcmp(live.from, expect.from, sizeof(live.from))) m |= kFrom;
    if (live.distance != expect.distance) m |= kDistance;
    if (live.hitpart != expect.hitpart) m |= kHitpart;
    return m;
}
inline Record Expected(const PublishArgs& a) {
    Record r;
    r.flags = uint8_t(a.flags | 1); r.timer = a.timer; r.weapon = a.weapon >= 0x31 ? 2 : a.weapon;
    std::memcpy(r.from, a.from, sizeof(r.from)); std::memcpy(&r.distance, &a.distance, 4); r.hitpart = a.hitpart;
    return r;
}
struct Native {
    void* context = nullptr;
    bool (*facts)(void* context, const HitTarget& target, Facts& out) = nullptr;
    bool (*publish)(void* context, const PublishArgs& args) = nullptr;
    // Live record of `actor` (Resolve and Tick). Null: no override is ever applied.
    bool (*record)(void* context, uint32_t actor, Record& out) = nullptr;
};
enum class Status { Published, Disabled, BadRequest, TokenUsed, Busy, IdentityLost, Dead, PendingNative, NoHitpart, OverCap, PublishFailed };
// Passthrough: our record reached the calc but the target was already dead, a
// Scaled hit met a native 0, or the evaluation was negative: the native amount
// went through (not Applied).
enum class Outcome { None, Pending, Applied, Cancelled, Expired, ConsumedNatively, Superseded, Lost, Passthrough };
struct PartRule { bool allow_head = true, allow_core = true; };
// GetWepDmVal detour input: return address as RVA, EDI actor, weapon byte.
struct Evaluation { uint32_t caller_rva = 0, actor = 0; unsigned weapon = 0; int original = 0, hp_now = 0; };
// owned: this evaluation is the adapter's published hit (token/kind valid).
// final: return `amount` as is; otherwise the caller's pass-through receives it.
// capped: a Lethal hit on a heavy target above its cap (CappedLethalAmount), log only.
struct Resolution { int amount = 0; bool owned = false, final = false, capped = false; uint64_t token = 0; Kind kind = Kind::Ordinary; unsigned mismatch = 0; };

inline Kind KindFor(Effect e) { return e == Effect::LethalStrike ? Kind::Lethal : Kind::Ordinary; }

// LifeDownSet2 enemy scale for IDs 0x10..0x4F (0x1B5E25..0x1B5E71): computed in
// double, stored as float; level <= 5: 2-0.2*lvl, else 1-0.03*(lvl-5).
inline float DifficultyScale(unsigned level) {
    if (level > 10) level = 10;  // unknown or out of range: worst case
    return float(level <= 5 ? 2.0 - 0.2 * level : 1.0 - 0.03 * (level - 5));
}
// Largest HP a single calc result can remove at `level`: the calc caps at 9999
// before LifeDownSet2 scales it (truncation as 0xA6ECF0). Level 10: 8499.
inline int MaxLethalHp(unsigned level) { return int(float(float(layout::calc_cap) * DifficultyScale(level))); }
// After GetWepDmVal the calc may scale by Ganado+0x81C (x0.5556 / x0.4545, +1),
// type2 (x0.667), armoured part 0x39CA0 (/4) and Plaga. The Lethal part rule
// refuses head and core; on the rest 4*hp+4 stays lethal down to a combined 0.25
// (x0.4545 and level 10 give 0.386; type 2's x0.667 on top gives about 0.26, not
// executed in the tests). Since 16-oct both lethal paths admit every
// Ganado but the Merchant, so an armoured part or an emerged Plaga can take the
// combined factor below 0.25 and leave the target alive (the runtime logs it:
// ending_survived, damage resolve). Saturated at 22000:
// x0.4545 still reaches the 9999 cap there, so a larger value could never remove more HP.
constexpr int lethal_saturation = 22000;
inline int LethalAmount(int hp) {
    if (hp <= 0) return 0;
    const int64_t v = int64_t(hp) * 4 + 4;
    return v > lethal_saturation ? lethal_saturation : int(v);
}
// HP that LifeDownSet2 removes for a calc result `amount` at `level` (the same
// float truncation as MaxLethalHp), before the HP floor.
inline int ScaledHp(int amount, unsigned level) { return int(float(float(amount) * DifficultyScale(level))); }
// Heavy-target limit: the largest calc result that LifeDownSet2 turns into at
// most `cap` HP at `level`. An unknown level (> 10) assumes the largest scale
// (x2, level 0), so the cap still holds; at level 5 it is the cap itself, at
// level 10 (x0.85) 1177 for 1000. The calc's own factors after GetWepDmVal
// (Ganado+0x81C, type 2, armoured parts, Plaga, the chainsaw's 1000) only lower it.
inline int CapAmount(int cap, unsigned level) {
    if (cap <= 0) return 0;
    if (cap > layout::calc_cap) cap = layout::calc_cap;
    if (level > 10) level = 0;
    int a = int(double(cap) / double(DifficultyScale(level)));
    if (a > layout::calc_cap) a = layout::calc_cap;
    while (a < layout::calc_cap && ScaledHp(a + 1, level) <= cap) ++a;
    while (a > 0 && ScaledHp(a, level) > cap) --a;
    return a;
}
// Amount for a Lethal hit (mortal_draw_target.h LethalCap gives `cap`): no cap,
// or HP already <= cap, is the ordinary lethal amount (kills); above the cap it
// is CapAmount, so a heavy target loses at most `cap` HP and survives.
inline bool Capped(int hp, int cap) { return cap > 0 && hp > cap; }
inline int CappedLethalAmount(int hp, int cap, unsigned level) {
    return Capped(hp, cap) ? CapAmount(cap, level) : LethalAmount(hp);
}

class Adapter {
public:
    static constexpr unsigned ttl_frames = 3, history = 64;
    Adapter() { rules_[unsigned(Kind::Lethal)] = PartRule{false, false}; rules_[unsigned(Kind::Scaled)] = PartRule{false, false}; }
    void Bind(const Native& n) { native_ = n; }
    void SetPartRule(Kind k, PartRule r) { rules_[unsigned(k)] = r; }
    // True when non-Ordinary kinds can be published (facts, publish and record bound).
    bool Overrides() const { return native_.facts && native_.publish && native_.record; }
    // One shared monotonic counter; tokens are never chosen by callers.
    uint64_t Issue() { return ++issued_; }

    // Impact frame. Revalidates identity/life/pending/part, then publishes once.
    // A rejection before the native call leaves the token usable.
    Status NativeHit(const HitTarget& target, int amount, Kind kind, uint64_t token) {
        if (!native_.facts || !native_.publish) return Status::Disabled;
        // N-2: Lethal/Fixed/Scaled exist only as an override bound to our record;
        // without the record reader they would land as a plain knife hit.
        if (kind != Kind::Ordinary && !native_.record) return Status::Disabled;
        if (unsigned(kind) >= kind_count || !target.identity || !target.actor ||
            ((kind == Kind::Fixed || kind == Kind::Lethal) && (amount < 0 || amount > layout::calc_cap)) ||
            (kind == Kind::Scaled && (amount < 100 || amount > 3000))) return Status::BadRequest;
        if (!token || token > issued_) return Status::BadRequest;  // not issued by this adapter
        if (token + history <= issued_ || Used(token)) return Status::TokenUsed;
        if (pending_) return Status::Busy;
        Facts& f = facts_; f = Facts{};
        if (!native_.facts(native_.context, target, f) || !f.same_identity) return Status::IdentityLost;
        if (f.hp <= 0 || f.hp_max <= 0 || f.hp > f.hp_max) return Status::Dead;
        if (f.pending_native) return Status::PendingNative;
        const PartRule& rule = rules_[unsigned(kind)];
        if (!f.hitpart || f.parts_no < 0 || (f.parts_no == 5 && !rule.allow_head) ||
            (f.parts_no == 0x25 && !rule.allow_core)) return Status::NoHitpart;
        // A capped Lethal above its cap is not meant to kill: never OverCap.
        if (kind == Kind::Lethal && !Capped(f.hp, amount) && f.hp > MaxLethalHp(f.level)) return Status::OverCap;
        Mark(token, Outcome::Pending);  // the native call is about to run: never retry this token
        const PublishArgs args{target.actor + layout::dmg_info, 0, layout::knife_timer, layout::knife_weapon,
                               f.from, f.hitpart_len, f.hitpart};
        if (!native_.publish(native_.context, args)) { Mark(token, Outcome::Lost); return Status::PublishFailed; }
        pending_ = true; actor_ = target.actor; token_ = token; kind_ = kind; amount_ = amount; level_ = f.level; frames_ = 0;
        expect_ = Expected(args);
        return Status::Published;
    }

    // Game thread, GetWepDmVal detour. Only the Ganado calc caller and only the
    // exact record we published; anything else is native (owned=false).
    Resolution Resolve(const Evaluation& e) {
        Resolution r; r.amount = e.original;
        if (!pending_ || e.caller_rva != layout::calc_return_rva || e.actor != actor_ ||
            e.weapon != layout::knife_weapon) return r;
        Record live;
        if (!native_.record || !native_.record(native_.context, e.actor, live)) { Finish(Outcome::Lost); return r; }
        r.mismatch = Mismatch(expect_, live);
        if (r.mismatch) { Finish(Outcome::Superseded); return r; }  // our record is gone: native amount
        r.owned = true; r.token = token_; r.kind = kind_;
        // Dead target or negative evaluation: the native amount passes through and
        // the token is NOT counted as Applied (review I2 minor 3).
        // Round 1 (M3): a Scaled hit on a native 0 also passes through (0 damage, not Applied).
        if (e.hp_now <= 0 || e.original < 0 || (kind_ == Kind::Scaled && e.original == 0)) {
            r.final = true; Finish(Outcome::Passthrough); return r;
        }
        Finish(Outcome::Applied);
        switch (kind_) {
        case Kind::Ordinary: r.final = false; break;  // native knife amount, passed through once
        case Kind::Lethal: {  // amount_: heavy cap (0 = none); never below the native knife amount
            const int l = CappedLethalAmount(e.hp_now, amount_, level_);
            r.capped = Capped(e.hp_now, amount_); r.amount = l > e.original ? l : e.original; r.final = true; break;
        }
        case Kind::Fixed: r.amount = amount_; r.final = true; break;
        case Kind::Scaled: {
            // Mirrors native 0 (review I2 minor 1); the min-1 clamp only applies to a positive hit.
            const int64_t v = (int64_t(e.original) * amount_ + 500) / 1000;
            r.amount = v < 1 ? 1 : v > layout::calc_cap ? layout::calc_cap : int(v); r.final = true; break;
        }
        }
        return r;
    }
    // Once per game frame. Drops the override when the native block consumed the
    // record without a Resolve (bit cleared), when the record was replaced, or on TTL.
    void Tick() {
        if (!pending_) return;
        Record live;
        if (!native_.record || !native_.record(native_.context, actor_, live)) { Finish(Outcome::Lost); return; }
        if (!(live.flags & 1)) { Finish(Outcome::ConsumedNatively); return; }
        if (Mismatch(expect_, live) & ~unsigned(kTimer)) { Finish(Outcome::Superseded); return; }
        if (++frames_ >= ttl_frames) Finish(Outcome::Expired);
    }
    // Damage/grab/death/scene/target loss. A hit already published stays native
    // (ordinary amount): the adapter only withdraws its own override.
    void Cancel(uint64_t token) { if (pending_ && token == token_) Finish(Outcome::Cancelled); }
    void Reset() { if (pending_) Finish(Outcome::Cancelled); }  // token history survives
    bool Pending() const { return pending_; }
    uint64_t PendingToken() const { return pending_ ? token_ : 0; }
    Outcome Last() const { return last_; }
    Outcome OutcomeOf(uint64_t token) const {
        if (!token || token > issued_ || token + history <= issued_) return Outcome::None;
        const Slot& s = slots_[token % history];
        return s.token == token ? s.outcome : Outcome::None;
    }
    bool Applied(uint64_t token) const { return OutcomeOf(token) == Outcome::Applied; }
    // The facts the last NativeHit read (cleared at each call; empty when it stopped before the read). Log/diagnosis.
    const Facts& LastFacts() const { return facts_; }

private:
    struct Slot { uint64_t token = 0; Outcome outcome = Outcome::None; };
    bool Used(uint64_t token) const { return slots_[token % history].token == token; }
    void Mark(uint64_t token, Outcome o) { slots_[token % history] = Slot{token, o}; }
    void Finish(Outcome o) { pending_ = false; last_ = o; Mark(token_, o); }
    Native native_;
    PartRule rules_[kind_count];
    Slot slots_[history];
    uint64_t issued_ = 0, token_ = 0;
    uint32_t actor_ = 0; Kind kind_ = Kind::Ordinary; int amount_ = 0; unsigned frames_ = 0;
    uint8_t level_ = 0xFF;  // difficulty read at the publish (Facts::level), for CapAmount
    Record expect_;
    Facts facts_;
    bool pending_ = false; Outcome last_ = Outcome::None;
};

// Detour composition: adapter-owned final amounts are returned as is and `sword` is
// skipped; otherwise `sword(amount)` runs exactly once. The runtime passes the identity
// since the knife multiplier (SwordDamage) was removed on 20-oct-2026.
template <class Sword> int Compose(const Resolution& r, Sword&& sword) { return r.final ? r.amount : sword(r.amount); }

template <class Reader> bool ReadRecord(Reader& reader, uint32_t actor, Record& r) {
    r = Record{};
    return reader.Read(actor + layout::dmg_flag, &r.flags, 1) && reader.Read(actor + layout::dmg_timer, &r.timer, 1) &&
           reader.Read(actor + layout::dmg_weapon, &r.weapon, 1) && reader.Read(actor + layout::dmg_from, r.from, 12) &&
           reader.Read(actor + layout::dmg_distance, &r.distance, 4) && reader.Read(actor + layout::dmg_hitpart, &r.hitpart, 4);
}
// Native facts for `actor`. Identity is decided by the caller (target::Revalidate
// for Mortal Draw) and passed in; it is not read here. The hit part comes from
// native_hitpart::Source: with `t.hitpart` set it must be a listed, ON node of
// this actor (its parts_no/len are read; the kind's PartRule decides head/core);
// otherwise the first ON node that is not head/core. Without `source` the
// manager is read from the image (ReadPool) and the actor must be a live em10
// slot (be_flag, vtable). Any refusal leaves
// f.hitpart = 0 (NoHitpart) with the reason in f.part_verdict; only an
// unreadable actor fails the read.
template <class Reader>
bool ReadFacts(Reader& reader, uint32_t image_base, const HitTarget& t, bool same_identity, const float (&from)[3], Facts& f,
               const native_hitpart::Source* source = nullptr) {
    f = Facts{};
    f.same_identity = same_identity;
    uint8_t pending = 0;
    if (!reader.Read(t.actor + layout::hp, &f.hp, 2) || !reader.Read(t.actor + layout::hp_max, &f.hp_max, 2) ||
        !reader.Read(t.actor + layout::dmg_flag, &pending, 1)) return false;
    f.pending_native = (pending & 1) != 0;
    uint32_t global = 0; uint8_t level = 0xFF;
    if (reader.Read(image_base + layout::global_slot_rva, &global, 4) && global &&
        reader.Read(global + layout::difficulty, &level, 1)) f.level = level;
    std::memcpy(f.from, from, sizeof(f.from));
    // Fix round 1 (I1): the source always carries the live enemy manager; an
    // unreadable manager refuses the part (NoManager) instead of skipping the slot test.
    const native_hitpart::Source managed = source ? *source : native_hitpart::ManagedSource(reader, image_base);
    const native_hitpart::Source& src = managed;
    native_hitpart::Node n;
    f.part_verdict = t.hitpart ? src.Validate(reader, t.actor, t.hitpart, native_hitpart::Rule{true, true, t.repair_len}, n)
                               : src.Select(reader, t.actor, native_hitpart::Rule{false, false, t.repair_len}, n);
    // Body parts first; the head only when it is the one ON part left (allow_head, Excluded = head/core only).
    if (!t.hitpart && t.allow_head && f.part_verdict == native_hitpart::Verdict::Excluded)
        f.part_verdict = src.Select(reader, t.actor, native_hitpart::Rule{true, false, t.repair_len}, n);
    if (f.part_verdict == native_hitpart::Verdict::ReadFailed) return false;
    if (f.part_verdict != native_hitpart::Verdict::Ok) return true;
    f.hitpart = n.address; f.parts_no = n.parts_no;
    f.hitpart_len = t.hitpart && t.distance > 0 ? t.distance : n.len;
    if (n.len_repaired && !(t.hitpart && t.distance > 0)) {
        float pos[3]{};
        if (!reader.Read(t.actor + layout::actor_pos, pos, 12)) return false;
        const float dx = pos[0] - from[0], dy = pos[1] - from[1], dz = pos[2] - from[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        f.hitpart_len = std::isfinite(d2) ? d2 : 0.f;
        f.len_repaired = true;
    }
    return true;
}

#if defined(_M_IX86)
// thiscall: ECX=dmg_info, stack flags,timer,weapon,from,distance,hitpart; callee RET 0x18.
inline void CallPublisher(uint32_t function, const PublishArgs& a) {
    const uint32_t dmg = a.dmg_info, flags = a.flags, timer = a.timer, weapon = a.weapon, hitpart = a.hitpart;
    const float* from = a.from; const float distance = a.distance;
    __asm {
        push hitpart
        push distance
        push from
        push weapon
        push timer
        push flags
        mov ecx, dmg
        call function
    }
}
#endif
}  // namespace link_runtime::mortal_draw::damage

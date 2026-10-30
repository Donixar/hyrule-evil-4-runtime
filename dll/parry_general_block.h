#pragma once
// Tier-2 parry, "general block" (docs/parry-two-tier-2026-10-12.md). Same pre-effects site and
// transport as tier 1 (parry_pre_effects.h): a true decision only makes the sphere test report
// "no hit" (EAX=0). Tier 2 writes nothing in the game: no latch, HP, animation or enemy reaction.
// This header is the pure policy (producer class, descriptor rules, follow-up guard, gate order)
// with injected sources, like defense_runtime::detail::Approve. The native sources and the
// effects live in the runtime (link_runtime.cpp); exceptions and reentry fail to native damage.
#include "parry_general_producers.h"
#include "parry_pre_effects.h"
#include "parry_front_native.h"
#include "native_pins.h"
#include <cmath>
#include <cstring>

namespace link_runtime::parry::general {
enum class Mode : uint8_t {Off, Active, Observe};
// Allowed passes the producer/descriptor gates; every other value lets native damage through.
enum class Verdict : uint8_t {Allowed, Boss, Thrown, Excluded, Unlisted, Unpinned, Lethal, NoPower, BadDescriptor};
struct Settings {
    Mode mode = Mode::Off;
    bool bosses = true;   // class B (Donald 12-oct: bosses too); grabs/lethal/power 0 still pass
    bool thrown = true;   // class T thrown weapons (Donald 12-oct)
};
// ATK_INFO as the classifier reads it: radius +0, dm_type +4, power +8, flag +0xA.
struct Descriptor {float radius = 0; uint32_t dm_type = 0; uint16_t power = 0, flag = 0; uint32_t tail = 0;};
static_assert(sizeof(Descriptor) == 16, "ATK_INFO is 16 bytes");
inline bool Same(const Descriptor& a, const Descriptor& b) {return !std::memcmp(&a, &b, sizeof(a));}
// Chainsaw (Donald 22-oct: parry almost everything): Em10AtkTbl[12] {500, PL_DM_AUTO, 9999, flag 8},
// from em10AtkCk (CSawWalkAtk) or em10CsawHitCk, is blocked like any melee hit; blocking the sphere
// also skips the C_SawHit kill. The same lethal descriptor from any other producer still lands.
inline constexpr uint32_t kEm10AtkCk = 0x55BE0, kEm10CsawHitCk = 0x2C0D0;
inline bool Chainsaw(uint32_t function, const Descriptor& d) {
    return (function == kEm10AtkCk || function == kEm10CsawHitCk) && d.radius == 500.f && d.dm_type == 8 && d.power == 9999 && d.flag == 8;
}
// index: Find(ret_rva); live: that producer's pin matched at startup.
inline Verdict Classify(size_t index, bool live, const Descriptor& d, const Settings& s) {
    if (index >= kProducerCount) return Verdict::Unlisted;
    const char cls = kProducers[index].cls;
    if (cls == 'X') return Verdict::Excluded;
    if (cls == 'C') return Verdict::Unlisted;
    if (cls == 'B' && !s.bosses) return Verdict::Boss;
    if (cls == 'T' && !s.thrown) return Verdict::Thrown;
    if (cls != 'A' && cls != 'B' && cls != 'T') return Verdict::Unlisted;
    if (!live) return Verdict::Unpinned;
    if (!std::isfinite(d.radius) || d.radius <= 0 || d.radius > 100000.f) return Verdict::BadDescriptor;
    if ((d.power >= 9999 || (d.flag & 8)) && !Chainsaw(kProducers[index].function, d)) return Verdict::Lethal;   // decapitation, head bite, grab rule
    if (d.power == 0) return Verdict::NoPower;                     // grab / push starters
    return Verdict::Allowed;
}
inline const char* VerdictName(Verdict v) {
    switch (v) {
    case Verdict::Allowed: return "allowed"; case Verdict::Boss: return "boss"; case Verdict::Thrown: return "thrown";
    case Verdict::Excluded: return "excluded"; case Verdict::Unlisted: return "unlisted"; case Verdict::Unpinned: return "unpinned";
    case Verdict::Lethal: return "lethal"; case Verdict::NoPower: return "power0"; default: return "descriptor";
    }
}
// Follow-up of one blocked swing (§2.4): later samples of the same swing are cancelled silently,
// without input, sparks or voice. Same producer function; same owner when both are known, else
// the sample within kNear of the previous one; gap <= kGapMs, hard cap kMaxMs from the block.
// Never renewed by input; never chains to another producer.
// Chain and flail swings (castle Zealots, 24-oct): tier 2 leaves the attacker's swing running, and a
// chain sweeps out and back, so the same swing touches Link again 200-700 ms after the block with no
// contact in between. Under the 100 ms gap that second contact was a new, unpressed hit: the parry
// showed (sparks, voice) and the damage landed anyway. When the owner is known and its routine
// (cEm r0/r1, [em+0xFC]/[em+0xFD]) is still the one it had at the block, the attacker is still in
// that attack: the follow-up then allows kSwingGapMs between contacts, up to kSwingMaxMs. 28-oct
// (play-20261028/tests/05-flail.log): the chain came back behind Link 453 ms after the block, past the
// old 400 ms gap, and landed; the routine check still ends the follow on a new swing.
struct Swing {bool known = false; uint8_t routine[2]{};};
inline bool SameSwing(const Swing& a, const Swing& b) {return a.known && b.known && a.routine[0] == b.routine[0] && a.routine[1] == b.routine[1];}
struct Follow {
    static constexpr uint64_t kGapMs = 100, kMaxMs = 500;
    static constexpr uint64_t kSwingGapMs = 700, kSwingMaxMs = 1500;
    static constexpr float kNear = 2500.f;
    bool armed = false;
    uint32_t function = 0, owner = 0;
    Swing swing{};
    float pos[3]{};
    uint64_t start = 0, last = 0;
    void Arm(uint32_t fn, uint32_t who, const float* p, uint64_t now, const Swing& sw = Swing{}) {
        armed = true; function = fn; owner = who; swing = who ? sw : Swing{}; std::memcpy(pos, p, sizeof(pos)); start = last = now;
    }
    bool Within(uint64_t now, bool extended) const {
        return now >= last && now - last <= (extended ? kSwingGapMs : kGapMs) && now - start <= (extended ? kSwingMaxMs : kMaxMs);
    }
    // The longer chain/flail bounds: known equal owner, attacker still in the routine of the block.
    bool Extended(uint32_t who, const Swing* sw) const {return owner && who == owner && sw && SameSwing(swing, *sw);}
    // by_owner: only a match by a known, equal owner (the follow-ups that skip the pose and position gates).
    // sw: the owner's current routine (null or unknown: the short bounds only).
    bool Matches(uint32_t fn, uint32_t who, const float* p, uint64_t now, bool by_owner = false, const Swing* sw = nullptr) {
        // A positively observed routine change ends this attack even inside the short gap.
        // Retire it permanently: returning to the same routine later is another swing.
        if (owner && who == owner && sw && swing.known && sw->known && !SameSwing(swing, *sw)) {
            armed = false; return false;
        }
        if (!armed || fn != function || !Within(now, Extended(who, sw))) return false;
        if (owner && who) return owner == who;
        if (by_owner) return false;
        const float dx = p[0] - pos[0], dy = p[1] - pos[1], dz = p[2] - pos[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        return std::isfinite(d2) && d2 <= kNear * kNear;
    }
    // A match that the short bounds alone would have refused (log counter).
    bool Long(uint64_t now) const {return armed && !Within(now, false);}
    void Touch(const float* p, uint64_t now) {std::memcpy(pos, p, sizeof(pos)); last = now;}
    // Disarms only once even the longer bounds have run out (they apply only if the routine still matches).
    void Expire(uint64_t now) {if (armed && !Within(now, owner && swing.known)) armed = false;}
};
// Post-parry grace (Donald 12-oct): after a committed tier-1 parry or a press-consuming tier-2 block,
// ParryGraceMs of GAME time (60 Hz frames of game_clock.h: the same at 60 and 165 FPS) in which another
// qualifying frontal hit (every tier-2 gate except the press) is blocked through the tier-2 path, as part
// of the same parry (sparks, no second voice or reaction). Not spammable: only Open() starts a window, the
// caller calls it only after a successful parry/block, it never renews or extends an open window, and a
// grace block never opens one (no chaining). Later samples of a grace-blocked swing are followed silently
// in `swings` (own Follow slots, so the opener's follow-up is kept), as is a follow-up a later press block displaced.
inline unsigned GraceMs(unsigned raw) {return raw <= 1000 ? raw : 0;}   // ParryGraceMs: 0..1000, else off
struct Grace {
    static constexpr unsigned kSlots = 4;
    static constexpr double kEps = 1e-6;
    double window = 0;              // frames; 0 = off
    bool open = false;
    double opened = 0, until = 0;
    uint32_t serial = 0;            // the opening parry (log)
    unsigned blocked = 0;           // grace blocks in this window
    Follow swings[kSlots]{};
    static double Frames(unsigned ms) {return ms * 60.0 / 1000.0;}
    void Configure(unsigned ms) {window = Frames(GraceMs(ms));}
    bool Covers(double now) const {return open && now >= opened - kEps && now <= until + kEps;}
    // True once, when an open window has run out (or the clock went back).
    bool Expire(double now) {
        if (open && !Covers(now)) {open = false; return true;}
        return false;
    }
    bool Open(double now, uint32_t by) {
        if (window <= 0 || Covers(now)) return false;
        open = true; opened = now; until = now + window; serial = by; blocked = 0;
        return true;
    }
    unsigned Free(uint64_t now_ms) {   // an expired slot, else the oldest
        unsigned slot = 0;
        for (unsigned i = 0; i < kSlots; ++i) {
            swings[i].Expire(now_ms);
            if (!swings[i].armed) {slot = i; break;}
            if (swings[i].start < swings[slot].start) slot = i;
        }
        return slot;
    }
    void ArmSwing(uint32_t fn, uint32_t who, const float* p, uint64_t now_ms, const Swing& sw = Swing{}) {swings[Free(now_ms)].Arm(fn, who, p, now_ms, sw);}
    // A press block re-arms the single Follow: a still-live follow-up of another swing (two chain/flail attackers) moves here.
    void Park(const Follow& f, uint64_t now_ms) {if (f.armed && f.Within(now_ms, f.owner && f.swing.known)) swings[Free(now_ms)] = f;}
    bool FollowSwing(uint32_t fn, uint32_t who, const float* p, uint64_t now_ms, bool by_owner = false, const Swing* sw = nullptr,
                     bool* long_follow = nullptr) {
        for (auto& s : swings) {
            s.Expire(now_ms);
            if (s.Matches(fn, who, p, now_ms, by_owner, sw)) {
                if (long_follow) *long_follow = s.Long(now_ms);
                s.Touch(p, now_ms); return true;
            }
        }
        return false;
    }
};
enum class Reason : uint8_t {Blocked, Off, Structure, Descriptor, Producer, InputBlocked, Pose, Front, Rear, Side, Window, Fault, Reentry};
inline const char* ReasonName(Reason r) {
    switch (r) {
    case Reason::Blocked: return "blocked"; case Reason::Off: return "off"; case Reason::Structure: return "structure";
    case Reason::Descriptor: return "descriptor"; case Reason::Producer: return "producer"; case Reason::InputBlocked: return "input_blocked";
    case Reason::Pose: return "pose"; case Reason::Front: return "front"; case Reason::Rear: return "rear"; case Reason::Side: return "side";
    case Reason::Window: return "window"; case Reason::Fault: return "fault"; default: return "reentry";
    }
}
// Front evidence: the native classifier formula (front_math, native RotVector), the sample and
// Link's position, and `half`, the sample's offset along Link's forward (XZ).
struct Front {bool frontal = false; float dot = 0, half = 0; float sample[3]{}, player[3]{};};
// Where the sample is around Link (XZ, forward from the native RotVector). Front and sides parry
// (Donald 22-oct, the castle flail sweeps hit from the side); the back arc, within 60 degrees of
// straight behind, stays with the rear shield. Too close to Link's centre for an angle: the native
// direction (`frontal`) decides.
inline constexpr float kBackCos = -0.5f;   // 120 degrees from the front
inline constexpr float kCentre = 50.f;
inline bool Parryable(const Front& f) {
    const float dx = f.sample[0] - f.player[0], dz = f.sample[2] - f.player[2];
    const float r = std::sqrt(dx * dx + dz * dz);
    if (!std::isfinite(r) || !std::isfinite(f.half)) return false;
    if (r < kCentre) return f.frontal;
    return f.half / r > kBackCos;
}
struct Outcome {
    bool cancel = false;     // Active only: the decision returned to the transport
    bool consumed = false, follow = false, handoff = false;
    bool pose_fallback = false; // fresh held press plus a stable known sword resource, without blade geometry
    bool long_follow = false; // a follow-up only the chain/flail bounds (same owner, same routine) kept
    uint64_t follow_gap_ms = 0, follow_age_ms = 0; // follow-up of the main Follow: gap since its last contact, age since the block (log only)
    bool would_block = false; // Observe only: every gate passed and a press was armed (not consumed)
    bool grace = false;       // blocked by the post-parry grace window (or a follow-up kept in Grace::swings)
    bool grace_opened = false, grace_expired = false;
    Reason reason = Reason::Off;
    Verdict verdict = Verdict::Unlisted;
    uint32_t ret_rva = 0, function = 0, owner = 0;
    char cls = '?';
    Descriptor descriptor{};
    Front front{};
};
// em10CsawHitCk (0x2C0D0) reads its Ganado from ESI ([esi+0x610] chainsaw model, [esi+0x98]) and keeps it there
// across its three EmAtkHitCk calls, so ESI at the wrapper entry is the swing's owner. Accepted only when that
// actor carries the chainsaw (+0x7F0 == 4, mortal_draw_target.h). Without it the swing's later checks had no
// owner: a block at 0x2C249 and the next check 31 ms later at Link's side passed as `rear` (power 9999, Donald
// 28-oct). With the owner they are the blocked swing's follow-ups.
inline bool ChainsawProducer(uint32_t ret) {return ret == 0x2C178 || ret == 0x2C249 || ret == 0x2C2CA;}
template <class Mem> uint32_t ChainsawOwner(Mem& m, uint32_t ret, uint32_t esi) {
    uint8_t weapon = 0;
    return ChainsawProducer(ret) && esi && m.Read(esi + 0x7F0, &weapon, 1) && weapon == 4 ? esi : 0;
}
// Gate order (§2.6), cheapest to irreversible. `handoff`: tier 1 consumed this very press in this
// invocation and wrote nothing (Approved + NotCommitted), so tier 2 uses it without consuming again.
// Sources: bool Invocation(c); uint32_t ImageBase(); bool Live(size_t); bool Descriptor(uint32_t,
// Descriptor&); bool Blocked(); bool Pose(); bool Guard(bool refresh_input); bool Front(c, Front&); uint32_t Owner(c);
// bool Routine(owner, uint8_t (&)[2]); uint64_t Now(); bool Refresh(); bool Consume(); bool Peek().
template <class S> Outcome Decide(S& s, const Candidate& c, const Settings& set, Follow& follow, bool handoff,
                                   Grace* grace = nullptr, double game_now = 0) {
    Outcome o;
    if (set.mode == Mode::Off) return o;
    o.reason = Reason::Structure;
    if (!c.body_part || !s.Invocation(c)) return o;
    const uint32_t base = s.ImageBase();
    o.ret_rva = c.outer_caller >= base ? c.outer_caller - base : 0;
    const size_t index = Find(o.ret_rva);
    if (index < kProducerCount) {o.function = kProducers[index].function; o.cls = kProducers[index].cls;}
    Descriptor again{};
    o.reason = Reason::Descriptor;
    if (!s.Descriptor(c.attack, o.descriptor) || !s.Descriptor(c.attack, again) || !Same(o.descriptor, again)) return o;
    o.verdict = Classify(index, index < kProducerCount && s.Live(index), o.descriptor, set);
    o.reason = Reason::Producer;
    if (o.verdict != Verdict::Allowed) return o;
    o.reason = Reason::InputBlocked;
    if (s.Blocked()) return o;
    const bool posed = s.Pose();
    const bool located = s.Front(c, o.front);
    const uint64_t now = s.Now();
    // Later samples of an already blocked swing are that swing, wherever they land: a wide swing
    // (castle flail Zealots) sweeps past Link's side or back after the block, and the parry
    // reaction can hide the sword pose. So a follow-up by its known owner comes before the pose
    // and position gates; one matched by distance only (owner unknown) waits for them, so another
    // attacker of the same kind at Link's back is never taken for it.
    Swing swing{};
    auto follow_up = [&](bool by_owner) {
        follow.Expire(now);
        if (follow.Matches(o.function, o.owner, o.front.sample, now, by_owner, &swing)) {
            o.follow = true; o.reason = Reason::Blocked; o.long_follow = follow.Long(now);
            o.follow_gap_ms = now - follow.last; o.follow_age_ms = now - follow.start;
            if (set.mode == Mode::Active) {follow.Touch(o.front.sample, now); o.cancel = true;}
            return true;
        }
        if (grace && set.mode == Mode::Active && grace->FollowSwing(o.function, o.owner, o.front.sample, now, by_owner, &swing, &o.long_follow)) {
            o.follow = o.grace = true; o.cancel = true; o.reason = Reason::Blocked;
            return true;
        }
        return false;
    };
    if (located) {
        o.owner = s.Owner(c);
        if (o.owner) swing.known = s.Routine(o.owner, swing.routine);
        if (follow_up(true)) return o;
    }
    o.reason = Reason::Pose;
    if (!posed) {
        // A hidden/non-rigid draw pose is not blade geometry. Admit only a new held press,
        // with the same stable player/model/resource guards; never borrow grace or a handoff.
        if (!located || !Parryable(o.front) || handoff || !s.Guard(set.mode == Mode::Active)) return o;
        o.pose_fallback = true;
    }
    o.reason = Reason::Front;
    if (!located) return o;
    o.reason = Reason::Rear;
    if (!Parryable(o.front)) return o;
    if (!o.pose_fallback && follow_up(false)) return o;
    if (!o.pose_fallback && grace && set.mode == Mode::Active) {
        o.grace_expired = grace->Expire(game_now);
        if (grace->Covers(game_now)) {   // no press read or consumed; never reopens the window
            grace->ArmSwing(o.function, o.owner, o.front.sample, now, swing);
            ++grace->blocked;
            o.grace = true; o.cancel = true; o.reason = Reason::Blocked;
            return o;
        }
    }
    if (set.mode == Mode::Observe) {
        o.would_block = handoff || s.Peek();
        o.reason = o.would_block ? Reason::Blocked : Reason::Window;
        return o;
    }
    o.reason = Reason::Window;
    if (!handoff && !(s.Refresh() && s.Consume())) return o;
    if (grace) grace->Park(follow, now);
    follow.Arm(o.function, o.owner, o.front.sample, now, swing);
    o.cancel = true; o.consumed = !handoff; o.handoff = handoff; o.reason = Reason::Blocked;
    if (grace) o.grace_opened = grace->Open(game_now, 0);
    return o;
}
// Startup qualification (worker, before the resolver is installed). Tier 2 stays off unless the
// pre-effects site (0x1BC4A6) and the native front (RotVector 0x2D06E0 + guard) qualify. Each
// producer is then live only if its 16-byte pin (ending with the E8 into the wrapper) matches
// at base+RVA after image_relocation.h undoes the loader's fixups: a failed pin turns off only
// that producer. Mem: bool Read(uint32_t address, void*, size_t) over the mapped module.
struct Code {
    bool verified = false;
    uint32_t image_base = 0;
    bool live[kProducerCount]{};
    unsigned live_a = 0, total_a = 0, live_b = 0, total_b = 0, live_t = 0, total_t = 0;
};
template <class Mem> Code Qualify(Mem& m, const unsigned char* image, size_t size, const shield::NativeAccess& a) {
    Code c;
    if (!image || !parry::FindCode(image, size, a).verified || !front_native::FindCode(image, size, a).verified) return c;
    for (size_t i = 0; i < kProducerCount; ++i) {
        const auto& p = kProducers[i];
        const bool counted = p.cls == 'A' || p.cls == 'B' || p.cls == 'T';
        if (!counted) continue;
        c.live[i] = p.ret >= 16 && native_pins::Matches(m, a.image_base, p.ret - 16, p.pin);
        unsigned& live = p.cls == 'A' ? c.live_a : p.cls == 'B' ? c.live_b : c.live_t;
        unsigned& total = p.cls == 'A' ? c.total_a : p.cls == 'B' ? c.total_b : c.total_t;
        ++total; live += c.live[i];
    }
    c.verified = true; c.image_base = a.image_base;
    return c;
}
// Thrown weapons (class T, cEmWep routines 5 Throw and 0xA ThrowScythe). Blocking only the
// damage would leave the weapon flying through Link (the scythe natively does). On a block the
// runtime writes the same damage record Leon's knife leaves on the weapon (cDmgInfo::set 0x1AEEF0
// at obj+0x328 with flag 0, timer 10, weapon 0x10, from, dist 0, part 0), flag byte last. Next
// frame cEmWep::move 0x300240 runs emWepDmCk 0x2FFCE0 before the routine table: knife id 0x10
// is not filtered, routines 5/0xA jump to 0x300001, which knocks the weapon away from `from`
// with setFall 0x2FB640 and the native sound/effect. Pins below are fail-closed: without them a
// thrown weapon is still blocked (no damage) but not deflected, and the log says so.
namespace thrown {
inline constexpr uint32_t kThrowReturn = 0x2FC952, kScytheReturn = 0x2FCEE6, kVtable = 0x716ED0;
inline constexpr native_pins::Pin kDeflect[] = {
    // emWepDmCk: cookie, record flag test/clear, ignored weapon ids (0x10 not among them).
    {0x2FFCE0, "558bec83ec5ca1105ec30033c58945fc53568bf133db389e2c0300000f84f20300008a862e030000889e2c0300003c140f84de0300003c16"},
    {0x2FFD18, "0f84d60300003c170f84ce0300003c2a0f84c60300003c0e0f84be0300003c190f84b60300003c1f0f84ae0300003c200f84a6030000576a"},
    // hp=0, routine switch on +0xFD through the byte/dword tables.
    {0x2FFD50, "01e893bfd0ff33c0668986240300000fb686fd00000083f80c0f87920200000fb6881c017000ff248d04017000"},
    {0x300104, "5dff6f007dfd6f00a4fd6f00cbfd6f0078fe6f0001007000"},
    {0x30011C, "00050005050505010203050504"},
    // knockback from PosFrom (+0x330) and setFall(0,&v,g).
    {0x300001, "8d9630030000528dbe9400000057e87658d0ffd95da4d945a483c404d91c248d45cc6a7950e8d2d0d0ffe88519d0ff"},
    {0x30007B, "8d4db451538bcee8c289d0ff"},
    {0x2FB640, "558bec83ec48a1105ec30033c58945fcd9ee538b5d0c56578bf133ff89bed801"},
    {0x8A49, "e9f22b2f00"},
    // cEmWep::move calls emWepDmCk before the routine table.
    {0x300240, "558bec83ec14a1105ec30033c58945fc568bf1e888faffff0fb686fc0000008b0c85b85dc10056ffd18b"},
    // cDmgInfo::set, whose field layout the record write reproduces.
    {0x1AEEF0, "558bec8a4508d9ee0c01568bf18a4d0cd91ed945188846048a4510884e058b4d"}};
template <class Mem> bool Qualify(Mem& m, uint32_t base) {return native_pins::All(m, base, kDeflect);}
// The weapon object from the arguments alone: current == obj+0x94, old == obj+0x110,
// [obj+0x620] == attack, cEmWep vtable, routine 1/5 (Throw) or 1/0xA (ThrowScythe).
template <class Mem> bool Identify(Mem& m, uint32_t base, const Candidate& c, uint32_t ret_rva, uint32_t& object) {
    object = 0;
    if ((ret_rva != kThrowReturn && ret_rva != kScytheReturn) || c.current < 0x10000 + 0x94) return false;
    const uint32_t obj = c.current - 0x94;
    uint32_t vtable = 0, attack = 0; unsigned char routine[2]{};
    if (c.old != obj + 0x110 || !m.Read(obj, &vtable, 4) || vtable != base + kVtable || !m.Read(obj + 0x620, &attack, 4) ||
        attack != c.attack || !m.Read(obj + 0xFC, routine, 2) || routine[0] != 1 || routine[1] != (ret_rva == kThrowReturn ? 5 : 0xA))
        return false;
    object = obj; return true;
}
// The knife's damage record; refused when a record is already pending. Flag byte written last.
template <class Mem> bool Deflect(Mem& m, uint32_t object, const float* from) {
    unsigned char pending = 1;
    if (!object || !from || !m.Read(object + 0x32C, &pending, 1) || pending) return false;
    for (unsigned i = 0; i < 3; ++i) if (!std::isfinite(from[i])) return false;
    const float zero = 0; const uint32_t part = 0; const unsigned char timer = 10, weapon = 0x10, flag = 1;
    return m.Write(object + 0x328, &zero, 4) && m.Write(object + 0x330, from, 12) && m.Write(object + 0x33C, &zero, 4) &&
           m.Write(object + 0x340, &part, 4) && m.Write(object + 0x32D, &timer, 1) && m.Write(object + 0x32E, &weapon, 1) &&
           m.Write(object + 0x32C, &flag, 1);
}
}  // namespace thrown
// [LinkRuntime] keys. get(key, out, count) fills `out` with the raw value ("" when missing).
// ParryGeneralBlock: missing/0 off, 1 active (needs ParryActive=1), 2 observe only (needs the
// parry transport: ParryObserve=1 or ParryActive=1). ParryGeneralBosses / ParryGeneralThrown:
// missing = 1, exact 0 or 1. Any invalid value or unmet dependency counts a problem and leaves
// tier 2 off.
template <class Get> Settings ReadSettings(Get&& get, bool parry_active, bool parry_transport, unsigned& problems) {
    Settings s; wchar_t v[32]{};
    auto read = [&](const wchar_t* key, int fallback) {
        v[0] = 0; get(key, v, 32);
        if (!v[0]) return fallback;
        if ((v[0] == L'0' || v[0] == L'1' || v[0] == L'2') && !v[1]) return int(v[0] - L'0');
        return -1;
    };
    const int mode = read(L"ParryGeneralBlock", 0), bosses = read(L"ParryGeneralBosses", 1), thrown_on = read(L"ParryGeneralThrown", 1);
    if (mode < 0 || bosses < 0 || bosses > 1 || thrown_on < 0 || thrown_on > 1) {++problems; return Settings{};}
    if ((mode == 1 && !parry_active) || (mode == 2 && !parry_transport)) {++problems; return Settings{};}
    s.mode = mode == 1 ? Mode::Active : mode == 2 ? Mode::Observe : Mode::Off;
    s.bosses = bosses == 1; s.thrown = thrown_on == 1;
    return s;
}
// Precedence (section 2.1). Tier 1 runs first; tier 2 only after a tier-1 NotCommitted (or with
// tier 1 inactive). Committed ends the hit; PartialFailure keeps the damage with no tier 2 for that
// invocation. Spent: tier 1 reached Approved (consumed the press) and still wrote nothing, so
// tier 2 may use that same press (handoff) without consuming again.
enum class TierOne : uint8_t {Inactive, Committed, Partial, NotCommitted, NotCommittedSpent};
inline bool TierTwoRuns(TierOne r, bool& handoff) {
    handoff = r == TierOne::NotCommittedSpent;
    return r == TierOne::Inactive || r == TierOne::NotCommitted || r == TierOne::NotCommittedSpent;
}
// One decision per thread at a time. A nested call (reentry through a native read or callback)
// returns false and taints the outer one, which then also returns false. Any SEH exception from a
// source leaves native damage. LastError is preserved for the native caller.
inline __declspec(thread) bool deciding = false, tainted = false;
template <class S> bool Run(S& s, const Candidate& c, const Settings& set, Follow& follow, bool handoff, Outcome& out,
                             Grace* grace = nullptr, double game_now = 0) {
    const DWORD error = GetLastError();
    out = Outcome{};
    if (deciding) {tainted = true; out.reason = Reason::Reentry; SetLastError(error); return false;}
    deciding = true; tainted = false;
    bool ok = false;
    __try {
        out = Decide(s, c, set, follow, handoff, grace, game_now);
        ok = out.cancel && !tainted;
        if (tainted) {out.cancel = false; out.reason = Reason::Reentry;}
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = Outcome{}; out.reason = Reason::Fault; ok = false;
    }
    deciding = false; tainted = false;
    SetLastError(error);
    return ok;
}
}  // namespace link_runtime::parry::general

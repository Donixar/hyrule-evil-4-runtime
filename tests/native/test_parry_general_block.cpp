// Tier-2 parry (general block): pure policy, follow-up guard and gate order with injected
// sources (T2), and the real producer pins on the pinned image and a relocated copy (T3).
#include "../../dll/parry_general_block.h"
#include "../../dll/game_clock.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace g = link_runtime::parry::general;
namespace p = link_runtime::parry;
static unsigned checks = 0;
static void Check(bool ok, const char* why) {++checks; if (!ok) {std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1);}}

constexpr uint32_t kBase = 0x10000000;
constexpr uint32_t kEm10 = 0x55CE2, kBoss = 0xB4CD7, kSaw = 0x2C178, kUnattributed = 0x27AD5, kRegen = 0x132F5A;

struct Fake {
    bool invocation = true, blocked = false, pose = true, front_ok = true, frontal = true, live = true;
    bool guard = false;
    unsigned guard_reads = 0;
    bool press = true, peek = true, refresh = true, fault = false, reenter = false;
    float half = 500.f, sample[3] = {0, 100, 500};
    uint32_t owner = 0x8BCC308;
    uint8_t routine[2] = {1, 0x0B};   // the owner's cEm r0/r1 ([em+0xFC], [em+0xFD])
    bool routine_ok = true;
    unsigned routine_reads = 0;
    g::Descriptor d{250.f, 8, 380, 0, 0}, second{250.f, 8, 380, 0, 0};
    bool vary = false;
    unsigned consumes = 0, peeks = 0, descriptor_reads = 0;
    uint64_t now = 1000;
    g::Follow* follow = nullptr;
    bool Invocation(const p::Candidate&) {return invocation;}
    uint32_t ImageBase() {return kBase;}
    bool Live(size_t) {return live;}
    bool Descriptor(uint32_t, g::Descriptor& out) {out = (vary && descriptor_reads++ == 1) ? second : d; return true;}
    bool Blocked() {return blocked;}
    bool Pose() {return pose;}
    bool Guard(bool refresh_input) {++guard_reads; return guard && peek && (!refresh_input || refresh);}
    bool Front(const p::Candidate&, g::Front& f) {
        if (fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        if (reenter) {g::Outcome inner; Fake nested; Check(!g::Run(nested, p::Candidate{1, 2, 3, 4, kBase + kEm10}, {g::Mode::Active}, *follow, false, inner) &&
                                                        inner.reason == g::Reason::Reentry, "nested decision refused");}
        f.frontal = frontal; f.half = half; f.dot = frontal ? -0.7f : 0.7f; std::memcpy(f.sample, sample, 12); return front_ok;
    }
    uint8_t saw_weapon = 4;   // [esi+0x7F0] of the chainsaw producer's ESI (4 = chainsaw)
    struct SawMem {const Fake& f; bool Read(uint32_t at, void* out, size_t n) {
        if (n != 1 || at != f.owner + 0x7F0) return false; *static_cast<uint8_t*>(out) = f.saw_weapon; return true;}};
    uint32_t Owner(const p::Candidate& c) {
        if (c.outer_caller == kBase + kEm10) return owner;
        SawMem m{*this}; return g::ChainsawOwner(m, c.outer_caller - kBase, c.producer_esi);
    }
    bool Routine(uint32_t who, uint8_t (&out)[2]) {++routine_reads; if (!routine_ok || !who) return false; out[0] = routine[0]; out[1] = routine[1]; return true;}
    uint64_t Now() {return now;}
    bool Refresh() {return refresh;}
    bool Consume() {++consumes; const bool ok = press; press = false; return ok;}   // one press = one defense
    bool Peek() {++peeks; return peek;}
    // Link at the origin facing +Z: put the sample at (x, z) around him (half = forward . offset).
    void At(float x, float z) {sample[0] = x; sample[2] = z; half = z;}
};
static p::Candidate Hit(uint32_t ret) {p::Candidate c; c.body_part = 0x1234; c.attack = 0x2000; c.current = 0x2100; c.old = 0x2200; c.outer_caller = kBase + ret; return c;}
static g::Outcome Go(Fake& f, uint32_t ret, g::Follow& follow, g::Mode mode = g::Mode::Active, bool bosses = true, bool handoff = false) {
    g::Outcome o; f.follow = &follow;
    const bool r = g::Run(f, Hit(ret), {mode, bosses, true}, follow, handoff, o);
    Check(r == o.cancel, "Run returns the outcome's cancel");
    return o;
}

static void Classes() {
    g::Settings on{g::Mode::Active, true, true}, off{g::Mode::Active, false, true};
    const g::Descriptor melee{250.f, 8, 380, 0, 0};
    auto at = [](uint32_t r) {return g::Find(r);};
    Check(g::kProducerCount == 163 && at(kEm10) < g::kProducerCount && g::kProducers[at(kEm10)].function == 0x55BE0, "generated table carries em10AtkCk");
    Check(g::Classify(at(kEm10), true, melee, on) == g::Verdict::Allowed, "A allowed");
    Check(g::Classify(at(kRegen), true, melee, off) == g::Verdict::Allowed, "A allowed without bosses");
    Check(g::Classify(at(kBoss), true, melee, on) == g::Verdict::Allowed, "B allowed with bosses (default on)");
    Check(g::Classify(at(kBoss), true, melee, off) == g::Verdict::Boss, "B refused with ParryGeneralBosses=0");
    // Chainsaw (Donald 22-oct): Em10AtkTbl[12] {500, 8, 9999, flag 8} is blockable from em10AtkCk and
    // em10CsawHitCk; every other instant kill still lands.
    const g::Descriptor saw{500.f, 8, 9999, 8, 0xA};
    Check(g::Classify(at(kSaw), true, saw, on) == g::Verdict::Allowed, "em10CsawHitCk chainsaw blockable");
    Check(g::Classify(at(kEm10), true, saw, on) == g::Verdict::Allowed, "chainsaw walk attack (em10AtkCk) blockable");
    Check(g::Classify(at(kSaw), true, melee, on) == g::Verdict::Allowed, "chainsaw producer, ordinary descriptor");
    Check(g::Classify(at(kRegen), true, saw, on) == g::Verdict::Lethal, "chainsaw descriptor from another producer stays lethal");
    Check(g::Classify(at(kBoss), true, saw, on) == g::Verdict::Lethal, "chainsaw descriptor from a boss stays lethal");
    Check(g::Classify(at(kEm10), true, {250.f, 8, 9999, 8, 0xA}, on) == g::Verdict::Lethal, "other radius: not the chainsaw");
    Check(g::Classify(at(kEm10), true, {500.f, 8, 9999, 0, 0xA}, on) == g::Verdict::Lethal, "9999 without flag 8: not the chainsaw");
    Check(g::Classify(at(kUnattributed), true, melee, on) == g::Verdict::Allowed, "formerly unattributed producer allowed");
    Check(g::Classify(at(0x3226C2), true, {400.f, 8, 9999, 0, 0}, on) == g::Verdict::Lethal, "formerly unattributed instant kill still lands");
    Check(g::Classify(g::Find(0x12345), true, melee, on) == g::Verdict::Unlisted, "unknown return passes");
    Check(g::Classify(at(kEm10), false, melee, on) == g::Verdict::Unpinned, "failed pin turns off only that producer");
    Check(g::Classify(at(kEm10), true, {250.f, 8, 0, 0, 0}, on) == g::Verdict::NoPower, "power 0 grab starter passes");
    Check(g::Classify(at(kEm10), true, {250.f, 8, 9999, 4, 0}, on) == g::Verdict::Lethal, "9999 lethal passes");
    Check(g::Classify(at(kBoss), true, {250.f, 8, 300, 8, 0}, on) == g::Verdict::Lethal, "flag 8 lethal passes, bosses too");
    Check(g::Classify(at(kEm10), true, {NAN, 8, 380, 0, 0}, on) == g::Verdict::BadDescriptor, "non-finite radius passes");
    Check(g::Classify(at(kEm10), true, {0.f, 8, 380, 0, 0}, on) == g::Verdict::BadDescriptor, "zero radius passes");
    unsigned a = 0, b = 0;
    for (const auto& prod : g::kProducers) {a += prod.cls == 'A'; b += prod.cls == 'B'; Check(prod.pin && std::strlen(prod.pin) == 32, "every row has a 16-byte pin");}
    Check(a == 18 && b == 138, "18 A and 138 B returns");
    const size_t throw_at = at(g::thrown::kThrowReturn), scythe_at = at(g::thrown::kScytheReturn);
    Check(throw_at < g::kProducerCount && g::kProducers[throw_at].cls == 'T' && g::kProducers[scythe_at].cls == 'T', "thrown axes and the scythe are class T");
    Check(g::Classify(throw_at, true, melee, on) == g::Verdict::Allowed, "thrown weapon blockable by default");
    Check(g::Classify(scythe_at, true, melee, {g::Mode::Active, true, false}) == g::Verdict::Thrown, "ParryGeneralThrown=0 lets it through");
    Check(g::Classify(at(0x26A682), true, melee, on) == g::Verdict::Excluded, "obj08 poison spit stays excluded");
}
// The weapon object as the game holds it: `base` maps to bytes[0].
struct Heap {
    uint32_t base = 0x20000000; std::vector<unsigned char> bytes = std::vector<unsigned char>(0x1000);
    unsigned writes = 0; bool fail_write = false;
    bool Read(uint32_t a, void* out, size_t n) {if (a < base || a - base + n > bytes.size()) return false; std::memcpy(out, &bytes[a - base], n); return true;}
    bool Write(uint32_t a, const void* in, size_t n) {++writes; if (fail_write || a < base || a - base + n > bytes.size()) return false; std::memcpy(&bytes[a - base], in, n); return true;}
    void Put(uint32_t off, uint32_t v) {std::memcpy(&bytes[off], &v, 4);}
};
static void Thrown() {
    Heap h; const uint32_t obj = h.base + 0x100, img = 0x400000;
    h.Put(0x100, img + g::thrown::kVtable); h.Put(0x100 + 0x620, 0xC0FFEE); h.bytes[0x100 + 0xFC] = 1; h.bytes[0x100 + 0xFD] = 5;
    p::Candidate c; c.attack = 0xC0FFEE; c.current = obj + 0x94; c.old = obj + 0x110; c.body_part = 1;
    uint32_t found = 0;
    Check(g::thrown::Identify(h, img, c, g::thrown::kThrowReturn, found) && found == obj, "thrown axe recovered from its arguments");
    Check(!g::thrown::Identify(h, img, c, g::thrown::kScytheReturn, found) && !found, "scythe return needs routine 0xA");
    h.bytes[0x100 + 0xFD] = 0xA; Check(g::thrown::Identify(h, img, c, g::thrown::kScytheReturn, found), "scythe recovered");
    Check(!g::thrown::Identify(h, img, c, kEm10, found), "non-thrown producer never identifies");
    auto bad = c; bad.old += 4; Check(!g::thrown::Identify(h, img, bad, g::thrown::kScytheReturn, found), "pos_old must be obj+0x110");
    bad = c; bad.attack++; Check(!g::thrown::Identify(h, img, bad, g::thrown::kScytheReturn, found), "pAtk must be the attack argument");
    Check(!g::thrown::Identify(h, img + 0x1000, c, g::thrown::kScytheReturn, found), "foreign vtable refused");
    h.bytes[0x100 + 0xFC] = 4; Check(!g::thrown::Identify(h, img, c, g::thrown::kScytheReturn, found), "falling weapon (routine 4) refused");
    h.bytes[0x100 + 0xFC] = 1;
    const float from[3] = {10, 20, 30};
    Check(g::thrown::Deflect(h, obj, from), "knife damage record written");
    float got[3]; std::memcpy(got, &h.bytes[0x100 + 0x330], 12);
    Check(h.bytes[0x100 + 0x32C] == 1 && h.bytes[0x100 + 0x32D] == 10 && h.bytes[0x100 + 0x32E] == 0x10 && got[0] == 10 && got[2] == 30, "flag 1, timer 10, knife id 0x10, PosFrom");
    const unsigned before = h.writes;
    Check(!g::thrown::Deflect(h, obj, from) && h.writes == before, "pending record never overwritten");
    h.bytes[0x100 + 0x32C] = 0; h.fail_write = true; const float nan3[3] = {NAN, 0, 0};
    Check(!g::thrown::Deflect(h, obj, nan3) && h.bytes[0x100 + 0x32C] == 0, "non-finite from writes nothing");
    Check(!g::thrown::Deflect(h, obj, from) && h.bytes[0x100 + 0x32C] == 0, "failed write never sets the flag");
}
static void Settings() {
    auto with = [](const wchar_t* mode, const wchar_t* bosses, const wchar_t* thrown_on, bool active, bool transport, unsigned& problems) {
        return g::ReadSettings([&](const wchar_t* key, wchar_t* out, size_t n) {
            const wchar_t* v = !wcscmp(key, L"ParryGeneralBlock") ? mode : !wcscmp(key, L"ParryGeneralBosses") ? bosses : thrown_on;
            wcsncpy_s(out, n, v, _TRUNCATE);}, active, transport, problems);
    };
    unsigned n = 0;
    auto s = with(L"", L"", L"", true, true, n); Check(s.mode == g::Mode::Off && !n, "missing key = off, no problem");
    s = with(L"1", L"", L"", true, true, n); Check(s.mode == g::Mode::Active && s.bosses && s.thrown && !n, "active; bosses and thrown default on");
    s = with(L"1", L"0", L"0", true, true, n); Check(s.mode == g::Mode::Active && !s.bosses && !s.thrown && !n, "bosses and thrown can be turned off");
    s = with(L"2", L"", L"", false, true, n); Check(s.mode == g::Mode::Observe && !n, "observe needs only the transport");
    s = with(L"1", L"", L"", false, true, n); Check(s.mode == g::Mode::Off && n == 1, "active without ParryActive: off, problem");
    s = with(L"2", L"", L"", false, false, n); Check(s.mode == g::Mode::Off && n == 2, "observe without transport: off, problem");
    s = with(L"3", L"", L"", true, true, n); Check(s.mode == g::Mode::Off && n == 3, "invalid mode: off");
    s = with(L"1", L"2", L"", true, true, n); Check(s.mode == g::Mode::Off && n == 4, "invalid bosses: off");
    s = with(L"1", L"", L"yes", true, true, n); Check(s.mode == g::Mode::Off && n == 5, "invalid thrown: off");
    s = with(L"01", L"", L"", true, true, n); Check(s.mode == g::Mode::Off && n == 6, "only exact values");
}
static void Gates() {
    g::Follow follow;
    {Fake f; const auto o = Go(f, kEm10, follow); Check(o.cancel && o.consumed && f.consumes == 1 && o.reason == g::Reason::Blocked && o.owner == 0x8BCC308 && o.cls == 'A', "frontal A with a fresh press blocks, consuming once");}
    follow = {};
    {Fake f; f.press = false; const auto o = Go(f, kEm10, follow); Check(!o.cancel && o.reason == g::Reason::Window && f.consumes == 1, "held button without a fresh press does not protect");}
    // Front and sides parry (Donald 22-oct); only the back arc (within 60 degrees of straight behind) is
    // left to the rear shield, by where the sample is around Link, not by how the weapon moves.
    {Fake f; f.At(0, -500); const auto o = Go(f, kEm10, follow); Check(!o.cancel && o.reason == g::Reason::Rear && !f.consumes, "hit at Link's back passes without spending the press");}
    {Fake f; f.At(300, -500); Check(Go(f, kEm10, follow).reason == g::Reason::Rear && !f.consumes, "31 degrees off his back: rear shield's");}
    follow = {};
    {Fake f; f.At(500, 0); Check(Go(f, kEm10, follow).cancel, "hit square on Link's side blocks");}
    follow = {};
    {Fake f; f.At(-500, -40); Check(Go(f, kEm10, follow).cancel, "side sample slightly behind the chest plane blocks (castle flail)");}
    follow = {};
    {Fake f; f.At(500, -280); Check(Go(f, kEm10, follow).cancel, "29 degrees behind the side (119 from the front) still blocks");}
    follow = {};
    {Fake f; f.At(500, -300); Check(Go(f, kEm10, follow).reason == g::Reason::Rear, "121 degrees from the front is the back arc");}
    {Fake f; f.At(300, 300); f.frontal = false; Check(Go(f, kEm10, follow).cancel, "front sample with the weapon moving away still blocks");}
    follow = {};
    {Fake f; f.At(0, 0); f.frontal = false; Check(Go(f, kEm10, follow).reason == g::Reason::Rear, "sample at Link's centre: native direction decides (rear)");}
    {Fake f; f.At(0, 0); Check(Go(f, kEm10, follow).cancel, "sample at Link's centre: native direction decides (front)");}
    follow = {};
    {Fake f; f.front_ok = false; Check(Go(f, kEm10, follow).reason == g::Reason::Front && !f.consumes, "unknown direction passes");}
    {Fake f; f.pose = false; Check(Go(f, kEm10, follow).reason == g::Reason::Pose && !f.consumes, "no sword pose passes");}
    {Fake f; f.blocked = true; Check(Go(f, kEm10, follow).reason == g::Reason::InputBlocked && !f.consumes, "ocarina / Mortal Draw block the parry input");}
    {Fake f; f.invocation = false; Check(Go(f, kEm10, follow).reason == g::Reason::Structure && !f.consumes, "non-structural invocation passes");}
    {Fake f; f.vary = true; f.second.power = 381; Check(Go(f, kEm10, follow).reason == g::Reason::Descriptor && !f.consumes, "descriptor changed between reads passes");}
    {Fake f; f.live = false; const auto o = Go(f, kEm10, follow); Check(o.reason == g::Reason::Producer && o.verdict == g::Verdict::Unpinned, "unpinned producer passes");}
    {Fake f; f.d = f.second = {500.f, 8, 9999, 8, 0xA}; Check(Go(f, kSaw, follow).cancel && f.consumes == 1, "chainsaw blocked with a press");}
    follow = {};
    {Fake f; Check(Go(f, 0x1D769F, follow).verdict == g::Verdict::Excluded && !f.consumes, "boulders never consume");}
    {Fake f; Check(Go(f, kBoss, follow, g::Mode::Active, false).verdict == g::Verdict::Boss, "boss off passes");}
    {Fake f; f.refresh = false; Check(!Go(f, kEm10, follow).cancel && !f.consumes, "unrefreshed input never consumes");}
    {Fake f; Check(!Go(f, kEm10, follow, g::Mode::Off).cancel && !f.consumes, "Off decides nothing");}
}
static void Handoff() {
    g::Follow follow;
    Fake f; f.press = false;
    const auto o = Go(f, kEm10, follow, g::Mode::Active, true, true);
    Check(o.cancel && o.handoff && !o.consumed && f.consumes == 0, "tier-1 handoff blocks with that press, never consuming again");
    follow = {};
    Fake r; r.At(0, -500);
    Check(!Go(r, kEm10, follow, g::Mode::Active, true, true).cancel, "handoff still needs every other gate");
}
static void Precedence() {
    bool handoff = true;
    Check(!g::TierTwoRuns(g::TierOne::Committed, handoff) && !handoff, "a tier-1 commit (Axe or Suki) never reaches tier 2");
    Check(!g::TierTwoRuns(g::TierOne::Partial, handoff) && !handoff, "partial failure keeps the damage, no tier 2");
    Check(g::TierTwoRuns(g::TierOne::NotCommitted, handoff) && !handoff, "tier-1 rejection falls to tier 2 with its own press");
    Check(g::TierTwoRuns(g::TierOne::NotCommittedSpent, handoff) && handoff, "approved-but-unwritten tier 1 hands its press over");
    Check(g::TierTwoRuns(g::TierOne::Inactive, handoff) && !handoff, "tier 1 off or quarantined: tier 2 alone");
}
static void FollowUp() {
    g::Follow follow;
    Fake f; Check(Go(f, kEm10, follow).cancel && f.consumes == 1, "first sample blocks");
    f.now += 0; Check(Go(f, kEm10, follow).follow && f.consumes == 1, "same frame next sample: silent follow, no consume");
    f.now += 100; const auto o = Go(f, kEm10, follow); Check(o.cancel && o.follow && f.consumes == 1, "gap 100 ms still the same swing");
    f.now += 101; f.routine[1] = 0x0C; Check(!Go(f, kEm10, follow).cancel, "gap over 100 ms, attacker in another routine: a new swing needs a new press");
    follow = {}; Fake q; q.routine_ok = false; Check(Go(q, kEm10, follow).cancel, "block, owner routine unreadable");
    q.now += 101; Check(!Go(q, kEm10, follow).cancel, "unreadable routine: gap over 100 ms ends the follow");
    follow = {}; Fake h; h.now = 0; h.routine_ok = false; Check(Go(h, kEm10, follow).cancel, "block at 0");
    for (uint64_t t = 90; t <= 450; t += 90) {h.now = t; Check(Go(h, kEm10, follow).follow, "chained samples within the cap");}
    h.now = 540; Check(!Go(h, kEm10, follow).cancel, "500 ms hard cap from the blocked press");
    follow = {}; Fake k; Check(Go(k, kEm10, follow).cancel, "block em10");
    k.owner = 0x8BCFB28; k.now += 10; Check(!Go(k, kEm10, follow).cancel, "another em10 owner is not followed");
    follow = {}; Fake m; Check(Go(m, kRegen, follow).cancel, "block a non-em10 producer");
    m.now += 10; m.sample[0] = 2400; Check(Go(m, kRegen, follow).follow, "unknown owner: sample within 2500 follows");
    m.now += 10; m.sample[0] = 6000; Check(!Go(m, kRegen, follow).cancel, "unknown owner: far sample is not followed");
    follow = {}; Fake n; Check(Go(n, kRegen, follow).cancel, "block Regenerador");
    n.now += 10; Check(!Go(n, 0x9E97D, follow).cancel, "another producer never chains");
    // A wide swing (castle flail Zealot, room 0x208, 22-oct) sweeps past Link's side or back after the
    // block: those later samples are still the blocked swing, not a new frontal hit.
    follow = {}; Fake w; Check(Go(w, kEm10, follow).cancel, "block a wide swing");
    w.now += 16; w.half = -40.f; Check(Go(w, kEm10, follow).follow && w.consumes == 1, "same swing on Link's side: silent follow");
    w.now += 16; w.At(0, -500); w.frontal = false; Check(Go(w, kEm10, follow).follow, "same swing behind Link: silent follow");
    w.now += 16; w.pose = false; Check(Go(w, kEm10, follow).follow, "same swing during the parry reaction (no sword pose): silent follow");
    w.owner = 0x8BCFB28; w.now += 16; Check(!Go(w, kEm10, follow).cancel, "another attacker on the side is not followed");
    // Unknown owner (every producer but em10AtkCk and thrown weapons): the follow-up is by distance only, so it never
    // skips the gates; another attacker of the same kind at Link's back must still land (review 22-oct).
    follow = {}; Fake u; Check(Go(u, kRegen, follow).cancel, "block a producer without an owner");
    u.now += 16; u.At(0, -500); Check(!Go(u, kRegen, follow).cancel, "unknown owner at Link's back: not followed past the gates");
    u.now += 16; u.At(0, 500); u.pose = false; Check(!Go(u, kRegen, follow).cancel, "unknown owner without the sword pose: not followed");
    u.now += 16; u.pose = true; Check(Go(u, kRegen, follow).follow, "unknown owner in front: still followed after the gates");
}
// Chainsaw (Donald 28-oct, play-20261028/tests/04-chainsaw.log): block at 0x2C249 (power 9999, owner_id 0), then
// 31 ms later 0x2C2CA at Link's side passed as `rear` and killed him. ESI of em10CsawHitCk is the owner now.
static p::Candidate Saw(uint32_t ret, uint32_t esi) {p::Candidate c = Hit(ret); c.producer_esi = esi; return c;}
static g::Outcome GoSaw(Fake& f, uint32_t ret, uint32_t esi, g::Follow& follow) {
    g::Outcome o; f.follow = &follow; g::Run(f, Saw(ret, esi), {g::Mode::Active, true, true}, follow, false, o); return o;
}
static void ChainsawSwing() {
    Check(g::ChainsawProducer(0x2C178) && g::ChainsawProducer(0x2C249) && g::ChainsawProducer(0x2C2CA) && !g::ChainsawProducer(kEm10),
          "the three em10CsawHitCk returns");
    const g::Descriptor saw{500.f, 8, 9999, 8, 0xA};
    {   g::Follow follow; Fake f; f.d = f.second = saw;
        const auto a = GoSaw(f, 0x2C249, f.owner, follow);
        Check(a.cancel && a.consumed && a.owner == f.owner, "chainsaw block resolves its Ganado from ESI");
        f.now += 31; f.At(30, 13); f.frontal = false; f.press = false;
        const auto b = GoSaw(f, 0x2C2CA, f.owner, follow);
        Check(b.cancel && b.follow && f.consumes == 1, "31 ms later, the same saw at Link's side: followed, not a rear kill");
        f.now += 16; f.At(0, -500); f.pose = false;
        Check(GoSaw(f, 0x2C178, f.owner, follow).follow, "same swing behind Link, no sword pose: followed by owner");
        Fake other; other.owner = 0x8BD0000; other.d = other.second = saw; other.press = false; other.now = f.now + 10; other.At(0, -500);
        Check(!GoSaw(other, 0x2C2CA, other.owner, follow).cancel, "another chainsaw Ganado at Link's back still lands");
        f.now += 701; f.pose = true;   // past Follow::kSwingGapMs
        Check(!GoSaw(f, 0x2C2CA, f.owner, follow).cancel, "outside the follow window the rear check lands again");
    }
    {   g::Follow follow; Fake f; f.d = f.second = saw; f.saw_weapon = 0;
        const auto a = GoSaw(f, 0x2C249, f.owner, follow);
        Check(a.cancel && a.owner == 0, "ESI without the chainsaw (+0x7F0 != 4) is no owner");
        f.now += 31; f.At(30, -13); f.frontal = false; f.press = false;
        Check(!GoSaw(f, 0x2C2CA, f.owner, follow).cancel, "no owner: a rear check is not followed past the gates (old behaviour)");
    }
    {   g::Follow follow; Fake f; f.d = f.second = saw;
        Check(GoSaw(f, 0x2C249, 0, follow).cancel && !follow.owner, "ESI 0: no owner");
    }
}
static void Observe() {
    g::Follow follow;
    Fake f; const auto o = Go(f, kEm10, follow, g::Mode::Observe);
    Check(!o.cancel && o.would_block && f.consumes == 0 && f.peeks == 1, "Observe reports would_block, never cancels or consumes");
    Fake q; q.peek = false; Check(Go(q, kEm10, follow, g::Mode::Observe).reason == g::Reason::Window && !q.consumes, "Observe without a press: window");
    Check(!follow.armed, "Observe never arms the follow");
}
static void Faults() {
    g::Follow follow;
    Fake f; f.fault = true; SetLastError(4321);
    const auto o = Go(f, kEm10, follow);
    Check(!o.cancel && o.reason == g::Reason::Fault && !f.consumes && GetLastError() == 4321, "source exception: native damage, LastError kept");
    Check(!g::deciding, "decision scope released after a fault");
    Fake r; r.reenter = true;
    const auto e = Go(r, kEm10, follow);
    Check(!e.cancel && e.reason == g::Reason::Reentry, "reentry taints the outer decision");
    Fake ok; Check(Go(ok, kEm10, follow).cancel, "next decision after reentry works");
}
// The module as the game process sees it: `base` maps to bytes[0].
struct Image {
    uint32_t base = 0x400000; std::vector<unsigned char> bytes;
    bool Read(uint32_t address, void* out, size_t n) {
        if (address < base || uint64_t(address - base) + n > bytes.size()) return false;
        std::memcpy(out, bytes.data() + (address - base), n); return true;
    }
};
static uint32_t U32(const unsigned char* q) {uint32_t v; std::memcpy(&v, q, 4); return v;}
static void Relocate(Image& img, uint32_t base) {
    const uint32_t delta = base - img.base; unsigned char* d = img.bytes.data();
    const uint32_t opt = U32(d + 0x3C) + 24, rva = U32(d + opt + 136), size = U32(d + opt + 140);
    for (uint32_t c = 0; c < size;) {
        const uint32_t page = U32(d + rva + c), block = U32(d + rva + c + 4);
        Check(block >= 8, "relocation block");
        for (uint32_t at = 8; at < block; at += 2) {
            uint16_t e; std::memcpy(&e, d + rva + c + at, 2);
            if (!(e >> 12)) continue;
            Check((e >> 12) == 3, "HIGHLOW only");
            unsigned char* q = d + page + (e & 0xFFF); const uint32_t v = U32(q) + delta; std::memcpy(q, &v, 4);
        }
        c += block;
    }
    img.base = base;
}
static link_runtime::shield::NativeAccess Access(Image& img) {
    using namespace link_runtime::shield;
    NativeAccess a; uint32_t preferred = 0;
    Check(MappedRegions(img.bytes.data(), img.bytes.size(), preferred, a.regions), "actual PE regions");
    a.image_base = img.base; a.image_size = static_cast<uint32_t>(img.bytes.size());
    a.code = FindNative(img.bytes.data(), img.bytes.size(), a.image_base, a.regions.data(), a.regions.size()); a.verified = a.code.verified;
    Check(Complete(a), "native wrapper/classifier qualified");
    return a;
}
static void RealImage(const char* path) {
    Image img; FILE* file = nullptr; Check(!fopen_s(&file, path, "rb") && file, "read pinned mapped image");
    std::fseek(file, 0, SEEK_END); const long n = std::ftell(file); std::rewind(file);
    img.bytes.resize(static_cast<size_t>(n)); Check(std::fread(img.bytes.data(), 1, img.bytes.size(), file) == img.bytes.size(), "complete image"); std::fclose(file);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass) Relocate(img, 0x1000000);
        auto a = Access(img);
        const auto code = g::Qualify(img, img.bytes.data(), img.bytes.size(), a);
        Check(code.verified && code.image_base == img.base, "site and native front qualify tier 2");
        Check(code.live_a == 18 && code.total_a == 18 && code.live_b == 138 && code.total_b == 138 && code.live_t == 2 && code.total_t == 2, "every A/B/T pin matches at this base");
        Check(g::thrown::Qualify(img, img.base), "thrown deflect route pinned at this base");
        for (const auto& pin : g::thrown::kDeflect) {
            const uint32_t at = pin.rva + static_cast<uint32_t>(std::strlen(pin.hex) / 2) - 1;
            img.bytes[at] ^= 1; Check(!g::thrown::Qualify(img, img.base), "any deflect pin byte mutated: deflect off"); img.bytes[at] ^= 1;
        }
        for (size_t i = 0; i < g::kProducerCount; ++i) {
            const char cls = g::kProducers[i].cls;
            if (cls == 'X' || cls == 'C') Check(!code.live[i], "X and C are never live");
        }
        // Mutating one byte of a pin turns off only that producer.
        const size_t em10 = g::Find(kEm10), boss = g::Find(kBoss);
        for (size_t at : {g::kProducers[em10].ret - 16, g::kProducers[em10].ret - 1}) {
            img.bytes[at] ^= 1;
            const auto cut = g::Qualify(img, img.bytes.data(), img.bytes.size(), a);
            Check(cut.verified && !cut.live[em10] && cut.live[boss] && cut.live_a == 17 && cut.live_b == 138, "one mutated pin byte turns off only em10");
            img.bytes[at] ^= 1;
        }
        // Site (0x1BC4A6) or RotVector mutated: tier 2 off as a whole.
        for (uint32_t at : {0x1BC4A6U, 0x1BC4ABU, 0x2D06E0U, 0x2D06F0U}) {
            img.bytes[at] ^= 1;
            Check(!g::Qualify(img, img.bytes.data(), img.bytes.size(), a).verified, "mutated site or RotVector turns tier 2 off");
            img.bytes[at] ^= 1;
        }
        a.image_base += 0x10000;
        Check(!g::Qualify(img, img.bytes.data(), img.bytes.size(), a).verified, "wrong image base fails closed");
    }
}
// Post-parry grace (Donald 12-oct): after a committed tier-1 parry or a press-consuming tier-2 block,
// ParryGraceMs (200) of GAME time during which another qualifying frontal hit is blocked through the
// tier-2 path without a press. Opened only by a successful parry; never by holding or pressing; no chaining.
static g::Outcome GoGrace(Fake& f, uint32_t ret, g::Follow& follow, g::Grace& grace, double game_now, g::Mode mode = g::Mode::Active) {
    g::Outcome o; f.follow = &follow;
    const bool r = g::Run(f, Hit(ret), {mode, true, true}, follow, false, o, &grace, game_now);
    Check(r == o.cancel, "Run returns the outcome's cancel (grace)");
    return o;
}
static void GraceWindow() {
    Check(g::GraceMs(200) == 200 && g::GraceMs(0) == 0 && g::GraceMs(1000) == 1000 && g::GraceMs(1001) == 0, "ParryGraceMs 0..1000, else off");
    Check(std::fabs(g::Grace::Frames(200) - 12.0) < 1e-9, "200 ms = 12 frames of 60 Hz game time");
    {   // two attackers within 200 ms: both blocked, the second without a press (grace flag: sparks only)
        g::Follow follow; g::Grace grace; grace.Configure(200);
        Fake a; a.now = 1000; const auto first = GoGrace(a, kEm10, follow, grace, 100.0);
        Check(first.cancel && first.consumed && first.grace_opened && !first.grace && grace.open, "press block opens the grace window");
        Fake b; b.owner = 0x8BCFB28; b.press = false; b.now = 1100;
        const auto second = GoGrace(b, kEm10, follow, grace, 106.0);
        Check(second.cancel && second.grace && !second.consumed && b.consumes == 0 && !second.follow, "second attacker inside 200 ms blocked without a press");
        Check(grace.blocked == 1 && !second.grace_opened, "counted in the same parry, no new window");
        Fake c; c.owner = 0x8BD0000; c.press = false; c.now = 1250;
        const auto third = GoGrace(c, kEm10, follow, grace, 115.0);   // 250 ms
        Check(!third.cancel && third.reason == g::Reason::Window && third.grace_expired && !grace.open, "third hit at 250 ms gets through; window expired");
    }
    {   // no chaining: a grace block at 190 ms does not extend the window
        g::Follow follow; g::Grace grace; grace.Configure(200);
        Fake a; Check(GoGrace(a, kEm10, follow, grace, 0.0).grace_opened, "opened at 0");
        Fake b; b.owner = 0x8BCFB28; b.press = false; b.now = a.now + 190;
        Check(GoGrace(b, kEm10, follow, grace, 11.4).grace, "blocked at 190 ms");
        Fake c; c.owner = 0x8BD0000; c.press = false; c.now = a.now + 216;
        Check(!GoGrace(c, kEm10, follow, grace, 13.0).cancel, "216 ms: no chaining, the hit lands");
    }
    {   // a second success while the window is open does not reopen or extend it
        g::Grace grace; grace.Configure(200);
        Check(grace.Open(0.0, 1) && !grace.Open(6.0, 2) && grace.until == 12.0 && grace.serial == 1, "second success inside the window: same window");
        Check(!grace.Expire(12.0) && grace.Covers(12.0) && grace.Expire(12.5) && !grace.Covers(12.5), "covers through 12 frames, then expires once");
        Check(!grace.Expire(13.0), "expired reported once");
        Check(grace.Open(13.0, 3) && grace.until == 25.0, "a new successful parry after the window opens a new one");
    }
    {   // holding the button never opens or renews a window
        g::Follow follow; g::Grace grace; grace.Configure(200);
        Fake held; held.press = false;
        const auto o = GoGrace(held, kEm10, follow, grace, 0.0);
        Check(!o.cancel && !o.grace_opened && !grace.open, "held button without a parry opens nothing");
        Fake a; Check(GoGrace(a, kEm10, follow, grace, 1.0).grace_opened, "real block opens");
        Fake h2; h2.owner = 0x8BCFB28; h2.press = false; h2.now = a.now + 300;
        Check(!GoGrace(h2, kEm10, follow, grace, 19.0).cancel && !grace.open, "held button after the window: hit lands, nothing renewed");
    }
    {   // rear, grab and instant-kill hits inside the window still land; a side hit is blocked
        g::Follow follow; g::Grace grace; grace.Configure(200);
        Fake a; GoGrace(a, kEm10, follow, grace, 0.0);
        Fake rear; rear.owner = 0x8BCFB28; rear.press = false; rear.At(0, -500); rear.now = a.now + 50;
        Check(!GoGrace(rear, kEm10, follow, grace, 3.0).cancel, "rear hit inside the grace window lands");
        Fake side; side.owner = 0x8BD0000; side.press = false; side.At(500, -5); side.now = a.now + 60;
        Check(GoGrace(side, kEm10, follow, grace, 3.6).grace, "side hit inside the window is blocked by the grace");
        Fake grab; grab.owner = 0x8BCFB28; grab.press = false; grab.d.power = 0; grab.now = a.now + 70;
        Check(!GoGrace(grab, kEm10, follow, grace, 4.2).cancel, "grab starter inside the window lands");
        Fake kill; kill.owner = 0x8BCFB28; kill.press = false; kill.d.power = 9999; kill.now = a.now + 80;
        Check(!GoGrace(kill, kEm10, follow, grace, 4.8).cancel, "instant kill inside the window lands");
        Check(grace.blocked == 1, "only the side hit counted as blocked");
    }
    {   // the grace-blocked swing's later samples stay blocked silently, even past the window
        g::Follow follow; g::Grace grace; grace.Configure(200);
        Fake a; GoGrace(a, kEm10, follow, grace, 0.0);
        Fake b; b.owner = 0x8BCFB28; b.press = false; b.now = a.now + 150;
        Check(GoGrace(b, kEm10, follow, grace, 9.0).grace, "second attacker blocked");
        b.now += 80; const auto later = GoGrace(b, kEm10, follow, grace, 13.8);
        Check(later.cancel && later.follow && later.grace && grace.blocked == 1, "same swing after 200 ms: silent follow-up");
        b.now += 16; b.half = -40.f; const auto swept = GoGrace(b, kEm10, follow, grace, 14.8);
        Check(swept.cancel && swept.follow && swept.grace && grace.blocked == 1, "grace-blocked swing sweeping to the side: silent follow-up");
    }
    {   // 165 FPS: the window is game time (deltaTime), not ticks
        g::Follow follow; g::Grace grace; grace.Configure(200);
        link_runtime::game_clock::Clock clock; const float dt = 30.f / 165.f;
        uint32_t frame = 5000; double now = clock.Observe(frame, dt);
        Fake a; Check(GoGrace(a, kEm10, follow, grace, now).grace_opened, "opened at 165");
        for (int i = 0; i < 32; ++i) now = clock.Observe(++frame, dt);       // 32 ticks = 193.9 ms
        Fake b; b.owner = 0x8BCFB28; b.press = false; b.now = a.now + 194;
        Check(GoGrace(b, kEm10, follow, grace, now).grace, "32 ticks at 165 FPS (194 ms) still inside");
        for (int i = 0; i < 9; ++i) now = clock.Observe(++frame, dt);        // 41 ticks = 248 ms
        Fake c; c.owner = 0x8BD0000; c.press = false; c.now = a.now + 248;
        Check(!GoGrace(c, kEm10, follow, grace, now).cancel, "41 ticks at 165 FPS (248 ms) is past the window");
    }
    {   // ParryGraceMs=0 and Observe mode
        g::Follow follow; g::Grace off; off.Configure(0);
        Fake a; Check(!GoGrace(a, kEm10, follow, off, 0.0).grace_opened && !off.open, "0 disables the window");
        Fake b; b.owner = 0x8BCFB28; b.press = false; b.now = a.now + 50;
        Check(!GoGrace(b, kEm10, follow, off, 3.0).cancel, "no window: second attacker hits");
        g::Follow f2; g::Grace grace; grace.Configure(200);
        Fake o; Check(!GoGrace(o, kEm10, f2, grace, 0.0, g::Mode::Observe).grace_opened && !grace.open, "Observe never opens a window");
    }
}
// Bug #2 (24-oct, castle chain Zealots, link_runtime.log 6228609..6228812): tier 2 blocked the flail
// (owner 0x71B, em10AtkCk, power 528) with sparks and voice; ~26 silent follow-ups; then no contact for
// more than 100 ms while the chain swung out, and the same swing touched Link again at 203 ms: the follow
// had expired (100 ms gap), the grace too (12 frames), no fresh press -> reason=window, damage. Same at
// 6234171 -> 6234375 (reason=pose) and 6270281 -> 6270546 (reason=window).
static void ChainSwing() {
    {   // A positively observed new routine ends the followed swing, even inside the short 100 ms gap.
        g::Follow follow;Fake f;f.now=0;Check(Go(f,kEm10,follow).cancel,"block before routine transition");
        f.now=50;f.routine[1]=0x0C;
        Check(!Go(f,kEm10,follow).cancel,"a different known attack within 100 ms needs a new press");
        f.now=80;f.routine[1]=0x0B;
        Check(!Go(f,kEm10,follow).cancel,"returning to the old routine cannot resurrect a cancelled swing");
    }
    {   // The same rule applies to a second attack tracked by post-parry grace, after grace has ended.
        g::Follow follow;g::Grace grace;grace.Configure(200);Fake a;a.now=0;
        Check(GoGrace(a,kEm10,follow,grace,0.0).grace_opened,"open grace before routine transition");
        Fake b;b.owner=0x8BCFB28;b.press=false;b.now=100;
        Check(GoGrace(b,kEm10,follow,grace,6.0).grace,"grace tracks the second attack");
        b.now=330;Check(GoGrace(b,kEm10,follow,grace,19.8).follow,"second attack continues after grace");
        b.now=350;b.routine[1]=0x0C;
        Check(!GoGrace(b,kEm10,follow,grace,21.0).cancel,"new routine ends a grace follow inside 100 ms");
        b.now=370;b.routine[1]=0x0B;
        Check(!GoGrace(b,kEm10,follow,grace,22.2).cancel,"grace follow cannot resurrect on a repeated routine");
    }
    {   // the logged sequence: block, contacts for ~80 ms, a 200 ms gap, contact again with no press
        g::Follow follow; Fake f; f.now = 0;
        const auto block = Go(f, kEm10, follow);
        Check(block.cancel && block.consumed && f.consumes == 1, "flail blocked with the press");
        for (uint64_t t = 6; t <= 78; t += 6) {f.now = t; Check(Go(f, kEm10, follow).follow, "chain contacts after the block: silent");}
        f.now = 280; f.At(300, 400);
        const auto back = Go(f, kEm10, follow);
        Check(back.cancel && back.follow && back.long_follow && f.consumes == 1, "same swing back after a 202 ms gap: still cancelled, no press");
        f.now = 300; f.pose = false;
        Check(Go(f, kEm10, follow).follow, "during the parry reaction (no sword pose): still cancelled");
        f.now = 600; f.At(500, -300); f.frontal = false;
        Check(Go(f, kEm10, follow).follow, "the chain sweeping past Link's back 300 ms later: still that swing");
        f.now = 1000; f.pose = true; f.At(0, 500);
        Check(Go(f, kEm10, follow).follow, "one second after the block, same routine: still that swing");
    }
    {   // a wider gap or a new routine is a new attack, which needs its own parry
        g::Follow follow; Fake f; f.now = 0; Check(Go(f, kEm10, follow).cancel, "block");
        f.now = 701; Check(!Go(f, kEm10, follow).cancel, "no contact for over 700 ms: the follow ends");
        follow = {}; Fake b; b.now = 0; Check(Go(b, kEm10, follow).cancel, "block (28-oct)");
        b.now = 453; b.At(0, -500); b.frontal = false; b.press = false;
        Check(Go(b, kEm10, follow).follow, "chain back behind Link 453 ms later, same routine: still that swing");
        follow = {}; Fake r; r.now = 0; Check(Go(r, kEm10, follow).cancel, "block");
        r.now = 200; r.routine[1] = 0x0C; const auto o = Go(r, kEm10, follow);
        Check(!o.cancel && o.reason == g::Reason::Window, "attacker moved to another routine (new attack): needs a fresh press");
        follow = {}; Fake z; z.now = 0; Check(Go(z, kEm10, follow).cancel, "block");
        z.now = 200; z.routine[0] = 3; Check(!Go(z, kEm10, follow).cancel, "r0 changed (damaged, dying): not followed");
    }
    {   // hard cap 1500 ms from the block even with contacts in the same routine
        g::Follow follow; Fake f; f.now = 0; Check(Go(f, kEm10, follow).cancel, "block at 0");
        for (uint64_t t = 300; t <= 1500; t += 300) {f.now = t; Check(Go(f, kEm10, follow).long_follow, "contacts every 300 ms in the same routine");}
        f.now = 1800; Check(!Go(f, kEm10, follow).cancel, "1500 ms cap from the block");
    }
    {   // the longer bounds need a known, equal owner
        g::Follow follow; Fake f; f.now = 0; Check(Go(f, kEm10, follow).cancel, "block");
        Fake other; other.owner = 0x8BCFB28; other.now = 200; other.press = false;
        Check(!Go(other, kEm10, follow).cancel, "another Zealot in the same routine is not followed");
        follow = {}; Fake u; u.now = 0; Check(Go(u, kRegen, follow).cancel && !u.routine_reads, "unknown owner: no routine read");
        u.now = 200; Check(!Go(u, kRegen, follow).cancel, "unknown owner: the 100 ms gap still applies");
    }
    {   // a grace-blocked chain swing keeps the same longer follow-up
        g::Follow follow; g::Grace grace; grace.Configure(200);
        Fake a; a.now = 0; Check(GoGrace(a, kEm10, follow, grace, 0.0).grace_opened, "opening parry");
        Fake b; b.owner = 0x8BCFB28; b.press = false; b.now = 100;
        Check(GoGrace(b, kEm10, follow, grace, 6.0).grace, "second Zealot blocked by the grace");
        b.now = 330; const auto later = GoGrace(b, kEm10, follow, grace, 19.8);
        Check(later.cancel && later.follow && later.grace && later.long_follow, "its chain back 230 ms later, past the window: still blocked");
        b.routine[1] = 0x0C; b.now = 450; Check(!GoGrace(b, kEm10, follow, grace, 27.0).cancel, "its next attack (another routine, 120 ms later) lands without a parry");
    }
    {   // Observe never arms, so never follows
        g::Follow follow; Fake f; Go(f, kEm10, follow, g::Mode::Observe); Check(!follow.armed, "Observe arms nothing");
    }
}
// Two flail Zealots: B's press block re-arms the single Follow while A's chain is still out. A's live follow-up
// moves to a Grace slot (Grace::Park), so A's chain coming back ~320 ms later is still A's swing (it used to need
// a new press: reason=window, damage).
static g::Outcome GoTwo(Fake& f, g::Follow& follow, g::Grace* grace, double game_now, bool handoff = false) {
    g::Outcome o; f.follow = &follow;
    const bool r = g::Run(f, Hit(kEm10), {g::Mode::Active, true, true}, follow, handoff, o, grace, game_now);
    Check(r == o.cancel, "Run returns the outcome's cancel (two Zealots)");
    return o;
}
// A press-blocked at 0 with chain contacts to 198 ms; B blocked at 250 ms (press or tier-1 handoff), after A's
// 200 ms grace window. Returns B's block.
static g::Outcome TwoZealots(Fake& a, Fake& b, g::Follow& follow, g::Grace* grace, bool handoff = false) {
    a.now = 0; Check(GoTwo(a, follow, grace, 0.0).consumed, "A press block");
    for (uint64_t t = 6; t <= 198; t += 6) {a.now = t; Check(GoTwo(a, follow, grace, t * 0.06).follow, "A chain contacts: silent");}
    a.press = false; b.owner = 0x8BCFB28; b.now = 250; b.press = !handoff;
    const auto o = GoTwo(b, follow, grace, 15.0, handoff);
    Check(o.cancel && (handoff ? o.handoff && !b.consumes : o.consumed), "B blocked after A's grace window");
    return o;
}
static void TwoChains() {
    {   g::Follow follow; g::Grace grace; grace.Configure(200); Fake a, b;
        Check(TwoZealots(a, b, follow, &grace).grace_opened, "B's press block opens its own window");
        b.now = 280; const auto bf = GoTwo(b, follow, &grace, 16.8);
        Check(bf.cancel && bf.follow && !bf.grace, "B's follow-up stays in the main slot");
        a.now = 520; const auto back = GoTwo(a, follow, &grace, 31.2);
        Check(back.cancel && back.follow && back.long_follow && a.consumes == 1, "A's chain back 320 ms after B's block: still A's swing, no press");
        a.now = 560; a.pose = false; a.At(500, -300); a.frontal = false;
        Check(GoTwo(a, follow, &grace, 33.6).follow, "parked A sweeping behind with no sword pose: still A's swing (by owner)");
        a.pose = true; a.At(0, 500); a.frontal = true;
        Fake c; c.owner = 0x8BD0000; c.press = false; c.now = 580;
        Check(!GoTwo(c, follow, &grace, 34.8).cancel, "a third Zealot is not followed");
        for (uint64_t t = 860; t <= 1460; t += 300) {a.now = t; Check(GoTwo(a, follow, &grace, t * 0.06).follow, "parked A, same routine every 300 ms");}
        a.now = 1560; Check(!GoTwo(a, follow, &grace, 93.6).cancel, "parked A keeps the 1500 ms cap from A's own block");
    }
    {   g::Follow follow; g::Grace grace; grace.Configure(200); Fake a, b; TwoZealots(a, b, follow, &grace, true);
        a.now = 520; Check(GoTwo(a, follow, &grace, 31.2).follow, "A back after B's handoff block: still A's swing");
    }
    {   g::Follow follow; g::Grace grace; grace.Configure(200); Fake a, b; TwoZealots(a, b, follow, &grace);
        a.now = 470; a.routine[1] = 0x0C; Check(!GoTwo(a, follow, &grace, 28.2).cancel, "parked A in a new routine needs a press");
        a.now = 490; a.routine[1] = 0x0B; Check(!GoTwo(a, follow, &grace, 29.4).cancel, "parked A back in its old routine is not resurrected");
    }
    {   // ParryGraceMs=0: the runtime passes a window-0 Grace (it never opens) that still holds the parked follow-up
        g::Follow follow; g::Grace off; off.Configure(0); Fake a, b;
        Check(!TwoZealots(a, b, follow, &off).grace_opened && !off.open, "window 0: B's block opens nothing");
        a.now = 520; Check(GoTwo(a, follow, &off, 0).follow && !off.open, "window 0: A's chain back is still A's swing");
    }
    {   // no Grace at all (a window but no game clock): the single Follow slot still drops A's follow-up
        g::Follow follow; Fake a, b; TwoZealots(a, b, follow, nullptr);
        a.now = 520; Check(!GoTwo(a, follow, nullptr, 0).cancel, "no Grace: A's chain back needs a press");
    }
}

// A live press can defend while draw visibility/geometry is unavailable. A geometry-free
// resource guard is not a substitute for a press, grace admission or distance-only follow-up.
static void DrawGuard() {
    {g::Follow follow; Fake f; f.pose = false; f.guard = true;
        const auto o = Go(f, kEm10, follow);
        Check(o.cancel && o.consumed && f.consumes == 1, "fresh held press blocks while sword is drawing");
        f.now += 16;
        Check(Go(f, kEm10, follow).follow && f.consumes == 1, "fallback keeps the known swing without a second press");}
    {g::Follow follow; Fake f; f.pose = false; f.guard = true; f.At(500, -5);
        Check(Go(f, kEm10, follow).cancel, "drawing fallback covers a lateral sweep");}
    {g::Follow follow; Fake f; f.pose = false; f.guard = true; f.At(0, -500);
        Check(!Go(f, kEm10, follow).cancel && !f.consumes && !f.guard_reads, "rear draw contact does not read or consume fallback input");}
    for (unsigned bad = 0; bad < 7; ++bad) {
        g::Follow follow; Fake f; f.pose = false; f.guard = true;
        if (bad == 0) f.guard = false;
        if (bad == 1) f.peek = false;
        if (bad == 2) f.refresh = false;
        if (bad == 3) f.press = false;
        if (bad == 4) f.front_ok = false;
        if (bad == 5) f.blocked = true;
        if (bad == 6) f.d.power = 0;
        Check(!Go(f, kEm10, follow).cancel && !follow.armed, "fallback never skips resource, input, geometry, action or grab gates");
    }
    {g::Follow follow; Fake f; f.pose = false; f.guard = true;
        const auto o = Go(f, kEm10, follow, g::Mode::Observe);
        Check(!o.cancel && o.would_block && !f.consumes && !follow.armed, "observe draw fallback reports without consuming or following");}
    {g::Follow follow; Fake f; f.pose = false; f.guard = true;
        Check(!Go(f, kEm10, follow, g::Mode::Active, true, true).cancel && !f.consumes, "pose-free fallback does not reuse an already consumed handoff");}
    {g::Follow follow; g::Grace grace; grace.Configure(200); grace.Open(0, 1); Fake f;
        f.pose = false; f.guard = true; f.peek = false;
        Check(!GoGrace(f, kEm10, follow, grace, 1).cancel && !grace.blocked, "open grace alone cannot admit a hidden sword");
        f.peek = true;
        const auto o = GoGrace(f, kEm10, follow, grace, 2);
        Check(o.cancel && o.consumed && !o.grace && !grace.blocked, "draw fallback inside grace still consumes its own press");}
    {g::Follow follow; Fake f;
        Check(Go(f, kRegen, follow).cancel, "arm distance-only follow before hidden pose");
        f.pose = false; f.guard = true; f.press = f.peek = false; f.now += 16;
        Check(!Go(f, kRegen, follow).cancel, "draw fallback does not extend distance-only follow without a press");}
}

int main(int argc, char** argv) {
    DrawGuard(); Classes(); Thrown(); Settings(); Gates(); Handoff(); Precedence(); FollowUp(); Observe(); Faults(); GraceWindow(); ChainSwing(); TwoChains(); ChainsawSwing();
    if (argc > 1) RealImage(argv[1]);
    std::printf("parry general block: %u checks passed\n", checks);
    return 0;
}

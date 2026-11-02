// Direct contacts: native pins, conservative cliff lookup and consumable input.
#define main GeneralFixtureMain
#include "test_parry_general_block.cpp"
#undef main
#include "../../dll/parry_direct_runtime.h"
namespace d = link_runtime::parry::direct;
namespace defense = link_runtime::defense;
struct DirectSources : Fake {
    defense::ParryWindow window{};
    bool safe = true;
    double game = 1000.0;   // game time in 60 Hz frames (the cart's continuity clock)
    double Game() {return game;}
    DirectSources() {
        defense::UpdateParryButton(window, false, true, 990, 750);
        defense::UpdateParryButton(window, true, true, 1000, 750);
    }
    defense::PressAge Press() {return defense::PressAt(window, now);}
    bool Safe() {return safe;}
    bool Consume() {
        const bool ok = defense::ConsumeParry(window, now, true);
        if (ok) ++consumes;
        return ok;
    }
};
static void DirectPolicy() {
    const auto c = Hit(0x146172);
    DirectSources s;
    Check(d::Decide(s, c).cancel && s.consumes == 1, "direct fresh press blocks once without fabricated ATK_INFO");
    Check(!d::Decide(s, c).cancel, "consumed press cannot block another contact");
    for (unsigned bad = 0; bad < 8; ++bad) {
        DirectSources n;
        if (bad == 0) n.window = {};
        if (bad == 1) n.now = 1751;
        if (bad == 2) n.At(0, -500);
        if (bad == 3) n.pose = false;
        if (bad == 4) n.blocked = true;
        if (bad == 5) n.refresh = false;
        if (bad == 6) n.front_ok = false;
        if (bad == 7) n.safe = false;
        Check(!d::Decide(n, c).cancel && !n.consumes, "rejected contact preserves input and native damage");
    }
    DirectSources side;
    side.At(500, 0);
    Check(d::Decide(side, c).cancel, "side shares tier-2 cone");
    DirectSources draw;
    draw.pose = false;
    draw.guard = true;
    Check(d::Decide(draw, c).pose_fallback, "drawing guard fallback");
    DirectSources boundary;
    boundary.now = 1750;
    Check(d::Decide(boundary, c).cancel, "inclusive configured press boundary");
    Check(!s.descriptor_reads, "fixed-damage routes never read ATK_INFO");
}
static void DirectPins(const char* path) {
    Image img;
    FILE* f = nullptr;
    Check(!fopen_s(&f, path, "rb") && f, "open pinned image");
    std::fseek(f, 0, SEEK_END);
    img.bytes.resize(static_cast<size_t>(std::ftell(f)));
    std::rewind(f);
    Check(std::fread(img.bytes.data(), 1, img.bytes.size(), f) == img.bytes.size(), "read pinned image");
    std::fclose(f);
    for (unsigned relocated = 0; relocated != 2; ++relocated) {
        if (relocated) Relocate(img, 0x1000000);
        for (unsigned route = 0; route < d::kCount; ++route) {
            const auto r = static_cast<d::Route>(route);
            if (r == d::Route::Novistador) {
                Check(!d::Qualify(img, img.base, r), "unproven deferred-effects group remains unavailable");
                continue;
            }
            Check(d::Qualify(img, img.base, r), "all route pins qualify at preferred and relocated bases");
            const auto pins = d::Pins(r);
            for (size_t j = 0; j < pins.count; ++j) {
                const auto& pin = pins.data[j];
                for (unsigned i = 0; i < std::strlen(pin.hex) / 2; ++i) {
                    img.bytes[pin.rva + i] ^= 1;
                    Check(!d::Qualify(img, img.base, r), "any changed byte disables its entire route group");
                    img.bytes[pin.rva + i] ^= 1;
                }
            }
        }
    }
}
struct CliffMemory {
    uint32_t base = 0x10000000, global = 0x20000000, emi = 0x21000000;
    int32_t count = 1;
    bool fault = false, none = false;
    unsigned char entries[4 * 64]{};
    bool Read(uint32_t at, void* out, size_t n) {
        if (fault) return false;
        if (at == base + 0x806F3C && n == 4) {std::memcpy(out, &global, 4); return true;}
        if (at == global + 0x4F30 && n == 4) {const uint32_t v = none ? 0 : emi; std::memcpy(out, &v, 4); return true;}
        if (at == emi && n == 4) {std::memcpy(out, &count, 4); return true;}
        if (at < emi + 8 || at - (emi + 8) + n > sizeof(entries)) return false;
        std::memcpy(out, entries + at - (emi + 8), n);
        return true;
    }
    void Point(unsigned i, uint8_t type, float x, float z) {
        entries[i * 64] = type;
        std::memcpy(entries + i * 64 + 4, &x, 4);
        std::memcpy(entries + i * 64 + 12, &z, 4);
    }
};
static void CliffPolicy() {
    CliffMemory m;
    const float player[3]{};
    m.Point(0, 0xD, 2000, 0);
    Check(d::Cliff(m, m.base, player) == d::CliffResult::Near, "exactly 2m cliff is excluded");
    m.Point(0, 0xD, 2001, 0);
    Check(d::Cliff(m, m.base, player) == d::CliffResult::Clear, "outside cliff radius");
    m.Point(0, 3, 0, 0);
    Check(d::Cliff(m, m.base, player) == d::CliffResult::Clear, "only EMI type D is a cliff");
    m.count = 4;
    m.Point(3, 0xD, 0, -1000);
    Check(d::Cliff(m, m.base, player) == d::CliffResult::Near, "scan all records at native 64-byte stride");
    m.count = 4097;
    Check(d::Cliff(m, m.base, player) == d::CliffResult::Fault, "unbounded count fails closed");
    m.none = true;
    Check(d::Cliff(m, m.base, player) == d::CliffResult::Clear, "null EMI matches native no-cliff return");
    m.fault = true;
    Check(d::Cliff(m, m.base, player) == d::CliffResult::Fault, "faulting read cannot authorize high kick");
}
static void DirectFollow() {
    d::Follows follows;
    d::Contact one{}, two{};
    one.actor.owner = 0x10000;
    two.actor.owner = 0x20000;
    p::input::Sample context{};
    context.frame = 10;
    follows.Arm(one, context, 1000);
    follows.Arm(two, context, 1000);
    Check(follows.Match(one, context, 1100) && follows.Match(two, context, 1100), "two independent owners retain bounded follow");
    for (uint64_t now = 1200; now <= 1500; now += 100)
        Check(follows.Match(one, context, now), "same contact inside absolute and gap bounds");
    Check(!follows.Match(one, context, 1501), "follow cannot renew its 500ms absolute bound");
    follows.Arm(one, context, 2000);
    ++one.actor.parts;
    Check(!follows.Match(one, context, 2010), "reused owner with new parts invalidates follow");
    --one.actor.parts;
    Check(!follows.Match(one, context, 2020), "retired identity never rearms without a press");
    follows.Arm(one, context, 3000);
    ++context.room;
    Check(!follows.Match(one, context, 3010), "room change retires follow");
    follows.Arm(one, context, 4000);
    ++one.actor.routine[1];
    Check(!follows.Match(one, context, 4010), "different attack retires follow");
}
struct ComposedSources : DirectSources {
    bool resource_ok = true;
    d::ResourceStamp resource{1, 2, 3, 4, 1};
    bool Resource(d::ResourceStamp& out) {out = resource; return resource_ok;}
};
static void DirectComposition() {
    ComposedSources s;
    s.pose = false;
    s.guard = true;
    d::Contact c{};
    c.actor.owner = 0x10000;
    d::Follows f;
    p::input::Sample context{};
    auto o = d::Run(s, c, context, f);
    Check(o.cancel && o.pose_fallback && s.consumes == 1, "initial drawing contact consumes press");
    s.now += 6;
    s.guard = false; // A consumed input cannot pass the fresh-press Guard.
    o = d::Run(s, c, context, f);
    Check(o.cancel && o.follow && s.consumes == 1, "same drawing contact at 165Hz follows the approved press");
    s.now += 16;
    Check(d::Run(s, c, context, f).cancel, "same drawing contact at 60Hz follows without a new press");
    ++s.resource.resource;
    Check(!d::Run(s, c, context, f).cancel, "resource change cannot inherit a consumed press");
    --s.resource.resource;
    Check(!d::Run(s, c, context, f).cancel, "resource changed back does not resurrect the old follow");
    for (unsigned bad = 0; bad < 9; ++bad) {
        ComposedSources a;
        d::Follows local;
        auto actor_contact = c;
        Check(d::Run(a, actor_contact, context, local).cancel, "arm composition test");
        a.now += 6;
        if (bad == 0) a.At(0, -500);
        if (bad == 1) a.blocked = true;
        if (bad == 2) a.safe = false;
        if (bad == 3) a.resource_ok = false;
        if (bad == 4) ++actor_contact.actor.parts;
        if (bad == 5) ++actor_contact.actor.attachment;
        if (bad == 6) ++a.resource.info;
        if (bad == 7) ++actor_contact.actor.guid;
        if (bad == 8) ++actor_contact.actor.attachment_guid;
        Check(!d::Run(a, actor_contact, context, local).cancel, "follow still rejects rear, blocked, fault or changed identities");
    }
}
static d::Contact CartContact() {
    d::Contact c{};
    c.route = d::Route::Cart;
    c.actor.owner = 0x10000;
    c.actor.vtable = 0x400000 + 0x712A60;
    c.actor.flags = 1;
    c.actor.guid = 17;
    c.actor.parts = 0x20000;
    c.actor.identity[0] = 0x3B;
    c.actor.identity[1] = 1;
    c.actor.routine[0] = 1;
    c.actor.routine[1] = 4;
    c.actor.routine[2] = 1;
    return c;
}
static void CartRunFollow() {
    auto c = CartContact();
    ComposedSources s;
    d::Follows f;
    p::input::Sample context{};
    Check(d::Run(s, c, context, f).cancel, "fresh frontal cart run consumes press");
    for (unsigned i = 0; i != 16; ++i) {
        s.now += 90;
        s.game += 5.4;   // 90 ms at 60 FPS
        ++context.frame;
        Check(d::Run(s, c, context, f).follow, "continuous cart run remains blocked past 500 ms");
    }
    s.At(0, -500);
    s.now += 6;
    Check(d::Run(s, c, context, f).follow, "same cart follows after crossing to rear");
    c.actor.routine[1] = 6;
    c.actor.routine[2] = 0;
    s.now += 6;
    Check(d::Run(s, c, context, f).follow, "final Cart_Run contact follows armed run");
    s.now += 6;
    Check(!d::Run(s, c, context, f).cancel, "final cart contact retires its slot");
    ComposedSources fresh;
    d::Follows empty;
    Check(!d::Run(fresh, c, context, empty).cancel && !fresh.consumes, "last-frame contact cannot start a block");
    for (unsigned bad = 0; bad != 9; ++bad) {
        ComposedSources n;
        d::Follows slots;
        auto changed = CartContact();
        auto ctx = context;
        Check(d::Run(n, changed, ctx, slots).cancel, "arm cart isolation case");
        n.now += 6;
        n.game += 0.36;
        if (bad == 0) n.game += 6.0;   // more than 6 frames of game time since the last contact
        if (bad == 1) ++changed.actor.guid;
        if (bad == 2) ++ctx.room;
        if (bad == 3) ++n.resource.resource;
        if (bad == 4) changed.actor.routine[2] = 0;
        if (bad == 5) n.safe = false;
        if (bad == 6) n.blocked = true;
        if (bad == 7) ++changed.actor.parts;
        if (bad == 8) ++changed.actor.owner;
        Check(!d::Run(n, changed, ctx, slots).cancel, "cart follow rejects gaps and changed context or instance");
    }
}
// hunt-findings P2-2: the blocked run's continuity is game time, not wall clock.
static void CartGameTime() {
    auto c = CartContact();
    p::input::Sample context{};
    {
        ComposedSources s; d::Follows f;
        Check(d::Run(s, c, context, f).cancel, "arm: fresh frontal cart run");
        s.now += 5000; s.game += 0.36; ++context.frame;   // the pause menu: wall time passes, game time does not
        Check(d::Run(s, c, context, f).follow, "a pause inside the run keeps it blocked");
        s.now += 400; s.game += 1.0; ++context.frame;      // one hitched frame
        Check(d::Run(s, c, context, f).follow, "a hitched frame keeps it blocked");
        for (unsigned i = 0; i != 40; ++i) {               // re4_fps 165: a contact every tick
            s.now += 6; s.game += 2.0 * 30.0 / 165.0; ++context.frame;
            Check(d::Run(s, c, context, f).follow, "165 FPS contacts every tick stay blocked");
        }
        s.now += 6; s.game += 5.9; ++context.frame;
        Check(d::Run(s, c, context, f).follow, "a gap just under 6 frames of game time still follows");
        c.actor.routine[1] = 6; c.actor.routine[2] = 0; s.now += 2000; s.game += 0.5; ++context.frame;
        Check(d::Run(s, c, context, f).follow, "the final 500-damage contact after a pause still follows the run");
    }
    c = CartContact();
    {
        ComposedSources s; d::Follows f;
        Check(d::Run(s, c, context, f).cancel, "arm: second run");
        s.now += 20; s.game += 6.2; ++context.frame;
        Check(!d::Run(s, c, context, f).cancel && s.consumes == 1, "a real gap (over 6 frames of game time) needs a new press");
    }
    {
        ComposedSources s; d::Follows f;
        Check(d::Run(s, c, context, f).cancel, "arm: third run");
        s.now += 6; s.game -= 3.0; ++context.frame;
        Check(!d::Run(s, c, context, f).cancel, "a game clock that went back ends the run");
    }
    {
        ComposedSources s; d::Follows f;
        Check(d::Run(s, c, context, f).cancel, "arm: fourth run");
        s.now += 6; s.game = NAN; ++context.frame;
        Check(!d::Run(s, c, context, f).cancel, "an unknown game time fails closed (native damage)");
    }
}
// hunt-findings P2-1: box first. em39 tests the box (0x146172) before the leg spheres (0x15585F) in each frame; a box
// block must cover the same kick's sphere samples, which reach tier 2 with the press already spent.
static void KickBoxFirst() {
    auto krauser = [] {
        d::Contact c{};
        c.route = d::Route::Krauser;
        c.actor.owner = 0x10000;
        c.actor.routine[0] = 1;
        c.actor.routine[1] = 0x2B;
        return c;
    };
    auto sphere = [](uint32_t esi) {auto h = Hit(0x15585F); h.producer_esi = esi; return h;};
    p::input::Sample context{};
    {
        ComposedSources s; d::Follows f; const auto c = krauser();
        auto box = d::Run(s, c, context, f);
        Check(box.cancel && !box.follow && s.consumes == 1, "the box contact is blocked first with the press");
        d::FollowSlot* slot = nullptr;
        Check(d::FollowKick(s, sphere(c.actor.owner), c, context, f, &slot) && slot && slot->kick && s.consumes == 1,
              "the same frame's sphere sample follows the box block without a press");
        s.At(0, -500); s.now += 15;
        Check(d::FollowKick(s, sphere(c.actor.owner), c, context, f), "a later sphere sample at Link's back is the same kick");
        s.now += 333;
        Check(d::FollowKick(s, sphere(c.actor.owner), c, context, f), "kick gap: 333 ms later still the same kick");
        s.At(0, 500);
        Check(d::Run(s, c, context, f).follow, "and the box route still follows it too");
        s.now += 600;
        Check(d::FollowKick(s, sphere(c.actor.owner), c, context, f), "600 ms after the last contact: inside the 700 ms kick gap");
        s.now = 1000 + 1500;
        Check(d::FollowKick(s, sphere(c.actor.owner), c, context, f), "kick age includes 1500 ms");
        ++s.now;
        Check(!d::FollowKick(s, sphere(c.actor.owner), c, context, f), "a kick cannot renew its absolute lifetime");
    }
    for (unsigned bad = 0; bad != 11; ++bad) {
        ComposedSources s; d::Follows f; auto c = krauser(); auto ctx = context;
        Check(d::Run(s, c, ctx, f).cancel, "arm box-first case");
        auto h = sphere(c.actor.owner);
        s.now += 20;
        if (bad == 0) ++h.outer_caller;               // another producer
        if (bad == 1) ++h.producer_esi;               // another Krauser
        if (bad == 2) c.actor.routine[1] = 0x2C;      // another kick
        if (bad == 3) ++c.actor.guid;                 // another instance
        if (bad == 4) ++ctx.room;                     // another room
        if (bad == 5) ++s.resource.resource;          // another Link resource
        if (bad == 6) s.safe = false;
        if (bad == 7) s.blocked = true;
        if (bad == 8) s.now += 701;                   // past the kick gap
        if (bad == 9) c.actor.routine[1] = 0x10;      // not a kick routine
        if (bad == 10) c.route = d::Route::Bats;
        Check(!d::FollowKick(s, h, c, ctx, f) && s.consumes == 1, "box-first bridge rejects other producers, kicks, contexts and gaps");
    }
    {   // A box block of a non-kick contact never arms the bridge; a box follow-up alone is not a sphere follow.
        ComposedSources s; d::Follows f; auto c = krauser(); c.actor.routine[1] = 0x10;
        Check(d::Run(s, c, context, f).cancel, "non-kick box contact blocks");
        Check(!d::FollowKick(s, sphere(c.actor.owner), c, context, f), "but arms no kick bridge");
    }
}
static void ActorContracts() {
    unsigned char bytes[0x400]{};
    const uint32_t owner = reinterpret_cast<uint32_t>(bytes);
    const uint32_t base = 0x400000, vtable = base + 0x712A60, flags = 1, parts = owner + 0x350;
    std::memcpy(bytes, &vtable, 4);
    std::memcpy(bytes + 4, &flags, 4);
    std::memcpy(bytes + 0xF4, &parts, 4);
    bytes[0xFC] = 1;
    bytes[0xFD] = 4;
    bytes[0xFE] = 1;
    bytes[0x100] = 0x3B;
    bytes[0x101] = 1;
    g::Memory memory;
    d::Actor actor{};
    Check(d::ReadActor(memory, base, d::Route::Cart, owner, actor), "read exact running cart instance");
    bytes[0xFE] = 0;
    Check(!d::ReadActor(memory, base, d::Route::Cart, owner, actor), "cart setup is not an active run");
    bytes[0xFD] = 6;
    Check(d::ReadActor(memory, base, d::Route::Cart, owner, actor), "read terminal contact for follow-only admission");
    const uint32_t krauser = base + 0x7127CC;
    std::memcpy(bytes, &krauser, 4);
    bytes[0x100] = 0x39;
    bytes[0xFD] = 0x2B;
    Check(d::ReadActor(memory, base, d::Route::Krauser, owner, actor), "read sphere owner as Krauser high kick");
    bytes[0xFD] = 0x2C;
    Check(d::ReadActor(memory, base, d::Route::Krauser, owner, actor), "read low kick owner");
    for (unsigned offset : {0U, 4U, 0xFCU, 0xFDU, 0x100U}) {
        bytes[offset] ^= 1;
        Check(!d::ReadActor(memory, base, d::Route::Krauser, owner, actor), "changed owner contract refuses bridge");
        bytes[offset] ^= 1;
    }
}
static void KickGap() {
    ComposedSources s;
    d::Follows f;
    d::Contact c{};
    c.route = d::Route::Krauser;
    c.actor.owner = 0x10000;
    c.actor.routine[0] = 1;
    c.actor.routine[1] = 0x2B;
    p::input::Sample context{};
    auto candidate = Hit(0x15585F);
    candidate.producer_esi = c.actor.owner;
    g::Follow tier_follow;
    g::Outcome tier_out;
    s.owner = 0; // Native tier 2 has no attributed owner for this producer.
    Check(g::Run(s, candidate, {g::Mode::Active}, tier_follow, false, tier_out) && tier_out.consumed,
          "real tier-2 policy spends the sphere press");
    g::Applied applied{tier_out.cancel, !tier_out.follow && !tier_out.grace, tier_out.grace};
    Check(d::ShareKick(s, candidate, applied, c, context, f), "fresh sphere press shares same-owner kick");
    Check(s.consumes == 1 && !s.window.armed, "shared kick never consumes another press");
    s.now += 333;
    Check(d::Run(s, c, context, f).follow, "Krauser sphere-to-box gap remains covered by the same kick");
    s.now += 700;
    Check(d::Run(s, c, context, f).follow, "kick gap includes 700 ms boundary");
    s.now = 2500;
    Check(d::Run(s, c, context, f).follow, "kick age includes 1500 ms boundary");
    ++s.now;
    Check(!d::Run(s, c, context, f).cancel, "kick cannot renew its absolute lifetime");
    for (unsigned bad = 0; bad != 15; ++bad) {
        ComposedSources n;
        d::Follows slots;
        auto hit = candidate;
        auto actor_contact = c;
        auto ctx = context;
        auto result = applied;
        if (bad == 0) result.cancel = false;
        if (bad == 1) result.effects = false;
        if (bad == 2) result.grace = true;
        if (bad == 3) ++hit.outer_caller;
        if (bad == 4) ++hit.producer_esi;
        if (bad == 5) n.safe = false;
        if (bad == 6) n.resource_ok = false;
        if (bad == 7) n.blocked = true;
        const bool armed = d::ShareKick(n, hit, result, actor_contact, ctx, slots);
        if (bad < 8) {Check(!armed, "uncommitted or unsafe sphere contact never shares a kick"); continue;}
        Check(armed, "arm kick invalidation case");
        n.window = {};
        n.now += 333;
        if (bad == 8) ++actor_contact.actor.routine[2];
        if (bad == 9) ++actor_contact.actor.routine[3];
        if (bad == 10) ++actor_contact.actor.guid;
        if (bad == 11) ++ctx.room;
        if (bad == 12) ++n.resource.resource;
        if (bad == 13) n.now += 368;
        if (bad == 14) n.At(0, -500);
        Check(!d::Run(n, actor_contact, ctx, slots).cancel, "kick follow rejects new routines, identities, context, rear and long gaps");
    }
}
#ifdef DIRECT_ROUTE_FIXTURE_INCLUDED
int DirectRoutesFixtureMain(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
    Check(argc <= 2, "optional pinned mapped image");
    DirectPolicy();
    CliffPolicy();
    DirectFollow();
    DirectComposition();
    CartRunFollow();
    CartGameTime();
    KickBoxFirst();
    KickGap();
    ActorContracts();
    if (argc == 2) DirectPins(argv[1]);
    std::printf("parry direct routes: %u checks passed%s\n", checks, argc == 2 ? " (pinned image: route pins checked)" : " (no image: pin checks skipped)");
    return 0;
}

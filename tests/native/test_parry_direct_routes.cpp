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
// The other routes' follow-ups (a Krauser blow or kick, bats, the tree swing) expire on game time as well: a pause or
// the case right after the block must not drop them. Same bounds as before in 60 Hz frames: 6 / 30, a kick 42 / 90.
static void FollowGameTime() {
    auto krauser = [](unsigned char routine) {
        d::Contact c{};
        c.route = d::Route::Krauser;
        c.actor.owner = 0x10000;
        c.actor.routine[0] = 1;
        c.actor.routine[1] = routine;
        return c;
    };
    auto sphere = [](uint32_t esi) {auto h = Hit(0x15585F); h.producer_esi = esi; return h;};
    p::input::Sample context{};
    {
        ComposedSources s; d::Follows f; const auto c = krauser(0x10);
        Check(d::Run(s, c, context, f).cancel && s.consumes == 1, "arm: a non-kick blow blocked with the press");
        s.now += 5000; s.game += 0.36; ++context.frame;   // the pause menu: wall time passes, game time does not
        Check(d::Run(s, c, context, f).follow, "a pause after the block keeps its follow-up");
        s.now += 6; s.game += 6.2; ++context.frame;
        Check(!d::Run(s, c, context, f).cancel && s.consumes == 1, "a real gap (over 6 frames of game time) ends it, no second press");
    }
    {   // The wall clock stands still: game time alone bounds the follow-up.
        ComposedSources s; d::Follows f; const auto c = krauser(0x10);
        Check(d::Run(s, c, context, f).cancel, "arm: second blow");
        for (unsigned i = 0; i != 6; ++i) {
            s.game += 5.0; ++context.frame;
            Check(d::Run(s, c, context, f).follow, "contacts up to 30 frames (500 ms) of game time after the block follow");
        }
        s.game += 0.1; ++context.frame;
        Check(!d::Run(s, c, context, f).cancel && s.consumes == 1, "a follow-up cannot renew its 30-frame lifetime");
    }
    {
        ComposedSources s; d::Follows f; const auto c = krauser(0x10);
        Check(d::Run(s, c, context, f).cancel, "arm: third blow");
        s.now += 6; s.game -= 3.0; ++context.frame;
        Check(!d::Run(s, c, context, f).cancel, "a game clock that went back ends the follow-up");
    }
    {
        ComposedSources s; d::Follows f; const auto c = krauser(0x2B);
        Check(d::Run(s, c, context, f).cancel, "arm: a kick blocked on the box");
        s.now += 5000; s.game += 1.0;
        Check(d::FollowKick(s, sphere(c.actor.owner), c, context, f), "a pause inside the kick keeps its sphere samples followed");
        s.game += 42.1;
        Check(!d::FollowKick(s, sphere(c.actor.owner), c, context, f) && s.consumes == 1,
              "over 42 frames (700 ms) of game time since the last sample: no longer that kick");
    }
    {
        ComposedSources s; d::Follows f; const auto c = krauser(0x2B);
        Check(d::Run(s, c, context, f).cancel, "arm: second kick");
        for (unsigned i = 0; i != 3; ++i) {
            s.game += 30.0;
            Check(d::FollowKick(s, sphere(c.actor.owner), c, context, f), "kick samples up to 90 frames (1500 ms) after the block follow");
        }
        s.game += 0.1;
        Check(!d::FollowKick(s, sphere(c.actor.owner), c, context, f), "a kick cannot renew its 90-frame lifetime");
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
        s.game += 90.1; Check(!d::FollowKick(s, sphere(c.actor.owner), c, context, f), "a kick cannot renew its absolute lifetime");
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
        if (bad == 8) {s.now += 701; s.game += 42.1;} // past the kick gap (game time decides)
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
    s.game += 90.1; Check(!d::Run(s, c, context, f).cancel, "kick cannot renew its absolute lifetime");
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
        if (bad == 13) {n.now += 368; n.game += 42.1;}
        if (bad == 14) n.At(0, -500);
        Check(!d::Run(n, actor_contact, ctx, slots).cancel, "kick follow rejects new routines, identities, context, rear and long gaps");
    }
}
// Review 17-nov: a sword action or the Mortal Draw is a soft block here too (HardBlocked() false). A Ctrl+tap parry
// starts the Shield Bash on its release: the contact already blocked keeps its follow-up through that clip, while a new
// contact still takes no press. ComposedSources has no HardBlocked(), so every block stays hard for it (cases above).
struct SoftSources : ComposedSources {bool HardBlocked() {return false;}};
static void DirectSoftBlock() {
    p::input::Sample context{};
    d::Contact c{};
    c.actor.owner = 0x10000;
    {
        SoftSources s; d::Follows f;
        Check(d::Run(s, c, context, f).cancel && s.consumes == 1, "arm: a contact blocked before the sword action");
        s.blocked = true; s.now += 6;
        const auto o = d::Run(s, c, context, f);
        Check(o.cancel && o.follow && s.consumes == 1 && f.slots[0].armed, "soft block: the blocked contact is still followed and its slot kept");
        ++s.resource.resource;
        Check(!d::Run(s, c, context, f).cancel && !f.slots[0].armed, "soft block: a changed Link resource still drops the follow");
    }
    {
        SoftSources s; d::Follows f;
        s.blocked = true;
        const auto o = d::Run(s, c, context, f);
        Check(!o.cancel && o.reason == p::line::Reason::InputBlocked && !s.consumes && s.window.armed && !f.slots[0].armed,
              "soft block: a new contact takes no press and arms nothing");
    }
    {   // every block is hard without the source: the slots are cleared, so nothing follows once it ends
        ComposedSources s; d::Follows f;
        Check(d::Run(s, c, context, f).cancel, "arm: before a hard block");
        s.blocked = true; s.now += 6;
        Check(!d::Run(s, c, context, f).cancel && !f.slots[0].armed, "hard block: the slots are cleared");
        s.blocked = false; s.now += 6;
        Check(!d::Run(s, c, context, f).cancel && s.consumes == 1, "after a hard block the same contact needs a new press");
    }
    {   // the cart run of the report: blocked, then the bash clip runs over its remaining contacts
        auto cart = CartContact(); auto ctx = context;
        SoftSources s; d::Follows f;
        Check(d::Run(s, cart, ctx, f).cancel, "arm: fresh frontal cart run");
        s.blocked = true;
        for (unsigned i = 0; i != 16; ++i) {
            s.now += 90; s.game += 5.4; ++ctx.frame;
            Check(d::Run(s, cart, ctx, f).follow, "soft block: the cart run stays blocked contact after contact");
        }
        cart.actor.routine[1] = 6; cart.actor.routine[2] = 0; s.now += 6; s.game += 0.36; ++ctx.frame;
        Check(d::Run(s, cart, ctx, f).follow && s.consumes == 1, "soft block: the final 500-damage contact follows the armed run");
    }
    {   // Krauser's kick: the box block's sphere samples through the bridge
        d::Contact kick{};
        kick.route = d::Route::Krauser;
        kick.actor.owner = 0x10000;
        kick.actor.routine[0] = 1;
        kick.actor.routine[1] = 0x2B;
        auto sphere = Hit(0x15585F); sphere.producer_esi = kick.actor.owner;
        SoftSources s; d::Follows f;
        Check(d::Run(s, kick, context, f).cancel && s.consumes == 1, "arm: a kick blocked on the box");
        s.blocked = true; s.now += 15; s.game += 0.9;
        Check(d::FollowKick(s, sphere, kick, context, f) && s.consumes == 1, "soft block: the kick's sphere samples are still followed");
        ComposedSources hard; d::Follows g2;
        Check(d::Run(hard, kick, context, g2).cancel, "arm: a kick before a hard block");
        hard.blocked = true;
        Check(!d::FollowKick(hard, sphere, kick, context, g2), "hard block: the bridge refuses");
    }
}
// The runtime's own sources: which input gates are hard. The sword actions' gate (sword_action_blocks, which keeps the
// button tracked) and the Mortal Draw refuse only a new press; the ocarina and the Blade Lock duel refuse the
// follow-ups too. Tier 2, its follow clock and the direct routes must agree.
static void BlockKinds() {
    g::Runtime r;
    g::NativeSources tier{r};
    g::FollowSources follow{r};
    d::Contact c{};
    p::input::Sample context{};
    d::NativeSources direct{r, c, context};
    auto kinds = [&](bool blocked, bool hard, const char* why) {
        Check(tier.Blocked() == blocked && follow.Blocked() == blocked && direct.Blocked() == blocked, why);
        Check(g::HardBlocked(tier, 0) == hard && g::HardBlocked(follow, 0) == hard && g::HardBlocked(direct, 0) == hard, why);
    };
    kinds(false, false, "no gate: nothing blocked");
    p::input::SetSwordActionBlocks(true); kinds(true, false, "a sword action is a soft block");
    p::input::SetSwordLockBlocks(true); kinds(true, true, "a Blade Lock over a sword action is hard");
    p::input::SetSwordLockBlocks(false); p::input::SetSwordActionBlocks(false);
    p::input::SetMortalBlocks(true); kinds(true, false, "the Mortal Draw is a soft block"); p::input::SetMortalBlocks(false);
    p::input::SetOcarinaBlocks(true); kinds(true, true, "the ocarina is a hard block"); p::input::SetOcarinaBlocks(false);
    p::input::SetSwordLockBlocks(true); kinds(true, true, "the Blade Lock duel is a hard block"); p::input::SetSwordLockBlocks(false);
    kinds(false, false, "gates released");
}
// Tier 2's follow-ups read game time when the runtime has it; the press age and the direct routes keep the wall clock.
static double fixture_game = 0;
static double FixtureGame() {return fixture_game;}
static d::FollowTime DirectFixtureGame(const p::input::Sample&) {return {9.5, 99, 7, 20};}

struct CrossedSources : ComposedSources {
    d::FollowFrame live{};
    d::FollowTime Game() {return d::pad_clock.AtContact(live);}
};
struct PadSchedule {
    HANDLE first = nullptr, unpause = nullptr;
    unsigned scenario = 0;
    d::FollowFrame sample{};
};
static DWORD WINAPI FixturePadThread(void* pointer) {
    auto& job = *static_cast<PadSchedule*>(pointer);
    auto& sample = job.sample;
    d::pad_clock.Publish(sample);
    SetEvent(job.first);
    if (WaitForSingleObject(job.unpause, 5000) != WAIT_OBJECT_0) return 0;
    // Recorded native PadRead frames. The contact thread runs no actor updates inside Options.
    for (++sample.frame; sample.frame <= 2054844; ++sample.frame) {
        const bool held = sample.frame >= 2054358 && sample.frame <= 2054843;
        sample.routine = held && job.scenario != 1 ? 6 : 3;
        sample.status[0] = held && job.scenario == 1 ? link_runtime::case_key_debounce::layout::kStaSubScrn : 0;
        if (sample.frame == 2054480) {
            if (job.scenario == 2) sample.routine = 4;
            if (job.scenario == 3) continue;
            if (job.scenario == 4) {auto backwards = sample; backwards.frame -= 10; d::pad_clock.Publish(backwards);}
            if (job.scenario == 5) {auto room = sample; ++room.room; d::pad_clock.Publish(room);}
            if (job.scenario == 6) d::pad_clock.Publish({});
        }
        d::pad_clock.Publish(sample);
        d::pad_clock.Publish(sample); // duplicate polls on the publisher do not age twice
    }
    return GetCurrentThreadId();
}
static void DirectMenuClock() {
    d::Contact tree{};
    tree.route = d::Route::Tree; tree.actor.owner = 0x09257058;
    tree.actor.attachment_guid = 31;
    for (unsigned scenario = 0; scenario < 11; ++scenario) {
        CrossedSources s;
        s.live.global = 0x017AA760; s.live.room = 0x119; s.live.frame = 2054356;
        s.live.routine = 3; s.live.dt = 30.f/165.f;
        PadSchedule job{};
        job.scenario = scenario; job.sample = s.live; --job.sample.frame;
        job.first = CreateEventW(nullptr,FALSE,FALSE,nullptr);
        job.unpause = CreateEventW(nullptr,FALSE,FALSE,nullptr);
        Check(job.first && job.unpause, "cross-thread fixture events");
        DWORD pad_thread = 0;
        HANDLE worker = CreateThread(nullptr, 0, FixturePadThread, &job, 0, &pad_thread);
        Check(worker && WaitForSingleObject(job.first,5000)==WAIT_OBJECT_0 && pad_thread!=GetCurrentThreadId(),
            "PadRead is a different native thread");
        p::input::Sample ctx{};
        ctx.frame=s.live.frame;ctx.global=s.live.global;ctx.room=s.live.room;
        d::follows = {};
        auto armed = s.Game();
        Check(armed.generation && armed.pad_frame+1==ctx.frame,"arm with the precise +1 native skew");
        Check(d::Run(s,tree,ctx,d::follows).cancel && d::follows.slots[0].generation==armed.generation,
            "fresh press saves generation on the contact worker");
        s.pose=false; s.guard=false; s.now+=3100;
        SetEvent(job.unpause);
        Check(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0,"PadRead completed all menu frames");
        DWORD actual=0; GetExitCodeThread(worker,&actual);
        CloseHandle(worker); CloseHandle(job.first); CloseHandle(job.unpause);
        Check(actual==pad_thread,"same publisher end to end");
        s.live.frame=2054845; ctx.frame=s.live.frame;
        auto matched=tree;
        if(scenario==7)++s.resource.resource;
        if(scenario==8)++matched.actor.attachment_guid;
        if(scenario==9)++s.live.frame;
        if(scenario==10)++s.live.global;
        const auto resumed=s.Game();
        if(scenario<2){
            Check(resumed.generation==armed.generation && resumed.menu==armed.menu+1 && resumed.pad_frame+1==ctx.frame,
                  "paused epoch and menu crossing survive without any contact update");
            Check(std::abs((resumed.game-armed.game)-3*link_runtime::game_clock::Step60(s.live.dt))<0.000001,
                  "three active intervals between native block and return");
        }
        const auto result=d::Run(s,matched,ctx,d::follows);
        Check(result.follow==(scenario<2) && s.consumes==1,
              "same owner tree resource and generation only; no new press");
        if(scenario<2)Check(d::follows.slots[0].menu==resumed.menu,"first real match consumes the diagnostic menu marker");
    }
    for(float dt:{1.f,0.5f,30.f/165.f}){
        d::FollowFrame f{};
        f.global=1234;f.room=0x119;f.frame=100;f.routine=3;f.dt=dt;
        d::FollowClock ledger;
        ledger.Observe(f);
        auto exact=ledger.AtContact(f);
        for(unsigned i=0;i<12;++i){++f.frame;ledger.Observe(f);}
        Check(std::abs(ledger.AtContact(f).game-exact.game-12*link_runtime::game_clock::Step60(dt))<0.00001,
              "gameplay at 30/60/165");
        const double prior=ledger.Time();
        ++f.frame; f.dt=0.75f;
        const auto projected=ledger.AtContact(f);
        ledger.Observe(f);
        Check(std::abs(projected.game-ledger.Time())<0.000001 && std::abs(ledger.Time()-prior-link_runtime::game_clock::Step60(dt))<0.000001,
              "published delta agrees on both sides of +1 when dt changes");
        ++f.frame;const double next=ledger.AtContact(f).game;
        f.routine=6;ledger.Observe(f);
        Check(ledger.Held() && std::abs(ledger.Time()-next)<0.000001 && std::abs(next-projected.game-1.5)<0.000001,
              "pre-menu final contact never ages backwards at the boundary");
        f.routine=3;Check(ledger.AtContact(f).game==d::kUnknownFollowTime,"same frame opposite menu state is ambiguous");
        ++f.frame;auto first=ledger.AtContact(f);ledger.Observe(f);
        Check(first.game==ledger.Time() && first.menu && std::abs(first.game-next)<0.000001,"a held interval contributes zero on +1 resume");
        auto invalid=f;invalid.frame+=2;Check(ledger.AtContact(invalid).game==d::kUnknownFollowTime,"reader cannot extrapolate two ticks");
        --invalid.frame;invalid.status[0]=link_runtime::case_key_debounce::layout::kStaSubScrn;
        Check(ledger.AtContact(invalid).game==d::kUnknownFollowTime,"case blocks reads even if preceding publisher was active");
        auto valid=ledger.AtContact(f);Check(valid.generation==first.generation && valid.game==first.game,"an invalid reader cannot change the ledger");
    }
    d::FollowSlot old{};old.generation=19;old.game_start=0;old.game_last=2;
    Check(d::Expired(old,{1,d::FollowTime{8,20,0,7}}), "identical time in another epoch expires");
    Check(!d::Expired(old,{1,d::FollowTime{8,19,0,7}}) && d::Expired(old,{1,d::FollowTime{8.0011,19,0,7}}),"unchanged six-frame gap");
    old.game_last=30;
    Check(!d::Expired(old,{1,d::FollowTime{30,19,0,7}}) && d::Expired(old,{1,d::FollowTime{30.0011,19,0,7}}),"unchanged thirty-frame age");
    d::Follows none{};
    none.Arm(tree,{},{1,d::FollowTime{}});
    Check(!none.slots[0].armed,"unreadable clock cannot arm unseen follow-up");

    d::Follows separate{};
    auto other=tree;
    other.actor.owner+=0x1000;
    p::input::Sample ctx{};
    const d::When valid{1,d::FollowTime{8,19,0,7}};
    const d::When incoherent{2,d::FollowTime{}};
    const d::ResourceStamp resource{1,2,3,4,1};
    separate.Arm(tree,ctx,valid,resource);
    separate.Arm(other,ctx,valid,resource);
    Check(!separate.Match(other,ctx,incoherent,resource),"incoherent contact fails to native damage");
    Check(!separate.slots[1].armed && separate.Match(tree,ctx,valid,resource),
          "incoherent contact retires its owner without discarding another owner's block");
    Check(!separate.Match(tree,ctx,{3,d::FollowTime{8,20,0,7}},resource),
          "coherent new generation still retires older blocks");
}
static void FollowRuntimeClock() {
    g::Runtime r;
    const uint64_t before = GetTickCount64();
    auto wall = [&](uint64_t v) {return v >= before && v - before < 60000;};
    Check(wall(g::FollowNow(r)), "no game clock: the follow-ups stay on the wall clock");
    r.game_now = &FixtureGame; fixture_game = 600.0;
    g::FollowSources follow{r};
    g::NativeSources tier{r};
    Check(g::FollowNow(r) == 10000 && follow.Now() == 10000, "game clock: tier 2's Decide reads game time as ms");
    Check(wall(tier.Now()), "the press age clock stays the wall clock");
    d::Contact c{};
    p::input::Sample context{};
    d::NativeSources direct{r, c, context};
    Check(wall(direct.Now()) && direct.Game().game == 600.0, "the direct routes keep their own pair of clocks");
    d::direct_game_now = &DirectFixtureGame;
    Check(wall(direct.Now()) && direct.Game().game == 9.5 && follow.Now() == 10000, "dedicated clock preserves tier 2 and real press deadlines");
    d::direct_game_now = nullptr;
    fixture_game = 0.0;
    Check(wall(g::FollowNow(r)) && wall(follow.Now()), "an unreadable game time falls back to the wall clock");
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
    FollowGameTime();
    DirectMenuClock();
    KickBoxFirst();
    KickGap();
    DirectSoftBlock();
    BlockKinds();
    FollowRuntimeClock();
    ActorContracts();
    if (argc == 2) DirectPins(argv[1]);
    std::printf("parry direct routes: %u checks passed%s\n", checks, argc == 2 ? " (pinned image: route pins checked)" : " (no image: pin checks skipped)");
    return 0;
}

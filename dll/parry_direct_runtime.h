#pragma once
#include "parry_direct_routes.h"
#include "game_clock.h"
#include <cmath>

namespace link_runtime::parry::direct {
// Fixed-damage contacts share the line adapter's input, pose and cone sources.
// They have no native ATK_INFO and must never borrow a spherical WrapperToken.
template <class Sources> line::Outcome Decide(Sources& s, const Candidate& c) {
    line::Outcome o;
    o.reason = line::Reason::InputBlocked;
    if (s.Blocked()) return o;
    const bool pose = s.Pose();
    o.reason = line::Reason::Front;
    if (!s.Front(c, o.front)) return o;
    o.reason = line::Reason::Rear;
    if (!general::Parryable(o.front)) return o;
    o.reason = line::Reason::Pose;
    if (!pose) {
        if (!s.Guard(true)) return o;
        o.pose_fallback = true;
    }
    o.reason = line::Reason::Window;
    if (!s.Refresh()) return o;
    o.press = s.Press();
    if (!o.press.down || !s.Safe() || !s.Consume()) return o;
    o.reason = line::Reason::Blocked;
    o.cancel = true;
    return o;
}
struct Actor {
    uint32_t owner = 0, vtable = 0, parts = 0, flags = 0, attachment = 0, guid = 0, attachment_guid = 0;
    uint8_t routine[4]{}, identity[2]{};
};
inline bool Same(const Actor& a, const Actor& b) {
    return a.owner == b.owner && a.vtable == b.vtable && a.parts == b.parts &&
        a.flags == b.flags && a.attachment == b.attachment && a.guid == b.guid && a.attachment_guid == b.attachment_guid &&
        !std::memcmp(a.routine, b.routine, 4) && !std::memcmp(a.identity, b.identity, 2);
}
template <class Mem> bool ReadActor(Mem& m, uint32_t base, Route route, uint32_t who, Actor& a) {
    const auto index = static_cast<unsigned>(route);
    if (index >= kCount || route == Route::Novistador || !shield::Range(who, 0x32E)) return false;
    const auto& spec = kSpecs[index];
    a.owner = who;
    if (!m.Read(who, &a.vtable, 4) || a.vtable != base + spec.vtable ||
        !m.Read(who + 4, &a.flags, 4) || !(a.flags & 1) ||
        !m.Read(who + 0xF4, &a.parts, 4) || !shield::Range(a.parts, 4) ||
        !m.Read(who + 0xF8, &a.guid, 4) ||
        !m.Read(who + 0xFC, a.routine, 4) || a.routine[0] != 1 ||
        !m.Read(who + 0x100, a.identity, 2) || a.identity[0] != spec.id) return false;
    switch (route) {
    case Route::Krauser: return a.routine[1] == 0x2B || a.routine[1] == 0x2C;
    case Route::Bats: return a.routine[1] == 5 && a.routine[2] == 2;
    case Route::Cart: return a.identity[1] == 1 &&
        ((a.routine[1] == 4 && a.routine[2] == 1) || (a.routine[1] == 6 && a.routine[2] == 0));
    case Route::Tree: {
        uint8_t id = 0, latch = 0;
        return a.routine[1] == 0xE && shield::Range(who, 0xA57) &&
            m.Read(who + 0x908, &a.attachment, 4) && shield::Range(a.attachment, 0x101) &&
            m.Read(a.attachment + 0x100, &id, 1) && id == 0x49 &&
            m.Read(a.attachment + 0xF8, &a.attachment_guid, 4) &&
            m.Read(who + 0xA56, &latch, 1) && !latch;
    }
    default: return false;
    }
}
struct Contact {Route route{}; Actor actor{}; Candidate candidate{}; uint32_t caller = 0;};
template <class Mem> bool ReadContact(Mem& m, uint32_t base, Route route, const MachineFrame& f, Contact& c) {
    const bool edi = route == Route::Krauser || route == Route::Cart;
    c.route = route;
    if (!ReadActor(m, base, route, edi ? f.edi : f.esi, c.actor)) return false;
    if (route == Route::Krauser) {
        if (!StackSpan(f.ebp, 8, __readfsdword(8), __readfsdword(4)) ||
            !m.Read(f.ebp + 4, &c.caller, 4)) return false;
        const uint32_t ret = c.caller - base;
        if (!((ret == 0x152D24 && c.actor.routine[1] == 0x2B) ||
              (ret == 0x152E48 && c.actor.routine[1] == 0x2C))) return false;
    }
    const uint32_t who = c.actor.owner;
    // The attacker position supplies direction, not a fabricated collision descriptor.
    c.candidate = {0, 0, who + 0x94, who + 0x110, base + kSpecs[static_cast<unsigned>(route)].site, f.ebp, who, who};
    return true;
}
struct ResourceStamp {uint32_t info = 0, resource = 0, part0 = 0, part10 = 0, profile = 0, player_guid = 0;};
inline bool Same(const ResourceStamp& a, const ResourceStamp& b) {
    return a.info == b.info && a.resource == b.resource && a.part0 == b.part0 &&
        a.part10 == b.part10 && a.profile == b.profile && a.player_guid == b.player_guid;
}
// When a contact happens: wall-clock ms and game time in 60 Hz frames (game_clock.h: paused with the game, the same at
// 60 and 165 FPS). A plain ms converts implicitly with an unknown game time (NaN).
struct When {
    uint64_t ms = 0;
    double game = NAN;
    When(uint64_t wall = 0, double game_time = NAN) : ms(wall), game(game_time) {}
};
struct FollowSlot {
    bool armed = false;
    Route route{};
    Actor actor{};
    input::Sample context{};
    ResourceStamp resource{};
    uint64_t start = 0, last = 0;
    bool kick = false;
    double game_last = NAN;   // the cart run's continuity (game time of the last contact)
    bool bridged = false;     // a sphere sample of this kick was already followed (one log line per kick)
};
// The continuity of a blocked cart run (hunt-findings P2-2, 1-nov): contacts at most 6 frames of game time apart (the
// old 100 ms at 60 FPS), with no cap on the whole run. It was 100 ms of wall clock: a hitched frame or the pause menu
// broke the blocked run and its 500-damage contact landed after the parry.
inline constexpr double kCartGapFrames = 6.0;
// Krauser's high (0x2B) and low (0x2C) kick routines: the attack both the box and the leg spheres belong to.
inline bool KickRoutine(const Actor& a) {return a.routine[0] == 1 && (a.routine[1] == 0x2B || a.routine[1] == 0x2C);}
inline bool CartRunning(const Actor& a) {return a.routine[0] == 1 && a.routine[1] == 4 && a.routine[2] == 1;}
inline bool CartFinal(const Actor& a) {return a.routine[0] == 1 && a.routine[1] == 6 && a.routine[2] == 0;}
inline bool SameCart(const Actor& armed, const Actor& current) {
    if (!CartRunning(armed) || (!CartRunning(current) && !CartFinal(current))) return false;
    Actor normalized = current;
    normalized.routine[1] = armed.routine[1];
    normalized.routine[2] = armed.routine[2];
    return Same(armed, normalized);
}
inline bool Expired(const FollowSlot& s, const When& now) {
    if (s.route == Route::Cart)   // game time only; an unknown or backwards clock ends the run (fail to native damage)
        return !std::isfinite(now.game) || !std::isfinite(s.game_last) || now.game + game_clock::kEpsilon < s.game_last ||
               game_clock::Exceeded(now.game - s.game_last, kCartGapFrames);
    const uint64_t gap = s.kick ? general::Follow::kSwingGapMs : 100;
    const uint64_t age = s.kick ? general::Follow::kSwingMaxMs : 500;
    return now.ms < s.last || now.ms - s.last > gap || now.ms - s.start > age;
}
struct Follows {
    FollowSlot slots[8]{};
    void Retire(uint32_t who) {
        for (auto& s : slots) if (s.actor.owner == who) s.armed = false;
    }
    // kick_only: only a slot armed for a whole Krauser kick (the box/sphere bridge in either order).
    // matched (optional): the slot that matched.
    bool Match(const Contact& c, const input::Sample& context, const When& now, const ResourceStamp& resource = {},
               bool kick_only = false, FollowSlot** matched = nullptr) {
        for (auto& s : slots) {
            if (!s.armed) continue;
            if (Expired(s, now) ||
                !input::Context(s.context, context) || !Same(s.resource, resource) || context.frame < s.context.frame) {s.armed = false; continue;}
            if (s.actor.owner != c.actor.owner) continue;
            if (kick_only && !s.kick) continue;
            if (s.route != c.route || !(c.route == Route::Cart ? SameCart(s.actor, c.actor) : Same(s.actor, c.actor))) {
                s.armed = false;
                continue;
            }
            s.last = now.ms;
            s.game_last = now.game;
            s.context.frame = context.frame;
            if (c.route == Route::Cart && CartFinal(c.actor)) s.armed = false;
            if (matched) *matched = &s;
            return true;
        }
        return false;
    }
    void Arm(const Contact& c, const input::Sample& context, const When& now, const ResourceStamp& resource = {}, bool kick = false) {
        unsigned slot = 0;
        for (unsigned i = 0; i < std::size(slots); ++i) {
            if (!slots[i].armed || Expired(slots[i], now)) {slot = i; break;}
            if (slots[i].start < slots[slot].start) slot = i;
        }
        slots[slot] = {true, c.route, c.actor, context, resource, now.ms, now.ms, kick, now.game, false};
    }
};
// Sources: When Clock() = {Now() ms, Game() frames}.
template <class Sources> When Clock(Sources& s) {return When{s.Now(), s.Game()};}
// Only a fresh tier-2 press block may bridge the sphere and box windows of one kick.
template <class Sources> bool ShareKick(Sources& s, const Candidate& candidate, const general::Applied& applied,
                                      const Contact& c, const input::Sample& context, Follows& following) {
    if (!applied.cancel || !applied.effects || applied.grace ||
        candidate.outer_caller - s.ImageBase() != 0x15585F || c.route != Route::Krauser ||
        !c.actor.owner || c.actor.owner != candidate.producer_esi || !KickRoutine(c.actor)) return false;
    ResourceStamp resource{};
    if (s.Blocked() || !s.Resource(resource) || !s.Safe()) return false;
    following.Arm(c, context, Clock(s), resource, true);
    return true;
}
// The other order (hunt-findings P2-1, 1-nov): em39's dispatcher tests the box contact (0x146172) before the leg spheres
// (0x15585F) in each frame, so a kick can be press-blocked on the box first. Its sphere samples then reached tier 2 with
// the press already spent and no follow-up of their own: the parry showed and the damage landed. A sphere sample by the
// same Krauser (ESI), still in that kick (same routine and identity), in the same input context and Link resource,
// inside the kick bounds of a slot that kick armed (the box block, or ShareKick) is that kick: cancelled silently (no
// press, sparks or voice), wherever it lands, like tier 2's owner follow-ups.
template <class Sources> bool FollowKick(Sources& s, const Candidate& candidate, const Contact& c, const input::Sample& context,
                                        Follows& following, FollowSlot** matched = nullptr) {
    if (candidate.outer_caller - s.ImageBase() != 0x15585F || c.route != Route::Krauser || !c.actor.owner ||
        c.actor.owner != candidate.producer_esi || !KickRoutine(c.actor)) return false;
    ResourceStamp resource{};
    if (s.Blocked() || !s.Resource(resource) || !s.Safe()) return false;
    return following.Match(c, context, Clock(s), resource, true, matched);
}
struct Result : line::Outcome {bool follow = false;};
template <class Sources> Result Run(Sources& s, const Contact& c, const input::Sample& context, Follows& following) {
    Result o;
    ResourceStamp resource{};
    o.reason = line::Reason::InputBlocked;
    if (s.Blocked()) {following = {}; return o;}
    // Resource validates the same Link model even during draw, without borrowing
    // another press or requiring matrices that were absent at the approved contact.
    s.Pose();
    o.reason = line::Reason::Pose;
    if (!s.Resource(resource)) {following = {}; return o;}
    o.reason = line::Reason::Reentry;
    if (!s.Safe()) {following = {}; return o;}
    // The same run can push Link along or pass beside him after the initial block.
    if (c.route == Route::Cart) {
        if (following.Match(c, context, Clock(s), resource)) {
            o.cancel = o.follow = true;
            o.reason = line::Reason::Blocked;
            return o;
        }
        if (!CartRunning(c.actor)) return o;
    }
    o.reason = line::Reason::Front;
    if (!s.Front(c.candidate, o.front)) {following.Retire(c.actor.owner); return o;}
    o.reason = line::Reason::Rear;
    if (!general::Parryable(o.front)) {following.Retire(c.actor.owner); return o;}
    if (c.route != Route::Cart && following.Match(c, context, Clock(s), resource)) {
        o.cancel = true;
        o.follow = true;
        o.reason = line::Reason::Blocked;
        return o;
    }
    static_cast<line::Outcome&>(o) = direct::Decide(s, c.candidate);
    // A Krauser kick's box block arms a slot for the whole kick, so its later sphere samples follow it (FollowKick).
    if (o.cancel) following.Arm(c, context, Clock(s), resource, c.route == Route::Krauser && KickRoutine(c.actor));
    return o;
}
struct Hook {volatile LONG active = 0; void* target = nullptr; void* original = nullptr; uint32_t skip = 0;};
inline Hook hooks[kCount];
inline bool requested[kCount] = {true, true, true, true, true};
inline uint32_t image_base = 0;
inline general::Runtime* runtime = nullptr;
inline __declspec(thread) bool busy = false, tainted = false, fresh_effects = false;
inline __declspec(thread) Follows follows;
using Decision = bool(__cdecl*)(const Contact&);
inline Decision decision = nullptr;
inline void(__cdecl* on_block)() = nullptr;
enum class PassReason : unsigned {Off, Structure, Cliff, Context, InputBlocked, Pose, Front, Rear, Window, Fault, Reentry, Count};
inline const char* Name(PassReason r) {
    constexpr const char* names[] = {"off", "structure", "cliff", "context", "input_blocked", "pose", "front", "rear", "window", "fault", "reentry"};
    return names[static_cast<unsigned>(r)];
}
inline uint64_t last_pass[kCount][static_cast<unsigned>(PassReason::Count)]{};
inline void Pass(Route r, PassReason reason) {
    const auto route = static_cast<unsigned>(r);
    const uint64_t now = GetTickCount64();
    auto& last = last_pass[route][static_cast<unsigned>(reason)];
    if (!last || now - last >= 1000) {
        last = now;
        general::Log("parry direct pass route=%s reason=%s", kSpecs[route].name, Name(reason));
    }
}
struct NativeSources : line::NativeSources {
    bool Front(const Candidate& c, general::Front& f) {return general::NativeSources::Front(c, f);}
    const Contact& contact;
    input::Sample context;
    ResourceStamp resource{};
    bool resource_known = false;
    NativeSources(general::Runtime& r, const Contact& c, const input::Sample& ctx) : line::NativeSources(r), contact(c), context(ctx) {}
    // Game time in 60 Hz frames (the runtime's game clock); without it the logic frame counter (= game time at 60 FPS).
    double Game() {return r.game_now ? r.game_now() : double(context.frame);}
    bool Resource(ResourceStamp& out) {
        general::Memory m;
        sword::reader::Observation found{};
        const sword::reader::Slots slots{r.access.image_base + static_cast<uint32_t>(r.access.code.player_slot_rva),
                                        r.access.image_base + static_cast<uint32_t>(r.access.code.global_slot_rva)};
        if (!sword::reader::ReadIdentified(m, slots, found, nullptr, true) ||
            found.player != context.player || found.global != context.global) return false;
        out = {found.info, found.resource, found.part0, found.part10, static_cast<uint32_t>(found.profile)};
        if (!m.Read(found.player + 0xF8, &out.player_guid, 4)) return false;
        if (!resource_known) {resource = out; resource_known = true;}
        return Same(resource, out);
    }
    bool Safe() {
        general::Memory m;
        Actor again{};
        input::Sample fresh{};
        ResourceStamp current{};
        return !tainted && input::Enabled() &&
            InterlockedCompareExchange(&hooks[static_cast<unsigned>(contact.route)].active, 0, 0) &&
            ReadActor(m, image_base, contact.route, contact.actor.owner, again) && Same(contact.actor, again) &&
            input::Read(m, r.access, fresh) && fresh.valid && fresh.gameplay &&
            input::Context(context, fresh) && context.frame == fresh.frame &&
            input::Context(context, input::state.sample) && Resource(current);
    }
};
inline void ShareTierTwo(general::Runtime& r, const Candidate& candidate, const general::Applied& applied) {
    if (!applied.cancel || !applied.effects || applied.grace || r.settings.mode != general::Mode::Active ||
        !r.settings.bosses || candidate.outer_caller - r.access.image_base != 0x15585F ||
        image_base != r.access.image_base || !InterlockedCompareExchange(&hooks[0].active, 0, 0)) return;
    __try {
        general::Memory m;
        Contact c{};
        c.route = Route::Krauser;
        input::Sample context{};
        if (!ReadActor(m, image_base, Route::Krauser, candidate.producer_esi, c.actor) ||
            !input::Read(m, r.access, context) || !context.valid || !context.gameplay) return;
        NativeSources s{r, c, context};
        ShareKick(s, candidate, applied, c, context, follows);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// Tier-2 entry of the box-first kick bridge (FollowKick): a sphere sample (ret 0x15585F) of a kick the Krauser box
// route already blocked is cancelled silently before tier 2 looks for a press. Game thread, inside ResolveParry.
// One log line per bridged kick (its first sphere sample).
inline bool FollowTierTwo(general::Runtime& r, const Candidate& candidate) {
    if (r.settings.mode != general::Mode::Active || !r.settings.bosses || candidate.outer_caller - r.access.image_base != 0x15585F ||
        image_base != r.access.image_base || !InterlockedCompareExchange(&hooks[0].active, 0, 0)) return false;
    bool follow = false, first = false;
    uint64_t age = 0;
    uint32_t owner = 0;
    __try {
        general::Memory m;
        Contact c{};
        c.route = Route::Krauser;
        input::Sample context{};
        if (!ReadActor(m, image_base, Route::Krauser, candidate.producer_esi, c.actor) ||
            !input::Read(m, r.access, context) || !context.valid || !context.gameplay) return false;
        NativeSources s{r, c, context};
        FollowSlot* slot = nullptr;
        follow = FollowKick(s, candidate, c, context, follows, &slot);
        if (follow && slot) {
            owner = c.actor.owner; age = slot->last - slot->start;
            first = !slot->bridged; slot->bridged = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {follow = false;}
    if (follow && first) general::Log("parry direct follow route=krauser_kick_probe via=sphere producer=0x15585F owner=0x%08X since_block_ms=%llu",
                                      owner, age);
    return follow;
}
inline bool __cdecl Resolve(const Contact& c) {
    if (!runtime || runtime->settings.mode != general::Mode::Active ||
        ((c.route == Route::Krauser || c.route == Route::Tree) && !runtime->settings.bosses)) {
        Pass(c.route, PassReason::Off);
        return false;
    }
    general::Memory m;
    input::Sample context{};
    if (!input::Read(m, runtime->access, context) || !context.valid || !context.gameplay) {
        follows = {};
        Pass(c.route, PassReason::Context);
        return false;
    }
    if (c.route == Route::Krauser && c.actor.routine[1] == 0x2B) {
        float player[3]{};
        if (!m.Read(context.player + 0x94, player, sizeof(player)) || Cliff(m, image_base, player) != CliffResult::Clear) {
            follows.Retire(c.actor.owner);
            Pass(c.route, PassReason::Cliff);
            return false;
        }
    }
    NativeSources s{*runtime, c, context};
    const auto o = direct::Run(s, c, context, follows);
    if (!o.cancel) {
        PassReason why = PassReason::Window;
        if (tainted) why = PassReason::Reentry;
        else if (o.reason == line::Reason::InputBlocked) why = PassReason::InputBlocked;
        else if (o.reason == line::Reason::Pose) why = PassReason::Pose;
        else if (o.reason == line::Reason::Front) why = PassReason::Front;
        else if (o.reason == line::Reason::Rear) why = PassReason::Rear;
        Pass(c.route, why);
        return false;
    }
    if (o.follow) return true;
    fresh_effects = true;
    general::Log("parry direct block route=%s owner=0x%08X press_age_ms=%llu pose_fallback=%u caller=0x%X",
                 kSpecs[static_cast<unsigned>(c.route)].name, c.actor.owner, o.press.age_ms, o.pose_fallback ? 1U : 0U,
                 c.caller ? c.caller - image_base : 0);
    return true;
}
__declspec(noinline) inline void __cdecl Record(MachineFrame* f, unsigned index) {
    const DWORD error = GetLastError();
    // Each shim reserves its continuation above PUSHFD/PUSHAD; RET consumes only that slot.
    auto* next = reinterpret_cast<uint32_t*>(reinterpret_cast<unsigned char*>(f) + sizeof(MachineFrame));
    *next = reinterpret_cast<uint32_t>(hooks[index].original);
    if (busy) {
        tainted = true;
        SetLastError(error);
        return;
    }
    busy = true;
    tainted = false;
    fresh_effects = false;
    __try {
        if (InterlockedCompareExchange(&hooks[index].active, 0, 0) && decision) {
            general::Memory m;
            Contact c{};
            if (!ReadContact(m, image_base, static_cast<Route>(index), *f, c)) Pass(static_cast<Route>(index), PassReason::Structure);
            else if (decision(c) && !tainted && InterlockedCompareExchange(&hooks[index].active, 0, 0)) {
                *next = hooks[index].skip;
                // Cosmetics cannot undo a committed cancellation, including native exceptions/reentry.
                __try {if (fresh_effects && on_block) on_block();} __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {Pass(static_cast<Route>(index), PassReason::Fault);}
    busy = false;
    tainted = false;
    SetLastError(error);
}
#define DIRECT_SHIM(name, index) \
__declspec(naked) inline void name() { \
    __asm push 0 \
    __asm pushfd \
    __asm pushad \
    __asm cld \
    __asm mov ebx, esp \
    __asm sub esp, 528 \
    __asm and esp, 0fffffff0h \
    __asm fxsave [esp] \
    __asm fninit \
    __asm mov dword ptr [esp+512], 1f80h \
    __asm ldmxcsr [esp+512] \
    __asm push index \
    __asm push ebx \
    __asm call Record \
    __asm add esp, 8 \
    __asm fxrstor [esp] \
    __asm mov esp, ebx \
    __asm popad \
    __asm popfd \
    __asm ret \
}
DIRECT_SHIM(KrauserDetour, 0)
DIRECT_SHIM(BatsDetour, 2)
DIRECT_SHIM(CartDetour, 3)
DIRECT_SHIM(TreeDetour, 4)
#undef DIRECT_SHIM
inline void* Detour(Route r) {
    switch (r) {
    case Route::Krauser: return reinterpret_cast<void*>(&KrauserDetour);
    case Route::Bats: return reinterpret_cast<void*>(&BatsDetour);
    case Route::Cart: return reinterpret_cast<void*>(&CartDetour);
    case Route::Tree: return reinterpret_cast<void*>(&TreeDetour);
    default: return nullptr;
    }
}
inline MH_STATUS Start(general::Runtime& r, Route route, Decision callback = Resolve, const input::Operations& ops = {}) {
    const unsigned index = static_cast<unsigned>(route);
    if (index >= kCount) return MH_ERROR_NOT_EXECUTABLE;
    auto& h = hooks[index];
    if (h.target) return MH_ERROR_ALREADY_CREATED;
    general::Memory m;
    if (!requested[index] || !callback || !Detour(route) || r.settings.mode != general::Mode::Active ||
        ((route == Route::Krauser || route == Route::Tree) && !r.settings.bosses) ||
        !r.front.verified || r.front.image_base != r.access.image_base || !Qualify(m, r.access.image_base, route))
        return MH_ERROR_NOT_EXECUTABLE;
    auto status = ops.initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return status;
    image_base = r.access.image_base;
    runtime = &r;
    decision = callback;
    h.skip = image_base + kSpecs[index].skip;
    h.target = reinterpret_cast<void*>(image_base + kSpecs[index].site);
    status = ops.create(h.target, Detour(route), &h.original);
    if (status != MH_OK) {h.target = nullptr; return status;}
    InterlockedExchange(&h.active, 1);
    status = ops.enable(h.target);
    if (status != MH_OK) InterlockedExchange(&h.active, 0);
    return status;
}
inline void Stop() {
    for (auto& hook : hooks) InterlockedExchange(&hook.active, 0);
}
} // namespace link_runtime::parry::direct

#pragma once
// Tier-2 parry native sources and effects (docs/parry-two-tier-2026-10-12.md §2, T5). Game thread
// only, inside the pre-effects resolver. The decision is general::Run (pure gate order); this file
// supplies the native reads and, after a true decision, the cosmetic effects plus the thrown-weapon
// deflect. Effects never change the decision. Counters are atomics; the log is a copied ring the
// worker drains (no game pointers cross threads).
#include "parry_general_block.h"
#include "parry_input_runtime.h"
#include "parry_sword_epoch.h"
#include "parry_spark_runtime.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace link_runtime::parry::general {
struct Memory {
    bool Read(uint32_t at, void* out, size_t n) {
        if (!shield::Range(at, n)) return false;
        __try {std::memcpy(out, reinterpret_cast<const void*>(at), n); return true;} __except (EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool Write(uint32_t at, const void* in, size_t n) {
        if (!shield::Range(at, n)) return false;
        __try {std::memcpy(reinterpret_cast<void*>(at), in, n); return true;} __except (EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
};
struct Counters {volatile LONG candidates = 0, blocked = 0, follows = 0, handoffs = 0, passes = 0, would_block = 0, deflects = 0, deflect_failed = 0, faults = 0,
                 grace_open = 0, grace_blocked = 0, follow_long = 0;};
struct Runtime {
    Settings settings;          // set before the resolver is installed; constant afterwards
    Code code;
    front_native::Code front;
    shield::NativeAccess access;
    spark::Code sparks;
    bool deflect = false;       // thrown deflect route pinned
    Follow follow;              // game thread
    Grace grace;                // game thread; window set from ParryGraceMs before the resolver is installed
    double (*game_now)() = nullptr;   // game time in 60 Hz frames (game_clock.h); null = no grace window
    Counters counters;
    uint64_t last_pass[32]{};   // game thread: 1/s per reason (and per producer verdict)
};
struct NativeSources {
    Runtime& r;
    sword::reader::Observation pose{};
    bool pose_ok = false;
    bool Invocation(const Candidate& c) {return CurrentInvocation(c).wrapper != nullptr;}
    uint32_t ImageBase() {return r.access.image_base;}
    bool Live(size_t i) {return i < kProducerCount && r.code.live[i];}
    bool Descriptor(uint32_t attack, general::Descriptor& d) {Memory m; return m.Read(attack, &d, sizeof(d));}
    bool Blocked() {return input::Blocks();}
    bool Pose() {sword::epoch::Stamp stamp{}; pose_ok = sword::epoch::AtContact(pose, stamp); return pose_ok;}
    bool Guard(bool refresh_input) {
        // Observe never refreshes or consumes real input. Active still consumes through the
        // ordinary fresh-read gate after every geometric/producer gate has passed.
        if ((refresh_input && !input::RefreshAtContact()) || !input::PeekCurrent()) return false;
        const auto before = input::state.sample;
        sword::reader::ProcessMemory m; sword::reader::Guard guard;
        const sword::reader::Slots slots{r.access.image_base + static_cast<uint32_t>(r.access.code.player_slot_rva),
                                         r.access.image_base + static_cast<uint32_t>(r.access.code.global_slot_rva)};
        if (!sword::reader::ReadGuard(m, slots, guard) || guard.player != before.player || guard.global != before.global) return false;
        const auto& after = input::state.sample;
        return input::Context(before, after) && before.frame == after.frame && before.on == after.on && before.trg == after.trg && input::PeekCurrent();
    }
    bool Front(const Candidate& c, general::Front& f) {
        Memory m; uint32_t player = 0; float pa[6]{};
        owner::Observation o;
        if (!m.Read(c.current, o.current.data(), 12) || !m.Read(c.old, o.previous.data(), 12) ||
            !m.Read(r.access.image_base + static_cast<uint32_t>(r.access.code.player_slot_rva), &player, 4) ||
            !shield::Range(player, 0xAC) || !m.Read(player + 0x94, pa, sizeof(pa))) return false;
        front_math::Result res; float forward[3]{};
        if (!front_native::Evaluate(r.front, r.access, o, pa, pa + 3, res, forward)) return false;
        std::memcpy(f.sample, o.current.data(), 12); std::memcpy(f.player, pa, 12);
        f.frontal = res.frontal; f.dot = res.dot;
        // Along Link's horizontal forward, unit length (Parryable reads half / distance as the cosine).
        const float flat = std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
        if (!(flat > 1e-3f)) return false;
        f.half = (forward[0] * (f.sample[0] - pa[0]) + forward[2] * (f.sample[2] - pa[2])) / flat;
        return std::isfinite(f.half);
    }
    uint32_t Owner(const Candidate& c) {
        const uint32_t ret = c.outer_caller - r.access.image_base;
        if (ret == 0x55CE2) return c.producer_owner;   // em10AtkCk: EDI at the wrapper entry
        Memory m; uint32_t object = 0;
        if (ChainsawProducer(ret)) return ChainsawOwner(m, ret, c.producer_esi);   // em10CsawHitCk: ESI
        return thrown::Identify(m, r.access.image_base, c, ret, object) ? object : 0;
    }
    // The owner's cEm routine r0/r1 ([em+0xFC], [em+0xFD]; the r1 dispatch byte, gigante_tree_guard.h): same
    // values as at the block = still that attack (chain/flail follow-up, parry_general_block.h Follow).
    bool Routine(uint32_t owner, uint8_t (&out)[2]) {Memory m; return owner && m.Read(owner + 0xFC, out, 2);}
    uint64_t Now() {return GetTickCount64();}
    bool Refresh() {return input::RefreshAtContact();}
    bool Consume() {return input::ConsumeCurrent(true);}
    bool Peek() {return input::PeekCurrent();}
};
// Copied log lines (game thread -> worker).
struct Line {char text[320];};
inline Line lines[64];
inline std::atomic<unsigned> write_index{0}, read_index{0}, dropped{0};
inline void Log(const char* format, ...) {
    const unsigned w = write_index.load(std::memory_order_relaxed), rd = read_index.load(std::memory_order_acquire);
    if (w - rd >= 64) {dropped.fetch_add(1); return;}
    va_list args; va_start(args, format);
    vsnprintf_s(lines[w % 64].text, sizeof(lines[0].text), _TRUNCATE, format, args);
    va_end(args);
    write_index.store(w + 1, std::memory_order_release);
}
template <class Sink> void Drain(Sink&& sink) {
    unsigned rd = read_index.load(std::memory_order_relaxed); const unsigned w = write_index.load(std::memory_order_acquire);
    while (rd != w) {sink(lines[rd % 64].text); read_index.store(++rd, std::memory_order_release);}
}
// What the runtime did after a true decision, for the caller's own effects (voice, reaction).
// grace: blocked by the post-parry grace window, part of the parry that opened it (sparks only).
struct Applied {bool cancel = false, effects = false, grace = false; uint32_t serial = 0, owner_id = 0;
    uint16_t power = 0; float hit[3]{}, player[3]{};    // power and positions of a block (parry_pushback.h)
    uint32_t owner = 0; bool chainsaw = false;};         // Owner() of the block and Chainsaw() (sword_lock_probe.h, H0)
inline bool GraceOn(const Runtime& r) {return r.game_now && r.grace.window > 0 && r.settings.mode == Mode::Active;}
// A committed tier-1 parry opens the window (never renews an open one).
inline void OpenGraceAfterCommit(Runtime& r, uint32_t serial) {
    if (!GraceOn(r)) return;
    const double now = r.game_now();
    if (r.grace.Expire(now)) Log("parry grace expired serial=0x%08X blocked=%u", r.grace.serial, r.grace.blocked);
    if (r.grace.Open(now, serial)) {
        InterlockedIncrement(&r.counters.grace_open);
        Log("parry grace open by=tier1 serial=0x%08X window=%.1f frames until=%.1f", serial, r.grace.window, r.grace.until);
    }
}
inline Applied Resolve(Runtime& r, const Candidate& c, bool handoff) {
    Applied a;
    InterlockedIncrement(&r.counters.candidates);
    NativeSources s{r};
    Outcome o;
    const bool grace_on = GraceOn(r);
    const double game_now = grace_on ? r.game_now() : 0;
    const uint32_t grace_serial = r.grace.serial; const unsigned grace_blocked = r.grace.blocked;
    // A window-0 Grace never opens (Open refuses window 0) but still holds parked follow-ups (Grace::Park).
    Grace* const grace = (grace_on || (r.settings.mode == Mode::Active && r.grace.window <= 0)) ? &r.grace : nullptr;
    a.cancel = Run(s, c, r.settings, r.follow, handoff, o, grace, game_now);
    const uint64_t now = GetTickCount64();
    if (o.grace_expired) Log("parry grace expired serial=0x%08X blocked=%u", grace_serial, grace_blocked);
    if (o.reason == Reason::Fault || o.reason == Reason::Reentry) InterlockedIncrement(&r.counters.faults);
    uint32_t owner_id = 0;
    if (o.ret_rva == 0x55CE2 && o.owner) {
        Memory m; unsigned char id[2]{};
        if (m.Read(o.owner + 0x100, id, 2)) owner_id = id[0] | uint32_t(id[1]) << 8;
    }
    if (o.follow) {
        if (a.cancel) InterlockedIncrement(&r.counters.follows);
        if (a.cancel && o.long_follow) InterlockedIncrement(&r.counters.follow_long);
        // A chain/flail coming back after a pause (gap past the short bound): one line per return, so a live check sees
        // the return itself and its owner, not only the follow_long counter.
        if (a.cancel && o.long_follow && o.follow_gap_ms > Follow::kGapMs)
            Log("parry general follow_long owner=0x%08X producer=0x%X owner_id=0x%X gap_ms=%llu since_block_ms=%llu",
                o.owner, o.ret_rva, owner_id, o.follow_gap_ms, o.follow_age_ms);
        a.grace = o.grace;
        return a;   // same swing: silent, no effects; a log line only for a long return (above)
    }
    if (r.settings.mode == Mode::Observe) {
        if (o.would_block) {
            InterlockedIncrement(&r.counters.would_block);
            Log("parry general observe would_block=1 producer=0x%X class=%c owner_id=0x%X power=%u radius=%.0f front_dot=%.3f half=%.0f handoff=%u",
                o.ret_rva, o.cls, owner_id, o.descriptor.power, o.descriptor.radius, o.front.dot, o.front.half, handoff ? 1U : 0U);
            return a;
        }
    }
    if (!a.cancel) {
        InterlockedIncrement(&r.counters.passes);
        const unsigned key = o.reason == Reason::Producer ? 16U + static_cast<unsigned>(o.verdict) : static_cast<unsigned>(o.reason);
        if (o.reason != Reason::Off && key < 32 && now - r.last_pass[key] >= 1000) {
            r.last_pass[key] = now;
            const defense::PressAge press = defense::PressAt(input::state.window, now);
            Log("parry general pass reason=%s producer=0x%X class=%c owner_id=0x%X power=%u flag=0x%X front_dot=%.3f half=%.0f "
                "press_down=%u press_age_ms=%llu owner=0x%08X",
                o.reason == Reason::Producer ? VerdictName(o.verdict) : ReasonName(o.reason), o.ret_rva, o.cls, owner_id,
                o.descriptor.power, o.descriptor.flag, o.front.dot, o.front.half, press.down, press.age_ms, o.owner);
        }
        return a;
    }
    // Blocked. Effects are cosmetic (except the thrown deflect) and never change the decision.
    const LONG n = InterlockedIncrement(&r.counters.blocked);
    if (handoff) InterlockedIncrement(&r.counters.handoffs);
    a.effects = !o.grace; a.grace = o.grace; a.owner_id = owner_id;
    a.power = o.descriptor.power; std::memcpy(a.hit, o.front.sample, sizeof(a.hit)); std::memcpy(a.player, o.front.player, sizeof(a.player));
    a.owner = o.owner; a.chainsaw = Chainsaw(o.function, o.descriptor);
    a.serial = o.grace ? r.grace.serial : 0x40000000U | static_cast<uint32_t>(n);
    if (o.grace) InterlockedIncrement(&r.counters.grace_blocked);
    if (o.grace_opened) {
        r.grace.serial = a.serial; InterlockedIncrement(&r.counters.grace_open);
        Log("parry grace open by=tier2 serial=0x%08X window=%.1f frames until=%.1f", a.serial, r.grace.window, r.grace.until);
    }
    float point[3]; std::memcpy(point, o.front.sample, sizeof(point));
    bool on_blade = false;
    if (s.pose_ok) {
        const double base[3] = {s.pose.blade.base.x, s.pose.blade.base.y, s.pose.blade.base.z};
        const double tip[3] = {s.pose.blade.tip.x, s.pose.blade.tip.y, s.pose.blade.tip.z};
        on_blade = spark::BladePoint(base, tip, o.front.sample, o.descriptor.radius, point);
    }
    const unsigned sparks = spark::EmitAt(r.sparks, c, point, o.front.sample);
    int deflected = -1;   // -1 not thrown, 0 not deflected, 1 record written
    if (o.cls == 'T') {
        deflected = 0;
        Memory m; uint32_t object = 0;
        const float* from = on_blade ? point : o.front.player;
        if (r.deflect && thrown::Identify(m, r.access.image_base, c, o.ret_rva, object) && thrown::Deflect(m, object, from)) deflected = 1;
        InterlockedIncrement(deflected ? &r.counters.deflects : &r.counters.deflect_failed);
    }
    if (o.grace)
        Log("parry grace blocked serial=0x%08X n=%u at=%.1f until=%.1f producer=0x%X class=%c owner_id=0x%X power=%u front_dot=%.3f "
            "half=%.0f sparks=%u deflect=%d owner=0x%08X", a.serial, r.grace.blocked, game_now, r.grace.until, o.ret_rva, o.cls, owner_id,
            o.descriptor.power, o.front.dot, o.front.half, sparks, deflected, o.owner);
    else
    Log("parry general block serial=0x%08X producer=0x%X class=%c owner_id=0x%X power=%u radius=%.0f front_dot=%.3f half=%.0f "
        "follow=0 handoff=%u pose_fallback=%u spark_on_blade=%u sparks=%u deflect=%d deflect_route=%u press_age_ms=%llu owner=0x%08X",
        a.serial, o.ret_rva, o.cls, owner_id, o.descriptor.power, o.descriptor.radius, o.front.dot, o.front.half,
        handoff ? 1U : 0U, o.pose_fallback ? 1U : 0U, on_blade ? 1U : 0U, sparks, deflected, r.deflect ? 1U : 0U, defense::PressAt(input::state.window, now).age_ms, o.owner);
    return a;
}
}  // namespace link_runtime::parry::general

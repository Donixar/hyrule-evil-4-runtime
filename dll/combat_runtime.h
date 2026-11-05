#pragma once
// Combat runtime switches (12-oct-2026): the real Mortal Draw and the parry
// reaction/riposte. Pure: INI parsing and the hook plan only. No process access.
//
// Every key lives in [LinkRuntime], defaults to 0 and only the exact value "1"
// enables it. With every key 0 the plan installs nothing new and the damage
// hook decision is exactly the legacy one (TraceKnife), so the
// DLL behaves as before (tests/native/test_combat_runtime.cpp, isolation).
//
// Staged keys, one at a time for Donald's tests (docs/combat-runtime-2026-10-12.md):
//   HitPartObserve        log the hit list of the Ganado the selection picks (no action)
//   MortalDrawObserve     log target / prompt / press decisions at the arbiter (no action)
//   MortalDrawContextual  real trigger: action button from the sheath, animation + voice, no damage
//   MortalDrawDamage      native strike through the shared adapter (needs Contextual)
//   ParryReaction         Link's secondary-layer recoil after a Committed parry (needs ParryActive)
//   ParryRiposte          small native hit on the parried Ganado (needs ParryActive), ParryRiposteMilli
// Tuning (only read when a Mortal Draw key is on):
//   MortalDrawLineWaiver  1 = no obstacle test (no native raycast is attributed; without it nothing qualifies)
//   MortalDrawMaxRange    horizontal reach 300..1200 (default 1200)
//   MortalDrawStrikeTick  player frames from the start to the cut, 1..33 (default 16)
//   MortalDrawStrikeRange horizontal reach accepted at the cut, 300..4000 (default 2000; never
//                         below MaxRange). The clip lunges Link ~1.2-1.5 m (log 12-oct, build 05).
//   MortalDrawFaceTarget  1 (default) turns Link to face the target at the start (native yaw +0xA4)
//                         so the clip's lunge carries him to it; 0 leaves his yaw alone
// Ending Blow leap (only read with EndingBlow=1; ending_blow_leap.h, docs/ending-blow-leap-2026-10-16.md).
// An out-of-range value keeps its default and is counted in leap_problems (logged):
//   EndingBlowLeap           0 (default) in-place jump as before, 1 leap, 2 observe (plan logged, no write)
//   EndingBlowLeapRange      reach with a latched Z-lock, 1200..9000 (default 8500 since 17-oct, Donald asked
//                            for 8.5 m; was 7000, before that 3000 and 4500: the native kick lands a Ganado
//                            3.6-4.6 m away). Maximum 9000 = the Z-lock's hold (ztarget.h kHold)
//   EndingBlowLeapFreeRange  reach without one, 1200..LeapRange (default 8500 since 17-oct; was 4000, then 2000),
//                            45-degree cone
//   EndingBlowLeapStandoff   landing distance from the target, 500..StrikeRange-100 (default 900)
//   EndingBlowStrikeRange    reach accepted at the strike, 600..1500 (default 1200, as before)
//   EndingBlowLeapPreferLock 1 (default): the latched Z-lock wins over a nearer target
//   EndingBlowLeapBurst      percent 100..300 (default 200, 17-oct): the leap holds through the clip's grounded
//                            crouch, then starts at burst/100 x the mean rate and eases out; 100 = the linear
//                            leap of 16-oct bit for bit (ending_blow_leap.h Curve)
// Ending Blow camera lift (only read with EndingBlow=1; ending_blow_camera.h). Its own counter, lift_problems:
//   EndingBlowCameraLift     percent of the clip's 2 m arc the camera rises with, 0..120 (default 85, 17-oct;
//                            0 = off, no camera hook for it)
//   EndingBlowCameraLiftObserve 1 = compute and log the lift, never write the camera (default 0)
// Great Spin (only read with SpinAttack=1; great_spin.h) and Shield Bash (only read with SpinAttack=1 or
// EndingBlow=1; shield_bash.h), 18-oct. Their own counter, gs_sb_problems; a bad value keeps its default:
//   GreatSpin           missing or 1 = on (default on), 0 = off
//   SpinBreakables      missing or 1 = the spin and the Great Spin break boxes, wooden barrels, vases, cabinets and
//                       boards in their radius (never the red explosive barrel), 0 = off (sword_breakables.h)
//   GreatSpinRadius     mm, 2200..6000 (default 3500)
//   GreatSpinPermille   of the native knife amount, 1000..3000 (default 2000)
//   GreatSpinSparkles   spark bursts on the ring at the strike, 0..16 (default 8)
//   ShieldBash          missing or 1 = on (default on), 0 = off; needs ZTarget=1 at run time
//   ShieldBashReach     mm, 1000..3000 (default 2200)
//   ShieldBashTargets   Ganados per bash, 1..4 (default 3)
//   ShieldBashDeflectMs game time the weapon deflect stays open, 0..1000 (default 500; 0 = no deflect)
//   ShieldBashDeflectReach mm, 1000..5000 (default 3000)
//   ShieldBashMotion    missing or 1 = TP's shield attack clip (spin-fix-01), 0 = the bash fires on the tap as before
// Back Slice (18-oct, back_slice.h; only read with SpinAttack=1, the clip it plays), counted in gs_sb_problems too:
//   BackSlice           missing or 1 = on (default on), 0 = off; needs ZTarget=1 at run time
//   BackSlicePermille   of the native knife amount, 1000..3000 (default 1500)
//   BackSliceArcFrames  game frames of the ~180 degree arc, 10..40 (default 20)
//   BackSliceMaxRadius  mm, 1500..3000 (default 3000; the minimum is 1000)
//   BackSliceMotion     missing or 1 = TP's Back Slice clip (backslice-tp-01: roll round, twirl jump slash, landing;
//                       the arc lasts the roll and BackSliceArcFrames is not used), 0 = the spin clip as before
//   BackSliceCamera     missing or 1 = the camera follows the Back Slice (backslice-cam-01, back_slice_camera.h): no
//                       native falling camera during the roll, a smooth orbit round Link at the lock camera's offset
//                       and a vertical follow for the roll and the twirl jump; 0 = the native camera as before (no
//                       hook for it)
//   BackSliceCameraMs   the orbit spring, ms, 30..200 (default 40)
//   BackSliceCameraLift vertical follow percent for the roll and the twirl jump, 0..150 (default 100; 0 = orbit only)
// Backflip (backflip-01, 30-oct, backflip.h; read with SpinAttack=1, the dispatch it plays through), in gs_sb_problems:
//   Backflip            exact 1 = on (default off): Z held + S + knife tap = TP's backflip (else the tap is as before)
//   BackflipDistance    mm Link travels straight back during the flip, 1000..4000 (default 2500; read only with
//                       Backflip=1)
//   BackflipLandFrames  game frames (60 Hz) of the landing before control returns, 0..30 (default 2 = TP's land cancel
//                       point; 30 = the whole landing clip as backflip-01; read only with Backflip=1)
// Native prompts (offer-fix-01, 3-nov; sword_offer_policy.h; read with SpinAttack=1 or EndingBlow=1), in gs_sb_problems:
//   SwordOverNativeOffer missing or 1 = a queued action-button prompt ("E TAKE", "E KICK", OPEN, CLIMB...) no longer
//                       stops the spin / Great Spin charge, the Shield Bash, Back Slice and backflip tap, a queued
//                       request or a running clip; E (or aiming) still gives way to the prompt, and QTE / mash /
//                       forced prompts, the prompt lock and the arbiter's early-outs still win. 0 = 3.5.0: any queued
//                       offer or prompt up gives way (the Ending Blow keeps its own rule, 24-oct, either way)
// Sword in hand (sword-hand-01, 19-oct; sword_hand.h; read with SpinAttack=1 or EndingBlow=1), counted in gs_sb_problems:
//   SwordInHand         missing or 1 = during a sword action started outside the knife routine (Back Slice, Shield
//                       Bash, spin / Great Spin, Ending Blow and its leap) the right hand shows the Master Sword (the
//                       knife slot) and the equipped weapon is hidden, as when the knife is drawn natively; afterwards
//                       both come back exactly as they were. 0 = the hand keeps the equipped weapon (before 19-oct)
// Shield guard (shield-guard-01, 18-oct; shield_guard.h). Its own counter, guard_problems; a bad value keeps its default:
//   ShieldGuard         exact 1 = on (default off); needs ZTarget=1 and the parry transport at run time
//   ShieldGuardBlocks   blocked hits before the shield breaks, 1..99 (default 5)
//   ShieldRegenSeconds  game seconds until a broken shield comes back, 1..600 (default 45)
#include "great_spin.h"
#include "spin_bullets.h"
#include "shield_guard_config.h"
#include "shield_bash.h"
#include "back_slice.h"
#include "backflip.h"
#include <cstddef>
#include <cstdint>
#include <cwchar>

namespace link_runtime::combat {
struct Config {
    bool hitpart_observe = false, md_observe = false, md_contextual = false, md_damage = false;
    bool parry_reaction = false, parry_riposte = false;
    bool spin_attack = false, ending_blow = false;
    unsigned riposte_milli = 1000;
    bool line_waiver = false;
    unsigned max_range = 1200, strike_tick = 16;
    unsigned strike_range = 2000;
    bool face_target = true;
    unsigned ending_leap = 0, leap_range = 8500, leap_free_range = 8500, leap_standoff = 900, ending_strike_range = 1200;
    unsigned leap_burst = 200;
    unsigned camera_lift = 85;
    bool camera_lift_observe = false;
    unsigned lift_problems = 0;  // invalid Ending Blow camera lift values (logged; their defaults kept)
    bool leap_prefer_lock = true;
    great_spin::Config great;       // read with SpinAttack=1 only (great.on stays true but unused otherwise)
    shield_bash::Config bash;       // read with SpinAttack=1 or EndingBlow=1 only
    back_slice::Config back;        // read with SpinAttack=1 only (back.on stays true but unused otherwise)
    bool back_camera = true;        // BackSliceCamera (read with SpinAttack=1)
    unsigned back_camera_ms = 40, back_camera_lift = 100;   // BackSliceCameraMs, BackSliceCameraLift
    backflip::Config flip;          // Backflip / BackflipDistance (read with SpinAttack=1; flip.on default off)
    bool sword_hand = true;         // SwordInHand (read with SpinAttack=1 or EndingBlow=1)
    bool sword_over_offer = true;   // SwordOverNativeOffer (read with SpinAttack=1 or EndingBlow=1; sword_offer_policy.h)
    bool spin_breakables = true;    // SpinBreakables (read with SpinAttack=1): spins break boxes and boards (sword_breakables.h)
    spin_bullets::Config bullets;   // SpinBullets* (read with SpinAttack=1): spins fire free shots all around (spin_bullets.h)
    unsigned gs_sb_problems = 0;    // invalid Great Spin / Shield Bash values (logged; their defaults kept)
    shield_guard::Config guard;     // ShieldGuard / ShieldGuardBlocks / ShieldRegenSeconds
    unsigned guard_problems = 0;    // invalid shield guard values (logged; their defaults kept)
    bool GreatSpin() const { return spin_attack && great.on; }
    bool ShieldBash() const { return SwordActions() && bash.on; }
    bool BackSlice() const { return spin_attack && back.on; }
    bool Backflip() const { return spin_attack && flip.on; }
    unsigned leap_problems = 0;  // invalid Ending Blow leap values (logged; their defaults kept)
    unsigned problems = 0;  // invalid values or unmet dependencies (logged; the feature stays off)
    bool MortalTargeting() const { return hitpart_observe || md_observe || md_contextual; }
    bool MortalAction() const { return md_contextual; }
    bool SwordActions() const { return spin_attack || ending_blow; }
    bool Adapter() const { return md_damage || parry_riposte || SwordActions(); }
    bool AnyOn() const { return MortalTargeting() || md_damage || parry_reaction || parry_riposte || SwordActions() || guard.enabled; }
};
inline bool ExactOne(const wchar_t* v) { return v && v[0] == L'1' && v[1] == 0; }
inline bool Number(const wchar_t* v, unsigned& out) {
    if (!v || !*v) return false;
    unsigned n = 0;
    for (const wchar_t* p = v; *p; ++p) {
        if (*p < L'0' || *p > L'9' || n > 100000) return false;
        n = n * 10 + unsigned(*p - L'0');
    }
    out = n; return true;
}
// get(key, out, count) fills `out` with the raw INI value ("" when missing).
// parry_active: ParryActive=1 (the commit point both parry features hang from).
template <class Get> Config ReadConfig(Get&& get, bool parry_active) {
    Config c;
    wchar_t v[32]{};
    auto on = [&](const wchar_t* key) {
        v[0] = 0; get(key, v, sizeof(v) / sizeof(v[0]));
        if (ExactOne(v)) return true;
        if (v[0] && !(v[0] == L'0' && v[1] == 0)) ++c.problems;
        return false;
    };
    auto number = [&](const wchar_t* key, unsigned fallback, unsigned lo, unsigned hi) {
        v[0] = 0; get(key, v, sizeof(v) / sizeof(v[0]));
        unsigned n = 0;
        if (!v[0]) return fallback;
        if (!Number(v, n) || n < lo || n > hi) { ++c.problems; return fallback; }
        return n;
    };
    c.hitpart_observe = on(L"HitPartObserve");
    c.md_observe = on(L"MortalDrawObserve");
    c.md_contextual = on(L"MortalDrawContextual");
    c.md_damage = on(L"MortalDrawDamage");
    c.parry_reaction = on(L"ParryReaction");
    c.parry_riposte = on(L"ParryRiposte");
    c.spin_attack = on(L"SpinAttack");
    c.ending_blow = on(L"EndingBlow");
    if (c.MortalTargeting() || c.SwordActions()) {
        c.line_waiver = on(L"MortalDrawLineWaiver");
        c.max_range = number(L"MortalDrawMaxRange", 1200, 300, 1200);
        c.strike_tick = number(L"MortalDrawStrikeTick", 16, 1, 33);
        c.strike_range = number(L"MortalDrawStrikeRange", 2000, 300, 4000);
        if (c.strike_range < c.max_range) c.strike_range = c.max_range;
        c.face_target = number(L"MortalDrawFaceTarget", 1, 0, 1) == 1;
    }
    if (c.ending_blow) {
        const unsigned before = c.problems;
        c.ending_leap = number(L"EndingBlowLeap", 0, 0, 2);
        c.ending_strike_range = number(L"EndingBlowStrikeRange", 1200, 600, 1500);
        c.leap_standoff = number(L"EndingBlowLeapStandoff", 900, 500, c.ending_strike_range - 100);
        c.leap_range = number(L"EndingBlowLeapRange", 8500, 1200, 9000);
        c.leap_free_range = number(L"EndingBlowLeapFreeRange", 8500, 1200, c.leap_range);
        c.leap_prefer_lock = number(L"EndingBlowLeapPreferLock", 1, 0, 1) == 1;
        c.leap_burst = number(L"EndingBlowLeapBurst", 200, 100, 300);
        // A kept default can still sit beyond a bound another key set: clamp it to that bound.
        if (c.leap_standoff > c.ending_strike_range - 100) c.leap_standoff = c.ending_strike_range - 100;
        if (c.leap_free_range > c.leap_range) c.leap_free_range = c.leap_range;
        c.leap_problems = c.problems - before; c.problems = before;
        c.camera_lift = number(L"EndingBlowCameraLift", 85, 0, 120);
        c.camera_lift_observe = number(L"EndingBlowCameraLiftObserve", 0, 0, 1) == 1;
        c.lift_problems = c.problems - before; c.problems = before;
    }
    if (c.SwordActions()) {
        const unsigned before = c.problems;
        auto flag = [&](const wchar_t* key) {
            v[0] = 0; get(key, v, sizeof(v) / sizeof(v[0]));
            const auto f = shield_bash::ParseFlag(v);
            if (f == shield_bash::Flag::Bad) ++c.problems;
            return f != shield_bash::Flag::Off;
        };
        if (c.spin_attack) {
            c.great.on = flag(L"GreatSpin");
            c.spin_breakables = flag(L"SpinBreakables");
            c.bullets = spin_bullets::ReadConfig([&](const wchar_t* key, wchar_t* out, unsigned n) {out[0] = 0; get(key, out, n);});
            c.problems += c.bullets.problems;
            c.great.radius = number(L"GreatSpinRadius", 3500, 2200, 6000);
            c.great.permille = number(L"GreatSpinPermille", 2000, 1000, 3000);
            c.great.sparkles = number(L"GreatSpinSparkles", 8, 0, great_spin::kMaxSparkles);
            c.back.on = flag(L"BackSlice");
            c.back.permille = number(L"BackSlicePermille", 1500, 1000, 3000);
            c.back.arc_frames = number(L"BackSliceArcFrames", 20, 10, 40);
            c.back.max_radius = number(L"BackSliceMaxRadius", 3000, 1500, 3000);
            c.back.motion = flag(L"BackSliceMotion");
            c.back_camera = flag(L"BackSliceCamera");
            c.back_camera_ms = number(L"BackSliceCameraMs", 40, 30, 200);
            c.back_camera_lift = number(L"BackSliceCameraLift", 100, 0, 150);
            c.flip.on = on(L"Backflip");
            if (c.flip.on) {
                c.flip.distance = number(L"BackflipDistance", backflip::kDefaultDistance, backflip::kMinDistance, backflip::kMaxDistance);
                c.flip.land_frames = number(L"BackflipLandFrames", backflip::kLandCancelFrames, 0, backflip::kMaxLandFrames);
            }
        }
        c.bash.on = flag(L"ShieldBash");
        c.bash.motion = flag(L"ShieldBashMotion");
        c.bash.reach = number(L"ShieldBashReach", 2200, 1000, 3000);
        c.bash.targets = number(L"ShieldBashTargets", 3, 1, shield_bash::kMaxTargets);
        c.bash.deflect_ms = number(L"ShieldBashDeflectMs", 500, 0, 1000);
        c.bash.deflect_reach = number(L"ShieldBashDeflectReach", 3000, 1000, 5000);
        c.sword_hand = flag(L"SwordInHand");
        c.sword_over_offer = flag(L"SwordOverNativeOffer");
        c.gs_sb_problems = c.problems - before; c.problems = before;
    }
    {
        const unsigned before = c.problems;
        c.guard.enabled = on(L"ShieldGuard");
        if (c.guard.enabled) {
            c.guard.blocks = number(L"ShieldGuardBlocks", 5, shield_guard::kMinBlocks, shield_guard::kMaxBlocks);
            c.guard.regen_seconds = number(L"ShieldRegenSeconds", 45, shield_guard::kMinRegen, shield_guard::kMaxRegen);
        }
        c.guard_problems = c.problems - before; c.problems = before;
    }
    if (c.parry_riposte) {
        // Out-of-range milli disables the riposte (riposte::MakeConfig).
        unsigned milli = 0;
        v[0] = 0; get(L"ParryRiposteMilli", v, sizeof(v) / sizeof(v[0]));
        if (!v[0]) milli = 1000;
        else if (!Number(v, milli)) milli = 0;
        c.riposte_milli = milli;
        if (milli < 100 || milli > 3000) { ++c.problems; c.parry_riposte = false; }
    }
    // Dependencies: the damage needs the contextual action; the parry features need the commit point.
    if (c.md_damage && !c.md_contextual) { ++c.problems; c.md_damage = false; }
    if ((c.parry_reaction || c.parry_riposte) && !parry_active) {
        ++c.problems; c.parry_reaction = c.parry_riposte = false;
    }
    return c;
}
// Which hooks the combat runtime adds. legacy_damage = TraceKnife.
struct Plan {
    bool arbiter = false;        // 0x18AB40 entry/exit: Mortal Draw sample + target selection
    bool dispatch = false;       // player handler dispatch (Mortal Draw start)
    bool motion_move = false;    // 0x262B90: reaction weight + per-frame pump (adapter Tick, riposte)
    bool neck = false;           // 0x27E6E0: head-look gate while the reaction owns 2A8
    bool damage_hook = false;    // GetWepDmVal detour (legacy policy and/or the shared adapter)
    bool adapter = false;        // bind and Resolve the shared damage::Adapter in that detour
    bool mortal_assets = false;  // Mortal Draw clip + voice
    bool reaction_assets = false;
};
inline Plan MakePlan(const Config& c, bool legacy_damage, bool ocarina) {
    Plan p;
    p.arbiter = c.MortalTargeting() || c.SwordActions();
    p.dispatch = c.MortalAction() || ocarina || c.SwordActions();
    p.motion_move = c.parry_reaction || c.Adapter() || c.guard.enabled;
    p.neck = c.parry_reaction || c.guard.enabled;
    p.adapter = c.Adapter();
    p.damage_hook = legacy_damage || p.adapter;
    p.mortal_assets = c.MortalAction();
    p.reaction_assets = c.parry_reaction;
    return p;
}
// True when the combat runtime adds nothing to the legacy DLL.
inline bool Inert(const Plan& p, bool legacy_damage, bool ocarina) {
    return !p.arbiter && !p.motion_move && !p.neck && !p.adapter && !p.reaction_assets &&
           p.damage_hook == legacy_damage && p.dispatch == ocarina && !p.mortal_assets;
}
}  // namespace link_runtime::combat

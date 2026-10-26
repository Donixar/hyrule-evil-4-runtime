#pragma once
// Ending Blow height limits (17-oct, Donald: the leap did nothing with Link above the downed Ganado; logs: locked
// press d=2846 dy=-267 refused by the old symmetric 250, free presses dy -406..-569 beyond 4 m). One table that the
// selection (ending_blow_target.h Reach / Diagnose) and the leap plan (ending_blow_leap.h Plan) both read, so the
// press and the plan cannot drift apart (a press accepted by one and refused by the other jumps in place and misses).
// dy = target root Y - Link Y (Y up): negative = the Ganado lies BELOW Link.
//   leap off (EndingBlowLeap=0): 400 both ways everywhere, as before.
//   leap on, beyond the strike reach: down 6000, up 2500 (24-oct, bug 5: Donald wants height not to matter, "aumentarlo
//     harto más"; was 900 / 600). Logged refusals 24-oct: six presses at dy=-4010 (Link 4 m above the Ganados, d 1269..2811)
//     and one at dy=-1576 (d 4308), all reason=vertical. Down beyond the native floor snap (cSatMgr::check ->
//     scrAtCheckSphere, UHD 0x594C69..0x594D07: one ray from y+600 down, Y snapped only when |floor - y| < 1000) is
//     safe because the leap now lowers Link's Y itself on a planned descent (ending_blow_leap.h Descend): over a floor
//     the snap puts him back on it the same frame, over a drop he comes down toward the target's level instead of
//     hovering. Up has no such help: Link climbs only what the snap carries (slopes, stairs, steps under 600); a wall
//     stops the leap (blocked) and the strike recheck below misses honestly.
//   leap on, within the strike reach and at the strike (Strike mode; lethal on rise inherits it): down 1000 (the snap's
//     reach), up 900. AtStrike widens it for a long standoff: a walkable slope (45 degrees) leaves at most the standoff
//     of height between the landing and the target (default standoff 900: 1000 / 900; Standoff 1400: 1500 / 1400).
namespace link_runtime::ending_blow::height {
struct Band {
    float down = 400.f, up = 400.f;
    bool Holds(float dy) const { return dy >= -down && dy <= up; }   // NaN fails
};
constexpr float kInPlace = 400.f;              // both ways: leap off, the spin and in-place selection
constexpr Band kFar{6000.f, 2500.f};           // leap on, beyond the strike reach (selection, pre-start, plan)
constexpr Band kStrike{1000.f, 900.f};         // leap on, at the strike recheck and the in-place selection
inline Band AtStrike(float standoff, float strike) {  // never below kStrike; NaN or a bad pair keeps kStrike
    const float s = strike > 0.f && standoff > 0.f && standoff <= strike ? standoff : 0.f;
    return {s + 100.f > kStrike.down ? s + 100.f : kStrike.down, s > kStrike.up ? s : kStrike.up};
}
}  // namespace link_runtime::ending_blow::height

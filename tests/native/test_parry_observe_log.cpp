// Pending 13 (3-nov): rate limit of the repeated `parry observe` lines (dll/parry_observe_log.h). The live log of
// 2-nov had a kind=0 line every ~360 ms with no input (pose_poisoned=1 pose_failure=5 pose_reader_failure=6): with the
// game idle the gate must write less than one line per minute. Live session 2 of 3-nov had kind=3 (AttackSample) bursts
// of up to 281 identical lines/s with the knife/sword input active: an unchanged state of kinds 0, 2 and 3 is written
// at most once per 5 s and the skipped ones are counted per kind. Every candidate (kind 1), every record whose contact
// touched the blade and every state change must still be written. Synthetic data only: no process, no hooks.
#include "../../dll/parry_observe_log.h"
#include <cstdio>
#include <cstdlib>

namespace ol = link_runtime::parry::observe_log;
static unsigned checks;
static void Check(bool ok, const char* why) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); } }

// The 2-nov idle line: kind=0 reasons=0x1418 flags=0x2 pose_ready=0 pose_poisoned=1 room=0x307 weapon=22
// character=256 pose_failure=5 pose_reader_failure=6 input_on=0x0 input_trg=0x0.
static ol::Facts Idle() {
    ol::Facts f; f.kind = 0; f.reasons = 0x1418; f.flags = 0x2; f.ready = 0; f.poisoned = 1; f.failure = 5; f.reader_failure = 6;
    f.room = 0x307; f.weapon = 22; f.character = 256; return f;
}
// The 3-nov session 2 burst line: kind=3 reasons=0x11418 flags=0x112 pose_ready=0 pose_poisoned=1 room=0x100 weapon=22
// character=256 pose_failure=5 pose_reader_failure=6 input_on=0x0 input_trg=0x0 (contact valid=0).
static ol::Facts Sample() {
    ol::Facts f = Idle(); f.kind = 3; f.reasons = 0x11418; f.flags = 0x112; f.room = 0x100; return f;
}

static void IdleMinute() {
    ol::Gate g; unsigned written = 0, total = 0;
    for (uint64_t t = 1000; t < 1000 + 10 * 60000; t += 360) { ++total; written += g.Admit(Idle(), t) ? 1U : 0U; }
    // 10 minutes idle: the first line and one per minute after it.
    Check(written <= 11 && written >= 10, "idle: about one line per minute");
    Check(g.TakeSkipped() == total - written && g.TakeSkipped() == 0, "skipped count, taken once");
    Check(g.TakeRepeated(0) == 0, "quiet ticks go to quiet_skipped, not to the repeated summary");
    ol::Gate h; Check(h.Admit(Idle(), 0) && !h.Admit(Idle(), 360) && !h.Admit(Idle(), 59999) && h.Admit(Idle(), 60000),
        "first line written, then at most once per 60 s");
}

static void SampleBurst() {
    // 23 s of the session 2 burst: 281 identical AttackSample records per second.
    ol::Gate g; unsigned written = 0, total = 0;
    for (uint64_t t = 0; t < 23000; ++t) for (unsigned k = 0; k < (t % 1000 < 281 ? 1U : 0U); ++k) {
        ++total; written += g.Admit(Sample(), t) ? 1U : 0U;
    }
    Check(total == 23 * 281, "burst size");
    Check(written >= 4 && written <= 5, "identical samples: first one, then at most once per 5 s");
    Check(g.TakeRepeated(3) == total - written && g.TakeRepeated(3) == 0, "repeated count of kind 3, taken once");
    Check(g.TakeSkipped() == 0 && g.TakeRepeated(0) == 0 && g.TakeRepeated(2) == 0, "other counters untouched");
    Check(g.TakeRepeated(9) == 0, "unknown kind: no counter");
    ol::Gate h; Check(h.Admit(Sample(), 0) && !h.Admit(Sample(), 1) && !h.Admit(Sample(), 4999) && h.Admit(Sample(), 5000),
        "repeat window is 5 s");
}

static void AlwaysWritten() {
    ol::Gate g; g.Admit(Idle(), 0); g.Admit(Sample(), 0);
    ol::Facts cand = Sample(); cand.kind = 1;
    for (int i = 0; i < 100; ++i) Check(g.Admit(cand, 10 + static_cast<uint64_t>(i)), "candidate (kind 1): always, even identical");
    ol::Facts hit = Sample(); hit.contact = true;
    Check(g.Admit(hit, 200) && g.Admit(hit, 201), "sample whose contact touched the blade: always");
    ol::Facts odd = Sample(); odd.kind = 7; Check(g.Admit(odd, 300) && g.Admit(odd, 301), "unknown kind: always");
    Check(g.TakeRepeated(1) == 0 && g.TakeRepeated(3) == 0, "nothing counted as repeated");
}

static void StateChanges() {
    ol::Gate g; g.Admit(Idle(), 0);
    ol::Facts ready = Idle(); ready.ready = 1; ready.poisoned = 0; ready.failure = 0; ready.reader_failure = 0; ready.reasons = 0x1800;
    Check(g.Admit(ready, 360), "pose became ready (sword drawn): written at once");
    Check(!g.Admit(ready, 720), "same ready state: skipped");
    Check(g.Admit(Idle(), 1080), "sword away again: written at once");
    ol::Facts room = Idle(); room.room = 0x308; Check(g.Admit(room, 1440), "new room: written");
    ol::Facts weapon = Idle(); weapon.room = 0x308; weapon.weapon = 3; Check(g.Admit(weapon, 1800), "weapon change: written");
    ol::Facts reader = weapon; reader.reader_failure = 5; Check(g.Admit(reader, 2160), "other reader failure: written");

    ol::Gate s; Check(s.Admit(Sample(), 0) && !s.Admit(Sample(), 1), "sample burst starts");
    ol::Facts held = Sample(); held.on = 0x40800; Check(s.Admit(held, 2), "knife held: new state, written");
    Check(!s.Admit(held, 3) && !s.Admit(held, 4), "knife still held: repeated");
    ol::Facts trg = held; trg.trg = 0x800; Check(s.Admit(trg, 5), "trigger: written");
    ol::Facts armed = held; armed.armed = true; armed.flags = 0x1DF; Check(s.Admit(armed, 6) && !s.Admit(armed, 7), "window armed: written once");
    ol::Facts drawn = armed; drawn.ready = 1; drawn.poisoned = 0; drawn.failure = 0; drawn.reader_failure = 0;
    Check(s.Admit(drawn, 8), "pose ready inside the window: written");
    ol::Facts flags = drawn; flags.flags = 0xFF; Check(s.Admit(flags, 9), "other flags: written");
    ol::Facts reasons = flags; reasons.reasons = 0x0; Check(s.Admit(reasons, 10), "other reasons: written");
    Check(s.Admit(Sample(), 11), "back to the burst state after another state: written (transition)");
    Check(s.TakeRepeated(3) == 4, "four repeats counted");
}

static void KindsIndependent() {
    // Interleaved input edges, ticks with input and samples: each kind compares with its own last written state.
    ol::Gate g; ol::Facts edge = Sample(); edge.kind = 2; edge.flags = 0x2;
    ol::Facts tick = Sample(); tick.kind = 0; tick.on = 0x1; tick.flags = 0x2;
    unsigned written = 0;
    for (uint64_t t = 0; t < 1000; t += 10) {
        written += g.Admit(Sample(), t) ? 1U : 0U; written += g.Admit(edge, t) ? 1U : 0U; written += g.Admit(tick, t) ? 1U : 0U;
        written += g.Admit(Idle(), t) ? 1U : 0U;
    }
    Check(written == 4, "one line per kind (and one quiet tick) in a second of interleaved repeats");
    Check(g.TakeRepeated(3) == 99 && g.TakeRepeated(2) == 99 && g.TakeRepeated(0) == 99 && g.TakeSkipped() == 99,
        "repeats counted per kind; quiet ticks apart");
}

int main() {
    IdleMinute(); SampleBurst(); AlwaysWritten(); StateChanges(); KindsIndependent();
    std::printf("parry_observe_log: %u checks passed\n", checks);
    return 0;
}

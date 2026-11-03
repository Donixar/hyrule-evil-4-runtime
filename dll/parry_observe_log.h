#pragma once
// Rate limit for the `parry observe` log lines (pending 13, 3-nov). The observer publishes a periodic Tick record every
// 60 tails (~360 ms at 165 FPS). With the sword away and no input, each one read pose_poisoned=1 pose_failure=5
// (epoch Failure::TailRead: the pose reader refused the tail) pose_reader_failure=6 (ReadFailure::InfoFlags: the
// blade model is not drawn), i.e. "no sword in hand", which is expected outside a parry window. Those lines were 18% of
// the log. A quiet periodic Tick (kind 0, input on/trg 0, no armed window) is written only when its state differs
// from the last quiet one written, and otherwise at most once per kQuietMs.
// Second pass (3-nov, live session 2): with the knife/sword input active, AttackSample records (kind 3, one per
// qualified enemy attack sample, several per frame) came in identical bursts of up to 281 lines/s, 6525 in one process,
// each with its `parry contact` line. Every non-candidate record (kinds 0, 2, 3) is now written when its state differs
// from the last one written of the same kind, and an unchanged state at most once per kRepeatMs; the records skipped
// that way are counted per kind and reported by the worker as `parry observe repeated kind=K n=N` at most once per
// 5 s. The state is everything the line shows except per-record counters, timestamps and blade coordinates: kind,
// reasons, flags, pose ready/poisoned/failure/reader failure, room, weapon, character, input on/trg.
// Always written, unchanged: Candidates (kind 1, the body hits scripts/parry_log_correlate.py and the coverage census
// read one by one) and any record whose contact touched the blade (endpoint or segment intersection, or a body hit).
// Pure logic, worker thread only: no process access.
#include <cstdint>

namespace link_runtime::parry::observe_log {
struct Facts {
    uint32_t kind = 0;                 // observation::Kind: 0 Tick, 1 Candidate, 2 InputEdge, 3 AttackSample
    uint64_t on = 0, trg = 0;          // input bits of the record
    bool armed = false;                // WindowArmed or InputSampleArmed flag set
    bool contact = false;              // valid contact that intersects the blade or reports a body hit
    uint32_t reasons = 0, flags = 0, ready = 0, poisoned = 0, failure = 0, reader_failure = 0, room = 0, weapon = 0, character = 0;
};
inline bool Quiet(const Facts& f) { return f.kind == 0 && !f.on && !f.trg && !f.armed; }
inline bool Always(const Facts& f) { return f.kind == 1 || f.kind > 3 || f.contact; }

class Gate {
public:
    static constexpr uint64_t kQuietMs = 60000;
    static constexpr uint64_t kRepeatMs = 5000;
    static constexpr uint32_t kKinds = 4;
    // True: write this record. now_ms is a monotonic wall clock (logging only, never gameplay).
    bool Admit(const Facts& f, uint64_t now_ms) {
        if (Always(f)) return true;
        const bool quiet = Quiet(f);
        Slot& s = quiet ? quiet_ : kinds_[f.kind];
        if (s.seen && Same(f, s.last) && now_ms - s.written < (quiet ? kQuietMs : kRepeatMs)) {
            if (quiet) ++skipped_; else ++repeated_[f.kind];
            return false;
        }
        s.seen = true; s.last = f; s.written = now_ms;
        return true;
    }
    // Quiet records not written since the previous call (for the periodic totals line).
    uint32_t TakeSkipped() { const uint32_t n = skipped_; skipped_ = 0; return n; }
    // Repeated non-quiet records of `kind` not written since the previous call (`parry observe repeated`).
    uint32_t TakeRepeated(uint32_t kind) {
        if (kind >= kKinds) return 0;
        const uint32_t n = repeated_[kind]; repeated_[kind] = 0; return n;
    }
private:
    struct Slot { bool seen = false; Facts last{}; uint64_t written = 0; };
    static bool Same(const Facts& a, const Facts& b) {
        return a.kind == b.kind && a.on == b.on && a.trg == b.trg && a.armed == b.armed && a.reasons == b.reasons &&
               a.flags == b.flags && a.ready == b.ready && a.poisoned == b.poisoned && a.failure == b.failure &&
               a.reader_failure == b.reader_failure && a.room == b.room && a.weapon == b.weapon && a.character == b.character;
    }
    Slot quiet_{}, kinds_[kKinds]{};
    uint32_t skipped_ = 0, repeated_[kKinds]{};
};
}  // namespace link_runtime::parry::observe_log

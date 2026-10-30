// dll/ocarina_pitch.h
#pragma once
// Chromatic ocarina (Donald, 29-oct-2026: three octaves, every note; docs/ocarina-cromatica-2026-10-29.md).
// A note code travels the same int8_t path as before (NoteInput -> Session -> NoteMailbox -> voice, songs,
// staff):
//   0..4    the five OoT buttons (ocarina_input.h Note order: A, C-down, C-right, C-left, C-up), unchanged;
//   60..95  a chromatic pitch, MIDI number of the WRITTEN note C4..B6 (the OoT staff names: the button notes
//           are written D4 F4 A4 B4 D5 = 62 65 69 71 74; the ocarina sample sounds one octave higher).
// Songs and the staff compare by pitch (ToMidi), so a button and the piano key of the same written note are
// the same note for a melody.
#include <cstdint>
namespace link_runtime::ocarina {
constexpr int kPitchLo = 60, kPitchHi = 95, kPitchCount = kPitchHi - kPitchLo + 1;
inline constexpr int8_t kButtonMidi[5] = {62, 65, 69, 71, 74};
inline bool IsButtonNote(int n) { return n >= 0 && n <= 4; }
inline bool IsPitchNote(int n) { return n >= kPitchLo && n <= kPitchHi; }
inline bool ValidNote(int n) { return IsButtonNote(n) || IsPitchNote(n); }
inline int ToMidi(int n) { return IsButtonNote(n) ? kButtonMidi[n] : IsPitchNote(n) ? n : -1; }
// The button whose written pitch is `midi`, or -1.
inline int ButtonOfMidi(int midi) {
    for (int i = 0; i < 5; ++i) if (kButtonMidi[i] == midi) return i;
    return -1;
}
// Staff position: diatonic steps (half a line gap each) above the bottom line E4, and whether the note is a
// sharp (C C# D D# E F F# G G# A A# B, sharps only).
inline constexpr int8_t kLetter[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
inline constexpr bool kSharp[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
inline int StaffStep(int midi) { return (midi / 12 - 1) * 7 + kLetter[midi % 12] - (4 * 7 + 2); }
inline bool StaffSharp(int midi) { return kSharp[midi % 12]; }
// Ledger lines a note needs: the staff lines are steps 0 2 4 6 8 (E4 G4 B4 D5 F5); above, lines at 10 12 ...;
// below, -2 -4 ... `count` lines from `first` (the one nearest the staff) to the note, 0 when none.
struct Ledgers { int count = 0, first = 0, last = 0; };
inline Ledgers LedgersFor(int step) {
    Ledgers l;
    if (step >= 10) { l.first = 10; l.last = step - (step & 1); l.count = (l.last - 10) / 2 + 1; }
    else if (step <= -2) { l.first = -2; l.last = step + ((-step) & 1); l.count = (-2 - l.last) / 2 + 1; }
    return l;
}
}  // namespace link_runtime::ocarina

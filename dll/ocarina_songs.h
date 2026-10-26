// dll/ocarina_songs.h
#pragma once
// Ocarina phase 2: songs (Donald, 12-oct-2026; docs/ocarina-serenity-2026-10-12.md). "Serenity" (RE4's
// save-room theme) opens the native save screen (save_anywhere.h); since 17-oct Sheik and Epona (below) run
// combat effects. The Flow below is the phase-2 single-song flow (Serenity only, kept for its tests); the
// runtime uses ocarina_staff.h ReplayFlow, which carries the song table.
//
// Flow, OoT style, with the phase-1 session untouched:
//   1. Every note the session plays (Host::PlayNote of ocarina_session.h) also goes to OnNote. When the
//      last N notes equal the melody (exact sequence) and Host::SongAllowed passes, the flow holds the
//      last note for kHoldFrames and then asks the runtime to stow (WantStow -> InputEvents::stow), so the
//      session runs its own exit: exit clip, unmount, FreezeEnd, Ended. Nothing here touches the world.
//   2. OnEnded(token, Stowed) for that session plays the success jingle; any other reason cancels.
//   3. kJingleFrames later, on a gameplay (non-native) frame where SongAllowed passes again, RunEffect.
//      A frame that does not qualify waits, up to kEffectWaitFrames, then gives up (logged, no effect).
// A blocked match (SongAllowed false while playing) is logged and the player keeps playing.
//
// Host contract (game thread only):
//   bool SongAllowed(Song, const char*& why);   // effect rules; also "world thawed" (freeze not pending)
//   void PlayJingle();                          // queue link_ocarina_song_ok.wav to the audio worker
//   bool RunEffect(Song);                       // Serenity: SaveAnywhere::Open
//   void SongLog(const char* text, int code);   // record for the worker log ("ocarina kind=9")
// Frames are 60 Hz frames of game time (game_clock.h Step60), so the waits keep their seconds at 165 FPS.
#include <cstdint>
#include "game_clock.h"

namespace link_runtime::ocarina::songs {
// Sheik and Epona (17-oct, docs/ocarina-canciones-sheik-epona-2026-10-17.md): combat songs whose effects live
// in ocarina_song_effects.h (flash stun, wolf charge); the replay flow is ocarina_staff.h ReplayFlow.
enum class Song : int8_t { None = -1, Serenity = 0, Sheik = 1, Epona = 2 };
inline const char* SongName(Song s) {
    switch (s) { case Song::Serenity: return "serenity"; case Song::Sheik: return "sheik"; case Song::Epona: return "epona"; default: return "song"; }
}
// Note index = ocarina_input.h Note: 0 A (D4), 1 C-down (F4), 2 C-right (A4), 3 C-left (B4), 4 C-up (D5).
struct Melody { uint8_t length; int8_t notes[8]; };
// Reference motif (scripts/ocarina_serenity.py): G4 F#4 B4 A4 | G4 F#4 D4.
// A: B, A and D exact, G -> A4, F# -> F4.   B: contour by rank (D<F#<G<A<B -> D4<F4<A4<B4<D5).
inline constexpr Melody kSerenityA{7, {2, 1, 3, 2, 2, 1, 0}};
inline constexpr Melody kSerenityB{7, {2, 1, 4, 3, 2, 1, 0}};
// Epona's Song as OoT writes it: C-up C-left C-right twice (D5 B4 A4 | D5 B4 A4).
inline constexpr Melody kEpona{6, {4, 3, 2, 4, 3, 2}};
// Sheik: the opening phrase of OoT's Sheik's Theme (Donald, 17-oct: the real melody, not an invented motif).
// The harp lead of the PAL 1.1 sequence (D:/link-in-re4-work/sheik-music-source-01/timeline.json, channel 10
// layer 0, transpose +12; onsets confirmed in the ROM render) plays Bb5 F5 G5 F5 D5 Bb4 C5 (24 24 48 48 24 24
// 96 ticks). Down a minor sixth it is D5 A4 B4 A4 F#4 D4 E4: five notes exact on the ocarina, F#4 -> F4 (-1)
// and E4 -> F4 (+1), every step keeps its direction:
//   Bb5->C-up  F5->C-right  G5->C-left  F5->C-right  D5->C-down  Bb4->A  C5->C-down
inline constexpr Melody kSheik{7, {4, 2, 3, 2, 1, 0, 1}};

// a is a suffix of b (equal included): the detector would fire a on the way to b, so b could never match.
inline bool SuffixOf(const Melody& a, const Melody& b) {
    if (!a.length || a.length > 8 || b.length > 8 || a.length > b.length) return false;
    for (uint8_t i = 0; i < a.length; ++i) if (a.notes[a.length - 1 - i] != b.notes[b.length - 1 - i]) return false;
    return true;
}
inline bool Clash(const Melody& a, const Melody& b) { return SuffixOf(a, b) || SuffixOf(b, a); }

// [Ocarina] Serenity=1 enables the song (Enabled=1 is still the master); SerenityMelody=A|B picks the
// candidate (anything else keeps A). Sheik=1 / Epona=1 enable the combat songs; SheikRadius / EponaRadius
// are their reach in mm (1000..15000; anything else keeps the default and counts a problem).
// Get(name, buffer, count) has the GetPrivateProfileStringW shape.
constexpr unsigned kSheikRadius = 6000, kEponaRadius = 8000, kMinRadius = 1000, kMaxRadius = 15000;
struct Config {
    bool serenity = false; Melody melody = kSerenityA;
    bool sheik = false, epona = false; unsigned sheik_radius = kSheikRadius, epona_radius = kEponaRadius;
    // OcarinaSongPlayback (18-oct, Donald): after a correct song the whole song plays (backing + ocarina lead,
    // Bin32/link_ocarina_play_<song>.wav) in the ocarina pose with the world frozen, then the effect; the player
    // (toggle / stow) or damage cut it and nothing fires. Missing or exact 1 = on, exact 0 = off, else a problem (on).
    bool playback = true;
    unsigned problems = 0;
};
template <class Get> Config ReadConfig(Get get) {
    Config c; wchar_t v[8] = {};
    get(L"Serenity", v, 8); c.serenity = v[0] == L'1' && !v[1];
    v[0] = 0; get(L"SerenityMelody", v, 8);
    if ((v[0] == L'B' || v[0] == L'b') && !v[1]) c.melody = kSerenityB;
    v[0] = 0; get(L"Sheik", v, 8); c.sheik = v[0] == L'1' && !v[1];
    v[0] = 0; get(L"Epona", v, 8); c.epona = v[0] == L'1' && !v[1];
    auto radius = [&](const wchar_t* key, unsigned& out) {
        v[0] = 0; get(key, v, 8);
        if (!v[0]) return;
        unsigned n = 0; unsigned i = 0;
        for (; v[i] && i < 6; ++i) { if (v[i] < L'0' || v[i] > L'9') break; n = n * 10 + unsigned(v[i] - L'0'); }
        if (v[i] || n < kMinRadius || n > kMaxRadius) { ++c.problems; return; }
        out = n;
    };
    radius(L"SheikRadius", c.sheik_radius); radius(L"EponaRadius", c.epona_radius);
    v[0] = 0; get(L"OcarinaSongPlayback", v, 8);
    if (v[0] == L'0' && !v[1]) c.playback = false;
    else if (v[0] && !(v[0] == L'1' && !v[1])) ++c.problems;
    return c;
}

// Last 8 notes of the current session; a match consumes them, so one melody fires once.
class Detector {
    int8_t notes_[8] = {}; uint8_t count_ = 0;
public:
    void Reset() { count_ = 0; }
    uint8_t Size() const { return count_; }
    bool Push(int8_t note, const Melody& m) {
        if (note < 0 || note > 4) return false;
        if (count_ == 8) { for (int i = 1; i < 8; ++i) notes_[i - 1] = notes_[i]; --count_; }
        notes_[count_++] = note;
        if (!m.length || m.length > 8 || count_ < m.length) return false;
        for (uint8_t i = 0; i < m.length; ++i) if (notes_[count_ - m.length + i] != m.notes[i]) return false;
        count_ = 0;
        return true;
    }
};

enum class Phase { Idle, Hold, Stowing, Jingle };
constexpr double kHoldFrames = 30;          // 0.5 s: the last note sounds before the exit clip
constexpr double kStowFrames = 900;         // 15 s: the session's own watchdog (600) ends it first
constexpr double kJingleFrames = 75;        // 1.25 s: the chime's notes, its tail plays under the menu
constexpr double kEffectWaitFrames = 300;   // 5 s for a qualifying frame after the jingle

template <class Host> class Flow {
    Config cfg_; Detector det_; Phase phase_ = Phase::Idle; uint64_t token_ = 0, seen_ = 0;
    double elapsed_ = 0; bool stow_ = false;
    void To(Phase p) { phase_ = p; elapsed_ = 0; }
public:
    explicit Flow(Config c = {}) : cfg_(c) {}
    void Configure(Config c) { cfg_ = c; det_.Reset(); To(Phase::Idle); stow_ = false; }
    Phase phase() const { return phase_; }
    bool Busy() const { return phase_ != Phase::Idle; }
    // Taken once by the runtime and OR-ed into InputEvents::stow (the session stows only while Playing).
    bool WantStow() { const bool s = stow_; stow_ = false; return s; }
    void OnNote(uint64_t token, int8_t note, Host& h) {
        if (!cfg_.serenity) return;
        if (token != seen_) { seen_ = token; det_.Reset(); }
        if (phase_ != Phase::Idle) return;
        if (!det_.Push(note, cfg_.melody)) return;
        const char* why = "";
        if (!h.SongAllowed(Song::Serenity, why)) { h.SongLog(why && *why ? why : "serenity blocked", 1); return; }
        token_ = token; To(Phase::Hold); h.SongLog("serenity matched", 0);
    }
    void OnEnded(uint64_t token, int reason_stowed, Host& h) {
        if ((phase_ != Phase::Hold && phase_ != Phase::Stowing) || token != token_) return;
        stow_ = false;
        if (!reason_stowed) { To(Phase::Idle); h.SongLog("serenity cancelled (session)", 2); return; }
        h.PlayJingle(); To(Phase::Jingle); h.SongLog("serenity jingle", 0);
    }
    // Every frame, gameplay (native=false) or native event/door frame (native=true).
    void Tick(Host& h, bool native, double step = 1.0) {
        if (phase_ == Phase::Idle) return;
        elapsed_ += step;
        switch (phase_) {
        case Phase::Hold:
            if (game_clock::Reached(elapsed_, kHoldFrames)) { stow_ = true; To(Phase::Stowing); }
            return;
        case Phase::Stowing:
            if (game_clock::Reached(elapsed_, kStowFrames)) { stow_ = false; To(Phase::Idle); h.SongLog("serenity cancelled (stow timeout)", 3); }
            return;
        case Phase::Jingle: {
            if (!game_clock::Reached(elapsed_, kJingleFrames)) return;
            const char* why = "";
            if (!native && h.SongAllowed(Song::Serenity, why)) {
                const bool ok = h.RunEffect(Song::Serenity);
                To(Phase::Idle); h.SongLog(ok ? "serenity effect" : "serenity effect failed", ok ? 0 : 4);
                return;
            }
            if (game_clock::Reached(elapsed_, kJingleFrames + kEffectWaitFrames)) {
                To(Phase::Idle); h.SongLog(native ? "serenity gave up (native frame)" : (why && *why ? why : "serenity gave up"), 5);
            }
            return;
        }
        default: return;
        }
    }
};
}  // namespace link_runtime::ocarina::songs

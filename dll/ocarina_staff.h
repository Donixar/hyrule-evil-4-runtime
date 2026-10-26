// dll/ocarina_staff.h
#pragma once
// Ocarina of Time presentation for the RE4 ocarina (Donald, 12-oct-2026): the OoT note staff at the
// bottom of the screen and the song replay. Spec, attribution and tests: docs/ocarina-staff-2026-10-12.md.
// Pure logic and native layout: no hooks, no process access; the runtime wires it (diff in the doc).
//
// Drawing uses the game's own 2D sprite system, IDSystem (IdSys, the HUD "id units" of the life meter
// and the action prompt), not a D3D9 hook: re4_fps/re4_tweaks already own Present/EndScene. Each unit is
// a native textured quad queued in the native OT (otType 0x13 = cockpit, so the game hides it with the
// rest of the HUD: events, case, DPF_COCKPIT). The art is one id texture (tex id 0xED) of six frames
// (staff + five notes), registered with the native IdTexDataLoad from a block the DLL loads
// (Bin32/link_ocarina_staff.eff, scripts/ocarina_staff.py); its TPL points at six DDS appended to the
// core HUD pack 07000000 (the same append the approved ocarina mount uses on the player packs).
//
// Every address is an RVA of the pinned bio4.exe (SHA 19aed4af..., preferred base 0x400000): live
// address = module base + RVA. Absolute operands inside kPins are compared after image_relocation.h
// undoes the loader's fixups (native_pins.h kOcarinaStaff, aliased here as kPins).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game_clock.h"
#include "native_pins.h"

namespace link_runtime::ocarina::staff {
namespace layout {
// IDSystem IdSys (GC decomp include/id_sys.h; UHD: m_maxId +0x00, set bits +0x0C, units +0x50).
constexpr uint32_t idsys_rva = 0x859788;
constexpr uint32_t idsys_max = 0x00, idsys_set_bits = 0x0C, idsys_units = 0x50;
// thiscall ECX = IdSys. set(ID_FILE_HEADER*, markNo, classNo, otType, otNo, Attr), RET 0x18 (0x248EB9).
constexpr uint32_t set_thunk_rva = 0x4449, set_rva = 0x2451C0;
// kill(markNo, classNo), RET 8. unitPtr(markNo, classNo), RET 8: a miss returns the static dummy unit.
constexpr uint32_t kill_thunk_rva = 0xCE9B, kill_rva = 0x241A30;
constexpr uint32_t unit_ptr_thunk_rva = 0x95F7, unit_ptr_rva = 0x243580, unit_dummy_rva = 0x8599D0;
// cdecl IdTexDataLoad(void* block, int owner) (version 0xB block) and IdTexRelease(int owner).
constexpr uint32_t tex_load_thunk_rva = 0x1F46, tex_load_rva = 0x24A330;
constexpr uint32_t tex_release_thunk_rva = 0xBF50, tex_release_rva = 0x24A310;
// g_pIdTexSys (cTexSys*): TEX_WK[256] of 0x54 bytes from +0, owner at +0x50 (0 = free; 0x2D4ED0).
constexpr uint32_t tex_sys_ptr_rva = 0x859B8C, tex_wk_size = 0x54, tex_wk_owner = 0x50;
// ID_UNIT, UHD: 0x144 bytes (GC 0x138); every field from 0x08 on sits 0xC after the GC layout.
constexpr uint32_t unit_size = 0x144;
constexpr uint32_t u_be_flag = 0x00, u_class = 0x02, u_mark = 0x03, u_tex_id = 0x78, u_tex_no = 0x7A,
                   u_tex_flag = 0x8B, u_pos0 = 0x94, u_size_w = 0xDC, u_size_h = 0xE0, u_col0 = 0xEC;
constexpr uint8_t be_free = 0xFF, be_alive = 0x01, be_visible = 0x08, be_drawing = 0x10;
// Ours. Class 0x3C: no set/kill/unitPtr call in the EXE uses an immediate class in 0x36..0x3F
// (census of 141 set, 104 kill and 475 unitPtr calls). Tex id 0xED: absent from the cockpit block
// (core 0x1D) and from every SS/Etc id texture block, not in IdTexDataLoad's no-check list
// (1 4 10 24 30 31 70 128 239) nor in set()'s PC-glyph remaps (0x14 0x30 0x31 0x49 0x89 0x92 0x96
// 0xA5 0xB4..0xB6 0xBC 0xC1 0xF1 0xF2). Owner 0xCA0: native owners are 0 4 6 7 9 0xA 0x200.
constexpr uint8_t id_class = 0x3C, tex_id = 0xED, ot_type = 0x13, ot_no = 5;
constexpr uint32_t tex_owner = 0xCA0;
constexpr uint8_t tex_flag_hold = 0x02;  // idSysMove04 (0x242CBA) leaves texNo alone
// Renderer post-projection matrix (float[16], read by ztarget_reticle_runtime.h): element 5 is the
// vertical crop UHD applies to the 640x480 HUD plane, 1 at 4:3 and 4/3 at 16:9 (HUD 640x360).
constexpr uint32_t post_rva = 0xCED730, post_y = 5 * 4;
// Backbuffer size (int32), written only by device create/reset (0x53A320: arg1 -> W at 0xCED9F0, arg2 -> H
// at 0xCEDA98) and its restore thunks 0x703680/0x703690. Every gameplay pass renders at h 448, so the crop
// in post element 5 is exactly max(1, 0.75 W/H) (0x576960, 0x5333A0; review wf_d84e108c).
constexpr uint32_t screen_w_rva = 0xCED9F0, screen_h_rva = 0xCEDA98;
}  // namespace layout

using Pin = native_pins::Pin;
// The IdSys pins live in native_pins.h (kOcarinaStaff); the wiring checks them before arming.
inline constexpr const auto& kPins = native_pins::kOcarinaStaff;

// ---------------------------------------------------------------- geometry (IdSys screen units)
// 640x480 virtual screen, origin at the centre, y up (life meter group at (217,-130), action prompt
// at y -140..-153 in core 0x1F/0x20). The staff sits at the bottom, centred: x 0 is the centre at
// any aspect; Fit (below) places it for the visible height. Texture: 512x128 staff, lines 16 px apart
// at rows 32..96 (E4 G4 B4 D5 F5).
constexpr int kSlots = 8, kUnits = 1 + kSlots;
struct Geometry {
    float staff_x = 0.0f, staff_y = -187.0f, staff_w = 360.0f, staff_h = 90.0f;
    float note = 28.0f, slot_x0 = -88.0f, slot_dx = 34.0f;
};
// Pitch steps above the bottom line (E4), half a line gap each: D4 F4 A4 B4 D5 (ocarina_input.h order).
inline constexpr int8_t kStep[5] = {-1, 1, 3, 4, 6};
inline float LineGap(const Geometry& g) { return g.staff_h * 16.0f / 128.0f; }
inline float BottomLine(const Geometry& g) { return g.staff_y - g.staff_h * 32.0f / 128.0f; }
inline float NoteY(const Geometry& g, int note) {
    return note < 0 || note > 4 ? g.staff_y : BottomLine(g) + kStep[note] * LineGap(g) * 0.5f;
}
inline float SlotX(const Geometry& g, int slot) { return g.slot_x0 + slot * g.slot_dx; }

// The design above assumed the 4:3 height (y down to -240). UHD keeps the HUD 640 wide and crops its
// height by the post-projection Y scale, so at 16:9 the bottom edge is y -180 and a staff centred at
// -187 showed only its top quarter (Donald, 16-oct). Fit puts the staff's bottom kFitMargin above the
// visible edge for the current crop, at 0.9 scale so its right end (x 162) clears the life meter
// ring (x ~172 at 16:9). The crop is max(1, 0.75 W/H) (0x576960): 1 at 4:3 and narrower, 1.2 at 16:10,
// 4/3 at 16:9, 2.67 at 32:9. An unreadable or implausible value falls back to 16:9.
constexpr float kFitScale = 0.9f, kFitMargin = 10.0f, kDefaultPostY = 4.0f / 3.0f;
constexpr float kMinPostY = 0.99f, kMaxPostY = 4.0f;
inline bool PlausiblePostY(float v) { return std::isfinite(v) && v >= kMinPostY && v <= kMaxPostY; }
inline float VisibleHalfHeight(float post_y) {
    if (!PlausiblePostY(post_y)) post_y = kDefaultPostY;
    return 240.0f / post_y;
}
// The crop from the backbuffer size; 0 for a size that is not a real screen.
inline float CropFromScreen(int32_t w, int32_t h) {
    if (w < 320 || h < 200 || w > 16384 || h > 16384) return 0;
    const float c = 0.75f * float(w) / float(h);
    return c < 1.0f ? 1.0f : c;
}
inline Geometry Fit(float half_h, const Geometry& d = Geometry{}) {
    Geometry g;
    g.staff_x = d.staff_x; g.staff_w = d.staff_w * kFitScale; g.staff_h = d.staff_h * kFitScale;
    g.note = d.note * kFitScale; g.slot_x0 = d.slot_x0 * kFitScale; g.slot_dx = d.slot_dx * kFitScale;
    g.staff_y = -half_h + kFitMargin + g.staff_h * 0.5f;
    return g;
}

// ---------------------------------------------------------------- IDSystem::set v2 table
// "2.00", GroupNo 0, UnitNum 9; entries of 0x8C (UHD v2 stride, 0x24531C). Unit 0 = staff, 1..8 = slots.
constexpr size_t kEntry = 0x8C, kTableSize = 8 + kUnits * kEntry;
inline void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
inline void PutF(uint8_t* p, float v) { std::memcpy(p, &v, 4); }
inline void BuildTable(uint8_t* t, const Geometry& g = Geometry{}) {
    std::memset(t, 0, kTableSize);
    std::memcpy(t, "2.00", 4);
    t[5] = uint8_t(kUnits);
    for (int i = 0; i < kUnits; ++i) {
        uint8_t* e = t + 8 + i * kEntry;
        e[0x03] = 0;                 // flags: set() ORs alive|move|visible itself
        e[0x04] = uint8_t(i);        // markNo
        e[0x05] = uint8_t(0x40 + i); // unitNo
        e[0x07] = 0xFF;              // no parent
        e[0x0B] = layout::tex_id;
        PutF(e + 0x14, i ? SlotX(g, i - 1) : g.staff_x);
        PutF(e + 0x18, g.staff_y);
        PutF(e + 0x50, i ? g.note : g.staff_w);
        PutF(e + 0x54, i ? g.note : g.staff_h);
        e[0x58] = e[0x59] = e[0x5A] = 0xFF; e[0x5B] = 0;  // white, transparent until the first view
        e[0x6F] = layout::tex_flag_hold;                   // texNo is ours
    }
}

// ---------------------------------------------------------------- what the staff shows
enum class Mode : uint8_t { Hidden, Live, Replay, Jingle, Fade };
struct UnitView { bool visible = false; uint8_t tex_no = 0; float x = 0, y = 0, w = 0, h = 0; uint8_t rgba[4] = {}; };

// Notes of the session (last 8, scrolling), replay lighting, jingle pulse and fade. Time in 60 Hz frames.
class Staff {
    int8_t note_[kSlots] = {}; uint8_t count_ = 0; uint8_t lit_ = 0;
    double age_[kSlots] = {}; double mode_t_ = 0; Mode mode_ = Mode::Hidden;
public:
    static constexpr double kPop = 6, kJinglePeriod = 20, kFade = 18, kDimRgb = 96, kDimA = 200;
    Mode mode() const { return mode_; }
    uint8_t count() const { return count_; }
    int8_t note(int slot) const { return slot >= 0 && slot < count_ ? note_[slot] : -1; }
    void Open() { count_ = 0; lit_ = 0; mode_ = Mode::Live; mode_t_ = 0; }
    void Close() { count_ = 0; lit_ = 0; mode_ = Mode::Hidden; mode_t_ = 0; }
    void Push(int8_t n) {
        if (mode_ != Mode::Live || n < 0 || n > 4) return;
        if (count_ == kSlots) {
            for (int i = 1; i < kSlots; ++i) { note_[i - 1] = note_[i]; age_[i - 1] = age_[i]; }
            --count_;
        }
        note_[count_] = n; age_[count_] = 0; ++count_;
    }
    // The matched melody replaces the staff, dimmed; Light(i) brightens note i as the replay plays it.
    void BeginReplay(const int8_t* notes, uint8_t n) {
        if (mode_ == Mode::Hidden) return;
        count_ = n > kSlots ? uint8_t(kSlots) : n;
        for (uint8_t i = 0; i < count_; ++i) { note_[i] = notes[i]; age_[i] = kPop; }
        lit_ = 0; mode_ = Mode::Replay; mode_t_ = 0;
    }
    void Light(uint8_t i) { if (mode_ == Mode::Replay && i < count_ && i >= lit_) { lit_ = uint8_t(i + 1); age_[i] = 0; } }
    void BeginJingle() { if (mode_ != Mode::Hidden) { lit_ = count_; mode_ = Mode::Jingle; mode_t_ = 0; } }
    void BeginFade() { if (mode_ != Mode::Hidden && mode_ != Mode::Fade) { mode_ = Mode::Fade; mode_t_ = 0; } }
    void Step(double dt) {
        if (mode_ == Mode::Hidden) return;
        mode_t_ += dt;
        for (uint8_t i = 0; i < count_; ++i) age_[i] += dt;
        if (mode_ == Mode::Fade && game_clock::Reached(mode_t_, kFade)) Close();
    }
    // Unit 0 = staff, 1..8 = slots.
    UnitView View(int unit, const Geometry& g = {}) const {
        UnitView v;
        if (mode_ == Mode::Hidden || unit < 0 || unit >= kUnits) return v;
        double alpha = 255;
        if (mode_ == Mode::Fade) alpha = 255 * (1.0 - (mode_t_ < kFade ? mode_t_ / kFade : 1.0));
        if (unit == 0) {
            v.visible = alpha > 0.5; v.tex_no = 0; v.x = g.staff_x; v.y = g.staff_y; v.w = g.staff_w; v.h = g.staff_h;
            v.rgba[0] = v.rgba[1] = v.rgba[2] = 255; v.rgba[3] = uint8_t(alpha);
            return v;
        }
        const int slot = unit - 1;
        if (slot >= count_) return v;
        const int8_t n = note_[slot];
        const bool dim = mode_ == Mode::Replay && slot >= lit_;
        double scale = 1.0, rgb = 255, a = alpha;
        if (dim) { rgb = kDimRgb; a = alpha * kDimA / 255.0; }
        else if (age_[slot] < kPop) scale = 1.3 - 0.3 * age_[slot] / kPop;  // OoT-like pop when it sounds
        if (mode_ == Mode::Jingle) {  // all notes pulse white-gold with the chime
            const double ph = mode_t_ / kJinglePeriod; const double tri = ph - double(int(ph));
            scale = 1.0 + 0.12 * (tri < 0.5 ? tri * 2 : (1 - tri) * 2);
        }
        v.visible = a > 0.5; v.tex_no = uint8_t(1 + n);
        v.x = SlotX(g, slot); v.y = NoteY(g, n); v.w = v.h = float(g.note * scale);
        v.rgba[0] = v.rgba[1] = v.rgba[2] = uint8_t(rgb); v.rgba[3] = uint8_t(a);
        return v;
    }
};

// ---------------------------------------------------------------- replay timing
// Onsets in 60 Hz frames, the reference rhythm of scripts/ocarina_serenity.py (1.56 2.30 2.67 3.78 |
// 4.92 6.04 6.80 s, from the first note): the replay plays the song as written, not as the player did.
struct Rhythm { uint8_t length; uint16_t onset[8]; };
inline constexpr Rhythm kSerenityRhythm{7, {0, 44, 67, 133, 202, 269, 314, 0}};
// Epona (OoT, 3/4 waltz at ~180 bpm): two quarters and a held half per bar, D5 B4 A4 | D5 B4 A4.
inline constexpr Rhythm kEponaRhythm{6, {0, 20, 40, 90, 110, 130, 0, 0}};
// Sheik's Theme, first phrase: the onsets of the ROM render (0.016 0.360 0.704 1.384 2.160 2.496 2.880 s; the
// sequence's tempo map 90/84/80/85 bpm gives 0 0.33 0.67 1.38 2.13 2.48 2.84 s), in 60 Hz frames from the first.
inline constexpr Rhythm kSheikRhythm{7, {0, 21, 41, 82, 129, 149, 172, 0}};
// OcarinaSongPlayback (18-oct): the lead onsets inside link_ocarina_play_<song>.wav, 60 Hz frames from the file's start
// (scripts/ocarina_song_playback.py report.json staff_onsets60): the staff lights each note as the recording plays it.
inline constexpr Rhythm kSerenityPlayRhythm{7, {1, 44, 68, 137, 203, 270, 316, 0}};
inline constexpr Rhythm kSheikPlayRhythm{7, {1, 21, 41, 84, 129, 151, 172, 0}};
inline constexpr Rhythm kEponaPlayRhythm{6, {0, 18, 36, 108, 125, 144, 0, 0}};   // Donald's recording (onsets detected)
// A replay that plays recorded music (Extra::audio_frames) instead of the note WAVs: the chime follows the
// end of the recording after this gap.
constexpr double kAudioJingleGap = 15;      // 0.25 s
constexpr double kLeadFrames = 30;          // 0.5 s: the last played note sounds, the staff dims
constexpr double kJingleAfterLast = 75;     // 1.25 s after the last onset (note 0.95 s + 0.3 s)
constexpr double kJingleHoldFrames = 96;    // 1.6 s of chime with the ocarina still out, then stow
constexpr double kFadeLead = Staff::kFade;  // the staff fades in the last frames before stowing
constexpr double kStowWatchFrames = 900;    // 15 s for the session's exit
constexpr double kEffectDelayFrames = 20;   // world thawed, chime tail under the menu
constexpr double kEffectWaitFrames = 300;   // 5 s for a qualifying frame

enum class Phase { Idle, Lead, Replay, Tail, JingleHand, Stowing, Effect };
inline const char* PhaseName(Phase p) {
    switch (p) {
    case Phase::Idle: return "idle"; case Phase::Lead: return "lead"; case Phase::Replay: return "replay";
    case Phase::Tail: return "tail"; case Phase::JingleHand: return "jingle"; case Phase::Stowing: return "stowing";
    case Phase::Effect: return "effect";
    }
    return "?";
}

// Drop-in for songs::Flow<Host> (same OnNote/OnEnded/Tick/WantStow/Busy), OoT order:
//   match -> Lead -> Replay (Host::ReplayNote at each onset, Link keeps the loop pose, world frozen)
//   -> Tail -> JingleHand (Host::PlayJingle, ocarina still out, staff pulses, then fades)
//   -> WantStow -> session exit -> OnEnded(Stowed) -> Effect (SongAllowed, RunEffect).
// Host adds to the songs::Flow contract:  void ReplayNote(uint64_t token, int8_t note);
// (push to the NoteMailbox under the session token: the same waveOut path and 15 ms fades as a played
// note; Close(token) at the session's end silences it). While Busy() before Stowing, the wiring masks the
// player's notes and stow (OoT ignores input during the replay); any other session end cancels.
//
// Song table (17-oct): Serenity is entry 0 (Config::serenity/melody/rhythm, unchanged); Config::extra adds
// songs (Sheik, Epona) with their own melody, written rhythm and log name. Every history suffix is matched
// against the enabled entries in table order; Configure drops an extra whose melody is a suffix of (or has
// as suffix) an earlier enabled one, since the detector would fire the shorter one first. The log lines are
// "<name> matched", "<name> replay", ... ; for Serenity they are the exact strings of the one-song flow.
template <class Host, class Melody, class SongEnum> class ReplayFlow {
public:
    // audio_frames: length (60 Hz frames) of a recorded replay (Sheik's Theme). When it is set and the host has
    // `bool ReplayAudio(uint64_t token, SongEnum)` returning true, the replay plays that recording instead of the
    // note WAVs: the staff still lights each note at its onset, the chime waits for the recording's end. A host
    // without it, or a false return (file missing), keeps the note replay.
    // audio_rhythm (18-oct, OcarinaSongPlayback): the lead onsets inside the recording, used for the staff while the
    // recording plays (length 0: `rhythm`). Serenity's recording is Config::audio_frames / audio_rhythm.
    struct Extra { SongEnum song{}; bool enabled = false; Melody melody{}; Rhythm rhythm{}; const char* name = "song"; uint16_t audio_frames = 0;
                   Rhythm audio_rhythm{}; };
    static constexpr unsigned kExtraSongs = 3;
    // cancelable (18-oct, OcarinaSongPlayback=1): the player may cut the replay (CancelOpen / Cancel); no effect then.
    struct Config { bool serenity = false; Melody melody{}; Rhythm rhythm = kSerenityRhythm; Extra extra[kExtraSongs]{}; unsigned extra_count = 0;
                    uint16_t audio_frames = 0; Rhythm audio_rhythm{}; bool cancelable = false; };
private:
    Config cfg_; int8_t hist_[8] = {}; uint8_t hn_ = 0; Phase phase_ = Phase::Idle;
    uint64_t token_ = 0, seen_ = 0; double t_ = 0; uint8_t next_ = 0; bool stow_ = false; Staff staff_;
    int8_t cur_ = 0;                 // 0 = Serenity, 1 + i = cfg_.extra[i]; the song in flight
    unsigned dropped_ = 0;           // extras refused by Configure (melody clash)
    bool audio_ = false;             // this replay plays the song's recording
    uint16_t AudioFrames() const { return cur_ == 0 ? cfg_.audio_frames : cfg_.extra[cur_ - 1].audio_frames; }
    template <class H> static auto TryAudio(H& h, uint64_t t, SongEnum s, int) -> decltype(bool(h.ReplayAudio(t, s))) { return h.ReplayAudio(t, s); }
    template <class H> static bool TryAudio(H&, uint64_t, SongEnum, long) { return false; }
    template <class H> static auto TryStop(H& h, uint64_t t, int) -> decltype(h.StopReplay(t), void()) { h.StopReplay(t); }
    template <class H> static void TryStop(H&, uint64_t, long) {}
    // The staff onsets: the recording's own lead onsets while it plays (when given), else the written rhythm.
    const Rhythm& ActiveRhythm() const {
        const Rhythm& a = cur_ == 0 ? cfg_.audio_rhythm : cfg_.extra[cur_ - 1].audio_rhythm;
        return audio_ && a.length ? a : RhythmOf(cur_);
    }
    char line_[112] = {};
    // Why the effect waited: the give-up names the most frequent reason, not the last one (PID 36944 blamed
    // "Link not idle" after 5 s of an enemy at 5.3 m because Link started walking at the end).
    struct Reason { const char* text; unsigned count; };
    Reason why_[4] = {}; char gave_up_[112] = {};
    void Tally(const char* why) {
        if (!why || !*why) return;
        for (Reason& r : why_) {
            if (r.text && std::strcmp(r.text, why) == 0) { ++r.count; return; }
            if (!r.text) { r = {why, 1}; return; }
        }
    }
    const char* GaveUp() {
        const Reason* best = nullptr;
        for (const Reason& r : why_) if (r.text && (!best || r.count > best->count)) best = &r;
        if (!best) { std::snprintf(gave_up_, sizeof(gave_up_), "%s gave up", Name()); return gave_up_; }
        std::snprintf(gave_up_, sizeof(gave_up_), "%s gave up: %s", Name(), best->text);
        return gave_up_;
    }
    void To(Phase p) { phase_ = p; t_ = 0; if (p == Phase::Effect) for (Reason& r : why_) r = {}; }
    static bool Usable(const Melody& m) { return m.length && m.length <= 8; }
    static bool Suffix(const Melody& a, const Melody& b) {   // a is a suffix of b
        if (!Usable(a) || !Usable(b) || a.length > b.length) return false;
        for (uint8_t i = 0; i < a.length; ++i) if (a.notes[a.length - 1 - i] != b.notes[b.length - 1 - i]) return false;
        return true;
    }
    bool On(int8_t s) const { return s == 0 ? cfg_.serenity : (s > 0 && unsigned(s) <= cfg_.extra_count && cfg_.extra[s - 1].enabled); }
    const Melody& MelodyOf(int8_t s) const { return s == 0 ? cfg_.melody : cfg_.extra[s - 1].melody; }
    const Rhythm& RhythmOf(int8_t s) const { return s == 0 ? cfg_.rhythm : cfg_.extra[s - 1].rhythm; }
    SongEnum Id() const { return cur_ == 0 ? SongEnum::Serenity : cfg_.extra[cur_ - 1].song; }
    const char* Name() const { return cur_ == 0 ? "serenity" : cfg_.extra[cur_ - 1].name; }
    const char* Line(const char* what) { std::snprintf(line_, sizeof(line_), "%s %s", Name(), what); return line_; }
    bool Match(int8_t s) const {
        const Melody& mel = MelodyOf(s); const uint8_t m = mel.length;
        if (!m || m > 8 || hn_ < m) return false;
        for (uint8_t i = 0; i < m; ++i) if (hist_[hn_ - m + i] != mel.notes[i]) return false;
        return true;
    }
public:
    explicit ReplayFlow(Config c = {}) { Configure(c); }
    void Configure(Config c) {
        if (c.extra_count > kExtraSongs) c.extra_count = kExtraSongs;
        dropped_ = 0;
        for (unsigned i = 0; i < c.extra_count; ++i) {
            Extra& e = c.extra[i];
            if (!e.enabled) continue;
            bool clash = !Usable(e.melody) || (c.serenity && (Suffix(e.melody, c.melody) || Suffix(c.melody, e.melody)));
            for (unsigned j = 0; j < i && !clash; ++j)
                clash = c.extra[j].enabled && (Suffix(e.melody, c.extra[j].melody) || Suffix(c.extra[j].melody, e.melody));
            if (clash) { e.enabled = false; ++dropped_; }
        }
        cfg_ = c; hn_ = 0; stow_ = false; cur_ = 0; To(Phase::Idle); staff_.Close();
    }
    unsigned dropped() const { return dropped_; }
    bool Enabled(SongEnum s) const {
        if (s == SongEnum::Serenity) return cfg_.serenity;
        for (unsigned i = 0; i < cfg_.extra_count; ++i) if (cfg_.extra[i].song == s && cfg_.extra[i].enabled) return true;
        return false;
    }
    SongEnum song() const { return Id(); }   // the song in flight (Serenity when idle)
    bool audio() const { return audio_; }    // the replay in flight plays its recording
    Phase phase() const { return phase_; }
    bool Busy() const { return phase_ != Phase::Idle; }
    bool InputLocked() const { return phase_ != Phase::Idle && phase_ != Phase::Stowing && phase_ != Phase::Effect; }
    const Staff& staff() const { return staff_; }
    bool WantStow() { const bool s = stow_; stow_ = false; return s; }
    // The runtime calls this when the session reaches Playing (pose loop) and on every new token.
    void OnOpen(uint64_t token) { if (token != seen_) { seen_ = token; hn_ = 0; if (phase_ == Phase::Idle) staff_.Open(); } }
    void OnNote(uint64_t token, int8_t note, Host& h) {
        if (token != seen_) { seen_ = token; hn_ = 0; staff_.Open(); }
        if (phase_ != Phase::Idle || note < 0 || note > 4) return;
        staff_.Push(note);
        if (hn_ == 8) { for (int i = 1; i < 8; ++i) hist_[i - 1] = hist_[i]; --hn_; }
        hist_[hn_++] = note;
        int8_t hit = -1;
        for (int8_t s = 0; s <= int8_t(cfg_.extra_count) && hit < 0; ++s) if (On(s) && Match(s)) hit = s;
        if (hit < 0) return;
        hn_ = 0; cur_ = hit;
        const char* why = "";
        if (!h.SongAllowed(Id(), why)) { h.SongLog(why && *why ? why : Line("blocked"), 1); return; }
        token_ = token; next_ = 0; audio_ = false; To(Phase::Lead); h.SongLog(Line("matched"), 0);
    }
    // OcarinaSongPlayback (18-oct): while the song is being answered (lead, replay, tail, chime in hand) the player
    // may cut it (toggle or stow) and damage cuts it; Cancel stops the recording and leaves no effect behind.
    bool CancelOpen() const {
        return cfg_.cancelable && (phase_ == Phase::Lead || phase_ == Phase::Replay || phase_ == Phase::Tail || phase_ == Phase::JingleHand);
    }
    bool Cancel(Host& h, const char* why) {
        if (phase_ == Phase::Idle || phase_ == Phase::Stowing || phase_ == Phase::Effect) return false;
        if (audio_) TryStop(h, token_, 0);
        audio_ = false; stow_ = false; staff_.Close();
        char text[64]; std::snprintf(text, sizeof(text), "cancelled (%s)", why && *why ? why : "player");
        h.SongLog(Line(text), 2);
        To(Phase::Idle);
        return true;
    }
    void OnEnded(uint64_t token, int reason_stowed, Host& h) {
        staff_.Close();
        if (phase_ == Phase::Idle || phase_ == Phase::Effect || token != token_) return;
        stow_ = false;
        if (phase_ != Phase::Stowing || !reason_stowed) {
            h.SongLog(Line(phase_ == Phase::Stowing ? "cancelled (session)" : "cancelled (replay cut)"), 2);
            To(Phase::Idle); return;
        }
        To(Phase::Effect); h.SongLog(Line("stowed"), 0);
    }
    void Tick(Host& h, bool native, double step = 1.0) {
        staff_.Step(step);
        if (phase_ == Phase::Idle) return;
        t_ += step;
        const Melody& mel = MelodyOf(cur_);
        switch (phase_) {
        case Phase::Lead:
            if (game_clock::Reached(t_, kLeadFrames)) {
                staff_.BeginReplay(mel.notes, mel.length);
                To(Phase::Replay);
                audio_ = AudioFrames() && TryAudio(h, token_, Id(), 0);
                h.SongLog(Line(audio_ ? "replay audio" : "replay"), 0);
            } else return;
            [[fallthrough]];
        case Phase::Replay: {
            const Rhythm& rhy = ActiveRhythm();
            while (next_ < mel.length && next_ < rhy.length && game_clock::Reached(t_, rhy.onset[next_])) {
                if (!audio_) h.ReplayNote(token_, mel.notes[next_]);
                staff_.Light(next_); ++next_;
            }
            if (audio_) {   // the recording sets the end: the chime kAudioJingleGap after it
                if ((next_ >= mel.length || (next_ && next_ >= rhy.length)) && game_clock::Reached(t_, AudioFrames())) {
                    To(Phase::Tail); t_ = kJingleAfterLast - kAudioJingleGap;
                }
                return;
            }
            // A rhythm shorter than its melody (never in the table) ends the replay at its last onset.
            if (next_ >= mel.length || (next_ && next_ >= rhy.length)) { const double last = t_ - rhy.onset[next_ - 1]; To(Phase::Tail); t_ = last; }
            return;
        }
        case Phase::Tail:
            if (game_clock::Reached(t_, kJingleAfterLast)) { h.PlayJingle(); staff_.BeginJingle(); To(Phase::JingleHand); h.SongLog(Line("jingle"), 0); }
            return;
        case Phase::JingleHand:
            if (game_clock::Reached(t_, kJingleHoldFrames - kFadeLead)) staff_.BeginFade();
            if (game_clock::Reached(t_, kJingleHoldFrames)) { stow_ = true; To(Phase::Stowing); }
            return;
        case Phase::Stowing:
            if (game_clock::Reached(t_, kStowWatchFrames)) { stow_ = false; To(Phase::Idle); h.SongLog(Line("cancelled (stow timeout)"), 3); }
            return;
        case Phase::Effect: {
            if (!game_clock::Reached(t_, kEffectDelayFrames)) return;
            const char* why = "";
            if (!native && h.SongAllowed(Id(), why)) {
                const bool ok = h.RunEffect(Id());
                To(Phase::Idle); h.SongLog(Line(ok ? "effect" : "effect failed"), ok ? 0 : 4);
                return;
            }
            if (!native) Tally(why);
            if (game_clock::Reached(t_, kEffectDelayFrames + kEffectWaitFrames)) {
                const char* line = native ? Line("gave up (native frame)") : GaveUp();
                To(Phase::Idle); h.SongLog(line, 5);
            }
            return;
        }
        default: return;
        }
    }
};

// ---------------------------------------------------------------- native painter
// Game thread only, once per tick from the ocarina dispatch (the same frame the session ticks).
// Native contract (the runtime implements it over the live image, base + RVA, SEH around calls):
//   uint32_t base;  bool Read(uint32_t addr, void*, size_t);  bool Write(uint32_t addr, const void*, size_t);
//   bool TexLoad(void* block);          // cdecl base+0x1F46(block, layout::tex_owner), returns != 0
//   void TexRelease();                  // cdecl base+0xBF50(layout::tex_owner)
//   void Set(const void* table);        // thiscall base+0x4449 on base+idsys(table, 0xFF, class, 0x13, 5, 0)
//   void Kill();                        // thiscall base+0xCE9B on base+idsys(0xFF, class)
//   uint32_t UnitPtr(uint8_t mark);     // thiscall base+0x95F7 on base+idsys(mark, class)
// Units are created hidden and shown on the next tick, after one native IdSys.move has built their
// matrices. A room init (IdSys.roomInit / IdTexRoomInit) drops both; the painter sees it and re-arms.
enum class PaintWhy { Idle, Shown, Armed, NoBlock, TexTaken, TexFailed, ClassTaken, SetFailed, Lost, Unreadable };
inline const char* PaintWhyName(PaintWhy w) {
    switch (w) {
    case PaintWhy::Idle: return "idle"; case PaintWhy::Shown: return "shown"; case PaintWhy::Armed: return "armed";
    case PaintWhy::NoBlock: return "no_block"; case PaintWhy::TexTaken: return "tex_taken"; case PaintWhy::TexFailed: return "tex_failed";
    case PaintWhy::ClassTaken: return "class_taken"; case PaintWhy::SetFailed: return "set_failed"; case PaintWhy::Lost: return "lost";
    case PaintWhy::Unreadable: return "unreadable";
    }
    return "?";
}
template <class Native> class Painter {
    uint8_t table_[kTableSize] = {}; void* block_ = nullptr; Geometry g_{};
    uint32_t unit_[kUnits] = {}; bool live_ = false, armed_ = false, tex_ = false, failed_ = false;
    bool TexOwner(Native& n, uint32_t& owner) {
        uint32_t sys = 0;
        if (!n.Read(n.base + layout::tex_sys_ptr_rva, &sys, 4) || !sys) return false;
        return n.Read(sys + layout::tex_id * layout::tex_wk_size + layout::tex_wk_owner, &owner, 4);
    }
    bool ClassBit(Native& n, bool& on) {
        uint32_t w = 0;
        if (!n.Read(n.base + layout::idsys_rva + layout::idsys_set_bits + (layout::id_class >> 5) * 4, &w, 4)) return false;
        on = (w & (0x80000000u >> (layout::id_class & 31))) != 0; return true;
    }
    bool Ours(Native& n, uint32_t u, uint8_t mark) {
        uint8_t h[4] = {};
        return u && u != n.base + layout::unit_dummy_rva && n.Read(u, h, 4) && h[0] != layout::be_free &&
               h[2] == layout::id_class && h[3] == mark;
    }
    void Drop(Native& n) {
        if (live_) n.Kill();
        live_ = armed_ = false; std::memset(unit_, 0, sizeof(unit_));
        if (tex_) { n.TexRelease(); tex_ = false; }
    }
    // The crop comes from the backbuffer size, read every tick: two ints that change only on device
    // create/reset, so a resolution change refits either way. Post element 5 itself is per-pass scratch
    // of the render thread (reviews wf_04be90fc, wf_d84e108c): a pass with the correction off leaves ~1.0
    // (4.5% of live samples at 16:9) and the case/map sub-screens leave larger values, so it is only the
    // fallback when the size is unreadable: then the largest plausible value since arming is kept. A refit
    // just moves the units (positions and sizes are written every tick).
    bool ReadCrop(Native& n) {
        int32_t w = 0, h = 0;
        if (n.Read(n.base + layout::screen_w_rva, &w, 4) && n.Read(n.base + layout::screen_h_rva, &h, 4)) {
            const float c = CropFromScreen(w, h);
            if (PlausiblePostY(c)) {
                const bool changed = !screen_ || c != post_y_;
                post_y_ = c; screen_ = true; screen_w_ = w; screen_h_ = h; return changed;
            }
        }
        float v = 0;
        if (screen_ || !n.Read(n.base + layout::post_rva + layout::post_y, &v, 4) || !PlausiblePostY(v) || v <= post_y_) return false;
        post_y_ = v; return true;
    }
    void Refit() { g_ = Fit(VisibleHalfHeight(post_y_ > 0 ? post_y_ : kDefaultPostY)); }
    float post_y_ = 0; bool screen_ = false; int32_t screen_w_ = 0, screen_h_ = 0;
public:
    explicit Painter(void* block = nullptr) : block_(block), g_(Fit(VisibleHalfHeight(kDefaultPostY))) { BuildTable(table_, g_); }
    void SetBlock(void* block) { block_ = block; }
    bool Live() const { return live_; }
    const uint8_t* Table() const { return table_; }
    const Geometry& geometry() const { return g_; }
    float post_y() const { return post_y_; }   // crop in use, 0 = none read (16:9 used)
    bool from_screen() const { return screen_; }   // the crop came from the backbuffer size
    int32_t screen_w() const { return screen_w_; }
    int32_t screen_h() const { return screen_h_; }
    // show = staff mode != Hidden && gameplay frame. Returns what happened this tick (log on change).
    PaintWhy Tick(Native& n, const Staff& s, bool show) {
        if (!show || s.mode() == Mode::Hidden) { Drop(n); failed_ = false; return PaintWhy::Idle; }
        if (failed_) return PaintWhy::Idle;  // one failure per showing; the next session retries
        if (!block_) { failed_ = true; return PaintWhy::NoBlock; }
        uint32_t owner = 0;
        if (!TexOwner(n, owner)) return PaintWhy::Unreadable;
        if (tex_ && owner != layout::tex_owner) { tex_ = false; live_ = false; }   // room init cleared it
        if (!tex_) {
            if (owner && owner != layout::tex_owner) { failed_ = true; return PaintWhy::TexTaken; }
            if (!owner && !n.TexLoad(block_)) { failed_ = true; return PaintWhy::TexFailed; }
            if (!TexOwner(n, owner) || owner != layout::tex_owner) { failed_ = true; return PaintWhy::TexFailed; }
            tex_ = true;
        }
        if (live_) {
            for (int i = 0; i < kUnits; ++i) if (!Ours(n, unit_[i], uint8_t(i))) { live_ = armed_ = false; return PaintWhy::Lost; }
        }
        if (!live_) {
            bool taken = false;
            if (!ClassBit(n, taken)) return PaintWhy::Unreadable;
            if (taken) {  // our own class left by a crash of the previous showing is ours to clear
                for (int i = 0; i < kUnits && taken; ++i) if (Ours(n, n.UnitPtr(uint8_t(i)), uint8_t(i))) taken = false;
                if (taken) { failed_ = true; return PaintWhy::ClassTaken; }
                n.Kill();
            }
            post_y_ = 0; screen_ = false; ReadCrop(n); Refit(); BuildTable(table_, g_);
            n.Set(table_);
            live_ = true;
            for (int i = 0; i < kUnits; ++i) {
                unit_[i] = n.UnitPtr(uint8_t(i));
                if (!Ours(n, unit_[i], uint8_t(i))) { Drop(n); failed_ = true; return PaintWhy::SetFailed; }
                uint8_t be = 0; n.Read(unit_[i] + layout::u_be_flag, &be, 1);
                be = uint8_t(be & ~layout::be_visible); n.Write(unit_[i] + layout::u_be_flag, &be, 1);
            }
            armed_ = true;
            return PaintWhy::Armed;
        }
        if (ReadCrop(n)) Refit();
        for (int i = 0; i < kUnits; ++i) {
            const UnitView v = s.View(i, g_);
            const uint32_t u = unit_[i];
            uint8_t be = 0;
            if (!n.Read(u + layout::u_be_flag, &be, 1)) return PaintWhy::Unreadable;
            if (v.visible) {
                const float pos[3] = {v.x, v.y, 0.0f};
                n.Write(u + layout::u_tex_no, &v.tex_no, 1);
                n.Write(u + layout::u_pos0, pos, sizeof(pos));
                n.Write(u + layout::u_size_w, &v.w, 4);
                n.Write(u + layout::u_size_h, &v.h, 4);
                n.Write(u + layout::u_col0, v.rgba, 4);
                be = uint8_t(be | layout::be_visible);
            } else {
                be = uint8_t(be & ~layout::be_visible);
            }
            n.Write(u + layout::u_be_flag, &be, 1);
        }
        armed_ = false;
        return PaintWhy::Shown;
    }
    // Session end, unload or Enabled=0: free the units and the texture.
    void Reset(Native& n) { Drop(n); failed_ = false; }
};
}  // namespace link_runtime::ocarina::staff

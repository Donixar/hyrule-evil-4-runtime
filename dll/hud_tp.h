// dll/hud_tp.h
#pragma once
// Twilight Princess HUD for Link (Donald, 30-oct-2026): TP hearts at the top left in rows of 10 and a green
// magic-meter-style gauge under them for the shield's durability; Leon's life ring is hidden, the ammo digits and
// Ashley's (Zelda's) meter stay. Design, art and install: docs/hud-tp-2026-10-30.md.
// Pure logic and native layout: no hooks, no process access; link_runtime.cpp wires it ([Hud], HudFrame).
//
// Drawing is the ocarina staff's path (ocarina_staff.h): native IDSystem units in the cockpit OT (otType 0x13), so
// the game hides them with the rest of the HUD (events, case, DPF_COCKPIT). One id texture of kFrames frames
// (5 heart fills + 5 meter pieces) registered by the native IdTexDataLoad from Bin32/link_hud_tp.eff
// (scripts/hud_tp.py), whose TPL points at DDS appended to the core HUD pack 07000000 after the staff's.
//
// Every IdSys address is the staff's (same functions, same pins: native_pins.h kOcarinaStaff); ours are the class,
// the tex id and the texture owner. Life: GLOBAL_WK (pG, slot RVA 0x806F3C) +0x4FB4 pl_life, +0x4FB6 pl_life_max
// (int16), pl_type +0x4FC8 (link_runtime_health.h ReadHealth). Danger = cPlayer::getLifeLevel() 2: pl_life <= 1200/3
// for Leon (decomp src/game/pl_class.cpp:1027-1065). Yellow herb: max += 60, cap 2400 (item.cpp:1991-2031).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ocarina_staff.h"

namespace link_runtime::hud_tp {
namespace staff = link_runtime::ocarina::staff;
namespace layout {
using namespace staff::layout;   // IdSys / IdTex RVAs, ID_UNIT offsets, be_flag bits, post matrix, backbuffer size
// Ours. Class 0x3D: no set/kill/unitPtr call in the EXE uses an immediate class in 0x36..0x3F (the staff's census);
// the mod already uses 0x3B (Z-target reticle, ztarget_reticle.h) and 0x3C (staff). Tex id 0xEE: absent from the
// cockpit block (core 0x1D) and from every uncompressed SS/ and Etc/ id texture block (hud_tp.py census: 129
// version-0xB blocks in 790 files; free ids 0x5B 0x82 0x99 0x9A 0xED 0xEE; 0x9A = reticle, 0xED = staff), not in
// IdTexDataLoad's no-check list (1 4 10 24 30 31 70 128 239) nor in set()'s PC-glyph remaps (0x14 0x30 0x31 0x49 0x89
// 0x92 0x96 0xA5 0xB4..0xB6 0xBC 0xC1 0xF1 0xF2). Owner 0xCA2 (staff 0xCA0, reticle 0xCA1; native owners 0 4 6 7 9
// 0xA 0x200). The census skips the 623 compressed .lfs files (rooms, movies, memcard): a clash shows as tex_taken.
constexpr uint8_t id_class = 0x3D, tex_id = 0xEE;
constexpr uint32_t tex_owner = 0xCA2;
// ID_UNIT header (decomp include/id_sys.h; UHD shifts only from +0x08): be_flag +0x00, unitNo +0x01, classNo +0x02,
// markNo +0x03. classNo and markNo are pinned by unitPtr's body (kOcarinaStaff 0x243580: cmp [eax+3],bl / cmp [eax+2],cl).
constexpr uint32_t u_unit_no = 0x01;
// Leon's life meter: LifeMeter::roomInit sets core 0x1B (work/core/core/core_027.UWF, 55 units) as class
// IDC_LIFE_METER 0x21 (cockpit.cpp:113). BulletInfo::move (cockpit.cpp:536-611) writes the ammo digits of the SAME
// table (marks 0x0B 0x0A 0x17, empty 0x3F, icon parent 0x30), so the class is never killed or dispSw'd: only three
// depth-1 groups lose their visible bit (0x08). IDSystem::draw queues roots and unitTrans recurses only into visible
// children of a type-1 group (id_sys.cpp:1004-1052), so a hidden group hides its subtree:
//   unitNo 0x07 (no mark): the ring art (0x0F, 143x90), the life needle (mark 0xFE) and the name plates (0x40..0x45);
//   unitNo 0x03 (mark 0x12): the three life arcs (marks 7 8 9, re-shown every frame by LifeMeter::move);
//   unitNo 0x28 (no mark): the caution / danger glows (marks 0x13 0x14).
// unitNo 0x02 (no mark, tex 0x4A 92x92, 0x191919A5) is a dark disc under the ring: hidden only with HideNativeLifeDisc=1
// (unverified which ring it backs). Ashley's groups 0x01 / 0x04 (marks 1 3), the ammo digits 0x19 0x05 0x06 and the
// ammo ring 0x1B stay. Nothing native reads or writes the bit of 0x07 / 0x28 (no mark) or of mark 0x12.
constexpr uint8_t life_class = 0x21;
constexpr uint8_t ring_units[3] = {0x07, 0x03, 0x28};
constexpr uint8_t ring_disc = 0x02;
}  // namespace layout

using Pin = native_pins::Pin;
inline constexpr const auto& kPins = native_pins::kOcarinaStaff;   // the same IdSys / IdTex functions

// ---------------------------------------------------------------- configuration ([Hud] in link_runtime.ini)
constexpr unsigned kDefaultHeartLife = 120, kMinHeartLife = 40, kMaxHeartLife = 600;
constexpr int kMaxHearts = 20, kRow = 10;   // TP: 20 hearts, rows of 10
constexpr unsigned kDefaultRefillVolume = 60;
// TP d_camera.cpp defaultTriming: 35 of 448 lines (7.8 %) per bar; 8 % of the visible HUD height (28.8 of 360 rows at
// 16:9, 72 px at 1600x900). The bottom-bar clip: the left edge of the native ring at 16:9 (ocarina_staff.h: x ~172).
constexpr unsigned kDefaultLetterboxHeight = 8, kMinLetterboxHeight = 4, kMaxLetterboxHeight = 10;
constexpr unsigned kDefaultLetterboxClipX = 168, kMinTopRows = 4, kMaxTopRows = 40;
struct Config {
    bool hearts = false;        // TpHearts=1
    bool meter = false;         // TpShieldMeter=1 (drawn only while the shield feature is active)
    bool hide_native = false;   // HideNativeLife=1 (only while our hearts are shown)
    bool hide_disc = false;     // HideNativeLifeDisc=1 (unitNo 0x02 too; unverified)
    unsigned heart_life = kDefaultHeartLife;   // HeartLife: life per heart (Leon starts at 1200 = 10 hearts)
    // heart-refill-01 (3.5.0 F-01): TP's quarter-by-quarter refill of the DRAWN hearts (the life itself is native).
    bool refill = false;            // HeartRefill=1 (only with TpHearts=1)
    bool refill_sound = false;      // HeartRefillSound=1: Z2SE_HP_GAUGE_INC per whole heart (Bin32/link_heart_refill.wav)
    bool refill_sound_end = false;  // HeartRefillSoundEnd=1: plan B2, one tick at the end of each refill instead
    unsigned refill_volume = kDefaultRefillVolume;   // HeartRefillVolumePercent 0..100
    // ztarget-letterbox-01 (3.5.0 F-09): TP's cinema bars while Z-locked (or neutral Z), two fixed units of the painter.
    bool letterbox = false;               // ZTargetLetterbox=1 (only with TpHearts=1: the painter of the hearts draws them)
    unsigned letterbox_height = kDefaultLetterboxHeight;   // ZTargetLetterboxHeight: percent of the visible height, 4..10
    bool letterbox_start = false;         // LetterboxOrder=start: bars first in the table (A/B of the draw order)
    bool letterbox_instant = false;       // LetterboxInstant=1: plan B1, no easing
    bool letterbox_no_bottom = false;     // LetterboxNoBottom=1: plan B2, the top bar only
    bool letterbox_clip = false;          // LetterboxClipBottom=1: the bottom bar ends at LetterboxClipX (ammo ring)
    unsigned letterbox_clip_x = kDefaultLetterboxClipX;   // LetterboxClipX 0..320, HUD units right of the centre
    unsigned letterbox_top_rows = 0;      // LetterboxTopRows 4..40 HUD rows for the top bar (0: same as the bottom)
    unsigned problems = 0;
    bool Any() const { return hearts || meter; }
};
// Exact 1 enables; HeartLife outside 40..600 (or not a multiple of 4: quarters must be whole) keeps 120.
template <class ReadInt> Config ReadConfig(ReadInt read) {
    Config c;
    const auto on = [&](const wchar_t* key) {
        const unsigned v = read(key, 0u);
        if (v > 1) ++c.problems;
        return v == 1;
    };
    c.hearts = on(L"TpHearts"); c.meter = on(L"TpShieldMeter"); c.hide_native = on(L"HideNativeLife");
    c.hide_disc = on(L"HideNativeLifeDisc");
    const unsigned life = read(L"HeartLife", kDefaultHeartLife);
    if (life < kMinHeartLife || life > kMaxHeartLife || life % 4) ++c.problems;
    else c.heart_life = life;
    // The refill draws the hearts: without TpHearts=1 its keys are read (for their problems) but stay off.
    c.refill = on(L"HeartRefill") && c.hearts;
    c.refill_sound = on(L"HeartRefillSound") && c.refill;
    c.refill_sound_end = on(L"HeartRefillSoundEnd") && c.refill_sound;
    const unsigned volume = read(L"HeartRefillVolumePercent", kDefaultRefillVolume);
    if (volume > 100) ++c.problems;
    else c.refill_volume = volume;
    // Letterbox: same rule (the bars are units of the hearts' painter). LetterboxOrder is a word (ParseOrder).
    c.letterbox = on(L"ZTargetLetterbox") && c.hearts;
    const unsigned height = read(L"ZTargetLetterboxHeight", kDefaultLetterboxHeight);
    if (height < kMinLetterboxHeight || height > kMaxLetterboxHeight) ++c.problems;
    else c.letterbox_height = height;
    c.letterbox_instant = on(L"LetterboxInstant"); c.letterbox_no_bottom = on(L"LetterboxNoBottom");
    c.letterbox_clip = on(L"LetterboxClipBottom");
    const unsigned clip_x = read(L"LetterboxClipX", kDefaultLetterboxClipX);
    if (clip_x > 320) ++c.problems;
    else c.letterbox_clip_x = clip_x;
    const unsigned top = read(L"LetterboxTopRows", 0u);
    if (top && (top < kMinTopRows || top > kMaxTopRows)) ++c.problems;
    else c.letterbox_top_rows = top;
    return c;
}
// LetterboxOrder=end|start (missing or empty: end). Returns false for any other word (a problem; end kept).
inline bool ParseOrder(const wchar_t* s, bool& start) {
    start = false;
    if (!s || !*s) return true;
    auto same = [](const wchar_t* a, const char* b) {
        for (; *a && *b; ++a, ++b) {
            const wchar_t c = (*a >= L'A' && *a <= L'Z') ? wchar_t(*a + 32) : *a;
            if (c != wchar_t(*b)) return false;
        }
        return !*a && !*b;
    };
    if (same(s, "start")) { start = true; return true; }
    return same(s, "end");
}

// ---------------------------------------------------------------- hearts from life
// One heart = heart_life; containers = floor(max / heart_life) (Leon 1200 -> 10; a yellow herb adds 60, so every second
// herb adds a heart, up to 20), at least 1. Fill in quarters, rounded UP (any life > 0 shows at least a quarter) and
// capped at the containers (an odd max such as 1260 shows 10 full hearts until life drops under 1200).
inline int HeartCount(int max_hp, unsigned heart_life) {
    if (max_hp <= 0 || !heart_life) return 0;
    const int n = max_hp / int(heart_life);
    return n < 1 ? 1 : n > kMaxHearts ? kMaxHearts : n;
}
inline int Quarters(int hp, int hearts, unsigned heart_life) {
    if (hp <= 0 || hearts <= 0 || heart_life < 4) return 0;
    const int q = int((int64_t(hp) * 4 + heart_life - 1) / heart_life);
    return q > hearts * 4 ? hearts * 4 : q;
}
// Frame of heart i: 0 empty, 1 quarter, 2 half, 3 three quarters, 4 full.
inline uint8_t HeartFrame(int i, int quarters) {
    const int f = quarters - 4 * i;
    return uint8_t(f < 0 ? 0 : f > 4 ? 4 : f);
}
// The last heart with life beats (TP's big heart); -1 without life.
inline int BeatHeart(int quarters) { return quarters > 0 ? (quarters - 1) / 4 : -1; }
constexpr int kDangerLife = 1200 / 3;   // getLifeLevel 2 for Leon (pl_type 0, max 1200)
inline bool Danger(int hp) { return hp > 0 && hp <= kDangerLife; }
// Heartbeat: a quick swell to kBeatScale in the first kBeatRise of each period, then rest. Periods in 60 Hz frames.
constexpr double kBeatPeriod = 60, kDangerPeriod = 30, kBeatRise = 0.35, kBeatScale = 1.15;
inline double BeatScale(double t, bool danger) {
    const double period = danger ? kDangerPeriod : kBeatPeriod;
    if (!std::isfinite(t) || t < 0) return 1.0;
    const double ph = std::fmod(t, period) / period;
    if (ph >= kBeatRise) return 1.0;
    return 1.0 + (kBeatScale - 1.0) * std::sin(3.14159265358979323846 * ph / kBeatRise);
}

// ---------------------------------------------------------------- shield meter (shield_guard.h Durability)
struct MeterState { bool on = false; unsigned left = 0, max = 0; bool broken = false; double remaining = 0, regen = 0; };
// Fill 0..1: Left/Max while whole; while broken it refills over the regeneration (1 - Remaining/RegenFrames), dim.
inline float MeterFill(const MeterState& m) {
    if (!m.on || !m.max) return 0;
    if (m.broken) {
        if (!(m.regen > 0) || !std::isfinite(m.remaining)) return 0;
        const double f = 1.0 - m.remaining / m.regen;
        return float(f < 0 ? 0 : f > 1 ? 1 : f);
    }
    return float(m.left >= m.max ? 1.0 : double(m.left) / double(m.max));
}

// ---------------------------------------------------------------- texture frames (scripts/hud_tp.py FRAMES)
// 0..4 heart: empty, 1/4, 1/2, 3/4, full (128x128); 5 meter rim (512x64, transparent inside); 6 end ornament (256x64);
// 7 fill, live green (64x32); 8 fill, dim green while regenerating; 9 empty backing (64x32, dark translucent).
// hud-tp-02: our own HD art, uncompressed A8R8G8B8 with mipmaps; sizes on screen come from Geometry, not the texels.
constexpr uint8_t kFrameHeart0 = 0, kFrameRim = 5, kFrameCap = 6, kFrameFill = 7, kFrameFillDim = 8, kFrameBack = 9, kFrames = 10;
constexpr int kMeterUnits = 4;   // rim, fill, back, cap (rim/fill/back never overlap: no OT order assumption)

// ---------------------------------------------------------------- geometry (IdSys units: 640 wide, origin centre, y up)
// UHD keeps the HUD 640 wide and crops its height by the post-projection Y scale (ocarina_staff.h Fit): the top edge
// is +240/crop (180 at 16:9). Hearts hang from that edge at a fixed margin, so every aspect keeps them in the corner.
struct Geometry {
    float x0 = -294.0f, y0 = 156.0f;     // centre of heart 0
    float heart = 20.0f, dx = 19.0f, dy = 18.0f;
    float meter_x = -282.0f, meter_y = 0, meter_w = 120.0f, meter_h = 12.0f, inset = 3.0f;   // rim left edge, centre y
    float cap_w = 36.0f, cap_h = 12.0f;   // hud-tp-02: the ornament spans the frame's height (was 8)
    float half_h = 180.0f;                // the visible half height (ztarget-letterbox-01: the bars hang from it)
};
constexpr float kLeftMargin = 16.0f, kTopMargin = 14.0f, kMeterGap = 5.0f, kCapOverlap = 4.0f;
inline Geometry Fit(float half_h, int rows) {
    Geometry g;
    g.half_h = half_h;
    g.x0 = -320.0f + kLeftMargin + g.heart * 0.5f;
    g.y0 = half_h - kTopMargin - g.heart * 0.5f;
    g.meter_x = -320.0f + kLeftMargin + 22.0f;   // the spiral cap hangs left of the rim, its tail at the rim's end
    const float below = rows > 0 ? g.y0 - (rows - 1) * g.dy - g.heart * 0.5f - kMeterGap : half_h - kTopMargin;
    g.meter_y = below - g.meter_h * 0.5f;
    return g;
}
inline float HeartX(const Geometry& g, int i) { return g.x0 + (i % kRow) * g.dx; }
inline float HeartY(const Geometry& g, int i) { return g.y0 - (i / kRow) * g.dy; }
inline int Rows(int hearts) { return hearts <= 0 ? 0 : (hearts + kRow - 1) / kRow; }

// ---------------------------------------------------------------- heart refill (heart-refill-01, 3.5.0 F-01)
// TP d_meter2.cpp:646-675: mNowLifeGauge climbs one quarter per 30 Hz frame towards the life and plays
// Z2SE_HP_GAUGE_INC whenever it lands on a multiple of 4 (one tick per whole heart). Here: one quarter every
// kRefillFrames 60 Hz frames of game time (133 ms a heart, 10 hearts in 1.33 s). Only the DRAWN value chases: the
// native life changes at once, damage drops the drawing at once, Danger / LowHealth keep reading the real life.
// Frozen out of play (case, menus, events) and animated from where it was when play resumes. Snaps without
// animation or ticks on the first frame, a load (SYS_LOAD_GAME), a new character, a new room and death (hp <= 0,
// and the first living frame after it: the continue). A new heart container (UnitCount changes, the painter
// re-arms) is not a snap: the new heart is born empty and fills with its tick.
// The tick: scripts/heart_refill_sound.py (Z2SE_HP_GAUGE_INC trimmed to 180 ms, 60 ms fade, peak 0.8, mono 16-bit
// 32 kHz), Bin32/link_heart_refill.wav, SHA-pinned; missing or changed mutes the tick only.
inline constexpr const wchar_t* kRefillSoundFile = L"link_heart_refill.wav";
inline constexpr const char* kRefillSoundSha = "0491fc8d9424de95386c988d96a7188705e07e21e3ea20dc552a5dbb02d7033e";
constexpr double kRefillFrames = 2;          // 60 Hz frames per quarter
constexpr unsigned kRefillMaxPending = 4;    // ticks queued for the sound at most (a hitch never plays a long trail)
enum class Snap : uint8_t { None, First, Load, Character, Room, Death };
inline const char* SnapName(Snap s) {
    switch (s) {
    case Snap::First: return "first"; case Snap::Load: return "load"; case Snap::Character: return "character";
    case Snap::Room: return "room"; case Snap::Death: return "death"; default: return "none";
    }
}
struct RefillIn {
    int target = 0;            // Quarters(hp, hearts, heart_life): what the real life shows
    double dt = 1;             // 60 Hz frames of this logic frame (game_clock Step60)
    bool play = true;          // in play: the HUD is up
    uint32_t room = 0;         // global+0x4FAC
    uint8_t character = 0;     // pl_type
    int hp = 0;
    bool load = false;         // SYS_LOAD_GAME seen since the last frame
};
struct RefillDone { int from = 0, to = 0; unsigned ticks = 0; double ms = 0; };
class Refill {
    bool init_ = false, dead_ = false, load_ = false; int shown_ = 0; double acc_ = 0;
    uint32_t room_ = 0; uint8_t character_ = 0;
    bool chasing_ = false; int from_ = 0; unsigned chase_ticks_ = 0; double chase_frames_ = 0;
    unsigned pending_ = 0; bool end_tick_ = false;
    bool done_ = false; RefillDone last_{};
    void Queue() { if (pending_ < kRefillMaxPending) ++pending_; }
    void Finish() {
        chasing_ = false; acc_ = 0;
        last_ = {from_, shown_, chase_ticks_, chase_frames_ * 1000.0 / 60.0}; done_ = true;
        if (end_tick_ && shown_ > from_) Queue();   // plan B2: one tick when the drawing reaches the life
    }
public:
    explicit Refill(bool end_tick = false) : end_tick_(end_tick) {}
    int shown() const { return shown_; }
    bool chasing() const { return chasing_; }
    // One logic frame. Returns the snap reported this frame (None: no snap, or a silent one while dead / loading).
    Snap Update(const RefillIn& in) {
        const int target = in.target < 0 ? 0 : in.target;
        Snap why = Snap::None; bool snap = false;
        if (!init_) { why = Snap::First; snap = true; }
        else if (in.load) { snap = true; if (!load_) why = Snap::Load; }
        else if (in.character != character_) { why = Snap::Character; snap = true; }
        else if (in.room != room_) { why = Snap::Room; snap = true; }
        else if (in.hp <= 0) { snap = true; if (!dead_) why = Snap::Death; }
        else if (dead_) { why = Snap::Death; snap = true; }   // the continue: back to life without a refill
        init_ = true; load_ = in.load; room_ = in.room; character_ = in.character; dead_ = in.hp <= 0;
        if (snap) { shown_ = target; acc_ = 0; chasing_ = false; return why; }
        if (target < shown_) shown_ = target;   // damage: at once, the chase (if any) goes on from there
        if (in.play && shown_ < target) {
            if (!chasing_) { chasing_ = true; from_ = shown_; chase_ticks_ = 0; chase_frames_ = 0; acc_ = 0; }
            const double dt = std::isfinite(in.dt) && in.dt > 0 && in.dt < 10 ? in.dt : 1.0;
            acc_ += dt; chase_frames_ += dt;
            while (acc_ >= kRefillFrames && shown_ < target) {
                acc_ -= kRefillFrames; ++shown_;
                if (shown_ % 4 == 0) { ++chase_ticks_; if (!end_tick_) Queue(); }
            }
        }
        if (chasing_ && shown_ >= target) Finish();
        return Snap::None;
    }
    // Ticks for the sound (game thread -> HudFrame's atomic -> poll thread).
    unsigned TakeTicks() { const unsigned n = pending_; pending_ = 0; return n; }
    bool TakeDone(RefillDone& out) { if (!done_) return false; out = last_; done_ = false; return true; }
};
// Poll thread: plays the queued ticks at most one per kTickGapMs, alternating two voices so a tick never cuts the
// previous one short (no click on a chained refill).
constexpr unsigned kTickGapMs = 60;
struct TickPacer {
    unsigned pending = 0, voice = 0; uint64_t last = 0; bool played = false;
    // Adds the new ticks; returns the voice (0 / 1) to play now, or -1.
    int Poll(unsigned added, uint64_t now_ms) {
        pending += added;
        if (pending > kRefillMaxPending) pending = kRefillMaxPending;
        if (!pending || (played && now_ms - last < kTickGapMs)) return -1;
        --pending; played = true; last = now_ms; voice ^= 1u;
        return int(voice);
    }
};

// ---------------------------------------------------------------- what the HUD shows
struct UnitView { bool visible = false; uint8_t tex_no = 0; float x = 0, y = 0, w = 0, h = 0; uint8_t rgba[4] = {255, 255, 255, 255}; };
struct Inputs {
    bool leon = false;          // life readable and the player is Leon/Link (pl_type 0); Zelda's segment draws nothing
    int hp = 0, max_hp = 0;
    MeterState meter;
    // heart-refill-01: read only with HeartRefill=1 (the defaults keep the 3.4.3 path for every older caller).
    bool play = true; uint32_t room = 0; uint8_t character = 0; bool load = false;
    // ztarget-letterbox-01: Z-locked or neutral Z (ztarget runtime); read only with ZTargetLetterbox=1.
    bool lock = false;
};
// ---------------------------------------------------------------- Z-target letterbox (ztarget-letterbox-01, 3.5.0 F-09)
// TP d_camera.cpp:1549 eases the trim 0.25 per 30 Hz frame: per 60 Hz frame of dt, k = 1 - 0.75^(dt/2) (90 % in
// ~0.27 s, out on the same curve). Two units always armed at the end (or the start, LetterboxOrder=start) of the
// hearts' table while ZTargetLetterbox=1, invisible without a lock, so a lock never changes UnitCount nor re-arms the
// painter. Frame 7 (kFrameFill, 64x32: rows 1..30 opaque) tinted 0x000000FF; the quad is kBarOverscan wider than the
// 640 plane and 32/28 taller than the bar, so only its opaque rows fall on the bar and its soft rows outside the screen
// (outer edge) or as a 1/28 fringe (inner edge). Out of play (case, menus) the bars drop at once.
constexpr int kLetterboxUnits = 2;
constexpr float kBarOverscan = 32.0f, kBarTexelSpan = 32.0f / 28.0f;
constexpr double kBarEase = 0.75, kBarFull = 0.9, kBarClear = 0.1;
enum BarEvent : unsigned { BarNone = 0, BarIn = 1, BarFull = 2, BarOut = 4, BarClear = 8 };
inline double BarStep(double s, bool on, double dt) {
    const double target = on ? 1.0 : 0.0;
    if (!std::isfinite(dt) || dt <= 0 || dt >= 10) dt = 1.0;
    s += (target - s) * (1.0 - std::pow(kBarEase, dt / 2.0));
    if (std::fabs(target - s) < 0.002) s = target;
    return s;
}
class Hud {
    Config cfg_; Inputs in_; double t_ = 0; int hearts_ = 0, quarters_ = 0, target_ = 0;
    Refill refill_; Snap snap_ = Snap::None;
    double bar_ = 0, bar_ms_ = 0; bool bar_on_ = false, bar_full_ = false, bar_clear_ = true;
    unsigned bar_events_ = BarNone; double bar_full_ms_ = 0, bar_clear_ms_ = 0;
    void BarUpdate(const Inputs& in, double dt) {
        if (!cfg_.letterbox) { bar_ = 0; return; }
        const bool on = in.lock && in.play && hearts_ > 0;
        if (!in.play) bar_ = 0;   // the case / menus: no bars, and none left to ease out on return
        if (on != bar_on_) {
            bar_on_ = on; bar_ms_ = 0; bar_full_ = false; bar_clear_ = false;
            bar_events_ |= on ? BarIn : BarOut;
        }
        const double step = std::isfinite(dt) && dt > 0 && dt < 10 ? dt : 1.0;
        bar_ms_ += step * 1000.0 / 60.0;
        bar_ = cfg_.letterbox_instant ? (on ? 1.0 : 0.0) : BarStep(bar_, on, step);
        if (on && !bar_full_ && bar_ >= kBarFull) { bar_full_ = true; bar_events_ |= BarFull; bar_full_ms_ = bar_ms_; }
        if (!on && !bar_clear_ && bar_ <= kBarClear) { bar_clear_ = true; bar_events_ |= BarClear; bar_clear_ms_ = bar_ms_; }
    }
    // Unit index of the first bar and of the first heart, by LetterboxOrder.
    int BarBase() const { return cfg_.letterbox_start ? 0 : hearts_ + (cfg_.meter ? kMeterUnits : 0); }
    int HeartBase() const { return cfg_.letterbox && cfg_.letterbox_start ? kLetterboxUnits : 0; }
public:
    explicit Hud(Config c = {}) : cfg_(c), refill_(c.refill_sound_end) {}
    void Configure(const Config& c) { cfg_ = c; refill_ = Refill(c.refill_sound_end); }
    const Config& config() const { return cfg_; }
    // LetterboxOrder changed live (the bot's A/B): the layout changes, so the painter re-arms (LayoutKey).
    void SetOrder(bool start) { cfg_.letterbox_start = start; }
    unsigned LayoutKey() const { return cfg_.letterbox && cfg_.letterbox_start ? 1u : 0u; }
    void Update(const Inputs& in, double dt) {
        in_ = in;
        if (std::isfinite(dt) && dt > 0 && dt < 10) t_ += dt;
        if (t_ > 1e7) t_ = std::fmod(t_, kBeatPeriod * kDangerPeriod);
        hearts_ = cfg_.hearts && in.leon ? HeartCount(in.max_hp, cfg_.heart_life) : 0;
        target_ = Quarters(in.hp, hearts_, cfg_.heart_life);
        snap_ = Snap::None;
        if (!cfg_.refill) quarters_ = target_;   // HeartRefill=0: the 3.4.3 drawing
        else {
            RefillIn r; r.target = target_; r.dt = dt; r.play = in.play; r.room = in.room; r.character = in.character;
            r.hp = in.hp; r.load = in.load;
            snap_ = refill_.Update(r);
            quarters_ = refill_.shown();
        }
        BarUpdate(in, dt);
    }
    double bar() const { return bar_; }             // 0..1 of the bars' height
    bool bar_on() const { return bar_on_; }
    // Edges since the last call (BarIn / BarFull / BarOut / BarClear) and the ms from the edge to full / clear.
    unsigned TakeBarEvents(double& full_ms, double& clear_ms) {
        const unsigned e = bar_events_; bar_events_ = BarNone; full_ms = bar_full_ms_; clear_ms = bar_clear_ms_; return e;
    }
    // A bar's height in HUD rows (top: LetterboxTopRows when set), at full extension.
    float BarRows(const Geometry& g, bool top) const {
        if (top && cfg_.letterbox_top_rows) return float(cfg_.letterbox_top_rows);
        return float(cfg_.letterbox_height) * 0.01f * 2.0f * g.half_h;
    }
    UnitView BarView(int k, const Geometry& g) const {   // k 0 = top, 1 = bottom
        UnitView v;
        const bool top = k == 0;
        if (!top && cfg_.letterbox_no_bottom) return v;
        const float band = BarRows(g, top) * float(bar_);
        if (band <= 0.25f) return v;
        float left = -320.0f - kBarOverscan, right = 320.0f + kBarOverscan;
        if (!top && cfg_.letterbox_clip) right = float(cfg_.letterbox_clip_x);
        v.visible = right - left > 0.25f; v.tex_no = kFrameFill;
        v.rgba[0] = v.rgba[1] = v.rgba[2] = 0; v.rgba[3] = 255;
        v.w = right - left; v.x = (left + right) * 0.5f;
        v.h = band * kBarTexelSpan;   // the opaque rows (2/32..30/32 of the quad) span exactly the band
        const float c = g.half_h - band * 0.5f;
        v.y = top ? c : -c;
        return v;
    }
    int hearts() const { return hearts_; }
    int quarters() const { return quarters_; }      // drawn (the refill's value with HeartRefill=1)
    int target_quarters() const { return target_; } // the real life's
    Snap snap() const { return snap_; }             // reported by the last Update
    Refill& refill() { return refill_; }
    bool HeartsShown() const { return hearts_ > 0; }
    bool MeterShown() const { return cfg_.meter && in_.leon && in_.meter.on && in_.meter.max > 0; }
    bool Shown() const { return HeartsShown() || MeterShown(); }
    // Units: hearts first (as many as containers), then the meter's four, then the letterbox's two (ZTargetLetterbox=1;
    // first of all with LetterboxOrder=start). Changes when a herb adds a heart, never with a lock.
    int UnitCount() const { return hearts_ + (cfg_.meter ? kMeterUnits : 0) + (cfg_.letterbox ? kLetterboxUnits : 0); }
    UnitView View(int unit, const Geometry& g) const {
        UnitView v;
        if (unit < 0 || unit >= UnitCount()) return v;
        if (cfg_.letterbox) {
            const int b = unit - BarBase();
            if (b >= 0 && b < kLetterboxUnits) return BarView(b, g);
            unit -= HeartBase();
        }
        if (unit < hearts_) {
            v.visible = true; v.tex_no = uint8_t(kFrameHeart0 + HeartFrame(unit, quarters_));
            const double s = unit == BeatHeart(quarters_) ? BeatScale(t_, Danger(in_.hp)) : 1.0;
            v.x = HeartX(g, unit); v.y = HeartY(g, unit); v.w = v.h = float(g.heart * s);
            return v;
        }
        if (!MeterShown()) return v;
        const int k = unit - hearts_;
        const float fill = MeterFill(in_.meter);
        const float ix = g.meter_x + g.inset, iw = g.meter_w - 2 * g.inset, ih = g.meter_h - 2 * g.inset;
        switch (k) {
        case 0:   // rim
            v.visible = true; v.tex_no = kFrameRim; v.x = g.meter_x + g.meter_w * 0.5f; v.y = g.meter_y; v.w = g.meter_w; v.h = g.meter_h;
            return v;
        case 1: {   // fill, from the left
            const float w = iw * fill;
            v.visible = w > 0.25f; v.tex_no = in_.meter.broken ? kFrameFillDim : kFrameFill;
            v.x = ix + w * 0.5f; v.y = g.meter_y; v.w = w; v.h = ih;
            return v;
        }
        case 2: {   // the empty rest, to the right of the fill
            const float w = iw * (1.0f - fill);
            v.visible = w > 0.25f; v.tex_no = kFrameBack;
            v.x = ix + iw - w * 0.5f; v.y = g.meter_y; v.w = w; v.h = ih;
            return v;
        }
        default:   // spiral cap at the left end
            v.visible = true; v.tex_no = kFrameCap; v.x = g.meter_x - g.cap_w * 0.5f + kCapOverlap; v.y = g.meter_y; v.w = g.cap_w; v.h = g.cap_h;
            return v;
        }
    }
};

// ---------------------------------------------------------------- IDSystem::set v2 table (ocarina_staff.h BuildTable)
// ztarget-letterbox-01: + the two bars (20 hearts + meter + bars = 26; without the 2 the bars would be cut at 24).
constexpr int kMaxUnits = kMaxHearts + kMeterUnits + kLetterboxUnits;
constexpr size_t kEntry = staff::kEntry, kTableSize = 8 + kMaxUnits * kEntry;
inline void BuildTable(uint8_t* t, int units, const Geometry& g) {
    std::memset(t, 0, kTableSize);
    if (units < 0) units = 0;
    if (units > kMaxUnits) units = kMaxUnits;
    std::memcpy(t, "2.00", 4);
    t[5] = uint8_t(units);
    for (int i = 0; i < units; ++i) {
        uint8_t* e = t + 8 + i * kEntry;
        e[0x03] = 0;                 // flags: set() ORs alive|move|visible itself
        e[0x04] = uint8_t(i);        // markNo
        e[0x05] = uint8_t(0x40 + i); // unitNo
        e[0x07] = 0xFF;              // no parent
        e[0x0B] = layout::tex_id;
        staff::PutF(e + 0x14, HeartX(g, i < kMaxHearts ? i : 0));
        staff::PutF(e + 0x18, g.y0);
        staff::PutF(e + 0x50, g.heart);
        staff::PutF(e + 0x54, g.heart);
        e[0x58] = e[0x59] = e[0x5A] = 0xFF; e[0x5B] = 0;  // white, transparent until the first view
        e[0x6F] = layout::tex_flag_hold;                   // texNo is ours
    }
}

// ---------------------------------------------------------------- Leon's ring: visible bit of three groups
inline bool IsRingUnit(const uint8_t head[4], bool disc) {
    if (head[0] == layout::be_free || head[2] != layout::life_class) return false;
    for (uint8_t u : layout::ring_units) if (head[1] == u) return true;
    return disc && head[1] == layout::ring_disc;
}
// Walks the IdSys pool (m_maxId +0x00, units +0x50, stride 0x144) on the game thread. hide: clear 0x08 on the ring
// groups; !hide after a hide: set it back (their native state: roomInit creates them visible and nothing native
// touches them). A room init recreates them visible; the next Apply(true) hides them again. Returns units changed.
template <class Native> class RingHide {
    bool hidden_ = false;
public:
    bool hidden() const { return hidden_; }
    int Apply(Native& n, bool hide, bool disc) {
        if (!hide && !hidden_) return 0;
        uint32_t count = 0, units = 0;
        if (!n.Read(n.base + layout::idsys_rva + layout::idsys_max, &count, 4) ||
            !n.Read(n.base + layout::idsys_rva + layout::idsys_units, &units, 4) || !units || count > 0x400) return 0;
        int changed = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t u = units + i * layout::unit_size;
            uint8_t head[4] = {};
            if (!n.Read(u, head, 4) || !IsRingUnit(head, disc)) continue;
            const uint8_t be = hide ? uint8_t(head[0] & ~layout::be_visible) : uint8_t(head[0] | layout::be_visible);
            if (be != head[0] && n.Write(u + layout::u_be_flag, &be, 1)) ++changed;
        }
        hidden_ = hide;
        return changed;
    }
};

// ---------------------------------------------------------------- native painter (ocarina_staff.h Painter, N units)
// Game thread only, once per game frame (link_runtime.cpp HudFrame). Same Native contract as the staff's painter
// with our class, tex id and owner. Units are created hidden and shown on the next tick, after one native IdSys.move
// has built their matrices. A room init (IdSys.roomInit / IdTexRoomInit) drops both; the painter sees it and re-arms.
// A new unit count (a yellow herb added a heart) kills and re-arms.
// A failure (tex taken, IdSys pool full in a busy room...) waits: a new room (Scene) retries at once, and inside one
// room kRetryTicks ticks later, at most kRetriesPerRoom times, so a lasting clash logs a few lines per room, not per frame.
using PaintWhy = staff::PaintWhy;
constexpr unsigned kRetryTicks = 600;      // logic frames: 10 s at 60, 20 s at 30
constexpr unsigned kRetriesPerRoom = 3;
template <class Native> class Painter {
    uint8_t table_[kTableSize] = {}; void* block_ = nullptr; Geometry g_{};
    uint32_t unit_[kMaxUnits] = {}; int units_ = 0; int rows_ = -1; unsigned layout_ = 0;
    bool live_ = false, tex_ = false, failed_ = false;
    bool have_room_ = false; uint32_t room_ = 0; unsigned wait_ = 0, retries_ = 0;
    void Clear() { failed_ = false; wait_ = 0; retries_ = 0; }
    float post_y_ = 0; bool screen_ = false;
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
    void KillUnits(Native& n) { if (live_) n.Kill(); live_ = false; units_ = 0; std::memset(unit_, 0, sizeof(unit_)); }
    void Drop(Native& n) {
        KillUnits(n);
        if (tex_) { n.TexRelease(); tex_ = false; }
    }
    // The crop: backbuffer size first, the post matrix only when it is unreadable (ocarina_staff.h ReadCrop).
    void ReadCrop(Native& n) {
        int32_t w = 0, h = 0;
        if (n.Read(n.base + layout::screen_w_rva, &w, 4) && n.Read(n.base + layout::screen_h_rva, &h, 4)) {
            const float c = staff::CropFromScreen(w, h);
            if (staff::PlausiblePostY(c)) { post_y_ = c; screen_ = true; return; }
        }
        float v = 0;
        if (!screen_ && n.Read(n.base + layout::post_rva + layout::post_y, &v, 4) && staff::PlausiblePostY(v) && v > post_y_) post_y_ = v;
    }
public:
    explicit Painter(void* block = nullptr) : block_(block), g_(Fit(staff::VisibleHalfHeight(staff::kDefaultPostY), 1)) {}
    void SetBlock(void* block) { block_ = block; }
    bool Live() const { return live_; }
    int units() const { return units_; }
    const uint8_t* Table() const { return table_; }
    const Geometry& geometry() const { return g_; }
    float post_y() const { return post_y_; }
    bool from_screen() const { return screen_; }
    bool Failed() const { return failed_; }
    // The room now (global+0x4FAC), before Tick: a change clears a failure and its retry budget.
    void Scene(uint32_t room) {
        if (have_room_ && room != room_) Clear();
        have_room_ = true; room_ = room;
    }
    PaintWhy Tick(Native& n, const Hud& hud, bool show) {
        if (!show || !hud.Shown()) { Drop(n); Clear(); return PaintWhy::Idle; }
        if (failed_) {   // one failure per showing and room, retried after kRetryTicks (bounded)
            if (retries_ >= kRetriesPerRoom || ++wait_ < kRetryTicks) return PaintWhy::Idle;
            failed_ = false; wait_ = 0; ++retries_;
        }
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
        const int want = hud.UnitCount();
        if (live_) {
            for (int i = 0; i < units_; ++i) if (!Ours(n, unit_[i], uint8_t(i))) { live_ = false; units_ = 0; return PaintWhy::Lost; }
            if (want != units_ || hud.LayoutKey() != layout_) KillUnits(n);   // a heart more (or less), a new bar order: re-arm
        }
        if (!live_) {
            bool taken = false;
            if (!ClassBit(n, taken)) return PaintWhy::Unreadable;
            if (taken) {   // our own class left by a crash or a count change is ours to clear
                for (int i = 0; i < kMaxUnits && taken; ++i) if (Ours(n, n.UnitPtr(uint8_t(i)), uint8_t(i))) taken = false;
                if (taken) { failed_ = true; return PaintWhy::ClassTaken; }
                n.Kill();
            }
            post_y_ = 0; screen_ = false; ReadCrop(n);
            rows_ = Rows(hud.hearts());
            g_ = Fit(staff::VisibleHalfHeight(post_y_ > 0 ? post_y_ : staff::kDefaultPostY), rows_);
            BuildTable(table_, want, g_);
            n.Set(table_);
            live_ = true; units_ = want; layout_ = hud.LayoutKey();
            for (int i = 0; i < want; ++i) {
                unit_[i] = n.UnitPtr(uint8_t(i));
                if (!Ours(n, unit_[i], uint8_t(i))) { Drop(n); failed_ = true; return PaintWhy::SetFailed; }   // pool full
                uint8_t be = 0; n.Read(unit_[i] + layout::u_be_flag, &be, 1);
                be = uint8_t(be & ~layout::be_visible); n.Write(unit_[i] + layout::u_be_flag, &be, 1);
            }
            return PaintWhy::Armed;
        }
        ReadCrop(n);
        g_ = Fit(staff::VisibleHalfHeight(post_y_ > 0 ? post_y_ : staff::kDefaultPostY), rows_);
        for (int i = 0; i < units_; ++i) {
            const UnitView v = hud.View(i, g_);
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
        return PaintWhy::Shown;
    }
    void Reset(Native& n) { Drop(n); Clear(); }
};
}  // namespace link_runtime::hud_tp

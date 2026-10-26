// dll/boss_music_duck.h  (docs/dll-music-eventclock-01-2026-10-18.md; pins in native_pins.h kBossMusicDuck,
// wiring in link_runtime.cpp)
#pragma once
// BossMusicDuck=1: while a room's boss stream plays, the room's MIDI/bgmtbl blocks (bgm_state slots 0 and 1, the
// tp-zone-music-01 zone themes) are faded to volume 1 with the game's own SndRoomBgmVolSet, and faded back to
// their default volume when the stream is gone.
//
// Why: SndRoomStrStart (re4-decomp snd.cpp) only requests the stream; it never stops or ducks the blocks, and
// SndEventEnd hands the blocks their volume back after every event. With Zelda themes on both, El Gigante (r119,
// r11e, r224) plays Forest Temple / Goron Mines under Diababa (night review 18-oct, section 2). r10b (Del Lago)
// already silences its blocks with the dellago-music-01 FSE record; this duck must leave that alone.
//
// Where: after every native SndWatcher (MinHook on its entry; game thread, once per sound tick). SndWatcher has
// just refreshed now_play_vol (+2) of every busy slot and cleared the finished ones, so one snapshot of the
// 0x70 bytes at pSnd+0x10 is the whole input.
//
// Rules (pure; the Host does the reads, the clock and the native call):
//  - A boss stream is a busy str_state slot with stop_req 0, play_blk 0 (the room-stream block SndRoomStrStart
//    and SndRoomStrStop use) and play_no in kBossCues: the bio4bgm cues of waves 3, 4, 5, 33 and 36
//    (Morpheel/Del Lago 0x04, Diababa/El Gigante 0x05, Fyrus/Mendez 0x06, Armogohma/Verdugo 0x23,
//    Stallord/Salazar 0x26; build_tp_zone_music_02.py KEEP). Zelda combat cues (tp-zone-music-02) duck only with
//    CombatMusicDuck=1 (below); every other stream never ducks.
//  - While one plays, a BGM slot that is busy, not stopping, and louder than kQuiet is "taken": its play_id is
//    remembered and SndRoomBgmVolSet(slot, 1, kDuckTime) is called. A slot that is already quiet (the r10b FSE
//    record, Mendez's event duck, an event's SndEventInit mute) is never taken, so it is never restored either.
//  - A taken slot that gets loud again with the same play_id (SndEventEnd, a native SndRoomBgmStart restart) is
//    ducked again, at most once per kReduckMs. A different play_id or a free slot drops the claim (new music,
//    room change) without any call.
//  - When no boss stream is left, every taken slot that is still the same busy, non-stopping play_id gets
//    SndRoomBgmVolSet(slot, def_play_vol, kRestoreTime), the default volume SndRoomBgmVolReset would use.
//  - Unreadable pSnd: nothing changes (claims kept, no call).
//
// CombatMusicDuck=1 (combat_music, village r103 review 18-oct): the same rules with a Zelda combat stream as the
// trigger: a live play_blk-0 str slot whose play_no is in kCombatCues (the 20 cues tp-zone-music-02 re-points to
// wave 1, Hidden Village; build_tp_zone_music_02.py COMBAT) or kVillageCombatCue (0x02, the village combat cue that
// already played wave 1; KEEP). Boss and combat share one claim per BGM slot: the blocks are ducked while either
// stream plays and restored once, when neither is left. With the combat switch off (Config default) the Ducker
// never looks at combat cues, so the boss path is the one above, unchanged.
// Times are UHD driver units: 60 per second (SndRoomBgmMute/SndRoomBgmStop multiply seconds by 60 on PC).
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace link_runtime::boss_music {
inline constexpr uint8_t kBossCues[] = {0x04, 0x05, 0x06, 0x23, 0x26};
// build_tp_zone_music_02.COMBAT, in table order (tests/test_boss_music_duck.py cross-checks the script).
inline constexpr uint8_t kCombatCues[] = {0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x13,
                                          0x17, 0x1B, 0x25, 0x27, 0x28, 0x29, 0x2D, 0x2F, 0x30, 0x3B};
constexpr uint8_t kVillageCombatCue = 0x02;   // build_tp_zone_music_02.KEEP: village combat, already wave 1
constexpr int kDuckVolume = 1;         // the volume SndRoomBgmMute, Mendez's event and the r10b FSE record use
constexpr int kDuckTime = 30;          // 0.5 s: the boss theme is already starting
constexpr int kRestoreTime = 120;      // 2 s back to the zone theme
constexpr int kQuiet = 4;              // now_play_vol <= this counts as already ducked (0..127 scale)
constexpr uint64_t kReduckMs = 750;    // > kDuckTime (500 ms), so a fade in progress is never restarted
namespace rva {
constexpr uint32_t kWatcher = 0x570C80;     // SndWatcher body (thunk 0x6AFA, 2 callers), void __cdecl (void)
constexpr uint32_t kBgmVolSet = 0x56C550;   // SndRoomBgmVolSet(u8 blk, int vol, int time) cdecl, al = 1 when sent
constexpr uint32_t kSndSlot = 0xE23A7C;     // VA 0x1223A7C: pSnd
}  // namespace rva
namespace off {
constexpr uint32_t kSlots = 0x10;           // bgm_state[2] then str_state[4], BGM_STAT stride 0x10
constexpr uint32_t kSlotBytes = 0x70;
constexpr uint32_t kStride = 0x10;
constexpr uint32_t kBusy = 0x0, kStopReq = 0x1, kNowVol = 0x2, kDefVol = 0x3, kPlayId = 0x4, kPlayNo = 0x8,
                   kPlayBlk = 0xA;
}  // namespace off

struct Slot {
    uint8_t busy = 0, stop_req = 0;
    int8_t now_vol = 0, def_vol = 0;
    uint32_t play_id = 0;
    int16_t play_no = -1;
    uint16_t play_blk = 0;
    bool Live() const { return busy == 1 && stop_req == 0; }
};
struct Snapshot { Slot bgm[2]; Slot str[4]; };
// Decodes the 0x70 bytes at pSnd+0x10 (BGM_STAT, re4-decomp include/snd.h; UHD offsets proven by kBossMusicDuck).
inline Snapshot Decode(const unsigned char (&raw)[off::kSlotBytes]) {
    Snapshot s;
    for (unsigned i = 0; i < 6; ++i) {
        const unsigned char* p = raw + i * off::kStride;
        Slot& d = i < 2 ? s.bgm[i] : s.str[i - 2];
        d.busy = p[off::kBusy]; d.stop_req = p[off::kStopReq];
        d.now_vol = static_cast<int8_t>(p[off::kNowVol]); d.def_vol = static_cast<int8_t>(p[off::kDefVol]);
        std::memcpy(&d.play_id, p + off::kPlayId, 4); std::memcpy(&d.play_no, p + off::kPlayNo, 2);
        std::memcpy(&d.play_blk, p + off::kPlayBlk, 2);
    }
    return s;
}
inline bool IsBossCue(int16_t no) {
    for (uint8_t c : kBossCues) if (no == c) return true;
    return false;
}
inline bool IsCombatCue(int16_t no) {
    if (no == kVillageCombatCue) return true;
    for (uint8_t c : kCombatCues) if (no == c) return true;
    return false;
}
// Index (0..3) of the first live boss room stream, or -1.
inline int ActiveBoss(const Snapshot& s) {
    for (int i = 0; i < 4; ++i) {
        const Slot& w = s.str[i];
        if (w.Live() && w.play_blk == 0 && IsBossCue(w.play_no)) return i;
    }
    return -1;
}
// Index (0..3) of the first live Zelda combat room stream, or -1.
inline int ActiveCombat(const Snapshot& s) {
    for (int i = 0; i < 4; ++i) {
        const Slot& w = s.str[i];
        if (w.Live() && w.play_blk == 0 && IsCombatCue(w.play_no)) return i;
    }
    return -1;
}

enum class Kind : uint8_t { Duck, Reduck, Restore, Drop, BossOn, BossOff, CombatOn, CombatOff };
inline const char* Name(Kind k) {
    static const char* const names[] = {"duck", "reduck", "restore", "drop", "boss_on", "boss_off", "combat_on",
                                        "combat_off"};
    return names[static_cast<unsigned>(k)];
}
enum class Source : uint8_t { Boss, Combat };   // which stream the event belongs to (log prefix)
struct Config { bool boss = true; bool combat = false; };
struct Event {
    Kind kind = Kind::Duck;
    uint8_t slot = 0;           // BGM slot (Duck/Reduck/Restore/Drop) or str slot (BossOn/CombatOn)
    int16_t cue = -1;           // boss cue while one plays (else the combat cue for Source::Combat)
    Source source = Source::Boss;
    int8_t vol_before = 0;      // now_play_vol seen
    int volume = 0;             // volume asked for
    uint32_t play_id = 0;
    bool sent = false;          // SndRoomBgmVolSet returned 1
};

// Host (game thread):
//   bool Read(Snapshot& out);                        // SEH-guarded pSnd + slot read; false = skip this tick
//   uint64_t NowMs();
//   bool VolSet(unsigned slot, int vol, int time);   // SndRoomBgmVolSet; false on fault or when not sent
//   void Note(const Event& e);                       // copy for the worker log
class Ducker {
public:
    struct Claim { bool taken = false; uint32_t play_id = 0; uint64_t last_ms = 0; Source by = Source::Boss; };
    Ducker() = default;
    explicit Ducker(Config c) : cfg_(c) {}
    void Configure(Config c) { cfg_ = c; }
    const Config& config() const { return cfg_; }
    template <class Host> void Tick(Host& h) {
        Snapshot s;
        if (!h.Read(s)) return;
        ++ticks_;
        const int boss = cfg_.boss ? ActiveBoss(s) : -1;
        const int combat = cfg_.combat ? ActiveCombat(s) : -1;
        const uint64_t now = h.NowMs();
        const int16_t cue = boss >= 0 ? s.str[boss].play_no : int16_t(-1);
        const int16_t ccue = combat >= 0 ? s.str[combat].play_no : int16_t(-1);
        if (cue != cue_) {
            Event e; e.kind = boss >= 0 ? Kind::BossOn : Kind::BossOff; e.cue = boss >= 0 ? cue : cue_;
            e.slot = static_cast<uint8_t>(boss >= 0 ? boss : 0);
            e.play_id = boss >= 0 ? s.str[boss].play_id : 0; h.Note(e);
            cue_ = cue;
        }
        if (ccue != combat_cue_) {
            Event e; e.kind = combat >= 0 ? Kind::CombatOn : Kind::CombatOff; e.source = Source::Combat;
            e.cue = combat >= 0 ? ccue : combat_cue_; e.slot = static_cast<uint8_t>(combat >= 0 ? combat : 0);
            e.play_id = combat >= 0 ? s.str[combat].play_id : 0; h.Note(e);
            combat_cue_ = ccue;
        }
        // Boss first: with both streams live the events stay boss_music, exactly as before.
        const bool active = boss >= 0 || combat >= 0;
        const Source src = boss >= 0 ? Source::Boss : Source::Combat;
        const int16_t ecue = boss >= 0 ? cue : ccue;
        for (unsigned i = 0; i < 2; ++i) {
            const Slot& w = s.bgm[i];
            Claim& c = claims_[i];
            if (c.taken && (!w.busy || w.play_id != c.play_id)) {   // new music or room change: forget, no call
                Event e; e.kind = Kind::Drop; e.slot = static_cast<uint8_t>(i); e.cue = active ? ecue : cue;
                e.source = active ? src : c.by; e.play_id = c.play_id;
                h.Note(e); c = Claim{};
            }
            if (active) {
                if (c.taken) c.by = src;
                if (!w.Live() || w.now_vol <= kQuiet) continue;
                if (c.taken && now - c.last_ms < kReduckMs) continue;
                Event e; e.kind = c.taken ? Kind::Reduck : Kind::Duck; e.slot = static_cast<uint8_t>(i); e.cue = ecue;
                e.source = src; e.vol_before = w.now_vol; e.volume = kDuckVolume; e.play_id = w.play_id;
                e.sent = h.VolSet(i, kDuckVolume, kDuckTime);
                c.taken = true; c.play_id = w.play_id; c.last_ms = now; c.by = src;
                h.Note(e);
            } else if (c.taken) {
                Event e; e.kind = Kind::Restore; e.slot = static_cast<uint8_t>(i); e.source = c.by;
                e.vol_before = w.now_vol; e.volume = w.def_vol; e.play_id = w.play_id;
                if (w.Live() && w.def_vol > 0) e.sent = h.VolSet(i, w.def_vol, kRestoreTime);
                h.Note(e); c = Claim{};
            }
        }
    }
    const Claim& claim(unsigned i) const { return claims_[i]; }
    int16_t cue() const { return cue_; }
    int16_t combat_cue() const { return combat_cue_; }
    uint32_t ticks() const { return ticks_; }
private:
    Config cfg_;
    Claim claims_[2];
    int16_t cue_ = -1;
    int16_t combat_cue_ = -1;
    uint32_t ticks_ = 0;
};
}  // namespace link_runtime::boss_music

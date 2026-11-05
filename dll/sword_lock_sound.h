#pragma once
// Master Sword Blade Lock H3: the duel's TP sounds and sparks (docs/swordlock-h3-2026-11-04.md; design
// docs/idea-ambiciosa-2026-11-03.md, "Animacion y sonido"). Lab DLL only: link_runtime.cpp includes it inside
// #if LINK_SWORD_LOCK_LAB, so the public build never sees it. Pure: no hooks, no process access, no audio device. The
// runtime feeds each duel tick (game thread), publishes Out through atomics and the DLL's poll thread plays it on its
// own waveOut devices; the sparks go through the existing native parry emitter (parry_spark_runtime.h EmitFree).
//
// What TP does in the lock (dusklight d_a_b_gnd.cpp:3340-3375, d_a_alink_demo.inc procSwordPush):
//  * Z2SE_HIT_SWORD once when the blades meet (mDemoCamTimer == 0): here the duel's first tick (the Traba).
//  * Z2SE_EN_GND_TUBAZERI_EFF every frame of the lock (startCreatureExtraSoundLevel): a loop from the Traba to the end.
//  * Link's voice TUBA_A/B/C every frame by the PushValue band (Fan Zelda, round 5: the voice follows the band on each
//    tick, not the clip's 1000 ms lock): a loop of the band's voice, switched on the tick the band changes.
//  * TUBA_WIN / TUBA_LOSE once on the result. A draw or an abandon ends in silence.
//  * The design adds HIT_SWORD on a missed break and on a chainsaw contact while the world is ours (at most one every
//    kContactGapMs of game time). Z2SE_EN_GND_TUBAZERI_PUSH (the hit break) is a bare `finish` in Z2Sound.baa: TP's own
//    PUSH is silent, so a hit plays no sound of its own (no placeholder).
//  * A pause (the menu, 0x806F39) stops the loops; play starts them again from their beginning.
//  * Sparks (the design's 0x8B98/0x8B99 are TP JPA emitters of Ganondorf's scene, not RE4 effects): the native parry
//    sparks (CORE 0x1F/0x20) at the blade contact on the Traba (H1c's), on each hit break and on the win's flash.
// Every time is the duel's game time (game_clock), the same at 60 and 165 FPS.
// Tests: tests/native/test_sword_lock_sound.cpp (scripts/test_sword_lock_sound.bat).
#include "sword_lock_duel.h"
#include <cstdint>
#include <cwchar>

namespace link_runtime::sword_lock::sound {
namespace sd = link_runtime::sword_lock::duel;

enum Loop : uint8_t {kEff = 0, kVoiceA, kVoiceB, kVoiceC, kLoopCount};
enum Shot : uint8_t {kHitSword = 0, kWin, kLose, kWinBig, kShotCount};   // kWinBig: candidate 10, the win after a long duel
inline const char* LoopName(unsigned i) {
    static const char* const a[kLoopCount] = {"eff", "voice_a", "voice_b", "voice_c"};
    return i < kLoopCount ? a[i] : "none";
}
inline const wchar_t* LoopPath(unsigned i) {
    static const wchar_t* const n[kLoopCount] = {L"link_lock_eff.wav", L"link_lock_voice_a.wav", L"link_lock_voice_b.wav", L"link_lock_voice_c.wav"};
    return i < kLoopCount ? n[i] : L"";
}
inline const wchar_t* ShotPath(unsigned i) {
    static const wchar_t* const n[kShotCount] = {L"link_lock_hit_sword.wav", L"link_lock_win.wav", L"link_lock_lose.wav",
                                                 L"link_lock_win_big.wav"};
    return i < kShotCount ? n[i] : L"";
}
// SHA-256 of scripts/sword_lock_sounds.py's output (deterministic: draw 0 of every cue), in Bin32\sword_lock_lab\.
inline const char* LoopSha(unsigned i) {
    static const char* const s[kLoopCount] = {
        "1e8823ce725b0abf381d527408a68b1f5b6be79ba0cf9a15819f00bb5e666a02",   // link_lock_eff.wav (4.167 s loop)
        "8ae5a0c18f05fa4bc02e84f9050773b884a26de6ab9d30614ae3ba7324f54815",   // link_lock_voice_a.wav (2.125 s loop)
        "4e0f4f050639645133fdc6f6a1cec840653c3be83ec96db6cf441262de9b1772",   // link_lock_voice_b.wav
        "df6a39e048c41add663a7b30b98c0e4fe6c7e86c2ae0506f0e2db03d6ffe35d8"};  // link_lock_voice_c.wav
    return i < kLoopCount ? s[i] : "";
}
inline const char* ShotSha(unsigned i) {
    static const char* const s[kShotCount] = {
        "9a31cba667034a196ca1c0659963cb61f03d2018fa622f94f79926accf7040b4",   // link_lock_hit_sword.wav
        "befcfd2b23358e39f101789a1718e17e30b281debcabc6a7d2dddd75baf82fb4",   // link_lock_win.wav
        "cd8810b0af42707eabc95ab56ff98533853e706b2bf85f9d52747919ae565ff6",   // link_lock_lose.wav
        "e7c1bd26634300481330a7857f27cf8410afea0b5de1b2220599e0f255a84041"};  // link_lock_win_big.wav (scripts/sword_lock_salvador_sounds.py)
    return i < kShotCount ? s[i] : "";
}
inline const char* ShotName(unsigned i) {
    static const char* const n[kShotCount] = {"hit_sword", "win", "lose", "win_big"};
    return i < kShotCount ? n[i] : "none";
}
constexpr uint8_t LoopBit(unsigned i) {return static_cast<uint8_t>(1u << i);}
inline unsigned VoiceFor(sd::Band b) {return b == sd::Band::B ? kVoiceB : b == sd::Band::C ? kVoiceC : kVoiceA;}
constexpr double kContactGapMs = 300;   // chainsaw contacts while held: one HIT_SWORD at most this often
// Candidate 10 (Donald, 4-nov: "me interesa que Link al ganar después de un struggle largo grite más fuerte"): a win
// after this much duel game time (past every no-overtime win of his: 5.4-6.9 s) plays the louder shout instead.
constexpr double kLongDuelMs = 9000;

// [Combat] SwordLockSound / SwordLockSparks (lab DLL only): missing = 1, "0" = off, anything else counts a problem.
struct Config {bool sound = true, sparks = true;};
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[16]{};
    const auto one = [&](const wchar_t* key, bool& out) {
        v[0] = 0; get(key, v, 16);
        if (!v[0] || (v[0] == L'1' && !v[1])) out = true;
        else if (v[0] == L'0' && !v[1]) out = false;
        else {out = true; ++problems;}
    };
    one(L"SwordLockSound", c.sound);
    one(L"SwordLockSparks", c.sparks);
    return c;
}

// What the poll thread must play: the loops on now, and a counter per one-shot (each step = one play).
struct Out {
    uint8_t loops = 0;
    uint32_t shots[kShotCount]{};
};
// One duel tick, after Lab::Tick: running = Stage::Duel and the duel running; the band and events of that tick.
struct Facts {
    bool running = false, paused = false;
    sd::Band band = sd::Band::A;
    uint32_t ev = 0;
    sd::Result result = sd::Result::None;
    double game_ms = 0;
};
class Director {
public:
    // The Traba (the duel's Begin frame): the clash and the screech from the first frame.
    void Start() {
        const Out keep = out_;
        *this = Director();
        for (unsigned i = 0; i < kShotCount; ++i) out_.shots[i] = keep.shots[i];   // counters run for the whole session
        on_ = true; Play(kHitSword);
        out_.loops = static_cast<uint8_t>(LoopBit(kEff) | LoopBit(kVoiceA));
    }
    void Tick(const Facts& f) {
        if (!on_) return;
        if (f.ev & sd::kEvMiss) Play(kHitSword);
        if (!f.running) {
            if (f.result == sd::Result::Win) Play(f.game_ms >= kLongDuelMs ? kWinBig : kWin);
            else if (f.result == sd::Result::Lose) Play(kLose);
            Stop();
            return;
        }
        voice_ = VoiceFor(f.band);
        out_.loops = f.paused ? 0 : static_cast<uint8_t>(LoopBit(kEff) | LoopBit(voice_));
    }
    // A chainsaw contact the lab guard took while the world is ours (game ms of the duel).
    void Contact(double game_ms) {
        if (!on_) return;
        if (last_contact_ >= 0 && game_ms >= last_contact_ && game_ms - last_contact_ < kContactGapMs) return;
        last_contact_ = game_ms; Play(kHitSword); ++contacts_;
    }
    // The end of the duel without a result (an abandon, the probe let go): silence.
    void Stop() {on_ = false; out_.loops = 0;}
    const Out& out() const {return out_;}
    bool on() const {return on_;}
    unsigned voice() const {return voice_;}
    unsigned contacts() const {return contacts_;}
private:
    Out out_;
    bool on_ = false;
    unsigned voice_ = kVoiceA, contacts_ = 0;
    double last_contact_ = -1;
    void Play(unsigned s) {++out_.shots[s];}
};

// The sparks: one native burst at the blade contact on the Traba, on each hit break and on the win's flash.
enum class Spark : uint8_t {None, Lock, Hit, Win};
inline const char* SparkName(Spark s) {
    return s == Spark::Lock ? "lock" : s == Spark::Hit ? "hit" : s == Spark::Win ? "win" : "none";
}
inline Spark SparkOnTick(uint32_t ev) {return (ev & sd::kEvHit) ? Spark::Hit : Spark::None;}
inline Spark SparkOnRelease(sd::Result r) {return r == sd::Result::Win ? Spark::Win : Spark::None;}
}  // namespace link_runtime::sword_lock::sound

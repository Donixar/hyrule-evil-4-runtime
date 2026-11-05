#pragma once
// Master Sword Blade Lock against Dark Link (docs/swordlock-darklink-2026-11-04.md; design docs/idea-ambiciosa-2026-11-03.md,
// "Camino al 100%": "3.6.1: Dark Link em39 con destello y voces V_TUBAZERI", and H4: "Dark Link em39 por el productor B
// 0x15585F, con el destello 0x17"). Lab DLL only: link_runtime.cpp includes it inside #if LINK_SWORD_LOCK_LAB. Pure: no
// hooks, no process access.
//
// Dark Link is the mod's mirror of Link over Krauser (em39, layers dark-link-*): his blade swings reach Link through
// em39's attack-sphere producer 0x15585F (function 0x155830, class B, ESI = the em39). The general parry's own Owner()
// knows only the chainsaw and thrown producers (owner 0 for him, and it stays so: the public parry is untouched); the
// lab reads the em39 from the producer's ESI and admits it only complete:
//   * the producer is 0x15585F and the block was a press block with effects (the same gate as the chainsaw's);
//   * a live em39 slot: be_flag & 0x201 == 1, vtable base+0x7127CC, id 0x39 (parry_direct_routes.h kSpecs' Krauser);
//   * HP in range and his routine r0 == 1 (normal moves: not damaged, dying or in a scenario);
//   * not a kick (r1 0x2B high / 0x2C low, parry_direct_runtime.h): a kick is no blade to lock against.
// Then the same probe queue, crowd rule, freeze, lease and abandons as the Salvador's.
//
// The exit on a win: the native flash record 0x17 (ocarina_song_effects.h Fire, the Salvador's), which em39's own DmCk
// takes as a stun (design: setRno(2,1)); when it cannot be published on him (no hit part, a pending record, a failed or
// unreadable call) the native kick record 0x14 (Blow::Charge, his own knock-down reaction), and the log says which.
//
// The voices: TP's opponent strain in the lock, Ganondorf's Z2SE_EN_GND_V_TUBAZERI_A/B/C by the same PushValue band as
// Link's TUBA (d_a_b_gnd.cpp:3404-3420; B under -0.4, C over 0.4, A between: the letter of the band, since TP's value is
// Link's) on every tick, and _WIN when Link loses / _LOSE when Link wins, once, 14 / 12 TP frames into the result as TP
// plays them on those clips' frames. Exported by scripts/sword_lock_sounds.py build-darklink (SHA-pinned below); a
// missing or changed file mutes that sound only. A draw or an abandon ends in silence.
//
// The distances of H2b's clips for a sword opponent: the win clip's cut adds his body (kDarkLinkBodyMm, Krauser is
// Leon x1.03, slimmer than the Salvador) to the clip's reach when SwordLockWinCutMm is its default; the lose carry (TP's
// own knock-back, ~1.3 m) and the 1300 / 600 slides make the same sense against a sword.
// Tests: tests/native/test_sword_lock_darklink.cpp (scripts/test_sword_lock_darklink.bat).
#include "sword_lock_duel.h"
#include "sword_lock_build.h"   // LINK_SWORD_LOCK_GRADUATED: the 3.6.0 defaults
#include <cstdint>
#include <cwchar>

namespace link_runtime::sword_lock::darklink {
namespace sd = link_runtime::sword_lock::duel;

constexpr uint32_t kProducerRet = 0x15585F, kProducerFn = 0x155830;   // em39 attack spheres (parry_general_producers.h)
constexpr uint32_t kVtableRva = 0x7127CC;                             // em39 (parry_direct_routes.h kSpecs)
constexpr uint8_t kId = 0x39;
constexpr uint8_t kKickHigh = 0x2B, kKickLow = 0x2C;

// [Combat] SwordLockDarkLink (lab DLL only): missing = 1, "0" = off, anything else counts a problem and stays on.
#if LINK_SWORD_LOCK_GRADUATED
struct Config {bool on = false;};   // 3.6.0: off unless SwordLockDarkLink=1 (Donald's decision 2: Dark Link waits for 3.6.1)
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[16]{};
    get(L"SwordLockDarkLink", v, 16);
    if (v[0] == L'1' && !v[1]) c.on = true;
    else if (v[0] && !(v[0] == L'0' && !v[1])) ++problems;
    return c;
}
#else
struct Config {bool on = true;};
template <class Get> Config ReadConfig(Get&& get, unsigned& problems) {
    Config c; wchar_t v[16]{};
    get(L"SwordLockDarkLink", v, 16);
    if (!v[0] || (v[0] == L'1' && !v[1])) c.on = true;
    else if (v[0] == L'0' && !v[1]) c.on = false;
    else {c.on = true; ++problems;}
    return c;
}
#endif

// Who the duel's owner is (set when the probe took the request).
enum class Foe : uint8_t {Salvador, DarkLink};
inline const char* FoeName(Foe f) {return f == Foe::DarkLink ? "dark_link" : "salvador";}

// The em39's fields the admission reads (the runtime fills them from two identical reads, mortal_draw_target.h ReadRaw).
struct Owner {
    bool read = false;
    uint32_t ptr = 0, vtable = 0, be_flag = 0;
    uint8_t id = 0, type = 0, r0 = 0, r1 = 0;
    int16_t hp = 0, hp_max = 0;
};
enum class Why : uint8_t {Ok, Off, Producer, NoEffects, NoOwner, Unread, Slot, Vtable, Id, Dead, Routine, Kick};
inline const char* WhyName(Why w) {
    static const char* const n[] = {"ok", "off", "producer", "no_effects", "no_owner", "unread", "slot", "vtable", "id", "dead",
                                    "routine", "kick"};
    return n[unsigned(w)];
}
// A tier-2 block (ret_rva: the candidate's outer caller minus the image base; effects: a press block, not grace).
inline Why Admit(const Config& c, uint32_t ret_rva, bool effects, const Owner& o, uint32_t base) {
    if (!c.on) return Why::Off;
    if (ret_rva != kProducerRet) return Why::Producer;
    if (!effects) return Why::NoEffects;
    if (!o.ptr) return Why::NoOwner;
    if (!o.read) return Why::Unread;
    if ((o.be_flag & 0x201) != 1) return Why::Slot;
    if (o.vtable != base + kVtableRva) return Why::Vtable;
    if (o.id != kId) return Why::Id;
    if (o.hp <= 0 || o.hp_max <= 0 || o.hp > o.hp_max) return Why::Dead;
    if (o.r0 != 1) return Why::Routine;
    if (o.r1 == kKickHigh || o.r1 == kKickLow) return Why::Kick;
    return Why::Ok;
}
// The probe's liveness of the held em39 (every tick, and before the exit blow): the same slot, still an em39, alive and
// not dying; admitted: also the same id, type and max HP as at the admission (the pointer was not reused).
inline bool Alive(const Owner& o, uint32_t base, bool admitted, uint8_t id0, uint8_t type0, int16_t hp_max0) {
    if (!o.read || (o.be_flag & 0x201) != 1 || o.vtable != base + kVtableRva || o.id != kId || o.hp <= 0 || o.hp_max <= 0 ||
        o.hp > o.hp_max || o.r0 == 3) return false;
    return !admitted || (o.id == id0 && o.type == type0 && o.hp_max == hp_max0);
}
// The lab guard: an attack-sphere contact of the held em39 (ESI = the owner).
inline bool HeldContact(uint32_t ret_rva, uint32_t esi, uint32_t owner) {return owner && ret_rva == kProducerRet && esi == owner;}

// ---- the exit blow on a win -----------------------------------------------------------------------------------------
// The shot of ocarina_song_effects.h Fire as a plain value (its order): published and mismatch ran the native call.
enum class Shot : uint8_t {Published, Unreadable, Dead, Pending, NoHitpart, Failed, Mismatch};
// After the flash: try his own knock-down (the kick record) when the flash never reached the native call; a dead owner
// or a call that ran (published, or written over at once: mismatch) takes nothing more.
inline bool ChargeAfter(Shot flash) {
    return flash == Shot::Unreadable || flash == Shot::Pending || flash == Shot::NoHitpart || flash == Shot::Failed;
}

// ---- the opponent's voices ------------------------------------------------------------------------------------------
enum Loop : uint8_t {kVoiceA = 0, kVoiceB, kVoiceC, kLoopCount};
enum ShotSound : uint8_t {kWin = 0, kLose, kShotCount};   // HIS win (Link lost) and HIS lose (Link won)
constexpr uint8_t LoopBit(unsigned i) {return static_cast<uint8_t>(1u << i);}
inline unsigned VoiceFor(sd::Band b) {return b == sd::Band::B ? kVoiceB : b == sd::Band::C ? kVoiceC : kVoiceA;}
inline const wchar_t* LoopPath(unsigned i) {
    static const wchar_t* const n[kLoopCount] = {L"dl_lock_voice_a.wav", L"dl_lock_voice_b.wav", L"dl_lock_voice_c.wav"};
    return i < kLoopCount ? n[i] : L"";
}
inline const wchar_t* ShotPath(unsigned i) {
    static const wchar_t* const n[kShotCount] = {L"dl_lock_win.wav", L"dl_lock_lose.wav"};
    return i < kShotCount ? n[i] : L"";
}
// scripts/sword_lock_sounds.py build-darklink (D:/link-in-re4-work/swordlock-darklink/sounds-darklink.json).
inline const char* LoopSha(unsigned i) {
    static const char* const s[kLoopCount] = {
        "0bbbfa54a1009d48b764a96b392c594bc9d8a1909319df4a77317ea25cbc5719",   // dl_lock_voice_a.wav  Z2SE_EN_GND_V_TUBAZERI_A
        "a3d09a7c667ca5695aa4eed6247bb9c4e9c641e49f15e3a53051f6e158589352",   // dl_lock_voice_b.wav  Z2SE_EN_GND_V_TUBAZERI_B
        "f4bc2d128334b07cebe92f9c3d29f1608a572ecf8a6e1eb6eed3ac94c836f11f"};  // dl_lock_voice_c.wav  Z2SE_EN_GND_V_TUBAZERI_C
    return i < kLoopCount ? s[i] : "";
}
inline const char* ShotSha(unsigned i) {
    static const char* const s[kShotCount] = {
        "1be06469280eb0020f123908b659c7df1aad52ff78f5df416114f59d2136d715",   // dl_lock_win.wav   Z2SE_EN_GND_V_TUBAZERI_WIN
        "b01036e92b55d4e33e855905aef6a576e92dc8aa7a95cee7f25d12c8c0e0968a"};  // dl_lock_lose.wav  Z2SE_EN_GND_V_TUBAZERI_LOSE
    return i < kShotCount ? s[i] : "";
}
constexpr double kWinVoiceMs = 14 * 1000.0 / 30, kLoseVoiceMs = 12 * 1000.0 / 30;   // TP frames 14 / 12 of those clips
struct Out {uint8_t loops = 0; uint32_t shots[kShotCount]{};};
struct Facts {
    bool running = false, paused = false;   // the duel runs (its lock); a pause stops the loops
    sd::Band band = sd::Band::A;
    sd::Result result = sd::Result::None;   // once it is over
    double now_ms = 0;                      // any game-time clock (the runtime's game frames in ms)
};
class Voice {
public:
    void Start() {
        const Out keep = out_;
        *this = Voice();
        for (unsigned i = 0; i < kShotCount; ++i) out_.shots[i] = keep.shots[i];   // counters run for the whole session
        on_ = true; out_.loops = LoopBit(kVoiceA);
    }
    void Tick(const Facts& f) {
        if (due_ >= 0 && f.now_ms >= due_at_) {++out_.shots[unsigned(due_)]; due_ = -1;}
        if (!on_) return;
        if (!f.running) {
            if (f.result == sd::Result::Win) Due(kLose, f.now_ms + kLoseVoiceMs);        // Link won: HIS lose
            else if (f.result == sd::Result::Lose) Due(kWin, f.now_ms + kWinVoiceMs);    // Link lost: HIS win
            on_ = false; out_.loops = 0;
            return;
        }
        voice_ = VoiceFor(f.band);
        out_.loops = f.paused ? 0 : LoopBit(voice_);
    }
    // An abandon (or the release before its last tick): silence, nothing still due.
    void Stop() {on_ = false; out_.loops = 0; due_ = -1;}
    const Out& out() const {return out_;}
    bool on() const {return on_;}
    bool pending() const {return due_ >= 0;}
    unsigned voice() const {return voice_;}
private:
    void Due(unsigned s, double at) {due_ = int(s); due_at_ = at;}
    Out out_;
    bool on_ = false;
    unsigned voice_ = kVoiceA;
    int due_ = -1;
    double due_at_ = 0;
};

// ---- H2b's win cut against a sword --------------------------------------------------------------------------------
constexpr double kWinReachMm = 1740;        // sword_lock_anim.h kWinReachMm (the win clip's furthest guard point)
constexpr double kSalvadorBodyMm = 460;     // sword_lock_anim.h kOwnerBodyMm
constexpr double kDarkLinkBodyMm = 400;     // Krauser's frame (Leon x1.03): ~0.35 m round his centre plus margin
// The cut distance this duel uses: a user-set SwordLockWinCutMm (not its default) is kept as is.
inline double WinCutMm(Foe f, double configured) {
    const double def = kWinReachMm + kSalvadorBodyMm;
    if (f != Foe::DarkLink || configured != def) return configured;
    return kWinReachMm + kDarkLinkBodyMm;
}
}  // namespace link_runtime::sword_lock::darklink

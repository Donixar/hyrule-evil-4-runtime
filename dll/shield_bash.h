#pragma once
// Shield Bash (Golpe de escudo, TP hidden skill; docs/combat-gs-sb-01-2026-10-18.md). Pure: input, cone and
// settings. The scans and the native writes are in shield_bash_scan.h; the runtime glue in spin_attack_runtime.h.
//
// Control (no new physical key): hold the Z-target key (ztarget_runtime.h Held: Left Ctrl by default, or the pad's
// RS) and TAP the usual knife button (KEY_KAMAE_KNIFE 0x800): pressed and let go within kTapFrames game frames
// (250 ms) with no attack (KEY_FIRE) in between. The bash fires on the release. Holding the knife longer is not a
// bash (the spin charge, the knife slash and the Ending Blow keep their inputs), and without the Z key held the
// knife button is native. The tap is still a knife press for the parry (its 450 ms window opens as before).
// One bash per kCooldownFrames (0.75 s).
//
// Effect, as TP's procGuardAttack (d_a_alink_guard.inc): the shield hits what is in front.
//   Ganados   up to ShieldBashTargets (default 3) standing Ganados within ShieldBashReach mm (default 2200) and
//             kHalfConeDeg either side of Link's facing, 1.5 m height band, nearest first, each get one native kick
//             record (weapon 0x14, ocarina_song_effects.h Blow::Charge, the record PlWepHitCheck2 writes): the
//             native kick reaction knocks it back (em10DmSetWep00, Dm_Blow); since spin-fix-01 with a token damage
//             (Claims: the calc of that exact record returns KnockbackDamage, 1 or 0), so the bash never kills.
//             Down, getting up, heavy (> 1500 HP, chainsaw, claw types), dying, pending-record and Merchant are
//             skipped (song_effects Judge).
//   Weapons   for ShieldBashDeflectMs (default 500 ms of game time) after the bash, every enemy weapon in flight
//             (cEmWep routine 1/5 Throw: axes, hatchets, sickles; 1/6 Shot: arrows and bolts; 1/0xA ThrowScythe)
//             within ShieldBashDeflectReach mm (default 3000) in the same cone and 2.5 m band gets the knife record
//             the parry's thrown deflect writes (parry_general_block.h thrown::Deflect): next frame the native
//             emWepDmCk knocks it away from Link with its own sound and effect. Explosive arrows, rockets,
//             dynamite and grenades (routines 7, 8, 9, 0xC) are left alone: their native reaction is an explosion.
//   Motion    since spin-fix-01 (ShieldBashMotion, default on): TP's ANM_SHIELD_ATTACK (atgpush.bck, 17 TP frames,
//             0.57 s; scripts/shield_bash_anim.py, Bin32/link_shield_bash.fcv/.seq, SHA-pinned in spin_attack_assets.h)
//             on the sword lease, interruptible like the spin; the kick records, the weapon window and the sound land on
//             its impact frame (game frame 11). Without the clip (missing files, ShieldBashMotion=0, a refused start)
//             the bash fires on the tap as before, with Link's parry reaction layer (ParryReaction=1) as the shove.
//   Sound     Bin32/link_shield_bash.wav (Z2SE_AL_V_TATE_OSHI + Z2SE_HIT_SHIELD_ATTACK), SHA-pinned; plays on
//             every bash, even with nothing in front (like the Epona howl).
// Keys in [LinkRuntime], read only with SpinAttack=1 or EndingBlow=1 (the sword arbiter samples the input);
// needs ZTarget=1 at run time. ShieldBash: missing or exact 1 = on (Donald 18-oct: default on), exact 0 = off.
#include "game_clock.h"
#include "mortal_draw_target.h"
#include <cmath>
#include <cstdint>

namespace link_runtime::shield_bash {
namespace tg=link_runtime::mortal_draw::target;
inline constexpr const char* kSoundSha="1f5e600391df9e3548a15ea7bd71b8c3c53af7b1b62ed1d5889e213053962a8b";
inline constexpr const wchar_t* kSoundFile=L"link_shield_bash.wav";
inline constexpr double kTapFrames=15.0,kCooldownFrames=45.0;   // 60 Hz game frames (game_clock.h)
inline constexpr float kHalfConeDeg=60.f,kHalfConeCos=0.5f;        // cos(60 degrees)
inline constexpr float kGanadoVertical=1500.f,kWeaponVertical=2500.f;
inline constexpr unsigned kMaxTargets=4;
// spin-fix-01: the calc result the bash's own kick record gets (shield_bash_scan.h Claims): a token 1 (LifeDownSet2
// scales it by at most x2, so at most 2 HP) while the Ganado has 3 HP or more, else 0: the bash never kills.
inline int KnockbackDamage(int hp){return hp>=3?1:0;}
struct Config {
    bool on=true;
    bool motion=true;   // ShieldBashMotion (spin-fix-01): play TP's shield attack clip, the push lands on its impact frame
    unsigned reach=2200,targets=3,deflect_ms=500,deflect_reach=3000;
    double DeflectFrames() const{return deflect_ms*60.0/1000.0;}
};

// ---- input -------------------------------------------------------------------------------------
struct Input {
    uint64_t frame=0;double time=0;uint32_t scene=0,player=0;
    bool valid=false;   // gameplay, Link readable, foreground, nothing else owns Link (ocarina, sword lease...)
    bool ready=false;   // a press may start here: no native priority offer, a knife/idle routine
    bool knife=false,knife_edge=false,fire=false,z=false;
};
enum class Event:uint8_t {None,Pressed,Bash,Held,Aborted,Cooldown};
inline const char* EventName(Event e){
    static const char* const n[]={"none","pressed","bash","held","aborted","cooldown"};
    return n[unsigned(e)];
}
class Tap {
    bool known_=false,pressing_=false,have_last_=false;uint64_t frame_=0;double start_=0,last_=0;uint32_t scene_=0,player_=0;
public:
    bool Pressing() const{return pressing_;}
    void Reset(){pressing_=false;}
    Event Step(const Input& s){
        if(known_&&s.frame==frame_)return Event::None;   // the same frame twice
        known_=true;frame_=s.frame;
        if(!s.valid||!std::isfinite(s.time)){const bool was=pressing_;pressing_=false;return was?Event::Aborted:Event::None;}
        if(pressing_){
            if(s.scene!=scene_||s.player!=player_||s.fire||!s.z||s.time<start_-game_clock::kEpsilon){pressing_=false;return Event::Aborted;}
            if(game_clock::Exceeded(s.time-start_,kTapFrames)){pressing_=false;return Event::Held;}   // a hold, not a tap
            if(s.knife)return Event::None;
            pressing_=false;
            if(have_last_&&!game_clock::Reached(s.time-last_,kCooldownFrames)&&s.time>=last_-game_clock::kEpsilon)return Event::Cooldown;
            have_last_=true;last_=s.time;return Event::Bash;
        }
        if(s.knife_edge&&s.knife&&s.ready&&s.z&&!s.fire){pressing_=true;start_=s.time;scene_=s.scene;player_=s.player;return Event::Pressed;}
        return Event::None;
    }
};

// ---- cone --------------------------------------------------------------------------------------
// Horizontal distance from Link, height band and the facing cone (forward = (sin yaw, cos yaw), as
// ending_blow_target.h Select). A point closer than 100 mm counts as in front.
inline bool Front(const tg::Vec3& link,float yaw,const tg::Vec3& p,float reach,float vertical,float& distance){
    distance=0;
    if(!std::isfinite(yaw)||!tg::detail::Finite(link)||!tg::detail::Finite(p))return false;
    const float dx=p.x-link.x,dz=p.z-link.z;distance=std::sqrt(dx*dx+dz*dz);
    if(!(distance<=reach)||!(std::fabs(p.y-link.y)<=vertical))return false;
    if(distance<100.f)return true;
    return (std::sin(yaw)*dx+std::cos(yaw)*dz)/distance>=kHalfConeCos;
}

// ---- settings ----------------------------------------------------------------------------------
// Shared by the Great Spin and the Shield Bash keys: missing = default on, exact "1" on, exact "0" off.
enum class Flag:uint8_t {Missing,On,Off,Bad};
inline Flag ParseFlag(const wchar_t* v){
    if(!v||!v[0])return Flag::Missing;
    if(v[1]==0&&v[0]==L'1')return Flag::On;
    if(v[1]==0&&v[0]==L'0')return Flag::Off;
    return Flag::Bad;
}
}  // namespace link_runtime::shield_bash

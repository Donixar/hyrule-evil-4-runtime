#pragma once
// Great Spin (Gran Giro, TP hidden skill; docs/combat-gs-sb-01-2026-10-18.md). Pure: no hooks, no process access.
//
// The existing spin attack (spin_attack.h, SpinAttack=1: hold the knife button one second and let go) becomes the
// Great Spin when Link's HP is full at the release, as in TP (d_a_alink_cut.inc procCutTurnInit with
// checkCutLargeTurnState: mLargeAttackRadius, Z2SE_AL_KAITEN_L_SLASH + Z2SE_AL_V_KAITENGIRI_L). Same clip, lease,
// interruption rules and strike tick as the spin; only these differ:
//   radius   GreatSpinRadius mm horizontal (default 3500; the spin's is ending_blow_target.h spin_radius 2200),
//            the spin's 400 mm height band;
//   damage   one Scaled native knife hit per Ganado at GreatSpinPermille of the native amount (default 2000 = x2;
//            mortal_draw_damage.h Kind::Scaled, 1000..3000). A Scaled hit refuses the head/core part: then the
//            same token publishes an ordinary hit instead (HitQueue), never none;
//   effect   GreatSpinSparkles spark bursts (the parry's own CORE 0x1F/0x20 emitter, parry_spark_runtime.h
//            EmitFree) on a ring around Link at the strike, 0 = none (default 8);
//   sound    Bin32/link_great_spin.wav (scripts/combat_gs_sb_sounds.py, SHA-pinned) instead of the spin voice;
//            missing or changed: the spin voice plays as before.
// Keys in [LinkRuntime], read only with SpinAttack=1. GreatSpin: missing or exact 1 = on (Donald 18-oct: default
// on), exact 0 = off; anything else or a number out of range is a problem (logged) and keeps the default.
#include <cmath>
#include <cstdint>

namespace link_runtime::great_spin {
inline constexpr const char* kSoundSha="c9c94f46a89a564cb991e240569d13fa00251f8f7956ffa1764ad719b35804cd";
inline constexpr const wchar_t* kSoundFile=L"link_great_spin.wav";
inline constexpr unsigned kMaxSparkles=16;
struct Config {
    bool on=true;
    unsigned radius=3500,permille=2000,sparkles=8;
};
// HP at global+0x4FB4 (int16 hp, int16 max at +0x4FB6), as fairy_revive.h reads it. Full = alive and at max.
inline bool FullHealth(int hp,int max){return hp>0&&max>0&&hp>=max;}
// Ring of `count` points (count <= kMaxSparkles) at `radius` around center, `height` above it; each burst faces
// outwards (rot = pitch 0, yaw atan2(x,z) as the spark emitter's rot[1]). Returns how many were written.
inline unsigned Ring(const float* center,float radius,float height,unsigned count,float (*pos)[3],float (*rot)[3]){
    if(!center||!pos||!rot||!count||count>kMaxSparkles||!std::isfinite(radius)||!std::isfinite(height))return 0;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(center[i]))return 0;
    constexpr float kTwoPi=6.28318530718f;
    for(unsigned i=0;i<count;++i){
        const float a=kTwoPi*float(i)/float(count),s=std::sin(a),c=std::cos(a);
        pos[i][0]=center[0]+radius*s;pos[i][1]=center[1]+height;pos[i][2]=center[2]+radius*c;
        rot[i][0]=0.f;rot[i][1]=std::atan2(s,c);rot[i][2]=0.f;
    }
    return count;
}
}  // namespace link_runtime::great_spin

#pragma once
#include "rear_shield_measure.h"
#include "rear_shield_band_data.h"
#include <algorithm>
#include <cmath>

// Rear shield "angle + height" (approved by Donald 17-oct; the only mode since 20-oct).
// Each hit is judged on its own classifier epoch; no cross-frame history, no
// shield-before-body ordering (accepted simplification), no scythe handler
// byte pin (re4_fps redirects 0x700A5/0x700DF) and no hitpart body bytes
// (the game writes +0x28). A hit is halved when ALL hold:
//   * gameplay, pl08 or pl10 Hylian Link (character 0x100 or 0x300, 119 parts, kind 0);
//   * Link standing (Footwork 0x00010000 / 0x01010000) or walking/backing/running
//     (R0 0, R1 1/2/3; table pinned in mortal_draw_start_gate.h). Damage, grab,
//     ladder, kick, aim, knife, turn, death and loading are other routines;
//   * an attributed Ganado melee sample (RouteOnce chain: producer 0x55CE2,
//     handler return 0x29149, direct call 0x55BE0, radius 250): axe/short
//     sickle, hoe/pitchfork or scythe callers below; native_hit 1..6;
//   * the attacker (owner+0x94) lies in Link's rear 120 degrees (60 either side of straight
//     behind) and the weapon sphere
//     centre is behind Link (root-local z < 0);
//   * the sphere reaches the shield's height: centre height above Link's root
//     within [kShieldLow - radius, kShieldHigh + radius] (mesh band generated
//     offline by scripts/rear_shield_profile_data.py --band-output), with the top raised to
//     kTopOfHead: the 17-oct live run showed every axe swing from behind landing at 1720-1875,
//     above the mesh band, and Donald asked for a wider shield, so waist-to-head hits count.
namespace link_runtime::rear_shield::band {
inline constexpr double kTopOfHead=2000.0;  // sphere centre height above Link's root
enum class Family : unsigned {None,Axe,Hoe,Scythe};
inline const char* Name(Family f){
    return f==Family::Axe?"axe_sickle":f==Family::Hoe?"hoe_pitchfork":f==Family::Scythe?"scythe":"none";
}
// Spatial sample callers inside the attributed em10 attack handlers. Owner R1
// (+0xFD) is the handler's routine: AxeAtk 0x26, SukiAtk 0x29, ScytheAtk 0x2A.
inline Family CallerFamily(uint32_t rva){
    switch(rva){
    case 0x6EBE4:case 0x6EC1E:case 0x6EC64:case 0x6EC9E:return Family::Axe;
    case 0x6FADE:case 0x6FB1B:case 0x6FB5B:case 0x6FB98:case 0x6FBD5:case 0x6FC15:return Family::Hoe;
    // 0x7020A is the middle (z400) point of the same ScytheAtk loop; it is
    // observed with attack 9 / weapon 6 / owner 0x12A01 in the dll9-11 logs.
    case 0x701D0:case 0x7020A:case 0x70247:return Family::Scythe;
    default:return Family::None;
    }
}
inline bool FamilyMatches(Family f,const measure::Route& r){
    const unsigned r0=r.routines[0],r1=r.routines[1];
    if(r0!=1)return false;
    switch(f){
    case Family::Axe:return r1==0x26&&(r.weapon_type==2||r.weapon_type==3)&&r.attack_no==0;
    case Family::Hoe:return r1==0x29&&r.weapon_type==1&&r.attack_no==8;
    case Family::Scythe:return r1==0x2A&&r.weapon_type==6&&(r.attack_no==9||r.attack_no==10);
    default:return false;
    }
}
inline bool StandingRoutine(uint32_t r){return r==0x00010000U||r==0x01010000U;}
inline bool MovingRoutine(uint32_t r){const uint32_t r0=r&0xFF,r1=(r>>8)&0xFF;return r0==0&&r1>=1&&r1<=3;}
inline bool RoutineAllowed(uint32_t r){return StandingRoutine(r)||MovingRoutine(r);}

struct Sample {
    bool valid=false;int native_hit=0;
    shield::Frame frame;measure::Route route;
    uint32_t frame_counter=0;unsigned char parts=0;
    std::array<unsigned char,12> owner_position{};
};
struct Evidence {
    bool evaluated=false,qualified=false;
    Reason reason=Reason::Off;
    unsigned stage=0; // 1 classifier, 2 preclamp
    Family family=Family::None;
    uint32_t routine=0,caller=0,attack_no=0,weapon_type=0;
    double bearing=0,height=0,local_z=0,front_dot=0,band_low=0,band_high=0;
};
// Leon (pl_type 0) as pl08 (costume 1) or pl10 (costume 3). The installed pl10 body is the pl08
// body with the Master Sword sheath instead of the Ordon one: the Hylian shield (3248 triangles,
// rigid on bone 2) matches pl08's to 2.4e-6 in position, normal, UV and weights
// (docs/ms-castle-01-2026-10-18.md), so kShieldLow/kShieldHigh hold for both.
inline bool HylianLinkCharacter(uint32_t character){const uint32_t c=character&0xFFFFu;return c==0x100u||c==0x300u;}
inline Evidence Evaluate(const Sample& s,unsigned stage=1){
    Evidence e;e.evaluated=true;e.stage=stage;
    const auto& f=s.frame;const auto& r=s.route;
    e.routine=shield::U32(f.routines.data());e.caller=r.caller;e.attack_no=r.attack_no;e.weapon_type=r.weapon_type;
    e.family=CallerFamily(r.caller);
    const auto fail=[&](Reason why){e.reason=why;return e;};
    if(!s.valid)return fail(Reason::Unstable);
    if(!shield::Gameplay(f))return fail(Reason::NotGameplay);
    if(!HylianLinkCharacter(f.character)||s.parts!=119||f.kind[0]!=0)return fail(Reason::UnqualifiedModel);
    if(!RoutineAllowed(e.routine))return fail(Reason::RoutineExcluded);
    const float radius=shield::Float(f.attack.data());
    if(!r.stable||!r.chain||!r.owner||e.family==Family::None||!FamilyMatches(e.family,r)||
       s.native_hit<1||s.native_hit>6||radius!=250.f)return fail(Reason::UnknownRoute);
    const unsigned char* m=f.matrix.data();
    const shield::Vec angles=shield::Vector(f.position_angles.data()+12);
    const shield::Vec forward{shield::Float(m+8),shield::Float(m+24),shield::Float(m+40)};
    const shield::Vec owner=shield::Vector(s.owner_position.data()),current=shield::Vector(f.current.data());
    const double root_x=shield::Float(m+12),root_y=shield::Float(m+28),root_z=shield::Float(m+44),up_y=shield::Float(m+20);
    if(!shield::FlatYawBasis(m,angles,forward)||!shield::Finite(owner)||!shield::Finite(current)||
       !std::isfinite(root_x)||!std::isfinite(root_y)||!std::isfinite(root_z)||!(up_y>0))return fail(Reason::UnknownDirection);
    // Attacker bearing in XZ: d = owner - Link (forward/lateral components).
    const double fx=forward.x,fz=forward.z,dx=double(owner.x)-root_x,dz=double(owner.z)-root_z;
    const double ahead=dx*fx+dz*fz,lateral=dx*fz-dz*fx,n=(dx*dx+dz*dz)*(fx*fx+fz*fz);
    if(!std::isfinite(ahead)||!std::isfinite(lateral)||!std::isfinite(n)||n<=1e-6)return fail(Reason::UnknownDirection);
    e.front_dot=(std::max)(-1.,(std::min)(1.,ahead/std::sqrt(n)));
    e.bearing=std::atan2(lateral,ahead)*57.29577951308232;
    const double f_len=std::sqrt(fx*fx+fz*fz);
    e.local_z=((double(current.x)-root_x)*fx+(double(current.z)-root_z)*fz)/f_len;
    e.height=(double(current.y)-root_y)/up_y;
    e.band_low=data::kShieldLow-radius;e.band_high=(std::max)(data::kShieldHigh+radius,kTopOfHead);
    // Rear cone: 60 degrees either side of straight behind (lateral^2 <= 3 ahead^2). 17-oct live: a still
    // Link is hit mostly from the rear diagonals (bearings 122-130), which the first 45-degree cone missed.
    if(!(ahead<0)||lateral*lateral>3.0*ahead*ahead||!(e.local_z<0))return fail(Reason::OutsideCone);
    if(!(e.height>=e.band_low&&e.height<=e.band_high))return fail(Reason::OutsideBand);
    e.qualified=true;e.reason=Reason::Applied;return e;
}
inline Proof ToProof(const Evidence& e){
    Proof proof;if(!e.qualified||e.reason!=Reason::Applied)return proof;
    auto& c=proof.contact;c.stable=c.gameplay=c.hylian_pl08=c.ordinary=c.direction_qualified=true;
    // Accepted simplification (17-oct): no shield-before-body ordering.
    c.contact_before_body=true;
    // The cone test above already admitted the direction; RearDamage only needs a rear front_dot.
    c.front_dot=(std::min)(e.front_dot,-std::sqrt(.5));
    proof.material=Material::Metal;return proof;
}
template<class Memory> bool RouteNow(Memory& m,const measure::Route& expected){
    auto now=expected;
    return shield::Bytes(m,expected.owner+0xFC,now.routines)&&shield::Bytes(m,expected.owner+0x100,now.actor)&&
        shield::Word(m,expected.owner+0x610,now.weapon)&&m.Read(expected.owner+0x7F0,&now.weapon_type,1)&&
        m.Read(expected.owner+0x28B,&now.sequence,1)&&measure::Equal(now,expected);
}
// One bracketed read: frame/route/owner/parts/counter, then frame/owner/route/counter again.
template<class Memory> bool ReadOnce(Memory& m,const shield::NativeAccess& a,const shield::HitEvent& e,
                                    const measure::Route& route,Sample& n){
    n={};shield::Frame last{};std::array<unsigned char,12> owner_again{};uint32_t counter_again=0;
    if(!route.stable||!route.chain||!shield::Range(route.owner,0x7F1))return false;
    if(!shield::ReadFrame(m,a,e,n.frame)||!RouteNow(m,route)||!shield::Bytes(m,route.owner+0x94,n.owner_position)||
       !m.Read(e.player+0x102,&n.parts,1)||!shield::Word(m,e.global+0x530C,n.frame_counter)||
       !shield::ReadFrame(m,a,e,last)||!shield::Equal(n.frame,last)||
       !shield::Bytes(m,route.owner+0x94,owner_again)||owner_again!=n.owner_position||!RouteNow(m,route)||
       !shield::Word(m,e.global+0x530C,counter_again)||counter_again!=n.frame_counter)return false;
    n.route=route;n.native_hit=e.native_hit;n.valid=true;return true;
}
// Classifier epoch. `frame` is the classification copy already read twice.
template<class Memory> bool Capture(Memory& m,const shield::NativeAccess& a,const shield::HitEvent& e,
                                   const shield::Frame& frame,const measure::Route& route,Sample& out){
    out={};Sample n;
    if(!ReadOnce(m,a,e,route,n)||!shield::Equal(n.frame,frame))return false;
    out=n;return true;
}
inline bool Same(const Sample& a,const Sample& b){
    return a.valid&&b.valid&&a.native_hit==b.native_hit&&shield::Equal(a.frame,b.frame)&&measure::Equal(a.route,b.route)&&
        a.frame_counter==b.frame_counter&&a.parts==b.parts&&a.owner_position==b.owner_position;
}
struct Result {Proof proof;Evidence evidence;};
// Preclamp: reread twice and require the classifier epoch unchanged; then the
// same per-hit evaluation decides. Any disagreement keeps native damage.
template<class Memory> Result Revalidate(Memory& m,const shield::NativeAccess& a,const shield::HitEvent& e,const Sample& captured){
    Result out;Sample first,last;
    if(!captured.valid||e.native_hit!=captured.native_hit||e.player!=captured.frame.player||e.global!=captured.frame.global||
       !ReadOnce(m,a,e,captured.route,first)||!ReadOnce(m,a,e,captured.route,last)||
       !Same(first,captured)||!Same(last,captured)){
        // Keep the classifier measurements for the log, but never a proof.
        out.evidence=Evaluate(captured,2);out.evidence.qualified=false;out.evidence.reason=Reason::Unstable;return out;
    }
    out.evidence=Evaluate(last,2);out.proof=ToProof(out.evidence);return out;
}
}

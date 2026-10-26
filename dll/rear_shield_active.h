#pragma once
#include "link_defense_policy.h"
#include "parry_sword_contact.h"
#include <cstdint>

namespace link_runtime::rear_shield {
enum class Material : unsigned {Unknown,Wood,Metal};
struct Proof {defense::RearContact contact{};Material material=Material::Unknown;};
enum class Reason {Off,UnqualifiedModel,Unstable,NotGameplay,UnknownRoute,UnknownDirection,NoContact,OutsideCone,UnknownMaterial,InvalidDamage,Zero,Applied,Duplicate,OutsideBand,RoutineExcluded,ShieldAway};
inline const char* Name(Reason r){
    switch(r){
    case Reason::Off:return "off";case Reason::UnqualifiedModel:return "asset_pose_unqualified";
    case Reason::Unstable:return "unstable";case Reason::NotGameplay:return "not_gameplay";
    case Reason::UnknownRoute:return "ordinary_route_unqualified";case Reason::UnknownDirection:return "direction_unqualified";
    case Reason::NoContact:return "contact_order_unqualified";case Reason::OutsideCone:return "outside_rear_cone";
    case Reason::UnknownMaterial:return "material_unqualified";case Reason::InvalidDamage:return "invalid_damage";
    case Reason::Zero:return "zero_damage";case Reason::Applied:return "applied";case Reason::Duplicate:return "duplicate";
    case Reason::OutsideBand:return "outside_shield_band";case Reason::RoutineExcluded:return "routine_excluded";
    case Reason::ShieldAway:return "shield_away"; // shield-guard-01: shield in the hand or broken
    }return "unknown";
}
inline Reason CheckProof(const Proof& p){
    const auto& c=p.contact;
    if(!c.hylian_pl08)return Reason::UnqualifiedModel;
    if(!c.stable)return Reason::Unstable;
    if(!c.gameplay)return Reason::NotGameplay;
    if(!c.ordinary)return Reason::UnknownRoute;
    if(!c.direction_qualified)return Reason::UnknownDirection;
    if(!c.contact_before_body)return Reason::NoContact;
    if(!std::isfinite(c.front_dot)||c.front_dot< -1.||c.front_dot> -std::sqrt(0.5))return Reason::OutsideCone;
    if(p.material!=Material::Wood&&p.material!=Material::Metal)return Reason::UnknownMaterial;
    return Reason::Applied;
}
// Owned by ONE native wrapper invocation, never a reusable ATK or timer.
struct Impact {
    bool attempted=false,applied=false,sound=false;
    int original=0,result=0;
    Material material=Material::Unknown;
    Reason reason=Reason::Off;
};
inline bool Apply(bool enabled,const Proof& proof,int32_t& final_damage,Impact& impact,unsigned kept_percent=50){
    if(!enabled)return false;
    if(impact.attempted)return false;
    impact.attempted=true;impact.original=impact.result=final_damage;
    impact.reason=CheckProof(proof);
    if(impact.reason!=Reason::Applied)return false;
    if(final_damage<0){impact.reason=Reason::InvalidDamage;return false;}
    if(final_damage==0){impact.reason=Reason::Zero;return false;}
    impact.result=static_cast<int32_t>(defense::RearDamage(static_cast<uint32_t>(final_damage),proof.contact,kept_percent));
    impact.material=proof.material;impact.applied=impact.sound=true;
    final_damage=impact.result;return true;
}

// Conservative INSIDE rectangle in the identified shield surface, in native
// units. Caller must derive its bounds from the current mesh, not root yaw or
// a whole-player sphere. No production profile is implicitly supplied here.
// Pure fixed-patch / linear-center model: world must be valid over the whole
// interval (or an independently qualified relative-motion equivalent), and
// body_t must be the first body contact on that same trajectory. A current
// bone pose and two sampled endpoints do not establish these assumptions.
struct Surface {parry::sword::Mat34 world{};double half_width=0,half_height=0;};
struct ContactResult {bool valid=false,contact=false;double shield_t=0;};
inline ContactResult ContactBeforeBody(const Surface& s,parry::sword::Vec3 previous,parry::sword::Vec3 current,
                                        double radius,double body_t){
    using namespace parry::sword;
    Mat34 inverse{};
    if(!InverseRigid(s.world,inverse)||!Finite(previous)||!Finite(current)||!std::isfinite(radius)||radius<=0||
       !std::isfinite(body_t)||body_t<0||body_t>1||!std::isfinite(s.half_width)||!std::isfinite(s.half_height)||
       s.half_width<=0||s.half_height<=0)return {};
    const auto a=Transform(inverse,previous),b=Transform(inverse,current);
    ContactResult out{true,false,0};
    if(a.z<=radius||b.z>=a.z||b.z>radius)return out;
    const double t=(a.z-radius)/(a.z-b.z);
    const double x=a.x+t*(b.x-a.x),y=a.y+t*(b.y-a.y);
    out.shield_t=t;
    // At this instant center.z == radius, so (x,y,0) is the real tangent
    // point. Requiring the whole projected sphere footprint was unnecessary
    // and rejected spheres larger than an otherwise valid interior patch.
    // Earlier the center was farther than radius from every point in this
    // plane. Tangency outside the patch remains rejected, including edge
    // contacts that this deliberately conservative helper does not solve.
    // Equal times / unknown body contact are deliberately not a shield hit.
    out.contact=t>=0&&t<=1&&t+1e-6<body_t&&std::abs(x)<=s.half_width&&std::abs(y)<=s.half_height;
    return out;
}
}

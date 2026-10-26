#pragma once
#include "parry_owner_runtime.h"
#include "parry_sword_epoch.h"
#include "parry_front_native.h"
namespace link_runtime::parry::contact_diagnostic {
namespace epoch=sword::epoch;
enum class BodyOrder {Unknown};
struct Player {float values[6]{};};
struct Epoch {epoch::Stamp stamp{};uint64_t serial=0;uint32_t lifecycle=0;sword::reader::Observation pose{};};
struct Result {
 uint32_t current_caller=0;float radius=0,sample_local_z=0;
 bool endpoint_intersects=false,spatial_segment_intersects=false,body_hit_reported=false,native_short_direction=false;
 double endpoint_distance_squared=0,spatial_segment_distance_squared=0,native_direction_dot_estimate=0;
 BodyOrder body_order=BodyOrder::Unknown;
 bool front_known=false;front_math::Result front{};
 bool DefenseQualified() const{return false;}
};
// Diagnostic composition only. No hook, input consumption, latch write or Decision.
// The first Axe route passes a spatial base, NOT a previous-frame position.
namespace detail {
inline sword::Vec3 Vector(const unsigned char* p){float v[3];std::memcpy(v,p,12);return {v[0],v[1],v[2]};}
inline bool LocalZ(uint32_t caller,float& z){
 switch(caller){case 0x6EBE4:z=-200;return true;case 0x6EC1E:z=50;return true;
 case 0x6EC64:z=300;return true;case 0x6EC9E:z=550;return true;
 // SukiAtk samples along the weapon from its base at z=-2000 (parry-coverage section 3).
 case 0x6FADE:z=-1000;return true;case 0x6FB1B:z=-700;return true;case 0x6FB5B:z=-400;return true;
 case 0x6FB98:z=-100;return true;case 0x6FBD5:z=300;return true;case 0x6FC15:z=600;return true;
 default:return false;}
}
// Em10AtkTbl radius bits of the qualified selectors: 250 for attacks 0, 1 and 8; 350 for attack 3.
inline uint32_t ExpectedRadius(uint32_t attack_no){return attack_no==3?0x43AF0000U:0x437A0000U;}
inline bool Geometry(const owner::Observation& o,const Epoch& e,const Player& player,Result& r){
 const bool suki_return=o.producer_return>=0x6FADE&&o.producer_return<=0x6FC15;
 if(!o.QualifiedRoute()||suki_return!=(o.family==owner::Family::SukiHandWeapon)||!LocalZ(o.producer_return,r.sample_local_z)||
    shield::U32(o.descriptor.data())!=ExpectedRadius(o.attack_no)||shield::U32(o.table_descriptor.data())!=ExpectedRadius(o.attack_no))return false;
 for(float v:player.values)if(!std::isfinite(v))return false;
 const auto sample=Vector(o.current.data()),base=Vector(o.previous.data());
 const auto& blade=e.pose.blade;
 // Radius is the exact descriptor float passed twice by 1BC488..1BC4A1.
 // Zero blade radius tests the attributed blade axis; no thickness is invented.
 float radius;std::memcpy(&radius,o.descriptor.data(),4);
 const auto endpoint=sword::SweptSphereBlade(sample,sample,radius,blade.base,blade.tip,0);
 const auto spatial=sword::SweptSphereBlade(base,sample,radius,blade.base,blade.tip,0);
 if(!endpoint.valid||!spatial.valid)return false;
 r.current_caller=o.producer_return;r.radius=radius;
 r.endpoint_intersects=endpoint.intersects;r.spatial_segment_intersects=spatial.intersects;
 r.endpoint_distance_squared=endpoint.distance_squared;r.spatial_segment_distance_squared=spatial.distance_squared;
 const auto delta=sword::Sub(sample,base);r.native_short_direction=delta.x*delta.x+delta.z*delta.z<10000.;
 const sword::Vec3 position={player.values[0],player.values[1],player.values[2]};
 const auto direction=r.native_short_direction?sword::Sub(position,sample):delta;
 const double x=player.values[3],y=player.values[4],z=player.values[5];
 const double sx=std::sin(x),cx=std::cos(x),sy=std::sin(y),cy=std::cos(y),sz=std::sin(z),cz=std::cos(z);
 const sword::Vec3 forward={cx*sy*cz+sx*sz,cx*sy*sz-sx*cz,cx*cy};
 // Native RotVector/float rounding is not replayed. This finite double result
 // reports orientation evidence; it is never an authoritative admission gate.
 r.native_direction_dot_estimate=sword::Dot(forward,direction);
 return std::isfinite(r.native_direction_dot_estimate);
}
inline bool SameEpoch(const Epoch& a,const Epoch& b){
 return epoch::Same(a.stamp,b.stamp)&&a.serial==b.serial&&a.lifecycle==b.lifecycle&&epoch::SamePose(a.pose,b.pose)&&
        std::memcmp(&a.pose.blade,&b.pose.blade,sizeof(a.pose.blade))==0;
}
// Sources is an offline fixture seam. The public entry constructs NativeSources;
// it accepts neither caller-ready flags nor retained owner/epoch observations.
template<class Sources> bool Build(Sources& source,const Candidate& c,Result& out,bool sample=false){
 out={};owner::Observation first,last;Epoch before,after;Player player,again;Result r;
 if((!sample&&!c.body_part)||!source.Owner(c,first)||!source.EpochRead(before)||
    first.global!=before.stamp.global||first.engine_frame!=before.stamp.frame||
    before.pose.player!=before.stamp.player||before.pose.global!=before.stamp.global||
    !source.PlayerRead(before.stamp.player,player)||!Geometry(first,before,player,r))return false;
 front_math::Result front;
 r.front_known=source.Front(first,player,front);
 if(r.front_known)r.front=front;
 if(!source.PlayerRead(before.stamp.player,again)||std::memcmp(player.values,again.values,sizeof(player.values))||
    !source.EpochRead(after)||!SameEpoch(before,after)||!source.Owner(c,last)||!owner::Equal(first,last))return false;
 r.body_hit_reported=c.body_part!=0;out=r;return true;
}
struct NativeSources {
 const shield::NativeAccess& access;const owner::Code& code;
 const front_native::Code& front_code;bool sample=false;
 bool Owner(const Candidate& c,owner::Observation& out){return sample?owner::ObserveSampleCurrent(access,code,c,out):owner::ObserveCurrent(access,code,c,out);}
 bool EpochRead(Epoch& out){
  out={};if(!epoch::AtContact(out.pose,out.stamp))return false;
  out.serial=epoch::state.serial;out.lifecycle=epoch::seen_lifecycle;return true;
 }
 bool PlayerRead(uint32_t player,Player& out){
  owner::Memory memory;return shield::Range(player,0xAC)&&memory.Read(player+0x94,out.values,sizeof(out.values));
 }
 bool Front(const owner::Observation& owner,const Player& player,front_math::Result& out){
  return front_native::Evaluate(front_code,access,owner,player.values,player.values+3,out);
 }
};
inline __declspec(thread) bool reading=false,poisoned=false;
inline bool ReadImpl(const shield::NativeAccess& access,const owner::Code& code,const Candidate& c,Result& out,const front_native::Code& front_code,bool sample=false){
 out={};if(reading){poisoned=true;return false;}
 reading=true;poisoned=false;bool ok=false;
 __try {NativeSources source{access,code,front_code,sample};ok=Build(source,c,out,sample);}
 __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
 if(poisoned)ok=false;if(!ok)out={};reading=false;return ok;
}
}
inline bool ObserveCurrent(const shield::NativeAccess& access,const owner::Code& code,const Candidate& c,Result& out,const front_native::Code& front_code={},bool sample=false){
 const DWORD error=GetLastError();__declspec(align(16)) unsigned char fp[512];parry::SaveFP(fp);
 const uint32_t mxcsr=0x1f80;__asm {fninit
  ldmxcsr mxcsr}
 bool ok=false;
 __try {ok=detail::ReadImpl(access,code,c,out,front_code,sample);}
 __finally {parry::RestoreFP(fp);SetLastError(error);}
 return ok;
}
}

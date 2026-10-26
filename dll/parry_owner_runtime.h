#pragma once
#include "parry_pre_effects.h"
#include "shield_owner_context.h"
#include <intrin.h>
#include "parry_owner_fingerprint.h"

// Synchronous attribution only. Never retain this observation as an actor lease,
// call a native reaction, or write the connected-attack latch from this reader.
namespace link_runtime::parry::owner {
using namespace link_runtime::shield;
// suki: the SukiAtk handler (R1 0x29, the hoe/pitchfork Wep_type 1) is pinned too; Axe stands alone if not.
struct Code {bool verified=false;uint32_t image_base=0;bool suki=false;};
namespace detail {
// re4_fps 1.5.1 coexistence (docs/fps-coexistence-2026-10-12.md §7). re4_tweaks redirects
// `call 0xF3AD` (thunk -> 0x2D0020, the target-angle/turn helper: atan2 to the target minus the
// current yaw, normalized) into its own code; in the 12-oct dumps at AxeAtk 0x6E838/0x6E872
// (and the same helper in SukiAtk 0x6F9F0/0x6FA2D). Only the turn delta's scaling can change:
// the caller keeps adding st0 to +0xA4. Our parry never calls that helper nor predicts its
// result (front/owner read the live yaw), so the redirect is behaviour-neutral for us.
// A pinned window accepts, exactly where the pinned bytes hold `E8 rel32 -> 0xF3AD`, an E8 whose
// live target is outside the image AND inside another loaded module's code (probe), provided the
// thunk still reaches 0x2D0020. Everything else in the window is compared as before.
constexpr uint32_t kTurnHelperThunkRva=0xF3AD,kTurnHelperRva=0x2D0020;
inline const foreign::Probe* foreign_probe=nullptr;  // set by the runtime before qualification
struct ForeignCalls {uint32_t site_rva[8]{},target[8]{};unsigned count=0;};
inline ForeignCalls foreign_calls;  // accepted sites (deduplicated), for the session log
inline bool TurnHelperCall(const unsigned char* image,const NativeAccess& a,uint32_t site,const unsigned char* expected) {
    if(expected[0]!=0xE8||site+5+U32(expected+1)!=kTurnHelperThunkRva||image[site]!=0xE8)return false;
    const uint32_t target=a.image_base+site+5+U32(image+site+1);
    if(target-a.image_base<a.image_size||!foreign::Accept(foreign_probe,target))return false;
    size_t helper=0;
    if(!Area(a.regions.data(),a.regions.size(),kTurnHelperThunkRva,5,true)||
       !Relative(image,a.image_size,kTurnHelperThunkRva,0xE9,helper)||helper!=kTurnHelperRva)return false;
    auto& r=foreign_calls;bool known=false;
    for(unsigned k=0;k<r.count;++k)if(r.site_rva[k]==site)known=true;
    if(!known&&r.count<8){r.site_rva[r.count]=site;r.target[r.count]=target;++r.count;}
    return true;
}
template<size_t N,size_t M> bool Window(const unsigned char* image,const NativeAccess& a,uint32_t at,
        const char (&hex)[N],const Fixup (&fixups)[M]) {
    const size_t length=(N-1)/2;if(!Area(a.regions.data(),a.regions.size(),at,length,true))return false;
    for(size_t i=0;i<length;++i) {
        bool fixed=false;
        for(const auto& f:fixups)if(i==f.offset) {
            if(i+4>length||U32(image+at+i)!=uint64_t(a.image_base)+f.rva)return false;
            fixed=true;i+=3;break;
        }
        if(fixed)continue;
        const unsigned char want=static_cast<unsigned char>(owner_detail::Hex(hex[2*i])*16+owner_detail::Hex(hex[2*i+1]));
        if(want==0xE8&&i+5<=length&&image[at+i]==0xE8) {
            unsigned char expected[5];
            for(size_t k=0;k<5;++k)expected[k]=static_cast<unsigned char>(owner_detail::Hex(hex[2*(i+k)])*16+owner_detail::Hex(hex[2*(i+k)+1]));
            bool fixup_inside=false;for(const auto& f:fixups)if(f.offset>i&&f.offset<i+5)fixup_inside=true;
            if(!fixup_inside&&std::memcmp(image+at+i,expected,5)&&TurnHelperCall(image,a,static_cast<uint32_t>(at+i),expected)){i+=4;continue;}
        }
        if(image[at+i]!=want)return false;
    }
    return true;
}
inline bool Stack(uint32_t at,size_t size,uint32_t low,uint32_t high) {
    return Range(at,size)&&at>=low&&uint64_t(at)+size<=high;
}
using Invocation=parry::Invocation;
// Generic structural invocation plus the em10 producer frame (handler locals below EBP).
inline Invocation Current(const Candidate& c,bool sample=false) {
    const auto invocation=CurrentInvocation(c,sample);
    if(!invocation.wrapper||c.producer_frame<0x4C||!Stack(c.producer_frame-0x4C,0x60,invocation.low,invocation.high)||
       uint64_t(reinterpret_cast<uint32_t>(invocation.wrapper))+sizeof(WrapperToken)>c.producer_frame-0x4C)return {};
    return invocation;
}
}
inline Code FindCode(const unsigned char* image,size_t size,const NativeAccess& a) {
    if(!image||size!=a.image_size||!Complete(a)||!FindOwnerCode(image,size,a).verified||
       !Area(a.regions.data(),a.regions.size(),0x7F3AFC,4,false)||
       U32(image+0x7F3AFC)!=uint64_t(a.image_base)+0x6E610||
       !detail::Window(image,a,0x29120,detail::dispatch,detail::dispatch_fixups)||
       !detail::Window(image,a,0x6E610,detail::handler,detail::handler_fixups))return {};
    Code code{true,a.image_base,false};
    code.suki=Area(a.regions.data(),a.regions.size(),0x7F3B14,4,false)&&U32(image+0x7F3B14)==uint64_t(a.image_base)+0x6F7D0&&
        detail::Window(image,a,0x6F7D0,detail::suki_handler,detail::suki_handler_fixups);
    return code;
}
enum class Family {Unknown,AxeHandWeapon,SukiHandWeapon};
struct Observation {
    Family family=Family::Unknown;
    uint32_t owner=0,producer_frame=0,handler_frame=0,dispatcher_frame=0,producer_return=0;
    uint32_t global=0,engine_frame=0,weapon=0,shield=0,owner_flags=0,status=0,attack_no=0,parts=0;
    unsigned char weapon_type=0,connected=0,sequence=0;
    std::array<unsigned char,4> routines{};
    std::array<unsigned char,2> actor{};
    std::array<unsigned char,16> descriptor{},table_descriptor{};
    std::array<unsigned char,12> current{},previous{};
    // The currently attributed first selector, not the final parry coverage policy.
    // AxeAtk selectors (parry-coverage section 4): attack 0 for the hand weapons, attack 1 for the
    // decomp "pitchfork" Wep_type 0xA, attack 3 (radius 350) for Wep_type 0xF. Wep_type 0xB (base
    // outside the frame) stays out; the two extra samples belong to Wep_type 0x10 only.
    bool FirstHandWeaponRoute() const {
        const bool extra=producer_return==0x6EC64||producer_return==0x6EC9E;
        const bool selector=(attack_no==0&&weapon_type!=0xA&&weapon_type!=0xF)||(weapon_type==0xA&&attack_no==1)||
            (weapon_type==0xF&&attack_no==3);
        return family==Family::AxeHandWeapon&&routines[1]==0x26&&routines[2]==1&&
            (sequence&1)&&selector&&!parts&&U16(descriptor.data()+10)==0&&
            !(status&0x2000)&&!connected&&Range(weapon,0x3C)&&
            weapon_type!=0xB&&actor[1]!=2&&actor[1]!=0x18&&
            (!extra||weapon_type==0x10);
    }
    // SukiAtk (R1 0x29), attack 8, six samples along the weapon from z=-2000 (parry-coverage section 3).
    bool SukiRoute() const {
        return family==Family::SukiHandWeapon&&routines[1]==0x29&&routines[2]==1&&(sequence&1)&&attack_no==8&&!parts&&
            U16(descriptor.data()+10)==0&&!(status&0x2000)&&!connected&&Range(weapon,0x3C)&&weapon_type==1&&
            actor[1]!=2&&actor[1]!=0x18;
    }
    bool QualifiedRoute() const {return FirstHandWeaponRoute()||SukiRoute();}
};
inline bool Equal(const Observation& a,const Observation& b) {
    return a.family==b.family&&a.owner==b.owner&&a.producer_frame==b.producer_frame&&a.handler_frame==b.handler_frame&&
        a.dispatcher_frame==b.dispatcher_frame&&a.producer_return==b.producer_return&&a.global==b.global&&a.engine_frame==b.engine_frame&&
        a.weapon==b.weapon&&a.shield==b.shield&&a.owner_flags==b.owner_flags&&a.status==b.status&&a.attack_no==b.attack_no&&
        a.parts==b.parts&&a.weapon_type==b.weapon_type&&a.connected==b.connected&&a.sequence==b.sequence&&a.routines==b.routines&&
        a.actor==b.actor&&a.descriptor==b.descriptor&&a.table_descriptor==b.table_descriptor&&a.current==b.current&&a.previous==b.previous;
}
namespace detail {
template<class Memory> bool Once(Memory& m,const NativeAccess& a,const Candidate& c,const Invocation& invocation,Observation& o) {
    const uint32_t p=c.producer_frame;o.owner=c.producer_owner;o.producer_frame=p;
    uint32_t ret=0,handler_owner=0,dispatcher_owner=0,handler_return=0,current=0,previous=0,parts_arg=0,global_again=0;
    if(c.attack!=p-0x2C||!Range(o.owner,0x7F1)||!Word(m,p,o.handler_frame)||
       o.handler_frame<=p||!Stack(o.handler_frame-0x20,0x2C,invocation.low,invocation.high)||
       !Word(m,p+4,ret)||ret<a.image_base)return false;
    o.producer_return=ret-a.image_base;
    const bool axe=o.producer_return==0x6EBE4||o.producer_return==0x6EC1E||o.producer_return==0x6EC64||o.producer_return==0x6EC9E;
    const bool suki=o.producer_return==0x6FADE||o.producer_return==0x6FB1B||o.producer_return==0x6FB5B||
        o.producer_return==0x6FB98||o.producer_return==0x6FBD5||o.producer_return==0x6FC15;
    if(!axe&&!suki)return false;
    if(c.current!=o.handler_frame-0x10||c.old!=o.handler_frame-0x1C||
       !Word(m,o.handler_frame,o.dispatcher_frame)||o.dispatcher_frame<=o.handler_frame||
       !Stack(o.dispatcher_frame,12,invocation.low,invocation.high)||
       !Word(m,o.handler_frame+4,handler_return)||handler_return!=uint64_t(a.image_base)+0x29149||
       !Word(m,o.handler_frame+8,handler_owner)||handler_owner!=o.owner||
       !Word(m,o.dispatcher_frame+8,dispatcher_owner)||dispatcher_owner!=o.owner||
       !Word(m,p+8,current)||current!=c.current||!Word(m,p+12,previous)||previous!=c.old||
       !Word(m,p+16,parts_arg)||!Word(m,p-0x48,o.attack_no)||o.attack_no>=19||
       !Word(m,p-0x4C,o.parts)||o.parts!=parts_arg||!Bytes(m,c.attack,o.descriptor)||
       !Bytes(m,a.image_base+0x7F3F18+o.attack_no*16,o.table_descriptor)||
       !Bytes(m,o.owner+0xFC,o.routines)||!Bytes(m,o.owner+0x100,o.actor)||
       !Word(m,o.owner+0x610,o.weapon)||!Word(m,o.owner+0x618,o.shield)||!Word(m,o.owner+0x3D0,o.owner_flags)||
       !m.Read(o.owner+0x7F0,&o.weapon_type,1)||!m.Read(o.owner+0x7CC,&o.connected,1)||!m.Read(o.owner+0x28B,&o.sequence,1)||
       !Word(m,a.image_base+static_cast<uint32_t>(a.code.global_slot_rva),o.global)||!Range(o.global,0x5310)||
       !Word(m,o.global+0x530C,o.engine_frame)||!Word(m,o.global+0x5020,o.status)||
       !Bytes(m,c.current,o.current)||!Bytes(m,c.old,o.previous)||
       !Word(m,a.image_base+static_cast<uint32_t>(a.code.global_slot_rva),global_again)||global_again!=o.global)return false;
    // Producer changes power; the remaining table fields must still belong to its selector.
    for(unsigned i=0;i<16;++i)if(i!=8&&i!=9&&o.descriptor[i]!=o.table_descriptor[i])return false;
    o.family=axe?Family::AxeHandWeapon:Family::SukiHandWeapon;return true;
}
}
template<class Memory> bool ReadCurrent(Memory& memory,const NativeAccess& a,const Code& code,const Candidate& c,Observation& out,bool sample=false) {
    out={};const auto invocation=detail::Current(c,sample);
    if(!invocation.wrapper||!code.verified||code.image_base!=a.image_base||!Complete(a)||
       c.outer_caller!=uint64_t(a.image_base)+0x55CE2)return false;
    Observation first{},last{};
    if(!detail::Once(memory,a,c,invocation,first)||!detail::Once(memory,a,c,invocation,last)||!Equal(first,last)||
       (first.family==Family::SukiHandWeapon&&!code.suki))return false;
    const auto current=detail::Current(c,sample);
    if(current.wrapper!=invocation.wrapper||current.classifier!=invocation.classifier||current.low!=invocation.low||current.high!=invocation.high)return false;
    out=first;return true;
}
struct Memory {
    bool Read(uint32_t at,void* out,size_t size) {
        if(!Range(at,size))return false;
        __try {std::memcpy(out,reinterpret_cast<const void*>(at),size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
};
inline bool ObserveCurrent(const NativeAccess& a,const Code& code,const Candidate& c,Observation& out) {
    const DWORD error=GetLastError();Memory memory;const bool ok=ReadCurrent(memory,a,code,c,out);SetLastError(error);return ok;
}
inline bool ObserveSampleCurrent(const NativeAccess& a,const Code& code,const Candidate& c,Observation& out) {
    const DWORD error=GetLastError();Memory memory;const bool ok=ReadCurrent(memory,a,code,c,out,true);SetLastError(error);return ok;
}
} // namespace link_runtime::parry::owner

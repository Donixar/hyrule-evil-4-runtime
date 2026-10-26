#pragma once
#include "shield_reader.h"

namespace link_runtime::shield {
struct PreClampAudit {
    bool frame_read=false;
    uint32_t return_address=0,global=0,thread=0,raw_flags=0;
    int32_t damage=0;
    uint32_t native_return=0;
    bool bridged=false;
};
struct PreClampSample {
    uint32_t captures=0;
    bool valid=false;
    bool rejected=false;
    int32_t damage=0,hp=0;
    uint32_t raw_flags=0,global=0,return_rva=0;
    // Diagnostic copies only. Never used for admission or damage modification.
    uint32_t audit_count=0;
    bool audit_overflow=false;
    bool secondary_zero_seen=false;
    bool mitigated=false;
    int32_t applied_damage=0;
    PreClampAudit audit[2]{};
};
struct PreClampCode {
    bool verified=false;
    size_t body_rva=0,site_rva=0,wrapper_return_rva=0;
    size_t secondary_return_rva=0;
};
namespace preclamp_detail {
// Exact qualified player path through its RET; absolute operands below are ASLR-aware.
// This is a code fingerprint, not a new damage implementation or an instruction decoder.
inline constexpr char player_path[]=
    "558BEC535657E83FBCE4FF0FB6F0C1E608E834BCE4FF8B4D108B7D0C0FB6C00BC685C9740D998D3409F7FE2BD103FA897D0C8B75088A9E0001000084DB0F85A8010000A13C6FC0006683B8B44F0000007F075F5E33C05B5DC3"
    "0FB680984F0000894508DB4508DC0D30F7B000DC0550FBB000D95D08E81ED7E4FF84C0740CD94508DC0DF0FBB000D95D088B0D406FC000807908008B353C6FC0007515F7465400000040740CD94508DC0D20FCB000D95D08DB450CD84D08D95D08D94508E83E914B0080BE7C840000018BF8897D0C7710DB450CDC0D7020B100E822914B008BF8"
    "80BEC84F000001750D80BECB4F000002750433FFEB2183FF647E1C81FFF40100007E046A03EB026A02E89D22E5FF8B353C6FC00083C40480BE984F0000037316BA2C010000663996B44F00007E088B5D1483CB01EB038B5D14B82C010000663986B44F00007E20E8FEBAE4FF0FB6C881E10300008079054983C9FC418B353C6FC000750383CB01"
    "0FBF86B44F00003BC77D028BF86629BEB44F0000A13C6FC0006683B8B44F0000007F2EF6C3017411BA01000000668990B44F0000A13C6FC0006683B8B44F0000007D0E33C9668988B44F0000A13C6FC00080B8C84F000001751C80B8CB4F0000027513668B90B64F0000668990B44F0000A13C6FC0000FBFB8B44F00008BC75F5E5B5DC3";
inline unsigned Hex(char c) { return c<='9'?unsigned(c-'0'):unsigned(c-'A'+10); }
inline bool PlayerPath(const unsigned char* body,const NativeAccess& a) {
    static_assert((sizeof(player_path)-1)/2==0x1EB,"qualified player path bounds");
    const size_t absolute[]={0x44,0x68,0x6E,0x83,0x8C,0x96,0xAA,0xD5,0x110,0x15E,0x17C,0x19C,0x1B4,0x1D9};
    for(size_t i=0;i<0x1EB;++i) {
        bool operand=false;
        for(size_t at:absolute)if(i==at) {
            unsigned char original[4]={};
            for(unsigned k=0;k<4;++k)original[k]=static_cast<unsigned char>((Hex(player_path[2*(at+k)])<<4)|Hex(player_path[2*(at+k)+1]));
            const uint32_t rva=U32(original)-0x400000;
            // All ESI reloads and HP readers must use the established global slot.
            if((rva==0x806F3C&&a.code.global_slot_rva!=rva)||U32(body+at)!=uint64_t(a.image_base)+rva)return false;
            i+=3;operand=true;break;
        }
        if(!operand&&body[i]!=((Hex(player_path[2*i])<<4)|Hex(player_path[2*i+1])))return false;
    }
    return true;
}
} // namespace preclamp_detail
inline PreClampCode FindPreClamp(const unsigned char* image,size_t size,const NativeAccess& a) {
    if(!image||size!=a.image_size||!Complete(a))return {};
    const auto* r=a.regions.data();const size_t count=a.regions.size(),wrapper=a.code.wrapper_rva;
    if(!Area(r,count,wrapper,0x88,true)||
        !Match(image+wrapper+0x2F,{0x0F,0xB7,0x56,0x08,0x50,0xA1,-1,-1,-1,-1,0x6A,0,0x52,0x50,0xE8})||
        U32(image+wrapper+0x35)!=uint64_t(a.image_base)+a.code.player_slot_rva||
        !Match(image+wrapper+0x42,{0x83,0xC4,0x10,0xF6,0x46,0x0A,0x08,0x74,0x0F,0x8B,0x15})||
        U32(image+wrapper+0x4D)!=uint64_t(a.image_base)+a.code.global_slot_rva)return {};
    size_t thunk=0,body=0;
    if(!Relative(image,size,wrapper+0x3D,0xE8,thunk)||!Area(r,count,thunk,5,true)||
        !Relative(image,size,thunk,0xE9,body)||!Area(r,count,body,0x1EC,true)||
        !preclamp_detail::PlayerPath(image+body,a))return {};
    const size_t site=body+0x167;
    // Full clamp context, not the seven stolen bytes alone. The exact fingerprint also
    // pins every branch/instruction boundary throughout the player path.
    if(Unique(image,r,count,{0x0F,0xBF,0x86,0xB4,0x4F,0,0,0x3B,0xC7,0x7D,0x02,0x8B,0xF8,0x66,0x29,0xBE,0xB4,0x4F,0,0})!=site||
        body+0x147+static_cast<int8_t>(image[body+0x146])!=site||
        body+0x164+static_cast<int8_t>(image[body+0x163])!=site)return {};
    PreClampCode result{true,body,site,wrapper+0x42};
    // Optional exact fingerprint of the second caller observed in the human
    // trial. Its argument is NOT a constant zero; runtime checks are mandatory.
    constexpr size_t secondary=0x27ECC0;
    constexpr char bytes[]="558BEC568BF1F686680400000274098B068B50086A00FFD28BCEE8DD8ED8FF8B450C6A016A005056E8D484D8FF0FB64D1466018E72050000BAFF00000083C410663996720500000F82A3000000";
    bool exact=Area(r,count,secondary,77,true);
    if(exact)for(size_t i=0;i<77;++i)if(image[secondary+i]!=
        ((preclamp_detail::Hex(bytes[2*i])<<4)|preclamp_detail::Hex(bytes[2*i+1]))) {exact=false;break;}
    size_t secondary_thunk=0;
    if(exact&&Relative(image,size,secondary+0x28,0xE8,secondary_thunk)&&secondary_thunk==thunk)
        result.secondary_return_rva=secondary+0x2D;
    return result;
}
template<class ScopeLike> bool CanSeparateSecondaryZero(const ScopeLike& scope,const PreClampSample& prior,
        const NativeAccess& a,const PreClampCode& code,uint32_t thread,uint32_t global,uint32_t ret,
        int32_t damage,uint32_t flags,const HealthSample& health) {
    return Complete(a)&&code.verified&&code.secondary_return_rva&&
        Area(a.regions.data(),a.regions.size(),code.secondary_return_rva,1,true)&&
        ret==uint64_t(a.image_base)+code.secondary_return_rva&&damage==0&&flags==1&&
        prior.valid&&!prior.rejected&&prior.captures==1&&!prior.secondary_zero_seen&&
        prior.damage>0&&(prior.mitigated?prior.applied_damage:prior.damage)<prior.hp&&prior.return_rva==code.wrapper_return_rva&&
        scope.depth==1&&scope.invocation&&scope.captured&&thread&&thread==scope.thread&&
        global==prior.global&&global==scope.event.global&&health.stable&&
        health.player==scope.event.player&&health.global==global&&
        health.character==scope.before.character&&health.routine==scope.before.routine&&
        health.max_hp==scope.before.max_hp&&health.hp==prior.hp-(prior.mitigated?prior.applied_damage:prior.damage);
}
inline void RejectPreClamp(PreClampSample& s) { s.valid=false;s.rejected=true; }
// Task2 calls this before its first checked read, including EBP+4. On diagnostic
// SEH it must RejectPreClamp; the pure helper does not catch native exceptions.
inline bool ConsumePreClampAttempt(PreClampSample& s) {
    s.valid=false;s.captures=s.captures>=1?2:1;
    if(s.rejected||s.captures!=1){RejectPreClamp(s);return false;}return true;
}
template<class ScopeLike> bool AdmitPreClampAttempt(ScopeLike* scope,const NativeAccess& a,const PreClampCode& code,
        uint32_t thread,uint32_t global,uint32_t frame_return) {
    if(!scope)return false;
    auto& s=scope->preclamp;
    if(s.rejected||s.captures!=1||!Complete(a)||!code.verified||
        !Area(a.regions.data(),a.regions.size(),code.body_rva,0x1EC,true)||
        !Area(a.regions.data(),a.regions.size(),code.site_rva,7,true)||code.site_rva!=code.body_rva+0x167||
        code.wrapper_return_rva!=a.code.wrapper_rva+0x42||scope->depth!=1||!scope->invocation||!thread||scope->thread!=thread||
        !scope->captured||scope->event.native_hit<1||scope->event.native_hit>6||
        scope->event.classifier_return!=uint64_t(a.image_base)+a.code.classifier_return_rva||
        !scope->before.stable||scope->before.player!=scope->event.player||scope->before.global!=scope->event.global||
        scope->classification.player!=scope->event.player||scope->classification.global!=scope->event.global||
        scope->classification.character!=scope->before.character||(scope->before.character&0xFF)!=0||scope->classification.kind[0]!=0||
        global!=scope->event.global||frame_return!=uint64_t(a.image_base)+code.wrapper_return_rva) {
        RejectPreClamp(s);return false;
    }
    return true;
}
// Finish the admitted attempt. The callback owns sequencing Consume -> Admit ->
// checked health reads -> Copy; none of these helpers publishes a completion.
template<class ScopeLike> bool CopyPreClampAttempt(ScopeLike& scope,int32_t damage,uint32_t flags,
        const HealthSample& first,const HealthSample& last,uint32_t return_rva) {
    auto& s=scope.preclamp;
    if(s.rejected||s.valid||s.captures!=1||!first.stable||!last.stable||
        first.player!=scope.event.player||first.global!=scope.event.global||first.character!=scope.before.character||
        first.max_hp!=scope.before.max_hp||first.player!=last.player||first.global!=last.global||first.character!=last.character||
        first.routine!=last.routine||first.hp!=last.hp||first.max_hp!=last.max_hp||
        first.hp<0||first.max_hp<=0||first.hp>first.max_hp||first.hp>0x7FFF||first.max_hp>0x7FFF) {
        RejectPreClamp(s);return false;
    }
    // ReadHealthSample sign-extends HP/max words via int16_t, matching the stolen
    // MOVSX HP operand. Invalid signed health fails closed; EDI remains signed independently.
    s.damage=damage;s.hp=first.hp;s.raw_flags=flags;s.global=first.global;s.return_rva=return_rva;s.valid=true;return true;
}
// frame_return is a checked, copied EBP+4 value. EBP+8 is overwritten float scratch
// in LifeDown and is deliberately absent from this interface.
template<class Memory,class ScopeLike> bool CapturePreClamp(Memory& memory,const NativeAccess& a,const PreClampCode& code,
        ScopeLike* scope,uint32_t thread,uint32_t global,int32_t damage,uint32_t flags,uint32_t frame_return) {
    if(!scope||!ConsumePreClampAttempt(scope->preclamp)||!AdmitPreClampAttempt(scope,a,code,thread,global,frame_return))return false;
    HealthSample first{},last{};
    if(!ReadHealthSample(memory,a,scope->event,first)||!ReadHealthSample(memory,a,scope->event,last)) {
        RejectPreClamp(scope->preclamp);return false;
    }
    return CopyPreClampAttempt(*scope,damage,flags,first,last,static_cast<uint32_t>(code.wrapper_return_rva));
}
} // namespace link_runtime::shield

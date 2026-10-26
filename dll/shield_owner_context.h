#pragma once
#include "shield_reader.h"

// Copied producer diagnostics only. A valid context does NOT authorize defense,
// identify contact, or turn an attack number into an ordinary-attack allowlist.
namespace link_runtime::shield {
struct OwnerCode {bool verified=false;uint32_t return_rva=0;};
struct OwnerContext {
    bool valid=false;
    uint32_t owner=0,attack_no=0,parts=0,power=0,raw_flags=0;
    uint32_t owner_flags=0,status=0;
    unsigned char difficulty=0,attack_connected=0;
    std::array<unsigned char,16> descriptor{};
};
namespace owner_detail {
// Qualified prefix through the E8 producing return 0x55CE2. This pins every
// producer-frame local, argument, owner register and catch-held override used
// below. GC names are context only; this fingerprint is the UHD code contract.
inline constexpr char prefix[]=
    "558BEC83EC4CA1105EC30033C58945FC8B4510538B5D0C568B7508578BFA568975BC894DB88945B4E8633EFFFF83C40480BFCC070000000F85660E00008B4DB88BC1C1E00405183FBF008B108955D48B50048955D88B50088B400C8955DC8945E083F90D74308BC7E8B343FEFF0FB7CA894DC0DB45C0D97DC20FB745C2DEC90D000C00008945C4D96DC4DB5DC4668B45C4D96DC2EB218B87D004000083E8007411487407B8A0000000EB0CB840010000EB05B880020000F687D0030000048B0D3C6FC000668945DC740FF6415420750966D1E86640668945DCF7812050000000200000741080B9984F0000047207C745DC0F2704006A00538D4DD45651E89F18FBFF";
inline unsigned Hex(char c) {return c<='9'?unsigned(c-'0'):unsigned(c-'A'+10);}
inline bool Equal(const OwnerContext& a,const OwnerContext& b) {
    return a.owner==b.owner&&a.attack_no==b.attack_no&&a.parts==b.parts&&a.power==b.power&&a.raw_flags==b.raw_flags&&
        a.owner_flags==b.owner_flags&&a.status==b.status&&a.difficulty==b.difficulty&&
        a.attack_connected==b.attack_connected&&a.descriptor==b.descriptor;
}
}
inline OwnerCode FindOwnerCode(const unsigned char* image,size_t size,const NativeAccess& a) {
    static_assert((sizeof(owner_detail::prefix)-1)/2==0x102,"producer prefix bounds");
    if(!image||size!=a.image_size||!Complete(a)||a.code.global_slot_rva!=0x806F3C||a.code.wrapper_rva!=0x1BDE20||
       !Area(a.regions.data(),a.regions.size(),0x55BE0,0x102,true)||
       !Area(a.regions.data(),a.regions.size(),0x7581,5,true)||
       !Area(a.regions.data(),a.regions.size(),0x7F3F18,19*16,false))return {};
    for(size_t i=0;i<0x102;++i) {
        const uint32_t operand=i==7?0x835E10:i==0x46?0x7F3F18:i==0xC0?0x806F3C:0;
        if(operand) {if(U32(image+0x55BE0+i)!=uint64_t(a.image_base)+operand)return {};i+=3;continue;}
        if(image[0x55BE0+i]!=(owner_detail::Hex(owner_detail::prefix[i*2])<<4|owner_detail::Hex(owner_detail::prefix[i*2+1])))return {};
    }
    size_t target=0;
    if(!Relative(image,size,0x7581,0xE9,target)||target!=a.code.wrapper_rva)return {};
    return {true,0x55CE2};
}
template<class Memory> bool ReadOwnerOnce(Memory& memory,const NativeAccess& a,const HitEvent& e,
        uint32_t ebp,uint32_t owner,OwnerContext& out) {
    uint32_t current=0,previous=0,global=0;
    out.owner=owner;
    if(!Word(memory,ebp-0x48,out.attack_no)||!Word(memory,ebp-0x4C,out.parts)||
       !Word(memory,ebp+8,current)||!Word(memory,ebp+12,previous)||current!=e.current||previous!=e.previous||
       out.attack_no>=19||!memory.Read(e.attack,out.descriptor.data(),out.descriptor.size())||
       !Word(memory,owner+0x3D0,out.owner_flags)||!memory.Read(owner+0x7CC,&out.attack_connected,1)||
       !Word(memory,e.global+0x5020,out.status)||!memory.Read(e.global+0x4F98,&out.difficulty,1)||
       !Word(memory,a.image_base+static_cast<uint32_t>(a.code.global_slot_rva),global)||global!=e.global)return false;
    out.power=U16(out.descriptor.data()+8);out.raw_flags=U16(out.descriptor.data()+10);return true;
}
template<class Memory> bool CaptureOwnerContext(Memory& memory,const NativeAccess& a,const OwnerCode& code,
        const HitEvent& e,uint32_t ebp,uint32_t owner,int companion_filter,OwnerContext& out) {
    out={};
    if(!code.verified||code.return_rva!=0x55CE2||e.outer_return!=uint64_t(a.image_base)+code.return_rva||
       companion_filter||ebp<0x4C||!Range(ebp-0x4C,0x5C)||e.attack!=ebp-0x2C||
       !Range(owner,0x7CD)||!Range(e.global,0x5024)||
       uint64_t(a.image_base)+a.code.global_slot_rva>0xFFFFFFFFU)return false;
    OwnerContext first{},last{};
    if(!ReadOwnerOnce(memory,a,e,ebp,owner,first)||!ReadOwnerOnce(memory,a,e,ebp,owner,last)||!owner_detail::Equal(first,last))return false;
    out=first;out.valid=true;return true;
}
} // namespace link_runtime::shield

#pragma once
#include "chapter_policy.h"
#include "chapter_finder.h"
#include <array>
namespace link_runtime::chapter {
enum class Entry { selector,room_loader };
struct ReadEvent {Entry entry=Entry::selector;uint32_t caller_return=0,type=0,costume=0;};
namespace detail {
inline bool Complete(const NativeCode& c) {
    return c.verified&&c.modes_verified&&c.loader_abi_verified&&c.image_base>=0x10000U&&c.image_size>0x85f730&&
        uint64_t(c.image_base)+c.image_size<0x7fff0000U&&!(c.image_base&0xffffU)&&
        c.global_slot_rva==0x806f3c&&c.global_object_rva==0x85a760&&c.selector_rva==0x280c50&&c.loader_rva==0x2ae980&&
        c.loader_thunk_rva==0x3157&&c.room_return_rva==0x23c8f9&&c.chapter_end_rva==0x2c2c00&&c.chapter_section_rva==0x2bf7a0&&
        c.main_mode_rva==0x2d7752&&c.extra_mode_rva==0x2db2f6&&c.mercs_rva==0x2db960&&c.separate_ways_rva==0x2dc866;
}
template<class Memory> bool Word(Memory& m,uint32_t at,uint32_t& value) {
    unsigned char b[4]{};if(!m.Read(at,b,4))return false;value=U32(b);return true;
}
struct Frame {uint32_t global=0,system=0;uint8_t chapter=0;std::array<unsigned char,8> packed{};};
template<class Memory> bool ReadFrame(Memory& m,const NativeCode& c,Frame& f) {
    const uint32_t slot=c.image_base+c.global_slot_rva;uint32_t after=0;
    return Word(m,slot,f.global)&&f.global==c.image_base+c.global_object_rva&&!(f.global&7)&&
        Word(m,f.global+0x54,f.system)&&m.Read(f.global+0x4f9a,&f.chapter,1)&&m.Read(f.global+0x4fc8,f.packed.data(),8)&&
        Word(m,slot,after)&&after==f.global;
}
template<class Memory> bool Caller(Memory& m,const NativeCode& c,const ReadEvent& e) {
    if(e.caller_return!=c.image_base+c.room_return_rva)return false;
    unsigned char call[5]{},thunk[5]{};
    return m.Read(e.caller_return-5,call,5)&&call[0]==0xe8&&
        int64_t(e.caller_return)+static_cast<int32_t>(U32(call+1))==int64_t(c.image_base)+c.loader_thunk_rva&&
        m.Read(c.image_base+c.loader_thunk_rva,thunk,5)&&thunk[0]==0xe9&&
        int64_t(c.image_base)+c.loader_thunk_rva+5+static_cast<int32_t>(U32(thunk+1))==int64_t(c.image_base)+c.loader_rva;
}
}
// Memory exposes only Read. FindNative runs at startup, not in this snapshot; there is no allocation,
// process API, inventory/HP access, write, or exception suppression here. Host supplies bounded reads.
template<class Memory> bool ReadSnapshot(Memory& m,const NativeCode& c,const ReadEvent& e,ChapterSnapshot& out) {
    out={};
    if(!detail::Complete(c)||(e.entry!=Entry::selector&&e.entry!=Entry::room_loader))return false;
    if(e.entry==Entry::room_loader&&(!detail::Caller(m,c,e)||e.type>255||e.costume>255))return false;
    detail::Frame first{},last{};
    if(!detail::ReadFrame(m,c,first)||!detail::ReadFrame(m,c,last)||first.global!=last.global||first.system!=last.system||
        first.chapter!=last.chapter||first.packed!=last.packed)return false;
    if(e.entry==Entry::room_loader&&(e.type!=first.packed[0]||e.costume!=first.packed[1]))return false;
    out.valid=true;out.global=first.global;out.chapter=first.chapter;out.character=first.packed[0];out.costume=first.packed[1];
    out.subcostume=first.packed[3];out.system_flags=first.system;
    out.costume_dirty=uint64_t(detail::U32(first.packed.data()))|(uint64_t(detail::U32(first.packed.data()+4))<<32);
    // UHD title route tests attributed by the finder: sign bit = Assignment Ada, 40000000 = Mercs.
    // Separate Ways is type2/sub2/chapter20+. Character/special/unknown states cannot qualify.
    // 1000 is only the native selector bypass; it has no inferred Mercenaries meaning.
    out.normal_campaign=!(out.system_flags&0xc0000000U)&&out.character==0&&out.subcostume==0&&out.costume<=3&&out.chapter<=18;
    return true;
}
}

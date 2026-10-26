#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <initializer_list>
#include "foreign_patch.h"
namespace link_runtime::chapter {
struct NativeCode {
    bool verified=false,modes_verified=false,loader_abi_verified=false;
    uint32_t image_base=0,image_size=0;
    uint32_t global_slot_rva=0,global_object_rva=0;
    uint32_t selector_rva=0,loader_rva=0,loader_thunk_rva=0,room_return_rva=0;
    uint32_t chapter_end_rva=0,chapter_section_rva=0;
    uint32_t main_mode_rva=0,extra_mode_rva=0,mercs_rva=0,separate_ways_rva=0;
    // re4_tweaks costume MakeInline windows accepted (docs/fps-coexistence-2026-10-12.md 2b).
    unsigned foreign_windows=0;uint32_t foreign_site_rva[3]={},foreign_target[3]={};
};
namespace detail {
inline uint32_t U32(const unsigned char* p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline unsigned U16(const unsigned char* p) {return unsigned(p[0])|(unsigned(p[1])<<8);}
struct Region {size_t at,n;uint32_t flags;};
inline bool Area(const std::vector<Region>& r,size_t at,size_t n,bool code) {
    for(const auto& s:r)if(at>=s.at&&n<=s.n&&at-s.at<=s.n-n&&
        (code?(s.flags&0x20000000U)&&!(s.flags&0x80000000U):(s.flags&0x80000000U)&&!(s.flags&0x20000000U)))return true;
    return false;
}
inline bool Match(const unsigned char* p,std::initializer_list<int> pattern) {
    size_t i=0;for(int x:pattern) {if(x>=0&&p[i]!=x)return false;++i;}return true;
}
inline size_t Unique(const unsigned char* d,const std::vector<Region>& r,std::initializer_list<int> pattern) {
    size_t hit=0;unsigned count=0;
    for(const auto& s:r)if((s.flags&0x20000000U)&&!(s.flags&0x80000000U)&&s.n>=pattern.size())
        for(size_t i=0;i<=s.n-pattern.size();++i)if(d[s.at+i]==*pattern.begin()&&Match(d+s.at+i,pattern)) {hit=s.at+i;if(++count>1)return 0;}
    return count==1?hit:0;
}
inline bool Pe(const unsigned char* d,size_t size,std::vector<Region>& r,std::vector<size_t>& relocs) {
    if(!d||size<64||size>64*1024*1024||U16(d)!=0x5a4d)return false;
    const size_t pe=U32(d+0x3c);
    if(pe<64||pe>size||size-pe<24||U32(d+pe)!=0x4550||U16(d+pe+4)!=0x14c)return false;
    const unsigned count=U16(d+pe+6),optional=U16(d+pe+20);const size_t opt=pe+24;
    if(!count||count>=96||optional<144||optional>size-opt||U16(d+opt)!=0x10b||U32(d+opt+56)!=size||U32(d+opt+92)<6)return false;
    const size_t first=opt+optional;
    if(size-first<size_t(count)*40)return false;
    for(unsigned i=0;i<count;++i) {
        const auto* s=d+first+size_t(i)*40;Region next{U32(s+12),U32(s+8),U32(s+36)};
        if(next.at>=size||next.n>size-next.at)return false;
        for(const auto& prev:r)if(next.n&&prev.n&&next.at<prev.at+prev.n&&prev.at<next.at+next.n)return false;
        r.push_back(next);
    }
    const size_t start=U32(d+opt+136),n=U32(d+opt+140);
    if(start>=size||n<8||n>size-start)return false;
    size_t cursor=start;
    while(cursor<start+n) {
        if(start+n-cursor<8)return false;
        const size_t page=U32(d+cursor),block=U32(d+cursor+4);
        if(block<8||(block&1)||block>start+n-cursor)return false;
        for(size_t at=cursor+8;at<cursor+block;at+=2) {
            const unsigned entry=U16(d+at),type=entry>>12;if(!type)continue;
            const size_t target=page+(entry&4095);
            if(type!=3||target>=size||size-target<4)return false;
            relocs.push_back(target);
        }
        cursor+=block;
    }
    std::sort(relocs.begin(),relocs.end());
    for(size_t i=1;i<relocs.size();++i)if(relocs[i]<relocs[i-1]+4)return false;
    return !relocs.empty();
}
// Fingerprints cover the complete attributed bodies, including all loader exits. Each HIGHLOW
// operand is normalized to an RVA before FNV-1a, allowing real ASLR without masking instructions.
// This is a compatibility check, not a cryptographic trust boundary; source SHA256 is in the report.
inline bool Fingerprint(const unsigned char* d,const std::vector<Region>& r,const std::vector<size_t>& relocs,
                        uint32_t base,size_t at,size_t n,uint64_t expected) {
    if(!Area(r,at,n,true))return false;
    auto it=std::lower_bound(relocs.begin(),relocs.end(),at);
    if(it!=relocs.begin()&&*(it-1)+4>at)return false;
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=at;i<at+n;) {
        if(it!=relocs.end()&&*it==i) {
            if(at+n-i<4)return false;
            const uint32_t relative=U32(d+i)-base;
            for(unsigned b=0;b<4;++b)hash=(hash^uint8_t(relative>>(8*b)))*1099511628211ULL;
            i+=4;++it;
        } else hash=(hash^d[i++])*1099511628211ULL;
    }
    return hash==expected;
}
// Mode bodies that re4_tweaks' costume hooks patch with MakeInline (E8 rel32 + 90 90, 7 bytes:
// a CALL into its reg_pack wrapper, which returns after the window) whatever OverrideCostumes
// says. The window holds either the original bytes (full fingerprint) or exactly that form
// calling into another module's code (probe); the rest of the body is
// hashed in both cases (`hole`: FNV-1a of the body without the 7 window bytes). No reloc may
// touch the window. These bodies only identify the game mode: we never hook them.
inline bool WindowedFingerprint(const unsigned char* d,size_t size,const std::vector<Region>& r,const std::vector<size_t>& relocs,
                                uint32_t base,size_t at,size_t n,uint64_t full,size_t window,uint64_t hole,
                                const foreign::Probe* probe,uint32_t& target) {
    target=0;
    if(window>n||n-window<7||!Area(r,at,n,true))return false;
    const size_t w=at+window;
    uint32_t jump=0;
    if(!foreign::InlineCall(d+w,2,base+static_cast<uint32_t>(w),base,static_cast<uint32_t>(size),jump)) {
        if(d[w]==0xe8||d[w]==0xe9)return false;  // a call/jump into the image is never accepted
        return Fingerprint(d,r,relocs,base,at,n,full);
    }
    if(!foreign::Accept(probe,jump))return false;
    auto it=std::lower_bound(relocs.begin(),relocs.end(),at);
    if(it!=relocs.begin()&&*(it-1)+4>at)return false;
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=at;i<at+n;) {
        if(i==w) {
            if(it!=relocs.end()&&*it<w+7)return false;
            i+=7;continue;
        }
        if(it!=relocs.end()&&*it==i) {
            if(at+n-i<4||(i<w&&i+4>w))return false;
            const uint32_t relative=U32(d+i)-base;
            for(unsigned b=0;b<4;++b)hash=(hash^uint8_t(relative>>(8*b)))*1099511628211ULL;
            i+=4;++it;
        } else hash=(hash^d[i++])*1099511628211ULL;
    }
    if(hash!=hole)return false;
    target=jump;return true;
}
inline bool Relative(const unsigned char* d,size_t size,size_t at,unsigned char opcode,size_t& out) {
    if(at>=size||size-at<5||d[at]!=opcode)return false;
    const int64_t target=int64_t(at)+5+static_cast<int32_t>(U32(d+at+1));
    if(target<0||uint64_t(target)>=size)return false;out=static_cast<size_t>(target);return true;
}
}
inline NativeCode FindNative(const unsigned char* d,size_t size,uint32_t base,const foreign::Probe* probe=nullptr) {
    using namespace detail;
    if(base<0x10000U||uint64_t(base)+size>=0x7fff0000U)return {};
    std::vector<Region> regions;std::vector<size_t> relocs;
    if(!Pe(d,size,regions,relocs))return {};
    const size_t end=Unique(d,regions,{0x55,0x8b,0xec,0x83,0xec,0x4c,0x53,0x57,0x8d,0x4d,0xb4,0xe8,-1,-1,-1,-1,
        0x0f,0xb6,0x05,-1,-1,-1,-1,0x8b,0x0d,-1,-1,-1,-1,0xfe,0xc0,0x88,0x81,-1,-1,-1,-1,0xa1});
    const size_t selector=Unique(d,regions,{0xa1,-1,-1,-1,-1,0x8b,0x48,0x54,0x85,0xc9,0x79,0x07,0x8a,0x80,-1,-1,-1,-1,0xc3,
        0xf7,0xc1,0,0,0,0x40,0x75,0xf1,0xf7,0xc1,0,0x10,0,0,0x75,0xe9,0x0f,0xb6,0x88});
    const size_t section=Unique(d,regions,{0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xf8,0x18,0x0f,0x87,-1,-1,-1,-1,0xff,0x24,0x85});
    const size_t loader=Unique(d,regions,{0x55,0x8b,0xec,0x81,0xec,0x20,0x02,0,0,0xa1,-1,-1,-1,-1,0x33,0xc5,0x89,0x45,0xfc,
        0xa1,-1,-1,-1,-1,0xf6,0x80,-1,-1,-1,-1,0x01,0x0f,0x84,-1,-1,-1,-1,0x56,0x57,0xb9,0xfe,0xff,0,0,0x66,0x21,0x88});
    const size_t room=Unique(d,regions,{0xa1,-1,-1,-1,-1,0x0f,0xb6,0x88,0xc9,0x4f,0,0,0x0f,0xb6,0x90,0xc8,0x4f,0,0,
        0x56,0x51,0x52,0xe8,-1,-1,-1,-1,0x83,0xc4,0x08});
    const size_t main=Unique(d,regions,{0xa1,-1,-1,-1,-1,0x8b,0x48,0x54,0x56,0x8b,0x75,0x08,0x85,0xc9,0x79,0x6f});
    const size_t extra=Unique(d,regions,{0x8b,0x0d,-1,-1,-1,-1,0x8b,0x41,0x54,0x85,0xc0,0x79,0x6f,0x6a,0,0x6a,0x5a,0x6a,0,0xc6,0x06,0x07});
    const size_t mercs=Unique(d,regions,{0x33,0xc9,0x0f,0xbe,0xc0,0x83,0xf8,0x04,0x77,0x63,0xff,0x24,0x85,-1,-1,-1,-1,0xa1,
        -1,-1,-1,-1,0x88,0x88,0xc8,0x4f,0,0,0x8b,0x15});
    const size_t sw=Unique(d,regions,{0x8b,0x15,-1,-1,-1,-1,0xc6,0x82,0xc8,0x4f,0,0,0x02,0xa1,-1,-1,-1,-1,
        0xc6,0x80,0xcb,0x4f,0,0,0x02,0x8b,0x0d});
    if(!end||!selector||!section||!loader||!room||!main||!extra||!mercs||!sw||
        !Area(regions,end,0x2d,true)||!Area(regions,section,0x16,true))return {};
    uint32_t targets[3]={};
    if(!Fingerprint(d,regions,relocs,base,selector,0xda,0xa8f748c7e2007267ULL)||
        !Fingerprint(d,regions,relocs,base,loader,0x41c,0xf2d55ec24f289e19ULL)||
        !Fingerprint(d,regions,relocs,base,room,0x1e,0x64aa8d42bb85ab87ULL)||
        !Fingerprint(d,regions,relocs,base,main,0x349,0x37ae331b1798369cULL)||
        !WindowedFingerprint(d,size,regions,relocs,base,extra,0x87,0xb941ac1dbea7927cULL,0x39,0x87e27ed65f4eefb9ULL,probe,targets[0])||
        !WindowedFingerprint(d,size,regions,relocs,base,mercs,0x7d,0x6c55e339d5275e83ULL,0x71,0x2921026195c3be5cULL,probe,targets[1])||
        !WindowedFingerprint(d,size,regions,relocs,base,sw,0x38,0x96bbbff6c34e6163ULL,0x22,0x3a1dfa9906c515fbULL,probe,targets[2]))return {};
    const uint32_t global=U32(d+selector+1),source=U32(d+end+0x13);
    // The chapter producer must read SceSys.m_chapter_no, not merely any writable byte.
    // Compare its RVA so a genuine HIGHLOW relocation preserves this attribution.
    if(global<base||source<base||source-base!=0x867c10||!Area(regions,global-base,4,false)||!Area(regions,source-base,1,false)||
        U32(d+end+0x19)!=global||U32(d+loader+0x14)!=global||U32(d+end+0x21)!=0x4f9a||
        U32(d+selector+0xe)!=0x4fc9||U32(d+selector+0x26)!=0x4fc8||U32(d+loader+0x1a)!=0x4fce)return {};
    const uint32_t object=U32(d+global-base);
    if(object<base||(object&7)||object-base!=0x85a760||!Area(regions,object-base,0x4fd0,false))return {};
    const uint32_t table=U32(d+section+0x12);
    if(table<base||!Area(regions,table-base,25*4,true))return {};
    const uint32_t c13=U32(d+table-base+13*4),c14=U32(d+table-base+14*4);
    if(c13<base||c14<base||!Area(regions,c13-base,20,true)||!Area(regions,c14-base,20,true)||
        !Match(d+c13-base,{0x8b,0x55,0x0c,0x8b,0x45,0x10,0xc7,0x02,0x04,0,0,0,0xc7,0,0x04,0,0,0,0x5d,0xc3})||
        !Match(d+c14-base,{0x8b,0x4d,0x0c,0x8b,0x55,0x10,0xc7,0x01,0x05,0,0,0,0xc7,0x02,0x01,0,0,0,0x5d,0xc3}))return {};
    size_t thunk=0,target=0;
    if(!Relative(d,size,room+22,0xe8,thunk)||!Area(regions,thunk,5,true)||!Relative(d,size,thunk,0xe9,target)||target!=loader||
        room+27!=0x23c8f9||(selector&15)||(loader&15))return {};
    NativeCode c;c.verified=c.modes_verified=c.loader_abi_verified=true;c.image_base=base;c.image_size=static_cast<uint32_t>(size);
    c.global_slot_rva=global-base;c.global_object_rva=object-base;c.selector_rva=static_cast<uint32_t>(selector);c.loader_rva=static_cast<uint32_t>(loader);
    c.loader_thunk_rva=static_cast<uint32_t>(thunk);c.room_return_rva=static_cast<uint32_t>(room+27);
    c.chapter_end_rva=static_cast<uint32_t>(end);c.chapter_section_rva=static_cast<uint32_t>(section);
    const size_t sites[3]={extra+0x39,mercs+0x71,sw+0x22};
    for(unsigned i=0;i<3;++i)if(targets[i]) {
        c.foreign_site_rva[c.foreign_windows]=static_cast<uint32_t>(sites[i]);c.foreign_target[c.foreign_windows]=targets[i];++c.foreign_windows;}
    c.main_mode_rva=static_cast<uint32_t>(main);c.extra_mode_rva=static_cast<uint32_t>(extra);c.mercs_rva=static_cast<uint32_t>(mercs);c.separate_ways_rva=static_cast<uint32_t>(sw);
    return c;
}
}

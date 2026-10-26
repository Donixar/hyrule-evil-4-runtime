#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <initializer_list>
#include <utility>
#include "foreign_patch.h"

// Isolated PE/file inspection only. No Win32, process reads, hook or installation.
namespace link_runtime::shield {
struct Region { size_t begin=0,span=0; bool executable=false,writable=false; };
struct NativeCode {
    bool verified=false;
    size_t classifier_rva=0,wrapper_rva=0,classifier_return_rva=0;
    size_t helper_rva=0,helper_return_rva=0,key_check_rva=0;
    size_t player_slot_rva=0,global_slot_rva=0,input_rva=0;
    size_t knife_dispatch_rva=0,knife_move_rva=0,knife_set_rva=0,knife_ready_rva=0,knife_fire_rva=0,knife_down_rva=0;
    // re4_tweaks coexistence (docs/fps-coexistence-2026-10-12.md 2a): key_site's E8 redirected
    // out of the image (MouseTurning KeyOnCheck_hook); KeyOnCheck resolved through key_anchor.
    bool key_site_foreign=false;size_t key_site_rva=0,key_anchor_rva=0;uint32_t key_site_target=0;
};
using ForeignProbe=foreign::Probe;  // see foreign_patch.h
inline uint32_t U32(const unsigned char* p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
inline unsigned U16(const unsigned char* p) { return unsigned(p[0])|(unsigned(p[1])<<8); }
inline bool Fits(const Region& r,size_t at,size_t n) { return at>=r.begin&&n<=r.span&&at-r.begin<=r.span-n; }
inline bool Area(const Region* regions,size_t count,size_t at,size_t n,bool code) {
    for(size_t i=0;i<count;++i)if(Fits(regions[i],at,n)&&(code?regions[i].executable:regions[i].writable&&!regions[i].executable))return true;
    return false;
}
inline bool MappedRegions(const unsigned char* image,size_t size,uint32_t& base,std::vector<Region>& out) {
    out.clear();base=0;
    if(!image||size<64||size>64*1024*1024||U16(image)!=0x5A4D)return false;
    const size_t pe=U32(image+0x3C);
    if(pe<64||pe>size||size-pe<24||U32(image+pe)!=0x4550||U16(image+pe+4)!=0x14C)return false;
    const unsigned count=U16(image+pe+6),optional=U16(image+pe+20);
    if(!count||count>=96||optional<96||optional>size-pe-24)return false;
    const size_t opt=pe+24,first=opt+optional;
    if(U16(image+opt)!=0x10B||U32(image+opt+56)!=size||size-first<size_t(count)*40)return false;
    std::vector<Region> parsed;
    for(unsigned i=0;i<count;++i) {
        const auto* s=image+first+size_t(i)*40;
        const size_t begin=U32(s+12),span=U32(s+8);const uint32_t flags=U32(s+36);
        if(begin>=size||span>size-begin)return false;
        for(const auto& r:parsed)if(span&&r.span&&begin<r.begin+r.span&&r.begin<begin+span)return false;
        parsed.push_back({begin,span,(flags&0x20000000U)!=0,(flags&0x80000000U)!=0});
    }
    base=U32(image+opt+28);out=std::move(parsed);return true;
}
inline bool Match(const unsigned char* p,std::initializer_list<int> pattern) {
    size_t k=0;for(int x:pattern){if(x>=0&&p[k]!=x)return false;++k;}return true;
}
inline size_t Unique(const unsigned char* image,const Region* r,size_t count,std::initializer_list<int> pattern) {
    size_t found=0;unsigned matches=0;
    for(size_t s=0;s<count;++s)if(r[s].executable&&r[s].span>=pattern.size())
        for(size_t i=0;i<=r[s].span-pattern.size();++i)
            if(image[r[s].begin+i]==*pattern.begin()&&Match(image+r[s].begin+i,pattern)){++matches;found=r[s].begin+i;}
    return matches==1?found:0; // Count every prefix, including malformed candidates in another section.
}
inline bool Relative(const unsigned char* image,size_t size,size_t at,unsigned char opcode,size_t& target) {
    if(at>=size||size-at<5||image[at]!=opcode)return false;
    const int64_t next=int64_t(at)+5+static_cast<int32_t>(U32(image+at+1));
    if(next<0||uint64_t(next)>=size)return false;target=static_cast<size_t>(next);return true;
}
inline bool DataRva(const unsigned char* p,uint32_t base,const Region* r,size_t count,size_t n,size_t& result) {
    const uint32_t address=U32(p);if(address<base)return false;
    result=address-base;return Area(r,count,result,n,false);
}
inline NativeCode FindNative(const unsigned char* image,size_t size,uint32_t base,const Region* r,size_t count,const ForeignProbe* probe=nullptr) {
    NativeCode result;
    if(!image||!r||!count||uint64_t(base)+size>0xFFFFFFFFU)return result;
    for(size_t s=0;s<count;++s)if(r[s].begin>=size||r[s].span>size-r[s].begin)return result;
    const size_t classifier=Unique(image,r,count,{0x55,0x8B,0xEC,0x83,0xEC,0x20,0xA1,-1,-1,-1,-1,0x33,0xC5,
        0x89,0x45,0xFC,0xA1,-1,-1,-1,-1,0x66,0x83,0xB8,0xB4,0x4F,0,0,0,0x53,0x8B,0x5D,0x08,0x56,0x8B,0x75,0x0C,0x57,0x8B,0x7D,0x10,0x7F,0x13});
    const size_t wrapper=Unique(image,r,count,{0x55,0x8B,0xEC,0x8B,0x45,0x10,0x8B,0x4D,0x0C,0x53,0x56,0x8B,0x75,0x08,0x57,0x50,0x51,0x56,0x33,0xFF,0xE8});
    const size_t helper=Unique(image,r,count,{0x55,0x8B,0xEC,0x81,0xEC,0xB8,0,0,0,0xA1,-1,-1,-1,-1,0x33,0xC5,0x89,0x45,0xFC,0x53,0x8B,0x5D,0x08,0xD9,0x43,0x04,0x56});
    // On/trg/repeat bodies share a prefix. Resolve ON through its unique official SDK call site.
    const size_t key_site=Unique(image,r,count,{0xE8,-1,-1,-1,-1,0x83,0xC4,0x08,0x84,0xC0,0x74,-1,0xC7,0x86,-1,-1,-1,-1,-1,-1,-1,-1,0x5E,0xB8,-1,-1,-1,-1,0x5B,0x8B,0xE5,0x5D,0xC3,0x53});
    size_t key_thunk=0,key=0,call=key_site,anchor=0;bool redirected=false;uint32_t foreign_target=0;
    if(!key_site||image[key_site]!=0xE8)return {};
    {
        const int64_t next=int64_t(key_site)+5+static_cast<int32_t>(U32(image+key_site+1));
        if(next<0||uint64_t(next)>=size) {
            // Out of the image: only re4_tweaks' redirected call, only into another module's code.
            foreign_target=static_cast<uint32_t>(uint64_t(base)+uint64_t(next));
            if(!foreign::Accept(probe,foreign_target))return {};
            anchor=Unique(image,r,count,{0xE8,-1,-1,-1,-1,0x83,0xC4,0x08,0x84,0xC0,0x0F,0x84,0x9F,0x03,0,0,0x6A,0xFF,0x68,0xFF,0xFF,0xF7,0xFF});
            if(!anchor)return {};
            call=anchor;redirected=true;
        }
    }
    if(!Relative(image,size,call,0xE8,key_thunk)||!Area(r,count,key_thunk,5,true)||
        !Relative(image,size,key_thunk,0xE9,key)||!Area(r,count,key,32,true)||
        !Match(image+key,{0x55,0x8B,0xEC,0xA1,-1,-1,-1,-1,0x8B,0x0D,-1,-1,-1,-1,0x23,0x45,0x08,0x23,0x4D,0x0C,0x0B,0xC1,0x74,0x04,0xB0,0x01,0x5D,0xC3,0x32,0xC0,0x5D,0xC3}))return {};
    // The foreign form is pinned to the qualified UHD 1.1.0 layout (thunk 0x1357 -> 0x289C0).
    if(redirected&&(key_thunk!=0x1357||key!=0x289C0))return {};
    const size_t move=Unique(image,r,count,{0x55,0x8B,0xEC,0x56,0x8B,0x75,0x08,0x8B,0x86,0xD8,0x07,0,0,0xC6,0x86,0x20,0x03,0,0,0,0x80,0x78,0x42,0,0x7E,0x04,0xC6,0x40,0x42,0x02,0x0F,0xB6,0x86,0xFE,0,0,0,0x8B,0x0C,0x85});
    const size_t set=Unique(image,r,count,{0x55,0x8B,0xEC,0x53,0x56,0x8B,0x75,0x08,0x0F,0xB6,0x86,0xFF,0,0,0,0x8B,0x0C,0x85,-1,-1,-1,-1,0x56,0xFF,0xD1,0x83,0xC4,0x04,0x33,0xDB,0x80,0xBE,0xFF,0,0,0,0x04});
    const size_t dispatch=Unique(image,r,count,{0x55,0x8B,0xEC,0x8B,0x45,0x08,0x80,0xB8,0x70,0x05,0,0,0,0x75,0x09,
        0x33,0xC9,0x66,0x89,0x88,0x72,0x05,0,0,0x0F,0xB6,0x90,0xFD,0,0,0,0xC6,0x80,0x70,0x05,0,0,0,0x50,
        0x8B,0x04,0x95,-1,-1,-1,-1,0xFF,0xD0,0x83,0xC4,0x04,0x5D,0xC3});
    if(!classifier||!wrapper||!helper||!key||!move||!set||!dispatch||!Area(r,count,classifier,0x19E,true)||
        !Area(r,count,wrapper,0xB2,true)||!Area(r,count,helper,0x53C,true)||!Area(r,count,set,0x147,true)||!Area(r,count,move,0x30,true))return {};
    for(size_t at:{classifier,wrapper,helper,key,move,set,dispatch})if(at&15)return {};
    size_t thunk=0,target=0;
    if(!Relative(image,size,wrapper+20,0xE8,thunk)||!Area(r,count,thunk,5,true)||
        !Relative(image,size,thunk,0xE9,target)||target!=classifier||
        !Relative(image,size,classifier+0x91,0xE8,target)||target!=helper)return {};
    if(image[wrapper+0xB1]!=0xC3||image[classifier+0x19D]!=0xC3||image[helper+0x3EA]!=0xC3||
        !Match(image+classifier+0x99,{0x85,0xC0,0x74,0x8E})||
        !Match(image+classifier+0xA2,{0x66,0x21,0x48,0x24})||
        !Match(image+classifier+0xAC,{0x89,0x82,0x40,0x03,0,0})||
        !Match(image+set+0xB2,{0xC7,0x86,0xFC,0,0,0,0,0x0B,0x03,0}))return {};
    size_t player=0,global=0,input=0,table=0,top_table=0;
    if(!DataRva(image+classifier+17,base,r,count,4,global)||!DataRva(image+classifier+0x40,base,r,count,4,player)||
        !DataRva(image+key+4,base,r,count,16,input)||U32(image+key+10)!=uint64_t(base)+input+4||
        U32(image+classifier+0xA8)!=uint64_t(base)+player)return {};
    if(redirected&&input!=0x862F08)return {};  // parry_input_runtime pins the same logical input
    // The top native action dispatcher reads FD and index 0xB must call this knife move body.
    if(!DataRva(image+dispatch+42,base,r,count,12*4,top_table))return {};
    const uint32_t top_entry=U32(image+top_table+11*4);
    if(top_entry<base||!Area(r,count,top_entry-base,5,true)||
        !Relative(image,size,top_entry-base,0xE9,target)||target!=move)return {};
    const uint32_t absolute=U32(image+move+40);if(absolute<base)return {};table=absolute-base;
    if(!Area(r,count,table,16,false)||U32(image+table+4)!=uint64_t(base)+set)return {};
    size_t phases[4]={};
    for(unsigned i=0;i<4;++i) {
        const uint32_t address=U32(image+table+i*4);if(address<base)return {};phases[i]=address-base;
        if(!Area(r,count,phases[i],3,true)||!Match(image+phases[i],{0x55,0x8B,0xEC}))return {};
    }
    result.verified=true;result.classifier_rva=classifier;result.wrapper_rva=wrapper;result.classifier_return_rva=wrapper+25;
    result.helper_rva=helper;result.helper_return_rva=classifier+0x96;result.key_check_rva=key;
    result.key_site_foreign=redirected;result.key_site_rva=key_site;result.key_anchor_rva=anchor;result.key_site_target=foreign_target;
    result.player_slot_rva=player;result.global_slot_rva=global;result.input_rva=input;
    result.knife_dispatch_rva=dispatch;result.knife_move_rva=move;result.knife_set_rva=set;result.knife_ready_rva=phases[0];result.knife_fire_rva=phases[2];result.knife_down_rva=phases[3];
    return result;
}
} // namespace link_runtime::shield

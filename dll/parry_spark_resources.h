#pragma once
#include <cstdint>
#include <cstring>
namespace link_runtime::parry::spark {
struct Resources {uint32_t values[2]{};};
template<class Memory> bool ResourcesNow(Memory& memory,uint32_t slot,Resources& out){
    out={};uint32_t manager=0,again=0,table[3]{},after[3]{},count=0;
    auto word=[&](uint32_t p,uint32_t& value){return p>=0x10000&&p<=0xfffffffb&&memory.Read(p,&value,4);};
    if(!word(slot,manager)||manager<0x10000||manager>0xffff97f3||
       !memory.Read(manager+0x6800,table,sizeof(table))||table[2]==0xd6||
       !word(table[1],count)||!count||count>4096||table[1]>0xffffffff-count*8-4||
       table[0]<0x10000||table[0]>0xffffffff-count*4-4)return false;
    Resources found;
    for(uint32_t index=0;index<count;++index){
        uint16_t id=0;
        if(!memory.Read(table[1]+4+8*index,&id,2))return false;
        if(id!=0x1f&&id!=0x20)continue;
        const unsigned which=id-0x1f;if(found.values[which])continue;
        uint32_t offset=0;
        if(!word(table[0]+4+4*index,offset)||offset<4+count*4||offset>64*1024*1024||
           table[0]>0xffffffff-offset-40)return false;
        const uint32_t resource=table[0]+offset;unsigned char header[40]{};
        if(!memory.Read(resource,header,sizeof(header))||!(header[0]|header[1]))return false;
        found.values[which]=resource;
    }
    uint32_t count_again=0;
    if(!found.values[0]||!found.values[1]||!word(slot,again)||again!=manager||
       !memory.Read(manager+0x6800,after,sizeof(after))||std::memcmp(table,after,sizeof(table))||
       !word(table[1],count_again)||count_again!=count)return false;
    out=found;return true;
}
}

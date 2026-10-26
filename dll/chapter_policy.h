#pragma once
#include <cstdint>
namespace link_runtime::chapter {
struct ChapterSnapshot {
    bool valid=false,normal_campaign=false;
    uint32_t global=0;
    uint8_t chapter=0,character=0,costume=0,subcostume=0;
    uint32_t system_flags=0;
    uint64_t costume_dirty=0;
};
struct ChapterPlan { bool apply=false; uint8_t costume=0; uint64_t expected=0,desired=0; };
// Costume stage of a RE4 chapter byte (0 = 1-1 ... 6 = 3-1, 13 = 4-4, 14 = 5-1, 18 = Final).
//   0 pl00 (wood + Ordon shield), 1 pl08 (green tunic, Ordon sword, Hylian shield),
//   3 pl10 (green tunic, Master Sword + its sheath, Hylian shield; only with master_from_castle,
//     chapters 6..13 = the castle), 2 pl09 (Magic Armor, Master Sword).
// pl10 is the native Special 1 costume. The game's Special 1 also sets Ashley's costume
// (subcostume 1), which stays excluded; Leon costume 3 with subcostume 0 is this progression's own
// stage, so a save made in it is managed in both directions (also when the flag is turned off).
inline uint8_t StageCostume(uint8_t chapter,bool master_from_castle) {
    return chapter==0?0:chapter<6?1:chapter<14?(master_from_castle?3:1):2;
}
inline ChapterPlan PlanChapter(const ChapterSnapshot& s,bool enabled,bool master_from_castle=false) {
    ChapterPlan p{false,s.costume,s.costume_dirty,s.costume_dirty};
    if(!enabled||!s.valid||!s.normal_campaign||s.global<0x10000U||s.global>=0x7FFE0000U||(s.global&7)||
        s.chapter>18||s.character!=0||s.costume>3||s.subcostume!=0||(s.system_flags&0xC0001000U)||
        uint8_t(s.costume_dirty)!=s.character||uint8_t(s.costume_dirty>>8)!=s.costume||
        uint8_t(s.costume_dirty>>24)!=s.subcostume)return p;
    p.costume=StageCostume(s.chapter,master_from_castle);
    p.apply=p.costume!=s.costume;
    if(p.apply)p.desired=(p.expected&~(uint64_t(0xFF)<<8))|(uint64_t(p.costume)<<8)|(uint64_t(1)<<48);
    return p;
}
}

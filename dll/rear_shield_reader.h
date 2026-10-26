#pragma once
#include "shield_reader.h"
#include "rear_shield_active.h"

namespace link_runtime::rear_shield {
struct PoseCopy {
    bool stable=false,transform_candidate=false;
    uint32_t player=0,global=0,frame=0,character=0,room=0,model_flags=0;
    uint32_t part0=0,part2=0,info0=0,resource=0,info_flags=0;
    unsigned char parts=0;
    std::array<unsigned char,80> header{};
    std::array<unsigned char,48> body0{},bone2{},inverse_bind2{},info_matrix{};
};
inline bool Equal(const PoseCopy& a,const PoseCopy& b){
    return a.player==b.player&&a.global==b.global&&a.frame==b.frame&&a.character==b.character&&a.room==b.room&&
        a.model_flags==b.model_flags&&a.part0==b.part0&&a.part2==b.part2&&a.info0==b.info0&&a.resource==b.resource&&
        a.info_flags==b.info_flags&&a.parts==b.parts&&a.header==b.header&&a.body0==b.body0&&a.bone2==b.bone2&&
        a.inverse_bind2==b.inverse_bind2&&a.info_matrix==b.info_matrix;
}
inline parry::sword::Mat34 Matrix(const std::array<unsigned char,48>& raw){
    parry::sword::Mat34 m{};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<4;++j)m.v[i][j]=shield::Float(raw.data()+4*(4*i+j));
    return m;
}
inline bool CandidateTransform(const PoseCopy& p,parry::sword::Mat34& world){
    // Same attributed UHD skin composition used by the sword reader, but the
    // body/node0/bone2 binding is ONLY a candidate until checked on this mesh.
    return parry::sword::WorldTransform({Matrix(p.body0),Matrix(p.info_matrix),Matrix(p.bone2),Matrix(p.inverse_bind2)},world);
}
template<class Memory> bool ReadPoseOnce(Memory& memory,const shield::NativeAccess& access,const shield::HitEvent& e,PoseCopy& p){
    using shield::Word;using shield::Bytes;using shield::Range;
    p.player=e.player;p.global=e.global;
    uint32_t player=0,global=0;
    if(!Range(e.player,0x160)||!Range(e.global,0x5310)||
       !Word(memory,e.global+0x530C,p.frame)||!Word(memory,e.global+0x4FC8,p.character)||
       !Word(memory,e.global+0x4FAC,p.room)||!Word(memory,e.player+4,p.model_flags)||
       !memory.Read(e.player+0x102,&p.parts,1)||p.parts<3||p.parts>128||
       !Word(memory,e.player+0xF4,p.part0)||!Range(p.part0,0x128))return false;
    uint32_t part1=0;
    if(!Word(memory,p.part0+0xF4,part1)||!Range(part1,0x128)||part1==p.part0||
       !Word(memory,part1+0xF4,p.part2)||!Range(p.part2,0x128)||p.part2==part1||p.part2==p.part0||
       !Word(memory,e.player+0x15C,p.info0)||!Range(p.info0,0x8C)||
       !Word(memory,p.info0+4,p.info_flags)||!Word(memory,p.info0+0xC,p.resource)||!Range(p.resource,80)||
       !Bytes(memory,p.resource,p.header)||!Bytes(memory,p.part0+0xC,p.body0)||!Bytes(memory,p.part2+0xC,p.bone2)||
       !Bytes(memory,p.part2+0xF8,p.inverse_bind2)||!Bytes(memory,p.info0+0x5C,p.info_matrix)||
       !Word(memory,access.image_base+static_cast<uint32_t>(access.code.player_slot_rva),player)||player!=e.player||
       !Word(memory,access.image_base+static_cast<uint32_t>(access.code.global_slot_rva),global)||global!=e.global)return false;
    uint32_t end=0;return Word(memory,e.global+0x530C,end)&&end==p.frame;
}
template<class Memory> bool ReadPose(Memory& memory,const shield::NativeAccess& access,const shield::HitEvent& e,PoseCopy& out){
    out={};if(!shield::Complete(access))return false;
    PoseCopy first{},last{};
    if(!ReadPoseOnce(memory,access,e,first)||!ReadPoseOnce(memory,access,e,last)||!Equal(first,last))return false;
    first.stable=true;parry::sword::Mat34 world{};first.transform_candidate=CandidateTransform(first,world);
    out=first;return true;
}
inline Proof ProductionProof(const PoseCopy& p,const shield::Frame& frame){
    Proof proof;
    proof.contact.stable=p.stable&&p.player==frame.player&&p.global==frame.global&&p.character==frame.character;
    proof.contact.gameplay=shield::Gameplay(frame);
    // CLOSED PRODUCTION GATE: no current body-resource digest, conservative
    // silhouette profile, synchronized final bone2 epoch or native body contact
    // time has been qualified. Costume and candidate matrices never set these.
    // No INI waiver exists. Add an evidenced model/route reader before enabling.
    return proof;
}
}

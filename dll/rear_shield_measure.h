#pragma once
#include "rear_shield_reader.h"

// Owned read-only copies. None of this module is an active defense proof.
// Native classifier consumes a current sphere, not a continuous body sweep.
namespace link_runtime::rear_shield::measure {
using shield::Word;using shield::Bytes;using shield::Range;
struct Route {
    bool stable=false,chain=false;
    uint32_t owner=0,caller=0,handler=0,weapon=0,attack_no=0,parts=0;
    std::array<unsigned char,4> routines{};
    std::array<unsigned char,2> actor{};
    unsigned char weapon_type=0,sequence=0;
};
inline bool Equal(const Route& a,const Route& b){
    return a.owner==b.owner&&a.caller==b.caller&&a.handler==b.handler&&a.weapon==b.weapon&&
        a.attack_no==b.attack_no&&a.parts==b.parts&&a.routines==b.routines&&a.actor==b.actor&&
        a.weapon_type==b.weapon_type&&a.sequence==b.sequence&&a.chain==b.chain;
}
inline bool Stack(uint32_t at,size_t n,uint32_t low,uint32_t high){return Range(at,n)&&at>=low&&uint64_t(at)+n<=high;}
inline bool SpatialCaller(uint32_t rva){
    switch(rva){case 0x6EBE4:case 0x6EC1E:case 0x6EC64:case 0x6EC9E:
    case 0x6FADE:case 0x6FB1B:case 0x6FB5B:case 0x6FB98:case 0x6FBD5:case 0x6FC15:return true;default:return false;}
}
template<class Memory> bool RouteOnce(Memory& m,const shield::NativeAccess& a,const shield::HitEvent& e,
                                     uint32_t ebp,uint32_t owner,uint32_t low,uint32_t high,Route& r){
    if(e.outer_return!=uint64_t(a.image_base)+0x55CE2||!Stack(ebp-0x4C,0x60,low,high)||
       e.attack!=ebp-0x2C||!Range(owner,0x7F1))return false;
    r.owner=owner;uint32_t ret=0,current=0,previous=0;
    if(!Word(m,ebp+4,ret)||ret<a.image_base||ret-a.image_base>=a.image_size||
       !Word(m,ebp,r.handler)||!Word(m,ebp-0x48,r.attack_no)||r.attack_no>=19||!Word(m,ebp-0x4C,r.parts)||
       !Word(m,ebp+8,current)||!Word(m,ebp+12,previous)||current!=e.current||previous!=e.previous||
       !Bytes(m,owner+0xFC,r.routines)||!Bytes(m,owner+0x100,r.actor)||!Word(m,owner+0x610,r.weapon)||
       !m.Read(owner+0x7F0,&r.weapon_type,1)||!m.Read(owner+0x28B,&r.sequence,1))return false;
    r.caller=ret-a.image_base;
    // Additional structural evidence only; the whole handler must still be
    // reconciled with its pinned source before assigning spatial semantics.
    if(r.handler>ebp&&Stack(r.handler-0x20,0x2C,low,high)&&
       e.current==r.handler-0x10&&e.previous==r.handler-0x1C){
        uint32_t hret=0,howner=0;unsigned char call[5]{};
        r.chain=Word(m,r.handler+4,hret)&&hret==uint64_t(a.image_base)+0x29149&&
            Word(m,r.handler+8,howner)&&howner==owner&&r.caller>=5&&
            shield::Area(a.regions.data(),a.regions.size(),r.caller-5,5,true)&&
            m.Read(ret-5,call,5)&&call[0]==0xE8&&
            int64_t(ret)+static_cast<int32_t>(shield::U32(call+1))==int64_t(a.image_base)+0x55BE0;
    }
    return true;
}
template<class Memory> bool ReadRoute(Memory& m,const shield::NativeAccess& a,const shield::HitEvent& e,
                                     uint32_t ebp,uint32_t owner,uint32_t low,uint32_t high,Route& out){
    out={};if(ebp<0x4C||!shield::Complete(a))return false;Route first{},last{};
    if(!RouteOnce(m,a,e,ebp,owner,low,high,first)||!RouteOnce(m,a,e,ebp,owner,low,high,last)||!Equal(first,last))return false;
    first.stable=true;out=first;return true;
}
struct BodyPart {
    uint32_t address=0,matrix_owner=0;
    std::array<unsigned char,52> raw{};
    std::array<unsigned char,48> matrix{};
};
struct BodyCopy {bool stable=false;uint32_t selected=0;unsigned count=0;BodyPart parts[16]{};};
inline bool Equal(const BodyCopy& a,const BodyCopy& b){
    if(a.selected!=b.selected||a.count!=b.count)return false;
    for(unsigned i=0;i<a.count;++i)if(a.parts[i].address!=b.parts[i].address||a.parts[i].matrix_owner!=b.parts[i].matrix_owner||
        a.parts[i].raw!=b.parts[i].raw||a.parts[i].matrix!=b.parts[i].matrix)return false;
    return true;
}
template<class Memory> bool BodyOnce(Memory& m,uint32_t player,BodyCopy& b){
    if(!Range(player,0x378)||!Word(m,player+0x340,b.selected))return false;
    unsigned char count=0;uint32_t part=0,parts[128]{};
    if(!m.Read(player+0x102,&count,1)||count>128||!Word(m,player+0xF4,part))return false;
    for(unsigned i=0;i<count;++i){
        if(!Range(part,0x128))return false;
        for(unsigned j=0;j<i;++j)if(parts[j]==part)return false;
        parts[i]=part;if(!Word(m,part+0xF4,part))return false;
    }
    if(part)return false; // declared chain and actual chain must agree
    uint32_t node=player+0x344;bool selected=b.selected==0;
    while(node){
        if(b.count==16||!Range(node,52))return false;
        for(unsigned j=0;j<b.count;++j)if(b.parts[j].address==node)return false;
        auto& p=b.parts[b.count++];p.address=node;
        if(!Bytes(m,node,p.raw))return false;
        const int number=static_cast<int16_t>(shield::U16(p.raw.data()+0x26));
        if(number<0||number>count)return false;
        p.matrix_owner=number?parts[number-1]:player;
        if(!Bytes(m,p.matrix_owner+0xC,p.matrix))return false;
        if(node==b.selected)selected=true;
        node=shield::U32(p.raw.data()+0x30);
    }
    return selected;
}
template<class Memory> bool ReadBody(Memory& m,uint32_t player,BodyCopy& out){
    out={};BodyCopy first{},last{};
    if(!BodyOnce(m,player,first)||!BodyOnce(m,player,last)||!Equal(first,last))return false;
    first.stable=true;out=first;return true;
}
struct Sample {
    bool valid=false,asset_bytes_match=false;int native_hit=0;Route route;PoseCopy pose;BodyCopy body;
    uint32_t player_routines=0,identity_reason=0;
    std::array<unsigned char,12> current{},base{},attack{};
    std::array<unsigned char,48> root{};
    std::array<unsigned char,24> position_angles{};
};
// pair is a candidate pair of observed endpoints. The current frame has not
// finished: a later duplicate can still occur, and no uniqueness is certified.
struct Packet {
    Sample now,before,before2;bool pair=false,triplet=false;uint32_t frame_gap=0;
    bool profile_checked=false,profile_eligible=false,contact_qualified=false;
    uint32_t preclamp_identity_reason=0;
    double profile_transfer=0,profile_error=0;
};
inline bool SameKey(const Sample& a,const Sample& b){
    return a.pose.player==b.pose.player&&a.pose.global==b.pose.global&&a.pose.room==b.pose.room&&
        a.pose.character==b.pose.character&&a.pose.resource==b.pose.resource&&a.route.owner==b.route.owner&&
        a.route.caller==b.route.caller&&a.route.attack_no==b.route.attack_no&&a.route.weapon==b.route.weapon&&
        a.route.weapon_type==b.route.weapon_type&&a.route.routines==b.route.routines&&
        a.route.sequence==b.route.sequence&&a.route.actor==b.route.actor;
}
class History {
    struct Slot {Sample sample,earlier;bool occupied=false,duplicate=false,earlier_valid=false;};Slot slots[16]{};unsigned next=0;
public:
    void Reset(){for(auto& s:slots)s.occupied=s.duplicate=s.earlier_valid=false;next=0;}
private:
    void ObserveCopy(const Sample& now,Packet& out){
        out={};out.now=now;
        if(!now.valid){Reset();return;}
        for(const auto& s:slots)if(s.occupied){
            const auto& a=s.sample.pose;const auto& b=now.pose;
            if(a.player!=b.player||a.global!=b.global||a.room!=b.room||a.character!=b.character||a.resource!=b.resource)Reset();
            break;
        }
        for(auto& s:slots)if(s.occupied&&SameKey(now,s.sample)){
            if(now.pose.frame==s.sample.pose.frame){s.duplicate=true;s.earlier_valid=false;return;}
            const bool increasing=now.pose.frame>s.sample.pose.frame;
            out.frame_gap=increasing?now.pose.frame-s.sample.pose.frame:0;
            // Frame continuity is necessary only. No claim of linear motion,
            // immutable render epoch, TOI, contact order or attack eligibility.
            if(increasing&&out.frame_gap==1&&!s.duplicate&&s.sample.native_hit==0){
                out.before=s.sample;out.pair=true;
                // Earlier misses are copied, not extrapolated. Even three
                // frames do not certify the first contact or a render epoch.
                if(s.earlier_valid&&s.earlier.native_hit==0&&SameKey(s.sample,s.earlier)&&
                   s.sample.pose.frame>s.earlier.pose.frame&&s.sample.pose.frame-s.earlier.pose.frame==1){
                    out.before2=s.earlier;out.triplet=true;
                }
            }
            s.earlier_valid=out.pair;if(out.pair)s.earlier=s.sample;
            s.sample=now;s.duplicate=false;return;
        }
        auto& s=slots[next++%16];s.sample=now;s.occupied=true;s.duplicate=s.earlier_valid=false;
    }
public:
    void Observe(const Sample& now,Packet& out){
        ObserveCopy(now,out);
        // A different spatial point (or enemy) may hit between two samples of
        // this key. Keep this invocation's evidence, but never carry an older
        // shield approach through any observed native body hit.
        if(now.valid&&now.native_hit>0)Reset();
    }
};
template<class Memory> bool ReadSample(Memory& m,const shield::NativeAccess& a,const shield::HitEvent& e,
                                      const shield::Frame& frame,const Route& route,Sample& out){
    out={};if(!route.stable||!route.chain||!shield::Gameplay(frame))return false;
    Sample n;n.route=route;n.native_hit=e.native_hit;n.current=frame.current;n.base=frame.previous;
    n.attack=frame.attack;n.root=frame.matrix;n.position_angles=frame.position_angles;n.player_routines=shield::U32(frame.routines.data());
    if(!ReadPose(m,a,e,n.pose)||!ReadBody(m,e.player,n.body))return false;
    PoseCopy again{};shield::Frame last{};
    if(!ReadPose(m,a,e,again)||!rear_shield::Equal(n.pose,again)||!shield::ReadFrame(m,a,e,last)||!shield::Equal(frame,last))return false;
    n.valid=true;out=n;return true;
}
template<class Logger> void LogPacket(Logger log,uint32_t invocation,const Packet& p){
    const auto& r=p.now.route;
    log("rear_shield measurement invocation=%u valid=%u route_stable=%u route_chain=%u handler_return=0x%X owner=0x%X weapon=0x%X weapon_type=%u attack_no=%u parts=%u routines=0x%X actor=%u,%u sequence=%u known_spatial_caller=%u temporal_pair_candidate=%u frame_gap=%u temporal_triplet_candidate=%u current_frame_closed=0 body_t_known=0 contact_qualified=%u asset_bytes_match=%u profile_checked=%u profile_eligible=%u profile_transfer=%.6f profile_error=%.6f identity_reason=%u preclamp_identity_reason=%u",
        invocation,p.now.valid?1U:0U,r.stable?1U:0U,r.chain?1U:0U,r.caller,r.owner,r.weapon,r.weapon_type,r.attack_no,r.parts,
        shield::U32(r.routines.data()),r.actor[0],r.actor[1],r.sequence,SpatialCaller(r.caller)?1U:0U,p.pair?1U:0U,p.frame_gap,p.pair&&p.triplet?1U:0U,
        p.contact_qualified?1U:0U,p.now.asset_bytes_match?1U:0U,p.profile_checked?1U:0U,p.profile_eligible?1U:0U,p.profile_transfer,p.profile_error,
        p.now.identity_reason,p.preclamp_identity_reason);
    if(!p.now.valid)return;
    for(unsigned epoch=0;epoch<(p.pair?(p.triplet?3U:2U):1U);++epoch){
        const auto& s=epoch==2?p.before2:epoch?p.before:p.now;const char* role=epoch==2?"before2":epoch?"before":"now";
        const auto cur=shield::Vector(s.current.data()),base=shield::Vector(s.base.data());
        const auto position=shield::Vector(s.position_angles.data()),angles=shield::Vector(s.position_angles.data()+12);
        // Already captured at the classifier epoch. Worker logging must not
        // reread live angles or replace a matrix/Euler disagreement with TURN.
        log("rear_shield sample invocation=%u role=%s frame=%u native_hit=%d selected=0x%X body_count=%u current=%.9g,%.9g,%.9g spatial_base=%.9g,%.9g,%.9g radius=%.9g root_position=%.9g,%.9g,%.9g root_angles=%.9g,%.9g,%.9g sample_routines=0x%X sample_sequence=%u sample_actor=%u,%u player_routines=0x%X identity_reason=%u",
            invocation,role,s.pose.frame,s.native_hit,s.body.selected,s.body.count,cur.x,cur.y,cur.z,base.x,base.y,base.z,shield::Float(s.attack.data()),
            position.x,position.y,position.z,angles.x,angles.y,angles.z,
            shield::U32(s.route.routines.data()),s.route.sequence,s.route.actor[0],s.route.actor[1],s.player_routines,s.identity_reason);
        const std::array<unsigned char,48>* matrices[]={&s.root,&s.pose.body0,&s.pose.bone2,&s.pose.inverse_bind2,&s.pose.info_matrix};
        const char* names[]={"root","body0","bone2","inverse_bind2","info0"};
        for(unsigned j=0;j<5;++j){const auto m=Matrix(*matrices[j]);
            log("rear_shield measured_matrix invocation=%u role=%s kind=%s values=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
                invocation,role,names[j],m.v[0][0],m.v[0][1],m.v[0][2],m.v[0][3],m.v[1][0],m.v[1][1],m.v[1][2],m.v[1][3],m.v[2][0],m.v[2][1],m.v[2][2],m.v[2][3]);}
        for(unsigned j=0;j<s.body.count;++j){const auto& b=s.body.parts[j];const auto m=Matrix(b.matrix);const auto v=shield::Vector(b.raw.data());
            char raw[105]{};constexpr char hex[]="0123456789abcdef";
            for(unsigned k=0;k<b.raw.size();++k){raw[2*k]=hex[b.raw[k]>>4];raw[2*k+1]=hex[b.raw[k]&15];}
            log("rear_shield body invocation=%u role=%s index=%u address=0x%X matrix_owner=0x%X flags=0x%X part=%u offset=%.9g,%.9g,%.9g radius=%.9g height=%.9g extent=%.9g values=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g raw=%s",
                invocation,role,j,b.address,b.matrix_owner,shield::U16(b.raw.data()+0x24),shield::U16(b.raw.data()+0x26),v.x,v.y,v.z,
                shield::Float(b.raw.data()+0x18),shield::Float(b.raw.data()+0x1C),shield::Float(b.raw.data()+0x20),
                m.v[0][0],m.v[0][1],m.v[0][2],m.v[0][3],m.v[1][0],m.v[1][1],m.v[1][2],m.v[1][3],m.v[2][0],m.v[2][1],m.v[2][2],m.v[2][3],raw);}
    }
}
} // namespace link_runtime::rear_shield::measure

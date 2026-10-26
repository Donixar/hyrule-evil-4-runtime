#pragma once
// Shield Bash scans and native writes (shield_bash.h has the rules). Injected reader/writer and publisher: no
// hooks, no process access, never writes HP. Game thread in the runtime (the sword arbiter's exit).
//
// Ganados: song_effects Judge (Blow::Charge rules) within the reach, then the cone (Front); nearest first, at most
// Config::targets. Each takes song_effects Fire(Blow::Charge): one native kick record (weapon 0x14) through the
// adapter's publisher 0x1AEEF0, re-read right before the call (life, pending record, body part).
// Weapons: every live manager slot with the cEmWep vtable (0x716ED0, the one thrown::Identify checks), routine
// 1/5, 1/6 or 1/0xA, inside the cone at the deflect reach: thrown::Deflect (refused while a record is pending,
// so a weapon is written once until the native emWepDmCk consumes it).
#include "shield_bash.h"
#include "ocarina_song_effects.h"
#include "parry_general_block.h"

namespace link_runtime::shield_bash {
namespace ose=link_runtime::ocarina::song_effects;
namespace thrown=link_runtime::parry::general::thrown;
namespace layout {
constexpr uint32_t emwep_vtable_rva=thrown::kVtable;   // cEmWep (emwep.h, cEmMgr::construct id 0x42)
constexpr uint8_t r_throw=5,r_shot=6,r_scythe=0xA;     // EmWep_R1_move_tbl: Throw, Shot, ThrowScythe
}
inline bool Flying(uint8_t r0,uint8_t r1){return r0==1&&(r1==layout::r_throw||r1==layout::r_shot||r1==layout::r_scythe);}

// ---- Ganados -----------------------------------------------------------------------------------
struct GanadoReport {
    ose::Pick pick;unsigned cone=0,published=0;
    unsigned shots[7]={};ose::Shot result[kMaxTargets]={};
};
template<class Reader> ose::Pick ScanGanados(Reader& m,uint32_t base,const tg::Vec3& link,float yaw,const Config& c,unsigned& cone){
    ose::Pick p;tg::Manager pool;cone=0;
    const unsigned cap=c.targets<1?1:c.targets>kMaxTargets?kMaxTargets:c.targets;
    if(tg::ReadManager(m,base,tg::Config{},pool)!=tg::Reject::None)return p;
    for(unsigned i=0;i<pool.count;++i){
        const uint32_t ptr=pool.array+i*pool.block;tg::Raw r;++p.scanned;
        if(tg::ReadRaw(m,ptr,r)!=tg::Reject::None){++p.skips[unsigned(ose::Skip::Unreadable)];continue;}
        float d=0;const ose::Skip s=ose::Judge(r,base,link,ose::Blow::Charge,float(c.reach),d);
        if(s!=ose::Skip::None){++p.skips[unsigned(s)];continue;}
        float fd=0;
        if(!Front(link,yaw,r.pos,float(c.reach),kGanadoVertical,fd)){++cone;continue;}
        ose::Offer(p,ose::Target{ptr,r.id,r.type,d},cap);
    }
    return p;
}
template<class Reader,class Publish>
GanadoReport HitGanados(Reader& m,uint32_t base,const tg::Vec3& link,float yaw,const Config& c,Publish&& publish,
                        const native_hitpart::Source* source=nullptr){
    GanadoReport out;out.pick=ScanGanados(m,base,link,yaw,c,out.cone);
    const float from[3]={link.x,link.y,link.z};
    for(unsigned i=0;i<out.pick.count&&i<kMaxTargets;++i){
        const ose::Shot s=ose::Fire(m,base,out.pick.items[i].actor,ose::Blow::Charge,from,publish,source);
        out.result[i]=s;++out.shots[unsigned(s)];
        if(s==ose::Shot::Published||s==ose::Shot::Mismatch)++out.published;   // the native call ran
    }
    return out;
}

// ---- non-lethal knockback (spin-fix-01) -------------------------------------------------------------
// Donald 18-oct night: a bash on a Ganado at 54/1000 HP killed it ("lo destruyó como si hubiese sido una Killer7"):
// the kick record carries the kick's native damage (Dmg_tbl_em10[0x14] = 200 before factors), enough to kill a
// weakened Ganado, and a kick death sends it flying apart. TP's shield attack only staggers. So each record the bash
// publishes is claimed: when the native calc 0x4AD40 evaluates it (GetWepDmVal, return 0x4AD87) for that actor with
// weapon 0x14 and the live cDmgInfo still exactly the one written, the damage is KnockbackDamage (1, or 0 at <3 HP). The
// kick reaction (em10DmSetWep00, chosen by the weapon, not the amount, like the flash grenade's 0-damage stun) still
// knocks it back. Any other evaluation (the real kick, the Epona song, a record replaced since) stays native.
// A claim lives until its record is evaluated, consumed without a calc, replaced, or kTtlFrames logic frames.
class Claims {
public:
    static constexpr unsigned kTtlFrames=3;
    // After the publisher wrote `a` (Fire returned Published).
    void Add(const ose::dmg::PublishArgs& a){
        const uint32_t actor=a.dmg_info-ose::dmg::layout::dmg_info;
        Slot* free=nullptr;
        for(auto& c:slots_){if(c.actor==actor){free=&c;break;}if(!c.actor&&!free)free=&c;}
        if(!free)free=&slots_[0];
        *free=Slot{actor,ose::dmg::Expected(a),0};
    }
    // GetWepDmVal detour: true = this evaluation is a bash record (the caller returns KnockbackDamage(hp)).
    // record(actor, out): the live cDmgInfo (damage::ReadRecord).
    template<class RecordFn> bool Claim(uint32_t caller_rva,uint32_t actor,unsigned weapon,RecordFn&& record){
        if(caller_rva!=ose::dmg::layout::calc_return_rva||weapon!=ose::kChargeWeapon||!actor)return false;
        for(auto& c:slots_){
            if(c.actor!=actor)continue;
            ose::dmg::Record live;const bool same=record(actor,live)&&!ose::dmg::Mismatch(c.expect,live);
            c=Slot{};++(same?claimed_:superseded_);return same;
        }
        return false;
    }
    // Once per logic frame (with the adapter's Tick).
    template<class RecordFn> void Tick(RecordFn&& record){
        for(auto& c:slots_){
            if(!c.actor)continue;
            ose::dmg::Record live;
            if(!record(c.actor,live)||!(live.flags&1)||(ose::dmg::Mismatch(c.expect,live)&~unsigned(ose::dmg::kTimer))||++c.frames>=kTtlFrames){c=Slot{};++dropped_;}
        }
    }
    unsigned Active() const{unsigned n=0;for(const auto& c:slots_)n+=c.actor?1:0;return n;}
    unsigned Claimed() const{return claimed_;}
    unsigned Superseded() const{return superseded_;}
    unsigned Dropped() const{return dropped_;}
private:
    struct Slot{uint32_t actor=0;ose::dmg::Record expect;unsigned frames=0;};
    Slot slots_[kMaxTargets];unsigned claimed_=0,superseded_=0,dropped_=0;
};

// ---- weapons in flight -------------------------------------------------------------------------
constexpr unsigned kLogged=4;
struct WeaponReport {
    unsigned scanned=0,weapons=0,flying=0,in_cone=0,deflected=0,refused=0;
    unsigned listed=0;uint32_t object[kLogged]={};uint8_t routine[kLogged]={};float distance[kLogged]={};
};
// write=false: count only (tests, observe). Mem: Read/Write(uint32_t, void*, size_t).
template<class Mem> WeaponReport DeflectWeapons(Mem& m,uint32_t base,const tg::Vec3& link,float yaw,const Config& c,bool write=true){
    WeaponReport out;tg::Manager pool;
    if(tg::ReadManager(m,base,tg::Config{},pool)!=tg::Reject::None)return out;
    const float from[3]={link.x,link.y,link.z};
    for(unsigned i=0;i<pool.count;++i){
        const uint32_t ptr=pool.array+i*pool.block;++out.scanned;
        uint32_t vtable=0,be=0;uint8_t r[2]{};tg::Vec3 pos;
        if(!m.Read(ptr,&vtable,4)||vtable!=base+layout::emwep_vtable_rva)continue;
        if(!m.Read(ptr+tg::layout::be_flag,&be,4)||(be&0x201)!=1)continue;
        ++out.weapons;
        if(!m.Read(ptr+tg::layout::routine,r,2)||!Flying(r[0],r[1]))continue;
        ++out.flying;
        float xyz[3]{};float d=0;
        if(!m.Read(ptr+tg::layout::pos,xyz,12))continue;
        pos={xyz[0],xyz[1],xyz[2]};
        if(!Front(link,yaw,pos,float(c.deflect_reach),kWeaponVertical,d))continue;
        ++out.in_cone;
        const bool ok=write&&thrown::Deflect(m,ptr,from);
        if(ok)++out.deflected;else ++out.refused;
        if(ok&&out.listed<kLogged){out.object[out.listed]=ptr;out.routine[out.listed]=r[1];out.distance[out.listed]=d;++out.listed;}
    }
    return out;
}
}  // namespace link_runtime::shield_bash

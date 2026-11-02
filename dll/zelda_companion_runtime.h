#pragma once
// Zelda as the AI partner casts her light ray, the glue (zelda_companion.h has the logic; doc
// docs/zelda-companero-rayo-2026-10-30.md). Included by zelda_light_runtime.h inside its glue: it uses the ray's beam,
// sparks, hand star, sound and config (zlr) and the combat state (g_cmb_*, Select, BulletWall, the damage adapter).
// CompanionFrame runs once per native frame while the player is Link (pl_type 0) and ZeldaCompanionRay=1: CompanionTick
// (zelda_light_runtime.h), from Link's Normal dispatch and from his core on damage and death frames too.
//
// Her arm: the same TP cast clip on HER +0x2A8 (zelda_cast_clip_runtime.h). cSubChar has no neck work there:
// cSubChar::neckCtrl (pl_npc.cpp:2161-2190) only reads pMotionB, homing the head while a blend is on. The weight
// written from the player's dispatch stays until her own MotBase->move blends it (nothing else writes it). Without the
// clip (ZeldaCastClip files missing) the cast is armless: the hand star for 150 ms, then the ray from her hand.
// The hand: part 10's world translation (matrix +0x0C), through the parts chain +0xF4 with the parents of Ashley's
// skeleton checked (zelda_cast_pose.h kParents), as the player's cast reads it.
// Damage: the ray's own caller tag (CmbCaller::Zelda) inside NativeHit, so nothing of Link's attributes the hit to him.
#include "zelda_companion.h"
namespace zlr {
namespace zc=link_runtime::zelda_companion;
namespace zcr=link_runtime::zelda_cast_clip::runtime;
// cSubChar::move/moveCore bytes the offsets come from (rtti and disassembly of the pinned image, preferred base 0x400000;
// native_pins::Matches undoes the relocations of the absolute operands).
inline constexpr link_runtime::native_pins::Pin kCompanionPins[]={
    {0x361A70,"558bec83ec08568bf1f68628040000010f8527020000a13c6fc00066"},    // F_SLEEP test, pG load
    {0x361A8B,"668b88b84f000066898e24030000"},                                // hp = pG->ashley_life (+0x4FB8 -> +0x324)
    {0x361AB9,"81a024500000ffffffdf"},                                        // pG+0x5024 &= ~0x20000000 (STA_SUB_CATCHED)
    {0x361AE8,"8a86fc0000003c04750a8b9634080000ffd2eb0e0fb6c08b14853cbbc1008bceffd2"},   // r_no_0 +0xFC, NpcFuncTbl
    {0x3619D0,"568bf1e86a18caff0fb686fd0000008b1485d8bac1008bceffd2"},       // moveCore: r_no_1 +0xFD, funcTbl
    {0x361B4D,"e8d8c4caff8bcee84a42caff56e8b157caff"},                        // shadowCtrl, partsWorldCalc (ret 0x361B59)
    {0x727FF4,"60aa4000"},                                                    // cSubChar vtable slot 4 -> 0xAA60
    {0xAA60,"e90b703500"}};                                                   // thunk -> cSubChar::move 0x361A70
static zc::Config g_comp_cfg;
static bool g_comp_on=false,g_comp_dead_logged=false;
static zc::Sanity g_comp_sanity;
static zc::Latch g_comp_latch;
static zc::Cooldown g_comp_cool;
static zc::CaughtDebounce g_comp_caught;
static zl::Cast g_comp_cast;
static zcr::Layer g_comp_layer;
static uint32_t g_comp_partner=0,g_comp_target=0;
static uint64_t g_comp_identity=0;
static zc::Situation g_comp_situation=zc::Situation::None;
static ::sword_trail::Vec3 g_comp_hand{};
static bool g_comp_have_hand=false;

// Startup (zlr::Prepare): the [ZeldaLight] keys of zelda_companion.h, read by the ray's own INI reader.
static void CompanionPrepare(const zc::Config& config){
    g_comp_cfg=config;
    if(!g_comp_cfg.on){Log("ZeldaCompanion ready=0 (ZeldaCompanionRay=0)");return;}
    zcr::Memory m;
    if(!link_runtime::native_pins::All(m,g_cmb_base,kCompanionPins)){Log("ZeldaCompanion disabled: cSubChar pins differ (offsets unverified on this exe)");g_comp_cfg.on=false;return;}
    g_comp_cool=zc::Cooldown(uint32_t(GetTickCount64())|1u);
    g_comp_on=true;
    Log("ZeldaCompanion ready=1 cooldown=%u..%us threat=%.0f danger_hp=%d vtable=0x%X core_frame=%u",g_comp_cfg.cooldown_min_s,g_comp_cfg.cooldown_max_s,
        double(g_comp_cfg.threat_mm),g_comp_cfg.danger_hp,zc::kSubCharVtable,::mortal::dispatch_hooks::player_frame?1U:0U);
}
// The cast ends (complete, cancelled, the partner gone): the clip gives +0x2A8 back.
static void CompanionEnd(const char* why){
    if(!g_comp_layer.on&&!g_comp_partner)return;
    if(why)CmbLog("zelda_companion end=%s",why);
    zcr::Release(g_comp_layer);
    g_comp_cast=zl::Cast{};g_comp_partner=0;g_comp_have_hand=false;
}
// Part 10 (her right hand) in world space, after her last partsWorldCalc.
static bool CompanionHand(uint32_t partner,::sword_trail::Vec3& out){
    CmbMem m;uint32_t part[11]{};uint32_t p=0;
    if(!m.Read(partner+0xF4,&p,4))return false;
    for(unsigned i=0;i<11;++i){
        uint32_t parent=0;
        if(!link_runtime::IsPointer(p)||!m.Read(p+0x6C,&parent,4))return false;
        const int k=link_runtime::zelda_cast::kParents[i];
        if(parent!=(k<0?partner:part[k]))return false;
        part[i]=p;
        if(i<10&&!m.Read(p+0xF4,&p,4))return false;
    }
    float mat[12]{};
    if(!m.Read(part[10]+0x0C,mat,sizeof mat)||!std::isfinite(mat[3])||!std::isfinite(mat[7])||!std::isfinite(mat[11]))return false;
    out={mat[3],mat[7],mat[11]};return true;
}
// The ray at the trigger's Ganado, re-found by pointer and combat identity now (it may have died or left).
static void CompanionFire(double now_ms,uint32_t room,uint32_t owner,const float* feet,float yaw){
    CmbMem m;
    auto c=BulletTargets();c.spin_radius=g_comp_cfg.range_mm;c.vertical=g_comp_cfg.vertical;
    const auto found=sword::target::Select(m,g_cmb_base,g_cmb_tracker,mtg::Vec3{feet[0],feet[1],feet[2]},yaw,c,false);
    int at=-1;
    for(unsigned i=0;i<found.count;++i)if(found.items[i].pointer==g_comp_target&&found.items[i].identity==g_comp_identity)at=int(i);
    float pos[3]{};
    if(at<0||!m.Read(g_comp_target+0x94,pos,12)||!std::isfinite(pos[0])||!std::isfinite(pos[1])||!std::isfinite(pos[2])){
        CmbLog("zelda_companion ray dropped (target gone) situation=%s",zc::SituationName(g_comp_situation));return;}
    const ::sword_trail::Vec3 hand=g_comp_hand;
    ::sword_trail::Vec3 end{pos[0],pos[1]+spb::kChestY,pos[2]},stop{};
    const auto seg=BulletWall(hand,end,stop);
    if(seg==BulletSeg::Fault){CmbLog("zelda_companion ray fault (line of sight)");return;}
    const bool clear=seg==BulletSeg::Clear;
    if(!clear)end=stop;
    g_beam.Fire(now_ms,hand,end,room,owner);
    g_sound.store(true,std::memory_order_release);
    Sparks(now_ms,room,owner,hand,end);
    if(!clear){CmbLog("zelda_companion ray wall situation=%s target=0x%08X",zc::SituationName(g_comp_situation),g_comp_target);return;}
    const auto& t=found.items[at];int16_t hp=0;m.Read(t.pointer+zl::layout::hp,&hp,2);
    mdmg::HitTarget hit;hit.identity=t.identity;hit.actor=t.pointer;hit.from[0]=feet[0];hit.from[1]=feet[1]+kCmbFromMark;hit.from[2]=feet[2];hit.repair_len=true;
    g_cmb_mortal_same=true;g_cmb_mortal_identity=t.identity;g_cmb_caller=CmbCaller::Zelda;
    const auto st=g_cmb_adapter.NativeHit(hit,int(g_cfg.ganado),mdmg::Kind::Fixed,g_cmb_adapter.Issue());
    g_cmb_caller=CmbCaller::None;
    CmbLog("zelda_companion ray target=0x%08X hp=%d amount=%u situation=%s status=%s",t.pointer,hp,g_cfg.ganado,zc::SituationName(g_comp_situation),
        mdr::StatusName(st));
}
// Once per native frame with Link as the player (CompanionTick).
static void CompanionFrame(double now_ms,uint32_t room,uint32_t link_player){
    if(!g_comp_on)return;
    CmbMem m;
    const uint32_t partner=CmbGet<uint32_t>(g_cmb_base+zc::kPartnerSlot);
    if(!link_runtime::IsPointer(partner)||!link_runtime::IsPointer(link_player)){CompanionEnd("no_partner");return;}
    uint32_t vt=0;uint8_t id=0,r[2]{};float feet[3]{},yaw=0;
    const bool read=m.Read(partner,&vt,4)&&m.Read(partner+zc::layout::id,&id,1)&&m.Read(partner+zc::layout::r0,r,2)&&
        m.Read(partner+zc::layout::pos,feet,12)&&m.Read(partner+zc::layout::rot_y,&yaw,4);
    const bool ok=read&&g_comp_sanity.Check(vt==g_cmb_base+zc::kSubCharVtable,id,r[0],r[1]);
    if(g_comp_sanity.Dead()){
        if(!g_comp_dead_logged){g_comp_dead_logged=true;Log("ZeldaCompanion disabled: implausible r0=%u r1=%u for %u frames (cSubChar offsets need a live probe)",
            r[0],r[1],zc::kImplausibleLimit);}
        CompanionEnd("implausible");g_comp_on=false;return;
    }
    const auto g=CmbGet<uint32_t>(g_cmb_global_slot);
    if(!link_runtime::IsPointer(g))return;
    const bool caught=g_comp_caught.Update((CmbGet<uint32_t>(g+zc::layout::status2)&zc::layout::caught)!=0,now_ms);
    const bool normal=ok&&zc::StateAllowed(r[0],r[1])&&zc::Finite3(feet)&&std::isfinite(yaw);
    const bool carried=ok&&caught&&zc::StateAllowed(r[0],r[1],true)&&zc::Finite3(feet)&&std::isfinite(yaw);
    if(g_comp_cast.Busy(now_ms)||g_comp_layer.on){
        const bool allowed=g_comp_situation==zc::Situation::Carried?carried:normal;
        if(partner!=g_comp_partner||!allowed){CmbLog("zelda_companion cancelled r0=%u r1=%u",r[0],r[1]);CompanionEnd(nullptr);return;}
        const double age=g_comp_cast.Age(now_ms);
        if(g_comp_layer.on&&!zcr::Tick(g_comp_layer,age))CmbLog("zelda_companion clip=yielded age_ms=%.0f",age);
        ::sword_trail::Vec3 hand{};
        if(CompanionHand(partner,hand)){g_comp_hand=hand;g_comp_have_hand=true;}
        double entries=0;const bool known=g_comp_cast.Clip()&&zcr::SeqFrame(g_comp_layer,entries);
        if(g_comp_cast.FireDue(now_ms,g_comp_cast.Clip()&&link_runtime::zelda_cast_clip::ReleaseDue(known,entries,age))){
            if(!g_comp_have_hand)CmbLog("zelda_companion no_hand: the ray is dropped");
            else CompanionFire(now_ms,room,link_player,feet,yaw);
        }
        if(g_comp_have_hand)HandStar(g_comp_cast.GlowAge(now_ms),g_comp_hand,yaw);
        if(!g_comp_cast.Busy(now_ms)){zcr::Release(g_comp_layer);g_comp_cast=zl::Cast{};g_comp_partner=0;g_comp_have_hand=false;}
        return;
    }
    if(!normal&&!carried)return;
    static double last_assess=-1e18;   // every kAssessMs of game time (time running back re-assesses at once)
    if(now_ms>=last_assess&&now_ms-last_assess<zc::kAssessMs)return;
    last_assess=now_ms;
    // The world: Ganados around her (the combat selection: live em10 family, not the Merchant), Link, the caught bit.
    zc::World w;w.caught=carried;w.link_hp=CmbGet<int16_t>(g+zc::layout::pl_life);
    w.zelda[0]=feet[0];w.zelda[1]=feet[1];w.zelda[2]=feet[2];
    if(!m.Read(link_player+0x94,w.link,12))return;
    auto c=BulletTargets();c.spin_radius=g_comp_cfg.range_mm;c.vertical=g_comp_cfg.vertical;
    const auto found=sword::target::Select(m,g_cmb_base,g_cmb_tracker,mtg::Vec3{feet[0],feet[1],feet[2]},yaw,c,false);
    zc::Enemy enemies[sword::target::kMaxTargets];unsigned count=0;
    for(unsigned i=0;i<found.count;++i){
        zc::Enemy e;float p[3]{};uint8_t er[2]{};
        if(!m.Read(found.items[i].pointer+0x94,p,12)||!m.Read(found.items[i].pointer+0xFC,er,2)||!zc::Finite3(p))continue;
        e.x=p[0];e.y=p[1];e.z=p[2];e.r0=er[0];e.r1=er[1];e.tag=i;enemies[count++]=e;
    }
    w.enemies=enemies;w.count=count;
    zc::Pick pick=zc::Assess(w,g_comp_cfg);
    if(!normal&&pick.situation!=zc::Situation::Carried)pick=zc::Pick{};   // carried: only the carrier
    const uint32_t target=pick.index>=0?found.items[enemies[pick.index].tag].pointer:0;
    if(!g_comp_latch.Observe(pick.situation,target)||!g_comp_cool.Ready(now_ms))return;
    // A clear line from about her hand to the Ganado's chest, or no cast (the trigger stays armed).
    const zc::Enemy& e=enemies[pick.index];
    ::sword_trail::Vec3 hand{};const bool have=CompanionHand(partner,hand);
    const ::sword_trail::Vec3 from=have?hand: ::sword_trail::Vec3{feet[0],feet[1]+1100.0,feet[2]},to{e.x,e.y+spb::kChestY,e.z};
    ::sword_trail::Vec3 stop{};
    if(BulletWall(from,to,stop)!=BulletSeg::Clear)return;
    g_comp_latch.Fired();g_comp_cool.Arm(now_ms,g_comp_cfg.cooldown_min_s,g_comp_cfg.cooldown_max_s);
    const auto& t=found.items[e.tag];
    g_comp_partner=partner;g_comp_target=t.pointer;g_comp_identity=t.identity;g_comp_situation=pick.situation;
    g_comp_hand=hand;g_comp_have_hand=have;
    const bool clip=zcr::Ready()&&zcr::Install(g_comp_layer,partner,0);
    g_comp_cast.Begin(now_ms,clip);
    CmbLog("zelda_companion cast situation=%s target=0x%08X r=%u/0x%02X d_zelda=%.0f link_hp=%d caught=%u clip=%u next_in=%.0fms",
        zc::SituationName(pick.situation),t.pointer,e.r0,e.r1,double(zc::Flat(feet,e)),w.link_hp,caught?1U:0U,clip?1U:0U,g_comp_cool.ReadyAt()-now_ms);
}
}  // namespace zlr

#pragma once
// Zelda's light ray, the glue (zelda_light.h has the logic; plan docs/superpowers/plans/2026-10-23-zelda-light-ray.md).
// Included by link_runtime.cpp after spin_attack_runtime.h: it uses the combat state (g_cmb_*, CmbMem, CmbGet), the
// Ganado selection (sword::target::Select), the damage adapter, the native line of sight (BulletWall) and the trail's
// table-17 node (g_trail_on, Submit). ZeldaLightFrame runs once per player dispatch (CombinedDecide), which the live probe
// of 23-oct showed running about 165 times a second with pl_type 1 in r20d. While the player is Link only the partner's
// own rays run (zelda_companion_runtime.h, ZeldaCompanionRay=1); with it off nothing here runs for Link.
#include "zelda_light.h"
namespace zlr {
namespace zl=link_runtime::zelda_light;
static zl::Config g_cfg;
static bool g_on=false;                   // fixed at startup: config, pins, the player dispatch
static zl::Press g_press;
static zl::Beam g_beam;
static zl::Cast g_cast;                    // the cast: hand glow, the deferred ray, the 600 ms pose window
static ::sword_trail::Vec3 g_cast_hand{};    // where the hand was at the last frame of the cast
static link_runtime::ztarget::voice::Voice g_voice;   // Bin32/link_zelda_light.wav, SHA-pinned
static std::atomic<bool> g_sound{false};  // game thread raises, the poll thread plays
inline constexpr const char* kSoundSha="32468ee94e73a4118838e77c3b7f9aa49f7cd96a6a62ff3c5292a061d324ae6b";

static ::sword_trail::Style Style(){::sword_trail::Style s{};s.length_ms=zl::kFadeMs;s.inner=0.5;s.rgb=zl::kGold;s.alpha=255;return s;}

// The probe ([ZeldaLight] Probe=1): per second, the frames seen with pl_type 1 and the key bits; per room, every enemy
// that is not a Ganado (id, class vtable, hp, Be_flg, the knight's inline hit part).
static void Probe(unsigned pl,uint32_t room){
    static unsigned calls=0;static DWORD last=0;static uint32_t room_seen=0xFFFFFFFF,held_or=0,pressed_or=0;
    if(pl==1){++calls;held_or|=CmbGet<uint32_t>(g_cmb_base+zl::kKeyHeld);pressed_or|=CmbGet<uint32_t>(g_cmb_base+zl::kKeyPressed);}
    const DWORD now=GetTickCount();
    if(now-last>=1000){if(calls)CmbLog("zelda_probe calls=%u pltype=%u held=0x%08X pressed=0x%08X room=0x%X",calls,pl,held_or,pressed_or,room);
        calls=0;held_or=pressed_or=0;last=now;}
    if(room==room_seen)return;
    room_seen=room;CmbMem m;mtg::Manager pool;
    if(mtg::ReadManager(m,g_cmb_base,mtg::Config{},pool)!=mtg::Reject::None)return;
    for(uint32_t i=0;i<pool.count&&i<1024;++i){
        const uint32_t a=pool.array+i*pool.block;uint32_t be=0,vt=0;uint8_t id=0;int16_t hp=0;float len=0;int16_t parts=0;
        if(!m.Read(a,&vt,4)||!m.Read(a+4,&be,4)||(be&1)==0||!m.Read(a+0x100,&id,1)||(id>=0x10&&id<=0x20))continue;
        m.Read(a+zl::layout::hp,&hp,2);m.Read(a+zl::layout::hit_info+0x28,&len,4);m.Read(a+zl::layout::hit_info+0x26,&parts,2);
        CmbLog("zelda_probe em ptr=0x%08X id=0x%02X vtable=0x%X hp=%d be=0x%08X hit_parts=%d hit_len=%.0f room=0x%X",a,id,vt-g_cmb_base,hp,be,parts,
            double(len),room);
    }
}

// The awake armor knights of the pool: class vtable 0x712AE4, live slot, hp > 0, not natively protected (Be_flg 0x400),
// in their move routine past StartWait/AtkWait (dormant and ambushing knights stay native).
struct Knight {uint32_t pointer=0;int16_t hp=0;float pos[3]{};};
static unsigned Knights(CmbMem& m,Knight* out,unsigned max){
    mtg::Manager pool;unsigned n=0;
    if(mtg::ReadManager(m,g_cmb_base,mtg::Config{},pool)!=mtg::Reject::None)return 0;
    for(uint32_t i=0;i<pool.count&&i<1024&&n<max;++i){
        const uint32_t a=pool.array+i*pool.block;uint32_t be=0,vt=0,work=0;Knight k;k.pointer=a;
        if(!m.Read(a,&vt,4)||vt!=g_cmb_base+zl::kKnightVtable||!m.Read(a+zl::layout::be_flag,&be,4)||(be&0x201)!=1||!m.Read(a+zl::layout::work_flags,&work,4)||(work&zl::layout::protect)||
           !m.Read(a+zl::layout::hp,&k.hp,2)||k.hp<=0||!m.Read(a+zl::layout::pos,k.pos,12))continue;
        uint8_t r[2]{};   // awake only (r0 move, r1 from Wait on): dormant and ambushing knights stay native
        if(!m.Read(a+zl::layout::routine,r,2)||r[0]!=1||r[1]<zl::kKnightAwake)continue;
        if(!std::isfinite(k.pos[0])||!std::isfinite(k.pos[1])||!std::isfinite(k.pos[2]))continue;
        out[n++]=k;
    }
    return n;
}

// The target the ray would take now (Ganados and awake knights, as Fire picks them): its feet, for the cast to turn her.
static bool PickTarget(const float* feet,float yaw,float* at){
    CmbMem m;
    auto c=BulletTargets();c.spin_radius=g_cfg.range;c.vertical=zl::kVertical;
    const auto found=sword::target::Select(m,g_cmb_base,g_cmb_tracker,mtg::Vec3{feet[0],feet[1],feet[2]},yaw,c,false);
    Knight knights[16];const unsigned nk=Knights(m,knights,16);
    zl::Candidate cand[sword::target::kMaxTargets+16];unsigned count=0;
    for(unsigned i=0;i<found.count;++i){
        float p[3]{};
        if(m.Read(found.items[i].pointer+0x94,p,12)&&std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]))cand[count++]={p[0],p[1],p[2],i,false};
    }
    for(unsigned i=0;i<nk;++i)cand[count++]={knights[i].pos[0],knights[i].pos[1],knights[i].pos[2],i,true};
    const int k=zl::Pick(feet,yaw,g_cfg.range,cand,count);
    if(k<0)return false;
    at[0]=cand[k].x;at[1]=cand[k].y;at[2]=cand[k].z;return true;
}

// One ray: the nearest Ganado or awake knight in front, the wall test from the hand, then the damage, the beam and the sound.
static bool Fire(double now_ms,uint32_t player,const float* feet,float yaw,uint32_t room,const ::sword_trail::Vec3& hand,::sword_trail::Vec3& impact){
    CmbMem m;
    auto c=BulletTargets();c.spin_radius=g_cfg.range;c.vertical=zl::kVertical;
    const auto found=sword::target::Select(m,g_cmb_base,g_cmb_tracker,mtg::Vec3{feet[0],feet[1],feet[2]},yaw,c,false);
    Knight knights[16];const unsigned nk=Knights(m,knights,16);
    zl::Candidate cand[sword::target::kMaxTargets+16];unsigned count=0;
    for(unsigned i=0;i<found.count;++i){
        float at[3]{};
        if(!m.Read(found.items[i].pointer+0x94,at,12)||!std::isfinite(at[0])||!std::isfinite(at[1])||!std::isfinite(at[2]))continue;
        cand[count++]={at[0],at[1],at[2],i,false};
    }
    for(unsigned i=0;i<nk;++i)cand[count++]={knights[i].pos[0],knights[i].pos[1],knights[i].pos[2],i,true};
    const int pick=zl::Pick(feet,yaw,g_cfg.range,cand,count);
    const float fx=std::sin(yaw),fz=std::cos(yaw);
    ::sword_trail::Vec3 end{hand.x+fx*g_cfg.range,hand.y,hand.z+fz*g_cfg.range};
    if(pick>=0)end={cand[pick].x,cand[pick].y+spb::kChestY,cand[pick].z};
    ::sword_trail::Vec3 stop{};
    const auto seg=BulletWall(hand,end,stop);
    const bool clear=seg==BulletSeg::Clear;
    if(!clear)end=stop;
    if(seg==BulletSeg::Fault){CmbLog("zelda_ray fault (line of sight)");return false;}   // never a hit or a beam through an unseen wall
    impact=end;
    g_beam.Fire(now_ms,hand,end,room,player);
    g_sound.store(true,std::memory_order_release);
    if(pick<0||!clear){
        const double dx=end.x-hand.x,dy=end.y-hand.y,dz=end.z-hand.z;
        CmbLog("zelda_ray miss target=%d wall=%u candidates=%u length=%.0f hand=%.0f,%.0f,%.0f yaw=%.2f first=%.0f,%.0f,%.0f",pick,clear?0U:1U,count,
            std::sqrt(dx*dx+dy*dy+dz*dz),hand.x,hand.y,hand.z,double(yaw),count?cand[0].x:0.f,count?cand[0].y:0.f,count?cand[0].z:0.f);
        return true;}
    const float from[3]={feet[0],feet[1]+kCmbFromMark,feet[2]};
    if(!cand[pick].knight){
        const auto& t=found.items[cand[pick].tag];int16_t hp=0;m.Read(t.pointer+zl::layout::hp,&hp,2);
        mdmg::HitTarget hit;hit.identity=t.identity;hit.actor=t.pointer;hit.from[0]=from[0];hit.from[1]=from[1];hit.from[2]=from[2];hit.repair_len=true;
        g_cmb_mortal_same=true;g_cmb_mortal_identity=t.identity;g_cmb_caller=CmbCaller::Sword;
        const auto st=g_cmb_adapter.NativeHit(hit,int(g_cfg.ganado),mdmg::Kind::Fixed,g_cmb_adapter.Issue());
        g_cmb_caller=CmbCaller::None;
        CmbLog("zelda_ray target=ganado ptr=0x%08X hp=%d amount=%u status=%s",t.pointer,hp,g_cfg.ganado,mdr::StatusName(st));
        return true;
    }
    const Knight& k=knights[cand[pick].tag];
    // The record first (it refuses a protected, dying or busy knight), then the drain: em3cDmCk reads both later this frame.
    const int16_t left=int16_t(zl::Drain(k.hp,int(g_cfg.knight),zl::kKnightNativeHit));
    const bool struck=zl::StrikeKnight(m,k.pointer,from,k.pointer+zl::layout::hit_info)&&(left==k.hp||m.Write(k.pointer+zl::layout::hp,&left,2));
    uint8_t routine=0;m.Read(k.pointer+zl::layout::routine,&routine,1);
    CmbLog("zelda_ray target=knight ptr=0x%08X hp=%d->%d struck=%u r0=%u",k.pointer,k.hp,struck?left:k.hp,struck?1U:0U,routine);
    return true;
}
// The golden sparks along the ray and the flash where it lands (TP's KAITENGIRIL_D sparkles tinted gold, SORD_FLUSH), on
// the shared TP particle system; Link's ParticlesFrame never draws it while the player is Ashley (not his gameplay).
static void Sparks(double now_ms,uint32_t room,uint32_t player,const ::sword_trail::Vec3& from,const ::sword_trail::Vec3& to){
    if(!g_part_on.load(std::memory_order_acquire)||!g_part_cfg.on)return;
    g_part.Advance(now_ms,room,player);
    ::sword_trail::Vec3 at[zl::kMaxSparks];const unsigned n=zl::SparkPoints(from,to,at);
    for(unsigned i=0;i<n;++i){
        spt::Placement p;p.origin={at[i].x,at[i].y,at[i].z};p.paint={spt::Tint::Toward,zl::kGold,0};
        p.size=i+1==n?1.8:1.1;   // live 23-oct: at 0.7 they read as small white dots
        g_part.Emit(spt::Effect::SpinSparkle,p);
    }
    spt::Placement flash;flash.origin={to.x,to.y,to.z};flash.paint={spt::Tint::Toward,zl::kGold,0};
    g_part.Emit(spt::kFlash,flash);
}
// The hand's star (the charge glow's glint, additive) for this cast's age, on a 1.2 m "blade" ending at the hand.
static void HandStar(double age,const ::sword_trail::Vec3& hand,float yaw){
    namespace scd=link_runtime::sword_charge::draw;
    const auto g=zl::Glow(age);
    if(!g.star||!g_charge_on.load(std::memory_order_acquire))return;
    schg::Look look;look.rgb=zl::kGold;look.glow_alpha=0;look.star_alpha=g.star;look.progress=1;look.star_scale=g.scale;
    const ::sword_trail::Vec3 base{hand.x-std::sin(yaw)*1200.0,hand.y,hand.z-std::cos(yaw)*1200.0};
    scd::runtime::Submit(look,base,hand);
}

}  // namespace zlr
#include "zelda_companion_runtime.h"   // zelda-companion-01: the AI partner's rays (Link as the player)
namespace zlr {
// The partner's step, at most once per native logic frame (global+0x530C): from Frame (CombinedDecide, only on Link's
// Normal dispatch) and from the player's core (dispatch_hooks::player_frame, PlayerFrame in link_runtime.cpp), which also
// runs on Link's damage and death frames. Without the second her cast froze while Link was hit: the clip weight on her
// +0x2A8 stayed where it was and the ray came late, by the time fallback (review 30-oct).
static uint32_t g_comp_frame=0xFFFFFFFFu;
static void CompanionTick(){
    if(!g_on||!g_trail_on.load(std::memory_order_acquire))return;
    const auto g=CmbGet<uint32_t>(g_cmb_global_slot);if(!link_runtime::IsPointer(g))return;
    const uint32_t frame=CmbGet<uint32_t>(g+0x530C);
    if(frame==g_comp_frame)return;
    g_comp_frame=frame;
    if((CmbGet<uint32_t>(g+0x4FC8)&0xFF)!=0){CompanionEnd("player_type");return;}
    CompanionFrame(link_runtime::sword_trail_log::Ms(GameNow(g_cmb_global_slot)),TrailRoom(),CmbGet<uint32_t>(g_cmb_player_slot));
}
// Startup, before the dispatch hooks are enabled (player_frame is published before Observe; Prepare runs after): whether
// the partner's rays are asked for ([ZeldaLight] Enabled and ZeldaCompanionRay=1). Prepare still decides if they run.
static bool CompanionRequested(const wchar_t* ini){
    auto get=[&](const wchar_t* key,wchar_t* out,unsigned count){GetPrivateProfileStringW(L"ZeldaLight",key,L"",out,count,ini);};
    unsigned extra=0;
    return zl::ReadConfig(get).on&&link_runtime::zelda_companion::ReadConfig(get,extra).on;
}
// Every player dispatch. Off, or the player is not Ashley in play: only the beam ends (room/player change) and nothing is read.
static void Frame(){
    if(!g_on&&!g_cfg.probe)return;
    const auto g=CmbGet<uint32_t>(g_cmb_global_slot);if(!link_runtime::IsPointer(g))return;
    const unsigned pl=CmbGet<uint32_t>(g+0x4FC8)&0xFF;const uint32_t room=TrailRoom();
    if(g_cfg.probe)Probe(pl,room);
    if(!g_on||!g_trail_on.load(std::memory_order_acquire))return;
    const double now_ms=link_runtime::sword_trail_log::Ms(GameNow(g_cmb_global_slot));
    const uint32_t link_player=pl==0?CmbGet<uint32_t>(g_cmb_player_slot):0;
    CompanionTick();   // the partner (Link as the player); the player's core may run it again this frame (deduplicated)
    LocalMemory memory;link_runtime::HealthState health;
    const bool read=pl==1&&link_runtime::ReadHealth(memory,g_cmb_player_slot,g_cmb_global_slot,health);
    const bool zelda=read&&link_runtime::ZeldaInPlay(health)&&SwordForeground();
    const bool raw=pl==1&&(CmbGet<uint32_t>(g_cmb_base+zl::kKeyHeld)&zl::kKnifeBit)!=0;
    const bool started=zl::Trigger(g_press,raw,zelda&&!g_cast.Busy(now_ms),now_ms,g_cfg.cooldown_ms);
    if(started){
        // Toward the target the ray would take (or her own facing): the extended arm points at it (goal 23-oct).
        float face=std::numeric_limits<float>::quiet_NaN(),feet[3]{},at[3]{};
        const float own=zl::Facing(CmbGet<float>(health.player+0xA4));CmbMem fm;
        if(fm.Read(health.player+0x94,feet,12)&&std::isfinite(own)&&PickTarget(feet,own,at))face=zl::YawTo(feet,at[0],at[2]);
        link_runtime::zelda_cast::Begin(now_ms,face,g_cfg.cast_clip);
        CmbLog("zelda_cast face=%.2f own=%.2f target=%u",double(face),double(own),std::isfinite(face)?1U:0U);
        if(link_runtime::zelda_cast::Active(now_ms)){
            g_cast.Begin(now_ms,link_runtime::zelda_cast::ClipCast());CmbLog("zelda_cast begin clip=%u",g_cast.Clip()?1U:0U);}
        else CmbLog("zelda_cast refused (busy, puzzle or damage)");
    }
    // The pose cancelled (damage, an event, a room change): the pending ray goes with it.
    if(g_cast.Busy(now_ms)&&!link_runtime::zelda_cast::Active(now_ms)){g_cast=zl::Cast{};CmbLog("zelda_cast cancelled");}
    if(read&&g_cast.Busy(now_ms)){
        float feet[3]{};const float yaw=zl::Facing(CmbGet<float>(health.player+0xA4));
        CmbMem m;
        if(m.Read(health.player+0x94,feet,12)&&std::isfinite(yaw)&&std::isfinite(feet[0])&&std::isfinite(feet[1])&&std::isfinite(feet[2])){
            float hand[3]{};
            const bool have_hand=link_runtime::zelda_cast::HandWorld(hand);   // her hand bone, last drawn frame
            if(have_hand)g_cast_hand={hand[0],hand[1],hand[2]};
            ::sword_trail::Vec3 impact{};
            double entries=0;   // the clip's release (zelda_cast_clip.h): its SEQ frame, or game time when unreadable
            const bool known=g_cast.Clip()&&link_runtime::zelda_cast::ClipFrame(entries);
            if(g_cast.FireDue(now_ms,g_cast.Clip()&&link_runtime::zelda_cast_clip::ReleaseDue(known,entries,g_cast.Age(now_ms)))){
                if(g_cast.Clip())CmbLog("zelda_cast release age_ms=%.0f seq=%.2f known=%u",g_cast.Age(now_ms),entries,known?1U:0U);
                if(!have_hand)CmbLog("zelda_cast no_hand: the ray is dropped");
                else if(zelda&&Fire(now_ms,health.player,feet,yaw,room,g_cast_hand,impact))Sparks(now_ms,room,health.player,g_cast_hand,impact);
            }
            if(have_hand)HandStar(g_cast.GlowAge(now_ms),g_cast_hand,yaw);
        }
    }
    // The sparks and the flash: this system's frames while the player is Ashley (Link's ParticlesFrame draws his own).
    if(read&&g_part_on.load(std::memory_order_acquire)&&g_part.Busy()){
        g_part.Advance(now_ms,room,health.player);
        if(g_part.Alive())link_runtime::sword_particles::draw::runtime::Submit(g_part,now_ms,g_part_cfg.gain);
    }
    if(!g_beam.Live(now_ms))return;
    static ::sword_trail::Quad quads[16];
    // The beam lives on through frames where the in-play gates blink (the knife press itself sets a native state bit):
    // only a room or player change ends it.
    const int n=g_beam.Frame(now_ms,room,read?health.player:link_player,quads,0,16);
    if(n>0){
        const auto r=link_runtime::sword_trail::ot::runtime::Submit(quads,n,Style());
        CmbLogV("zelda_beam quads=%d submit=%d in_play=%u",n,int(r),zelda?1U:0U);   // LogLevel 2
    }
}

// Startup (StartCombat, after SwordPrepare): the config, the sound, the needs. The trail node is checked per frame.
static void Prepare(const wchar_t* directory,bool dispatch_up){
    wchar_t path[MAX_PATH]{};
    if(link_runtime::paths::Format(path,L"%slink_runtime.ini",directory)<=0)return;
    g_cfg=zl::ReadConfig([&](const wchar_t* key,wchar_t* out,unsigned count){GetPrivateProfileStringW(L"ZeldaLight",key,L"",out,count,path);});
    if(g_cfg.probe)Log("ZeldaLight probe=1");
    if(!g_cfg.on){Log("ZeldaLight ready=0 (Enabled=0)");return;}
    if(!dispatch_up||!g_cmb_adapter_on||!g_bullet_los_ok){
        Log("ZeldaLight disabled: dispatch=%u adapter=%u line_of_sight=%u",dispatch_up?1U:0U,g_cmb_adapter_on?1U:0U,g_bullet_los_ok?1U:0U);return;}
    // The cast pose (zelda_cast_pose_runtime.h): its hooks or no ray at all (an armless cast would fire from nowhere).
    if(!link_runtime::zelda_cast::Install(g_cmb_base,&Log)){Log("ZeldaLight disabled: cast pose hooks failed");return;}
    link_runtime::zelda_cast::detail::log=&CmbLog;   // its begin/end/applied lines come from the game thread: the combat ring
    // zelda-companion-01: the TP cast clip (ZeldaCastClip=1, also the partner's arm with ZeldaCompanionRay=1) and the
    // partner's rays. A missing or changed clip leaves the procedural pose (player) or an armless cast (partner).
    unsigned extra=0;
    CompanionPrepare(link_runtime::zelda_companion::ReadConfig([&](const wchar_t* key,wchar_t* out,unsigned count){
        GetPrivateProfileStringW(L"ZeldaLight",key,L"",out,count,path);},extra));
    if(g_cfg.cast_clip||g_comp_cfg.on){
        namespace zcc=link_runtime::zelda_cast_clip;unsigned char* f=nullptr;unsigned char* q=nullptr;
        const bool clip=CmbResident(directory,zcc::kFcvName,zcc::kFcvBytes,zcc::kFcvSha,f)&&CmbResident(directory,zcc::kSeqName,zcc::kSeqBytes,zcc::kSeqSha,q)&&
            zcc::runtime::Prepare(g_cmb_base,f,q);
        Log("ZeldaCastClip clip=%u requested=%u companion=%u (%s)",clip?1U:0U,g_cfg.cast_clip?1U:0U,g_comp_cfg.on?1U:0U,
            clip?"TP hzelda attack_c on +0x2A8":"missing, changed or unpinned: procedural pose / armless partner cast");
    }
    g_cfg.problems+=extra;
    wchar_t wav[MAX_PATH]{};
    const bool sound=link_runtime::paths::Format(wav,L"%slink_zelda_light.wav",directory)>0&&g_voice.Load(wav,100,kSoundSha);
    g_on=true;
    Log("ZeldaLight ready=1 range=%.0f cooldown=%u ganado=%u knight=%u sound=%u knight_vtable=0x%X problems=%u",double(g_cfg.range),
        g_cfg.cooldown_ms,g_cfg.ganado,g_cfg.knight,sound?1U:0U,zl::kKnightVtable,g_cfg.problems);
}
// Poll thread: the queued sound.
static void PollAudio(){if(g_sound.exchange(false,std::memory_order_acq_rel)&&SwordForeground())g_voice.Play();}
}  // namespace zlr

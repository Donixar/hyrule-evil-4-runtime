#pragma once
#include "runtime_paths.h"
// Included inside link_runtime.cpp's combat block, after its target/adapter/
// prompt declarations. All native access stays on the game thread; worker
// access is restricted to resident audio and copied mailbox tokens.
#include "spin_attack.h"
#include "ending_blow.h"
#include "ending_blow_leap.h"
#include "ending_blow_camera.h"
#include "back_slice_camera.h"
#include "spin_attack_assets.h"
#include "great_spin.h"
#include "sword_breakables.h"
#include "spin_bullets.h"
#include "shield_bash_scan.h"
#include "back_slice.h"
#include "backflip.h"
#include "sword_parts.h"
#include "sword_hand.h"
#include "sword_trail_log.h"
#include "sword_trail_feed.h"
#include "sword_trail_draw_ot.h"
#include "sword_charge_draw.h"
#include "sword_particles_draw.h"

namespace sword=link_runtime::sword_actions;
namespace spin=link_runtime::spin_attack;
namespace ending=link_runtime::ending_blow;
// Back: the Back Slice (back_slice.h), on the spin's clip. Bash: the Shield Bash's TP clip (spin-fix-01, ShieldBashMotion).
// Flip: TP's backflip (backflip.h, backflip-01; Backflip=1), its own clip, no strike.
enum class SwordKind {None,Spin,Ending,Back,Bash,Flip};
static const char* SwordName(SwordKind k){return k==SwordKind::Spin?"spin":k==SwordKind::Back?"back_slice":k==SwordKind::Bash?"shield_bash":
    k==SwordKind::Flip?"backflip":"ending";}
static bool SwordSpinClip(SwordKind k){return k==SwordKind::Spin||k==SwordKind::Back;}
struct SwordClip {unsigned char* fcv=nullptr;unsigned char* seq=nullptr;unsigned strike_tick=0;bool ready=false;};
static SwordClip g_sword_spin_clip,g_sword_ending_clip,g_sword_bash_clip;
// backslice-tp-01 (BackSliceMotion): TP's Back Slice, one clip per roll direction (back_slice.h ClipRoll: D = side -1 =
// toward Link's right = the right clip; backslice-tp-02 fixed the swap), one shared voice + sword SE.
static SwordClip g_sword_back_right,g_sword_back_left;
static bool g_back_motion=false;   // both clips and the voice loaded: the Back Slice plays them (else the spin clip)
static spin::Charge g_sword_charge;
static ending::Press g_sword_press;
static mortal::voice::Mailbox g_sword_spin_voice,g_sword_ending_voice,g_sword_back_voice;
static mortal::voice::Consumer g_sword_spin_consumer,g_sword_ending_consumer,g_sword_back_consumer;
static AlertVoice g_sword_spin_audio,g_sword_ending_audio,g_sword_bash_audio;   // bash: loaded with its clip, not played (link_shield_bash.wav is)
static AlertVoice g_sword_back_audio;
// backflip-01 (Backflip=1): TP's backflip clip (flip + landing) and its voice Z2SE_AL_V_BACKTEN; missing = no backflip.
static SwordClip g_sword_flip_clip;
static mortal::voice::Mailbox g_sword_flip_voice;
static mortal::voice::Consumer g_sword_flip_consumer;
static AlertVoice g_sword_flip_audio;
struct SwordRequest {
    SwordKind kind=SwordKind::None;double time=0;uint32_t scene=0,player=0,weapon=0,frame=0;
    sword::target::Target target;
    sword::target::Bodies bodies;   // other Ganados at the press (leap body scan); empty with the leap off
    bool great=false;int hp=0,hp_max=0;   // Spin: the Great Spin (great_spin.h), decided at the release with full HP
    link_runtime::back_slice::Plan back;  // Back: the arc decided at the tap
    link_runtime::backflip::Plan flip;    // Flip: the travel decided at the tap
};
struct SwordAction {
    SwordRequest request;uint64_t lease=0;double start=0;bool struck=false;unsigned strike_tick=0;
    // Ending: the target was down at the start (lethal on rise, Revalidate rise); the last hit's kind for ending_survived.
    bool grounded=false,rising=false;unsigned kind=0;
    sword::Interrupt interruption=sword::Interrupt::None;
    sword::HitQueue hits;
};
static SwordRequest g_sword_request;
static SwordAction g_sword_action;
static SwordKind g_sword_starting=SwordKind::None;
static mctx::Sample g_sword_sample;
static bool g_sword_sample_player=false;
static bool SwordBusy(){return g_sword_action.lease||g_sword_request.kind!=SwordKind::None||g_sword_starting!=SwordKind::None;}
// shield-guard-02 (GuardTick): the Shield Bash and the Back Slice are TP clips with the shield on the left arm; the
// hand-shield body is shown from the start request to the end of the clip.
static bool SwordShieldInHand(){
    const SwordKind k=g_sword_action.lease?g_sword_action.request.kind:g_sword_starting;
    return k==SwordKind::Bash||k==SwordKind::Back||k==SwordKind::Flip;
}
static sword::target::Config SwordTargets(){
    sword::target::Config c;c.line_waiver=g_cmb.line_waiver;c.strike_radius=float(g_cmb.ending_strike_range);
    c.leap=g_cmb.ending_leap!=0;c.leap_range=float(g_cmb.leap_range);c.leap_free_range=float(g_cmb.leap_free_range);
    c.strike_band=ending::height::AtStrike(float(g_cmb.leap_standoff),float(g_cmb.ending_strike_range));return c;
}
// ---- Great Spin and Shield Bash (great_spin.h, shield_bash.h; 18-oct) -----------------------------------------------
namespace gspin=link_runtime::great_spin;
namespace sbash=link_runtime::shield_bash;
// The spin's targets; the Great Spin only widens the radius (same 400 height band, same rules).
static sword::target::Config SwordSpinTargets(bool great){
    auto c=SwordTargets();if(great)c.spin_radius=float(g_cmb.great.radius);return c;
}
// Bin32 sounds (scripts/combat_gs_sb_sounds.py), SHA-pinned; the worker plays them (SwordPollAudio).
static link_runtime::ztarget::voice::Voice g_great_spin_sound,g_shield_bash_sound;
static std::atomic<unsigned> g_gssb_sound{0};   // bit 1 Great Spin, bit 2 Shield Bash, 4 sword charge; game thread raises, worker takes
static constexpr unsigned kGssbVolume=60;
// The parry's spark emitter, whichever startup verified it (ParryActive or ParryGeneralBlock); unverified = no sparks.
static const link_runtime::parry::spark::Code& GreatSparkCode(){return g_parry_sparks.verified?g_parry_sparks:g_gen.sparks;}
// HP at the release (fairy_revive.h layout: global +0x4FB4 hp, +0x4FB6 max, int16).
static bool GreatHealth(int& hp,int& hp_max){
    const auto g=CmbGet<uint32_t>(g_cmb_global_slot);hp=hp_max=0;
    if(!link_runtime::IsPointer(g))return false;
    hp=CmbGet<int16_t>(g+0x4FB4);hp_max=CmbGet<int16_t>(g+0x4FB6);return true;
}
// Sparks on a ring around Link at the strike (cosmetic; GreatSpinSparkles=0 = none).
static unsigned GreatSparkle(const CmbPlayer& p){
    if(!g_cmb.great.sparkles)return 0;
    float at[gspin::kMaxSparkles][3]{},rot[gspin::kMaxSparkles][3]{};
    const float center[3]={p.pos.x,p.pos.y,p.pos.z};
    const unsigned n=gspin::Ring(center,0.6f*float(g_cmb.great.radius),900.f,g_cmb.great.sparkles,at,rot);
    return n?link_runtime::parry::spark::EmitFree(GreatSparkCode(),at,rot,n):0;
}
// Shield Bash state (game thread).
static sbash::Tap g_bash_tap;
static bool g_bash_ready=false,g_bash_deflect_ok=false,g_bash_window=false;
static double g_bash_window_start=0;
static uint32_t g_bash_window_scene=0,g_bash_serial=0;
static unsigned g_bash_window_deflected=0;
static void BashWindowEnd(const char* why){
    if(!g_bash_window)return;
    g_bash_window=false;CmbLog("shield_bash_window_end serial=%u deflected=%u why=%s",g_bash_serial,g_bash_window_deflected,why);
}
static bool BashPublish(const mdmg::PublishArgs& a){
    __try{mdmg::CallPublisher(g_cmb_base+mdmg::layout::publisher_rva,a);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// spin-fix-01: non-lethal knockback. Every kick record the bash writes is claimed; its calc returns KnockbackDamage.
static sbash::Claims g_bash_claims;
static bool BashRecord(uint32_t actor,mdmg::Record& r){CmbMem m;return mdmg::ReadRecord(m,actor,r);}
static bool BashClaim(uint32_t rva,uint32_t actor,unsigned weapon,int original,int hp){
    if(!g_bash_claims.Active()||!g_bash_claims.Claim(rva,actor,weapon,BashRecord))return false;
    CmbLog("shield_bash_damage actor=0x%08X weapon=0x%02X original=%d result=%d hp=%d",actor,weapon,original,sbash::KnockbackDamage(hp),hp);
    return true;
}
static void BashClaimsTick(){if(g_bash_claims.Active())g_bash_claims.Tick(BashRecord);}
static bool SwordChargeRoutine(uint32_t routine,bool knife);
static void ChargeEvent(spin::Event event,double time60,uint32_t player);   // the sword charge glow (below)
// ---- Back Slice (back_slice.h; 18-oct) ------------------------------------------------------------------------------
namespace bks=link_runtime::back_slice;
static bks::Circle g_back_circle;
static bool g_back_have_last=false;static double g_back_last=0;
struct BackLock {bool valid=false;unsigned index=0;uint32_t pointer=0;uint8_t id=0,type=0;int side=0;bool back=false;};
static BackLock g_back_lock;   // captured at the tap's press (the knife suspends the Z lock right after)
// The Back Slice's checks reuse the spin's (Revalidate ending=false): reach = BackSlice reach, band 700.
static sword::target::Config BackTargets(){auto c=SwordTargets();c.spin_radius=float(g_cmb.back.reach);c.vertical=g_cmb.back.band;return c;}
// The arc's config: on TP's clip it lasts the roll (the clip's arc ticks) along TP's turn curve (back_slice.h WithClip).
static bks::Config BackConfig(){return g_back_motion?bks::WithClip(g_cmb.back,sword::assets::back_slice_arc_ticks):g_cmb.back;}
static const SwordClip& BackClip(int side){return bks::ClipRoll(side)==bks::Roll::Right?g_sword_back_right:g_sword_back_left;}
// The Z lock now: the locked candidate, else the lock the knife latched (ztarget_runtime.h KnifeLatch).
static BackLock BackCurrentLock(){
    namespace zr=link_runtime::ztarget::runtime;BackLock l;
    if(zr::controller.phase==link_runtime::ztarget::Phase::Locked&&zr::locked.identity&&zr::locked.pointer){
        l.valid=true;l.index=zr::locked.index;l.pointer=zr::locked.pointer;l.id=zr::locked.raw.id;l.type=zr::locked.raw.type;return l;}
    if(const auto* k=zr::Latched()){l.valid=true;l.index=k->index;l.pointer=k->pointer;l.id=k->id;l.type=k->type;}
    return l;
}
// The locked actor as a combat target (same manager slot, pointer, id/type, a live Ganado), else false.
static bool BackTarget(const BackLock& l,sword::target::Target& t,mtg::Raw& r){
    if(!l.valid)return false;
    CmbMem m;mtg::Manager pool;
    if(mtg::ReadManager(m,g_cmb_base,mtg::Config{},pool)!=mtg::Reject::None||!sword::target::Pool(pool)||l.index>=pool.count||
       pool.array+l.index*pool.block!=l.pointer||mtg::ReadRaw(m,l.pointer,r)!=mtg::Reject::None||r.id!=l.id||r.type!=l.type||
       !sword::target::Actor(r,g_cmb_base))return false;
    const uint64_t identity=g_cmb_tracker.Observe(l.index,l.pointer,(r.be_flag&0x201)==1,r);
    if(!identity||!g_cmb_tracker.Scene())return false;
    t={identity,l.pointer,l.index,g_cmb_tracker.Scene(),r.id,r.type,0.f,true};return true;
}
static void BackAbort(uint32_t frame,const char* why,const bks::Plan* p=nullptr){
    if(p)CmbLog("back_slice abort frame=%u reason=%s side=%d d=%.0f dy=%.0f",frame,why,p->side,double(p->d),double(p->dy));
    else CmbLog("back_slice abort frame=%u reason=%s",frame,why);
}
// The tap with A/D held (BashArbiter): the request SwordDecide starts on the spin's clip.
static void BackRequest(const sword::Input& input,const CmbPlayer& p,int side){
    const uint32_t frame=static_cast<uint32_t>(input.frame);
    sword::target::Target t;mtg::Raw r;
    bool ok=BackTarget(g_back_lock,t,r);
    if(!ok)ok=BackTarget(BackCurrentLock(),t,r);
    const auto plan=bks::Decide(g_cmb.back,side,ok,{p.pos.x,p.pos.y,p.pos.z},{r.pos.x,r.pos.y,r.pos.z},g_back_have_last,g_back_last,input.time);
    if(plan.why!=bks::Why::Ok){BackAbort(frame,bks::WhyName(plan.why),&plan);return;}
    if(!g_sword_spin_clip.ready||!g_cmb.line_waiver){BackAbort(frame,"no_clip");return;}
    t.distance=plan.d;
    g_sword_request={SwordKind::Back,input.time,input.scene,input.player,input.weapon,frame,t};
    g_sword_request.back=plan;
    CmbLog("back_slice request frame=%u side=%s actor=0x%08X id=0x%02X type=0x%02X d=%.0f dy=%.0f radius=%.0f sweep_deg=%.0f",frame,
        side<0?"right":"left",t.pointer,t.id,t.type,double(plan.d),double(plan.dy),double(plan.radius),double(plan.sweep)*57.29578);
}
// ---- Backflip (backflip.h; backflip-01, 30-oct) --------------------------------------------------------------------
namespace bfl=link_runtime::backflip;
static bfl::Move g_flip_move;
static bool g_flip_have_last=false;static double g_flip_last=0;
static constexpr unsigned kFlipNoStrike=0xFFFFFFFFU;   // strike tick past any lease: SwordDecide's strike never runs
// The travel lasts the clip's flip (its travel ticks).
static bfl::Config FlipConfig(){return bfl::WithClip(g_cmb.flip,sword::assets::backflip_travel_ticks,sword::assets::backflip_land_ticks);}
// The tap with S held (BashArbiter, bfl::Pick): straight back from Link's facing, or away from the Z-locked Ganado.
static void FlipRequest(const sword::Input& input,const CmbPlayer& p){
    const uint32_t frame=static_cast<uint32_t>(input.frame);
    if(!g_sword_flip_clip.ready){CmbLog("backflip abort frame=%u reason=no_clip",frame);return;}
    sword::target::Target t;mtg::Raw r{};
    bool locked=BackTarget(g_back_lock,t,r);
    if(!locked)locked=BackTarget(BackCurrentLock(),t,r);
    const float yaw=input.player?CmbGet<float>(p.player+0xA4):0.f;
    const auto plan=bfl::Decide(FlipConfig(),{p.pos.x,p.pos.y,p.pos.z},yaw,locked,{r.pos.x,r.pos.y,r.pos.z},g_flip_have_last,g_flip_last,input.time);
    if(plan.why!=bfl::Why::Ok){CmbLog("backflip abort frame=%u reason=%s locked=%u",frame,bfl::WhyName(plan.why),locked?1U:0U);return;}
    g_sword_request={SwordKind::Flip,input.time,input.scene,input.player,input.weapon,frame,locked?t:sword::target::Target{}};
    g_sword_request.flip=plan;
    CmbLog("backflip request frame=%u locked=%u actor=0x%08X yaw=%.3f dir=%.3f,%.3f distance=%.0f",frame,locked?1U:0U,locked?t.pointer:0U,
        double(plan.yaw0),double(plan.dir_x),double(plan.dir_z),double(plan.distance));
}
// The bash itself (shield_bash.h): Ganados in front take the native kick record (claimed: KnockbackDamage), the weapon
// deflect window opens, the sound plays. pose: Link's parry reaction layer as the shove (the bash without its clip).
static void BashFire(uint32_t frame,double time,uint32_t scene,const CmbPlayer& p,bool pose){
    const float yaw=p.player?CmbGet<float>(p.player+0xA4):0.f;
    ++g_bash_serial;CmbMem m;
    const auto r=sbash::HitGanados(m,g_cmb_base,p.pos,yaw,g_cmb.bash,[](const mdmg::PublishArgs& a){const bool ok=BashPublish(a);if(ok)g_bash_claims.Add(a);return ok;});
    g_gssb_sound.fetch_or(2,std::memory_order_acq_rel);
    const uint32_t owner=r.pick.count?(r.pick.items[0].id|uint32_t(r.pick.items[0].type)<<8):0;
    if(pose&&g_rx)g_rx->OnCommitted(0x20000000U|g_bash_serial,owner);
    unsigned deflected=0;
    if(g_cmb.bash.deflect_ms&&g_bash_deflect_ok){
        const auto w=sbash::DeflectWeapons(m,g_cmb_base,p.pos,yaw,g_cmb.bash,true);deflected=w.deflected;
        BashWindowEnd("new_bash");
        g_bash_window=true;g_bash_window_start=time;g_bash_window_scene=scene;g_bash_window_deflected=deflected;
        for(unsigned i=0;i<w.listed;++i)
            CmbLog("shield_bash_deflect serial=%u frame=%u weapon=0x%08X routine=%u distance=%.0f",g_bash_serial,
                frame,w.object[i],static_cast<unsigned>(w.routine[i]),w.distance[i]);
    }
    // pose=2: the clip is the motion (no parry reaction layer).
    CmbLog("shield_bash serial=%u frame=%u yaw=%.3f targets=%u published=%u scanned=%u cone_out=%u down=%u heavy=%u far=%u pending=%u "
        "deflected=%u window_ms=%u pose=%u",g_bash_serial,frame,yaw,r.pick.count,r.published,r.pick.scanned,r.cone,
        r.pick.skips[unsigned(sbash::ose::Skip::Down)],r.pick.skips[unsigned(sbash::ose::Skip::Heavy)],r.pick.skips[unsigned(sbash::ose::Skip::Far)],
        r.pick.skips[unsigned(sbash::ose::Skip::Pending)],deflected,g_bash_deflect_ok?g_cmb.bash.deflect_ms:0U,pose?(g_rx?1U:0U):2U);
    for(unsigned i=0;i<r.pick.count&&i<sbash::kMaxTargets;++i)
        CmbLog("shield_bash_target serial=%u actor=0x%08X id=0x%02X type=0x%02X distance=%.0f shot=%s",g_bash_serial,r.pick.items[i].actor,
            r.pick.items[i].id,r.pick.items[i].type,r.pick.items[i].distance,sbash::ose::ShotName(r.result[i]));
}
// The arbiter's exit, after the sword input of this frame: the tap, the bash and the deflect window.
static void BashArbiter(const sword::Input& input,bool priority,bool occupied,const CmbPlayer& p){
    if(!g_bash_ready)return;
    namespace zr=link_runtime::ztarget::runtime;
    const float yaw=input.player?CmbGet<float>(p.player+0xA4):0.f;
    if(g_bash_window){
        if(!input.valid||input.scene!=g_bash_window_scene||!std::isfinite(input.time)||input.time<g_bash_window_start-link_runtime::game_clock::kEpsilon)
            BashWindowEnd("context");
        else if(link_runtime::game_clock::Exceeded(input.time-g_bash_window_start,g_cmb.bash.DeflectFrames()))BashWindowEnd("time");
        else{
            CmbMem m;const auto w=sbash::DeflectWeapons(m,g_cmb_base,p.pos,yaw,g_cmb.bash,true);
            g_bash_window_deflected+=w.deflected;
            for(unsigned i=0;i<w.listed;++i)
                CmbLog("shield_bash_deflect serial=%u frame=%u weapon=0x%08X routine=%u distance=%.0f",g_bash_serial,
                    static_cast<unsigned>(input.frame),w.object[i],static_cast<unsigned>(w.routine[i]),w.distance[i]);
        }
    }
    sbash::Input s;s.frame=input.frame;s.time=input.time;s.scene=input.scene;s.player=input.player;
    s.valid=input.valid&&!occupied&&!SwordBusy()&&zr::live.load(std::memory_order_acquire);
    s.ready=!priority&&SwordChargeRoutine(p.routine,input.knife);
    s.knife=input.knife;s.knife_edge=input.knife_edge;s.fire=input.fire||input.fire_edge;s.z=s.valid&&zr::Held();
    const auto e=g_bash_tap.Step(s);
    if(e!=sbash::Event::None&&e!=sbash::Event::Bash)
        CmbLogV("shield_bash_input frame=%u event=%s",static_cast<unsigned>(input.frame),sbash::EventName(e));
    // Back Slice (back_slice.h): A/D (or the stick sideways) at the press, else at the release, turns the tap into it.
    // Backflip (backflip.h, Backflip=1): S (back) likewise; bfl::Pick decides (A/D with a target first).
    const int side_now=bks::Side(g_sword_sample.on);
    const bool back_now=g_cmb.Backflip()&&bfl::Back(g_sword_sample.on);
    if(e==sbash::Event::Pressed){
        g_back_lock=g_cmb.BackSlice()||g_cmb.Backflip()?BackCurrentLock():BackLock{};g_back_lock.side=side_now;g_back_lock.back=back_now;}
    if(e!=sbash::Event::Bash)return;
    const int side=g_back_lock.side?g_back_lock.side:side_now;
    const bool back=g_back_lock.back||back_now;
    // shield-guard-01: the Back Slice needs a real target to circle. Without one (neutral Z-target, or a lock lost
    // since the press) the A/D tap is the Shield Bash, so strafing and tapping in neutral always does something.
    const bool slice=g_cmb.BackSlice()&&side&&(g_back_lock.valid||BackCurrentLock().valid);
    const auto tap=bfl::Pick(g_cmb.BackSlice(),side,slice,g_cmb.Backflip(),back);
    if(tap==bfl::Tap::BackSlice){BackRequest(input,p,side);g_back_lock={};return;}
    if(tap==bfl::Tap::Backflip){FlipRequest(input,p);g_back_lock={};return;}
    if(g_cmb.BackSlice()&&side)CmbLog("back_slice no_target frame=%u side=%d: shield bash instead",static_cast<unsigned>(input.frame),side);
    g_back_lock={};
    if(!g_cmb.ShieldBash())return;
    // spin-fix-01: with its TP clip the bash is a sword request; the push lands on the clip's impact frame (SwordDecide).
    if(g_cmb.bash.motion&&g_sword_bash_clip.ready){
        g_sword_request={SwordKind::Bash,input.time,input.scene,input.player,input.weapon,static_cast<uint32_t>(input.frame),{}};
        CmbLog("shield_bash request frame=%u strike_tick=%u",static_cast<unsigned>(input.frame),g_sword_bash_clip.strike_tick);
        return;
    }
    BashFire(static_cast<uint32_t>(input.frame),input.time,input.scene,p,true);
}
// Ending Blow leap (ending_blow_leap.h): mode 0 (default) leaves the in-place jump untouched.
static ending::leap::Leap g_sword_leap;
static ending::leap::Config SwordLeapConfig(){
    ending::leap::Config c;c.mode=g_cmb.ending_blow?g_cmb.ending_leap:0;c.range=float(g_cmb.leap_range);
    c.free_range=float(g_cmb.leap_free_range);c.standoff=float(g_cmb.leap_standoff);c.strike=float(g_cmb.ending_strike_range);
    c.at_strike=ending::height::AtStrike(c.standoff,c.strike);   // the same band as SwordTargets().strike_band
    c.prefer_lock=g_cmb.leap_prefer_lock;c.burst=float(g_cmb.leap_burst)/100.f;return c;   // EndingBlowLeapBurst 100..300 -> 1..3
}
static void SwordLeapLog(){
    char line[sizeof(CmbLine::text)];
    if(ending::leap::Format(line,sizeof(line),g_sword_leap.summary())>0)CmbLog("%s",line);
}
static void SwordLeapSettleLog(){   // the Y-only settle after a stop on a descent (ending_blow_leap.h Settle, 24-oct)
    char line[sizeof(CmbLine::text)];
    if(ending::leap::FormatSettle(line,sizeof(line),g_sword_leap.summary())>0)CmbLog("%s",line);
}
// Ending Blow camera lift (ending_blow_camera.h; its stage is in ztarget_runtime.h): this lease's facts, read on the
// game thread inside the camera's hitCheck (CameraMove runs after the player's dispatch, on the same thread).
static bool SwordCameraFacts(ending::camera::Facts& f){
    const auto& a=g_sword_action;f={};
    if(!a.lease)return true;
    const auto& run=mortal::dispatch_hooks::runtime;
    f.lease=a.lease;f.ending=a.request.kind==SwordKind::Ending;f.clean=a.interruption==sword::Interrupt::None;
    f.owned=run.active&&run.lease.token==a.lease;f.e=GameNow(g_cmb_global_slot)-a.start;
    f.mot=CmbGet<float>(a.request.player+0x1FC);   // MOTION_INFO Mot_frame (cModel +0x1D8 +0x24), log only
    return true;
}
// Before ztarget::runtime::Start: the lift's request. Only with the Ending Blow ready (its own lines say why not); the
// lift's ready line names a refusal: off (EndingBlowCameraLift=0), clip_mismatch (the curve follows the 2 m clip only).
static void SwordCameraPrepare(){
    namespace zr=link_runtime::ztarget::runtime;auto& r=zr::lift_request;r={};
    if(!g_cmb.ending_blow||!g_sword_ending_clip.ready)return;
    r.requested=true;r.percent=g_cmb.camera_lift;r.observe=g_cmb.camera_lift_observe;
    if(!r.percent)r.refused="off";
    else if(std::strcmp(sword::assets::ending_blow.fcv_sha,ending::camera::kClipSha))r.refused="clip_mismatch";
    else if(!g_cmb_global_slot)r.refused="no_clock";
    zr::lift_source=&SwordCameraFacts;zr::lift_log=&CmbLog;zr::lift_log_v=&CmbLogV;
    zr::lift_verbose=link_runtime::log::Writes(g_log_level,link_runtime::log::kVerbose);
}
// Back Slice camera (back_slice_camera.h, backslice-cam-01; its stages are in ztarget_runtime.h): this lease's facts,
// read on the game thread inside CameraControl::Check (the fall hold) and hitCheck (the orbit), both inside CameraMove,
// after the player's dispatch. jump: TP's clip plays (its twirl jump; the spin clip has none).
static bool SwordBackCameraFacts(link_runtime::back_slice::camera::Facts& f){
    const auto& a=g_sword_action;f={};
    if(!a.lease)return true;
    const auto& run=mortal::dispatch_hooks::runtime;
    f.lease=a.lease;f.back=a.request.kind==SwordKind::Back;f.clean=a.interruption==sword::Interrupt::None;
    f.owned=run.active&&run.lease.token==a.lease;f.jump=f.back&&g_back_motion;f.e=GameNow(g_cmb_global_slot)-a.start;
    return true;
}
// Before ztarget::runtime::Start: the Back Slice camera's request. Only with the Back Slice on (its ready line says why
// not): off (BackSliceCamera=0), no_clock.
static void SwordBackCameraPrepare(){
    namespace zr=link_runtime::ztarget::runtime;auto& r=zr::orbit_request;r={};
    if(!g_cmb.BackSlice())return;
    r.requested=true;r.config.ms=g_cmb.back_camera_ms;r.config.lift=g_cmb.back_camera_lift;
    if(!g_cmb.back_camera)r.refused="off";
    else if(!g_cmb_global_slot)r.refused="no_clock";
    zr::orbit_source=&SwordBackCameraFacts;zr::orbit_log=&CmbLog;
}
// The Z-lock the knife key suspended (ztarget_runtime.h KnifeLatch), offered to this scene's selection.
static bool SwordLatch(sword::target::Prefer& out){
    if(!g_cmb.ending_leap||!g_cmb.leap_prefer_lock)return false;
    const auto* l=link_runtime::ztarget::runtime::Latched();if(!l)return false;
    out={l->index,l->pointer,g_cmb_epoch,l->id,l->type,l->age};return true;
}
// ending_none (ending_blow_target.h Diagnose): a knife attack press the Ending Blow refused because the
// selection found nothing, logged only with a latched Z-lock or a Ganado down near Link. Feature line
// (LogLevel 1 and 2, like the other ending_* lines). Reads only: the decision is already made.
static void SwordEndingNone(uint32_t frame,const CmbPlayer& p,const sword::target::Prefer* prefer){
    if(!link_runtime::log::Writes(g_log_level,link_runtime::log::kNormal))return;   // LogLevel 0: not even the reads
    CmbMem m;
    const auto r=sword::target::Diagnose(m,g_cmb_base,g_cmb_tracker.Scene(),p.pos,CmbGet<float>(p.player+0xA4),SwordTargets(),prefer);
    if(r.why==sword::target::Miss::None)return;
    char line[sizeof(CmbLine::text)];
    if(sword::target::FormatMiss(line,sizeof(line),frame,r)>0)CmbLog("%s",line);
}
// ending_gate (24-oct, bug 5): a knife fire press refused before the selection ran (not in the knife hold 0x01010B00,
// native priority, another action running or queued, invalid input), logged only when Diagnose finds a Ganado down or
// getting up near Link (reason= is what the selection would have said). Feature line, reads only.
static void SwordEndingGate(uint32_t frame,const CmbPlayer& p,const sword::Input& in,bool priority,bool occupied,const mctx::Sample& s){
    if(!link_runtime::log::Writes(g_log_level,link_runtime::log::kNormal)||!in.player)return;
    CmbMem m;sword::target::Prefer prefer;const bool latched=SwordLatch(prefer);
    const auto r=sword::target::Diagnose(m,g_cmb_base,g_cmb_tracker.Scene(),p.pos,CmbGet<float>(p.player+0xA4),SwordTargets(),
        latched?&prefer:nullptr);
    if(r.why==sword::target::Miss::None||r.why==sword::target::Miss::NotDown)return;
    CmbLog("ending_gate frame=%u routine=0x%08X valid=%u priority=%u offers=%u lock=%u active=%u occupied=%u request=%u reason=%s d=%.0f dy=%.0f "
        "actor=0x%08X r=%u/0x%02X",frame,p.routine,in.valid?1U:0U,priority?1U:0U,unsigned(s.offers),unsigned(s.lock),unsigned(s.active),
        occupied?1U:0U,unsigned(g_sword_request.kind),sword::target::MissName(r.why),double(r.d),double(r.dy),r.pointer,unsigned(r.r0),unsigned(r.r1));
}
// Corridor guard at the press (ending_blow_leap.h Corridor, EndingBlowLeap=1 only): the selected target's root, the
// other Ganados of the same pool pass and the clip's strike tick. Reads only; an unreadable or changed target is
// left to the pre-start check (target_lost / target_out_of_reach / target_left_floor).
static ending::leap::Guard SwordCorridor(const CmbPlayer& p,const sword::target::Target& t,const sword::target::Bodies& all,mtg::Raw& r){
    namespace lp=ending::leap;
    if(!g_sword_leap.config().On())return {};
    CmbMem m;if(mtg::ReadRaw(m,t.pointer,r)!=mtg::Reject::None||r.id!=t.id||r.type!=t.type)return {};
    const auto others=sword::target::Others(all,t);lp::Body b[sword::target::kMaxBodies];
    for(unsigned i=0;i<others.count;++i)b[i]={others.items[i].x,others.items[i].y,others.items[i].z};
    return lp::Corridor(g_sword_leap.config(),{p.pos.x,p.pos.y,p.pos.z},{r.pos.x,r.pos.y,r.pos.z},g_sword_ending_clip.strike_tick,b,others.count);
}
// ending_none reason=bodies: the press the corridor guard refused (feature line, like ending_none).
static void SwordEndingBodies(uint32_t frame,const CmbPlayer& p,const sword::target::Target& t,const mtg::Raw& r,
                              const ending::leap::Guard& g,const sword::target::Prefer* prefer){
    if(!link_runtime::log::Writes(g_log_level,link_runtime::log::kNormal))return;
    const auto rep=sword::target::BodiesReport(r,t,p.pos,CmbGet<float>(p.player+0xA4),SwordTargets(),prefer,g.k.bodies,
        g.k.why==ending::leap::Why::Ok?g.k.travel:0.f,g.land);
    char line[sizeof(CmbLine::text)];
    if(sword::target::FormatMiss(line,sizeof(line),frame,rep)>0)CmbLog("%s",line);
}
static uint32_t SwordWeapon(){const auto g=CmbGet<uint32_t>(g_cmb_global_slot);return g?CmbGet<uint8_t>(g+0x4FC0):0xFFFFFFFF;}
static bool SwordForeground(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
static bool SwordPriority(const mctx::Sample& s){return !s.readable||!s.after_arbiter||s.offers||s.active||s.lock||s.actions_disabled;}
// The Ending Blow's native priority (24-oct, bug 5): the same without a merely queued offer. A pickup prompt (kind 0x28,
// the items a dead Ganado drops) or a kick prompt near Link only offers the action button; the knife press is fire, and
// the native knife slash plays through them, so the Ending Blow that replaces it may too. Logged: a leap interrupted
// 4 game frames in (ending_interrupt reason=4) the frame a pickup prompt appeared, and native slashes pressed with a
// prompt up. An offer that fires (active), the lock, the early-outs and a held action or aim key still win.
static bool SwordEndingPriority(const mctx::Sample& s){return !s.readable||!s.after_arbiter||s.active||s.lock||s.actions_disabled;}
static void SwordInterruptActive(const sword::Input& now,bool priority,bool ending_priority){
    auto& a=g_sword_action;if(!a.lease||a.interruption!=sword::Interrupt::None)return;
    if(a.request.kind==SwordKind::Ending)priority=ending_priority;
    sword::Input origin;origin.time=a.start;origin.scene=a.request.scene;origin.player=a.request.player;origin.weapon=a.request.weapon;
    auto& run=mortal::dispatch_hooks::runtime;
    const bool owns=run.active&&run.lease.token==a.lease&&run.Owns(reinterpret_cast<unsigned char*>(now.player));
    a.interruption=sword::ActiveInterrupt(origin,now,owns,priority);
    if(a.interruption==sword::Interrupt::None)return;
    // Arbiter/Scenario/EmMgr run before the player's dispatch. Withdraw only our
    // override now; cancel the owned animation later, inside its handler.
    a.hits.Cancel(g_cmb_adapter);g_sword_spin_voice.Stop(a.lease);g_sword_ending_voice.Stop(a.lease);g_sword_back_voice.Stop(a.lease);
    g_sword_flip_voice.Stop(a.lease);
    CmbLog("%s_interrupt lease=%llu reason=%u override_withdrawn=1",SwordName(a.request.kind),a.lease,static_cast<unsigned>(a.interruption));
}
static void SwordBeforeResolve(){
    if(!g_sword_action.lease)return;
    CmbPlayer p;const bool valid=CmbReadPlayer(p);if(valid)CmbScene(p.player);
    sword::Input input;input.time=GameNow(g_cmb_global_slot);input.scene=g_cmb_epoch;
    input.player=valid?p.player:0;input.weapon=SwordWeapon();
    input.valid=valid&&p.facts.gameplay&&!p.damaged&&SwordForeground()&&g_oca_session.state()==oca::State::Idle;
    SwordInterruptActive(input,false,false);
}
static bool SwordKnifeHold(uint32_t routine){return routine==0x01010B00U;}
static bool SwordChargeRoutine(uint32_t routine,bool knife){
    // Native knife draw/hold is allowed while charging; a released knife may
    // already be entering knife-down before the arbiter runs. Attack R2=2 is out.
    return CmbStartRoutine(routine)||((routine&0xFFFF)==0x0B00&&
        (((routine>>16)&0xFF)<=1||(!knife&&((routine>>16)&0xFF)==3)));
}
// Game frames a queued request may wait for a startable routine: the Back Slice's tap releases mid knife draw, and the
// native put-away runs before the clip can start (spin-fix-01); the spin and the Ending Blow keep 15.
static double SwordRequestFrames(SwordKind k){return k==SwordKind::Back||k==SwordKind::Bash||k==SwordKind::Flip?30.0:15.0;}
static void SwordDropRequest(const char* why){
    if(g_sword_request.kind!=SwordKind::None)CmbLog("%s_request cancelled=%s",SwordName(g_sword_request.kind),why);
    g_sword_request={};
}
// The arbiter sees only native offers; subtract our own NULL-function prompt
// from the post sample, just as contextual Mortal Draw does. Do not replace
// logical knife/fire with MortalDrawKey's physical E mapping.
static void SwordArbiter(mctx::Sample sample,bool player,const CmbPlayer& p){
    if(!g_cmb.SwordActions())return;
    if(g_cmb_prompt_cfg.on)mdp::ClaimOwnOffer(sample,g_cmb_prompt.Injected());
    g_sword_sample=sample;g_sword_sample_player=player;
    sword::Input input;
    input.frame=sample.frame;input.time=GameNow(g_cmb_global_slot);input.scene=g_cmb_epoch;
    input.player=player?p.player:0;input.weapon=SwordWeapon();
    input.knife=(sample.on&mctx::layout::key_knife_ready)!=0;input.knife_edge=(sample.trg&mctx::layout::key_knife_ready)!=0;
    input.fire=(sample.on&mctx::layout::key_fire)!=0;input.fire_edge=(sample.trg&mctx::layout::key_fire)!=0;
    input.valid=sample.readable&&sample.after_arbiter&&player&&p.facts.gameplay&&!p.damaged&&SwordForeground();
    const bool occupied=g_oca_session.state()!=oca::State::Idle||(g_rx&&g_rx->Active())||
        (g_cmb_md&&(g_cmb_md->Active()||g_cmb_md->Requested()))||mortal::dispatch_hooks::runtime.active;
    const bool keys=(sample.on&(mctx::layout::key_action|mctx::layout::key_ready))!=0;
    const bool priority=SwordPriority(sample)||keys;
    const bool ending_priority=SwordEndingPriority(sample)||keys;   // without a merely queued offer (above)
    SwordInterruptActive(input,priority||g_oca_session.state()!=oca::State::Idle,ending_priority||g_oca_session.state()!=oca::State::Idle);
    input.safe=input.valid&&!priority&&!occupied&&SwordChargeRoutine(p.routine,input.knife);
    BashArbiter(input,priority,occupied,p);   // Shield Bash (shield_bash.h): Z key held + knife tap; inert when off
    // spin-fix-01: a queued request is kept through the knife's own draw/put-away (sword::PendingRoutine) and dropped
    // with the reason named (was one context_or_native for all; every Back Slice died on its own release frame).
    if(g_sword_request.kind!=SwordKind::None){
        sword::Pending pn;pn.valid=input.valid;pn.priority=g_sword_request.kind==SwordKind::Ending?ending_priority:priority;
        pn.occupied=occupied;pn.time=input.time;
        pn.scene=input.scene;pn.player=input.player;pn.weapon=input.weapon;
        pn.routine_ok=sword::PendingRoutine(p.routine,input.knife,CmbStartRoutine(p.routine));
        const auto keep=sword::KeepRequest(pn,g_sword_request.scene,g_sword_request.player,g_sword_request.weapon,g_sword_request.time,
            SwordRequestFrames(g_sword_request.kind));
        if(keep!=sword::Keep::Ok){
            if(keep==sword::Keep::Routine)CmbLog("%s_request routine=0x%08X knife=%u",SwordName(g_sword_request.kind),p.routine,input.knife?1U:0U);
            SwordDropRequest(sword::KeepName(keep));
        }
    }
    auto charge=input;charge.safe=charge.safe&&!input.fire&&!input.fire_edge&&g_sword_request.kind==SwordKind::None;
    const auto event=g_cmb.spin_attack?g_sword_charge.Step(charge):spin::Event::None;
    if(event!=spin::Event::None)CmbLog("spin_charge frame=%u event=%u time60=%.3f",static_cast<unsigned>(sample.frame),static_cast<unsigned>(event),input.time);
    if(event!=spin::Event::None)ChargeEvent(event,input.time,input.player);   // charge-glow-01: inert unless SwordCharge is on
    if(event==spin::Event::Released&&g_sword_spin_clip.ready&&g_cmb.line_waiver){
        g_sword_request={SwordKind::Spin,input.time,input.scene,input.player,input.weapon,static_cast<uint32_t>(input.frame),{}};
        if(g_cmb.GreatSpin()){   // TP: full hearts at the release make it the Great Spin
            auto& r=g_sword_request;r.great=GreatHealth(r.hp,r.hp_max)&&gspin::FullHealth(r.hp,r.hp_max);
            CmbLog("spin_request frame=%u charge_ms=1000 release=1 great=%u hp=%d/%d",static_cast<unsigned>(input.frame),r.great?1U:0U,r.hp,r.hp_max);
        }else CmbLog("spin_request frame=%u charge_ms=1000 release=1",static_cast<unsigned>(input.frame));
    }
    auto ending_input=input;
    ending_input.safe=input.valid&&!ending_priority&&!occupied&&SwordKnifeHold(p.routine)&&g_sword_request.kind==SwordKind::None;
    sword::target::List targets;sword::target::Bodies bodies;
    sword::target::Prefer prefer;bool latched=false;
    const bool ending_scan=g_cmb.ending_blow&&g_sword_ending_clip.ready&&ending_input.safe&&input.fire_edge;
    if(ending_scan){
        CmbMem m;latched=SwordLatch(prefer);
        targets=sword::target::Select(m,g_cmb_base,g_cmb_tracker,p.pos,CmbGet<float>(p.player+0xA4),SwordTargets(),true,
            latched?&prefer:nullptr,g_cmb.ending_leap?&bodies:nullptr);
    }
    // Open finding (a), 16-oct night: another Ganado on the path that would stop the leap beyond the strike reach
    // refuses the press here, before anything replaces the native knife slash (which then plays as usual).
    ending::leap::Guard corridor;sword::target::Target refused;mtg::Raw refused_raw;
    if(ending_scan&&targets.count){
        corridor=SwordCorridor(p,targets.items[0],bodies,refused_raw);
        if(corridor.refuse){refused=targets.items[0];targets.count=0;}
    }
    // Review 24-oct L1: a target on another floor (beyond the 400 in-place band) needs a clear L-shaped path, its column
    // to Link's head height and across (ending_blow_target.h LineRefuses, the Z-target's native segment test); a blocked
    // or failed test refuses the press here, so the native knife slash plays. Without the Z-target's sight pins (or with
    // its debug waiver) nothing is refused, as before.
    bool line_refused=false;mtg::Vec3 line_at{};
    if(ending_scan&&targets.count){
        namespace zt=link_runtime::ztarget;
        CmbMem m;float at[3]{};
        if(m.Read(targets.items[0].pointer+0x94,at,sizeof(at))&&std::isfinite(at[0])&&std::isfinite(at[1])&&std::isfinite(at[2])){
            line_at={at[0],at[1],at[2]};
            const bool ready=zt::runtime::los_ready&&!zt::runtime::waiver&&zt::runtime::los_segment;
            line_refused=sword::target::LineRefuses(SwordTargets(),p.pos,line_at,ready,
                [](const mtg::Vec3& a,const mtg::Vec3& b,uint32_t& attr){return zt::runtime::los_segment(a,b,attr);});
        }
        if(line_refused){refused=targets.items[0];targets.count=0;}
    }
    const bool ending_press=g_cmb.ending_blow&&g_sword_press.Step(ending_input,targets.count!=0);
    // A press refused for lack of a target: say why, once (reads only; the native knife attack plays on).
    if(!ending_press&&ending_scan&&g_sword_press.Pressed()){
        if(line_refused)CmbLog("ending_none frame=%u reason=line actor=0x%08X d=%.0f dy=%.0f",static_cast<uint32_t>(input.frame),
            refused.pointer,double(refused.distance),double(line_at.y-p.pos.y));
        else if(corridor.refuse)SwordEndingBodies(static_cast<uint32_t>(input.frame),p,refused,refused_raw,corridor,latched?&prefer:nullptr);
        else SwordEndingNone(static_cast<uint32_t>(input.frame),p,latched?&prefer:nullptr);
    }
    // A fire press with the knife up that the Ending Blow never looked at (24-oct, bug 5): say which gate closed it, when a
    // Ganado down or getting up was there to be finished (otherwise silent, like ending_none).
    if(g_cmb.ending_blow&&g_sword_ending_clip.ready&&!ending_scan&&input.fire_edge&&input.knife)
        SwordEndingGate(static_cast<uint32_t>(input.frame),p,input,ending_priority,occupied,sample);
    if(ending_press){
        g_sword_charge.Cancel();
        g_sword_request={SwordKind::Ending,input.time,input.scene,input.player,input.weapon,static_cast<uint32_t>(input.frame),targets.items[0]};
        if(g_cmb.ending_leap){g_sword_request.bodies=sword::target::Others(bodies,targets.items[0]);link_runtime::ztarget::runtime::KnifeLatchUsed();}
        CmbLog("ending_request frame=%u actor=0x%08X target=%llu distance=%.0f%s",static_cast<unsigned>(input.frame),targets.items[0].pointer,targets.items[0].identity,targets.items[0].distance,
            !g_cmb.ending_leap?"":targets.items[0].locked?" prefer=lock":" prefer=near");
        // No mask yet: a failed admission leaves the native knife attack intact.
    }
}
// Spin bullets' state (SpinBullets, below): the volley's tracers (TrailFrame) and the Ganados its shots took.
namespace spb=link_runtime::spin_bullets;
static spb::Volley g_bullets;
static ::sword_trail::Style g_bullet_style{};
static bool g_bullet_los_ok=false,g_bullet_sound_ok=false;   // set at SwordPrepare
static struct {uint64_t lease=0;uint32_t pointers[spb::kMaxShots]{};unsigned count=0;} g_bullet_hits;
static bool BulletHit(uint64_t lease,uint32_t pointer){
    if(!lease||g_bullet_hits.lease!=lease)return false;
    for(unsigned i=0;i<g_bullet_hits.count;++i)if(g_bullet_hits.pointers[i]==pointer)return true;
    return false;
}
static sword::target::Config BulletTargets(){auto c=SwordTargets();c.spin_radius=g_cmb.bullets.range;c.vertical=spb::kVertical;return c;}
// Three shot sounds per volley (Donald 22-oct: «que algo de balas suenen»), kShotGapMs apart, pumped from TrailFrame.
static struct {unsigned left=0;double next=0;::sword_trail::Vec3 at{};} g_bullet_shots;
static void BulletSound(const ::sword_trail::Vec3& at){
    if(!g_bullet_sound_ok)return;
    float pos[3]={float(at.x),float(at.y),float(at.z)};
    using SndCallFn=uint32_t(__cdecl*)(uint16_t,uint16_t,float*,int,int,void*);
    __try{reinterpret_cast<SndCallFn>(g_cmb_base+spb::kSndCall)(2,0,pos,0,0,nullptr);}__except(EXCEPTION_EXECUTE_HANDLER){g_bullet_sound_ok=false;}
}
static void SwordPumpHits(double now,const CmbPlayer& p){
    auto& action=g_sword_action;
    const unsigned published=action.hits.Published(),applied=action.hits.Applied(),skipped=action.hits.Skipped();
    CmbMem m;
    action.hits.Pump(now,g_cmb_adapter,[&](const sword::target::Target& target,mdmg::HitTarget& hit,mdmg::Kind& kind,int& amount){
        // Lethal on rise (16-oct night): a target down at the start that is getting up (r1 0x1F) keeps the lethal kind.
        const bool spin_kind=action.request.kind==SwordKind::Spin,back_kind=action.request.kind==SwordKind::Back;
        const bool shot=spin_kind&&BulletHit(action.lease,target.pointer);
        const auto check=sword::target::Revalidate(m,g_cmb_base,g_cmb_tracker,target,p.pos,back_kind?BackTargets():shot?BulletTargets():
            spin_kind?SwordSpinTargets(action.request.great):SwordTargets(),
            action.request.kind==SwordKind::Ending,sword::target::Mode::Strike,action.grounded);
        if(!check.same||!check.reachable)return false;
        hit.identity=target.identity;hit.actor=target.pointer;CmbFrom(p.pos,hit.from);
        hit.repair_len=true;   // shield-guard-02: knife 0x10 never reads the node len (wading Ganados: bad_len)
        kind=check.lethal?mdmg::Kind::Lethal:mdmg::Kind::Ordinary;
        amount=check.cap;   // 16-oct damage limit: heavy targets (max HP > 1500, chainsaw or claw type) lose at most 1000 HP
        // Great Spin: the native knife amount x GreatSpinPermille/1000 (head/core part: ordinary, HitQueue).
        if(spin_kind&&action.request.great&&kind==mdmg::Kind::Ordinary){kind=mdmg::Kind::Scaled;amount=int(g_cmb.great.permille);}
        // Back Slice: the native knife amount x BackSlicePermille/1000 from behind (head/core part: ordinary, HitQueue).
        if(back_kind&&kind==mdmg::Kind::Ordinary){kind=mdmg::Kind::Scaled;amount=int(g_cmb.back.permille);}
        action.kind=static_cast<unsigned>(kind);action.rising=check.rising;
        g_cmb_mortal_same=check.same;g_cmb_mortal_identity=target.identity;g_cmb_caller=CmbCaller::Sword;
        CmbLog("%s_target actor=0x%08X identity=%llu id=0x%02X type=0x%02X r0=%u r1=0x%02X grounded=%u kind=%u hp=%d hp_max=%d wep=%u "
            "heavy=%u cap=%d capped=%u rising=%u",SwordName(action.request.kind),target.pointer,target.identity,check.raw.id,check.raw.type,
            check.raw.r0,check.raw.r1,ending::Grounded(check.raw)?1U:0U,static_cast<unsigned>(kind),check.raw.hp,check.raw.hp_max,
            check.raw.wep_type,mtg::Heavy(check.raw)?1U:0U,check.cap,mdmg::Capped(check.raw.hp,check.cap)?1U:0U,check.rising?1U:0U);
        return true;
    },[&](const sword::target::Target& target,const sword::HitQueue::Skip& k){
        // spin-fix-01: why a target took no hit (deferred=1: retried on a later frame while the window is open).
        if(k.revalidate)CmbLog("%s_skip lease=%llu actor=0x%08X reason=revalidate tries=%u",SwordName(action.request.kind),action.lease,target.pointer,k.tries);
        else CmbLog("%s_skip lease=%llu actor=0x%08X status=%s verdict=%s deferred=%u tries=%u",SwordName(action.request.kind),action.lease,
            target.pointer,mdr::StatusName(k.status),nhp::Name(k.verdict),k.deferred?1U:0U,k.tries);
    });
    g_cmb_caller=CmbCaller::None;
    if(action.request.kind==SwordKind::Back&&(published!=action.hits.Published()||applied!=action.hits.Applied()||skipped!=action.hits.Skipped()))
        CmbLog("back_slice hit lease=%llu actor=0x%08X published=%u applied=%u skipped=%u permille=%u len_repaired=%u",action.lease,action.request.target.pointer,
            action.hits.Published(),action.hits.Applied(),action.hits.Skipped(),g_cmb.back.permille,g_cmb_adapter.LastFacts().len_repaired?1U:0U);
    if(published!=action.hits.Published()||applied!=action.hits.Applied()||skipped!=action.hits.Skipped())
        CmbLog("%s_hits lease=%llu targets=%u published=%u applied=%u skipped=%u deferred=%u done=%u",SwordName(action.request.kind),action.lease,
            action.hits.Count(),action.hits.Published(),action.hits.Applied(),action.hits.Skipped(),action.hits.Deferred(),action.hits.Done()?1U:0U);
    // 16-oct, every grounded Ganado is finishable: an applied Ending Blow that leaves its target alive is
    // expected on a heavy target above 1000 HP (damage limit, heavy=1) or after an ordinary hit (kind=0: the
    // Ganado was fully up again at the strike), and otherwise a known gap (chainsaw cap 1000 at 0x4B061 above
    // difficulty 5, armoured parts, emerged Plaga), logged for the next test. The enemy's damage block runs
    // before the player's dispatch, so the HP read here is already reduced.
    if(action.request.kind==SwordKind::Ending&&action.hits.Applied()>applied){
        const auto& t=action.request.target;mtg::Raw r;
        if(mtg::ReadRaw(m,t.pointer,r)==mtg::Reject::None&&r.id==t.id&&r.type==t.type&&r.hp>0)
            CmbLog("ending_survived lease=%llu actor=0x%08X id=0x%02X type=0x%02X hp=%d/%d r=%u/0x%02X wep=%u shield=%u plaga=%u heavy=%u "
                "kind=%u rising=%u",action.lease,t.pointer,r.id,r.type,r.hp,r.hp_max,r.r0,r.r1,r.wep_type,r.p_shield?1U:0U,
                (r.p_core||r.p_parasite)?1U:0U,mtg::Heavy(r)?1U:0U,action.kind,action.rising?1U:0U);
    }
}
// ---- part visibility during the clip (sword_parts.h; spin-fix-01: the arm that vanished in a spin) ----------------
namespace sparts=link_runtime::sword_parts;
static sparts::Guard g_sword_parts;
static void SwordPartsLog(const char* what,uint64_t lease,uint32_t player){
    sparts::Snapshot s;LocalMemory lm;char list[200]{};
    if(sparts::Read(lm,player,s))sparts::Format(list,sizeof(list),s);
    const uint32_t wep=CmbGet<uint32_t>(player+0x7D8);
    const uint32_t gun=link_runtime::IsPointer(wep)?CmbGet<uint32_t>(wep+0x34):0,hand=link_runtime::IsPointer(wep)?CmbGet<uint32_t>(wep+0x38):0;
    CmbLog("sword_parts %s lease=%llu parts=[%s] gun_disp=0x%02X hand_disp=0x%02X weapon=%u",what,lease,list,
        link_runtime::IsPointer(gun)?unsigned(CmbGet<uint8_t>(gun+0x354)):0xFFU,link_runtime::IsPointer(hand)?unsigned(CmbGet<uint8_t>(hand+0x354)):0xFFU,
        static_cast<unsigned>(SwordWeapon()));
}
static void SwordPartsBegin(uint64_t lease,uint32_t player){
    const auto g=CmbGet<uint32_t>(g_cmb_global_slot);
    const uint32_t character=link_runtime::IsPointer(g)?CmbGet<uint32_t>(g+0x4FC8):0xFFU;
    LocalMemory lm;
    if((character&0xFF)!=0||!g_sword_parts.Begin(lm,player,(character>>8)&0xFF)){g_sword_parts=sparts::Guard{};return;}
    SwordPartsLog("begin",lease,player);
}
static void SwordPartsStep(uint64_t lease,uint32_t player){
    if(!g_sword_parts.Active())return;
    LocalMemory lm;sparts::Report r[sparts::Guard::kReports];
    const unsigned n=g_sword_parts.Step(lm,player,r);
    for(unsigned i=0;i<n;++i)
        CmbLog("sword_parts %s lease=%llu index=%u node=0x%08X flags=0x%X",r[i].change==sparts::Change::Restored?"restore":
            r[i].change==sparts::Change::Gone?"gone":"race_lost",lease,r[i].index,r[i].node,r[i].flags);
}
static void SwordPartsEnd(uint64_t lease,uint32_t player){
    if(!g_sword_parts.Active())return;
    const unsigned restores=g_sword_parts.Restores();LocalMemory lm;
    const unsigned back=g_sword_parts.End(lm);
    SwordPartsLog(restores?"end_restored":"end",lease,player);
    if(restores)CmbLog("sword_parts handed_back lease=%llu restores=%u handed_back=%u",lease,restores,back);
}
// ---- sword in the hand (sword_hand.h; sword-hand-01, 19-oct) --------------------------------------------------------
// Donald's video: the Back Slice from locomotion kept the handgun in the right hand. A sword action started outside the
// knife routine shows the knife slot (the Master Sword) and hides the equipped weapon at level 0, as the native knife
// draw does; the clip's end (any event), or the first player frame on which the action is no longer valid, hands both
// back. SwordInHand=0, the native pins, another character or the ocarina's model in the slot leave the hand as it is.
namespace shand=link_runtime::sword_hand;
static shand::Swap g_sword_hand;
static bool g_sword_hand_ok=false;
static void SwordHandEnd(uint64_t lease,const char* why){
    if(!g_sword_hand.Held()||g_sword_hand.Lease()!=lease)return;
    const uint32_t player=CmbGet<uint32_t>(g_cmb_player_slot);
    const uint32_t routine=link_runtime::IsPointer(player)?CmbGet<uint32_t>(player+shand::layout::kRoutine):0;
    const unsigned reasserts=g_sword_hand.Reasserts();const bool retry=g_sword_hand.Pending();
    LocalMemory lm;const auto r=g_sword_hand.End(lm,g_cmb_player_slot,shand::KnifeRoutine(routine));
    if(r!=shand::Result::Pending||!retry)
        CmbLog("sword_hand end lease=%llu why=%s result=%s reasserts=%u routine=0x%08X",lease,why,shand::Name(r),reasserts,routine);
}
static void SwordHandBegin(uint64_t lease,SwordKind kind){
    if(!g_sword_hand_ok)return;
    const auto g=CmbGet<uint32_t>(g_cmb_global_slot);
    const uint32_t character=link_runtime::IsPointer(g)?CmbGet<uint32_t>(g+0x4FC8):0xFFU;
    if((character&0xFF)!=0)return;
    LocalMemory lm;
    const auto r=g_sword_hand.Begin(lm,g_cmb_player_slot,lease,g_oca_mounted_bin.load(std::memory_order_acquire));
    CmbLog("sword_hand begin lease=%llu kind=%s result=%s knife_info=0x%08X guns_hidden=%u weapon=%u",lease,SwordName(kind),
        shand::Name(r),g_sword_hand.KnifeInfo(),r==shand::Result::Shown?g_sword_hand.GunsHidden():0U,static_cast<unsigned>(SwordWeapon()));
}
// Every player MotionMove (CmbPlayerMove, every routine): a native damage, death or event takes the player before the
// dispatch notices (its Abandon comes on the next hooked routine, up to ~1.4 s later on 19-oct), so the hand is handed
// back the same frame; otherwise the gun's level 0 is held and a pending restore retried.
static void SwordHandTick(const CmbPlayer& p,bool player){
    if(!g_sword_hand.Held())return;
    const uint64_t lease=g_sword_hand.Lease();
    if(g_sword_hand.Pending()){SwordHandEnd(lease,"retry");return;}
    const auto& run=mortal::dispatch_hooks::runtime;
    const char* why=shand::EndReason(player,p.facts.gameplay,p.damaged,g_sword_action.lease==lease,run.active&&run.lease.token==lease);
    if(why){SwordHandEnd(lease,why);return;}
    LocalMemory lm;const auto r=g_sword_hand.Hold(lm,g_cmb_player_slot);
    if(r==shand::Result::Foreign||r==shand::Result::Lost)CmbLog("sword_hand %s lease=%llu (native knife state or identity changed)",shand::Name(r),lease);
}
// E1 sword trail log (sword_trail_log.h; SwordTrailLog=1 and LogLevel=2): the blade and the camera on every frame of a
// spin or Great Spin, for scripts/sword_trail_render.py. Reads only: the spin runs the same with or without it.
// Full identity (resource SHA-256) at the start, then only the matrices per frame; a failed read logs ok=0.
static bool g_sword_trail_log=false;   // fixed from the INI before the hooks (link_runtime.cpp)
static link_runtime::sword_trail_log::Tracker g_sword_trail;
static link_runtime::parry::sword::reader::Slots SwordTrailSlots(){return {g_cmb_player_slot,g_cmb_global_slot};}
static void SwordTrailLine(double now,const link_runtime::parry::sword::Blade& blade,bool ok){
    namespace stl=link_runtime::sword_trail_log;
    char line[sizeof(CmbLine::text)];int n=stl::FormatSample(line,sizeof(line),stl::Ms(now),blade,ok);
    if(n>0&&size_t(n)<sizeof(line))CmbLogV("%s",line);
    CmbMem m;stl::Camera camera;
    if(!stl::ReadCamera(m,g_cmb_global_slot,g_cmb_base,camera)){CmbLogV("sword_trail camera ok=0");return;}
    n=stl::FormatCamera(line,sizeof(line),camera);
    if(n>0&&size_t(n)<sizeof(line))CmbLogV("%s",line);
}
static void SwordTrailBegin(uint64_t lease,SwordKind kind,bool great){
    namespace stl=link_runtime::sword_trail_log;
    if(!g_sword_trail_log||kind!=SwordKind::Spin)return;
    CmbMem m;link_runtime::parry::sword::Blade blade{};
    const bool ok=g_sword_trail.Begin(m,SwordTrailSlots(),lease,blade);
    char line[sizeof(CmbLine::text)];
    if(stl::FormatBegin(line,sizeof(line),SwordName(kind),great,ok,lease,g_sword_trail.SwordProfile())>0)CmbLogV("%s",line);
    SwordTrailLine(GameNow(g_cmb_global_slot),blade,ok);
}
static void SwordTrailStep(uint64_t lease,double now){
    namespace stl=link_runtime::sword_trail_log;
    if(!g_sword_trail_log||!lease||g_sword_trail.Lease()!=lease)return;
    CmbMem m;link_runtime::parry::sword::Blade blade{};
    const auto step=g_sword_trail.Sample(m,SwordTrailSlots(),blade);
    if(step==stl::Step::Identified)
        CmbLogV("sword_trail identity ok=1 attempt=%u profile=%s",g_sword_trail.Attempts(),stl::ProfileName(g_sword_trail.SwordProfile()));
    SwordTrailLine(now,blade,step!=stl::Step::Failed);
}
static void SwordTrailEnd(uint64_t lease,const char* reason){
    namespace stl=link_runtime::sword_trail_log;
    if(!g_sword_trail_log||!lease||g_sword_trail.Lease()!=lease)return;
    char line[sizeof(CmbLine::text)];
    if(stl::FormatEnd(line,sizeof(line),reason,g_sword_trail.Samples(),g_sword_trail.Failed())>0)CmbLogV("%s",line);
    g_sword_trail.End();
}
// Task 4a sword trail (sword_trail_feed.h, sword_trail_draw_ot.h; SwordTrail, missing or 1 = on): TP's ribbon behind the
// blade in the Spin Attack / Great Spin, the Back Slice, the Ending Blow with its leap (our clips) and Link's native knife
// slashes (SwordTrailMoves; one style per move), drawn through the world table 17. Its own E1 tracker (identity once at
// the start, pose-only per frame); every move runs the same with or without it. g_trail_on is fixed at startup
// (link_runtime.cpp StartSwordTrail): config on, SpinAttack=1, the player dispatch and kRuntimePins. Off = no reads.
// Atomic: stored once by the startup path, read on the game thread and by the worker's stats poll.
static link_runtime::sword_trail::feed::Config g_trail_cfg;   // from the INI before the hooks (link_runtime.cpp)
static std::atomic<bool> g_trail_on{false};
static link_runtime::sword_trail::feed::Trail g_trail;
static link_runtime::sword_trail_log::Tracker g_trail_tracker;
static uint32_t TrailRoom(){
    const auto g=CmbGet<uint32_t>(g_cmb_global_slot);return link_runtime::IsPointer(g)?CmbGet<uint16_t>(g+0x4FAC):0xFFFFU;
}
static void ParticlesSample(uint64_t lease,bool ok,const link_runtime::parry::sword::Blade& b);   // TP sword particles (below)
static void ParticlesMoveEnd(uint64_t lease);
static void TrailSample(uint64_t lease,double now,bool ok,const link_runtime::parry::sword::Blade& b){
    g_trail.Sample(lease,link_runtime::sword_trail_log::Ms(now),ok,{b.base.x,b.base.y,b.base.z},{b.tip.x,b.tip.y,b.tip.z});
    ParticlesSample(lease,ok,b);   // sword-particles-01: the contact search reads the same samples
}
// Our clips' trail move. The Ending Blow's leap is part of its clip (the blade moves during it). Shield Bash: none.
static bool TrailMove(SwordKind kind,bool great,link_runtime::sword_trail::feed::Move& out){
    using link_runtime::sword_trail::feed::Move;
    if(kind==SwordKind::Spin){out=great?Move::GreatSpin:Move::Spin;return true;}
    if(kind==SwordKind::Back){out=Move::Back;return true;}
    if(kind==SwordKind::Ending){out=Move::Ending;return true;}
    return false;
}
static void TrailBegin(uint64_t lease,SwordKind kind,bool great,uint32_t player){
    link_runtime::sword_trail::feed::Move move{};
    if(!g_trail_on.load(std::memory_order_acquire)||!TrailMove(kind,great,move)||
       !g_trail.Begin(g_trail_cfg,lease,move,TrailRoom(),player))return;
    CmbMem m;link_runtime::parry::sword::Blade blade{};
    const bool ok=g_trail_tracker.Begin(m,SwordTrailSlots(),lease,blade);
    TrailSample(lease,GameNow(g_cmb_global_slot),ok,blade);   // the start frame's pose: the trail skips it
}
static void TrailStep(uint64_t lease,double now){
    if(!g_trail_on.load(std::memory_order_acquire)||!lease||g_trail.Lease()!=lease||g_trail_tracker.Lease()!=lease)return;
    CmbMem m;link_runtime::parry::sword::Blade blade{};
    const auto step=g_trail_tracker.Sample(m,SwordTrailSlots(),blade);
    TrailSample(lease,now,step!=link_runtime::sword_trail_log::Step::Failed,blade);
}
static void TrailEnd(uint64_t lease){
    if(!g_trail_on.load(std::memory_order_acquire)||!lease)return;
    // A move whose sword was never identified drew nothing: counted (identity_fails) so the stats line shows it.
    if(g_trail_tracker.Lease()==lease){g_trail.SpinIdentity(lease,g_trail_tracker.Identified());g_trail_tracker.End();}
    g_trail.End(lease);   // the samples fade out by time (TrailFrame)
    ParticlesMoveEnd(lease);   // sword-particles-01: the hit grace starts
}
// Native knife slashes (SwordTrailMoves `slash`; sword_trail_feed.h SlashWatch): Link's own knife attack, read from the
// routine on each player dispatch (before the native routine runs), never while one of our clips or another mod action
// owns Link (that move wins), nor for another character. Each slash has its own lease (top bit set: never a dispatch
// token) and the same E1 tracker: identity once at the slash start, pose per frame. The knife routine shows the knife
// info, which is Link's Master Sword; any other model fails the identity and draws nothing (identity_fails).
constexpr uint64_t kSlashLease=1ULL<<63;
static link_runtime::sword_trail::feed::SlashWatch g_trail_slash;
static uint64_t g_trail_slash_lease=0,g_trail_slash_serial=0;
static void TrailSlashEnd(){
    if(!g_trail_slash_lease)return;
    const uint64_t lease=g_trail_slash_lease;g_trail_slash_lease=0;
    const bool mine=g_trail_tracker.Lease()==lease,identified=mine&&g_trail_tracker.Identified();
    TrailEnd(lease);
    if(mine)CmbLogV("sword_trail slash_end lease=0x%llX identity=%u",lease,identified?1U:0U);
}
static void TrailSlash(const mortal::dispatch::Runtime& run,const CmbPlayer& p){
    using link_runtime::sword_trail::feed::Move;using Step=link_runtime::sword_trail::feed::SlashWatch::Step;
    if(!g_trail_on.load(std::memory_order_acquire))return;
    const auto g=CmbGet<uint32_t>(g_cmb_global_slot);
    const bool link=link_runtime::IsPointer(g)&&(CmbGet<uint32_t>(g+0x4FC8)&0xFF)==0;
    const bool eligible=g_trail_cfg.Wants(Move::Slash)&&link&&p.facts.gameplay&&!p.damaged&&!g_sword_action.lease&&
        g_sword_starting==SwordKind::None&&!run.active;
    const auto step=g_trail_slash.Next(p.routine,eligible);
    if(step==Step::End){TrailSlashEnd();return;}
    if(step==Step::Continue){if(g_trail_slash_lease)TrailStep(g_trail_slash_lease,GameNow(g_cmb_global_slot));return;}
    if(step!=Step::Begin)return;
    TrailSlashEnd();   // a held fire's next slash: the previous one ends here (its ribbon is replaced, never recoloured)
    const uint64_t lease=kSlashLease|++g_trail_slash_serial;
    if(!g_trail.Begin(g_trail_cfg,lease,Move::Slash,TrailRoom(),p.player))return;
    g_trail_slash_lease=lease;
    CmbMem m;link_runtime::parry::sword::Blade blade{};
    const bool ok=g_trail_tracker.Begin(m,SwordTrailSlots(),lease,blade);
    TrailSample(lease,GameNow(g_cmb_global_slot),ok,blade);   // the start frame's pose: the trail skips it
    CmbLogV("sword_trail slash_begin lease=0x%llX routine=0x%08X identity=%u",lease,p.routine,ok?1U:0U);
}
// Every player dispatch (CombinedDecide): the native slash watch, then, while samples are alive, this frame's strip into
// table 17, once per native generation. Not gameplay (menus, events, scenes) = no registration; a room or player change
// resets. With `slash` off nothing is read unless samples are alive. `slash` turned off live (the panel drops the move
// or the whole trail) during a slash: the watch never sees it again, so the slash ends here as its own end would
// (TrailSlashEnd: the ribbon fades, the particles' hit grace starts and runs out), never leaving its lease live.
static void TrailFrame(const mortal::dispatch::Runtime& run){
    namespace sot=link_runtime::sword_trail::ot;
    if(!g_trail_on.load(std::memory_order_acquire))return;
    const bool slash=g_trail_cfg.Wants(link_runtime::sword_trail::feed::Move::Slash);
    if(!slash&&(g_trail_slash_lease||g_trail_slash.Active())){g_trail_slash.Reset();TrailSlashEnd();}
    const double now_ms=link_runtime::sword_trail_log::Ms(GameNow(g_cmb_global_slot));
    if(g_bullet_shots.left&&now_ms>=g_bullet_shots.next){BulletSound(g_bullet_shots.at);--g_bullet_shots.left;g_bullet_shots.next+=spb::kShotGapMs;}
    const bool shots=g_bullets.Live(now_ms);   // spin bullets' tracers (SpinBullets): drawn with the trail's node
    if(!slash&&!g_trail.Live()&&!shots)return;
    CmbPlayer p;
    if(!CmbReadPlayer(p))return;
    if(slash)TrailSlash(run,p);
    const bool trail=g_trail.Live();
    if(!trail&&!shots)return;
    static ::sword_trail::Quad quads[sot::kMaxQuads];
    int n=trail?g_trail.Frame(now_ms,TrailRoom(),p.player,p.facts.gameplay,quads,sot::kMaxQuads):0;
    if(n<0)n=0;
    if(shots&&p.facts.gameplay)n=g_bullets.Frame(now_ms,TrailRoom(),p.player,quads,n,sot::kMaxQuads);
    if(n>0)sot::runtime::Submit(quads,n,trail?g_trail.CurrentStyle():g_bullet_style);
}
// Sword charge glow (sword_charge.h, sword_charge_draw.h; charge-glow-01, SwordCharge missing or 1 = on): while the
// spin charges, TP's light along the blade and the glint from the hilt to the tip (arriving at Event::Charged), the
// burst and TP's Z2SE_SWORD_POWER_COME at the full charge, a 120 ms fade on release or cancel. Its own world table-17
// node (additive) and its own E1 tracker (identity once per charge, pose per frame); the colour is the move it would
// release (SwordTrailColor, or SwordTrailGreatColor with full life and GreatSpin on). g_charge_on is fixed at startup
// (link_runtime.cpp StartSwordCharge): config on, SpinAttack=1, the player dispatch and kRuntimePins. Off = no reads.
// Gameplay is untouched: spin::Charge decides the charge, the release and the cancel as before.
namespace schg=link_runtime::sword_charge;
static schg::Config g_charge_cfg;   // from the INI before the hooks (link_runtime.cpp)
static std::atomic<bool> g_charge_on{false};
static schg::Glow g_charge;
static link_runtime::sword_trail_log::Tracker g_charge_tracker;
static link_runtime::ztarget::voice::Voice g_charge_sound;   // Bin32/link_sword_charge.wav, SHA-pinned (SwordChargeSound)
static link_runtime::parry::sword::Blade g_charge_blade{};
static bool g_charge_have_blade=false;
static uint64_t g_charge_serial=0,g_charge_lease=0;   // this charge's tracker lease (kChargeLease | serial)
constexpr uint64_t kChargeLease=1ULL<<62;   // the tracker's lease: never a dispatch token, never a slash's
static void ChargeLine(const char* reason){
    char line[sizeof(CmbLine::text)];if(schg::FormatEnd(line,sizeof(line),reason)>0)CmbLog("%s",line);
}
static void ChargeStop(){g_charge_tracker.End();g_charge_have_blade=false;}
// SwordCharge=0 live (g_charge_cfg.on from the panel, user-ini-01; g_charge_on keeps what started): a glow that shows or
// fades goes at once, and with it the full-charge sound (Glow::Full needs Charging). bughunt-01: before, only
// Glow::Begin asked, so a charge already showing kept drawing and sounding until it ended (as ParticlesOff for R02).
static void ChargeOff(){
    if(g_charge.State()==schg::Phase::Idle)return;
    g_charge.Reset();ChargeStop();ChargeLine("off");
}
// spin::Charge's events, from the arbiter (game thread).
static void ChargeEvent(spin::Event event,double time60,uint32_t player){
    if(!g_charge_on.load(std::memory_order_acquire))return;
    if(!g_charge_cfg.on){ChargeOff();return;}
    const double now=link_runtime::sword_trail_log::Ms(time60);
    if(event==spin::Event::Charging){
        int hp=0,hp_max=0;
        const bool great=g_cmb.GreatSpin()&&GreatHealth(hp,hp_max)&&gspin::FullHealth(hp,hp_max);
        if(!g_charge.Begin(g_charge_cfg,now,great?g_trail_cfg.great_color:g_trail_cfg.color,great,TrailRoom(),player))return;
        ChargeStop();g_charge_blade={};g_charge_lease=kChargeLease|++g_charge_serial;   // identity once the sword is in hand
    }else if(event==spin::Event::Charged){
        if(!g_charge.Full(now))return;
        CmbLog("sword_charge full identity=%d",g_charge_tracker.Lease()==g_charge_lease&&g_charge_tracker.Identified()?1:0);
        if(g_charge_cfg.sound&&g_charge_sound.Loaded())g_gssb_sound.fetch_or(4,std::memory_order_acq_rel);
    }else if(event==spin::Event::Released||event==spin::Event::Cancelled){
        if(g_charge.End(now))ChargeLine(event==spin::Event::Released?"released":"cancelled");
    }
}
// Every player dispatch (CombinedDecide), after the trail: while a charge shows or fades, this frame's glow into table
// 17, once per native generation. The sword shows from the knife hold on (r_no_0 0, knife 0x0B, r_no_2 1: drawn from the
// sheath); not gameplay = nothing registered; a room or player change, or game time running back, resets.
static void ChargeFrame(){
    namespace scd=link_runtime::sword_charge::draw;
    if(!g_charge_on.load(std::memory_order_acquire)||g_charge.State()==schg::Phase::Idle)return;
    if(!g_charge_cfg.on){ChargeOff();return;}
    const double now=link_runtime::sword_trail_log::Ms(GameNow(g_cmb_global_slot));
    // spin::Charge cancelled without an event (the Ending Blow's press, a sword action's end): the glow fades too.
    if(g_charge.Charging()&&!g_sword_charge.Active()&&g_charge.End(now))ChargeLine("cancelled");
    CmbPlayer p;
    if(!CmbReadPlayer(p))return;
    const bool was=g_charge.Charging();
    schg::Look look;
    const auto step=g_charge.Frame(now,TrailRoom(),p.player,p.facts.gameplay,(p.routine&0xFFFFFF)==0x010B00,look);
    if(step==schg::Step::Room||step==schg::Step::Player||step==schg::Step::Backwards){if(was)ChargeLine(schg::StepName(step));ChargeStop();return;}
    if(step==schg::Step::Done){ChargeStop();return;}
    CmbMem m;link_runtime::parry::sword::Blade blade{};bool begun=false;
    // Identity on the first frame the sword is in hand (at the press it is still sheathed or swapping in): the `begin`
    // line reports it. A charge cancelled before that logs only its `end`.
    if(g_charge.Charging()&&g_charge.InHand()&&g_charge_tracker.Lease()!=g_charge_lease){
        g_charge_have_blade=g_charge_tracker.Begin(m,SwordTrailSlots(),g_charge_lease,g_charge_blade);begun=true;
        char line[sizeof(CmbLine::text)];
        if(schg::FormatBegin(line,sizeof(line),g_charge.Great(),g_charge_have_blade)>0)CmbLog("%s",line);
    }
    if(step!=schg::Step::Draw)return;
    if(!begun&&g_charge_tracker.Lease()==g_charge_lease&&
       g_charge_tracker.Sample(m,SwordTrailSlots(),blade)!=link_runtime::sword_trail_log::Step::Failed){
        g_charge_blade=blade;g_charge_have_blade=true;
    }
    if(!g_charge_have_blade)return;   // never identified or never read: nothing to draw on (a failed read keeps the last pose)
    const auto& b=g_charge_blade;
    scd::runtime::Submit(look,{b.base.x,b.base.y,b.base.z},{b.tip.x,b.tip.y,b.tip.z});
}
// ---- TP sword particles (sword_particles.h, sword_particles_draw.h; sword-particles-01, 21-oct) ----------------------
// TP's own particles on the real JPA values (docs/particulas-tp-diseno-2026-10-21.md): hit marks where the blade hits a
// Ganado (HITOK on a native knife slash, HITCR on our clips) with the flash, COLHIT sparks (KIKUZU chips for the
// SwordParticlesWoodTypes effect types) where it hits a wall, and at the spin's strike the KAITENGIRIL_D sparkles in the
// trail colour, DOWNSMOKE_A dust and KUSA01_KEN leaves. The contact search reads the trail's blade samples (TrailSample:
// so SwordTrail must be on and the move in SwordTrailMoves), the Ganado pool and the EatMgr nearest-hit test
// (ztarget_los.h Native::Nearest, sword-particles-02; the game thread's own scratch saved and restored); all on the game
// thread, drawn through two world table-17 nodes. g_part_on is fixed at startup (link_runtime.cpp StartSwordParticles):
// what started. g_part_cfg.on is the live switch (SwordParticles from the panel, user-ini-01): off = nothing sampled,
// emitted, searched or registered, and what is alive is cleared at once (ParticlesOff). Not started = no reads. Gameplay
// is untouched: nothing here writes the game.
namespace spt=link_runtime::sword_particles;
static spt::Config g_part_cfg;   // from the INI before the hooks (link_runtime.cpp)
static std::atomic<bool> g_part_on{false};
static bool g_part_wall_ok=false;   // the EatMgr nearest-hit pins (ztarget_los.h kNearestPins) passed at startup
static spt::System g_part;
static spt::HitWatch g_part_watch;
static bool g_part_scanned=false;   // the pool was read on the previous frame (else every HP baseline is stale)
static std::atomic<unsigned> g_part_hits{0},g_part_walls{0},g_part_spins{0},g_part_wall_faults{0},g_part_alive{0};
// The current move's blade, by the trail's lease (a clip's dispatch token, or kSlashLease | serial for a native slash).
// The start frame's pose is skipped (read before the sword reached the hand), as the trail skips it.
struct PartBlade {uint64_t lease=0;bool skip=false,have=false,have_prev=false,fresh=false,ended=false,ground_logged=false;
    spt::V3 base,tip,prev_tip;double end_ms=0,wall_ms=-1e9;};
static PartBlade g_part_blade;
static void ParticlesSample(uint64_t lease,bool ok,const link_runtime::parry::sword::Blade& b){
    if(!g_part_on.load(std::memory_order_acquire)||!g_part_cfg.on||!lease)return;
    auto& s=g_part_blade;
    if(lease!=s.lease){s=PartBlade{};s.lease=lease;s.skip=true;g_part_watch.Move(lease);}
    if(s.skip){s.skip=false;return;}
    const spt::V3 base{b.base.x,b.base.y,b.base.z},tip{b.tip.x,b.tip.y,b.tip.z};
    if(!ok||!spt::Finite(base)||!spt::Finite(tip)){s.have=s.have_prev=false;return;}
    s.prev_tip=s.tip;s.have_prev=s.have;s.base=base;s.tip=tip;s.have=s.fresh=true;
}
static void ParticlesMoveEnd(uint64_t lease){
    auto& s=g_part_blade;
    if(!g_part_on.load(std::memory_order_acquire)||!g_part_cfg.on||!lease||s.lease!=lease||s.ended)return;
    s.ended=true;s.fresh=false;s.end_ms=link_runtime::sword_trail_log::Ms(GameNow(g_cmb_global_slot));
}
// The spin's strike (SwordDecide): the sparkles on the ring around Link (900 above his feet, his facing, TP's 13 degree
// tilt), lightened toward the move's trail colour; the dust at his feet; leaves at 6 points of the tip's ring (the last
// blade sample of this spin, else 1.1 m) at his feet's height.
static void ParticlesSpin(const CmbPlayer& p,bool great){
    if(!g_part_on.load(std::memory_order_acquire)||!g_part_cfg.on||!g_part_cfg.spin)return;
    const double now=link_runtime::sword_trail_log::Ms(GameNow(g_cmb_global_slot));
    g_part.Advance(now,TrailRoom(),p.player);   // the effects start on this frame's tick
    const spt::V3 feet{p.pos.x,p.pos.y,p.pos.z};
    const double yaw=CmbGet<float>(p.player+0xA4);
    const unsigned before=g_part.Emitted();
    spt::Placement ring;ring.origin=spt::Add(feet,{0,spt::kSpinHeightMm,0});ring.frame=spt::YawFrame(std::isfinite(yaw)?yaw:0.0);
    ring.paint={spt::Tint::Toward,spt::Lighten(great?g_trail_cfg.great_color:g_trail_cfg.color,0.5),0};
    g_part.Emit(spt::Effect::SpinSparkle,ring);   // its own start frame: 10 per tick from the second tick on
    spt::Placement dust;dust.origin=feet;dust.paint={spt::Tint::Multiply,g_part_cfg.dust,0};
    g_part.Emit(spt::Effect::Dust,dust);
    double radius=spt::kLeafRadiusMm;
    const auto& s=g_part_blade;
    if(s.have&&s.lease==g_sword_action.lease){
        const double dx=s.tip.x-feet.x,dz=s.tip.z-feet.z,r=std::sqrt(dx*dx+dz*dz);
        if(std::isfinite(r)&&r>=300.0&&r<=3000.0)radius=r;
    }
    spt::V3 at[spt::kLeafPoints];
    const unsigned n=spt::RingPoints(feet,radius,spt::kLeafPoints,std::isfinite(yaw)?yaw:0.0,at);
    for(unsigned i=0;i<n;++i){
        spt::Placement leaf;leaf.origin=at[i];leaf.paint={spt::Tint::Fixed,spt::kLeafCentre,spt::kLeafEdge};leaf.size=spt::kLeafSize;
        g_part.Emit(spt::Effect::Leaves,leaf);
    }
    g_part_spins.fetch_add(1);
    char line[sizeof(CmbLine::text)];
    if(spt::FormatSpin(line,sizeof(line),great,g_part.Emitted()-before)>0)CmbLog("%s",line);
}
// The farthest a tracked move's hit is published from Link (horizontal; Revalidate in Strike mode checks it at the pump):
// the spin's radius, the Great Spin's GreatSpinRadius, the Back Slice's strike reach and the Ending Blow's strike range.
static double ParticlesReach(){
    double r=SwordTargets().spin_radius;
    // The bullets' 12 m only while a spin is live: other moves keep their own reach, so damage from elsewhere (fire,
    // a barrel) up to 12.5 m away never draws a sword burst during a slash (review 22-oct).
    const bool spin=g_sword_action.lease&&g_sword_action.request.kind==SwordKind::Spin;
    for(const double x:{double(g_cmb.great.radius),double(g_cmb.back.reach),double(g_cmb.ending_strike_range),
                        g_cmb.bullets.on&&spin?double(g_cmb.bullets.range):0.0})if(x>r)r=x;
    return r;
}
// Ganados within spt::HitRange(ParticlesReach()) whose HP dropped since the last frame while the move (or its grace) is
// live: one burst per Ganado per move at the blade point nearest its body axis. Reads only.
static void ParticlesHits(const CmbPlayer& p){
    auto& s=g_part_blade;
    CmbMem m;mtg::Manager pool;
    if(mtg::ReadManager(m,g_cmb_base,mtg::Config{},pool)!=mtg::Reject::None||!sword::target::Pool(pool)){g_part_scanned=false;return;}
    if(!g_part_scanned)g_part_watch.Reset();   // a gap since the last scan: every slot sets its baseline again
    g_part_scanned=true;
    const bool critical=(s.lease&kSlashLease)==0;   // our clips: TP's critical hit; a native knife slash: the normal one
    const double range=spt::HitRange(ParticlesReach());
    for(unsigned i=0;i<pool.count;++i){
        spt::Body b;
        if(!spt::ReadBody(m,pool.array+i*pool.block,g_cmb_base,b)){g_part_watch.Forget(i);continue;}
        if(!g_part_watch.Observe(i,b))continue;
        const double dx=b.pos.x-p.pos.x,dz=b.pos.z-p.pos.z;
        if(!(std::sqrt(dx*dx+dz*dz)<=range)||!g_part_watch.Burst(b.pointer))continue;
        spt::Placement at;at.origin=s.have?spt::HitPoint(s.base,s.tip,b.pos):spt::Add(b.pos,{0,1200,0});
        const spt::Effect set[4]={critical?spt::Effect::HitCrA:spt::Effect::HitOkA,critical?spt::Effect::HitCrB:spt::Effect::HitOkB,
            critical?spt::Effect::HitCrC:spt::Effect::HitOkC,critical?spt::Effect::HitCrD:spt::Effect::HitOkD};
        for(unsigned k=0;k<4;++k){spt::Placement x=at;x.size=k==1?1.0:k==3?spt::kDebrisSize:spt::kHitMarkSize;g_part.Emit(set[k],x);}
        g_part.Emit(spt::kFlash,at);
        g_part_hits.fetch_add(1);
        char line[sizeof(CmbLine::text)];
        if(spt::FormatHit(line,sizeof(line),critical,b.pointer,at.origin)>0)CmbLog("%s",line);
    }
}
// One native nearest-hit test (sword-particles-02): the contact point, the polygon's world normal and its full
// attribute word (0 = clear).
static spt::Seg ParticlesNearest(const spt::V3& a,const spt::V3& b,spt::NearHit& hit){
    link_runtime::ztarget::los::Native n{g_cmb_base};
    const link_runtime::ztarget::Vec from{float(a.x),float(a.y),float(a.z)},to{float(b.x),float(b.y),float(b.z)};
    link_runtime::ztarget::Vec cross{},normal{};uint32_t attr=0;
    if(!n.Nearest(from,to,cross,normal,attr))return spt::Seg::Fault;
    hit={{cross.x,cross.y,cross.z},{normal.x,normal.y,normal.z},attr};
    return attr?spt::Seg::Hit:spt::Seg::Clear;
}
// This frame's blade against the walls (base -> tip, then the previous tip -> tip: one nearest-hit call each), once per
// kWallCooldownMs per move: no native call inside the cooldown. A ground polygon (normal.y >= 0.5, TP's
// cBgW_CheckBGround) gives nothing, as TP; its first contact per move is logged. The material: the attribute word's
// effect type (EatGetEffectType), KIKUZU for the SwordParticlesWoodTypes ones, else HIBANA and the flash. The emitter
// is turned by the contact's normal as TP turns COLHIT_* (spt::WallFrame).
static void ParticlesWall(double now){
    auto& s=g_part_blade;
    if(now-s.wall_ms<spt::kWallCooldownMs)return;
    const auto c=spt::FindContact(&ParticlesNearest,s.base,s.tip,s.have_prev?&s.prev_tip:nullptr);
    if(c.fault){g_part_wall_faults.fetch_add(1);s.wall_ms=now;return;}   // a faulting test waits out the cooldown too
    if(!c.hit)return;
    char line[sizeof(CmbLine::text)];
    if(spt::Ground(c.normal)){
        if(!s.ground_logged&&spt::FormatGround(line,sizeof(line),c.attr,c.point,c.normal)>0)CmbLog("%s",line);
        s.ground_logged=true;return;
    }
    // Our clips (spins, Back Slice, Ending Blow): one burst per move. A spin beside a shed hit its boards every 120 ms
    // (live 21-oct); TP's own spin never rebounds off a wall. Native slashes keep the cooldown.
    s.wall_ms=1e300;   // native slashes too since 21-oct (Donald: too many flying bits)
    const bool wood=g_part_cfg.Wood(spt::EffectType(c.attr));
    spt::Placement at;at.origin=c.point;at.frame=spt::WallFrame(c.normal);
    g_part.Emit(wood?spt::Effect::Kikuzu:spt::Effect::Hibana,at);
    if(!wood)g_part.Emit(spt::kFlash,at);   // TP flashes the light on stone and metal only
    g_part_walls.fetch_add(1);
    if(spt::FormatWall(line,sizeof(line),c.attr,wood,c.point,c.normal,c.calls)>0)CmbLog("%s",line);
}
// Every player dispatch (CombinedDecide), after the trail and the charge glow: the contacts of the live move (or its
// kHitGraceMs after the end), the 30 Hz simulation up to game time now, then this frame's particles into table 17 once
// per native generation. Not gameplay: no contact and no registration (the particles are kept); a room or player
// change, or game time running back, clears them. Nothing alive and no move: nothing read.
// SwordParticles=0 live (g_part_cfg.on false; g_part_on keeps what started): the particles, the move's blade and the hit
// watch are cleared once (no fade, no new emission), and nothing is read or registered until it is on again.
static void ParticlesOff(){
    if(g_part.Busy()||g_part_blade.lease){g_part.Reset();g_part_blade=PartBlade{};g_part_watch.Reset();}
    g_part_scanned=false;g_part_alive.store(0);
}
static void ParticlesFrame(){
    namespace spd=link_runtime::sword_particles::draw;
    if(!g_part_on.load(std::memory_order_acquire))return;
    if(!g_part_cfg.on){ParticlesOff();return;}
    g_part.Amount(g_part_cfg.amount);
    auto& s=g_part_blade;
    const double now=link_runtime::sword_trail_log::Ms(GameNow(g_cmb_global_slot));
    const bool live=s.lease&&!s.ended,grace=s.lease&&s.ended&&now>=s.end_ms&&now-s.end_ms<=spt::kHitGraceMs;
    if(!g_part.Busy()&&!live&&!grace){g_part_scanned=false;s.fresh=false;g_part_alive.store(0);return;}
    CmbPlayer p;
    if(!CmbReadPlayer(p)){g_part_scanned=false;s.fresh=false;return;}
    const auto step=g_part.Advance(now,TrailRoom(),p.player);
    if(step!=spt::Step::Ok&&step!=spt::Step::Idle){g_part_watch.Reset();g_part_scanned=false;}
    if(p.facts.gameplay&&(live||grace)){
        if(g_part_cfg.hit)ParticlesHits(p);else g_part_scanned=false;
        if(live&&s.fresh&&s.have&&g_part_cfg.wall&&g_part_wall_ok)ParticlesWall(now);
    }else g_part_scanned=false;
    s.fresh=false;
    g_part_alive.store(g_part.Alive());
    if(p.facts.gameplay&&g_part.Alive())spd::runtime::Submit(g_part,now,g_part_cfg.gain);
}
static bool SwordLifecycle(mortal::dispatch::Event event,uint64_t token){
    if(event==mortal::dispatch::Event::Started&&g_sword_starting!=SwordKind::None){
        g_sword_action.lease=token;
        auto& voice=SwordSpinClip(g_sword_starting)?g_sword_spin_voice:g_sword_ending_voice;
        // Great Spin: TP's great spin slash and shout instead of the spin voice (the voice when the WAV is missing).
        if(g_sword_starting==SwordKind::Spin&&g_sword_action.request.great&&g_great_spin_sound.Loaded())
            g_gssb_sound.fetch_or(1,std::memory_order_acq_rel);
        // TP's Back Slice: its own roll voice, then the twirl's shout and sword swing at the twirl (one WAV).
        else if(g_sword_starting==SwordKind::Back&&g_back_motion)g_sword_back_voice.Start(token);
        else if(g_sword_starting==SwordKind::Flip)g_sword_flip_voice.Start(token);   // TP's BACKTEN shout
        else if(g_sword_starting!=SwordKind::Bash)voice.Start(token);   // the bash's sound plays at its impact (BashFire)
        SwordPartsBegin(token,g_sword_action.request.player);
        SwordHandBegin(token,g_sword_starting);
        SwordTrailBegin(token,g_sword_starting,g_sword_action.request.great);
        TrailBegin(token,g_sword_starting,g_sword_action.request.great,g_sword_action.request.player);
        link_runtime::parry::input::SetMortalBlocks(true);return true;
    }
    if(!token||token!=g_sword_action.lease)return g_sword_starting!=SwordKind::None;
    if(event==mortal::dispatch::Event::Started)return true;
    auto& a=g_sword_action;
    // The lease ended (finished, or the game took Link: a hit, a grab). On a descent still above the target's level the
    // Settle goes on (review 24-oct M1: Link was left hovering out of the floor snap's reach), now from the player's own
    // MotionMove (link_runtime.cpp CmbLeapSettleOwns), for this player in this room; its line is logged when it ends.
    if(g_sword_leap.lease()==token){
        const bool settling=g_sword_leap.phase()==ending::leap::Phase::Settle;
        if(g_sword_leap.Abort(ending::leap::Stop::Interrupted))SwordLeapLog();
        if(g_sword_leap.Detached()){
            g_sword_leap.Bind(a.request.player,TrailRoom());
            CmbLog("ending_leap settle_after lease=%llu player=0x%08X room=0x%X",token,a.request.player,TrailRoom());
        }else if(settling)SwordLeapSettleLog();
    }
    if(g_back_circle.lease()==token&&g_back_circle.Abort(bks::Stop::Interrupted)){
        char line[sizeof(CmbLine::text)];if(bks::FormatEnd(line,sizeof(line),g_back_circle.summary())>0)CmbLog("%s",line);}
    if(g_flip_move.lease()==token&&g_flip_move.Abort(bfl::Stop::Interrupted)){
        char line[sizeof(CmbLine::text)];if(bfl::FormatEnd(line,sizeof(line),g_flip_move.summary())>0)CmbLog("%s",line);}
    link_runtime::ztarget::runtime::LiftLeaseEnd(token,event==mortal::dispatch::Event::Finished);   // its ending_cam_lift line
    link_runtime::ztarget::runtime::OrbitLeaseEnd(token,event==mortal::dispatch::Event::Finished);  // the Back Slice camera eases back
    a.hits.Cancel(g_cmb_adapter);g_sword_spin_voice.Stop(token);g_sword_ending_voice.Stop(token);g_sword_back_voice.Stop(token);
    g_sword_flip_voice.Stop(token);
    const char* why=event==mortal::dispatch::Event::Finished?"finished":event==mortal::dispatch::Event::Cancelled?"cancelled":"abandoned";
    SwordHandEnd(token,why);
    SwordTrailEnd(token,why);
    TrailEnd(token);
    SwordPartsEnd(token,a.request.player);
    CmbLog("%s_end lease=%llu event=%u struck=%u published=%u applied=%u skipped=%u",SwordName(a.request.kind),token,
        static_cast<unsigned>(event),a.struck?1U:0U,a.hits.Published(),a.hits.Applied(),a.hits.Skipped());
    a={};g_sword_charge.Cancel();link_runtime::parry::input::SetMortalBlocks(false);return true;
}
// Leap takeoff on the start frame, after the face yaw (ending_blow_leap.h Plan): logs the plan; with
// EndingBlowLeap=1 the leap begins and CmbMoveDetour carries Link from this frame's MotionMove on.
// SpinBullets (Donald 22-oct): at a spin's strike, 8 shots (12 for the Great Spin) evenly around Link from his facing,
// free. Each takes the nearest Ganado in its path (spin_bullets.h Assign) within SpinBulletsRange whose chest the EatMgr
// nearest hit sees from Link's chest; those join the spin's hit list (the spin's damage, revalidated with the bullets'
// reach: BulletTargets). Every shot draws a tracer to where it stopped (the Ganado, a wall or its range) through the
// trail's table-17 node (TrailFrame), in the move's trail colour, and the equipped weapon's shot plays once
// (SndCall(2, 0), pinned). Without the nearest-hit pins nothing is shot (no shots through walls).
// The wall between two points: Wall and `stop` = the contact when a polygon is in the way; Fault when the native test
// failed (no shot: neither a tracer nor a hit through a wall it could not see).
enum class BulletSeg:uint8_t {Clear,Wall,Fault};
static BulletSeg BulletWall(const ::sword_trail::Vec3& a,const ::sword_trail::Vec3& b,::sword_trail::Vec3& stop){
    link_runtime::ztarget::los::Native n{g_cmb_base};
    const link_runtime::ztarget::Vec from{float(a.x),float(a.y),float(a.z)},to{float(b.x),float(b.y),float(b.z)};
    link_runtime::ztarget::Vec cross{},normal{};uint32_t attr=0;
    if(!n.Nearest(from,to,cross,normal,attr)){stop=a;return BulletSeg::Fault;}
    if(!attr)return BulletSeg::Clear;
    stop={cross.x,cross.y,cross.z};return BulletSeg::Wall;
}
// Returns how many Ganados the shots added to the spin's hit list (the caller widens the hit window for them).
static unsigned SpinBullets(uint64_t lease,double now,const CmbPlayer& p,bool great,sword::target::List& targets){
    const auto& cfg=g_cmb.bullets;
    // Tracers and the 2nd/3rd shot sound run from TrailFrame: without the trail (SwordTrail=0 or its pins) no shots,
    // never invisible hits (review 22-oct).
    if(!cfg.on||!g_bullet_los_ok||!g_trail_on.load(std::memory_order_acquire))return 0;
    const unsigned n=cfg.Shots(great);const float yaw=CmbGet<float>(p.player+0xA4);
    if(!n||n>spb::kMaxShots||!std::isfinite(yaw))return 0;
    CmbMem m;
    const auto found=sword::target::Select(m,g_cmb_base,g_cmb_tracker,p.pos,yaw,BulletTargets(),false);
    spb::Candidate cand[sword::target::kMaxTargets];unsigned count=0;
    for(unsigned i=0;i<found.count;++i){
        float at[3]{};
        if(!m.Read(found.items[i].pointer+0x94,at,12)||!std::isfinite(at[0])||!std::isfinite(at[1])||!std::isfinite(at[2]))continue;
        cand[count++]={at[0],at[1],at[2],i};
    }
    const float feet[3]={p.pos.x,p.pos.y,p.pos.z};int hit[spb::kMaxShots];float along[spb::kMaxShots];
    spb::Assign(feet,yaw,n,cfg.range,cand,count,hit,along);
    spb::Ray rays[spb::kMaxShots];unsigned struck=0,walls=0,faults=0,added=0;double wall_mm=0;
    g_bullet_hits.lease=lease;g_bullet_hits.count=0;
    const ::sword_trail::Vec3 muzzle{p.pos.x,p.pos.y+spb::kMuzzleY,p.pos.z};
    for(unsigned i=0;i<n;++i){
        float dx,dz;spb::Direction(yaw,i,n,dx,dz);
        ::sword_trail::Vec3 end{muzzle.x+dx*cfg.range,muzzle.y,muzzle.z+dz*cfg.range};
        const spb::Candidate* c=hit[i]>=0?&cand[hit[i]]:nullptr;
        if(c){   // only a Ganado the hit list holds (or can take) stops the shot: a full list lets it fly on
            const uint32_t ptr=found.items[c->tag].pointer;bool held=false;
            for(unsigned k=0;k<targets.count;++k)held|=targets.items[k].pointer==ptr;
            if(!held&&targets.count>=sword::target::kMaxTargets)c=nullptr;
        }
        if(c)end={c->x,c->y+spb::kChestY,c->z};
        ::sword_trail::Vec3 stop{};
        const auto seg=BulletWall(muzzle,end,stop);
        if(seg!=BulletSeg::Clear){
            end=stop;c=nullptr;
            if(seg==BulletSeg::Fault)++faults;
            else{++walls;const double wx=stop.x-muzzle.x,wz=stop.z-muzzle.z;wall_mm+=std::sqrt(wx*wx+wz*wz);}
        }
        rays[i]={muzzle,end};
        if(!c)continue;
        ++struck;
        const auto& t=found.items[c->tag];
        if(g_bullet_hits.count<spb::kMaxShots)g_bullet_hits.pointers[g_bullet_hits.count++]=t.pointer;
        bool present=false;for(unsigned k=0;k<targets.count;++k)present|=targets.items[k].pointer==t.pointer;
        if(!present&&targets.count<sword::target::kMaxTargets){targets.items[targets.count++]=t;++added;}
    }
    g_bullet_style=g_trail_cfg.Style(great?link_runtime::sword_trail::feed::Move::GreatSpin:link_runtime::sword_trail::feed::Move::Spin);
    g_bullets.Fire(link_runtime::sword_trail_log::Ms(now),rays,n,TrailRoom(),p.player);
    BulletSound(muzzle);
    g_bullet_shots={spb::kShotSounds-1,link_runtime::sword_trail_log::Ms(now)+spb::kShotGapMs,muzzle};
    CmbLog("spin_bullets lease=%llu great=%u shots=%u candidates=%u struck=%u added=%u walls=%u wall_mm=%.0f faults=%u sound=%u",lease,
        great?1U:0U,n,count,struck,added,walls,walls?wall_mm/walls:0.0,faults,g_bullet_sound_ok?1U:0U);
    return added;
}
// SpinBreakables (Donald 22-oct): at a spin's strike, boxes, wooden barrels, vases, cabinets and boards within the
// spin's radius (GreatSpinRadius for the Great Spin) and sbk::kVertical (1.2 m: vases on tables) take the knife's damage record, which their own
// damage check turns into a native break (sword_breakables.h). The red explosive barrel is never touched.
static void SwordBreakables(uint64_t lease,const CmbPlayer& p,bool great){
    if(!g_cmb.spin_breakables)return;
    namespace sbk=link_runtime::sword_breakables;
    CmbMem m;mtg::Manager pool;
    if(mtg::ReadManager(m,g_cmb_base,mtg::Config{},pool)!=mtg::Reject::None)return;
    const auto c=SwordSpinTargets(great);const float link[3]={p.pos.x,p.pos.y,p.pos.z};
    const auto r=sbk::Fire(m,g_cmb_base,pool.array,pool.count,pool.block,link,c.spin_radius,sbk::kVertical);
    if(r.seen)CmbLog("spin_break lease=%llu great=%u radius=%.0f seen=%u struck=%u boxes=%u boards=%u failed=%u",lease,great?1U:0U,
        double(c.spin_radius),r.seen,r.struck,r.boxes,r.bars,r.failed);
}
static double SwordLeapTakeoff(uint64_t lease,double now,const SwordRequest& r,const CmbPlayer& p,const sword::target::Recheck& v,
                               bool faced,unsigned strike_tick){
    namespace lp=ending::leap;
    lp::Body others[sword::target::kMaxBodies];
    for(unsigned i=0;i<r.bodies.count;++i)others[i]={r.bodies.items[i].x,r.bodies.items[i].y,r.bodies.items[i].z};
    const lp::Course k=lp::Plan(g_sword_leap.config(),{p.pos.x,p.pos.y,p.pos.z},{v.raw.pos.x,v.raw.pos.y,v.raw.pos.z},
        strike_tick,faced,others,r.bodies.count);
    const bool begun=g_sword_leap.Begin(lease,k,now);
    char line[sizeof(CmbLine::text)];
    if(lp::FormatPlan(line,sizeof(line),lease,g_sword_leap.config(),k,r.target.locked)>0)CmbLog("%s",line);
    return begun?double(k.travel):0.0;
}
static void SwordDecide(mortal::dispatch::Runtime& run,void* player){
    if(!g_cmb.SwordActions())return;
    const double now=GameNow(g_cmb_global_slot);const uint32_t frame=CmbFrame();
    CmbPlayer p;const bool valid=CmbReadPlayer(p)&&p.player==reinterpret_cast<uint32_t>(player);
    if(valid)CmbScene(p.player);
    if(g_sword_action.lease){
        auto& a=g_sword_action;
        if(a.interruption!=sword::Interrupt::None||!valid||!p.facts.gameplay||p.damaged||a.request.scene!=g_cmb_epoch||a.request.player!=p.player||
           a.request.weapon!=SwordWeapon()||!SwordForeground()||g_oca_session.state()!=oca::State::Idle||
           now<a.start||!run.active||run.lease.token!=a.lease){
            const auto token=a.lease;if(!run.Cancel(token))SwordLifecycle(mortal::dispatch::Event::Abandoned,token);return;
        }
        // backflip-02: control back at TP's land cancel point (backflip.h Release), a normal finish of the lease.
        if(a.request.kind==SwordKind::Flip&&bfl::Release(FlipConfig(),now-a.start)){
            const auto token=a.lease;
            CmbLog("backflip land_release lease=%llu frame=%u e=%.1f release_tick=%.0f",token,frame,now-a.start,bfl::ReleaseTick(FlipConfig()));
            if(!run.Finish(token))SwordLifecycle(mortal::dispatch::Event::Abandoned,token);
            return;
        }
        if(!a.struck&&link_runtime::game_clock::Reached(now-a.start,double(a.strike_tick))){
            a.struck=true;sword::target::List targets;
            if(a.request.kind==SwordKind::Bash){
                CmbLog("shield_bash_strike lease=%llu frame=%u strike_tick=%u",a.lease,frame,a.strike_tick);
                BashFire(frame,now,g_cmb_epoch,p,false);
            }
            else if(a.request.kind==SwordKind::Ending||a.request.kind==SwordKind::Back){targets.items[0]=a.request.target;targets.count=1;}
            else {CmbMem m;targets=sword::target::Select(m,g_cmb_base,g_cmb_tracker,p.pos,CmbGet<float>(p.player+0xA4),SwordSpinTargets(a.request.great),false);}
            // The bullets' Ganados are published after the spin's own, one per tick at best: 2 more ticks each, so a
            // full Great Spin still reaches them at 30 FPS (review 22-oct); never under 2 frames per target, so a full
            // 16-target list lands at 30 FPS too (review 27-oct; 60/165 FPS publish them inside the 24).
            const unsigned shot=a.request.kind==SwordKind::Spin?SpinBullets(a.lease,now,p,a.request.great,targets):0U;
            const double window=24.0+2.0*shot,need=2.0*targets.count;a.hits.Begin(targets,now+(window>need?window:need));
            if(a.request.kind==SwordKind::Spin&&a.request.great){
                const unsigned sparks=GreatSparkle(p);
                CmbLog("spin_strike lease=%llu frame=%u targets=%u strike_tick=%u great=1 radius=%u permille=%u sparks=%u spark_code=%u",a.lease,frame,
                    targets.count,a.strike_tick,g_cmb.great.radius,g_cmb.great.permille,sparks,GreatSparkCode().verified?1U:0U);
            }else
            CmbLog("%s_strike lease=%llu frame=%u targets=%u strike_tick=%u",SwordName(a.request.kind),a.lease,frame,targets.count,a.strike_tick);
            if(a.request.kind==SwordKind::Spin)ParticlesSpin(p,a.request.great);   // sword-particles-01: sparkles, dust, leaves
            if(a.request.kind==SwordKind::Spin)SwordBreakables(a.lease,p,a.request.great);
        }
        if(a.struck)SwordPumpHits(now,p);
        SwordPartsStep(a.lease,p.player);
        SwordTrailStep(a.lease,now);
        TrailStep(a.lease,now);
        return;
    }
    if(g_sword_request.kind==SwordKind::None)return;
    const auto request=g_sword_request;
    if(!valid||!p.facts.gameplay||p.damaged||request.scene!=g_cmb_epoch||request.player!=p.player||request.weapon!=SwordWeapon()||
       !g_sword_sample_player||g_sword_sample.frame!=frame||
       (request.kind==SwordKind::Ending?SwordEndingPriority(g_sword_sample):SwordPriority(g_sword_sample))||!SwordForeground()||run.active||
       (g_rx&&g_rx->Active())||g_oca_session.state()!=oca::State::Idle||now<request.time||
       link_runtime::game_clock::Exceeded(now-request.time,SwordRequestFrames(request.kind))){SwordDropRequest("start_context");return;}
    // Ending replaces this frame's knife press only. Never steal a later fire edge.
    if(request.kind==SwordKind::Ending&&request.frame!=frame){SwordDropRequest("fire_expired");return;}
    const bool knife=SwordKnifeHold(p.routine);
    if(!knife&&!CmbStartRoutine(p.routine))return; // spin release can wait for native knife-down
    if(request.kind==SwordKind::Ending){
        CmbMem m;const auto v=sword::target::Revalidate(m,g_cmb_base,g_cmb_tracker,request.target,p.pos,SwordTargets(),true,
            sword::target::Mode::Select);
        // Named after the failed check (17-oct; was target_left_floor for all three): gone, beyond the reach or the
        // height band (ending_blow_height.h), or no longer down.
        if(!v.reachable||!sword::target::Finishable(v.raw,SwordTargets())){
            SwordDropRequest(!v.same?"target_lost":!v.reachable?"target_out_of_reach":"target_left_floor");return;}
    }
    // Check offers queued since the arbiter too. A late native action always wins.
    CmbMem m;mctx::Sample late;
    if(!mctx::ReadPreArbiter(m,g_cmb_base,late)||(late.offers&&request.kind!=SwordKind::Ending)||late.lock||late.actions_disabled){
        SwordDropRequest("late_native_offer");return;}   // the Ending Blow lets a queued offer be (SwordEndingPriority)
    if(request.kind==SwordKind::Back){
        CmbMem bm;const auto v=sword::target::Revalidate(bm,g_cmb_base,g_cmb_tracker,request.target,p.pos,BackTargets(),false,sword::target::Mode::Select);
        if(!v.reachable){BackAbort(frame,!v.same?"target_lost":"target_out_of_reach");SwordDropRequest("back_target");return;}
    }
    const bool back_tp=request.kind==SwordKind::Back&&g_back_motion;   // TP's Back Slice clip for the press's side
    const auto& clip=request.kind==SwordKind::Bash?g_sword_bash_clip:request.kind==SwordKind::Flip?g_sword_flip_clip:back_tp?BackClip(request.back.side):
        SwordSpinClip(request.kind)?g_sword_spin_clip:g_sword_ending_clip;
    const auto token=clip.ready?mortal::resident_assets::ReserveToken():0;
    const mortal::dispatch::Lease lease{token,clip.fcv,clip.seq,clip.seq?clip.seq+4:nullptr};
    const auto from=knife?mortal::dispatch::StartFrom::KnifeHoldTrial:g_cmb_move_start?
        mortal::dispatch::StartFrom::NormalMove:mortal::dispatch::StartFrom::NormalIdle;
    g_sword_action={};g_sword_action.request=request;g_sword_action.start=now;
    g_sword_action.strike_tick=request.kind==SwordKind::Back?BackConfig().StrikeTick(clip.strike_tick):clip.strike_tick;
    if(request.kind==SwordKind::Flip)g_sword_action.strike_tick=kFlipNoStrike;   // the backflip never strikes (no hit list)
    g_sword_action.grounded=request.kind==SwordKind::Ending;   // the pre-start check required Grounded or Rising (lethal on rise)
    g_sword_starting=request.kind;
    const auto result=token?mortal::trial::TryStartClip(run,g_mortal_native,lease,from):mortal::trial::Attempt{mortal::trial::Status::AssetsMissing};
    g_sword_starting=SwordKind::None;g_sword_request={};
    if(result.status!=mortal::trial::Status::Started&&request.kind==SwordKind::Back)BackAbort(frame,"start_failed");
    if(result.status!=mortal::trial::Status::Started&&request.kind==SwordKind::Flip)CmbLog("backflip abort frame=%u reason=start_failed",frame);
    // The bash without its clip: it still lands now (with the parry reaction as the shove), as before spin-fix-01.
    if(result.status!=mortal::trial::Status::Started&&request.kind==SwordKind::Bash)BashFire(frame,now,g_cmb_epoch,p,true);
    if(result.status!=mortal::trial::Status::Started){g_sword_action={};CmbLog("%s_start failed status=%u native_fire_masked=0 native_transitioned=%u",SwordName(request.kind),static_cast<unsigned>(result.status),run.transitioned?1U:0U);return;}
    double travel=0;
    if(request.kind==SwordKind::Ending){
        uint32_t trg=0;if(m.Read(g_cmb_base+mctx::layout::input_trg_rva,&trg,4)){trg&=~uint32_t(mctx::layout::key_fire);m.Write(g_cmb_base+mctx::layout::input_trg_rva,&trg,4);}
        const auto v=sword::target::Revalidate(m,g_cmb_base,g_cmb_tracker,request.target,p.pos,SwordTargets(),true,sword::target::Mode::Select);
        float yaw=0;const bool faced=v.same&&mtg::FaceYaw(p.pos,v.raw.pos,yaw)&&m.Write(p.player+0xA4,&yaw,4);
        if(g_cmb.ending_leap)travel=SwordLeapTakeoff(token,now,request,p,v,faced,clip.strike_tick);
    }
    if(request.kind==SwordKind::Back){
        g_back_have_last=true;g_back_last=now;
        const auto bc=BackConfig();
        g_back_circle=bks::Circle(bc);
        const bool begun=g_back_circle.Begin(token,request.back,now,p.pos.y);
        CmbLog("back_slice start lease=%llu frame=%u side=%d d=%.0f radius=%.0f sweep_deg=%.0f arc_frames=%u strike_tick=%u permille=%u arc=%u "
            "motion=%s",token,frame,request.back.side,double(request.back.d),double(request.back.radius),double(request.back.sweep)*57.29578,
            bc.arc_frames,g_sword_action.strike_tick,g_cmb.back.permille,begun?1U:0U,back_tp?(bks::ClipRoll(request.back.side)==bks::Roll::Right?"tp_right":"tp_left"):"spin");
    }
    if(request.kind==SwordKind::Flip){
        g_flip_have_last=true;g_flip_last=now;
        g_flip_move=bfl::Move(FlipConfig());
        const bool begun=g_flip_move.Begin(token,request.flip,now,{p.pos.x,p.pos.y,p.pos.z});
        CmbLog("backflip start lease=%llu frame=%u locked=%u distance=%.0f travel_ticks=%u travel=%u",token,frame,request.flip.locked?1U:0U,
            double(request.flip.distance),g_flip_move.config().travel_ticks,begun?1U:0U);
    }
    CmbLog("%s_start started lease=%llu frame=%u strike_tick=%u from_knife=%u voice=TP root_travel=%.0f%s",SwordName(request.kind),token,frame,
        clip.strike_tick,knife?1U:0U,travel,request.kind==SwordKind::Ending&&g_cmb.ending_leap?(request.target.locked?" prefer=lock":" prefer=near"):"");
}
static bool CmbResident(const wchar_t*,const wchar_t*,size_t,const char*,unsigned char*&);
// The clip's fcv/seq (resident, SHA-pinned); ready only once its voice also loaded (SwordLoad / SwordPrepare).
static bool SwordLoadClip(const wchar_t* directory,const sword::assets::Clip& spec,SwordClip& clip){
    wchar_t name[96]{};
    if(link_runtime::paths::Format(name,L"link_%ls.fcv",spec.name)<0)return false;
    if(!CmbResident(directory,name,spec.fcv_size,spec.fcv_sha,clip.fcv))return false;
    if(link_runtime::paths::Format(name,L"link_%ls.seq",spec.name)<0)return false;
    if(!CmbResident(directory,name,spec.seq_size,spec.seq_sha,clip.seq))return false;
    clip.strike_tick=spec.strike_tick;return true;
}
// link_<voice or name>_voice.wav, SHA-pinned, at the TP voice volume.
static bool SwordLoadVoice(const wchar_t* directory,const sword::assets::Clip& spec,AlertVoice& voice){
    wchar_t path[MAX_PATH]{};
    return link_runtime::paths::Format(path,L"%lslink_%ls_voice.wav",directory,spec.voice?spec.voice:spec.name)>=0&&voice.Load(path,35,spec.voice_sha);
}
static bool SwordLoad(const wchar_t* directory,const sword::assets::Clip& spec,SwordClip& clip,AlertVoice& voice){
    if(!SwordLoadClip(directory,spec,clip)||!SwordLoadVoice(directory,spec,voice))return false;
    clip.ready=true;return true;
}
static void SwordPrepare(const wchar_t* directory,bool dispatch_up,bool move_up){
    if(!g_cmb.SwordActions())return;
    if(!dispatch_up||!move_up||!g_cmb_adapter_on){g_cmb.spin_attack=g_cmb.ending_blow=false;Log("Sword actions disabled: dispatch, MotionMove or adapter unavailable");return;}
    if(g_cmb.spin_attack&&!SwordLoad(directory,sword::assets::spin_attack,g_sword_spin_clip,g_sword_spin_audio)){
        g_cmb.spin_attack=false;Log("SpinAttack disabled: clip or voice missing/changed");}
    if(g_cmb.ShieldBash()&&g_cmb.bash.motion&&!SwordLoad(directory,sword::assets::shield_bash,g_sword_bash_clip,g_sword_bash_audio))
        Log("ShieldBashMotion disabled: clip or voice missing/changed (the bash fires on the tap)");
    if(g_cmb.ending_blow&&!SwordLoad(directory,sword::assets::ending_blow,g_sword_ending_clip,g_sword_ending_audio)){
        g_cmb.ending_blow=false;Log("EndingBlow disabled: clip or voice missing/changed");}
    // backslice-tp-01: TP's Back Slice clips (both sides) and their shared voice; missing or changed = the spin clip.
    g_back_motion=false;
    if(g_cmb.BackSlice()&&g_cmb.back.motion){
        g_back_motion=SwordLoadClip(directory,sword::assets::back_slice_right,g_sword_back_right)&&
            SwordLoadClip(directory,sword::assets::back_slice_left,g_sword_back_left)&&
            SwordLoadVoice(directory,sword::assets::back_slice_right,g_sword_back_audio);
        g_sword_back_right.ready=g_sword_back_left.ready=g_back_motion;
        if(!g_back_motion)Log("BackSliceMotion disabled: TP clip or voice missing/changed (the spin clip plays)");
    }
    g_bullet_los_ok=PinsAt(g_cmb_base,link_runtime::ztarget::los::kNearestPins);
    g_bullet_sound_ok=PinsAt(g_cmb_base,spb::kSoundPins);
    if(g_cmb.spin_attack)Log("SpinBullets on=%u spin=%u great=%u range=%.0f walls=%u sound=%u problems=%u",g_cmb.bullets.on?1U:0U,g_cmb.bullets.spin,
        g_cmb.bullets.great,double(g_cmb.bullets.range),g_bullet_los_ok?1U:0U,g_bullet_sound_ok?1U:0U,g_cmb.bullets.problems);
    Log("Sword actions ready SpinAttack=%u EndingBlow=%u charge_ms=1000 spin_radius=%.0f ending_range=%u line_waiver=%u native_qa=0",
        g_cmb.spin_attack?1U:0U,g_cmb.ending_blow?1U:0U,double(sword::target::Config{}.spin_radius),g_cmb.ending_strike_range,g_cmb.line_waiver?1U:0U);
    // sword-hand-01: the knife draw's setKnife / setDisp bytes and the weapon display levels (kOcarinaWeapon) must match.
    {
        const bool pins=PinsAt(g_cmb_base,link_runtime::native_pins::kSwordHand)&&PinsAt(g_cmb_base,link_runtime::native_pins::kOcarinaWeapon);
        g_sword_hand_ok=g_cmb.sword_hand&&pins;
        if(!g_cmb.sword_hand)Log("SwordInHand=0: the hand keeps the equipped weapon during sword actions");
        else if(!pins)Log("SwordInHand disabled: native pins (knife draw setKnife/setDisp, draw gate, weapon display levels)");
        else Log("SwordInHand ready=1 knife=Body+0x2C bit8 gun=cObjWep level0 actions=spin,great_spin,ending,leap,back_slice,shield_bash");
    }
    // Great Spin / Shield Bash (18-oct): sounds (optional, SHA-pinned), the deflect pins, readiness lines.
    {
        wchar_t path[MAX_PATH]{};
        const bool gs_sound=g_cmb.GreatSpin()&&link_runtime::paths::Format(path,L"%ls%ls",directory,gspin::kSoundFile)>0&&g_great_spin_sound.Load(path,kGssbVolume,gspin::kSoundSha);
        const bool sb_sound=g_cmb.ShieldBash()&&link_runtime::paths::Format(path,L"%ls%ls",directory,sbash::kSoundFile)>0&&g_shield_bash_sound.Load(path,kGssbVolume,sbash::kSoundSha);
        if(g_cmb.SwordActions())
            Log("GreatSpin ready=%u radius=%u permille=%u sparkles=%u sound=%u",g_cmb.GreatSpin()?1U:0U,g_cmb.great.radius,g_cmb.great.permille,
                g_cmb.great.sparkles,gs_sound?1U:0U);
        if(g_cmb.BackSlice()){
            g_bash_ready=true;   // the Back Slice reads the Shield Bash's knife tap
            const auto bc=BackConfig();
            Log("BackSlice ready=1 key=ZTarget+A/D(or stick sideways)+knife_tap cooldown_ms=1000 permille=%u arc_frames=%u radius=%u..%u reach=%u "
                "motion=%s curve=%s strike_tick=%u",g_cmb.back.permille,bc.arc_frames,g_cmb.back.min_radius,g_cmb.back.max_radius,g_cmb.back.reach,
                g_back_motion?"tp":"spin",bc.curve==bks::Curve::Tp?"tp_turn":"smootherstep",
                bc.StrikeTick(g_back_motion?g_sword_back_right.strike_tick:g_sword_spin_clip.strike_tick));
        }else if(g_cmb.spin_attack)Log("BackSlice ready=0 (BackSlice=0)");
        // backflip-01: only with Backflip=1 (default off: nothing loaded, the tap as before).
        if(g_cmb.Backflip()){
            if(SwordLoad(directory,sword::assets::backflip,g_sword_flip_clip,g_sword_flip_audio)){
                g_bash_ready=true;   // the backflip reads the Shield Bash's knife tap
                Log("Backflip ready=1 key=ZTarget+S(or stick back)+knife_tap cooldown_ms=1000 distance=%u travel_ticks=%u land_frames=%u "
                    "release_tick=%.0f early=%u iframes=0",g_cmb.flip.distance,FlipConfig().travel_ticks,FlipConfig().land_frames,
                    bfl::ReleaseTick(FlipConfig()),bfl::EarlyRelease(FlipConfig())?1U:0U);
            }else Log("Backflip ready=0 (clip or voice missing/changed: link_backflip.fcv/.seq/_voice.wav)");
        }
        if(g_cmb.ShieldBash()){
            link_runtime::parry::general::Memory pm;
            g_bash_deflect_ok=g_cmb.bash.deflect_ms&&sbash::thrown::Qualify(pm,g_cmb_base);
            g_bash_ready=true;
            Log("ShieldBash ready=1 key=ZTarget+knife_tap tap_ms=250 cooldown_ms=750 reach=%u targets=%u cone=%.0f deflect_ms=%u deflect_reach=%u "
                "deflect_pins=%u sound=%u motion=%u strike_tick=%u damage=knockback",g_cmb.bash.reach,g_cmb.bash.targets,double(sbash::kHalfConeDeg),
                g_cmb.bash.deflect_ms,g_cmb.bash.deflect_reach,g_bash_deflect_ok?1U:0U,sb_sound?1U:0U,g_sword_bash_clip.ready?1U:0U,g_sword_bash_clip.strike_tick);
        }else if(g_cmb.SwordActions())Log("ShieldBash ready=0 (ShieldBash=0)");
        if(g_cmb.gs_sb_problems)Log("GreatSpin/ShieldBash: %u invalid values; their defaults kept",g_cmb.gs_sb_problems);
    }
    g_sword_leap=ending::leap::Leap(SwordLeapConfig());
    if(g_cmb.ending_blow&&g_cmb.ending_leap){
        // burst= the INI percent after the 1..3 clamp; takeoff= e0 for the clip's window; max_cmd= the cap at max_travel;
        // far_*/strike_*: the height bands beyond the strike reach and at the strike (ending_blow_height.h).
        const auto& lc=g_sword_leap.config();
        const float land=g_sword_ending_clip.strike_tick>lc.land_margin?float(g_sword_ending_clip.strike_tick-lc.land_margin):0.f;
        Log("EndingBlowLeap mode=%s leap_range=%u leap_free_range=%u standoff=%u strike_range=%u prefer_lock=%u max_travel=%.0f land=strike_tick-2 "
            "burst=%u takeoff=%.0f max_cmd=%.1f far_down=%.0f far_up=%.0f strike_down=%.0f strike_up=%.0f",ending::leap::ModeName(lc),g_cmb.leap_range,g_cmb.leap_free_range,g_cmb.leap_standoff,
            g_cmb.ending_strike_range,g_cmb.leap_prefer_lock?1U:0U,double(lc.MaxTravel()),ending::leap::BurstPercent(lc),double(lc.Start(land)),
            double(lc.CmdCap(lc.MaxTravel(),land)),double(lc.beyond.down),double(lc.beyond.up),double(SwordTargets().strike_band.down),
            double(SwordTargets().strike_band.up));
    }
}
static void SwordPollAudio(){
    if(!SwordForeground()){g_sword_spin_voice.Stop(g_sword_spin_voice.Current());g_sword_ending_voice.Stop(g_sword_ending_voice.Current());
        g_sword_back_voice.Stop(g_sword_back_voice.Current());g_sword_flip_voice.Stop(g_sword_flip_voice.Current());}
    if(const unsigned s=g_gssb_sound.exchange(0,std::memory_order_acq_rel);s&&SwordForeground()){
        if(s&1)g_great_spin_sound.Play();
        if(s&2)g_shield_bash_sound.Play();
        if(s&4)g_charge_sound.Play();
    }
    if(g_sword_spin_clip.ready)g_sword_spin_consumer.Poll(g_sword_spin_voice,g_sword_spin_audio);
    if(g_sword_ending_clip.ready)g_sword_ending_consumer.Poll(g_sword_ending_voice,g_sword_ending_audio);
    if(g_back_motion)g_sword_back_consumer.Poll(g_sword_back_voice,g_sword_back_audio);
    if(g_sword_flip_clip.ready)g_sword_flip_consumer.Poll(g_sword_flip_voice,g_sword_flip_audio);
}

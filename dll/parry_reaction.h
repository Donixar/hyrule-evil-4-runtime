#pragma once
// Link's brief sword reaction after a Committed parry, layered on the native
// secondary-motion route (player+2A8) over the live primary pose.
//
// Pure logic: no process access, hooks or native calls live here. The wiring
// supplies a World snapshot and a Port with the effects. Native semantics this
// relies on were executed in tests/native/test_parry_secondary_route.cpp on the
// pinned image (SHA bb59e550...) and are documented in
// docs/parry-reaction-2026-10-12.md:
//  - Init 2611A0 clears player+2A8 unless work+44 has 20000000, so ANY primary
//    MotionSet detaches us. We never fight that: a changed 2A8 ends the reaction
//    without writes (LostOwner).
//  - MotionMove skips the blend at weight exactly 0 (exact primary pose), so the
//    exit ramps to 0 before giving the slot back.
//  - The native neck controller (27E6E0) owns player+474 as its own secondary.
//    It rewrites [2A8]+C8 every call while 2A8 is non-null AND, when the look
//    angle changes sign, calls its setter 27CAC0 (27E8F1/27E954), which writes
//    2A8=player+474 (27CB1A) and steals the slot. Wiring contract:
//      1. Tick runs in the MotionMove (262B90) detour right before the original
//         call. No call inside 262B90..262FC0 reaches 27E6E0, 27CAC0 or 2611A0,
//         so the weight we write is the one the secondary block consumes.
//      2. The neck update detour (thunk 4836 -> 27E6E0, thiscall, RET 4) returns
//         without calling the original while SuppressNeck(player) is true.
//      3. 27EC20 (neck re-init from ~20 player action routines, call 27EC66) is
//         NOT gated: it starts a new player action, normally together with a
//         primary MotionSet that detaches us anyway; it ends as lost_owner.
//    On exit the CAS gives 2A8 back to player+474 and the neck resumes.
//  - Non-looping SEQ reports work+42 == 4 at its end and holds the last pose; the
//    native code never detaches it, so the exit is ours.
// Units: one Tick per player MotionMove (game logic frame). The schedule below is in 60 Hz
// frames of game time: each Tick advances World::step (2*deltaTime, 1.0 at 60 FPS), so under
// re4_fps 165 the layer lasts as long as the native SEQ instead of 2.75x less. With the study SEQ
// (17 entries) at controller step 1 and speed 1 the end status arrives on the
// 17th MotionMove. Real step/speed in game are unverified.
#include <atomic>
#include <cstdint>
#include "game_clock.h"

namespace link_runtime::parry::reaction {
inline constexpr uint32_t kNeckWorkOffset=0x474;  // 27CAC0: LEA EDI,[ESI+474h]
// Knife/sword stance (KAMAE knife) secondary work: a static block of bio4.exe, not a player field.
// Live read 15-oct night (PID of that session, base 0x540000): player+2A8 = 0xDBA800 while the sword
// is drawn, 0 when sheathed. Every parry happens in this stance, so without accepting it the
// reaction was refused on every commit (combat stats refused=1, starts=0). Same contract as the
// neck: we take the slot, and the exit CAS gives this exact work back.
inline constexpr uint32_t kStanceWorkRva=0x87A800;
inline constexpr uint16_t kSeqEnded=4;            // 25FE85: work+42 = 4 at non-looping end

struct Config {
    bool enabled=false;          // INI ParryReaction, default 0
    unsigned ramp_in_ticks=3;    // weight k/3 -> 1
    unsigned ramp_out_start=13;  // tick (from install) where the ramp out begins
    unsigned ramp_out_ticks=4;   // weight 3/4 .. 0
    unsigned max_ticks=0;        // derived by Sanitize
};
inline Config Sanitize(Config c){
    auto clamp=[](unsigned v,unsigned lo,unsigned hi){return v<lo?lo:v>hi?hi:v;};
    c.ramp_in_ticks=clamp(c.ramp_in_ticks,1,10);
    c.ramp_out_ticks=clamp(c.ramp_out_ticks,1,15);
    c.ramp_out_start=clamp(c.ramp_out_start,c.ramp_in_ticks,60);
    c.max_ticks=c.ramp_out_start+c.ramp_out_ticks+2;
    return c;
}
// Read by the wiring on the game thread right before the player's MotionMove.
struct World {
    uint32_t player=0;           // current player object; 0 = none
    bool damaged=false,grabbed=false,dead=false,scene=false;
    bool mortal_draw=false,ocarina=false,weapon_switch=false;
    bool disabled=false;         // module stopped / quarantined
    uint32_t frame=0;            // game frame counter if known (0 = unknown);
                                 // a repeated frame only reasserts the weight
    float step=1.f;              // this tick in 60 Hz frames (game_clock::Step60; 1 at 60 FPS)
    uint32_t image_base=0;       // bio4.exe base (ASLR); 0 = unknown: the stance work is not accepted
    uint32_t guest=0;            // shield-guard-01: the guard layer's work (shield_guard.h); accepted like the
                                 // neck/stance, and the exit CAS gives 2A8 back to it (0 = none)
    bool Blocked() const {return !player||damaged||grabbed||dead||scene||mortal_draw||ocarina||weapon_switch||disabled;}
};
struct Stats {
    unsigned starts=0,completed=0,cancelled=0,lost_owner=0,dropped=0,refused=0;
    unsigned install_failed=0,ignored_overlap=0,foreign_weight=0,timeouts=0;
    unsigned neck_suppressed=0,repeated_frame=0;
    uint32_t owner_id=0;         // owner+100/+101 (id | type<<8) of the last commit, for the log
};
enum class Phase {Idle,In,Hold,Out,Zero};

// Port contract (all on the game thread):
//  bool     Install(player, out work)  native init of OUR work (override mode,
//                                       hokan 0, attr 0, keep 0) then 2A8=work, C8=0
//  uint32_t Secondary(player)          reads player+2A8
//  bool     ReplaceSecondary(player, expected, value)  writes 2A8 only if it equals expected
//  float    Weight(work) / void SetWeight(work, w)      work+C8
//  uint16_t Status(work)                                work+42
class Reaction {
    Config config_;
    std::atomic<uint32_t> latest_{0};
    std::atomic<uint32_t> latest_owner_{0};
    uint32_t consumed_=0,player_=0,work_=0,previous_=0,frame_=0;
    double tick_=0,out_tick_=0;  // game time since install / since the ramp out (60 Hz frames)
    float last_=0.f,out_from_=1.f;
    Phase phase_=Phase::Idle;
    Stats stats_;
    void Idle(){phase_=Phase::Idle;player_=work_=previous_=frame_=0;tick_=out_tick_=0;}
    template<class Port> void Give(Port& port){
        if(port.ReplaceSecondary(player_,work_,previous_))return;
        ++stats_.lost_owner;
    }
    template<class Port> void Write(Port& port,float w){
        if(port.Weight(work_)!=last_)++stats_.foreign_weight;
        port.SetWeight(work_,w);last_=w;
    }
public:
    explicit Reaction(Config c):config_(Sanitize(c)){}
    const Config& config() const {return config_;}
    const Stats& stats() const {return stats_;}
    bool Active() const {return phase_!=Phase::Idle;}
    // Called once per Committed parry (commit::Result::Committed) with a
    // nonzero serial that changes per commit. Never touches native state.
    // owner_id = owner+100 | owner+101<<8 from the commit observation (log only).
    void OnCommitted(uint32_t serial,uint32_t owner_id=0){
        if(!config_.enabled||!serial)return;
        latest_owner_.store(owner_id,std::memory_order_relaxed);
        latest_.store(serial,std::memory_order_release);
    }
    // Neck update detour: true = skip 27E6E0 this call (our reaction owns 2A8).
    bool SuppressNeck(uint32_t player){
        if(phase_==Phase::Idle||player!=player_)return false;
        ++stats_.neck_suppressed;return true;
    }

    template<class Port> void Tick(const World& world,Port& port){
        const uint32_t latest=latest_.load(std::memory_order_acquire);
        const bool fresh=latest!=consumed_;consumed_=latest;
        if(fresh)stats_.owner_id=latest_owner_.load(std::memory_order_relaxed);
        if(phase_!=Phase::Idle&&world.frame&&world.frame==frame_){
            // Second MotionMove of the player in one frame: same weight, no advance.
            ++stats_.repeated_frame;
            if(world.player==player_&&port.Secondary(player_)==work_&&phase_!=Phase::Zero)port.SetWeight(work_,last_);
            return;
        }
        if(phase_==Phase::Idle){
            if(!fresh||!config_.enabled)return;
            if(world.Blocked()){++stats_.refused;return;}
            const uint32_t current=port.Secondary(world.player);
            const bool stance=world.image_base&&current==world.image_base+kStanceWorkRva;
            const bool guest=world.guest&&current==world.guest;
            if(current&&current!=world.player+kNeckWorkOffset&&!stance&&!guest){++stats_.refused;return;}
            uint32_t work=0;
            if(!port.Install(world.player,work)||!work||port.Secondary(world.player)!=work){++stats_.install_failed;return;}
            ++stats_.starts;player_=world.player;work_=work;previous_=current;tick_=0;last_=port.Weight(work);
            phase_=Phase::In;
        }else if(fresh){
            ++stats_.ignored_overlap;
        }
        frame_=world.frame;
        if(world.player!=player_){++stats_.dropped;Idle();return;}           // never write through an old player
        if(port.Secondary(player_)!=work_){++stats_.lost_owner;Idle();return;} // native detach or neck takeover
        if(world.Blocked()){++stats_.cancelled;Give(port);Idle();return;}
        namespace gc=link_runtime::game_clock;
        const double step=std::isfinite(world.step)&&world.step>0.f?double(world.step):1.0;
        if(gc::Reached(tick_,double(config_.max_ticks))){++stats_.timeouts;Give(port);Idle();return;}
        if((phase_==Phase::In||phase_==Phase::Hold)&&
           (gc::Reached(tick_,double(config_.ramp_out_start))||port.Status(work_)==kSeqEnded)){phase_=Phase::Out;out_tick_=0;out_from_=last_;}
        switch(phase_){
        case Phase::In:{
            const double k=tick_+step;
            const bool full=gc::Reached(k,double(config_.ramp_in_ticks));
            Write(port,full?1.f:float(k)/float(config_.ramp_in_ticks));
            if(full)phase_=Phase::Hold;
            break;}
        case Phase::Hold:Write(port,1.f);break;
        case Phase::Out:{
            const double k=out_tick_+=step;
            // Ramp from the weight in effect (an early end during ramp-in never jumps up).
            const float w=gc::Reached(k,double(config_.ramp_out_ticks))?0.f:
                out_from_*float(double(config_.ramp_out_ticks)-k)/float(config_.ramp_out_ticks);
            Write(port,w);
            if(w==0.f)phase_=Phase::Zero;
            break;}
        case Phase::Zero:
            // The previous tick ran MotionMove at weight 0 (exact primary pose).
            Give(port);++stats_.completed;Idle();return;
        default:break;
        }
        tick_+=step;
    }
};
}  // namespace link_runtime::parry::reaction

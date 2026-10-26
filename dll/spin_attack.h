#pragma once
// Pure input and bounded multi-target hit queue. No hooks, native input writes,
// HP writes, immunity or parry-window modifications.
#include "ending_blow_target.h"
#include "mortal_draw_damage.h"
#include "game_clock.h"

namespace link_runtime::sword_actions {
struct Input {
    uint64_t frame=0;double time=0;uint32_t scene=0,player=0,weapon=0;
    bool valid=false,safe=false,knife=false,knife_edge=false,fire=false,fire_edge=false;
};
inline bool SameContext(const Input& a,const Input& b) {return a.scene==b.scene&&a.player==b.player&&a.weapon==b.weapon;}
// spin-fix-01: a queued request (Spin released, Back Slice tapped) waits in the arbiter until the player's dispatch
// can start its clip (SwordDecide: the knife hold or a normal idle/move routine). The arbiter used to drop it as
// soon as the routine was not a "charge" routine; a Back Slice is decided on the TAP's release, when the native
// knife is still in its draw/put-away (r1 0x0B, r2 not 0/1/3), so every one was cancelled the same frame
// (Donald 18-oct night: back_slice request ... then back_slice_request cancelled=context_or_native at once).
// Kept: the knife family except an attack (r2 2) and, while the knife key is still held, only draw/hold; and any
// routine the clip may start from (start_routine). Anything else (aim, damage, grab, event...) drops it.
// shield-guard-01 (live pid=2304, 18-oct night): a Ctrl+tap releases the knife into the normal routine's entry
// step, 0x00000000 (r0 0, r1 0, r2 0), for the frame the bash is requested; the next frame is the native idle
// 0x00010000. Every Shield Bash died there as cancelled=routine. The normal family (r0 0, r1 0..4: idle, walk,
// back, run, turn, any step) is kept: the start itself still waits for a start routine (SwordDecide).
inline bool NormalFamily(uint32_t routine){return (routine&0xFF)==0&&((routine>>8)&0xFF)<=4;}
inline bool PendingRoutine(uint32_t routine,bool knife,bool start_routine){
    if(start_routine||NormalFamily(routine))return true;
    if((routine&0xFFFF)!=0x0B00)return false;
    const uint32_t r2=(routine>>16)&0xFF;
    return r2!=2&&(r2<=1||!knife);
}
enum class Keep {Ok,Invalid,Priority,Occupied,Routine,Scene,Player,Weapon,Expired};
inline const char* KeepName(Keep k){
    static const char* const n[]={"ok","invalid","native_priority","occupied","routine","scene","player","weapon","expired"};
    return n[unsigned(k)];
}
struct Pending {bool valid=false,priority=false,occupied=false,routine_ok=false;uint32_t scene=0,player=0,weapon=0;double time=0;};
// Why a queued request is dropped this frame (Ok = it stays). limit: game frames it may wait.
inline Keep KeepRequest(const Pending& now,uint32_t scene,uint32_t player,uint32_t weapon,double since,double limit){
    if(!now.valid||!std::isfinite(now.time))return Keep::Invalid;
    if(now.priority)return Keep::Priority;
    if(now.occupied)return Keep::Occupied;
    if(now.scene!=scene)return Keep::Scene;
    if(now.player!=player)return Keep::Player;
    if(now.weapon!=weapon)return Keep::Weapon;
    if(game_clock::Exceeded(now.time-since,limit))return Keep::Expired;
    if(!now.routine_ok)return Keep::Routine;
    return Keep::Ok;
}
enum class Interrupt {None,Invalid,Context,Ownership,NativePriority};
inline Interrupt ActiveInterrupt(const Input& origin,const Input& now,bool owns,bool native_priority) {
    // safe is an admission gate: the owned Event routine itself makes it false.
    if(!now.valid||!std::isfinite(now.time)||now.time<origin.time-game_clock::kEpsilon)return Interrupt::Invalid;
    if(!SameContext(origin,now))return Interrupt::Context;
    if(!owns)return Interrupt::Ownership;
    return native_priority?Interrupt::NativePriority:Interrupt::None;
}
// spin-fix-01 (Donald 18-oct night, r103: a Great Spin found 2 Ganados in reach and skipped both on the strike
// frame, published=0 skipped=2, with no reason logged). A rejection before the native call is now judged:
//   - Scaled refused on the head/core part: the same token publishes an ordinary hit (as before);
//   - Ordinary with no body part ON but the head (verdict Excluded): one ordinary hit on the head, as the native
//     knife would land it (HitTarget::allow_head; core 0x25 is never taken);
//   - transient (the Ganado's own native record still pending, or no ON part / a hit list in flux: Empty,
//     BadFlags, UnknownPart, Disabled, NotListed): the target is deferred and retried on later game frames, at most
//     kRetries times, until the hit window closes (then skipped);
//   - anything else (dead, identity lost, a kind's part rule, a failed publish): skipped at once.
// Every attempt that does not publish is reported to the observer (target, status, part verdict, deferred) so the
// runtime can log it. Still one native publish per target at most: no retry after a publish attempt.
class HitQueue {
public:
    static constexpr unsigned kRetries=12;
    struct Skip {mortal_draw::damage::Status status=mortal_draw::damage::Status::Published;
        native_hitpart::Verdict verdict=native_hitpart::Verdict::Empty;bool deferred=false,revalidate=false;unsigned tries=0;};
    static bool Transient(mortal_draw::damage::Status s,const mortal_draw::damage::Facts& f){
        namespace d=mortal_draw::damage;namespace h=native_hitpart;
        if(s==d::Status::PendingNative)return true;
        if(s!=d::Status::NoHitpart||f.hitpart)return false;   // a part was found and the kind's rule refused it: final
        return f.part_verdict==h::Verdict::Empty||f.part_verdict==h::Verdict::BadFlags||f.part_verdict==h::Verdict::UnknownPart||
               f.part_verdict==h::Verdict::Disabled||f.part_verdict==h::Verdict::NotListed;
    }
private:
    target::List list_;unsigned next_=0,published_=0,applied_=0,skipped_=0,deferred_total_=0;
    unsigned retry_[target::kMaxTargets]={},tries_[target::kMaxTargets]={},retry_count_=0;double retry_at_[target::kMaxTargets]={};
    uint64_t pending_=0;double deadline_=0;bool on_=false;
    struct NoObserver{void operator()(const target::Target&,const Skip&)const{}};
    // One attempt at list_.items[i]. Returns 0 published, 1 busy (try again this frame later), 2 skipped, 3 deferred.
    template<class Check,class Observe> int Attempt(unsigned i,double now,mortal_draw::damage::Adapter& adapter,Check& check,Observe& observe){
        namespace d=mortal_draw::damage;
        d::HitTarget hit;d::Kind kind=d::Kind::Ordinary;int amount=0;
        Skip info;info.tries=tries_[i];
        if(!check(list_.items[i],hit,kind,amount)){info.revalidate=true;observe(list_.items[i],info);return 2;}   // gone or out of reach
        const uint64_t token=adapter.Issue();
        auto status=adapter.NativeHit(hit,kind==d::Kind::Ordinary?0:amount,kind,token);
        if(status==d::Status::OverCap)status=adapter.NativeHit(hit,0,d::Kind::Ordinary,token);
        // Great Spin / Back Slice (Scaled): a refused head/core part still takes an ordinary hit.
        if(status==d::Status::NoHitpart&&kind==d::Kind::Scaled){kind=d::Kind::Ordinary;status=adapter.NativeHit(hit,0,kind,token);}
        // Ordinary with only the head ON: the head, as the native knife (never the core).
        if(status==d::Status::NoHitpart&&kind==d::Kind::Ordinary&&!hit.hitpart&&!hit.allow_head&&
           adapter.LastFacts().part_verdict==native_hitpart::Verdict::Excluded){hit.allow_head=true;status=adapter.NativeHit(hit,0,kind,token);}
        if(status==d::Status::Busy)return 1;
        if(status==d::Status::Published){pending_=token;++published_;return 0;}
        info.status=status;info.verdict=adapter.LastFacts().part_verdict;
        info.deferred=Transient(status,adapter.LastFacts())&&tries_[i]<kRetries;
        observe(list_.items[i],info);
        if(!info.deferred)return 2;
        ++tries_[i];++deferred_total_;retry_at_[i]=now;return 3;
    }
public:
    void Begin(const target::List& in,double deadline) {
        list_={};next_=published_=applied_=skipped_=deferred_total_=retry_count_=0;pending_=0;deadline_=deadline;on_=true;
        for(auto& t:tries_)t=0;
        for(unsigned i=0;i<in.count&&i<target::kMaxTargets;++i){
            const auto& t=in.items[i];bool duplicate=!t.identity||!t.pointer;
            for(unsigned j=0;j<list_.count;++j)duplicate|=list_.items[j].identity==t.identity||list_.items[j].pointer==t.pointer;
            if(!duplicate)list_.items[list_.count++]=t;
        }
    }
    unsigned Count() const{return list_.count;}
    unsigned Published() const{return published_;}
    unsigned Applied() const{return applied_;}
    unsigned Skipped() const{return skipped_;}
    unsigned Deferred() const{return retry_count_;}          // targets waiting for a later frame now
    unsigned DeferredTotal() const{return deferred_total_;}  // deferrals since Begin
    bool Done() const{return !on_||(next_==list_.count&&!pending_&&!retry_count_);}
    // check revalidates identity/geometry at the publish boundary and fills the
    // native HitTarget, Kind and amount (Lethal: the heavy-target cap, 0 = none;
    // see damage::Kind). No retry after a native publish attempt.
    template<class Check> void Pump(double now,mortal_draw::damage::Adapter& adapter,Check&& check){NoObserver o;Pump(now,adapter,check,o);}
    template<class Check,class Observe> void Pump(double now,mortal_draw::damage::Adapter& adapter,Check&& check,Observe&& observe) {
        namespace d=mortal_draw::damage;
        if(!on_)return;
        if(pending_){const auto o=adapter.OutcomeOf(pending_);if(o==d::Outcome::Pending)return;if(o==d::Outcome::Applied)++applied_;pending_=0;}
        if(!std::isfinite(now)||game_clock::Exceeded(now-deadline_,0)){skipped_+=list_.count-next_+retry_count_;next_=list_.count;retry_count_=0;return;}
        while(next_<list_.count&&!adapter.Pending()){
            const int r=Attempt(next_,now,adapter,check,observe);
            if(r==1)return;
            const unsigned i=next_++;
            if(r==0)return;
            if(r==2)++skipped_;
            if(r==3)retry_[retry_count_++]=i;
        }
        // Deferred targets: once per game frame each, in deferral order.
        for(unsigned k=0;k<retry_count_&&!adapter.Pending();){
            const unsigned i=retry_[k];
            if(!game_clock::Reached(now-retry_at_[i],1.0)){++k;continue;}
            const int r=Attempt(i,now,adapter,check,observe);
            if(r==1)return;
            if(r==3){++k;continue;}   // still transient: stays, next frame
            for(unsigned j=k+1;j<retry_count_;++j)retry_[j-1]=retry_[j];
            --retry_count_;
            if(r==2)++skipped_;
            if(r==0)return;
        }
    }
    void Cancel(mortal_draw::damage::Adapter& adapter){
        if(pending_){if(adapter.Applied(pending_))++applied_;else adapter.Cancel(pending_);}
        pending_=0;skipped_+=list_.count-next_+retry_count_;next_=list_.count;retry_count_=0;on_=false;
    }
};
}
namespace link_runtime::spin_attack {
enum class Event {None,Charging,Charged,Released,Cancelled};
class Charge {
    sword_actions::Input previous_;bool known_=false,armed_=false,charging_=false,charged_=false;double start_=0;
public:
    bool Active() const{return charging_;}
    void Cancel(){charging_=charged_=false;armed_=false;}
    Event Step(const sword_actions::Input& s) {
        if(known_&&s.frame==previous_.frame&&sword_actions::SameContext(s,previous_)&&s.valid&&s.safe)return Event::None;
        const bool broken=!known_||!s.valid||!s.safe||!std::isfinite(s.time)||s.time<previous_.time-game_clock::kEpsilon||
            s.frame!=previous_.frame+1||!sword_actions::SameContext(s,previous_);
        const bool was=charging_;
        if(broken){Cancel();known_=s.valid;previous_=s;armed_=s.valid&&s.safe&&!s.knife;return was?Event::Cancelled:Event::None;}
        previous_=s;
        if(!s.knife){const bool release=charging_&&game_clock::Reached(s.time-start_,60.0);charging_=charged_=false;armed_=true;return release?Event::Released:was?Event::Cancelled:Event::None;}
        if(!charging_&&armed_&&s.knife_edge){armed_=false;charging_=true;start_=s.time;return Event::Charging;}
        if(charging_&&!charged_&&game_clock::Reached(s.time-start_,60.0)){charged_=true;return Event::Charged;}
        return Event::None;
    }
};
}

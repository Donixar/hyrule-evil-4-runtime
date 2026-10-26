#pragma once
// Small native hit on the Ganado after a Committed parry ("riposte").
//
// Policy only: no process access, hooks, native calls or HP writes. This module
// decides WHEN and ON WHOM (arming on Committed, window, exact-owner admission,
// permille config) and hands the hit to the shared native-hit adapter
// (dll/mortal_draw_damage.h), the single publisher and GetWepDmVal override also
// used by Mortal Draw:
//
//   token = adapter.Issue()                                   // shared counter
//   adapter.NativeHit(target, milli, Kind::Scaled, token)     // publishes once
//   detour: Compose(adapter.Resolve(evaluation), pass-through)
//   riposte counts the damage as applied ONLY when adapter.Applied(token)
//
// Kind::Scaled is final: Compose returns permille*native knife value and skips
// the pass-through (1000 = the raw native knife value). Callers must not scale
// the Resolve result again.
//
// N-2: Scaled needs the adapter's record reader to bind the override to our
// exact cDmgInfo. Without it the riposte refuses (stats.no_record) instead of
// letting a plain native knife hit through; the adapter refuses too (Disabled).
//
// The Ganado's own consumer computes the knife damage (calc 4AD40 / GetWepDmVal
// 1B1A90), applies it through LifeDownSet2 (1B5AF0, difficulty and death
// included) and plays the native knife reaction (table 7F3DB0, weapon 0x10 ->
// 58B90). Native gates mirrored from the knife path (pinned image SHA
// bb59e550...): 1BD5C6/1BD5D4/1BD5E1 HP>0, +32C == 0, +32D == 0; 286DA0 never
// publish while +32C bit0 is set. The adapter publishes knife flags 0, timer
// 0x0A, weapon 0x10 (286DB9).
// Hitpart: the consumer dereferences hitpart+26 (part kind) and +28 (distance),
// so a real hit-part record of THIS actor is mandatory. Port::ResolveHitPart
// defaults to riposte::ResolveHitPart below: the qualified list source
// (dll/native_hitpart.h, executed in tests/native/test_native_hitpart.cpp),
// head/core refused. It is passed to the adapter in HitTarget.
#include "mortal_draw_damage.h"
#include "mortal_draw_target.h"
#include "native_hitpart.h"
#include <cstdint>

namespace link_runtime::parry::riposte {
namespace damage = link_runtime::mortal_draw::damage;

struct Config {
    bool enabled=false;           // INI ParryRiposte, default 0
    unsigned milli=1000;          // INI ParryRiposteMilli: 1000 = one raw native knife hit
    unsigned window_ms=250;       // window to publish after the commit
    // Admission is "the exact owner the committed parry qualified": its actor,
    // id (+100) and type (+101) as seen by the commit observation, re-read here.
    // Second 16-oct decision (every skill on the Ending Blow's set): a Ganado
    // family id (mortal_draw_target.h GanadoFamily) that is not the Merchant
    // (MerchantKind: id 0x18 or type 6). The commit producer is em10 code
    // (AxeAtk/SukiAtk), so the owner already has the cEm10 class. The old type
    // list {2, 6, 0xA, 0xD, 0x16, 0x18} is gone; the hit stays a Scaled ordinary
    // knife hit (no damage limit), so armoured parts just scale it natively.
    // Only used when the commit supplied no id (owner_id 0): any Ganado family id
    // (mortal_draw_target.h GanadoFamily, 0x10..0x20 except the Merchant's 0x18),
    // the same family the sword actions and Z-targeting accept since 16-oct.
};
inline Config MakeConfig(unsigned enabled,unsigned milli){
    Config c;c.milli=milli;
    c.enabled=enabled==1&&milli>=100&&milli<=3000;   // same range as Kind::Scaled
    return c;
}
struct Target {                   // read by the Port from the live actor
    bool valid=false;             // actor still listed by the enemy manager
    uint8_t id=0;                 // actor+100
    uint8_t type=0;               // actor+101
    int16_t hp=0;                 // actor+324
    uint8_t dmg_flags=0;          // actor+32C (cDmgInfo+4)
    uint8_t dmg_timer=0;          // actor+32D (cDmgInfo+5)
};
struct HitPart {uint32_t record=0;float position[3]={0,0,0};float distance=0;};
// Default Port::ResolveHitPart: first ON node of THIS actor's validated list that
// is not head 5 / core 0x25 (native_hitpart::Source::Select). `from` is Link's
// position (publisher XYZ); distance is the node's len, as the knife sweep
// publishes it (0x286DA6). Any refusal returns false (stats.no_hitpart).
// `owner` (id +0x100, type +0x101 of the committed owner) refuses a slot reused
// by another enemy (Identity); the source also requires a live em10 manager slot.
template<class Mem> bool ResolveHitPart(const native_hitpart::Source& source,Mem& mem,uint32_t actor,const float (&from)[3],
                                        HitPart& out,native_hitpart::Verdict* why=nullptr,const native_hitpart::Expect* owner=nullptr){
    out=HitPart{};native_hitpart::Node n;
    const native_hitpart::Verdict v=source.Select(mem,actor,native_hitpart::Rule{},n,owner);
    if(why)*why=v;
    if(v!=native_hitpart::Verdict::Ok)return false;
    out.record=n.address;out.distance=n.len;for(unsigned i=0;i<3;++i)out.position[i]=from[i];
    return true;
}
// HitTarget::identity for a riposte: tag 'P' in the top byte, then type, id and
// the actor. The wiring's Native::facts callback re-reads actor+100/+101 and
// sets same_identity when they still match (and passes target.from to ReadFacts).
inline constexpr uint64_t kIdentityTag=0x50;
inline uint64_t OwnerIdentity(uint32_t actor,uint8_t id,uint8_t type){
    return kIdentityTag<<56|uint64_t(type)<<40|uint64_t(id)<<32|actor;
}
struct Stats {
    unsigned armed=0,published=0,applied=0,unconfirmed=0,expired=0,refused=0,excluded=0,identity=0;
    unsigned no_hitpart=0,no_record=0,busy=0,dead=0,rejected=0,lost=0;
    uint32_t owner_id=0;          // id | type<<8 of the last committed owner (log owner_id=0x..)
    uint8_t expired_flags=0,expired_timer=0;  // last +32C/+32D seen when the window expired
    damage::Status last_status=damage::Status::Disabled;   // last NativeHit answer (log)
    damage::Outcome last_outcome=damage::Outcome::None;    // last confirmed outcome (log)
    uint64_t last_token=0;
};
// Safety release if the adapter never resolves (Tick not wired): cancel our
// override this long after the window closes. The adapter TTL normally ends it first.
inline constexpr unsigned kTrackMs=2000;

// Port contract (game thread):
//  bool ReadTarget(actor, Target&)
//  bool ResolveHitPart(actor, HitPart&)   per-actor native hit test of Link's blade
// Adapter: the one shared damage::Adapter (Mortal Draw + riposte), Tick()ed by
// the wiring once per frame and Resolve()d from the single GetWepDmVal detour.
class Riposte {
    Config config_;
    enum class State {Idle,Armed,Published} state_=State::Idle;
    uint32_t serial_=0,actor_=0;
    uint8_t id_=0,type_=0,seen_flags_=0,seen_timer_=0;
    uint64_t until_=0,token_=0;
    Stats stats_;
    // id 0 (the commit supplied none): only the type is judged here, SameOwner's fallback checks the family.
    static bool Excluded(uint8_t id,uint8_t type){
        return (id&&!mortal_draw::target::GanadoFamily(id))||mortal_draw::target::MerchantKind(id,type);
    }
    bool SameOwner(const Target& t) const {
        if(id_)return t.id==id_&&t.type==type_;
        return mortal_draw::target::GanadoFamily(t.id)&&!Excluded(t.id,t.type);
    }
    bool Eligible(const Target& t) const {return t.valid&&t.hp>0&&t.dmg_flags==0&&t.dmg_timer==0;}
    void Drop(){state_=State::Idle;actor_=0;id_=type_=0;token_=0;}
    void Confirm(damage::Outcome o){
        stats_.last_outcome=o;
        if(o==damage::Outcome::Applied)++stats_.applied;else ++stats_.unconfirmed;
        Drop();
    }
public:
    explicit Riposte(Config c):config_(c){}
    const Stats& stats() const {return stats_;}
    bool Pending() const {return state_!=State::Idle;}
    uint64_t Token() const {return token_;}
    // Committed parry: actor, id (+100) and type (+101) of the owner the commit
    // observation qualified (owner::Observation::owner / actor[0] / actor[1]).
    void OnCommitted(uint32_t serial,uint32_t actor,uint8_t owner_id,uint8_t owner_type,uint64_t now_ms){
        if(!config_.enabled||!serial||!actor||serial==serial_)return;
        serial_=serial;stats_.owner_id=owner_id|uint32_t(owner_type)<<8;
        if(state_!=State::Idle){++stats_.refused;return;}   // one hit in flight at a time
        if(Excluded(owner_id,owner_type)){++stats_.excluded;return;}
        state_=State::Armed;actor_=actor;id_=owner_id;type_=owner_type;until_=now_ms+config_.window_ms;++stats_.armed;
        seen_flags_=seen_timer_=0;token_=0;
    }
    // Damage/grab/death/scene on the player side: withdraw our override only;
    // a record already published stays a native knife hit (adapter contract).
    void Cancel(damage::Adapter& adapter){
        if(state_==State::Published){adapter.Cancel(token_);Confirm(adapter.OutcomeOf(token_));return;}
        Drop();
    }
    template<class Port> void Pump(uint64_t now_ms,Port& port,damage::Adapter& adapter){
        if(state_==State::Idle)return;
        if(state_==State::Published){
            const damage::Outcome o=adapter.OutcomeOf(token_);
            if(o==damage::Outcome::Pending){
                if(now_ms>until_+kTrackMs){adapter.Cancel(token_);++stats_.expired;Confirm(adapter.OutcomeOf(token_));}
                return;
            }
            Confirm(o);   // Applied, or anything else = not our amount (never counted as applied)
            return;
        }
        if(now_ms>until_){++stats_.expired;stats_.expired_flags=seen_flags_;stats_.expired_timer=seen_timer_;Drop();return;}
        // N-2: without the record reader the adapter cannot bind the override to
        // our record; refuse rather than let an unscaled knife hit through.
        if(!adapter.Overrides()){++stats_.no_record;Drop();return;}
        Target t;
        if(!port.ReadTarget(actor_,t))return;                        // retry within the window
        if(t.valid&&!SameOwner(t)){++stats_.identity;Drop();return;} // not the parried owner any more
        seen_flags_=t.dmg_flags;seen_timer_=t.dmg_timer;
        if(!Eligible(t))return;                                      // retry within the window
        HitPart part;
        if(!port.ResolveHitPart(actor_,part)||!part.record){++stats_.no_hitpart;Drop();return;}
        damage::HitTarget h;
        h.identity=OwnerIdentity(actor_,t.id,t.type);h.actor=actor_;
        h.hitpart=part.record;h.distance=part.distance;
        for(unsigned i=0;i<3;++i)h.from[i]=part.position[i];
        if(!token_)token_=adapter.Issue();   // a rejection before the native call keeps the token usable
        const damage::Status s=adapter.NativeHit(h,int(config_.milli),damage::Kind::Scaled,token_);
        stats_.last_status=s;stats_.last_token=token_;
        switch(s){
        case damage::Status::Published:++stats_.published;state_=State::Published;return;
        case damage::Status::Busy:++stats_.busy;return;              // Mortal Draw hit in flight: retry
        case damage::Status::PendingNative:return;                   // native record pending: retry
        case damage::Status::Disabled:++stats_.no_record;break;      // adapter unbound or no record reader
        case damage::Status::IdentityLost:++stats_.identity;break;
        case damage::Status::Dead:++stats_.dead;break;
        case damage::Status::NoHitpart:++stats_.no_hitpart;break;
        case damage::Status::PublishFailed:++stats_.lost;break;
        default:++stats_.rejected;break;                              // BadRequest, TokenUsed, OverCap
        }
        Drop();
    }
};
}  // namespace link_runtime::parry::riposte

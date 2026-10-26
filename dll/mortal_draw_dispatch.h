#pragma once
#include "mortal_draw_native_scope.h"

// Game-thread dispatch adapter. No hooks are installed by this header.
// API callbacks must be qualified engine functions; buffers stay resident.
namespace link_runtime::mortal_draw::dispatch {
using native_scope::Read;using native_scope::Write;
using Begin=void(__thiscall*)(void*);
using End=void(__thiscall*)(void*,uint32_t);
using Set=native_scope::ModelSet;
using Move=int(__thiscall*)(void*);
using Handler=void(__cdecl*)(void*);
struct Api {Begin begin=nullptr;End end=nullptr;Set set=nullptr;Move move=nullptr;};
struct Lease {uint64_t token=0;void* fcv=nullptr;void* seq=nullptr;void* entries=nullptr;};
// NormalMove (12-oct, Mortal Draw from walk/run): normal idle OR the move routine (R0 0) in
// Walk 1 / Back 2 / Run 3 / Turn 4 (cPlayer +0xFC bytes R0 R1 R2 R3; R2/R3 are the routine's
// own phase and motion-continuity flags). Turn180 (5), weapon (6, aim/reload), knife (0xB),
// ladders, jumps, damage and events stay out. Table attribution: mortal_draw_start_gate.h.
enum class StartFrom {NormalIdle,KnifeHoldTrial,NormalMove};
// Footwork R2=1 is steady idle. Native pose end leaves R3 bit 0 set for motion
// continuity; Footwork preserves that bit. Only these two observed idle states
// are qualified; entry/transition phases and other R3 flags remain excluded.
inline bool NormalIdleRoutine(uint32_t r){return r==0x00010000U||r==0x01010000U;}
inline bool NormalMoveRoutine(uint32_t r){
    const uint32_t r0=r&0xFF,r1=(r>>8)&0xFF;
    return NormalIdleRoutine(r)||(r0==0&&r1>=1&&r1<=4);
}
inline bool StartRoutine(StartFrom from,uint32_t r){
    return from==StartFrom::KnifeHoldTrial?r==0x01010B00U:from==StartFrom::NormalMove?NormalMoveRoutine(r):NormalIdleRoutine(r);
}
struct Runtime;
using Decide=void(__cdecl*)(Runtime&,void*);
using Cleanup=bool(__cdecl*)(void*); // True only for a verified native pose clear.
// Residual pose (bug #1, 24-oct): resets the part SRT an abandoned own clip left (trial::RestorePose). Called only
// from Core, on the player's primary motion, right before the native clip that replaced ours is evaluated.
using Restore=bool(__cdecl*)(void*);
enum class Event {Started,Finished,Cancelled,Abandoned};
using Lifecycle=void(__cdecl*)(void*,Event,uint64_t);
// Optional (ocarina, Task 10): called inside Handle when the owned clip reports its end,
// before EndOwned. Returning true means the callback replaced the clip (Replace) and the
// lease continues; null or false keeps the original finish path.
using Proceed=bool(__cdecl*)(Runtime&,unsigned char*);
// Optional (ocarina, Task 10): called on Event-handler frames where decide does not run (no
// owned lease: a native event, door or cutscene owns the player), including the frame a native
// event abandons our lease. Release only: Start and Replace are refused while it runs; it may
// only release its own state (session, freeze, mount).
using Observe=void(__cdecl*)(Runtime&,void*);
struct Runtime {
    bool enabled=false,active=false,busy=false,transitioned=false;
    Api api;void* volatile* player_slot=nullptr;Decide decide=nullptr;
    Lease lease;uintptr_t owner=0;uint64_t last_token=0;
    Cleanup cleanup=nullptr; // Qualified pose reset, only while ownership holds.
    // A lease that left the model without its cleanup (abandoned to a native damage, grab, death or event, or a
    // cleanup that refused): the model and our last clip on it. Kept past Abandon; cleared by a verified cleanup.
    Restore restore=nullptr;uintptr_t residue_owner=0;void* residue_fcv=nullptr;uint64_t residue_token=0;
    unsigned restored=0,restore_failed=0;
    StartFrom return_from=StartFrom::NormalIdle;
    // Immutable while hooks run. Callback may only publish copied event data;
    // it must not reenter dispatch or operate on the player.
    Lifecycle lifecycle=nullptr;void* lifecycle_context=nullptr;
    Proceed proceed=nullptr;
    Observe observe=nullptr;bool observing=false;
    void Notify(Event event,uint64_t token){if(lifecycle&&token){__try{lifecycle(lifecycle_context,event,token);}__except(1){}}}
    unsigned observed=0,started=0,finished=0,cancelled=0,abandoned=0;
    // Only valid within Handle. Never dereference owner, which is an identity.
    unsigned char* current=nullptr;
    bool Current(void* p) const {return p&&player_slot&&*player_slot==p;}
    bool Owns(unsigned char* p,bool cleared=false) const {
        if(!active||!Current(p)||reinterpret_cast<uintptr_t>(p)!=owner)return false;
        const uint32_t r=Read<uint32_t>(p+0xFC);
        return (r==5||r==0x10005)&&(Read<uint32_t>(p+0x468)&2)&&
            !(Read<uint32_t>(p+0x464)&0x100)&&Read<void*>(p+0x2A8)==nullptr&&
            (Read<void*>(p+0x1D8)==lease.fcv||(cleared&&!Read<void*>(p+0x1D8)))&&Read<void*>(p+0x280)==lease.entries;
    }
    void Abandon(){if(active){++abandoned;Notify(Event::Abandoned,lease.token);}active=false;owner=0;lease={};}
    bool Start(const Lease& next,StartFrom from=StartFrom::NormalIdle) {
        if(!enabled||!busy||observing||!current||active||!Current(current)||!api.begin||!api.end||!api.set||!api.move||
           !next.token||next.token<=last_token||!next.fcv||!next.seq||!next.entries||
           !StartRoutine(from,Read<uint32_t>(current+0xFC))||(Read<uint32_t>(current+0x468)&2)||
           (Read<uint32_t>(current+0x464)&0x100))return false;
        // Admission policy belongs at decide; no gameplay eligibility is inferred.
        last_token=next.token;transitioned=true;return_from=from;
        api.begin(current);
        if(!Current(current)||Read<uint32_t>(current+0xFC)!=5||!(Read<uint32_t>(current+0x468)&2))return false;
        api.set(current,next.fcv,0,0,1,next.seq); // root enabled, no hokan
        lease=next;owner=reinterpret_cast<uintptr_t>(current);active=true;
        residue_owner=owner;residue_fcv=next.fcv;residue_token=next.token;   // our clip is on the model from here
        if(!Owns(current)){Abandon();return false;}
        ++started;Notify(Event::Started,lease.token);return true;
    }
    bool EndOwned(unsigned char* p,Event event){
        if(!Owns(p))return false;
        const bool cleared=cleanup&&cleanup(p);
        if(!Owns(p,cleared)){Abandon();return false;}
        if(cleared){residue_owner=0;residue_fcv=nullptr;residue_token=0;}
        Notify(event,lease.token);active=false;owner=0;lease={};
        if(event==Event::Finished)++finished;else ++cancelled;
        api.end(p,0);
        // The knife trial bypassed native knife-down. Re-enter that state so
        // native code hides the blade and restores the equipped weapon.
        if(return_from==StartFrom::KnifeHoldTrial&&Current(p)&&
           Read<uint32_t>(p+0xFC)==0x01000000U&&!(Read<uint32_t>(p+0x468)&2))
            Write<uint32_t>(p+0xFC,0x00030B00U);
        return true;
    }
    // Same token, new clip: only inside a handler, only on the owned lease. The new FCV/SEQ
    // drive the parts they carry; the others keep what the previous clip left.
    bool Replace(void* fcv,void* seq,void* entries){
        if(!busy||observing||!current||!Owns(current)||!fcv||!seq||!entries||!api.set)return false;
        api.set(current,fcv,0,0,1,seq);
        lease.fcv=fcv;lease.seq=seq;lease.entries=entries;residue_fcv=fcv;
        if(!Owns(current)){Abandon();return false;}
        return true;
    }
    bool Cancel(uint64_t token){
        if(!busy||!current||token!=lease.token||!Owns(current))return false;
        return EndOwned(current,Event::Cancelled);
    }
    // Before a non-owned evaluation: once the player's primary motion is a clip that is not ours (the native
    // damage/grab/death/event clip that took the model from our lease), reset what our last clip left, one attempt
    // per lease. Our own clip still on the model (the lease or its abandoned remains) waits; a null clip waits too.
    // Another player model drops the record (the old model is gone).
    void Residue(void* model,void* work){
        if(!residue_owner||!Current(model))return;
        auto* p=static_cast<unsigned char*>(model);
        if(reinterpret_cast<uintptr_t>(model)!=residue_owner){residue_owner=0;residue_fcv=nullptr;residue_token=0;return;}
        if(work!=p+0x1D8)return;
        void* const fcv=Read<void*>(p+0x1D8);
        if(!fcv||fcv==residue_fcv||(active&&fcv==lease.fcv))return;
        residue_owner=0;residue_fcv=nullptr;
        if(restore&&restore(model))++restored;else ++restore_failed;
        residue_token=0;
    }
    void Core(native_scope::Core original,void* model,void* work,void* camera){
        if(!original)return;
        if(busy&&current==model&&Owns(current))
            native_scope::Invoke(original,model,work,camera,current,lease.fcv,lease.entries);
        else {Residue(model,work);original(model,work,camera);}
    }
    void Handle(bool event,Handler original,void* player){
        if(!original)return;
        if(busy){original(player);return;}
        if(!Current(player)){Abandon();original(player);return;}
        auto* p=static_cast<unsigned char*>(player);++observed;
        if(active&&(!event||!Owns(p)))Abandon();
        if(!enabled){
            if(Owns(p)){EndOwned(p,Event::Cancelled);return;}
            original(player);return;
        }
        // Event calls only consume an existing lease. Native event ownership
        // cannot be acquired by merely observing event/Normal routines.
        if(event&&!active){
            if(observe){
                busy=true;observing=true;current=p;
                __try {observe(*this,player);}
                __finally {current=nullptr;observing=false;busy=false;}
            }
            original(player);return;
        }
        busy=true;current=p;transitioned=false;
        bool consumed=false;
        __try {
            const bool was_active=active;
            if(decide)decide(*this,player);
            if(transitioned||(was_active&&!active))consumed=true; // explicit cancellation
            if(Owns(p)) {
                consumed=true;
                if(p[0xFE]==0)p[0xFE]=1; // exact EventNormal initial substate
                const int ended=api.move(p);
                if(!Owns(p))Abandon();
                else if(ended&&!(proceed&&proceed(*this,p)&&Owns(p))){if(Owns(p))EndOwned(p,Event::Finished);}
            }
        } __finally {current=nullptr;busy=false;}
        if(!consumed)original(player);
    }
};
}

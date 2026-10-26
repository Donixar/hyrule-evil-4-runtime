#pragma once
#include "shield_preclamp.h"
#include "shield_owner_context.h"
#include "rear_shield_reader.h"
#include "rear_shield_measure.h"
#include "rear_shield_band.h"
#include <array>

namespace link_runtime::shield {
struct CompletionRecord {
    uint32_t invocation=0,thread=0;
    int wrapper_return=0;
    HealthSample before,after;
    bool comparable=false;
    int hp_delta_observed=0;
};
inline CompletionRecord PairCompletion(uint32_t id,uint32_t thread,int result,const HealthSample& a,const HealthSample& b) {
    CompletionRecord r{id,thread,result,a,b,false,0};
    // Comparable identity within an invocation does not establish exclusive authorship or final damage.
    r.comparable=id!=0&&a.stable&&b.stable&&a.player!=0&&a.global!=0&&
        a.player==b.player&&a.global==b.global&&a.character==b.character&&
        a.max_hp>0&&a.max_hp==b.max_hp&&a.hp>=0&&b.hp>=0&&a.hp<=a.max_hp&&b.hp<=b.max_hp;
    if(r.comparable)r.hp_delta_observed=a.hp-b.hp;
    return r;
}
struct ObserverConfig { bool enabled=false; unsigned events=500,poses=200,pose_ms=200; };
inline ObserverConfig MakeObserverConfig(unsigned enabled,unsigned events,unsigned poses,unsigned pose_ms) {
    return {enabled==1,events>=1&&events<=5000?events:500,
            poses>=1&&poses<=2000?poses:200,pose_ms>=50&&pose_ms<=5000?pose_ms:200};
}
struct Scope {
    Scope* previous=nullptr;
    HitEvent event;
    unsigned depth=0;
    int companion_filter=0;
    uint32_t invocation=0,thread=0;
    HealthSample before;
    Frame classification;
    bool captured=false,caller_shape=false;
    PreClampSample preclamp;
    OwnerContext owner;
    rear_shield::PoseCopy rear_pose;
    rear_shield::Impact rear_impact;
    rear_shield::measure::Route rear_route;
    rear_shield::measure::Packet rear_measure;
    rear_shield::band::Sample rear_band;
    rear_shield::band::Evidence rear_band_evidence;
};
inline bool Matches(const Scope* scope,uint32_t attack,uint32_t current,uint32_t previous,
                    uint32_t classifier_return,const NativeAccess& access) {
    return scope&&scope->depth==1&&access.verified&&access.code.verified&&
        Range(scope->event.player,0x814)&&Range(scope->event.global,0x502C)&&
        scope->event.attack==attack&&scope->event.current==current&&scope->event.previous==previous&&
        classifier_return==uint64_t(access.image_base)+access.code.classifier_return_rva;
}
struct HitRecord {
    HitEvent event; Frame frame; unsigned tick=0; int companion_filter=0;
    bool caller_shape=false,captured=false;
    CompletionRecord completion;
    PreClampSample preclamp;
    OwnerContext owner;
    rear_shield::PoseCopy rear_pose;
    rear_shield::Impact rear_impact;
    rear_shield::measure::Packet rear_measure;
    rear_shield::band::Evidence rear_band;
};
// Structural call evidence is a candidate only. It never establishes ordinary attack semantics.
template<class Memory> bool CallerShape(Memory& memory,const NativeAccess& a,uint32_t caller) {
    if(caller<a.image_base||caller-a.image_base<5||
        !Area(a.regions.data(),a.regions.size(),caller-a.image_base-5,5,true))return false;
    unsigned char call[5]={};if(!memory.Read(caller-5,call,5)||call[0]!=0xE8)return false;
    const int64_t target=int64_t(caller)+static_cast<int32_t>(U32(call+1));
    const int64_t wrapper=int64_t(a.image_base)+a.code.wrapper_rva;
    if(target==wrapper)return true;
    if(target<a.image_base||target-a.image_base>=a.image_size||
        !Area(a.regions.data(),a.regions.size(),static_cast<size_t>(target-a.image_base),5,true))return false;
    unsigned char thunk[5]={};
    return memory.Read(static_cast<uint32_t>(target),thunk,5)&&thunk[0]==0xE9&&
        target+5+static_cast<int32_t>(U32(thunk+1))==wrapper;
}
template<class Memory> bool CaptureHit(Memory& memory,const NativeAccess& a,const Scope* scope,
        uint32_t attack,uint32_t current,uint32_t previous,uint32_t caller,int result,HitRecord& out) {
    out={};
    if(!Complete(a)||!Matches(scope,attack,current,previous,caller,a)||!Range(attack,12)||!Range(current,12)||!Range(previous,12))return false;
    HitEvent e=scope->event;e.classifier_return=caller;e.native_hit=result;
    Frame first{},last{};
    if(!ReadFrame(memory,a,e,first)||!ReadFrame(memory,a,e,last)||!Equal(first,last))return false;
    out.event=e;out.frame=first;out.companion_filter=scope->companion_filter;
    out.caller_shape=CallerShape(memory,a,e.outer_return);return true;
}
// Independent pose sampling uses only current slots. Queued event pointers are never read by the worker.
template<class Memory> bool ReadPoseOnce(Memory& memory,const NativeAccess& a,Frame& f) {
    const uint32_t player_slot=a.image_base+static_cast<uint32_t>(a.code.player_slot_rva);
    const uint32_t global_slot=a.image_base+static_cast<uint32_t>(a.code.global_slot_rva);
    if(!Word(memory,player_slot,f.player)||!Range(f.player,0x814)||
        !Word(memory,global_slot,f.global)||!Range(f.global,0x502C))return false;
    if(!Bytes(memory,a.image_base+static_cast<uint32_t>(a.code.input_rva),f.input)||
        !Bytes(memory,f.player+0x0C,f.matrix)||!Bytes(memory,f.player+0x94,f.position_angles)||
        !Bytes(memory,f.player+0xFC,f.routines)||!Bytes(memory,f.player+0x100,f.kind)||!Bytes(memory,f.player+0x32C,f.damage)||
        !Word(memory,f.global+0x20,f.routine)||!Word(memory,f.global+0x54,f.system)||!Word(memory,f.global+0x170,f.stop)||
        !Word(memory,f.global+0x4FB4,f.health)||!Word(memory,f.global+0x4FC8,f.character)||!Bytes(memory,f.global+0x501C,f.status))return false;
    uint32_t player=0,global=0;
    return Word(memory,player_slot,player)&&player==f.player&&Word(memory,global_slot,global)&&global==f.global;
}
template<class Memory> bool ReadPose(Memory& memory,const NativeAccess& a,Frame& out) {
    out={};if(!Complete(a))return false;
    Frame first{},last{};
    if(!ReadPoseOnce(memory,a,first)||!ReadPoseOnce(memory,a,last)||!Equal(first,last))return false;
    out=first;return true;
}
template<size_t Capacity> class CopyQueue {
    std::array<HitRecord,Capacity> records{}; size_t begin=0,count=0;
public:
    bool Push(const HitRecord& r) { if(count==Capacity)return false;records[(begin+count)%Capacity]=r;++count;return true; }
    bool Pop(HitRecord& r) { if(!count)return false;r=records[begin];begin=(begin+1)%Capacity;--count;return true; }
};
} // namespace link_runtime::shield

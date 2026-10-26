#pragma once
// E1 sword trail log (docs/estela-espada-diseno-2026-10-20.md sections 2, 4 and 6): the blade and the camera on every
// frame of a spin, for the offline renderer (scripts/sword_trail_render.py). Pure: templated memory, no logging,
// no native call, nothing written. The wiring (spin_attack_runtime.h) calls it on the game thread only with
// SwordTrailLog=1 and LogLevel=2.
//
// Cost rule: the full identity (parry_sword_reader.h ReadObservation, SHA-256 of the model resource) runs once at the
// spin start; every later frame re-walks the owners from the slots and re-reads only the matrices (ReadPose, no
// bcrypt). Only the identity's values cross frames, never a pointer that is dereferenced. When the start read fails
// (the sword in hand may still be swapping in) the full read is retried on the next frames, kIdentityAttempts in all.
#include "parry_sword_reader.h"
#include <cmath>
#include <cstdio>

namespace link_runtime::sword_trail_log {
namespace rd=link_runtime::parry::sword::reader;
using link_runtime::parry::sword::Blade;
using link_runtime::parry::sword::Profile;

constexpr unsigned kIdentityAttempts=4;   // the start and up to three later frames
// GameNow() counts 60 Hz frames; the trail core (sword_trail.h) and the log work in game milliseconds.
inline double Ms(double game60){return game60*1000.0/60.0;}

enum class Step {Sampled,Identified,Failed};

class Tracker {
    rd::Identity id_{};uint64_t lease_=0;unsigned attempts_=0,samples_=0,failed_=0;bool identified_=false;
    template<class Memory> bool Identify(Memory& memory,rd::Slots slots,Blade& blade){
        ++attempts_;rd::Observation o;
        if(!rd::ReadObservation(memory,slots,o))return false;
        id_=rd::IdentityOf(o);identified_=true;blade=o.blade;return true;
    }
public:
    uint64_t Lease() const {return lease_;}
    bool Identified() const {return identified_;}
    Profile SwordProfile() const {return id_.profile;}
    unsigned Attempts() const {return attempts_;}
    unsigned Samples() const {return samples_;}
    unsigned Failed() const {return failed_;}
    // Spin start: the full read, counted as the first sample. false = not identified yet (blade zeroed).
    template<class Memory> bool Begin(Memory& memory,rd::Slots slots,uint64_t lease,Blade& blade){
        *this={};lease_=lease;blade={};++samples_;
        const bool ok=lease&&Identify(memory,slots,blade);
        if(!ok)++failed_;
        return ok;
    }
    // Every later frame of the spin. Failed zeroes the blade (the log writes ok=0 and the renderer cuts the ribbon).
    template<class Memory> Step Sample(Memory& memory,rd::Slots slots,Blade& blade){
        blade={};
        if(!lease_)return Step::Failed;
        ++samples_;
        if(!identified_){
            if(attempts_<kIdentityAttempts&&Identify(memory,slots,blade))return Step::Identified;
            ++failed_;return Step::Failed;
        }
        rd::Observation o;
        if(!rd::ReadPose(memory,slots,id_,o)){++failed_;return Step::Failed;}
        blade=o.blade;return Step::Sampled;
    }
    void End(){lease_=0;}   // counters stay readable for the end line
};

// The camera as ztarget_reticle_runtime.h reads it: view 3x4 at global+0xA4, projection 4x4 at +0xD8, fovy at
// +0x134, post 4x4 at base+0xCED730. Re-read every frame from the global slot.
struct Camera {float view[12],proj[16],post[16],fovy;};
template<class Memory> bool ReadCamera(Memory& memory,uint32_t global_slot,uint32_t base,Camera& out){
    Camera c{};uint32_t global=0;
    if(!memory.Read(global_slot,&global,4)||!rd::Pointer(global)||!memory.Read(global+0xA4,c.view,sizeof(c.view))||
       !memory.Read(global+0xD8,c.proj,sizeof(c.proj))||!memory.Read(global+0x134,&c.fovy,4)||
       !memory.Read(base+0xCED730,c.post,sizeof(c.post)))return false;
    for(float v:c.view)if(!std::isfinite(v))return false;
    for(float v:c.proj)if(!std::isfinite(v))return false;
    for(float v:c.post)if(!std::isfinite(v))return false;
    if(!std::isfinite(c.fovy))return false;
    out=c;return true;
}

inline const char* ProfileName(Profile p){
    return p==Profile::Ordon?"ordon":p==Profile::MasterSword?"master":p==Profile::CampaignPl00?"campaign_pl00":"unknown";
}
// Log lines (the renderer's contract). Each returns the snprintf length (<= 0 or >= size: not written whole).
inline int FormatBegin(char* out,size_t size,const char* kind,bool great,bool identity,uint64_t lease,Profile profile){
    return std::snprintf(out,size,"sword_trail begin kind=%s great=%d identity=%d lease=%llu profile=%s",kind,great?1:0,
        identity?1:0,static_cast<unsigned long long>(lease),ProfileName(profile));
}
inline int FormatSample(char* out,size_t size,double t_ms,const Blade& b,bool ok){
    return std::snprintf(out,size,"sword_trail sample t=%.3f base=%f,%f,%f tip=%f,%f,%f ok=%d",t_ms,b.base.x,b.base.y,b.base.z,
        b.tip.x,b.tip.y,b.tip.z,ok?1:0);
}
inline int FormatCamera(char* out,size_t size,const Camera& c){
    int n=std::snprintf(out,size,"sword_trail view=");
    auto list=[&](const float* v,unsigned count,const char* next){
        for(unsigned i=0;i<count&&n>0&&size_t(n)<size;++i)
            n+=std::snprintf(out+n,size-n,i+1<count?"%.7g,":"%.7g",double(v[i]));
        if(n>0&&size_t(n)<size)n+=std::snprintf(out+n,size-n,"%s",next);
    };
    list(c.view,12," proj=");list(c.proj,16," post=");list(c.post,16," fovy=");
    if(n>0&&size_t(n)<size)n+=std::snprintf(out+n,size-n,"%.4f",double(c.fovy));
    return n;
}
inline int FormatEnd(char* out,size_t size,const char* reason,unsigned samples,unsigned failed){
    return std::snprintf(out,size,"sword_trail end reason=%s samples=%u failed=%u",reason,samples,failed);
}
} // namespace link_runtime::sword_trail_log

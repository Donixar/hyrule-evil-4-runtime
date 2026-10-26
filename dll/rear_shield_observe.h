#pragma once
#include "shield_observer.h"
// Worker-only interpretation of OWNED copies. Costume is merely a model candidate;
// neither root yaw nor this trajectory supplies bone-2 shield contact or its ordering.
// No writer, resolver, audio publisher or active-damage API exists in this module.
namespace link_runtime::rear_shield {
enum class Model {Unknown,OrdonCandidate,HylianCandidate};
struct Diagnostic {
    Model model=Model::Unknown;bool direction=false,rear=false,final_known=false,active=false;
    double source_dot=0,travel=0;int damage=0,half_preview=0;uint32_t invocation=0,thread=0,producer=0;
    const char* final_reason="no_native_hit";
    const char* direction_reason="capture_or_caller_unqualified";
};
inline Diagnostic Inspect(const shield::HitRecord& h,uint32_t base){
    Diagnostic d;d.invocation=h.completion.invocation;d.thread=h.completion.thread;
    d.producer=h.event.outer_return>=base?h.event.outer_return-base:0xFFFFFFFFU;
    if((h.frame.character&0xFF)==0){
        const unsigned costume=(h.frame.character>>8)&0xFF;
        d.model=costume==0?Model::OrdonCandidate:(costume==1||costume==3)?Model::HylianCandidate:Model::Unknown;
    }
    const auto& p=h.preclamp;
    d.final_known=h.captured&&h.event.native_hit>0&&p.valid&&!p.rejected&&p.captures==1&&p.damage>=0;
    if(h.captured&&h.event.native_hit>0)d.final_reason=p.captures==0?"no_preclamp":p.rejected?"rejected_provenance":
        p.captures!=1?"ambiguous_calls":!p.valid?"invalid_sample":p.damage<0?"negative_damage":"captured";
    if(d.final_known){d.damage=p.damage;d.half_preview=p.damage/2+p.damage%2;}
    if(!h.captured||!h.caller_shape)return d;
    const auto& f=h.frame;const auto current=shield::Vector(f.current.data()),old=shield::Vector(f.previous.data());
    const auto angles=shield::Vector(f.position_angles.data()+12);
    const shield::Vec forward{shield::Float(f.matrix.data()+8),shield::Float(f.matrix.data()+24),shield::Float(f.matrix.data()+40)};
    if(!shield::Finite(current)||!shield::Finite(old)){d.direction_reason="nonfinite_endpoints";return d;}
    const double dx=double(current.x)-old.x,dz=double(current.z)-old.z,travel2=dx*dx+dz*dz;
    if(!std::isfinite(travel2)||travel2<=1e-6){d.direction_reason="zero_or_invalid_travel";return d;}
    // Legacy field name: this is endpoint separation, NOT proven temporal
    // travel. Axe/Suki can supply a same-frame spatial base as "previous".
    // measure::Packet carries independently observed same-sample history.
    d.travel=std::sqrt(travel2); // measured independently of root-basis qualification
    if(!shield::FlatYawBasis(f.matrix.data(),angles,forward)){d.direction_reason="flat_yaw_basis_unqualified";return d;}
    const double f2=double(forward.x)*forward.x+double(forward.z)*forward.z;
    if(f2<=0){d.direction_reason="invalid_forward";return d;}
    const double source_dot_unscaled=-(forward.x*dx+forward.z*dz);
    d.source_dot=source_dot_unscaled/std::sqrt(f2*travel2);
    d.direction=std::isfinite(d.source_dot)&&d.source_dot>=-1.000001&&d.source_dot<=1.000001;
    d.direction_reason=d.direction?"captured":"invalid_dot";
    d.rear=d.direction&&source_dot_unscaled<0&&2*source_dot_unscaled*source_dot_unscaled>=f2*travel2;
    return d;
}
inline const char* Name(Model m){return m==Model::OrdonCandidate?"pl00_candidate":m==Model::HylianCandidate?"pl08_candidate":"unknown";}
class Reporter {
    struct Key{uint32_t invocation=0,thread=0;};Key recent[64]{};unsigned next=0;
public:
    bool Take(const Diagnostic& d){
        if(!d.invocation||!d.thread)return false;
        for(const auto& k:recent)if(k.invocation==d.invocation&&k.thread==d.thread)return false;
        recent[next++%64]={d.invocation,d.thread};return true;
    }
};
}

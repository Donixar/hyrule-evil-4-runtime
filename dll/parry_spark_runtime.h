#pragma once
#include "parry_owner_runtime.h"
#include "parry_spark_resources.h"
#include "parry_spark_fingerprint.h"
#include <cmath>
namespace link_runtime::parry::spark {
struct Code {bool verified=false;uint32_t base=0;};
inline Code FindCode(const unsigned char* image,const shield::NativeAccess& a){
    if(!a.verified||!shield::Area(a.regions.data(),a.regions.size(),0x856ee0,4,false)||
       !fingerprint::Matches(image,a))return {};
    return {true,a.image_base};
}
using Emit=void(__cdecl*)(void*,int,const float*,const float*,void*,unsigned,unsigned,unsigned,unsigned,unsigned,void*);
inline __declspec(thread) bool emitting=false;
// Cosmetic-only call, synchronous after a confirmed defense. It never changes
// defense outcome and retains no actor/resource/stack pointers between frames.
inline unsigned EmitCurrent(const Code& code,const Candidate& candidate){
    if(!code.verified||emitting||!owner::detail::Current(candidate).wrapper)return 0;
    const DWORD error=GetLastError();unsigned submitted=0;
    __declspec(align(16)) unsigned char fp[512];const unsigned mxcsr=0x1f80;
    __asm {fxsave fp
        fninit
        ldmxcsr mxcsr}
    emitting=true;
    __try {
        __try {
            owner::Memory memory;Resources resources;float pos[3]{},previous[3]{},rot[3]{};
            bool valid=memory.Read(candidate.current,pos,sizeof(pos))&&memory.Read(candidate.old,previous,sizeof(previous));
            for(unsigned i=0;i<3&&valid;++i)valid=std::isfinite(pos[i])&&std::isfinite(previous[i])&&std::fabs(pos[i])<1.e7f&&std::fabs(previous[i])<1.e7f;
            if(valid&&ResourcesNow(memory,code.base+0x856ee0,resources)){
                const float x=pos[0]-previous[0],y=pos[1]-previous[1],z=pos[2]-previous[2];
                rot[0]=-std::atan2(y,std::sqrt(x*x+z*z));rot[1]=std::atan2(x,z);
                const auto emit=reinterpret_cast<Emit>(code.base+0x215cc0);
                for(unsigned i=0;i<2;++i){
                    emit(nullptr,-1,pos,rot,reinterpret_cast<void*>(resources.values[i]),0,0,0,0,0x1f+i,nullptr);
                    ++submitted;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    } __finally {emitting=false;__asm {fxrstor fp} SetLastError(error);}
    return submitted; // Submission only: native allocation/render success is not claimed.
}
// Tier-2 spark point: the point of Link's blade segment [base,tip] closest to the attack sample,
// when it lies within radius+200 of the sample; otherwise the sample itself. Pure; false on
// non-finite input (the caller then keeps the sample).
inline bool BladePoint(const double* base,const double* tip,const float* sample,float radius,float* out){
    if(!base||!tip||!sample||!out||!std::isfinite(radius))return false;
    double u[3],w[3],uu=0,uw=0;
    for(unsigned i=0;i<3;++i){
        if(!std::isfinite(base[i])||!std::isfinite(tip[i])||!std::isfinite(sample[i]))return false;
        u[i]=tip[i]-base[i];w[i]=double(sample[i])-base[i];uu+=u[i]*u[i];uw+=u[i]*w[i];
    }
    const double t=uu>0?(uw<0?0:uw>uu?1:uw/uu):0;
    double point[3],d2=0;
    for(unsigned i=0;i<3;++i){point[i]=base[i]+t*u[i];const double d=point[i]-sample[i];d2+=d*d;}
    const double limit=double(radius)+200.;
    const bool close=radius>=0&&d2<=limit*limit;
    for(unsigned i=0;i<3;++i)out[i]=close?float(point[i]):sample[i];
    return close;
}
// EmitCurrent with the position given: same pinned emitter, resources and validation, but the
// generic structural invocation (tier 2 has no em10 producer frame). Direction from `from` to
// `pos`; when they coincide it falls back to the candidate's old->current like EmitCurrent.
inline unsigned EmitAt(const Code& code,const Candidate& candidate,const float* at,const float* from){
    if(!code.verified||emitting||!at||!from||!CurrentInvocation(candidate).wrapper)return 0;
    const DWORD error=GetLastError();unsigned submitted=0;
    __declspec(align(16)) unsigned char fp[512];const unsigned mxcsr=0x1f80;
    __asm {fxsave fp
        fninit
        ldmxcsr mxcsr}
    emitting=true;
    __try {
        __try {
            owner::Memory memory;Resources resources;float pos[3]{},source[3]{},rot[3]{};
            bool valid=true;
            for(unsigned i=0;i<3;++i){pos[i]=at[i];source[i]=from[i];}
            float x=pos[0]-source[0],y=pos[1]-source[1],z=pos[2]-source[2];
            if(x*x+y*y+z*z<1.e-4f){
                float current[3]{},previous[3]{};
                valid=memory.Read(candidate.current,current,sizeof(current))&&memory.Read(candidate.old,previous,sizeof(previous));
                if(valid){x=current[0]-previous[0];y=current[1]-previous[1];z=current[2]-previous[2];}
            }
            for(unsigned i=0;i<3&&valid;++i)valid=std::isfinite(pos[i])&&std::fabs(pos[i])<1.e7f;
            valid=valid&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);
            if(valid&&ResourcesNow(memory,code.base+0x856ee0,resources)){
                rot[0]=-std::atan2(y,std::sqrt(x*x+z*z));rot[1]=std::atan2(x,z);
                const auto emit=reinterpret_cast<Emit>(code.base+0x215cc0);
                for(unsigned i=0;i<2;++i){
                    emit(nullptr,-1,pos,rot,reinterpret_cast<void*>(resources.values[i]),0,0,0,0,0x1f+i,nullptr);
                    ++submitted;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    } __finally {emitting=false;__asm {fxrstor fp} SetLastError(error);}
    return submitted;
}
// Great Spin (great_spin.h, 18-oct): the same pinned emitter and resources at given positions and rotations,
// outside a parry invocation (game thread, the player's dispatch at the spin's strike). Cosmetic only: one
// resource lookup, then CORE 0x1F and 0x20 per point; non-finite points are skipped. Submissions, not renders.
inline unsigned EmitFree(const Code& code,const float (*at)[3],const float (*rot)[3],unsigned count){
    if(!code.verified||emitting||!at||!rot||!count||count>64)return 0;
    const DWORD error=GetLastError();unsigned submitted=0;
    __declspec(align(16)) unsigned char fp[512];const unsigned mxcsr=0x1f80;
    __asm {fxsave fp
        fninit
        ldmxcsr mxcsr}
    emitting=true;
    __try {
        __try {
            owner::Memory memory;Resources resources;
            if(ResourcesNow(memory,code.base+0x856ee0,resources)){
                const auto emit=reinterpret_cast<Emit>(code.base+0x215cc0);
                for(unsigned k=0;k<count;++k){
                    float pos[3]{},r[3]{};bool valid=true;
                    for(unsigned i=0;i<3;++i){pos[i]=at[k][i];r[i]=rot[k][i];
                        valid=valid&&std::isfinite(pos[i])&&std::fabs(pos[i])<1.e7f&&std::isfinite(r[i]);}
                    if(!valid)continue;
                    for(unsigned i=0;i<2;++i){
                        emit(nullptr,-1,pos,r,reinterpret_cast<void*>(resources.values[i]),0,0,0,0,0x1f+i,nullptr);
                        ++submitted;
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    } __finally {emitting=false;__asm {fxrstor fp} SetLastError(error);}
    return submitted;
}
}

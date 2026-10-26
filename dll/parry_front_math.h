#pragma once
#include <windows.h>
#include <cmath>
#include <cstring>

// Pure math seam; no damage decision or startup wiring. The eventual runtime
// adapter must supply a verified native RotVector, never a caller-ready flag.
namespace link_runtime::parry::front_math {
using Rotate=void(__cdecl*)(const float*,const float*,float*);
struct Result {bool frontal=false,short_direction=false;float dot=0;};
namespace detail {
inline void Subtract(const float* a,const float* b,float* destination){
    __asm {
        mov eax,a
        mov ecx,b
        mov edx,destination
        fld dword ptr [eax]
        fsub dword ptr [ecx]
        fstp dword ptr [edx]
        fld dword ptr [eax+4]
        fsub dword ptr [ecx+4]
        fstp dword ptr [edx+4]
        fld dword ptr [eax+8]
        fsub dword ptr [ecx+8]
        fstp dword ptr [edx+8]
    }
}
inline bool Short(const float* sample,const float* base){
    const double threshold=10000.;unsigned short status=0;
    __asm {
        mov eax,sample
        mov ecx,base
        fld dword ptr [eax+8]
        fsub dword ptr [ecx+8]
        fld dword ptr [eax]
        fsub dword ptr [ecx]
        fmul st(0),st(0)
        fld st(1)
        fmulp st(2),st(0)
        faddp st(1),st(0)
        fcomp threshold
        fnstsw status
    }
    return (status&0x4500)==0x0100;
}
inline float Dot(const float* a,const float* b){
    float result=0;
    __asm {
        mov eax,a
        mov ecx,b
        fld dword ptr [eax+4]
        fmul dword ptr [ecx+4]
        fld dword ptr [eax]
        fmul dword ptr [ecx]
        faddp st(1),st(0)
        fld dword ptr [eax+8]
        fmul dword ptr [ecx+8]
        faddp st(1),st(0)
        fstp result
    }
    return result;
}
inline bool Finite(const float* values){return values&&std::isfinite(values[0])&&std::isfinite(values[1])&&std::isfinite(values[2]);}
}
// forward (optional, 3 floats): the native RotVector((0,0,1),angles) used for the dot, for the
// tier-2 forward half-plane. Written only on success.
inline bool Evaluate(Rotate rotate,const float* sample,const float* base,const float* position,const float* angles,Result& out,float* forward_out=nullptr){
    const DWORD error=GetLastError();
    __declspec(align(16)) unsigned char fp[512];const unsigned mxcsr=0x1f80;
    __asm {fxsave fp
        fninit
        ldmxcsr mxcsr}
    bool ok=false;out={};
    __try {
        __try {
            if(rotate&&detail::Finite(sample)&&detail::Finite(base)&&detail::Finite(position)&&detail::Finite(angles)){
                Result r;float direction[3]{},forward[3]={0,0,1};
                r.short_direction=detail::Short(sample,base);
                detail::Subtract(r.short_direction?position:sample,r.short_direction?sample:base,direction);
                rotate(forward,angles,forward);
                if(detail::Finite(direction)&&detail::Finite(forward)){
                    r.dot=detail::Dot(forward,direction);
                    if(std::isfinite(r.dot)){r.frontal=r.dot<0;out=r;ok=true;if(forward_out)std::memcpy(forward_out,forward,sizeof(forward));}
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;out={};}
    } __finally {__asm {fxrstor fp}
        SetLastError(error);}
    return ok;
}
}

#pragma once
#include "parry_owner_runtime.h"
#include "parry_front_math.h"
#include "parry_front_fingerprint.h"

namespace link_runtime::parry::front_native {
struct Code {bool verified=false;uint32_t image_base=0;};
inline Code FindCode(const unsigned char* image,size_t size,const shield::NativeAccess& access){
    if(!image||size!=access.image_size||!shield::Complete(access)||!detail::Match(image,access)||
       !shield::Area(access.regions.data(),access.regions.size(),0x130DB88,4,false))return {};
    return {true,access.image_base};
}
// travel: front_math::Evaluate's travel mode (the bolt line); false = the native classifier formula.
inline bool Evaluate(const Code& code,const shield::NativeAccess& access,const owner::Observation& owner,
                     const float* position,const float* angles,front_math::Result& result,float* forward=nullptr,bool travel=false){
    result={};
    if(!code.verified||code.image_base!=access.image_base)return false;
    owner::Memory memory;uint32_t before=0,after=0;
    if(!shield::Word(memory,code.image_base+0x130DB88,before)||before>1)return false;
    float sample[3],base[3];std::memcpy(sample,owner.current.data(),12);std::memcpy(base,owner.previous.data(),12);
    const auto rotate=reinterpret_cast<front_math::Rotate>(code.image_base+0x2D06E0);
    front_math::Result observed;
    if(!front_math::Evaluate(rotate,sample,base,position,angles,observed,forward,travel)||
       !shield::Word(memory,code.image_base+0x130DB88,after)||before!=after)return false;
    result=observed;return true;
}
}

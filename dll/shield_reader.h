#pragma once
#include "link_runtime_shield.h"
#include "shield_finder.h"
#include <array>

// Prepared trace snapshot only. Memory exposes Read; this contract cannot write or hook a game.
namespace link_runtime::shield {
struct NativeAccess {
    bool verified=false,ordinary_routes_verified=false;
    uint32_t image_base=0,image_size=0;
    NativeCode code;
    std::vector<Region> regions; // Verified PE sections, retained so a declared caller cannot point into data.
    std::vector<uint32_t> ordinary_return_rvas; // Empty until root observes an ordinary incoming attack.
};
struct HitEvent {
    uint32_t player=0,global=0,attack=0,current=0,previous=0;
    uint32_t classifier_return=0,outer_return=0;
    int native_hit=0;
};
struct Observation {
    Snapshot decision;
    uint32_t player=0,global=0,routines=0;
    uint64_t keys_on=0,keys_trg=0;
    int hp=0,max_hp=0,native_hit=0;
};
struct HealthSample {
    bool stable=false;
    uint32_t player=0,global=0,character=0,routine=0;
    int hp=0,max_hp=0;
};
inline bool Range(uint32_t address,size_t n) { return address>=0x10000U&&n&&uint64_t(address)+n<0x7FFF0000U; }
inline bool Complete(const NativeAccess& a) {
    const auto& c=a.code;
    if(!a.verified||!c.verified||!Range(a.image_base,a.image_size)||a.regions.empty()||a.regions.size()>=96)return false;
    for(size_t i=0;i<a.regions.size();++i) {
        const auto& r=a.regions[i];
        if(r.begin>=a.image_size||r.span>a.image_size-r.begin)return false;
        for(size_t j=0;j<i;++j) {
            const auto& other=a.regions[j];
            if(r.span&&other.span&&r.begin<other.begin+other.span&&other.begin<r.begin+r.span)return false;
        }
    }
    for(size_t at:{c.classifier_rva,c.wrapper_rva,c.classifier_return_rva,c.helper_rva,c.helper_return_rva,
                  c.key_check_rva,c.knife_dispatch_rva,c.knife_move_rva,c.knife_set_rva,c.knife_ready_rva,c.knife_fire_rva,c.knife_down_rva})
        if(!at||!Area(a.regions.data(),a.regions.size(),at,1,true))return false;
    for(size_t at:{c.player_slot_rva,c.global_slot_rva,c.input_rva})
        if(!at||!Area(a.regions.data(),a.regions.size(),at,16,false))return false;
    return c.classifier_return_rva==c.wrapper_rva+25&&c.helper_return_rva==c.classifier_rva+0x96;
}
template<class Memory> bool Word(Memory& memory,uint32_t at,uint32_t& out) {
    unsigned char bytes[4]={};if(!Range(at,4)||!memory.Read(at,bytes,4))return false;out=U32(bytes);return true;
}

// Completion diagnostics only: HP0 and a changed routine may be the result of a lethal native hit.
// Sample only attributed identity/health fields, without requiring contact, posture or Gameplay().
template<class Memory> bool ReadHealthSample(Memory& memory,const NativeAccess& a,const HitEvent& expected,HealthSample& out) {
    out={};
    if(!Complete(a)||!Range(expected.player,0x814)||!Range(expected.global,0x502C))return false;
    const uint32_t player_slot=a.image_base+static_cast<uint32_t>(a.code.player_slot_rva);
    const uint32_t global_slot=a.image_base+static_cast<uint32_t>(a.code.global_slot_rva);
    const auto read=[&](HealthSample& sample) {
        uint32_t health=0,player=0,global=0;
        if(!Word(memory,player_slot,sample.player)||sample.player!=expected.player||
            !Word(memory,global_slot,sample.global)||sample.global!=expected.global||
            !Word(memory,sample.global+0x20,sample.routine)||
            !Word(memory,sample.global+0x4FB4,health)||!Word(memory,sample.global+0x4FC8,sample.character)||
            !Word(memory,player_slot,player)||player!=sample.player||
            !Word(memory,global_slot,global)||global!=sample.global)return false;
        sample.hp=static_cast<int16_t>(health&0xFFFF);sample.max_hp=static_cast<int16_t>(health>>16);
        return sample.max_hp>0&&sample.hp>=0&&sample.hp<=sample.max_hp;
    };
    HealthSample first{},last{};
    if(!read(first)||!read(last)||first.player!=last.player||first.global!=last.global||
        first.character!=last.character||first.routine!=last.routine||first.hp!=last.hp||first.max_hp!=last.max_hp)return false;
    first.stable=true;out=first;return true;
}
template<size_t N,class Memory> bool Bytes(Memory& memory,uint32_t at,std::array<unsigned char,N>& out) {
    return Range(at,N)&&memory.Read(at,out.data(),N);
}
struct Frame {
    uint32_t player=0,global=0,routine=0,system=0,stop=0,health=0,character=0;
    std::array<unsigned char,16> input{},status{};
    std::array<unsigned char,48> matrix{};
    std::array<unsigned char,24> position_angles{};
    std::array<unsigned char,4> routines{};
    std::array<unsigned char,2> kind{},damage{};
    std::array<unsigned char,12> attack{},current{},previous{};
};
inline bool Equal(const Frame& a,const Frame& b) {
    return a.player==b.player&&a.global==b.global&&a.routine==b.routine&&a.system==b.system&&a.stop==b.stop&&
        a.health==b.health&&a.character==b.character&&a.input==b.input&&a.status==b.status&&a.matrix==b.matrix&&
        a.position_angles==b.position_angles&&a.routines==b.routines&&a.kind==b.kind&&a.damage==b.damage&&
        a.attack==b.attack&&a.current==b.current&&a.previous==b.previous;
}
template<class Memory> bool ReadFrame(Memory& memory,const NativeAccess& a,const HitEvent& e,Frame& f) {
    const uint32_t player_slot=a.image_base+static_cast<uint32_t>(a.code.player_slot_rva);
    const uint32_t global_slot=a.image_base+static_cast<uint32_t>(a.code.global_slot_rva);
    if(!Word(memory,player_slot,f.player)||f.player!=e.player||!Range(f.player,0x814)||
        !Word(memory,global_slot,f.global)||f.global!=e.global||!Range(f.global,0x502C))return false;
    if(!Bytes(memory,a.image_base+static_cast<uint32_t>(a.code.input_rva),f.input)||
        !Bytes(memory,f.player+0x0C,f.matrix)||!Bytes(memory,f.player+0x94,f.position_angles)||
        !Bytes(memory,f.player+0xFC,f.routines)||!Bytes(memory,f.player+0x100,f.kind)||!Bytes(memory,f.player+0x32C,f.damage)||
        !Word(memory,f.global+0x20,f.routine)||!Word(memory,f.global+0x54,f.system)||!Word(memory,f.global+0x170,f.stop)||
        !Word(memory,f.global+0x4FB4,f.health)||!Word(memory,f.global+0x4FC8,f.character)||!Bytes(memory,f.global+0x501C,f.status)||
        !Bytes(memory,e.attack,f.attack)||!Bytes(memory,e.current,f.current)||!Bytes(memory,e.previous,f.previous))return false;
    uint32_t player=0,global=0;
    return Word(memory,player_slot,player)&&player==f.player&&Word(memory,global_slot,global)&&global==f.global;
}
inline bool Gameplay(const Frame& f) {
    const int hp=static_cast<int16_t>(f.health&0xFFFF),maximum=static_cast<int16_t>(f.health>>16);
    // Same verified UHD gates as the native health QA; latched ROOMJUMP remains allowed.
    constexpr uint32_t system_block=0x20000000U|0x04000000U|0x02000000U|0x00200000U|0x00100000U|
        0x00080000U|0x00008000U|0x00000400U|0x00000100U;
    constexpr uint32_t stop_block=0x80000000U|0x10000000U|0x02000000U;
    constexpr uint32_t status0_block=0x10000000U|0x08000000U|0x04000000U|0x00100000U|0x00040000U|0x00001000U|0x80U;
    constexpr uint32_t status1_block=0x10000000U|2U;
    constexpr uint32_t status2_block=0x04000000U|0x00080000U|0x00040000U|0x00008000U;
    constexpr uint32_t status3_block=0x00008000U|0x00004000U|0x00002000U|0x00001000U|0x00000800U;
    return hp>0&&hp<=maximum&&(f.routine&0xFF)==3&&!(f.system&system_block)&&!(f.stop&stop_block)&&
        !(U32(f.status.data())&status0_block)&&!(U32(f.status.data()+4)&status1_block)&&
        !(U32(f.status.data()+8)&status2_block)&&!(U32(f.status.data()+12)&status3_block);
}
inline float Float(const unsigned char* p) { float f=0;std::memcpy(&f,p,4);return f; }
inline Vec Vector(const unsigned char* p) { return {Float(p),Float(p+4),Float(p+8)}; }
inline bool FlatYawBasis(const unsigned char* matrix,const Vec& angles,const Vec& forward) {
    const Vec right{Float(matrix),Float(matrix+16),Float(matrix+32)};
    const Vec up{Float(matrix+4),Float(matrix+20),Float(matrix+36)};
    if(!Finite(angles)||!Finite(right)||!Finite(up)||!Finite(forward)||
        std::fabs(angles.x)>1e-6f||std::fabs(angles.z)>1e-6f||up.y<=0)return false;
    const double f=std::sqrt(double(forward.x)*forward.x+double(forward.z)*forward.z);
    const double r=std::sqrt(double(right.x)*right.x+double(right.z)*right.z);
    if(f<=0||r<=0)return false;
    // Positive per-axis scale is allowed. Tilt, roll, shear and reflection must not look like a flat yaw.
    const double sine=std::sin(double(angles.y)),cosine=std::cos(double(angles.y));
    return std::fabs(double(forward.y))<=f*0.001&&std::fabs(double(right.y))<=r*0.001&&
        std::fabs(double(up.x))<=double(up.y)*0.001&&std::fabs(double(up.z))<=double(up.y)*0.001&&
        std::fabs(double(forward.x)/f-sine)<0.001&&std::fabs(double(forward.z)/f-cosine)<0.001&&
        std::fabs(double(right.x)/r-cosine)<0.001&&std::fabs(double(right.z)/r+sine)<0.001;
}
template<class Memory> bool Ordinary(Memory& memory,const NativeAccess& a,const HitEvent& e) {
    if(!a.ordinary_routes_verified||a.ordinary_return_rvas.empty()||a.ordinary_return_rvas.size()>128||e.outer_return<a.image_base)return false;
    const size_t rva=e.outer_return-a.image_base;unsigned count=0;
    for(uint32_t value:a.ordinary_return_rvas)if(value==rva)++count;
    if(count!=1||rva<5||!Area(a.regions.data(),a.regions.size(),rva-5,5,true))return false;
    unsigned char call[5]={};if(!memory.Read(e.outer_return-5,call,5)||call[0]!=0xE8)return false;
    const int64_t target=int64_t(e.outer_return)+static_cast<int32_t>(U32(call+1));
    const uint64_t wrapper=uint64_t(a.image_base)+a.code.wrapper_rva;
    if(target==static_cast<int64_t>(wrapper))return true;
    if(target<a.image_base||uint64_t(target)-a.image_base>=a.image_size||
        !Area(a.regions.data(),a.regions.size(),static_cast<size_t>(target-a.image_base),5,true))return false;
    unsigned char thunk[5]={};
    return memory.Read(static_cast<uint32_t>(target),thunk,5)&&thunk[0]==0xE9&&
        target+5+static_cast<int32_t>(U32(thunk+1))==static_cast<int64_t>(wrapper);
}
inline Observation DescribeFrame(const Frame& first,const HitEvent& e,bool ordinary=false) {
    Observation next;next.player=first.player;next.global=first.global;next.routines=U32(first.routines.data());
    next.keys_on=uint64_t(U32(first.input.data()))|(uint64_t(U32(first.input.data()+4))<<32);
    next.keys_trg=uint64_t(U32(first.input.data()+8))|(uint64_t(U32(first.input.data()+12))<<32);
    next.hp=static_cast<int16_t>(first.health&0xFFFF);next.max_hp=static_cast<int16_t>(first.health>>16);next.native_hit=e.native_hit;
    auto& s=next.decision;s.stable=true;s.gameplay=Gameplay(first);s.leon=(first.character&0xFF)==0&&first.kind[0]==0;
    s.caller_verified=s.player_match=true;s.positive_collision=e.native_hit>=1&&e.native_hit<=6;
    s.posture_verified=true;
    constexpr uint64_t attack_keys=0x80U|0x10U|0x1000000000000ULL|0x2000000000000ULL|0x10000000000ULL|0x40000000000ULL;
    // Native FE==1 is hold; begin with FF==1 idle only. Drawing/turning/slash/lowering remain excluded until pose QA.
    s.ready=next.routines==0x01010B00U&&(next.keys_on&0x800U)&&!((next.keys_on|next.keys_trg)&attack_keys)&&
        first.damage[0]==0&&first.damage[1]==0;
    s.attack_flags=U16(first.attack.data()+10);s.power=U16(first.attack.data()+8);
    const Vec player=Vector(first.position_angles.data()),angles=Vector(first.position_angles.data()+12);
    const Vec forward{Float(first.matrix.data()+8),Float(first.matrix.data()+24),Float(first.matrix.data()+40)};
    // Until tilted poses are observed, require flat native Euler angles and a matching world-matrix yaw.
    // This avoids inferring an unverified Euler order from GC code for inclined player transforms.
    const bool orientation=FlatYawBasis(first.matrix.data(),angles,forward);
    s.geometry_valid=orientation&&FrontDot(forward,player,Vector(first.current.data()),Vector(first.previous.data()),s.front_dot)&&
        std::isfinite(Float(first.attack.data()))&&Float(first.attack.data())>0;
    s.ordinary_route_verified=ordinary;
    return next;
}
template<class Memory> bool ReadObservation(Memory& memory,const NativeAccess& a,const HitEvent& e,Observation& out) {
    out={};
    if(!Complete(a)||!Range(e.player,0x814)||!Range(e.global,0x502C)||!Range(e.attack,12)||!Range(e.current,12)||!Range(e.previous,12)||
        e.classifier_return!=uint64_t(a.image_base)+a.code.classifier_return_rva)return false;
    Frame first{},last{};
    if(!ReadFrame(memory,a,e,first)||!ReadFrame(memory,a,e,last)||!Equal(first,last))return false;
    out=DescribeFrame(first,e,Ordinary(memory,a,e));return true;
}
} // namespace link_runtime::shield

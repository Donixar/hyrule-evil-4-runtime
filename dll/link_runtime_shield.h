#pragma once
#include <cmath>
#include <cstdint>

// Observation only. No hook, health writer, animation writer or active block.
namespace link_runtime::shield {
struct Vec { float x=0, y=0, z=0; };
struct Snapshot {
    bool stable=false, gameplay=false, leon=false, caller_verified=false;
    bool player_match=false, positive_collision=false, posture_verified=false, ready=false;
    bool geometry_valid=false, ordinary_route_verified=false;
    unsigned attack_flags=0, power=0;
    float front_dot=0;
};
inline bool Finite(const Vec& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
inline bool FrontDot(const Vec& forward,const Vec& player,const Vec& current,const Vec& previous,float& dot) {
    dot=0;
    if(!Finite(forward)||!Finite(player)||!Finite(current)||!Finite(previous)) return false;
    const double dx=double(current.x)-previous.x,dz=double(current.z)-previous.z;
    // UHD classifier uses the center when horizontal movement squared <10000,
    // otherwise its travel vector. A SOURCE direction is opposite that travel vector.
    const bool slow=dx*dx+dz*dz<10000.0;
    const double sx=slow?double(current.x)-player.x:-dx,sz=slow?double(current.z)-player.z:-dz;
    const double f2=double(forward.x)*forward.x+double(forward.z)*forward.z,s2=sx*sx+sz*sz;
    if(f2<=0||s2<=0) return false;
    double value=(double(forward.x)*sx+double(forward.z)*sz)/std::sqrt(f2*s2);
    if(!std::isfinite(value)) return false;
    if(value>1) value=1;
    if(value< -1) value=-1;
    dot=static_cast<float>(value);return true;
}
inline bool WouldBlock(bool observe_enabled,const Snapshot& s) {
    return observe_enabled&&s.stable&&s.gameplay&&s.leon&&s.caller_verified&&s.player_match&&
        s.positive_collision&&s.posture_verified&&s.ready&&s.geometry_valid&&s.ordinary_route_verified&&
        s.power>0&&s.power<=0xFFFFU&&(s.attack_flags&~4U)==0&&std::isfinite(s.front_dot)&&
        s.front_dot>=0.5f&&s.front_dot<=1.0f;
}
} // namespace link_runtime::shield

#pragma once
#include "ztarget_native.h"
#include "mortal_draw_magnet.h"
namespace link_runtime::ztarget {
// A single yaw output per native frame. The dispatch transport owns all admission checks.
struct Facing {
    bool known=false;uint32_t frame=0;
    void Reset(){known=false;}
    bool Step(uint32_t tick,float dt,float yaw,Vec player,Vec target,float& out){
        if((known&&tick==frame)||!ValidDt(dt)||!std::isfinite(yaw)||!Finite(player)||!Finite(target))return false;
        namespace mag=link_runtime::mortal_draw::magnet;
        float desired=0;
        if(!mag::FaceYaw({player.x,player.z},{target.x,target.z},desired))return false;
        known=true;frame=tick;
        float proposed=yaw,step=0;
        if(!mag::StepYaw(yaw,{player.x,player.z},{target.x,target.z},8.f*dt/30.f,1.f,proposed,step))return false;
        out=Turn(0,proposed);return true;
    }
};
}

#pragma once
#include "ztarget_strafe.h"
#include <windows.h>
#include <cfenv>
namespace link_runtime::ztarget::strafe {
using Handler=void(__cdecl*)(void*);
using Core=void(__cdecl*)(void*,void*,void*);
struct Ports {
    void* context=nullptr;
    bool (*read)(void*,Sample&)=nullptr;
    bool (*routine)(void*,uint32_t,uint32_t,uint32_t)=nullptr;
    bool (*pose)(void*,const Sample&,const Output&)=nullptr;
};
class Transport {
    unsigned depth=0;uint32_t actor=0;bool failed=false,owns_walk=false;uint32_t owned_actor=0,owned_epoch=0,owned_frame=0;
    bool Ready()const{return !failed&&gate_ready&&thread==GetCurrentThreadId()&&controller.config.enabled&&
        !controller.config.observe&&controller.config.qualified&&ports.read&&ports.routine&&ports.pose;}
    bool Read(Sample& s){
        const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);
        const bool ok=ports.read(ports.context,s);
        std::fesetenv(&env);SetLastError(error);return ok;
    }
    void Fault(){failed=true;controller.End();}
    bool Begin(const Sample& s){
        const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);
        const bool ok=controller.Begin(s);std::fesetenv(&env);SetLastError(error);return ok;
    }
    bool SetRoutine(const Sample& s,uint32_t next){
        const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);
        const bool ok=ports.routine(ports.context,s.actor,s.routine,next);
        std::fesetenv(&env);SetLastError(error);return ok;
    }
    void ReleaseWalk(const Sample& s){
        // Only a Walk this transport entered or successfully redirected. A native action/damage/scene change wins.
        // With W/S held, leave native locomotion selection intact.
        const bool stopped=!s.held||(s.keys&12)==0||(s.keys&12)==12;
        if(uint32_t(s.frame-owned_frame)>1)owns_walk=false;
        if(owns_walk&&s.actor==owned_actor&&s.epoch==owned_epoch&&s.safe&&s.focused&&s.walk_motion&&
           (s.routine&0xFFFF)==0x100&&!(s.keys&3)&&stopped){if(!SetRoutine(s,0))Fault();}
        if(stopped||!s.safe||!s.focused||s.actor!=owned_actor||s.epoch!=owned_epoch||(s.routine&0xFFFF)!=0x100)owns_walk=false;
    }
public:
    Controller controller;Ports ports;DWORD thread=0;bool gate_ready=false;
    // Called only around the original NORMAL handler after arbitration has declined all leases.
    // Never wrap the event handler, or the entire Runtime::Handle (which includes owned actions).
    void Normal(Handler original,void* player,bool qualified){
        if(!original)return;
        if(depth){++depth;__try{original(player);}__finally{--depth;}return;}
        if(!qualified||!Ready()){original(player);return;}
        Sample s;
        if(!Read(s)||s.actor!=reinterpret_cast<uint32_t>(player)){owns_walk=false;original(player);return;}
        if(!Begin(s)){ReleaseWalk(s);original(player);return;}
        const bool entering=controller.NeedsWalk();
        const bool entered=!entering||SetRoutine(s,0x100);
        if(!entered){Fault();original(player);return;}
        if(entering){owns_walk=true;owned_actor=s.actor;owned_epoch=s.epoch;owned_frame=s.frame;}
        actor=s.actor;depth=1;
        __try{original(player);}__finally{depth=0;actor=0;controller.End();}
    }
    // Native return address must be captured in dispatch_hooks::Core before Runtime::Core.
    // Caller is RVA, not VA. This runs before the original Core exactly once, never after collision.
    bool BeforeCore(void* model,void* work,void* camera,uint32_t caller_rva){
        if(depth!=1||!Ready()||reinterpret_cast<uint32_t>(model)!=actor)return false;
        const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);
        Sample s;Output out;
        if(!Read(s)||!controller.BeforeCore(s,caller_rva,reinterpret_cast<uint32_t>(work),
                                          reinterpret_cast<uint32_t>(camera),out)){
            std::fesetenv(&env);SetLastError(error);return false;}
        const bool ok=ports.pose(ports.context,s,out);
        std::fesetenv(&env);SetLastError(error);
        if(!ok){Fault();return false;}
        owns_walk=true;owned_actor=s.actor;owned_epoch=s.epoch;owned_frame=s.frame;return true;
    }
    uint32_t LocalGate(uint32_t player,uint32_t native_keys){
        if(depth!=1||player!=actor||!Ready())return native_keys;
        Sample s;uint32_t result=native_keys;
        if(Read(s))controller.LocalKeys(s,native_keys,result);
        return result;
    }
    bool Failed()const{return failed;}
};
}

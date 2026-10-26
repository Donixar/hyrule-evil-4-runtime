#pragma once
#include <atomic>
#include <cstdint>
namespace link_runtime::parry::audio {
// Worker-owned nonrepeating choice. Seed does not affect combat or timing.
class Variation {
    uint32_t state;
    unsigned last=3;
public:
    explicit Variation(uint32_t seed):state(seed?seed:1){}
    unsigned Next(){
        state^=state<<13;state^=state>>17;state^=state<<5;
        last=last==3?state%3:(last+1+state%2)%3;
        return last;
    }
};
// Copied successful-event timestamp only; no actor pointers or playback in hook.
// Worker owns attempted/playing and the audio device. Latest event wins.
class Events {
    std::atomic<uint64_t> latest{0};
    uint64_t attempted=0;
    bool playing=false;
public:
    void Publish(bool committed,uint64_t now){
        if(!committed||!now)return;
        auto old=latest.load(std::memory_order_relaxed);
        while(now>old&&!latest.compare_exchange_weak(old,now,std::memory_order_release,std::memory_order_relaxed)){}
    }
    template<class Audio> void Poll(uint64_t now,bool foreground,Audio& audio){
        const auto event=latest.load(std::memory_order_acquire);
        if(!foreground){
            if(event>attempted)attempted=event;
            if(playing){audio.Stop();playing=false;}
            return;
        }
        if(event<=attempted)return;
        attempted=event;
        if(now<event||now-event>200)return;
        if(playing){audio.Stop();playing=false;}
        playing=audio.Play();
    }
};
}

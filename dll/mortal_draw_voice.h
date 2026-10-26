#pragma once
#include <atomic>
#include <cstdint>
#include "mortal_draw_dispatch.h"

namespace link_runtime::mortal_draw::voice {
// One serialized game-thread producer, one audio-worker consumer. Publish only
// after dispatch accepts a Mortal Draw lease. Carries no game pointers.
// Token zero means silence. Tokens are process-lifetime monotonic lease IDs.
class Mailbox {
    std::atomic<uint64_t> current{0};
    uint64_t last=0; // producer-owned
public:
    bool Start(uint64_t token){
        if(!token||token<=last)return false;
        last=token;current.store(token,std::memory_order_release);return true;
    }
    void Stop(uint64_t token){
        if(token)current.compare_exchange_strong(token,0,std::memory_order_acq_rel);
    }
    uint64_t Current() const{return current.load(std::memory_order_acquire);}
};
class Consumer {
    uint64_t attempted=0,playing=0;
public:
    // Audio owns waveOut and is accessed solely by this worker. Cancellation is
    // asynchronous: a cancellation racing Play is stopped at the next Poll.
    // A superseded/cancelled pending action is never replayed on a later tick.
    template<class Audio> void Poll(const Mailbox& box,Audio& audio){
        const uint64_t token=box.Current();
        if(playing&&playing!=token){audio.Stop();playing=0;}
        if(!token||token<=attempted)return;
        attempted=token;
        if(box.Current()!=token)return;
        if(audio.Play())playing=token;
        // Catch cancellation/replacement that occurred during the backend call.
        if(playing&&box.Current()!=playing){audio.Stop();playing=0;}
    }
};
inline void __cdecl Bridge(void* context,dispatch::Event event,uint64_t token){
    if(!context)return;
    auto& box=*static_cast<Mailbox*>(context);
    if(event==dispatch::Event::Started)box.Start(token);else box.Stop(token);
}
}

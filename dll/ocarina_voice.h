#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "ocarina_pitch.h"
namespace link_runtime::ocarina {
// Packed request: token (48 bits) | sequence (8 bits) | note+1 (8 bits); note = ocarina_pitch.h code (0..4 or 60..95).
// One game-thread producer, one audio-worker consumer, no game pointers.
// Push and Close are game-thread only (single producer). A focus-loss fade is
// done consumer-side: the worker calls audio.FadeStop() itself, never Close.
static_assert(std::atomic<uint64_t>::is_always_lock_free,"mailbox atomic must be lock-free");
class NoteMailbox {
    std::atomic<uint64_t> req{0}; uint64_t closed=0; uint8_t seq=0;
public:
    bool Push(uint64_t token,int8_t note){
        if(!token||token<=closed||!ValidNote(note)||token>=(1ULL<<48))return false;
        ++seq;req.store((token<<16)|(uint64_t(seq)<<8)|uint64_t(note+1),std::memory_order_release);return true;
    }
    void Close(uint64_t token){
        if(token>closed)closed=token;
        uint64_t cur=req.load(std::memory_order_acquire);
        while((cur>>16)==token&&!req.compare_exchange_weak(cur,0,std::memory_order_acq_rel)){}
    }
    uint64_t Current() const {return req.load(std::memory_order_acquire);}
};
// Consumer-side fade (Task 10 fix I2): what is left of the sounding note from frame `at` on,
// times a linear ramp that reaches 0 after `frames` frames (shorter if the note ends first).
// The worker queues this instead of cutting with waveOutReset. Returns the frames written;
// `out` holds at least frames*channels samples.
constexpr unsigned kFadeMs = 15;
inline size_t FadeFrames(unsigned rate){return size_t(rate)*kFadeMs/1000;}
inline size_t FadeTail(const int16_t* pcm,size_t total_frames,unsigned channels,size_t at,size_t frames,int16_t* out){
    if(!pcm||!out||!channels||!frames||at>=total_frames)return 0;
    const size_t n=frames<total_frames-at?frames:total_frames-at;
    for(size_t i=0;i<n;++i){
        const float gain=float(frames-1-i)/float(frames);
        for(unsigned c=0;c<channels;++c)
            out[i*channels+c]=int16_t(float(pcm[(at+i)*channels+c])*gain);
    }
    return n;
}
class NoteConsumer {
    uint64_t handled=0; bool sounding=false;
public:
    template<class Audio> void Poll(const NoteMailbox& box,Audio& audio){
        const uint64_t r=box.Current();
        if(!r){if(sounding){audio.FadeStop();sounding=false;}handled=0;return;}
        if(r==handled)return;
        handled=r;
        if(sounding){audio.FadeStop();sounding=false;}
        if(audio.Play(int(r&0xFF)-1))sounding=true;
    }
};
}

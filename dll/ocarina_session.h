// dll/ocarina_session.h
#pragma once
#include <cstdint>
#include "ocarina_input.h"
#include "game_clock.h"
namespace link_runtime::ocarina {
enum class State { Idle, Sheathing, Entering, Playing, Leaving };
enum class Reason { None, Stowed, FocusLost, GateLost, ClipFailed, Timeout, Rejected };
// Game-thread only. Acquire order freeze -> mount -> clip; release is always
// the reverse (StopClip if a clip runs, Unmount, FreezeEnd, then Ended), on
// every exit path, so the world is never left frozen.
// Sheathing, Entering and Leaving are bounded by a watchdog in 60 Hz frames of game
// time: each Tick adds its `step` (2*deltaTime, 1.0 at 60 FPS, game_clock.h), so the
// budgets keep their seconds under re4_fps 165. Playing is unbounded by design.
//
// Host/runtime contract (Task 10 wiring):
//  - Session and NoteInput are game-thread only.
//  - Tick exactly once per game frame, from the player handler. The budgets
//    (default 120 sheathe / 600 clip) are 60 Hz frames: 2 s / 10 s of game time.
//  - Host::Foreground() must also report a focus loss that happened between
//    ticks: the worker publishes a sticky atomic flag and Foreground() returns
//    false once and clears it.
//  - Host::RequestSheathe() must latch suppression of the native knife hold
//    until Ended(), so a held knife button cannot keep the sword ready (this
//    also gives "no vuelve a prepararla sola" after stow).
//  - Host::GatesOk() must admit the sword-ready posture (Sheathing runs with
//    the sword still out).
//  - Toggle/stow during Sheathing and Entering are intentionally ignored.
//  - Host::StopClip() stops the running clip (called before Unmount). It must
//    be idempotent: a clip that already ended on its own is a no-op.
//  - Host::Ended(token,reason) is called exactly once, at the end of release,
//    for every session: the token is taken on leaving Idle (Idle->Sheathing or
//    Idle->Entering), so a Sheathing cancel (focus, gates, timeout) also ends it
//    and the Host's knife-hold latch is always released.
// Last 8 notes played (spec: "registro en memoria de las últimas 8 notas para la fase 2").
// Oldest first through At(0..Size()-1). Cleared when a new session leaves Idle, so the notes
// of the session that just ended stay readable until the next one starts.
class NoteHistory {
    int8_t notes_[8]={};uint8_t head_=0,count_=0;
public:
    static constexpr uint8_t kCapacity=8;
    void Push(int8_t note){if(note<0||note>4)return;notes_[head_]=note;head_=uint8_t((head_+1)%kCapacity);if(count_<kCapacity)++count_;}
    void Clear(){head_=count_=0;}
    uint8_t Size() const {return count_;}
    int8_t At(uint8_t i) const {return i<count_?notes_[(head_+kCapacity-count_+i)%kCapacity]:int8_t(-1);}
    // 4 bits per note (note+1, 0 = empty), oldest in the lowest nibble: for one log word.
    uint32_t Packed() const {uint32_t v=0;for(uint8_t i=0;i<count_;++i)v|=uint32_t(At(i)+1)<<(4*i);return v;}
};
template<class Host> class Session {
    State st=State::Idle; Reason why=Reason::None; uint64_t tok=0,next=1;
    bool frozen=false,mounted=false,clip_active=false,has_token=false;
    NoteHistory notes_;
    uint32_t sheathe_limit,clip_limit;
    double elapsed=0;  // game time in the current bounded state (60 Hz frames)
    void SetState(State s){st=s;elapsed=0;}
    void Release(Host& h,Reason r){
        if(clip_active){h.StopClip();clip_active=false;}
        if(mounted){h.Unmount();mounted=false;}
        if(frozen){h.FreezeEnd();frozen=false;}
        SetState(State::Idle);why=r;
        if(has_token){has_token=false;h.Ended(tok,r);}
    }
    void ClipFail(Host& h){Release(h,Reason::ClipFailed);h.Log("ocarina cancelled clip");}
    void TakeToken(){if(!has_token){tok=next++;has_token=true;notes_.Clear();}}
    void Enter(Host& h){
        TakeToken();
        if(!h.FreezeBegin()){h.Log("ocarina rejected freeze");Release(h,Reason::Rejected);return;}
        frozen=true;
        if(!h.Mount()){h.Log("ocarina rejected mount");Release(h,Reason::Rejected);return;}
        mounted=true;
        if(!h.StartClip(0)){ClipFail(h);return;}
        clip_active=true;
        SetState(State::Entering);h.Log("ocarina started");
    }
public:
    explicit Session(uint32_t sheathe_ticks=120,uint32_t clip_ticks=600)
        :sheathe_limit(sheathe_ticks?sheathe_ticks:1),clip_limit(clip_ticks?clip_ticks:1){}
    State state() const {return st;}
    uint64_t token() const {return tok;}
    Reason last_reason() const {return why;}
    bool BlocksParry() const {return st!=State::Idle;}
    const NoteHistory& history() const {return notes_;}
    // `step`: this tick in 60 Hz frames (game_clock::Step60(deltaTime)); 1.0 at 60 FPS.
    void Tick(const InputEvents& e,Host& h,double step=1.0){
        const bool fg=h.Foreground();
        if(st!=State::Idle){
            if(!fg){Release(h,Reason::FocusLost);h.Log("ocarina cancelled focus");return;}
            if(!h.GatesOk()){Release(h,Reason::GateLost);h.Log("ocarina cancelled gates");return;}
        }
        if(st==State::Sheathing||st==State::Entering||st==State::Leaving){
            const uint32_t limit=st==State::Sheathing?sheathe_limit:clip_limit;
            elapsed+=step;
            if(game_clock::Exceeded(elapsed,double(limit))){Release(h,Reason::Timeout);h.Log("ocarina cancelled timeout");return;}
        }
        switch(st){
        case State::Idle:
            if(!e.toggle)return;
            if(!fg||!h.GatesOk()){h.Log("ocarina rejected gates");return;}
            if(h.SwordReady()){TakeToken();h.RequestSheathe();SetState(State::Sheathing);return;}
            Enter(h);return;
        case State::Sheathing:
            if(!h.SwordReady())Enter(h);
            return;
        case State::Entering:
            if(h.ClipFinished()){clip_active=false;if(!h.StartClip(1)){ClipFail(h);return;}clip_active=true;SetState(State::Playing);}
            return;
        case State::Playing:
            if(e.toggle||e.stow){if(!h.StartClip(2)){ClipFail(h);return;}clip_active=true;SetState(State::Leaving);return;}
            if(e.note>=0){notes_.Push(e.note);h.PlayNote(tok,e.note);}
            return;
        case State::Leaving:
            if(h.ClipFinished()){clip_active=false;Release(h,Reason::Stowed);h.Log("ocarina finished");}
            return;
        }
    }
};
}

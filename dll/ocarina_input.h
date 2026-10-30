// dll/ocarina_input.h
#pragma once
#include <cmath>
#include <cstdint>
#include "ocarina_pitch.h"
namespace link_runtime::ocarina {
// Index order matches OoT's OCARINA_BTN_* (general.c:826-830): D4 F4 A4 B4 D5.
enum class Note : int8_t { A=0, CDown=1, CRight=2, CLeft=3, CUp=4 };
// piano (29-oct, chromatic ocarina): bit i = the piano key of written MIDI 60+i (C4..B5) is down; shift = one
// octave up (C5..B6). Pad: lb/rb alone = one semitone down/up, rt = one octave up, applied to the button or
// stick note pressed with them (LB+RB together stays the toggle combo and changes nothing).
struct RawInput { bool up=false,down=false,left=false,right=false,a=false,stow=false,toggle_key=false;
                  float lx=0,ly=0; bool pad_a=false,pad_b=false,lb=false,rb=false;
                  uint32_t piano=0; bool shift=false,rt=false; };
struct InputEvents { bool toggle=false; bool stow=false; int8_t note=-1; };
constexpr int kPianoKeys=24;
// Stick convention: lx>0 is right, ly>0 is up, range -1..1; callers normalize
// native/XInput axes before filling RawInput. Poll emits at most one note per
// call, with priority A, up, down, left, right, then stick, then the piano (lowest key first).
// Game-thread only. Every action fires on a fresh press seen while focused;
// a key already down on the first poll or across a focus loss is not an edge.
class NoteInput {
    static constexpr float kEnter=0.6f, kRelease=0.3f;
    bool seen=false; RawInput prev{}; bool combo_prev=false; bool stick_latched=false;
    static bool Edge(bool now,bool before){return now&&!before;}
    // A button note pressed with the pad modifiers: the chromatic pitch it becomes (unchanged without them).
    static int8_t Modified(int8_t note,const RawInput& r){
        if(!IsButtonNote(note))return note;
        const int semi=r.lb==r.rb?0:(r.rb?1:-1);
        if(!semi&&!r.rt)return note;
        const int midi=ToMidi(note)+semi+(r.rt?12:0);
        return IsPitchNote(midi)?int8_t(midi):note;
    }
public:
    void Reset(){seen=false;prev=RawInput{};combo_prev=false;stick_latched=false;}
    InputEvents Poll(const RawInput& r,bool foreground){
        InputEvents e{};
        const bool combo=r.lb&&r.rb;
        if(!foreground||!seen){prev=r;combo_prev=combo;seen=foreground;
            stick_latched=std::fabs(r.lx)>=kRelease||std::fabs(r.ly)>=kRelease;return e;}
        e.toggle=Edge(r.toggle_key,prev.toggle_key)||Edge(combo,combo_prev);
        e.stow=Edge(r.stow,prev.stow)||Edge(r.pad_b,prev.pad_b);
        if(Edge(r.a,prev.a)||Edge(r.pad_a,prev.pad_a))e.note=int8_t(Note::A);
        else if(Edge(r.up,prev.up))e.note=int8_t(Note::CUp);
        else if(Edge(r.down,prev.down))e.note=int8_t(Note::CDown);
        else if(Edge(r.left,prev.left))e.note=int8_t(Note::CLeft);
        else if(Edge(r.right,prev.right))e.note=int8_t(Note::CRight);
        const float ax=std::fabs(r.lx),ay=std::fabs(r.ly),mag=ax>ay?ax:ay;
        if(stick_latched){if(mag<kRelease)stick_latched=false;}
        else if(mag>=kEnter){stick_latched=true;
            if(e.note<0)e.note=int8_t(ax>ay?(r.lx>0?Note::CRight:Note::CLeft):(r.ly>0?Note::CUp:Note::CDown));}
        if(e.note>=0)e.note=Modified(e.note,r);
        else {
            const uint32_t fresh=r.piano&~prev.piano&((1u<<kPianoKeys)-1);
            for(int i=0;i<kPianoKeys&&fresh;++i)if(fresh&(1u<<i)){e.note=int8_t(kPitchLo+i+(r.shift?12:0));break;}
        }
        prev=r;combo_prev=combo;return e;
    }
};
}

#pragma once
#include "spin_attack.h"
namespace link_runtime::ending_blow {
class Press {
    sword_actions::Input previous_;bool known_=false,armed_=false,pressed_=false;
public:
    // The last Step saw a fresh knife attack press in a safe context. Step admits it only with a
    // target; without one the runtime logs ending_none for it (diagnostics, once per press).
    bool Pressed() const{return pressed_;}
    bool Step(const sword_actions::Input& s,bool target) {
        pressed_=false;
        if(known_&&s.frame==previous_.frame&&sword_actions::SameContext(s,previous_))return false;
        const bool context=known_&&s.valid&&sword_actions::SameContext(s,previous_)&&s.time>=previous_.time;
        previous_=s;known_=s.valid;
        if(!context){armed_=s.valid&&!s.fire;return false;}
        if(!s.fire&&!s.fire_edge){armed_=true;return false;}
        const bool press=armed_&&s.fire_edge;armed_=false;
        pressed_=press&&s.valid&&s.safe&&s.knife;
        return pressed_&&target;
    }
};
}

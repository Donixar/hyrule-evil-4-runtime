#pragma once
#include <cmath>
#include <cstdint>

// Pure candidate policies. Qualification is supplied by offline fixtures only;
// no game reader, hook, input, animation or native damage write is connected here.
namespace link_runtime::defense {
struct RearContact {
    bool stable=false,gameplay=false,hylian_pl08=false;
    bool ordinary=false,direction_qualified=false,contact_before_body=false;
    double front_dot=0;
};
struct ParryWindow {
    bool released=false,down=false,armed=false;
    uint64_t armed_at=0,last_time=0;
    uint32_t duration=0;
};
// RearShieldDamagePercent: the share of a qualified rear hit that still reaches Link, 50..100 (50 = the old halving;
// Donald 22-oct: the rear shield must not be that strong, default 75). Anything else: the default.
constexpr unsigned kRearPercentDefault=75;
inline unsigned RearPercent(unsigned raw){return raw>=50&&raw<=100?raw:kRearPercentDefault;}
inline uint32_t RearDamage(uint32_t d,const RearContact& c,unsigned kept_percent=50) {
    if(!c.stable||!c.gameplay||!c.hylian_pl08||!c.ordinary||
       !c.direction_qualified||!c.contact_before_body||!std::isfinite(c.front_dot)||
       c.front_dot < -1.0||c.front_dot > -std::sqrt(0.5))return d;
    const uint64_t pct=kept_percent>=1&&kept_percent<=100?kept_percent:50;
    return static_cast<uint32_t>((uint64_t(d)*pct+99)/100);   // rounded up
}
// ParryWindowMs: how long one fresh knife press stays armed while the button is held (wall time).
// Donald's 24-oct logs: every tier-2 `window` refusal with the button held came 453..1375 ms after
// the press (11 of 16 at <=734 ms), while the successful parries came 172..390 ms after it; the old
// fixed 450 ms window was shorter than his habit of readying the sword early. Still one press per
// hit (consumed on use, released = disarmed, holding never rearms). 100..1000, else the default.
constexpr uint32_t kParryWindowDefault=750;
inline uint32_t ParryWindowMs(unsigned raw){return raw>=100&&raw<=1000?raw:kParryWindowDefault;}
inline void UpdateParryButton(ParryWindow& p,bool down,bool gameplay,uint64_t now_ms,uint32_t window_ms) {
    if(!gameplay||!window_ms||now_ms<p.last_time) {
        p={};p.last_time=now_ms;p.down=down;return;
    }
    p.last_time=now_ms;
    if(!down) {
        p.down=false;p.released=true;p.armed=false;p.armed_at=0;p.duration=0;return;
    }
    if(!p.down&&p.released) {
        p.armed=true;p.armed_at=now_ms;p.duration=window_ms;p.released=false;
    }
    p.down=true;
    if(p.armed&&now_ms>=p.armed_at&&now_ms-p.armed_at>p.duration)p.armed=false;
}
// Press evidence for the parry log (bug #3, 24-oct): whether the button is down and how long ago the
// current or last press armed (0 = released or never armed). Tells a missed attempt (down, early or late)
// from a hit that was never parried. Read-only.
struct PressAge {unsigned down=0;unsigned long long age_ms=0;};
inline PressAge PressAt(const ParryWindow& p,uint64_t now_ms) {
    PressAge a;a.down=p.down?1U:0U;
    if(p.armed_at&&now_ms>=p.armed_at)a.age_ms=now_ms-p.armed_at;
    return a;
}
inline bool ConsumeParry(ParryWindow& p,uint64_t now_ms,bool frontal_ordinary_contact_qualified) {
    if(now_ms<p.last_time||(p.armed&&now_ms<p.armed_at)) {
        p={};p.last_time=now_ms;return false;
    }
    p.last_time=now_ms;
    if(!p.armed)return false;
    // Subtract after ordering checks; an absolute deadline can overflow uint64 near its maximum.
    if(!p.duration||now_ms-p.armed_at>p.duration) {p.armed=false;return false;}
    if(!frontal_ordinary_contact_qualified)return false;
    p.armed=false;return true;
}
} // namespace link_runtime::defense

#pragma once
// TP-style sword charge glow (charge-glow-01, 21-oct; TP d_a_alink_effect.inc setSwordChargeEffect: KIRARI_A light on
// the blade, KIRARI_B glint from the hilt to the tip over the charge, KIRARI_C sparkle once full, the light fading
// after). Pure: the INI keys, the charge's timing and what each frame shows; no game access. The glue
// (spin_attack_runtime.h) feeds spin::Charge's events and the blade (E1 tracker); sword_charge_draw.h draws it in the
// world table 17 like the trail.
//
// INI, [LinkRuntime]; an invalid value keeps its default and is counted in `problems`:
//   SwordCharge       missing or 1 = on, 0 = fully inert (no reads, no registration, no sound)
//   SwordChargeAlpha  0..255 (default 140): the glow's alpha once fully charged and the glint's
//   SwordChargeSound  missing or 1 = Bin32/link_sword_charge.wav (TP's Z2SE_SWORD_POWER_COME) once at the full charge
// Colours are the trail's: SwordTrailColor for the spin, SwordTrailGreatColor for the Great Spin (full life).
// Timing, all game time in ms (the same look at 30/60/165 FPS): the glow ramps 0 -> alpha over kChargeMs; the glint
// goes from the hilt (progress 0) to the tip (1) over kChargeMs, arriving with Event::Charged; once full the glint
// bursts (scale 1 -> kBurstScale -> 1 over kBurstMs) and then pulses at kPulseHz while held; a release or cancel fades
// everything out over kFadeMs. Nothing shows before the sword is in hand (the knife hold routine), nor outside
// gameplay; a room or player change, or game time running back, resets.
#include "sword_trail_feed.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace link_runtime::sword_charge {
inline constexpr const char* kSoundSha="0c2c4d28edbf884f0cef1db9ac166e36a57da18d11e0abccaf01802954b30ca8";
inline constexpr const wchar_t* kSoundFile=L"link_sword_charge.wav";
constexpr double kChargeMs=1000.0;    // spin::Charge: 60 game frames
constexpr double kFadeMs=120.0;
constexpr double kBurstMs=150.0,kBurstScale=1.6;
constexpr double kPulseHz=2.0,kPulseScale=1.2;   // 1 .. kPulseScale
constexpr double kBackwardsMs=50.0;
constexpr double kPi=3.14159265358979323846;

struct Config {bool on=true;uint8_t alpha=140;bool sound=true;unsigned problems=0;};
// get(key, out, count) fills `out` with the raw INI value ("" when missing).
template<class Get> Config ReadConfig(Get&& get){
    Config c;wchar_t v[32]{};
    auto read=[&](const wchar_t* key){v[0]=0;get(key,v,sizeof(v)/sizeof(v[0]));return v[0]!=0;};
    auto flag=[&](const wchar_t* key,bool& out){
        if(!read(key))return;
        if(v[0]==L'0'&&!v[1])out=false;else if(v[0]==L'1'&&!v[1])out=true;else ++c.problems;
    };
    flag(L"SwordCharge",c.on);
    if(read(L"SwordChargeAlpha")){
        unsigned n=0;if(sword_trail::feed::Digits(v,255,n)&&n<=255)c.alpha=uint8_t(n);else ++c.problems;
    }
    flag(L"SwordChargeSound",c.sound);
    return c;
}

// The glint's scale `t` ms after the full charge: the burst, then the pulse.
inline double StarScale(double t){
    if(!(t>0))return 1.0;
    if(t<kBurstMs)return 1.0+(kBurstScale-1.0)*std::sin(kPi*t/kBurstMs);
    return 1.0+(kPulseScale-1.0)*0.5*(1.0-std::cos(2.0*kPi*kPulseHz*(t-kBurstMs)/1000.0));
}

// What one frame shows. Alphas 0..255 (before the per-vertex factors), progress 0 (hilt) .. 1 (tip).
struct Look {uint32_t rgb=0;double glow_alpha=0,star_alpha=0,progress=0,star_scale=1;};
enum class Phase:uint8_t {Idle,Charging,Fading};
// Frame: Idle (nothing kept), Hidden (kept, not drawn: not gameplay or the sword not in hand yet), Draw, Done (the fade
// just ended), Room / Player / Backwards (a reset).
enum class Step:uint8_t {Idle,Hidden,Draw,Done,Room,Player,Backwards};
inline const char* StepName(Step s){
    static const char* const n[]={"idle","hidden","draw","done","room","player","backwards"};
    return unsigned(s)<7?n[unsigned(s)]:"?";
}

// One charge at a time (game thread). Counters are atomics so the worker can log them.
class Glow {
    Phase phase_=Phase::Idle;bool great_=false,full_=false,in_hand_=false;
    double start_=0,full_at_=0,end_at_=0;uint32_t room_=0,player_=0,rgb_=0;uint8_t alpha_=0;
    std::atomic<unsigned> charges_{0},fulls_{0};
public:
    Phase State() const {return phase_;}
    bool Charging() const {return phase_==Phase::Charging;}
    bool Great() const {return great_;}
    bool IsFull() const {return full_;}
    bool InHand() const {return in_hand_;}   // the knife hold seen in this charge
    unsigned Charges() const {return charges_.load();}
    unsigned Fulls() const {return fulls_.load();}
    void Reset(){phase_=Phase::Idle;full_=in_hand_=false;}
    // Event::Charging (now in game ms). false: SwordCharge=0 (nothing kept).
    bool Begin(const Config& c,double now,uint32_t rgb,bool great,uint32_t room,uint32_t player){
        if(!c.on||!std::isfinite(now))return false;
        phase_=Phase::Charging;great_=great;full_=in_hand_=false;start_=now;full_at_=end_at_=0;
        room_=room;player_=player;rgb_=rgb&0xFFFFFF;alpha_=c.alpha;
        charges_.fetch_add(1);return true;
    }
    // Event::Charged. true once per charge (the sound and the `full` line).
    bool Full(double now){
        if(phase_!=Phase::Charging||full_||!std::isfinite(now))return false;
        full_=true;full_at_=now<start_?start_:now;fulls_.fetch_add(1);return true;
    }
    // Released or cancelled: the fade starts. true when it was charging (the `end` line).
    bool End(double now){
        if(phase_!=Phase::Charging)return false;
        phase_=Phase::Fading;end_at_=std::isfinite(now)&&now>=start_?now:start_;return true;
    }
    // This frame. in_hand: the knife hold routine (the sword left the sheath); seen once, it stays for the charge.
    Step Frame(double now,uint32_t room,uint32_t player,bool gameplay,bool in_hand,Look& out){
        out=Look{};
        if(phase_==Phase::Idle)return Step::Idle;
        if(room!=room_){Reset();return Step::Room;}
        if(player!=player_){Reset();return Step::Player;}
        const double last=phase_==Phase::Fading?end_at_:full_?full_at_:start_;
        if(!std::isfinite(now)||now<last-kBackwardsMs){Reset();return Step::Backwards;}
        double fade=1.0,t=now;
        if(phase_==Phase::Fading){
            const double age=now-end_at_;
            if(age>=kFadeMs){Reset();return Step::Done;}
            fade=age>0?1.0-age/kFadeMs:1.0;t=end_at_;
        }
        if(in_hand)in_hand_=true;
        if(!gameplay||!in_hand_)return Step::Hidden;
        const double e=t-start_;
        const double ramp=full_?1.0:e<=0?0.0:e>=kChargeMs?1.0:e/kChargeMs;
        out.rgb=rgb_;out.progress=ramp;out.star_scale=full_?StarScale(t-full_at_):1.0;
        out.glow_alpha=double(alpha_)*ramp*fade;out.star_alpha=double(alpha_)*fade;
        return Step::Draw;
    }
};

// Log lines. Each returns the snprintf length (<= 0 or >= size: not written whole).
inline int FormatReady(char* out,size_t size,bool ready,const Config& c,bool sound){
    return std::snprintf(out,size,"sword_charge ready=%d alpha=%u sound=%d",ready?1:0,unsigned(c.alpha),sound?1:0);
}
inline int FormatBegin(char* out,size_t size,bool great,bool identity){
    return std::snprintf(out,size,"sword_charge begin great=%d identity=%d",great?1:0,identity?1:0);
}
inline int FormatEnd(char* out,size_t size,const char* reason){
    return std::snprintf(out,size,"sword_charge end reason=%s",reason);
}
inline int FormatStats(char* out,size_t size,unsigned charges,unsigned full,unsigned frames_drawn,unsigned draw_fails,
                       unsigned restore_missing){
    return std::snprintf(out,size,"sword_charge stats charges=%u full=%u frames_drawn=%u draw_fails=%u restore_missing=%u",
        charges,full,frames_drawn,draw_fails,restore_missing);
}
} // namespace link_runtime::sword_charge

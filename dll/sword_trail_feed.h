#pragma once
// Task 4a sword trail feeding (docs/estela-espada-diseno-2026-10-20.md sections 2 and 4). Pure: the INI keys and the
// per-spin history rules; no game access. The glue (spin_attack_runtime.h) reads the blade with the E1 tracker
// (sword_trail_log.h) and hands each frame's strip to sword_trail_draw_ot.h.
//
// INI, [LinkRuntime]; an invalid value keeps its default and is counted in `problems`:
//   SwordTrail           missing or 1 = on, 0 = fully inert (no reads, no registration)
//   SwordTrailMoves      comma list of spin, great_spin, back_slice, ending, slash (default all five; an unknown name
//                        keeps the whole default list)
//   SwordTrailLengthMs   game ms of trail of both spins, clamped 60..350 (default 200, TP's blur ~6 frames at 30 FPS)
//   SwordTrailInner      ribbon start along the blade, 0 = hilt .. 1 = tip, clamped 0..0.9 (default 0.25)
//   SwordTrailColor      RRGGBB hex (default 9FD0FF), SwordTrailAlpha 0..255 (default 150)
//   SwordTrailGreatColor RRGGBB hex (default FF3A28), SwordTrailGreatAlpha 0..255 (default 170)
//   SwordTrailBack*      Back Slice: Color FFD24A, Alpha 160, LengthMs 250 (60..350)
//   SwordTrailEnding*    Ending Blow and its leap: Color FFFFFF, Alpha 190, LengthMs 300 (60..350)
//   SwordTrailSlash*     native knife slashes: Color E8F0FF, Alpha 90, LengthMs 150 (60..350)
// Rules (Trail): the first sample after Begin is the start frame's pose, read before the sword reached the hand (E1:
// sheathed, 1.2 m away), so it is skipped; a base that moves faster than kJumpMm per 60 Hz frame from the previous
// sample (at least kJumpMm, at most kJumpCapMm, whatever the frame rate) cuts the strip (never bridged); a failed read cuts. After End (finished or cancelled) the samples fade out by time; a new move start
// drops them (one style per history, so a new move never recolours a fading one); a room or player change, or game time running back more than kBackwardsMs,
// resets; outside gameplay no quads are built (the samples stay).
#include "sword_trail.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace link_runtime::sword_trail::feed {
namespace core=::sword_trail;
constexpr double kJumpMm=400.0;      // per 1/60 s: the 30 FPS Ending Blow leap (~600 mm a frame) is not a jump
constexpr double kJumpCapMm=1000.0;  // a hitch never lets a real jump (sheath to hand, 1.2 m) bridge
inline double JumpLimit(double dt_ms){
    const double d=std::isfinite(dt_ms)&&dt_ms>0?kJumpMm*dt_ms*60.0/1000.0:0.0;
    return d<kJumpMm?kJumpMm:d>kJumpCapMm?kJumpCapMm:d;
}
constexpr double kBackwardsMs=50.0;   // now earlier than the newest sample by more than this: reset

// The moves with a trail (SwordTrailMoves names, in this order). Shield Bash never: the sword is not swung.
enum class Move:uint8_t {Spin,GreatSpin,Back,Ending,Slash};
constexpr unsigned kMoves=5;
inline const char* MoveName(Move m){
    static const char* const n[kMoves]={"spin","great_spin","back_slice","ending","slash"};
    return unsigned(m)<kMoves?n[unsigned(m)]:"none";
}
struct Config {
    bool on=true,spin=true,great_spin=true,back_slice=true,ending=true,slash=true;
    unsigned length_ms=200;double inner=0.25;
    uint32_t color=0x9FD0FF;uint8_t alpha=150;uint32_t great_color=0xFF3A28;uint8_t great_alpha=170;
    uint32_t back_color=0xFFD24A;uint8_t back_alpha=160;unsigned back_length_ms=250;
    uint32_t ending_color=0xFFFFFF;uint8_t ending_alpha=190;unsigned ending_length_ms=300;
    uint32_t slash_color=0xE8F0FF;uint8_t slash_alpha=90;unsigned slash_length_ms=150;
    unsigned problems=0;
    // Listed in SwordTrailMoves (whatever SwordTrail says).
    bool Listed(Move m) const {
        return m==Move::Spin?spin:m==Move::GreatSpin?great_spin:m==Move::Back?back_slice:m==Move::Ending?ending:m==Move::Slash&&slash;
    }
    bool Wants(Move m) const {return on&&Listed(m);}
    core::Style Style(Move m) const {
        core::Style s{};s.inner=inner;
        switch(m){
        case Move::GreatSpin:s.length_ms=double(length_ms);s.rgb=great_color;s.alpha=great_alpha;break;
        case Move::Back:s.length_ms=double(back_length_ms);s.rgb=back_color;s.alpha=back_alpha;break;
        case Move::Ending:s.length_ms=double(ending_length_ms);s.rgb=ending_color;s.alpha=ending_alpha;break;
        case Move::Slash:s.length_ms=double(slash_length_ms);s.rgb=slash_color;s.alpha=slash_alpha;break;
        default:s.length_ms=double(length_ms);s.rgb=color;s.alpha=alpha;break;
        }
        return s;
    }
    bool Wants(bool great) const {return Wants(great?Move::GreatSpin:Move::Spin);}
    core::Style Style(bool great) const {return Style(great?Move::GreatSpin:Move::Spin);}
};
inline bool Digits(const wchar_t* v,unsigned max,unsigned& out){
    if(!v||!*v)return false;
    unsigned n=0;
    for(const wchar_t* p=v;*p;++p){if(*p<L'0'||*p>L'9')return false;n=n*10+unsigned(*p-L'0');if(n>max)n=max+1;}
    out=n;return true;
}
inline bool Hex6(const wchar_t* v,uint32_t& out){
    uint32_t n=0;
    for(unsigned i=0;i<6;++i){
        const wchar_t c=v[i];unsigned d;
        if(c>=L'0'&&c<=L'9')d=unsigned(c-L'0');else if(c>=L'a'&&c<=L'f')d=unsigned(c-L'a'+10);
        else if(c>=L'A'&&c<=L'F')d=unsigned(c-L'A'+10);else return false;
        n=n<<4|d;
    }
    if(v[6])return false;
    out=n;return true;
}
// Move names (MoveName), comma separated, any order (spaces around a name allowed). false: empty or an unknown name;
// `wanted` is then left as it was.
inline bool ParseMoves(const wchar_t* v,bool (&wanted)[kMoves]){
    static const wchar_t* const names[kMoves]={L"spin",L"great_spin",L"back_slice",L"ending",L"slash"};
    bool w[kMoves]={};bool any=false;
    const wchar_t* p=v;
    while(true){
        while(*p==L' '||*p==L'\t')++p;
        const wchar_t* start=p;
        while(*p&&*p!=L','&&*p!=L' '&&*p!=L'\t')++p;
        const size_t len=size_t(p-start);
        while(*p==L' '||*p==L'\t')++p;
        unsigned i=0;
        while(i<kMoves&&!(std::wcslen(names[i])==len&&!std::wcsncmp(start,names[i],len)))++i;
        if(i==kMoves)return false;
        w[i]=true;any=true;
        if(!*p)break;
        if(*p!=L',')return false;
        ++p;
    }
    if(!any)return false;
    for(unsigned i=0;i<kMoves;++i)wanted[i]=w[i];
    return true;
}
// get(key, out, count) fills `out` with the raw INI value ("" when missing).
template<class Get> Config ReadConfig(Get&& get){
    Config c;wchar_t v[64]{};
    auto read=[&](const wchar_t* key){v[0]=0;get(key,v,sizeof(v)/sizeof(v[0]));return v[0]!=0;};
    if(read(L"SwordTrail")){
        if(v[0]==L'0'&&!v[1])c.on=false;
        else if(!(v[0]==L'1'&&!v[1]))++c.problems;
    }
    if(read(L"SwordTrailMoves")){
        bool w[kMoves]={c.spin,c.great_spin,c.back_slice,c.ending,c.slash};
        if(ParseMoves(v,w)){c.spin=w[0];c.great_spin=w[1];c.back_slice=w[2];c.ending=w[3];c.slash=w[4];}else ++c.problems;
    }
    // Game ms of trail per move, clamped 60..350 (the 64-sample ring).
    auto length=[&](const wchar_t* key,unsigned& out){
        if(!read(key))return;
        unsigned n=0;
        if(Digits(v,100000,n))out=n<60?60:n>350?350:n;else ++c.problems;
    };
    length(L"SwordTrailLengthMs",c.length_ms);
    if(read(L"SwordTrailInner")){
        wchar_t* end=nullptr;const double d=std::wcstod(v,&end);
        if(end!=v&&!*end&&std::isfinite(d))c.inner=d<0?0:d>0.9?0.9:d;else ++c.problems;
    }
    auto colour=[&](const wchar_t* key,uint32_t& out){if(read(key)&&!Hex6(v,out))++c.problems;};
    auto alpha=[&](const wchar_t* key,uint8_t& out){
        if(!read(key))return;
        unsigned n=0;if(Digits(v,255,n)&&n<=255)out=uint8_t(n);else ++c.problems;
    };
    colour(L"SwordTrailColor",c.color);alpha(L"SwordTrailAlpha",c.alpha);
    colour(L"SwordTrailGreatColor",c.great_color);alpha(L"SwordTrailGreatAlpha",c.great_alpha);
    colour(L"SwordTrailBackColor",c.back_color);alpha(L"SwordTrailBackAlpha",c.back_alpha);length(L"SwordTrailBackLengthMs",c.back_length_ms);
    colour(L"SwordTrailEndingColor",c.ending_color);alpha(L"SwordTrailEndingAlpha",c.ending_alpha);
    length(L"SwordTrailEndingLengthMs",c.ending_length_ms);
    colour(L"SwordTrailSlashColor",c.slash_color);alpha(L"SwordTrailSlashAlpha",c.slash_alpha);
    length(L"SwordTrailSlashLengthMs",c.slash_length_ms);
    return c;
}
// The listed moves, comma separated, in MoveName order ("" when none). The snprintf contract: <0 or >= size = cut.
inline int FormatMoves(char* out,size_t size,const Config& c){
    int n=0;
    if(size)out[0]=0;
    for(unsigned i=0;i<kMoves;++i){
        if(!c.Listed(Move(i)))continue;
        const int k=std::snprintf(size_t(n)<size?out+n:nullptr,size_t(n)<size?size-size_t(n):0,"%s%s",n?",":"",MoveName(Move(i)));
        if(k<0)return k;
        n+=k;
    }
    return n;
}

// Native knife slash (re4-decomp src/wep/pl_knife.cpp): the routine word at player+0xFC is r_no_0 | r_no_1<<8 |
// r_no_2<<16 | r_no_3<<24. The knife routine is r_no_0 0, r_no_1 0x0B; r_no_2 2 is fire: step 0 (knife_r3_fire00) starts
// the slash clip and sets step 1, step 1 (knife_r3_fire10) plays it with the blade hit checks and, two frames before
// its end, hands over to the set state (r_no_2 1, step 4). A held fire goes from there straight back to fire step 0.
// Live (13-oct trial-04 log): 0x01000B00 draw, 0x01010B00 hold, 0x01020B00 while firing, 0x01030B00 knife-down.
inline bool KnifeSlash(uint32_t routine){return (routine&0xFFFF)==0x0B00&&((routine>>16)&0xFF)==2;}
// One native slash at a time, from the routine read on each player dispatch (before the native routine runs).
class SlashWatch {
    bool active_=false;uint8_t step_=0;
public:
    enum class Step:uint8_t {Idle,Begin,Continue,End};
    bool Active() const {return active_;}
    void Reset(){active_=false;step_=0;}
    // eligible=false: no slash may run a trail now (our own clip or another mod action owns Link, not Link, not
    // gameplay, `slash` not wanted): an active one ends, none begins. Begin also covers a new slash right after the
    // last one (fire step 0 seen after step 1: the set-state frame was not observed); the caller ends the old one.
    Step Next(uint32_t routine,bool eligible){
        const bool in=eligible&&KnifeSlash(routine);const uint8_t step=uint8_t(routine>>24);
        if(!in){const bool was=active_;Reset();return was?Step::End:Step::Idle;}
        const bool fresh=!active_||(step==0&&step_!=0);
        active_=true;step_=step;
        return fresh?Step::Begin:Step::Continue;
    }
};

// One trail at a time (game thread). Counters are atomics so the worker can log them.
class Trail {
    core::Ring ring_;core::Style style_{};Move move_=Move::Spin;
    uint64_t lease_=0;bool skip_=false,have_last_=false;core::Vec3 last_{};double last_ms_=0;
    uint32_t room_=0,player_=0;uint64_t identity_noted_=0;
    std::atomic<unsigned> moves_[kMoves]{},samples_{0},cuts_{0},identity_fails_{0};
    static double Distance(const core::Vec3& a,const core::Vec3& b){
        const double dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;return std::sqrt(dx*dx+dy*dy+dz*dz);
    }
public:
    unsigned Spins() const {return moves_[0].load()+moves_[1].load();}   // Spin + Great Spin (the 3.0.3 stats field)
    unsigned MoveCount(Move m) const {return unsigned(m)<kMoves?moves_[unsigned(m)].load():0;}
    Move CurrentMove() const {return move_;}
    unsigned Samples() const {return samples_.load();}
    unsigned Cuts() const {return cuts_.load();}
    unsigned IdentityFails() const {return identity_fails_.load();}
    uint64_t Lease() const {return lease_;}
    bool Live() const {return ring_.Count()>0;}
    const core::Style& CurrentStyle() const {return style_;}
    void Reset(){ring_.Reset();lease_=0;skip_=have_last_=false;}
    // A move starts. false: SwordTrail=0 or this move is not in SwordTrailMoves (nothing kept for it; the current trail
    // is left alone).
    bool Begin(const Config& c,uint64_t lease,Move m,uint32_t room,uint32_t player){
        if(!lease||unsigned(m)>=kMoves||!c.Wants(m))return false;
        ring_.Reset();   // the previous move's fading samples are dropped: never joined, never recoloured
        style_=c.Style(m);move_=m;lease_=lease;skip_=true;have_last_=false;room_=room;player_=player;
        moves_[unsigned(m)].fetch_add(1);return true;
    }
    bool Begin(const Config& c,uint64_t lease,bool great,uint32_t room,uint32_t player){
        return Begin(c,lease,great?Move::GreatSpin:Move::Spin,room,player);
    }
    // One blade read of the spin (now in game ms). ok=false: the read failed.
    void Sample(uint64_t lease,double now,bool ok,const core::Vec3& base,const core::Vec3& tip){
        if(!lease_||lease!=lease_)return;
        if(skip_){skip_=false;return;}
        if(!ok){ring_.Cut(now);have_last_=false;cuts_.fetch_add(1);return;}
        if(have_last_&&!(Distance(base,last_)<=JumpLimit(now-last_ms_))){ring_.Cut(now);cuts_.fetch_add(1);}
        ring_.Push(now,base,tip);last_=base;last_ms_=now;have_last_=core::Finite(base)&&core::Finite(tip);
        samples_.fetch_add(1);
    }
    void End(uint64_t lease){if(!lease_||lease!=lease_)return;ring_.Cut(0);lease_=0;have_last_=false;}
    // At a spin's end: was its sword ever identified (sword_trail_log.h Tracker)? Never = no samples and no ribbon for
    // the whole spin (SwordInHand=0, so the hand holds no known sword model, an unknown sword, or every read failed).
    // Counted once per spin lease, independent of End and of a room/player reset.
    void SpinIdentity(uint64_t lease,bool identified){
        if(!lease||lease==identity_noted_)return;
        identity_noted_=lease;
        if(!identified)identity_fails_.fetch_add(1);
    }
    // This frame's strip (oldest first, the newest kept over max). 0: nothing to register.
    int Frame(double now,uint32_t room,uint32_t player,bool gameplay,core::Quad* out,int max){
        if(!Live())return 0;
        if(room!=room_||player!=player_){Reset();return 0;}
        // Game time ran backwards (GameNow is a signed delta, e.g. a frame-counter reset): ages would go negative,
        // Expire would never drop and AlphaAt would clamp to full, freezing an opaque ribbon. Drop the history.
        if(now<ring_.At(ring_.Count()-1).t-kBackwardsMs){Reset();return 0;}
        if(!gameplay)return 0;
        ring_.Expire(now,style_.length_ms);
        return core::BuildStrip(ring_,now,style_,out,max);
    }
};
// The per-move line the worker logs after the stats line.
inline int FormatMoveCounts(char* out,size_t size,const Trail& t){
    return std::snprintf(out,size,"sword_trail moves spin=%u great=%u back=%u ending=%u slash=%u",t.MoveCount(Move::Spin),
        t.MoveCount(Move::GreatSpin),t.MoveCount(Move::Back),t.MoveCount(Move::Ending),t.MoveCount(Move::Slash));
}
} // namespace link_runtime::sword_trail::feed

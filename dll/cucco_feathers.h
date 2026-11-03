#pragma once
// Cucco feathers (F-11, 3.5.0; docs/features-3.5.0-consenso.md). Pure: the INI keys, the em28 (chicken, a Cucco since
// the chicken-01 layer) watch, the hit rate, the distance volume, the PCM gain and the log lines; no game access. The
// glue (cucco_feathers_runtime.h, run first in ParticlesFrame) reads the em manager, emits TP's ZI_S_NI_HANE_WHITE
// through the sword particle system (sword_particles.h) and asks the poll thread for TP's Z2SE_CHICKEN_CRY and
// Z2SE_CHICKEN_WING (Bin32/link_cucco_cry.wav, link_cucco_wing.wav, scripts/cucco_sounds.py, SHA-pinned).
//
// TP (d_a_ni.cpp:55-62 hane_set, 176-185): on a hit the Cucco sets its feather emitter for one frame with setRate(15) and
// setDirectionalSpeed(30) after a sword hit (mHitType 1), setRate(10) after any other; the native egg, death and AI are
// untouched here (read only).
//
// INI, [LinkRuntime]:
//   CuccoFeathers                exact 1 = on; anything else = off (no reads, no files, no devices). A value other than
//                                0 or empty is counted in `problems`.
//   CuccoFeathersParticles       missing or 1 = the feathers (needs SwordTrail=1 and SwordParticles=1: the particle
//                                system), 0 = sound only (plan B1)
//   CuccoFeathersSound           missing or 1 = cry and wing, 0 = feathers only, the game's own sound (plan B2)
//   CuccoFeathersVolumePercent   0..100 (default 80): the sounds' volume at Link's position; linear to 0 at 20 m
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace link_runtime::cucco_feathers {
constexpr uint8_t kCuccoId=0x28;             // cModel::id of the chicken (scripts/live/animals_live.py)
constexpr double kCooldownMs=300.0;          // one burst per bird every 300 ms
constexpr unsigned kRescanFrames=30;         // the bird list is rebuilt every 30 frames and on a room change
constexpr double kHearMm=20000.0;            // silent from 20 m
constexpr unsigned kMaxBirds=16;
constexpr unsigned kMaxSlots=256;
constexpr double kRateSword=15.0,kRateOther=10.0,kDirSpeed=30.0;   // hane_set(15 | 10, 30)
constexpr uint8_t kSwordWeapon=0x10;         // the knife record Link's sword leaves (sword_particles.h kSwordWeapon)
constexpr unsigned kDefaultVolume=80;
// cEm base layout (mortal_draw_target.h layout): valid when (be_flag & 0x201) == 1, pos XYZ, id, signed int16 HP, the
// weapon byte of the last cDmgInfo (published at +0x328 by 0x1AEEF0).
namespace layout {
constexpr uint32_t be_flag=0x004,pos=0x094,id=0x100,hp=0x324,weapon=0x32E,span=0x330;
}
constexpr const char* kEffectName="ZI_S_NI_HANE_WHITE";
constexpr const char* kCryFile="link_cucco_cry.wav";
constexpr const char* kWingFile="link_cucco_wing.wav";

// ---- INI ------------------------------------------------------------------------------------------------------------
struct Config {bool on=false,particles=true,sound=true;unsigned volume=kDefaultVolume;unsigned problems=0;};
// get(key, out, count) fills `out` with the raw INI value ("" when missing).
template<class Get> Config ReadConfig(Get&& get){
    Config c;wchar_t v[32]{};
    auto read=[&](const wchar_t* key){v[0]=0;get(key,v,sizeof(v)/sizeof(v[0]));return v[0]!=0;};
    if(read(L"CuccoFeathers")){
        if(v[0]==L'1'&&!v[1])c.on=true;else if(!(v[0]==L'0'&&!v[1]))++c.problems;   // only the exact 1 turns it on
    }
    auto flag=[&](const wchar_t* key,bool& out){
        if(!read(key))return;
        if(v[0]==L'0'&&!v[1])out=false;else if(v[0]==L'1'&&!v[1])out=true;else ++c.problems;
    };
    flag(L"CuccoFeathersParticles",c.particles);flag(L"CuccoFeathersSound",c.sound);
    if(read(L"CuccoFeathersVolumePercent")){
        wchar_t* end=nullptr;const unsigned long x=std::wcstoul(v,&end,10);
        if(end!=v&&!*end&&v[0]>=L'0'&&v[0]<=L'9'&&x<=100)c.volume=unsigned(x);else ++c.problems;
    }
    return c;
}

// ---- one bird ---------------------------------------------------------------------------------------------------------
struct Bird {bool valid=false;uint8_t id=0,weapon=0;int16_t hp=0;double x=0,y=0,z=0;};
// One read of a slot's first layout::span bytes. false: a slot whose position is not finite (never a bird).
inline bool ParseBird(const unsigned char* b,Bird& out){
    out=Bird{};
    if(!b)return false;
    uint32_t be=0;float pos[3]{};int16_t hp=0;
    std::memcpy(&be,b+layout::be_flag,4);std::memcpy(pos,b+layout::pos,12);std::memcpy(&hp,b+layout::hp,2);
    out.valid=(be&0x201u)==1u;out.id=b[layout::id];out.hp=hp;out.weapon=b[layout::weapon];
    out.x=pos[0];out.y=pos[1];out.z=pos[2];
    return std::isfinite(out.x)&&std::isfinite(out.y)&&std::isfinite(out.z);
}
inline bool IsCucco(const Bird& b){return b.valid&&b.id==kCuccoId;}
// TP's rate: 15 after a sword hit, 10 after a bullet, a kick or anything else.
inline double RateFor(uint8_t weapon){return weapon==kSwordWeapon?kRateSword:kRateOther;}

// ---- the watch --------------------------------------------------------------------------------------------------------
enum class Why:uint8_t {Hit,Dead,Gone};
inline const char* WhyName(Why w){return w==Why::Hit?"hit":w==Why::Dead?"dead":"gone";}
struct Burst {unsigned slot=0;int hp_from=0,hp_to=0;Why why=Why::Hit;double x=0,y=0,z=0;uint8_t weapon=0;};
// Per tracked bird: the last HP and position, its last burst (game ms) and whether its death already burst. A drop of HP
// is a hit (why=hit) or the killing hit (why=dead, once); a tracked live bird whose slot stops being a valid em28 is gone
// (why=gone, at its last position). Never a burst: the first sight of a bird (its baseline), a HP rise, a room change, a
// gap in the observation (Reset: menus, events, loads), or two or more birds gone in the same frame (an unload: `mass`).
// A burst within kCooldownMs of the same bird's last one is dropped (`cooled`).
class Watch {
    struct Entry {bool used=false,dead=false;unsigned slot=0;int16_t hp=0;double x=0,y=0,z=0,last_ms=0;bool burst=false;};
    Entry e_[kMaxBirds]{};unsigned since_scan_=kRescanFrames;bool room_known_=false;uint32_t room_=0;
    std::atomic<unsigned> cooled_{0},mass_{0},rooms_{0};   // counters: the worker's stats line reads them
    Entry* Find(unsigned slot){for(auto& e:e_)if(e.used&&e.slot==slot)return &e;return nullptr;}
    bool Cool(Entry& e,double now){
        if(e.burst&&now>=e.last_ms&&now-e.last_ms<kCooldownMs){cooled_.fetch_add(1);return false;}
        e.burst=true;e.last_ms=now;return true;
    }
public:
    void Reset(){for(auto& e:e_)e=Entry{};since_scan_=kRescanFrames;}
    // The room of this frame: a different one clears every bird without a burst. true when it changed.
    bool Room(uint32_t room){
        if(room_known_&&room==room_)return false;
        const bool changed=room_known_;room_known_=true;room_=room;Reset();if(changed)rooms_.fetch_add(1);
        return changed;
    }
    bool ScanDue() const {return since_scan_>=kRescanFrames;}
    unsigned Count() const {unsigned n=0;for(const auto& e:e_)if(e.used)++n;return n;}
    unsigned Cooled() const {return cooled_.load();}
    unsigned Mass() const {return mass_.load();}
    unsigned Rooms() const {return rooms_.load();}
    // The full scan's em28 slots (each with its bird): a slot already tracked keeps its baseline (Frame judges it); a new
    // one sets its baseline (no burst). Tracked slots missing from the list stay for Frame to judge (gone). Past
    // kMaxBirds, the rest are not tracked.
    void Rescan(const unsigned* slots,const Bird* birds,unsigned n){
        since_scan_=0;
        for(unsigned i=0;i<n&&slots&&birds;++i){
            if(slots[i]>=kMaxSlots||!IsCucco(birds[i])||Find(slots[i]))continue;
            Entry* spare=nullptr;for(auto& e:e_)if(!e.used){spare=&e;break;}
            if(!spare)break;
            const Bird& b=birds[i];
            *spare=Entry{};spare->used=true;spare->slot=slots[i];spare->hp=b.hp;spare->dead=b.hp<=0;spare->x=b.x;spare->y=b.y;spare->z=b.z;
        }
    }
    // One frame at game ms `now`: read(slot, Bird&) -> false when the slot cannot be read. The bursts go to `out` (up to
    // `max`); returns how many.
    template<class Read> unsigned Frame(double now,Read&& read,Burst* out,unsigned max){
        if(since_scan_<kRescanFrames)++since_scan_;
        unsigned n=0;Burst gone[kMaxBirds];unsigned gone_n=0;Entry* gone_e[kMaxBirds]{};
        for(auto& e:e_){
            if(!e.used)continue;
            Bird b;
            if(!read(e.slot,b)||!IsCucco(b)){
                if(!e.dead&&e.hp>0){gone[gone_n]={e.slot,e.hp,e.hp,Why::Gone,e.x,e.y,e.z,0};gone_e[gone_n++]=&e;}
                else e=Entry{};
                continue;
            }
            const int16_t was=e.hp;
            e.x=b.x;e.y=b.y;e.z=b.z;e.hp=b.hp;
            if(b.hp>=was){if(b.hp>0)e.dead=false;continue;}   // the same or more: a baseline (a new bird in the slot)
            Why why=Why::Hit;
            if(b.hp<=0){if(e.dead)continue;e.dead=true;why=Why::Dead;}
            if(!Cool(e,now)||n>=max||!out)continue;
            out[n++]={e.slot,was,b.hp,why,b.x,b.y,b.z,b.weapon};
        }
        if(gone_n>=2){mass_.fetch_add(1);for(unsigned i=0;i<gone_n;++i)*gone_e[i]=Entry{};}   // an unload, not a hit
        else if(gone_n==1){
            Entry& e=*gone_e[0];const bool fire=Cool(e,now);
            if(fire&&n<max&&out)out[n++]=gone[0];
            e=Entry{};
        }
        return n;
    }
};

// ---- sound --------------------------------------------------------------------------------------------------------
// The distance volume: 1 at Link, linear to 0 at kHearMm (and beyond, or not finite).
inline double Gain(double distance_mm){
    if(!std::isfinite(distance_mm)||distance_mm<0)return 0.0;
    return distance_mm>=kHearMm?0.0:1.0-distance_mm/kHearMm;
}
inline double Distance(double ax,double ay,double az,double bx,double by,double bz){
    const double dx=ax-bx,dy=ay-by,dz=az-bz;return std::sqrt(dx*dx+dy*dy+dz*dz);
}
// The request the game thread hands the poll thread: 0 = none, else 1 + the volume in permille (0..1000).
inline unsigned Request(double gain,unsigned volume_percent){
    if(!std::isfinite(gain)||gain<=0)return 0;
    const double g=(gain>1?1:gain)*double(volume_percent>100?100:volume_percent)/100.0;
    const long permille=std::lround(g*1000.0);
    return permille<=0?0u:1u+unsigned(permille);
}
inline double RequestGain(unsigned request){return request?double(request-1)/1000.0:0.0;}
// 16-bit PCM x gain (0..1), rounded, into `out` (the same count).
inline void ScalePcm(const int16_t* in,int16_t* out,size_t count,double gain){
    if(!in||!out)return;
    const double g=std::isfinite(gain)?(gain<0?0:gain>1?1:gain):0.0;
    for(size_t i=0;i<count;++i){
        const long v=std::lround(double(in[i])*g);
        out[i]=int16_t(v<-32768?-32768:v>32767?32767:v);
    }
}

// ---- log lines (each returns the snprintf length; <= 0 or >= size: not written whole) -------------------------------
inline int FormatReady(char* out,size_t size,bool particles,bool cry,bool wing,unsigned volume){
    return std::snprintf(out,size,"cucco_feathers ready particles=%d cry=%d wing=%d effect=%s volume=%u hear_m=%.0f cooldown_ms=%.0f",
        particles?1:0,cry?1:0,wing?1:0,kEffectName,volume,kHearMm/1000.0,kCooldownMs);
}
inline int FormatBurst(char* out,size_t size,const Burst& b,unsigned emitted,double distance,unsigned request){
    return std::snprintf(out,size,"cucco_feathers burst em=28 slot=%u hp=%d->%d why=%s n=%u weapon=0x%02X at=%.0f,%.0f,%.0f dist=%.0f vol=%u",
        b.slot,b.hp_from,b.hp_to,WhyName(b.why),emitted,unsigned(b.weapon),b.x,b.y,b.z,distance,request?request-1:0u);
}
inline int FormatWatch(char* out,size_t size,uint32_t room,unsigned birds){
    return std::snprintf(out,size,"cucco_feathers watch room=0x%X birds=%u",room,birds);
}
inline int FormatStats(char* out,size_t size,unsigned bursts,unsigned cooled,unsigned mass,unsigned sounds){
    return std::snprintf(out,size,"cucco_feathers stats bursts=%u cooled=%u mass=%u sounds=%u",bursts,cooled,mass,sounds);
}
} // namespace link_runtime::cucco_feathers

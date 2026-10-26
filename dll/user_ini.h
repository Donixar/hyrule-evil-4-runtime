#pragma once
// Live cosmetic overrides (user-ini-01, 22-oct; docs/panel-ajustes-2026-10-22.md). Pure: the override file's lookup,
// the merge over the main INI, the cosmetic snapshot, what changed between two snapshots, what a reload may turn on,
// the worker -> game thread handoff and the log lines; no file or game access (the glue in link_runtime.cpp reads the
// files, polls the stamp and applies the snapshot on the game thread).
//
// Bin32/link_runtime.user.ini, section [LinkRuntime], same Key=Value format as link_runtime.ini. Each key it carries
// (with a non-empty value) wins over the main INI's; an invalid override keeps the code default and counts in
// `problems`, as an invalid main INI value does. Only these keys are live (read at startup and on every change of the
// file); every other key, gameplay included, stays startup-only and is ignored in the override file:
//   sword trail     SwordTrail, SwordTrailMoves, SwordTrailLengthMs, SwordTrailInner, SwordTrail{,Great,Back,Ending,
//                   Slash}Color / Alpha, SwordTrail{Back,Ending,Slash}LengthMs (sword_trail_feed.h)
//   charge glow     SwordCharge, SwordChargeAlpha, SwordChargeSound (sword_charge.h)
//   particles       SwordParticles, SwordParticlesHit / Wall / Spin, SwordParticlesGain, SwordParticlesAmount,
//                   SwordParticlesDust, SwordParticlesWoodTypes (sword_particles.h)
// The Z-target 3D arrow (ztarget_arrow3d.h) has no INI look keys (its colours, size and spin are constants).
// A feature that did not start (its key off at startup, its pins or its gate failed, the charge WAV missing, the wall
// pins failed) is never turned on by a reload: Admit keeps it off and names the key in `refused`.
#include "sword_trail_feed.h"
#include "sword_charge.h"
#include "sword_particles.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace link_runtime::user_ini {
inline constexpr const wchar_t* kFile=L"link_runtime.user.ini";
inline constexpr const wchar_t* kSectionName=L"LinkRuntime";
constexpr size_t kSectionChars=32768;   // GetPrivateProfileSectionW's documented maximum section (32767) + 1
constexpr uint64_t kPollMs=1000;        // the override file's stamp is checked at most once per second

// `main` with its file name replaced by `file` (after the last \ or /). false: no directory part or it does not fit
// (`out` is then empty).
inline bool SiblingPath(const wchar_t* main,const wchar_t* file,wchar_t* out,size_t count){
    if(!out||!count)return false;
    out[0]=0;
    if(!main||!file)return false;
    const wchar_t* slash=nullptr;
    for(const wchar_t* p=main;*p;++p)if(*p==L'\\'||*p==L'/')slash=p;
    if(!slash)return false;
    const size_t dir=size_t(slash-main)+1,name=std::wcslen(file);
    if(dir+name+1>count)return false;
    for(size_t i=0;i<dir;++i)out[i]=main[i];
    for(size_t i=0;i<=name;++i)out[dir+i]=file[i];
    return true;
}

// One section as GetPrivateProfileSectionW returns it: entries "key=value", each ending in a null, the API's return
// value = the characters before the final null (comment lines are already dropped; spaces around key and value are
// already trimmed, trimmed again here). Find follows GetPrivateProfileStringW on the same file: the key compares
// without case (ASCII), the first entry with that key decides, one pair of enclosing quotes ("" or '') is stripped,
// the value is cut to count - 1 characters. An entry without '=' or with an empty key is skipped. An empty value is
// "missing" (false), as ReadConfig reads "" (so an empty override falls back to the main INI).
class Section {
    const wchar_t* text_=nullptr;size_t chars_=0;
    static wchar_t Fold(wchar_t c){return c>=L'A'&&c<=L'Z'?wchar_t(c+(L'a'-L'A')):c;}
    static bool Space(wchar_t c){return c==L' '||c==L'\t';}
public:
    Section()=default;
    Section(const wchar_t* text,size_t chars):text_(text),chars_(text?chars:0){}
    size_t Chars() const {return chars_;}
    bool Find(const wchar_t* key,wchar_t* out,size_t count) const {
        if(out&&count)out[0]=0;
        if(!key||!*key||!out||!count||!text_)return false;
        const size_t want=std::wcslen(key);
        size_t i=0;
        while(i<chars_){
            size_t end=i;
            while(end<chars_&&text_[end])++end;
            size_t eq=i;
            while(eq<end&&text_[eq]!=L'=')++eq;
            if(eq<end){
                size_t ks=i,ke=eq;
                while(ks<ke&&Space(text_[ks]))++ks;
                while(ke>ks&&Space(text_[ke-1]))--ke;
                bool same=ke-ks==want&&want>0;
                for(size_t k=0;same&&k<want;++k)same=Fold(text_[ks+k])==Fold(key[k]);
                if(same){
                    size_t vs=eq+1,ve=end;
                    while(vs<ve&&Space(text_[vs]))++vs;
                    while(ve>vs&&Space(text_[ve-1]))--ve;
                    if(ve-vs>=2&&(text_[vs]==L'"'||text_[vs]==L'\'')&&text_[ve-1]==text_[vs]){++vs;--ve;}
                    if(ve==vs)return false;
                    size_t n=ve-vs;if(n>count-1)n=count-1;
                    for(size_t k=0;k<n;++k)out[k]=text_[vs+k];
                    out[n]=0;return n>0;   // count 1: nothing fits, as missing
                }
            }
            i=end+1;
        }
        return false;
    }
};
// ReadConfig's get over the override and the main INI: the override's value when it has the key, else main(key, out,
// count) (a Section's Find, or GetPrivateProfileStringW when the main section did not fit).
template<class Main> void GetMerged(const Section& user,Main&& main,const wchar_t* key,wchar_t* out,size_t count){
    if(!out||!count)return;
    out[0]=0;
    if(user.Find(key,out,count))return;
    out[0]=0;
    main(key,out,count);
}

// The live configs, whole.
struct Cosmetic {
    link_runtime::sword_trail::feed::Config trail;link_runtime::sword_charge::Config charge;
    link_runtime::sword_particles::Config particles;
};
// get(key, out, count) fills `out` with the raw value ("" when missing), as each ReadConfig expects.
template<class Get> Cosmetic Read(Get&& get){
    Cosmetic c;
    c.trail=link_runtime::sword_trail::feed::ReadConfig(get);
    c.charge=link_runtime::sword_charge::ReadConfig(get);
    c.particles=link_runtime::sword_particles::ReadConfig(get);
    return c;
}
inline unsigned Problems(const Cosmetic& c){return c.trail.problems+c.charge.problems+c.particles.problems;}

// The live keys, in INI order, with what each reads in the snapshot (two snapshots differ in a key when its parsed
// value differs: a clamped or invalid value compares as what the runtime uses).
using Differs=bool(*)(const Cosmetic&,const Cosmetic&);
struct Key {const char* name;Differs differs;};
inline constexpr Key kKeys[]={
    {"SwordTrail",[](const Cosmetic& a,const Cosmetic& b){return a.trail.on!=b.trail.on;}},
    {"SwordTrailMoves",[](const Cosmetic& a,const Cosmetic& b){
        return a.trail.spin!=b.trail.spin||a.trail.great_spin!=b.trail.great_spin||a.trail.back_slice!=b.trail.back_slice||
               a.trail.ending!=b.trail.ending||a.trail.slash!=b.trail.slash;}},
    {"SwordTrailLengthMs",[](const Cosmetic& a,const Cosmetic& b){return a.trail.length_ms!=b.trail.length_ms;}},
    {"SwordTrailInner",[](const Cosmetic& a,const Cosmetic& b){return a.trail.inner!=b.trail.inner;}},
    {"SwordTrailColor",[](const Cosmetic& a,const Cosmetic& b){return a.trail.color!=b.trail.color;}},
    {"SwordTrailAlpha",[](const Cosmetic& a,const Cosmetic& b){return a.trail.alpha!=b.trail.alpha;}},
    {"SwordTrailGreatColor",[](const Cosmetic& a,const Cosmetic& b){return a.trail.great_color!=b.trail.great_color;}},
    {"SwordTrailGreatAlpha",[](const Cosmetic& a,const Cosmetic& b){return a.trail.great_alpha!=b.trail.great_alpha;}},
    {"SwordTrailBackColor",[](const Cosmetic& a,const Cosmetic& b){return a.trail.back_color!=b.trail.back_color;}},
    {"SwordTrailBackAlpha",[](const Cosmetic& a,const Cosmetic& b){return a.trail.back_alpha!=b.trail.back_alpha;}},
    {"SwordTrailBackLengthMs",[](const Cosmetic& a,const Cosmetic& b){return a.trail.back_length_ms!=b.trail.back_length_ms;}},
    {"SwordTrailEndingColor",[](const Cosmetic& a,const Cosmetic& b){return a.trail.ending_color!=b.trail.ending_color;}},
    {"SwordTrailEndingAlpha",[](const Cosmetic& a,const Cosmetic& b){return a.trail.ending_alpha!=b.trail.ending_alpha;}},
    {"SwordTrailEndingLengthMs",[](const Cosmetic& a,const Cosmetic& b){return a.trail.ending_length_ms!=b.trail.ending_length_ms;}},
    {"SwordTrailSlashColor",[](const Cosmetic& a,const Cosmetic& b){return a.trail.slash_color!=b.trail.slash_color;}},
    {"SwordTrailSlashAlpha",[](const Cosmetic& a,const Cosmetic& b){return a.trail.slash_alpha!=b.trail.slash_alpha;}},
    {"SwordTrailSlashLengthMs",[](const Cosmetic& a,const Cosmetic& b){return a.trail.slash_length_ms!=b.trail.slash_length_ms;}},
    {"SwordCharge",[](const Cosmetic& a,const Cosmetic& b){return a.charge.on!=b.charge.on;}},
    {"SwordChargeAlpha",[](const Cosmetic& a,const Cosmetic& b){return a.charge.alpha!=b.charge.alpha;}},
    {"SwordChargeSound",[](const Cosmetic& a,const Cosmetic& b){return a.charge.sound!=b.charge.sound;}},
    {"SwordParticles",[](const Cosmetic& a,const Cosmetic& b){return a.particles.on!=b.particles.on;}},
    {"SwordParticlesHit",[](const Cosmetic& a,const Cosmetic& b){return a.particles.hit!=b.particles.hit;}},
    {"SwordParticlesWall",[](const Cosmetic& a,const Cosmetic& b){return a.particles.wall!=b.particles.wall;}},
    {"SwordParticlesSpin",[](const Cosmetic& a,const Cosmetic& b){return a.particles.spin!=b.particles.spin;}},
    {"SwordParticlesGain",[](const Cosmetic& a,const Cosmetic& b){return a.particles.gain!=b.particles.gain;}},
    {"SwordParticlesAmount",[](const Cosmetic& a,const Cosmetic& b){return a.particles.amount!=b.particles.amount;}},
    {"SwordParticlesDust",[](const Cosmetic& a,const Cosmetic& b){return a.particles.dust!=b.particles.dust;}},
    {"SwordParticlesWoodTypes",[](const Cosmetic& a,const Cosmetic& b){return a.particles.wood_types!=b.particles.wood_types;}},
};
constexpr unsigned kKeyCount=unsigned(sizeof(kKeys)/sizeof(kKeys[0]));

// Appends `name` to the comma list in `out` (size bytes, always terminated). A name that does not fit whole is
// replaced by "+" (when that fits): the list never ends in a cut name.
inline void Append(char* out,size_t size,size_t& used,const char* name){
    if(!out||!size||!name)return;
    const size_t n=std::strlen(name),sep=used?1:0;
    if(used+sep+n+1<=size){
        if(sep)out[used++]=',';
        for(size_t i=0;i<n;++i)out[used++]=name[i];
        out[used]=0;return;
    }
    if(used+sep+2<=size&&!(used&&out[used-1]=='+')){if(sep)out[used++]=',';out[used++]='+';out[used]=0;}
}
// The keys whose parsed value differs between `before` and `after`, comma separated in INI order ("-" when none).
// Returns how many differ.
inline unsigned Diff(const Cosmetic& before,const Cosmetic& after,char* out,size_t size){
    unsigned n=0;size_t used=0;
    if(out&&size)out[0]=0;
    for(const Key& k:kKeys)if(k.differs(before,after)){++n;Append(out,size,used,k.name);}
    if(!n&&out&&size>=2){out[0]='-';out[1]=0;}
    return n;
}
// How many live keys `user` carries (non-empty values).
inline unsigned Overrides(const Section& user){
    unsigned n=0;wchar_t key[40],v[8];
    for(const Key& k:kKeys){
        size_t i=0;for(;k.name[i]&&i+1<sizeof(key)/sizeof(key[0]);++i)key[i]=wchar_t(k.name[i]);
        key[i]=0;
        if(user.Find(key,v,sizeof(v)/sizeof(v[0])))++n;
    }
    return n;
}

// What started at startup (link_runtime.cpp StartSwordTrail / StartSwordCharge / StartSwordParticles).
struct Started {bool trail=false,charge=false,charge_sound=false,particles=false,particles_wall=false;};
// What a reload may apply: a feature that did not start stays off (its startup work, the drawer, the WAV or the
// wall pins, never happened), and each key in `want` that asks for one of them is named in `refused` ("-" when none).
// Behaviour-neutral for the runtime (each of these is gated by its startup flag anyway); it makes the log say why.
// Returns how many keys were refused.
inline unsigned Admit(Cosmetic& c,const Started& s,char* refused,size_t size){
    unsigned n=0;size_t used=0;
    if(refused&&size)refused[0]=0;
    auto refuse=[&](bool asked,const char* name){if(asked){++n;Append(refused,size,used,name);}};
    if(!s.trail){refuse(c.trail.on,"SwordTrail");c.trail.on=false;}
    if(!s.charge){refuse(c.charge.on,"SwordCharge");c.charge.on=false;c.charge.sound=false;}
    else if(!s.charge_sound){refuse(c.charge.on&&c.charge.sound,"SwordChargeSound");c.charge.sound=false;}
    if(!s.particles){refuse(c.particles.on,"SwordParticles");c.particles.on=false;c.particles.wall=false;}
    else if(!s.particles_wall){refuse(c.particles.on&&c.particles.wall,"SwordParticlesWall");c.particles.wall=false;}
    if(!n&&refused&&size>=2){refused[0]='-';refused[1]=0;}
    return n;
}

// The override file's stamp: re-read only when it differs from the last one seen (created, deleted, rewritten), and
// checked at most once per kPollMs.
class Watch {
    bool have_=false,exists_=false;uint64_t time_=0,size_=0,next_=0;
public:
    // The stamp read just before a read of the file (startup, or a reload).
    void Seen(bool exists,uint64_t time,uint64_t size){have_=true;exists_=exists;time_=exists?time:0;size_=exists?size:0;}
    bool Due(uint64_t now_ms){if(now_ms<next_)return false;next_=now_ms+kPollMs;return true;}
    // true (and remembered) when the stamp differs from the last one seen.
    bool Changed(bool exists,uint64_t time,uint64_t size){
        if(!exists)time=size=0;
        if(have_&&exists==exists_&&time==time_&&size==size_)return false;
        Seen(exists,time,size);return true;
    }
};

// Worker -> game thread, lock-free, one producer and one consumer: a triple buffer. Publish (the worker) writes its
// own back slot, then swaps it into `ready` with the fresh bit (release: the slot's bytes before the index); Take
// (the game thread) swaps its front slot with `ready` only when that holds a fresh snapshot (acquire) and copies the
// slot it got. The three slots are always split one each between the producer (back), `ready` and the consumer
// (front), so the producer never writes the slot being copied: no torn snapshot, no lock, no wait on either side. A
// snapshot published twice before a Take is replaced by the newer one (the consumer only wants the latest).
template<class T> class Mailbox {
    static constexpr uint32_t kFresh=4,kIndex=3;
    T slot_[3]{};std::atomic<uint32_t> ready_{0};uint32_t back_=1,front_=2;
public:
    void Publish(const T& v){
        slot_[back_]=v;
        back_=ready_.exchange(back_|kFresh,std::memory_order_acq_rel)&kIndex;
    }
    bool Take(T& out){
        if(!(ready_.load(std::memory_order_acquire)&kFresh))return false;
        front_=ready_.exchange(front_,std::memory_order_acq_rel)&kIndex;
        out=slot_[front_];return true;
    }
    bool Pending() const {return (ready_.load(std::memory_order_acquire)&kFresh)!=0;}
};

// Log lines. Each returns the snprintf length (<= 0 or >= size: not written whole).
inline int FormatReady(char* out,size_t size,bool file,unsigned overrides,unsigned problems,const char* refused){
    return std::snprintf(out,size,"user_ini ready file=%d overrides=%u problems=%u refused=%s live_keys=%u poll_ms=%u",file?1:0,
        overrides,problems,refused&&*refused?refused:"-",kKeyCount,unsigned(kPollMs));
}
inline int FormatReload(char* out,size_t size,unsigned n,const char* changed,unsigned problems,const char* refused,bool file,
                        unsigned overrides){
    return std::snprintf(out,size,"user_ini reload n=%u changed=%s problems=%u refused=%s file=%d overrides=%u",n,
        changed&&*changed?changed:"-",problems,refused&&*refused?refused:"-",file?1:0,overrides);
}
} // namespace link_runtime::user_ini

#pragma once
// ==== Cucco feathers (F-11, 3.5.0; docs/features-3.5.0-consenso.md) — runtime glue =====================================
// Included by spin_attack_runtime.h right before ParticlesFrame, whose first call is CuccoFrame (before its early return
// without a sword move: a Cucco can be shot or kicked with no sword out). It uses the combat state (CmbReadPlayer, CmbMem,
// g_cmb_*), the em manager reader (mtg::ReadManager), the trail's room (TrailRoom) and the shared TP particle system
// (g_part, g_part_on, g_part_cfg). link_runtime.cpp reads g_cucco_cfg from the INI, calls StartCuccoFeathers after
// StartSwordParticles and PollCuccoFeathers from the worker loop. Read only: nothing here writes the game; the egg, the
// death and the bird's AI stay native.
//
// Game thread (CuccoFrame, every player dispatch in gameplay): the em28 watch (cucco_feathers.h); on a burst, TP's
// ZI_S_NI_HANE_WHITE with hane_set's rate (15 sword, 10 other) and directional speed 30, its whole count, on the bird's
// position (+30 cm from the resource), and a sound request (1 + volume permille, latest wins) for the poll thread.
// Worker (PollCuccoFeathers): plays link_cucco_cry.wav and link_cucco_wing.wav on their own waveOut devices, scaled by
// the request's volume; the stats line every 5 s while it changes.
#include "cucco_feathers.h"

namespace ccf=link_runtime::cucco_feathers;
// scripts/cucco_sounds.py (TP Z2SE_CHICKEN_CRY variant 0, Z2SE_CHICKEN_WING; mono 16-bit 32 kHz, peak 0.8).
inline constexpr const char* kCuccoCrySha="667c328060463a48894e0ab2c853167265f8726b1241bde64a878ce7ccfd4f5b";
inline constexpr const char* kCuccoWingSha="da27b1478d4988e5912c25144fc0754b74d52b0122b8e136a8c5eeeab2c7aaea";
// One sound on its own waveOut device, SHA-pinned, played at a volume given per play (the PCM is scaled into the
// buffer after the device is reset; never while it plays).
class CuccoVoice {
    HWAVEOUT out_=nullptr;WAVEHDR header_{};std::vector<int16_t> base_,play_;
public:
    CuccoVoice()=default;CuccoVoice(const CuccoVoice&)=delete;CuccoVoice& operator=(const CuccoVoice&)=delete;
    ~CuccoVoice(){if(out_){waveOutReset(out_);waveOutUnprepareHeader(out_,&header_,sizeof(header_));waveOutClose(out_);}}
    bool Loaded() const {return out_!=nullptr;}
    bool Load(const wchar_t* path,const char* sha){
        std::vector<unsigned char> bytes;FILE* file=nullptr;
        if(_wfopen_s(&file,path,L"rb")||!file)return false;
        std::fseek(file,0,SEEK_END);const long length=std::ftell(file);std::rewind(file);
        if(length<44||length>1024*1024){std::fclose(file);return false;}
        bytes.resize(size_t(length));const size_t got=std::fread(bytes.data(),1,bytes.size(),file);std::fclose(file);
        unsigned char digest[32]{};namespace hash=link_runtime::mortal_draw::resident_assets::detail;
        link_runtime::WaveData wave;
        if(got!=bytes.size()||!hash::Hash(bytes.data(),bytes.size(),digest)||!hash::Matches(digest,sha)||
           !link_runtime::ParseWave(bytes,wave)||!wave.channels||wave.pcm.size()<2)return false;
        base_.resize(wave.pcm.size()/2);std::memcpy(base_.data(),wave.pcm.data(),base_.size()*2);play_=base_;
        WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=WORD(wave.channels);f.nSamplesPerSec=wave.rate;
        f.wBitsPerSample=16;f.nBlockAlign=WORD(wave.channels*2);f.nAvgBytesPerSec=f.nSamplesPerSec*f.nBlockAlign;
        if(waveOutOpen(&out_,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){out_=nullptr;return false;}
        header_.lpData=reinterpret_cast<LPSTR>(play_.data());header_.dwBufferLength=DWORD(play_.size()*2);
        if(waveOutPrepareHeader(out_,&header_,sizeof(header_))!=MMSYSERR_NOERROR){waveOutClose(out_);out_=nullptr;return false;}
        return true;
    }
    // Restarts the sound at `gain` (0..1) when it is still playing. Poll thread only.
    bool Play(double gain){
        if(!out_)return false;
        waveOutReset(out_);
        ccf::ScalePcm(base_.data(),play_.data(),play_.size(),gain);
        return waveOutWrite(out_,&header_,sizeof(header_))==MMSYSERR_NOERROR;
    }
};
static ccf::Config g_cucco_cfg;                 // from the INI before the hooks (link_runtime.cpp)
static std::atomic<bool> g_cucco_on{false};     // started (StartCuccoFeathers); off = CuccoFrame reads nothing
static bool g_cucco_particles=false;            // fixed at start: CuccoFeathersParticles and the particle system started
static ccf::Watch g_cucco_watch;                // game thread
static bool g_cucco_seen=false;                 // the last frame was observed in gameplay (else every baseline is stale)
static unsigned g_cucco_birds=~0u;              // the last logged bird count
static std::atomic<unsigned> g_cucco_bursts{0},g_cucco_request{0},g_cucco_sounds{0};
static CuccoVoice g_cucco_cry,g_cucco_wing;     // worker
static bool CuccoBird(CmbMem& m,const mtg::Manager& pool,unsigned slot,ccf::Bird& b){
    static unsigned char bytes[ccf::layout::span];
    if(slot>=pool.count||slot>=ccf::kMaxSlots||!m.Read(pool.array+slot*pool.block,bytes,sizeof(bytes)))return false;
    return ccf::ParseBird(bytes,b);
}
static void CuccoFrame(){
    if(!g_cucco_on.load(std::memory_order_acquire))return;
    CmbPlayer p;
    if(!CmbReadPlayer(p)||!p.facts.gameplay){g_cucco_seen=false;return;}
    CmbMem m;mtg::Manager pool;
    if(mtg::ReadManager(m,g_cmb_base,mtg::Config{},pool)!=mtg::Reject::None){g_cucco_seen=false;return;}
    const uint32_t room=TrailRoom();
    if(!g_cucco_seen)g_cucco_watch.Reset();   // a gap (menu, case, event, load): baselines again, never a burst
    g_cucco_seen=true;
    g_cucco_watch.Room(room);                  // a new room: cleared, no burst
    char line[sizeof(CmbLine::text)];
    if(g_cucco_watch.ScanDue()){
        unsigned slots[ccf::kMaxBirds];ccf::Bird birds[ccf::kMaxBirds];unsigned n=0;
        const unsigned count=pool.count<ccf::kMaxSlots?pool.count:ccf::kMaxSlots;
        for(unsigned i=0;i<count&&n<ccf::kMaxBirds;++i){ccf::Bird b;if(CuccoBird(m,pool,i,b)&&ccf::IsCucco(b)){slots[n]=i;birds[n++]=b;}}
        g_cucco_watch.Rescan(slots,birds,n);
        const unsigned tracked=g_cucco_watch.Count();
        if(tracked!=g_cucco_birds){g_cucco_birds=tracked;if(ccf::FormatWatch(line,sizeof(line),room,tracked)>0)CmbLog("%s",line);}
    }
    const double now=link_runtime::sword_trail_log::Ms(GameNow(g_cmb_global_slot));
    ccf::Burst out[4];
    const unsigned bursts=g_cucco_watch.Frame(now,[&](unsigned slot,ccf::Bird& b){return CuccoBird(m,pool,slot,b);},out,4);
    for(unsigned i=0;i<bursts;++i){
        const ccf::Burst& b=out[i];
        unsigned emitted=0;
        if(g_cucco_particles&&g_part_on.load(std::memory_order_acquire)&&g_part_cfg.on){
            g_part.Advance(now,room,p.player);   // Emit starts on this frame's tick (ParticlesFrame advances after: same now)
            const unsigned before=g_part.Emitted();
            spt::Placement at;at.origin={b.x,b.y,b.z};at.rate=ccf::RateFor(b.weapon);at.dir_speed=ccf::kDirSpeed;at.full=true;
            g_part.Emit(spt::Effect::FeatherWhite,at);
            emitted=g_part.Emitted()-before;
        }
        const double distance=ccf::Distance(b.x,b.y,b.z,p.pos.x,p.pos.y,p.pos.z);
        const unsigned request=g_cucco_cfg.sound?ccf::Request(ccf::Gain(distance),g_cucco_cfg.volume):0u;
        if(request)g_cucco_request.store(request,std::memory_order_release);
        g_cucco_bursts.fetch_add(1);
        if(ccf::FormatBurst(line,sizeof(line),b,emitted,distance,request)>0)CmbLog("%s",line);
    }
}
// Worker, after StartSwordParticles (the feathers need the particle system started). `directory` ends with a backslash.
static void StartCuccoFeathers(bool dispatch_up,const wchar_t* directory){
    if(!g_cucco_cfg.on){
        if(g_cucco_cfg.problems)Log("cucco_feathers off: CuccoFeathers takes only the exact 1 (%u invalid values)",g_cucco_cfg.problems);
        return;
    }
    if(!dispatch_up||!g_cmb_base||!g_cmb_player_slot||!g_cmb_global_slot){
        Log("cucco_feathers off: needs the player dispatch and the combat runtime (its player and em manager reads)");return;
    }
    bool cry=false,wing=false;
    if(g_cucco_cfg.sound&&directory){
        wchar_t path[MAX_PATH]{};
        cry=link_runtime::paths::Format(path,L"%lslink_cucco_cry.wav",directory)>0&&g_cucco_cry.Load(path,kCuccoCrySha);
        wing=link_runtime::paths::Format(path,L"%lslink_cucco_wing.wav",directory)>0&&g_cucco_wing.Load(path,kCuccoWingSha);
    }
    g_cucco_particles=g_cucco_cfg.particles&&g_part_on.load(std::memory_order_acquire);
    g_cucco_on.store(true,std::memory_order_release);
    char line[256];
    if(ccf::FormatReady(line,sizeof(line),g_cucco_particles,cry,wing,g_cucco_cfg.volume)>0)Log("%s",line);
    if(!g_cucco_cfg.particles)Log("cucco_feathers particles off: CuccoFeathersParticles=0 (plan B1: sound only)");
    else if(!g_cucco_particles)Log("cucco_feathers particles off: SwordParticles is not running (needs SwordTrail=1 and SwordParticles=1); sound only");
    if(!g_cucco_cfg.sound)Log("cucco_feathers sound off: CuccoFeathersSound=0 (plan B2: feathers with the game's own sound)");
    else{
        if(!cry)Log("cucco_feathers sound: Bin32/link_cucco_cry.wav missing or changed (that sound only is muted)");
        if(!wing)Log("cucco_feathers sound: Bin32/link_cucco_wing.wav missing or changed (that sound only is muted)");
    }
    if(g_cucco_cfg.problems)Log("cucco_feathers: %u invalid values; their defaults kept",g_cucco_cfg.problems);
}
static uint64_t g_cucco_next=0;
static unsigned g_cucco_last_sum=0;   // nothing happened yet: no line
// Worker loop: the latest sound request (both sounds at its volume), the stats every 5 s while they change.
static void PollCuccoFeathers(){
    if(!g_cucco_on.load(std::memory_order_acquire))return;
    const unsigned request=g_cucco_request.exchange(0,std::memory_order_acq_rel);
    if(request){
        const double gain=ccf::RequestGain(request);
        const bool a=g_cucco_cry.Play(gain),b=g_cucco_wing.Play(gain);
        if(a||b)g_cucco_sounds.fetch_add(1);
    }
    if(GetTickCount64()<g_cucco_next)return;
    g_cucco_next=GetTickCount64()+5000;
    const unsigned bursts=g_cucco_bursts.load(),cooled=g_cucco_watch.Cooled(),mass=g_cucco_watch.Mass(),sounds=g_cucco_sounds.load();
    const unsigned sum=bursts+cooled+mass+sounds;
    if(sum==g_cucco_last_sum)return;
    g_cucco_last_sum=sum;
    char line[160];
    if(ccf::FormatStats(line,sizeof(line),bursts,cooled,mass,sounds)>0)Log("%s",line);
}
// ==== end Cucco feathers ==============================================================================================

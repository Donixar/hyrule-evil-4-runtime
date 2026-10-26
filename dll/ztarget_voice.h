#pragma once
#include "link_runtime_health.h"
#include "mortal_draw_resident_assets.h"
#include "runtime_paths.h"
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cstdio>
#include <vector>
#pragma comment(lib,"winmm.lib")
// Z-target lock sounds (17-oct): TP's Z2SE_SY_L_FOCUS_SET when a lock is taken or moves to another Ganado and
// Z2SE_SY_L_FOCUS_RESET when the player lets go or the target is lost (d_attention.cpp runDrawProc). Bin32
// link_ztarget_lock.wav / link_ztarget_unlock.wav from scripts/ztarget_lock_sounds.py, SHA-pinned. The game thread
// only raises an atomic request; the DLL's poll thread plays it on its own waveOut device. Optional: a missing or
// changed file mutes that sound only.
namespace link_runtime::ztarget::voice {
inline constexpr const char* kLockSha="fa881aa191db31a83f73f02f612dfcb8d56aeac51de100cb4b1718f080c19816";
inline constexpr const char* kUnlockSha="65e642c2b530e4e46452fab345a4f82e1c2103ce9b968190d05accb0dc8bff85";
enum Request : unsigned {None=0,Lock=1,Unlock=2};
// Latest request wins (a lock right after a release plays the lock). Game thread writes, poll thread takes.
inline std::atomic<unsigned> request{None};
inline void Raise(Request r){request.store(r,std::memory_order_release);}
inline Request Take(){return Request(request.exchange(None,std::memory_order_acq_rel));}
class Voice {
    HWAVEOUT out=nullptr;WAVEHDR header{};WaveData wave;
public:
    Voice()=default;Voice(const Voice&)=delete;Voice& operator=(const Voice&)=delete;
    ~Voice(){if(out){waveOutReset(out);waveOutUnprepareHeader(out,&header,sizeof(header));waveOutClose(out);}}
    bool Loaded()const{return out!=nullptr;}
    bool Load(const wchar_t* path,unsigned volume,const char* sha){
        std::vector<unsigned char> bytes;FILE* file=nullptr;
        if(_wfopen_s(&file,path,L"rb")||!file)return false;
        std::fseek(file,0,SEEK_END);const long length=std::ftell(file);std::rewind(file);
        if(length<44||length>1024*1024){std::fclose(file);return false;}
        bytes.resize(size_t(length));const size_t read=std::fread(bytes.data(),1,bytes.size(),file);std::fclose(file);
        unsigned char digest[32]{};namespace hash=link_runtime::mortal_draw::resident_assets::detail;
        if(read!=bytes.size()||!hash::Hash(bytes.data(),bytes.size(),digest)||!hash::Matches(digest,sha)||
           !ParseWave(bytes,wave)||!wave.channels)return false;
        ScaleWave(wave,volume);
        WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=WORD(wave.channels);f.nSamplesPerSec=wave.rate;
        f.wBitsPerSample=16;f.nBlockAlign=WORD(wave.channels*2);f.nAvgBytesPerSec=f.nSamplesPerSec*f.nBlockAlign;
        if(waveOutOpen(&out,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){out=nullptr;return false;}
        header.lpData=reinterpret_cast<LPSTR>(wave.pcm.data());header.dwBufferLength=DWORD(wave.pcm.size());
        if(waveOutPrepareHeader(out,&header,sizeof(header))!=MMSYSERR_NOERROR){waveOutClose(out);out=nullptr;return false;}
        return true;
    }
    // Restarts the sound when it is still playing.
    bool Play(){if(!out)return false;waveOutReset(out);return waveOutWrite(out,&header,sizeof(header))==MMSYSERR_NOERROR;}
    void Stop(){if(out)waveOutReset(out);}
};
struct Pair {
    Voice lock,unlock;std::atomic<unsigned> played{0};
    // directory ends with a backslash. Returns how many of the two files loaded.
    unsigned Load(const wchar_t* directory,unsigned volume){
        wchar_t path[MAX_PATH]{};unsigned n=0;
        if(paths::Format(path,L"%slink_ztarget_lock.wav",directory)>0&&lock.Load(path,volume,kLockSha))++n;
        if(paths::Format(path,L"%slink_ztarget_unlock.wav",directory)>0&&unlock.Load(path,volume,kUnlockSha))++n;
        return n;
    }
    // Poll thread.
    void Poll(){
        switch(Take()){
        case Lock:unlock.Stop();if(lock.Play())played.fetch_add(1);break;
        case Unlock:lock.Stop();if(unlock.Play())played.fetch_add(1);break;
        default:break;
        }
    }
};
}

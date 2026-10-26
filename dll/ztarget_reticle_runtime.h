#pragma once
#include "runtime_paths.h"
#include "ztarget_reticle.h"
#include "ztarget_native.h"
#include "native_pins.h"
#include "thread_binding.h"
#include "mortal_draw_resident_assets.h"
#include "MinHook.h"
#include <atomic>
#include <cfenv>
#include <cstdio>
#pragma comment(lib,"bcrypt.lib")
namespace link_runtime::ztarget::hud {
using Logger=void(*)(const char*,...);
// target: the cursor's tip (above the head). mode/identity: what the cursor shows (reticle::Mode) and for whom; a
// change restarts its animation (the lock impact on every new or switched lock).
struct Frame {View camera;Vec target;float fovy=0;uint32_t tick=0;int width=0,height=0;float post[16]{};
    uint32_t player=0,global=0,costume=0,cam_system=0;uint16_t room=0;uint8_t mode=0;uint64_t identity=0;};
using Query=bool(*)(Frame&);
using Move=void(__thiscall*)(void*);
inline Move original=nullptr;inline Query query=nullptr;
inline const std::atomic<DWORD>* owner_thread=nullptr;
inline uint32_t base=0;inline std::atomic<bool> ready{false};
inline std::atomic<unsigned> shown{0},hidden{0},occupied{0},lost{0},faults{0};
inline std::atomic<unsigned> calls{0},skip_ready{0},skip_inside{0},skip_thread{0},skip_self{0};
inline std::atomic<unsigned> query_reject{0},project_reject{0},viewport_reject{0};
inline std::atomic<uint32_t> last_self{0},last_thread{0};
inline std::atomic<DWORD> render_thread{0};
inline thread_binding::Binding render_binding;
inline std::atomic<unsigned> published{0},stale{0},scene_reject{0};
inline std::atomic<unsigned> valid_published{0},scene_why[14]{};
inline std::atomic<uint32_t> current_tick{0},packet_tick{0};
struct Delivery {Frame frame;uint32_t time=0;bool valid=false;};
// Camera and native HUD are different OS threads with re4_fps. Only a bounded
// value copy crosses the lock; no native call, logging or callback runs under it.
inline SRWLOCK delivery_lock=SRWLOCK_INIT;inline Delivery delivery;
inline void PublishFrame(bool valid,const Frame& frame,uint32_t now){
    AcquireSRWLockExclusive(&delivery_lock);delivery={frame,now,valid};ReleaseSRWLockExclusive(&delivery_lock);
    published.fetch_add(1);
    if(valid)valid_published.fetch_add(1);
}
inline Delivery Latest(){
    AcquireSRWLockShared(&delivery_lock);const Delivery result=delivery;ReleaseSRWLockShared(&delivery_lock);return result;
}
inline void Publish(){
    if(!ready.load(std::memory_order_acquire)||!owner_thread||owner_thread->load()!=GetCurrentThreadId())return;
    Frame frame;const bool valid=query&&query(frame);PublishFrame(valid,frame,GetTickCount());
}
template<class N>bool Current(N& n,const Frame& frame){
    ztarget::Frame now;unsigned why=0;packet_tick.store(frame.tick);
    if(!ReadFrame(n,base,now))why=1;
    else{current_tick.store(now.frame);
        if(!CameraSafe(now))why=2;else if(now.stop&0x40)why=3;else if(now.frame!=frame.tick)why=4;
        else if(now.player!=frame.player)why=5;else if(now.global!=frame.global)why=6;
        else if(now.costume!=frame.costume)why=7;else if(now.room!=frame.room)why=8;
        else if(now.cam_system!=frame.cam_system)why=9;
        else{float fovy=0,projection[4][4]{},post[16]{};Matrix view;
            if(!Get(n,now.global+0x134,fovy)||fovy!=frame.fovy)why=10;
            else if(!Get(n,now.global+0xA4,view)||std::memcmp(&view,&frame.camera.view,sizeof(view)))why=11;
            else if(!Get(n,now.global+0xD8,projection)||std::memcmp(projection,frame.camera.projection,sizeof(projection)))why=12;
            else if(!Get(n,base+0xCED730,post)||std::memcmp(post,frame.post,sizeof(post)))why=13;
        }}
    if(why)scene_why[why].fetch_add(1);return why==0;
}
inline thread_local bool inside=false;
inline reticle::Painter painter;
// EXE 19aed4af...: IdSys.move computes the HUD screen plane before moving each unit;
// IdCommonTrans uses CameraCurrentProjection and m_scrn_mat * unit.mat. No camera override.
inline constexpr native_pins::Pin pins[]={
    {0x3C15,"e9e6fc2300"},
    // Same 176 bytes, split at an instruction boundary to fit native_pins::kMaxPin.
    {0x243900,"558bec83ec18a1105ec30033c58945fca13c6fc000f6807001000040578bf9897de80f85ed000000d9ee68e097c500d955f0d95df4d9e8d95df8d98034010000dc0d50fbb000dc0d20fab000d95dece8ba72dcffd945ec83c404e891be4200d95decd945ec51dc3d5024b1008d45f08bc8"},
    {0x243971,"d95decd945ecdc0de8f9b000d95decd945ecd91c245051e8135edcffd945f0d91dec97c50033c9d945f483c40cd91dfc97c500894decd945f8d91d0c98c500"},
    {0x243B3B,"e8d7addcffe850a8dcff8d8d7cffffff518d46085068e097c500e8b81cdcff"},
    {0x70FB50,"000000000000e03f"},{0x70FA20,"abaaaaaa46df913f"},
    // TP cursor (17-oct): IdCalcVertex reads size_W/H at +0xDC/+0xE0 (anchor +0x86) every frame from idSysMove00's tail.
    {0x241E20,"558bec8b45080fb6888600000083e10f83f9040f874d020000ff248d88206400d980dc000000dd05e8f9b000dcc9dd0550fbb000dccad9cad95d08d94508d990ac000000d980e000"},
    {0x2437CC,"807e04015f740956e854a0dcff83"},
    {0x712450,"0000000000006e40"},{0x70F9E8,"000000000000f0bf"}
};
struct Native {
    uint32_t base=hud::base;
    bool Read(uint32_t a,void* out,size_t n){
        if(!a||uint64_t(a)+n>0x100000000ULL)return false;
        __try{std::memcpy(out,reinterpret_cast<void*>(a),n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Write(uint32_t a,const void* in,size_t n){
        if(!a||uint64_t(a)+n>0x100000000ULL)return false;
        __try{std::memcpy(reinterpret_cast<void*>(a),in,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool TexLoad(void* block){__try{return reinterpret_cast<int(__cdecl*)(void*,int)>(base+0x1F46)(block,int(reticle::owner))!=0;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
    bool TexRelease(){__try{reinterpret_cast<void(__cdecl*)(int)>(base+0xBF50)(int(reticle::owner));return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
    bool Set(const void* table){__try{reinterpret_cast<void(__thiscall*)(void*,const void*,uint8_t,int,uint8_t,uint8_t,uint32_t)>(base+0x4449)(
        reinterpret_cast<void*>(base+reticle::idsys),table,0xFF,reticle::id_class,0x13,5,0);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
    bool Kill(){__try{reinterpret_cast<void(__thiscall*)(void*,uint8_t,int)>(base+0xCE9B)(reinterpret_cast<void*>(base+reticle::idsys),0xFF,reticle::id_class);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
    uint32_t UnitPtr(){__try{return reinterpret_cast<uint32_t(__thiscall*)(void*,uint8_t,int)>(base+0x95F7)(reinterpret_cast<void*>(base+reticle::idsys),reticle::mark,reticle::id_class);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
};
// Animation clock of the shown cursor (HUD thread only): restarts when the mode or the target changes.
struct Clock {uint8_t mode=0;uint64_t identity=0;double start=0;
    double Seconds(uint8_t m,uint64_t id,double now){if(m!=mode||id!=identity){mode=m;identity=id;start=now;}return now-start;}};
inline Clock anim_clock;
inline double Now(){LARGE_INTEGER f{},c{};QueryPerformanceFrequency(&f);QueryPerformanceCounter(&c);return f.QuadPart?double(c.QuadPart)/double(f.QuadPart):0.0;}
inline std::atomic<unsigned> shown_notice{0},shown_locked{0};
template<class N>reticle::Why PaintWith(N& n,bool valid,const Frame& frame,double now=Now()){
    reticle::Point p;bool show=false;reticle::Look look;
    const auto mode=reticle::Mode(frame.mode);
    if(valid&&mode!=reticle::Mode::Notice&&mode!=reticle::Mode::Locked)valid=false;
    if(valid)look=reticle::Animate(mode,anim_clock.Seconds(frame.mode,frame.identity,now));
    if(!valid)query_reject.fetch_add(1);
    else if(!reticle::Project(frame.camera,frame.target,frame.fovy,p))project_reject.fetch_add(1);
    else{reticle::Point centre=p;centre.y+=look.dy;
        if(!reticle::ObservedViewport(frame.camera,centre,frame.post,frame.width,frame.height,look.w*0.5f,look.h*0.5f))viewport_reject.fetch_add(1);
        else show=true;}
    const auto why=painter.Tick(n,show,p,look);
    if(why==reticle::Why::Shown)(mode==reticle::Mode::Locked?shown_locked:shown_notice).fetch_add(1);
    return why;
}
inline void __fastcall MoveDetour(void* self,void*){
    calls.fetch_add(1);last_self.store(reinterpret_cast<uint32_t>(self));last_thread.store(GetCurrentThreadId());
    unsigned skip=0;
    if(!ready.load(std::memory_order_acquire)){skip_ready.fetch_add(1);skip=1;}
    else if(inside){skip_inside.fetch_add(1);skip=1;}
    else if(reinterpret_cast<uint32_t>(self)!=base+reticle::idsys){skip_self.fetch_add(1);skip=1;}
    else{const auto claim=render_binding.Take();
        if(!thread_binding::Serves(claim)){skip_thread.fetch_add(1);skip=1;}
        else{
            if(claim==thread_binding::Claim::Rebound)anim_clock={};
            render_thread.store(render_binding.Id(),std::memory_order_release);
        }}
    if(skip){original(self);return;}
    inside=true;const DWORD error=GetLastError();fenv_t env;std::fegetenv(&env);
    const Delivery packet=Latest();Native n;bool valid=packet.valid;
    if(valid&&uint32_t(GetTickCount()-packet.time)>100){stale.fetch_add(1);valid=false;}
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    if(valid&&(pid!=GetCurrentProcessId()||!Current(n,packet.frame))){scene_reject.fetch_add(1);valid=false;}
    const auto why=PaintWith(n,valid,packet.frame);
    switch(why){case reticle::Why::Shown:shown.fetch_add(1);break;case reticle::Why::Hidden:hidden.fetch_add(1);break;
    case reticle::Why::Occupied:occupied.fetch_add(1);break;case reticle::Why::Lost:lost.fetch_add(1);break;default:faults.fetch_add(1);break;}
    std::fesetenv(&env);SetLastError(error);
    // pos0 and visibility are ready BEFORE native movement builds the current frame matrices.
    original(self);inside=false;
}
// Single lifetime allocation: IdTexDataLoad relocates the block in place and retains references.
inline unsigned char* resident=nullptr;
inline bool Load(const wchar_t* ini){
    if(resident)return true;
    wchar_t path[MAX_PATH]{};if(paths::Copy(path,ini))return false;
    wchar_t* slash=std::wcsrchr(path,L'\\');if(!slash)return false;
    if(paths::Copy(slash+1,MAX_PATH-size_t(slash+1-path),L"link_ztarget.eff"))return false;
    // 448 bytes: two 64x64 frames (red locked, yellow notice), scripts/ztarget_reticle_assets.py build-tp.
    unsigned char bytes[448]{},digest[32]{};FILE* file=nullptr;
    if(_wfopen_s(&file,path,L"rb")||!file)return false;
    const size_t count=std::fread(bytes,1,sizeof(bytes),file);const int extra=std::fgetc(file);const bool io=std::ferror(file)==0;std::fclose(file);
    namespace hash=mortal_draw::resident_assets::detail;
    if(count!=sizeof(bytes)||extra!=EOF||!io||!hash::Hash(bytes,sizeof(bytes),digest)||
       !hash::Matches(digest,"0574ed86002dc01e00f736d74d6d2b8ff51dc84533dd148b83cb04ecbb16af51"))return false;
    resident=static_cast<unsigned char*>(VirtualAlloc(nullptr,sizeof(bytes),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!resident)return false;std::memcpy(resident,bytes,sizeof(bytes));painter.SetBlock(resident);return true;
}
template<class Mem,size_t N>bool QualifyPins(Mem& memory,uint32_t image,const native_pins::Pin (&set)[N],const char* group,Logger log){
    bool ok=true;
    for(const auto& pin:set){
        unsigned char want[native_pins::kMaxPin]{},have[native_pins::kMaxPin]{};size_t count=0;
        if(!native_pins::Hex(pin.hex,want,sizeof(want),count)){
            log("ZTarget reticle pin_failed group=%s rva=0x%08X reason=pin_format bytes=%u capacity=%u",group,pin.rva,
                static_cast<unsigned>(count),static_cast<unsigned>(sizeof(want)));ok=false;continue;
        }
        if(!image||!image_reloc::ReadPreferred(memory,image,pin.rva,have,count)){
            log("ZTarget reticle pin_failed group=%s rva=0x%08X reason=read_or_relocation bytes=%u",group,pin.rva,
                static_cast<unsigned>(count));ok=false;continue;
        }
        for(size_t i=0;i<count;++i)if(want[i]!=have[i]){
            log("ZTarget reticle pin_failed group=%s rva=0x%08X reason=mismatch offset=0x%X expected=%02X actual=%02X",group,pin.rva,
                static_cast<unsigned>(i),unsigned(want[i]),unsigned(have[i]));ok=false;break;
        }
    }return ok;
}
inline bool Start(uint32_t image,const wchar_t* ini,Query check,const std::atomic<DWORD>* thread,Logger log){
    base=image;Native n;
    const bool staff_ok=QualifyPins(n,base,native_pins::kOcarinaStaff,"staff",log);
    const bool hud_ok=QualifyPins(n,base,pins,"hud",log);
    if(!staff_ok||!hud_ok){
        log("ZTarget reticle disabled: HUD pins failed; camera remains available");return false;}
    if(!Load(ini)){log("ZTarget reticle disabled: link_ztarget.eff missing or changed; camera remains available");return false;}
    void* entry=reinterpret_cast<void*>(base+0x3C15);
    MH_STATUS status=MH_CreateHook(entry,reinterpret_cast<void*>(&MoveDetour),reinterpret_cast<void**>(&original));
    if(status!=MH_OK){log("ZTarget reticle disabled: HUD create=%s",MH_StatusToString(status));return false;}
    query=check;owner_thread=thread;status=MH_EnableHook(entry);
    if(status!=MH_OK){MH_RemoveHook(entry);log("ZTarget reticle disabled: HUD enable=%s",MH_StatusToString(status));return false;}
    ready.store(true,std::memory_order_release);log("ZTarget reticle ready=1 class=0x3B tex=0x9A owner=0xCA1 style=tp_cursor frames=red/yellow tip=feet+1950 size=%.0f pre_idsys_move=1 cross_thread_frame=1",double(reticle::kLockedSize));return true;
}
inline void Poll(Logger log){if(ready.load(std::memory_order_acquire)){
    log("ZTarget hud locked=%u notice=%u",shown_locked.load(),shown_notice.load());
    log("ZTarget hud shown=%u hidden=%u occupied=%u lost=%u faults=%u calls=%u skip_ready=%u skip_inside=%u skip_thread=%u skip_self=%u query=%u project=%u viewport=%u self=0x%X thread=%u owner_thread=%u published=%u stale=%u scene=%u render_thread=%u",
        shown.load(),hidden.load(),occupied.load(),lost.load(),faults.load(),calls.load(),skip_ready.load(),skip_inside.load(),skip_thread.load(),skip_self.load(),
        query_reject.load(),project_reject.load(),viewport_reject.load(),last_self.load(),last_thread.load(),owner_thread?owner_thread->load():0,
        published.load(),stale.load(),scene_reject.load(),render_thread.load());
    log("ZTarget hud frame valid=%u packet=%u current=%u scene_why=%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u",valid_published.load(),packet_tick.load(),current_tick.load(),
        scene_why[1].load(),scene_why[2].load(),scene_why[3].load(),scene_why[4].load(),scene_why[5].load(),scene_why[6].load(),scene_why[7].load(),scene_why[8].load(),
        scene_why[9].load(),scene_why[10].load(),scene_why[11].load(),scene_why[12].load(),scene_why[13].load());
}}
}

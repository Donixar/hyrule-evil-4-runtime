#pragma once
#include "ztarget.h"
#include "native_pins.h"
#include <atomic>
#include <cstring>
// Z-target line of sight (17-oct). Mirrors the Ganado's own sight test (re4-decomp src/em10/em10.cpp:16426,
// EM10_ROUTE_SIGHT_CK): EatMgr.hitCheck(&from,&to,NULL,NULL,0,0x4000), 0 = clear. EatMgr is the effect/bullet
// collision set: the room's walls and columns plus every door piece (emdoor.cpp registers its leaves in EatMgr),
// so "a Ganado could see you / a bullet would pass" is also "you can lock it". SatMgr (the player's walking
// collision) is not used: it misses doors and has walk-only pieces (fences Link cannot cross but can see over).
// mask 0x4000 = EAT_ATTR_MIDDLE_NOHIT, the polygons the Ganados look through.
//
// UHD (EXE 19aed4af..., attributed offline 17-oct, scripts in D:/link-in-re4-work/ztarget-tp/los):
//   cSatMgr::hitCheck thunk 0x8535 -> body 0x194160, thiscall, 6 args, RET 0x18. With cross=NULL and normal=NULL it
//   only calls the any-hit branch (thunk 0x71EE -> 0x193BF0, RET 0x10), which writes SEck (0x8504FC, from mgr+0x1C)
//   and the polyBit table (0x850500, cleared per piece before use). pBypassAt (0x8504F8) belongs to the nearest-hit
//   branch and is not written here; it is saved anyway. No other game data is written on that path (call-graph walk).
// Game thread only (inside the camera front hook, the thread every native hitCheck caller runs on). The scratch is
// saved before and restored after every call under __try, so a native caller never sees our values.
namespace link_runtime::ztarget::los {
namespace rva {constexpr uint32_t eat_mgr=0x7F9AA0,hit_thunk=0x8535,hit_body=0x194160,any_thunk=0x71EE,any_hit=0x193BF0,
    bypass=0x8504F8,seck=0x8504FC,poly_bit=0x850500;}
constexpr size_t kPolyBytes=0x400;   // u8 polyBit[0x400] (atari.cpp:34)
constexpr int kMask=0x4000;
inline constexpr native_pins::Pin kPins[]={
    {rva::hit_thunk,"e926bc1800"},
    {rva::hit_body,"558bec8b4510568b751485c0751e85f6751a8b451c8b5518508b450c528b55085052e86730e7ff5e5dc21800"},
    {rva::any_thunk,"e9fdc91800"},
    {rva::any_hit,"558bec83ec30a1105ec30033c58945fc8b550c8b45088955d88b511c8945d433c08915fc04c500"},
    {0x193C59,"680005c500e8f688e7ff"}
};
// Segment ends: Link's head (feet + 1500, the height the Ganados look at) to the target's chest (feet + 1200, the
// reticle's old point). The centre probe, then the same segment shifted 200 to each side (the Ganado's own +-200
// eye offset): clear when any of them is clear, so a thin column or a door frame edge does not flicker the lock.
constexpr float kEye=1500.f,kChest=1200.f,kSide=200.f;
struct Segment {Vec from,to;};
inline unsigned Probes(Vec player,Vec target,Segment out[3]){
    Vec a=player,b=target;a.y+=kEye;b.y+=kChest;
    const float dx=b.x-a.x,dz=b.z-a.z,len=std::sqrt(dx*dx+dz*dz);
    out[0]={a,b};
    if(!(len>1.f)||!std::isfinite(len))return 1;
    const float sx=-dz/len*kSide,sz=dx/len*kSide;
    out[1]={{a.x+sx,a.y,a.z+sz},{b.x+sx,b.y,b.z+sz}};
    out[2]={{a.x-sx,a.y,a.z-sz},{b.x-sx,b.y,b.z-sz}};
    return 3;
}
enum class Result {Clear,Blocked,Fault};
// N: bool Segment(const Vec& from,const Vec& to,uint32_t& attribute) — false = fault.
template<class N> Result Check(N& n,Vec player,Vec target){
    if(!Finite(player)||!Finite(target))return Result::Fault;
    Segment s[3];const unsigned count=Probes(player,target,s);
    for(unsigned i=0;i<count;++i){uint32_t attr=0;if(!n.Segment(s[i].from,s[i].to,attr))return Result::Fault;if(!attr)return Result::Clear;}
    return Result::Blocked;
}
// While locked: one check every kEvery game frames; the lock drops after kDrop seconds blocked without a clear check
// in between (TP keeps the lock through brief occlusion too). A fault drops at once.
constexpr unsigned kEvery=8;constexpr float kDrop=0.5f;
struct Hold {
    uint64_t identity=0;uint32_t frame=0;float blocked=0;bool known=false;
    void Reset(){*this={};}
    // Seeds the tracker with the acquisition check (already clear).
    void Start(uint64_t id,uint32_t f){identity=id;frame=f;blocked=0;known=true;}
    // true = keep the lock. dt in seconds of this game frame. check() runs the native test (Result).
    template<class CheckFn> bool Step(uint64_t id,uint32_t f,float dt,CheckFn check){
        if(!known||id!=identity){Start(id,f);const Result r=check();if(r==Result::Fault)return false;
            blocked=r==Result::Blocked?dt:0;return blocked<kDrop;}
        if(blocked>0)blocked+=dt;
        if(uint32_t(f-frame)>=kEvery){frame=f;const Result r=check();
            if(r==Result::Fault)return false;
            if(r==Result::Clear)blocked=0;else if(blocked==0)blocked=dt;}
        return blocked<kDrop;
    }
};
inline std::atomic<unsigned> calls{0},clear{0},blocked{0},faults{0};
struct Native {
    uint32_t base=0;
    bool Segment(const Vec& from,const Vec& to,uint32_t& attribute){
        unsigned char poly[kPolyBytes];uint32_t bypass=0,seck=0;Vec a=from,b=to;int result=0;bool ok=true;
        calls.fetch_add(1);
        __try{
            std::memcpy(&bypass,reinterpret_cast<void*>(base+rva::bypass),4);std::memcpy(&seck,reinterpret_cast<void*>(base+rva::seck),4);
            std::memcpy(poly,reinterpret_cast<void*>(base+rva::poly_bit),kPolyBytes);
            result=reinterpret_cast<int(__thiscall*)(void*,Vec*,Vec*,Vec*,Vec*,int,int)>(base+rva::hit_thunk)(
                reinterpret_cast<void*>(base+rva::eat_mgr),&a,&b,nullptr,nullptr,0,kMask);
        }__except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
        __try{
            std::memcpy(reinterpret_cast<void*>(base+rva::poly_bit),poly,kPolyBytes);
            std::memcpy(reinterpret_cast<void*>(base+rva::seck),&seck,4);std::memcpy(reinterpret_cast<void*>(base+rva::bypass),&bypass,4);
        }__except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
        if(!ok){faults.fetch_add(1);return false;}
        attribute=uint32_t(result);(result?blocked:clear).fetch_add(1);return true;
    }
    // The nearest hit (sword-particles-02, below): false = fault. attribute 0 = clear (cross = to, normal = 0);
    // otherwise the nearest polygon's full attribute word, its contact point and its world normal (as stored: it may
    // face away from `from`, RE4's line test meets both sides).
    bool Nearest(const Vec& from,const Vec& to,Vec& cross,Vec& normal,uint32_t& attribute);
};

// ---- the nearest-hit branch (sword-particles-02, 21-oct) ------------------------------------------------------------
// UHD (EXE 19aed4af..., attributed offline 21-oct, scripts in D:/link-in-re4-work/sword-particles-02/nearest-re):
//   the same hitCheck body (0x194160) with cross or normal non-NULL takes its second branch (+0x2C): it calls
//   cSatMgr::hitCheck2 (thunk 0xF3E9 -> body 0x1939D0, thiscall, 6 args, RET 0x18: pos0, pos1, pCross, &pn, flag,
//   mask; atari.cpp:841) and, on a hit with normal non-NULL, PSMTXMultVecSR(pBypassAt + 0x60 (its mat), pn, normal)
//   (thunk 0xDD19 -> 0x5725E0, cdecl, writes only its output). hitCheck2 writes SEck (0x8504FC = mgr+0x1C), clears
//   polyBit (0x850500, (polygon_num >> 3) + 1 bytes per piece, memset thunk 0xC559) whose bits blkPolyLineCkCore
//   (0x1910E0) sets, and pBypassAt (0x8504F8) on every nearer hit; every other write is through its arguments (pos1
//   moved onto the hit piece's grid, *pCross = the nearest point or pos1 when clear, *ppNorm) or its own stack. The
//   call-graph walk (recursive descent from 0x1939D0: blkPolyLineCk 0x1932D0, lineOverlap 0x190910, the core,
//   At_poly_line_ck 0x18F5E0, the PSVEC/PSMTX helpers) finds no other absolute write besides the CRT's stack-cookie
//   failure path, which ends the process. The result is At_poly_line_ck's attr | 0x01000000, attr = (u16 poly+0x10
//   << 16) | u16 poly+0x12: the full EAT word (effect bits 0x800000 / 0x8000 / 0x80, EatGetEffectType), where the
//   any-hit branch above only returns its flag bits. The scratch is the same three globals as Segment: saved before,
//   restored after, under __try. Game thread only.
namespace rva {constexpr uint32_t near_thunk=0xF3E9,near_body=0x1939D0,mult_sr_thunk=0xDD19,mult_sr=0x5725E0,
    near_core=0x1910E0;}
inline constexpr native_pins::Pin kNearestPins[]={
    {rva::hit_thunk,"e926bc1800"},
    // hitCheck: both branches; the second calls hitCheck2, then PSMTXMultVecSR(pBypassAt->mat, pn, normal) on a hit.
    {rva::hit_body,"558bec8b4510568b751485c0751e85f6751a8b451c8b5518508b450c528b55085052e86730e7ff5e5dc21800"
        "8b551c57528b5518528d551c528b5508508b450c5052e842b2e7ff8bf885f6741b85ff74178b451c8b0df804c500565083c16051"
        "e8549be7ff83c40c8bc75f5e5dc21800"},
    {rva::near_thunk,"e9e2451800"},
    // hitCheck2: frame, cookie, SEck = this->+0x1C, cur = *pos1.
    {rva::near_body,"558bec83ec68a1105ec30033c58945fc8b55108bc18b4d08894da08b481c538b5d0c890dfc04c500"},
    // per piece: memclr(polyBit, (polygon_num >> 3) + 1).
    {0x193A55,"0fb74e1e8b4630c1e9034151680005c5008945bce8eb8ae7ff"},
    // a nearer hit: pos1 = mat * lb, cur = mat * lcur, pBypassAt = the piece.
    {0x193AFF,"8b55bc538d45cc508d7e60578955b0e82911e7ff8d4df0518d55e45257e81b11e7ff83c4188935f804c500"},
    // *pCross = cur; on a hit *ppNorm = pn; cookie; RET 0x18.
    {0x193B46,"85d274118b45f08b4df489028b45f8894a048942088bc685f6740985ff74058b4da8890f8b4dfc5f5e33cd5be823924d008be55dc21800"},
    // blkPolyLineCkCore: the polyBit test and set.
    {0x191192,"8a880005c50084ca0f858f0000000aca8b551088880005c500"},
    {rva::mult_sr_thunk,"e9c2485600"},
    {rva::mult_sr,"558bec83ec0c8b4d088b450cd94104d84804d901d808dec1d94108d84808dec1d95df4d94110d808d94114d84804dec1d94118d84808"
        "dec1d95df8d94120d808d94124d84804dec1d94128d848088b4510dec1d95dfcd945f4d918d945f8d95804d945fcd958088be55dc3"}
};
inline std::atomic<unsigned> nearest_calls{0},nearest_faults{0};
inline bool Native::Nearest(const Vec& from,const Vec& to,Vec& cross,Vec& normal,uint32_t& attribute){
    unsigned char poly[kPolyBytes];uint32_t bypass=0,seck=0;Vec a=from,b=to,c=to,n{};int result=0;bool ok=true;
    nearest_calls.fetch_add(1);
    __try{
        std::memcpy(&bypass,reinterpret_cast<void*>(base+rva::bypass),4);std::memcpy(&seck,reinterpret_cast<void*>(base+rva::seck),4);
        std::memcpy(poly,reinterpret_cast<void*>(base+rva::poly_bit),kPolyBytes);
        result=reinterpret_cast<int(__thiscall*)(void*,Vec*,Vec*,Vec*,Vec*,int,int)>(base+rva::hit_thunk)(
            reinterpret_cast<void*>(base+rva::eat_mgr),&a,&b,&c,&n,0,kMask);
    }__except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    __try{
        std::memcpy(reinterpret_cast<void*>(base+rva::poly_bit),poly,kPolyBytes);
        std::memcpy(reinterpret_cast<void*>(base+rva::seck),&seck,4);std::memcpy(reinterpret_cast<void*>(base+rva::bypass),&bypass,4);
    }__except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    if(!ok){nearest_faults.fetch_add(1);return false;}
    attribute=uint32_t(result);cross=result?c:to;normal=result?n:Vec{};return true;
}
}

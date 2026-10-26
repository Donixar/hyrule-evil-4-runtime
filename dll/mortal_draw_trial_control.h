#pragma once
#include "mortal_draw_resident_assets.h"
#include "mortal_draw_baseline_data.h"
#include "parry_sword_reader.h"
#include "shield_reader.h"
#include <cmath>

namespace link_runtime::mortal_draw::trial {
namespace reader=link_runtime::parry::sword::reader;
using Slots=reader::Slots;
enum class Status {Ready,Started,NotInHandler,IdentityMismatch,BaselineMismatch,NativeMismatch,AssetsMissing,StartRejected};
struct Attempt {Status status=Status::StartRejected;uint64_t token=0;uint32_t voice_event=0;};
using Clear=void(__cdecl*)(void*,int);
constexpr float bind_tolerance=0.002f,local_tolerance=0.005f;
inline bool Near(float a,float b,float tolerance){return std::isfinite(a)&&std::abs(a-b)<=tolerance;}
inline bool Read(uint32_t p,void* out,size_t n){reader::ProcessMemory m;return m.Read(p,out,n);}
template<class T> inline bool At(uint32_t p,T& value){return Read(p,&value,sizeof(value));}
inline bool Parts(void* model,uint32_t (&parts)[119]){
    const auto p=reinterpret_cast<uint32_t>(model);uint8_t count=0;uint32_t flags=0,node=0;
    if(!At(p+0x102,count)||count!=119||!At(p+4,flags)||!At(p+0xF4,node))return false;
    for(unsigned i=0;i<119;++i){
        if(!reader::Pointer(node))return false;
        for(unsigned j=0;j<i;++j)if(parts[j]==node)return false;
        if((flags&0x2000)&&node!=parts[0]+i*0x1D8&&i)return false;
        parts[i]=node;if(!At(node+0xF4,node))return false;
    }
    return node==0;
}
// Player allocates this0x98-byte work block even without an attached camera.
// Require inactive channels/type and already-cleared camera vectors. Preserve
// residual frame/matrix/history bytes, which native clear does not erase.
inline bool CameraIdle(uint32_t camera){
    if(!camera)return true;
    unsigned char work[0x98]{};if(!Read(camera,work,sizeof(work)))return false;
    for(unsigned i=0;i<5;++i)if(work[i]!=0xFF)return false;
    if(work[5]!=0)return false;
    // MotionClear writes only channels/type and camera_data. Frame, matrix
    // and key-history bytes survive native cutscenes while type is off;
    // preserve them instead of requiring a freshly allocated zero block.
    for(unsigned i=0x3C;i<0x78;++i)if(work[i]!=0)return false;
    return true;
}
// Link's rig: every part's parent and its identity-basis inverse bind at the verified bind position.
inline bool Rig(uint32_t player,const uint32_t (&parts)[119]){
    for(unsigned i=0;i<119;++i){
        uint32_t parent=0;float inverse[12]{};
        if(!At(parts[i]+0x6C,parent)||!Read(parts[i]+0xF8,inverse,48))return false;
        const int id=baseline_data::parents[i];if(parent!=(id<0?player:parts[id]))return false;
        for(unsigned row=0;row<3;++row){
            for(unsigned col=0;col<3;++col)if(!Near(inverse[4*row+col],row==col?1.f:0.f,0.000001f))return false;
            if(!Near(inverse[4*row+3],baseline_data::inverse_position[i][row],bind_tolerance))return false;
        }
    }
    return true;
}
inline bool Baseline(void* model,bool before_begin=false){
    uint32_t parts[119]{};if(!Parts(model,parts))return false;
    const auto player=reinterpret_cast<uint32_t>(model);float scale[3]{};uint32_t camera=0,secondary=0;
    // MotionClear camera writes must be idempotent on the inactive block.
    // Native begin clears secondary blending; after begin it must be absent.
    if(!At(player+0x2A4,camera)||!CameraIdle(camera)||!At(player+0x2A8,secondary)||(!before_begin&&secondary))return false;
    if(!Read(player+0xAC,scale,12))return false;
    for(float v:scale)if(!Near(v,1.f,0.000001f))return false;
    for(unsigned i=0;i<119;++i){
        uint32_t flags=0;if(!At(parts[i]+0x1C0,flags)||(flags&0x20000000))return false;
    }
    return Rig(player,parts);
}
inline Status Gate(void* model,Slots slots,bool before_begin=false){
    reader::Observation pose;
    if(!reader::ObserveCurrent(slots,pose)||pose.player!=reinterpret_cast<uint32_t>(model))return Status::IdentityMismatch;
    if(!Baseline(model,before_begin))return Status::BaselineMismatch;
    uint32_t p=0,g=0;if(!At(slots.player,p)||!At(slots.global,g)||p!=pose.player||g!=pose.global)return Status::IdentityMismatch;
    return Status::Ready;
}
// Ocarina (Task 10, final review C1): Link's body only. Same identity, costume, rig and baseline
// checks as Gate, but it never reads the knife slot: the ocarina starts with the sword sheathed
// (knife info bit 8 clear) and, with Mount=1, with the ocarina BIN in that slot. Gate (Mortal
// Draw) keeps requiring the Ordon/Master Sword in hand.
inline Status BodyGate(void* model,Slots slots,bool before_begin=false){
    const auto player=reinterpret_cast<uint32_t>(model);
    uint32_t p=0,g=0,character=0,flags=0;uint8_t parts=0;
    if(!At(slots.player,p)||p!=player||!reader::Pointer(p)||!At(slots.global,g)||!reader::Pointer(g)||
       !At(g+0x4FC8,character)||(character&0xFF)!=0||((character>>8)&0xFF)>3||
       !At(p+0x102,parts)||parts!=119||!At(p+4,flags)||(flags&0x4000))return Status::IdentityMismatch;
    if(!Baseline(model,before_begin))return Status::BaselineMismatch;
    uint32_t p2=0,g2=0;if(!At(slots.player,p2)||!At(slots.global,g2)||p2!=p||g2!=g)return Status::IdentityMismatch;
    return Status::Ready;
}
inline bool Cleared(void* model){
    uint32_t parts[119]{};if(!Parts(model,parts))return false;
    for(unsigned i=0;i<119;++i){float srt[9]{};if(!Read(parts[i]+0x94,srt,sizeof(srt)))return false;
        for(unsigned axis=0;axis<3;++axis)if(!Near(srt[axis],baseline_data::local_position[i][axis],local_tolerance)||
            !Near(srt[3+axis],0.f,0.000001f)||!Near(srt[6+axis],1.f,0.000001f))return false;
    }
    return true;
}
inline bool CleanupPose(void* model,Clear clear){
    if(!clear||!Baseline(model))return false;
    clear(model,0);
    return Cleared(model);
}
// Residual pose (bug #1, 24-oct: a Ganado grabbed Link mid Back Slice and he stayed twisted until another own move
// ended). A native clip writes only the channels it carries; the rest keep what the previous clip left. An own clip
// that ends under its lease runs CleanupPose above; one a native damage, grab, death or event takes over is abandoned
// without it, so TP's channels (the roll's hips and spine, part translations RE4 clips never key) stay on the native
// pose. Called by the dispatch (mortal_draw_dispatch.h Runtime::Residue) right before the native clip that replaced
// ours is evaluated: the part-local SRT MotionClear would write (bind translation, zero angles, unit scale), and
// nothing else. The native clip's FCV at +1D8, the attached camera and the secondary motion stay untouched, and the
// native evaluation that follows rewrites every channel it carries on the same frame. Link's rig only (Rig); a part
// another system drives (skip flag 0x20000000) is left as it is. Returns the parts written; 0 = refused.
inline bool Put(uint32_t at,const void* in,size_t n){
    SIZE_T written=0;return reader::Pointer(at)&&WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),in,n,&written)&&written==n;
}
inline unsigned RestorePose(void* model){
    uint32_t parts[119]{};const auto player=reinterpret_cast<uint32_t>(model);
    if(!Parts(model,parts)||!Rig(player,parts))return 0;
    unsigned written=0;
    for(unsigned i=0;i<119;++i){
        uint32_t flags=0;if(!At(parts[i]+0x1C0,flags))return written;
        if(flags&0x20000000)continue;
        float srt[9]{};
        for(unsigned axis=0;axis<3;++axis){srt[axis]=baseline_data::local_position[i][axis];srt[6+axis]=1.f;}
        if(!Put(parts[i]+0x94,srt,sizeof(srt)))return written;
        ++written;
    }
    return written;
}
inline bool KnifeHoldNative(const shield::NativeAccess& a){
    using namespace shield;
    const auto* image=reinterpret_cast<const unsigned char*>(a.image_base);
    for(uint32_t r:{0x3632F8U,0x3546DEU,0x354418U,0x352D40U,0x25EEB0U,0x27E5F0U,0x354610U,0x353D10U,0x353F30U})if(!Area(a.regions.data(),a.regions.size(),r,64,true))return false;
    for(uint32_t r:{0x81BCFCU,0x81BA24U,0x81B9F4U,0x81BA2CU,0x81BA14U,0x81BA18U})if(!Area(a.regions.data(),a.regions.size(),r,4,false))return false;
    size_t target=0;
    return U32(image+0x81BA2C)==a.image_base+0x354610&&
        Match(image+0x35461A,{0x0F,0xB6,0x86,0xFF,0,0,0})&&U32(image+0x354624)==a.image_base+0x81BA14&&
        U32(image+0x81BA14)==a.image_base+0x353D10&&U32(image+0x81BA18)==a.image_base+0x353F30&&
        Match(image+0x353D10,{0x55,0x8B,0xEC,0x53,0x56,0x57,0x33,0xDB,0x33,0xFF})&&
        Match(image+0x353F55,{0x8B,0x86,0xE4,7,0,0,0x83,0x78,0x2C,0})&&
        Match(image+0x3632F8,{0x0F,0xB6,0x90,0xFD,0,0,0})&&U32(image+0x36330A)==a.image_base+0x81BCD0&&
        U32(image+0x81BCFC)==a.image_base+0xD1A7&&Relative(image,a.image_size,0xD1A7,0xE9,target)&&target==0x3546C0&&
        Match(image+0x3546DE,{0x0F,0xB6,0x86,0xFE,0,0,0})&&U32(image+0x3546E8)==a.image_base+0x81BA20&&
        U32(image+0x81BA24)==a.image_base+0x354410&&
        Match(image+0x354418,{0x0F,0xB6,0x86,0xFF,0,0,0})&&U32(image+0x354422)==a.image_base+0x81B9F0&&
        U32(image+0x81B9F4)==a.image_base+0x352D40&&Match(image+0x352D40,{0x55,0x8B,0xEC,0x8B,0x4D,8,0x5D,0xE9})&&
        Relative(image,a.image_size,0x352D47,0xE9,target)&&target==0x974B&&Relative(image,a.image_size,0x974B,0xE9,target)&&target==0x25CFD0&&
        Relative(image,a.image_size,0x27E603,0xE8,target)&&target==0x8594&&Relative(image,a.image_size,0x8594,0xE9,target)&&target==0x25EEB0&&
        Match(image+0x25EEB0,{0x55,0x8B,0xEC,0x8B,0x45,8,0xC7,0x80,0xA8,2,0,0,0,0,0,0,0x5D,0xC3});
}
inline bool Native(const shield::NativeAccess& a){
    if(!shield::Complete(a)||!KnifeHoldNative(a)||!shield::Area(a.regions.data(),a.regions.size(),0x25EF10,sizeof(baseline_data::clear_bytes),true))return false;
    unsigned char bytes[sizeof(baseline_data::clear_bytes)]{};
    if(!Read(a.image_base+0x25EF10,bytes,sizeof(bytes))||dispatch::Read<uint32_t>(bytes+7)!=a.image_base+0x835E10)return false;
    dispatch::Write<uint32_t>(bytes+7,0xC35E10);
    if(std::memcmp(bytes,baseline_data::clear_bytes,sizeof(bytes)))return false;
    for(const auto& edge:std::initializer_list<std::pair<uint32_t,uint32_t>>{{0x4D22,0x572310},{0x5812,0x573760},{0xC559,0x55B760}}){
        unsigned char code[5]{};if(!Read(a.image_base+edge.first,code,5)||code[0]!=0xE9||
            edge.first+5+static_cast<int32_t>(dispatch::Read<uint32_t>(code+1))!=edge.second)return false;
    }
    return true;
}
namespace detail {
struct Bridge {dispatch::Runtime* runtime;dispatch::Set set;Clear clear;Slots slots;Status failure=Status::StartRejected;bool body=false;};
inline __declspec(thread) Bridge* bridge=nullptr;
inline void __fastcall Set(void* player,void*,void* fcv,uint32_t hokan,uint32_t frame,uint32_t attrs,void* seq){
    auto* b=bridge;if(!b||b->runtime->current!=player||!b->runtime->Current(player))return;
    b->failure=b->body?BodyGate(player,b->slots):Gate(player,b->slots);
    if(b->failure==Status::Ready){b->clear(player,0);if(!b->runtime->Current(player))return;
        if(Cleared(player)){b->set(player,fcv,hokan,frame,attrs,seq);return;}
        b->failure=Status::BaselineMismatch;
    }
    // Abort only the just-started native event, never a replacement event.
    auto* p=static_cast<unsigned char*>(player);
    if(b->runtime->Current(player)&&dispatch::Read<uint32_t>(p+0xFC)==5&&(dispatch::Read<uint32_t>(p+0x468)&2))b->runtime->api.end(player,0);
}
}
namespace detail {
// Admission of TryStartClip and the sword/combat starts: inside the handler, pinned native code, the
// player's weapon block readable and the model at its verified baseline (before begin).
// body=false: Mortal Draw (Gate, sword in hand). body=true: ocarina (BodyGate, no knife slot).
inline Status AdmitAt(dispatch::Runtime& runtime,const Slots& slots,bool body){
    if(!runtime.busy||!runtime.current||bridge)return Status::NotInHandler;
    uint32_t weapon=0,object=0;
    const auto player=reinterpret_cast<uint32_t>(runtime.current);
    if(!At(player+0x7D8,weapon)||!reader::Pointer(weapon)||!At(weapon+0x34,object)||(object&&!reader::Pointer(object)))return Status::IdentityMismatch;
    return body?BodyGate(runtime.current,slots,true):Gate(runtime.current,slots,true);
}
inline Attempt Launch(dispatch::Runtime& runtime,Clear clear,const Slots& slots,const dispatch::Lease& lease,dispatch::StartFrom from,bool body=false){
    Bridge b{&runtime,runtime.api.set,clear,slots};b.body=body;
    if(!b.set||!clear)return {Status::StartRejected};
    bool started=false;bridge=&b;runtime.api.set=reinterpret_cast<dispatch::Set>(Set);
    __try {started=runtime.Start(lease,from);}
    __finally {runtime.api.set=b.set;bridge=nullptr;}
    // Voice intent only; no playback or native SE backend is connected here.
    return started?Attempt{Status::Started,lease.token,0x1009A}:Attempt{b.failure==Status::Ready?Status::StartRejected:b.failure};
}
}
namespace detail {
// Ocarina body-gated start after the native qualification (tests call it with fixture slots).
inline Attempt StartClipAt(dispatch::Runtime& runtime,const Slots& slots,Clear clear,const dispatch::Lease& lease,dispatch::StartFrom from){
    const auto gate=AdmitAt(runtime,slots,true);if(gate!=Status::Ready)return {gate};
    if(!lease.token||!lease.fcv||!lease.seq||!lease.entries)return {Status::AssetsMissing};
    return Launch(runtime,clear,slots,lease,from,true);
}
}
// Ocarina (Task 10): the same verified begin + MotionClear + setter, with the caller's own
// resident clip (token from resident_assets::ReserveToken) and normal idle as the start state.
// Admission and the setter bridge use BodyGate: no sword requirement, the knife slot is not read.
inline Attempt TryStartClip(dispatch::Runtime& runtime,const shield::NativeAccess& native,const dispatch::Lease& lease,
                            dispatch::StartFrom from=dispatch::StartFrom::NormalIdle){
    Slots slots{};
    if(!runtime.busy||!runtime.current||detail::bridge)return {Status::NotInHandler};
    if(!Native(native))return {Status::NativeMismatch};
    slots=Slots{native.image_base+static_cast<uint32_t>(native.code.player_slot_rva),native.image_base+static_cast<uint32_t>(native.code.global_slot_rva)};
    return detail::StartClipAt(runtime,slots,reinterpret_cast<Clear>(native.image_base+0x25EF10),lease,from);
}
}

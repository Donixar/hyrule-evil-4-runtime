#pragma once
#include "parry_owner_runtime.h"
#include "parry_commit_fingerprint.h"
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")

// No startup/Decision wiring. This irreversible operation is usable only inside
// the current pre-effects callback; only Committed authorizes saved EAX=0.
namespace link_runtime::parry::commit {
using namespace link_runtime::shield;
enum class Result {NotCommitted,Committed,PartialFailure};
using ModelSet=void(__thiscall*)(void*,void*,uint32_t,uint32_t,uint32_t,void*);
using Approve=bool(__cdecl*)(const Candidate&,const owner::Observation&);
struct Code {bool verified=false;owner::Code owner_code;uint32_t image_base=0;};
namespace detail {
inline constexpr owner::detail::Fixup no_fixups[]={{0xFFFFFFFF,0}};
}
inline Code FindCode(const unsigned char* image,size_t size,const NativeAccess& a) {
    const auto own=owner::FindCode(image,size,a);size_t target=0;
    if(!own.verified||!owner::detail::Window(image,a,0x25CF80,detail::setter,detail::no_fixups)||
       !owner::detail::Window(image,a,0x2611A0,detail::initializer,detail::initializer_fixups)||
       !owner::detail::Window(image,a,0x68070,detail::crash,detail::crash_fixups)||
       !owner::detail::Window(image,a,0x56A83,detail::latch_exit,detail::no_fixups)||
       !Relative(image,size,0xB14A,0xE9,target)||target!=0x2611A0)return {};
    return {true,own,a.image_base};
}
struct Runtime {
    // Configure before hook admission; once live, access/code and enable changes
    // belong to the game thread. Quarantine is terminal for this runtime.
    bool enabled=false; // Configuration only; never an owner/contact/reaction qualification.
    NativeAccess access;Code code;
    volatile LONG quarantined=0;
};
struct Recoil {
    uint32_t arc=0,fcv=0,seq=0,fcv_offset=0,seq_offset=0,next_offset=0,count=0;
    uint32_t original_motion=0,original_seq=0,motion_flags=0;
    uint16_t original_attrs=0;
    unsigned char parts=0;
    std::array<uint32_t,128> nodes{};
};
inline bool Equal(const Recoil& a,const Recoil& b) {
    return a.arc==b.arc&&a.fcv==b.fcv&&a.seq==b.seq&&a.fcv_offset==b.fcv_offset&&a.seq_offset==b.seq_offset&&
        a.next_offset==b.next_offset&&a.count==b.count&&a.original_motion==b.original_motion&&a.original_seq==b.original_seq&&
        a.motion_flags==b.motion_flags&&a.original_attrs==b.original_attrs&&a.parts==b.parts&&a.nodes==b.nodes;
}
namespace detail {
class Hash {
    BCRYPT_ALG_HANDLE algorithm_=nullptr;BCRYPT_HASH_HANDLE hash_=nullptr;
public:
    Hash(){if(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0)BCryptCreateHash(algorithm_,&hash_,nullptr,0,nullptr,0,0);}
    ~Hash(){if(hash_)BCryptDestroyHash(hash_);if(algorithm_)BCryptCloseAlgorithmProvider(algorithm_,0);}
    Hash(const Hash&)=delete;Hash& operator=(const Hash&)=delete;
    bool Add(unsigned char* p,unsigned n){return hash_&&BCryptHashData(hash_,p,n,0)>=0;}
    bool Matches(const char* expected){
        unsigned char digest[32]{};char hex[65]{};constexpr char digits[]="0123456789abcdef";
        if(!hash_||BCryptFinishHash(hash_,digest,32,0)<0)return false;
        for(unsigned i=0;i<32;++i){hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15];}
        return std::strcmp(hex,expected)==0;
    }
};
template<class Memory> bool Asset(Memory& memory,uint32_t pointer,bool fcv) {
    const unsigned size=fcv?12224:320;Hash hash;unsigned char bytes[256]{};
    if(!Range(pointer,size))return false;
    for(unsigned at=0;at<size;at+=sizeof(bytes)) {
        const unsigned n=(size-at)<sizeof(bytes)?size-at:sizeof(bytes);
        if(!memory.Read(pointer+at,bytes,n))return false;
        if(fcv&&at==0) {
            // Only the 24 native channel pointers at offsets80..175 are relocated
            // by MotionSetCore. Normalize precisely those fields before hashing.
            const uint32_t first=U32(bytes+80);const bool relocated=first>=pointer&&uint64_t(first)<uint64_t(pointer)+size;
            for(unsigned i=0;i<24;++i) {
                uint32_t value=U32(bytes+80+i*4);
                const bool absolute=value>=pointer&&uint64_t(value)<uint64_t(pointer)+size;
                if(absolute!=relocated||(!absolute&&value>=size))return false;
                if(absolute)value-=pointer;
                std::memcpy(bytes+80+i*4,&value,4);
            }
        }
        if(!hash.Add(bytes,n))return false;
    }
    return hash.Matches(fcv?"9418221f6dbce41215f4bb08075b58ac65a6a3907d39205b29b044c02cb1c396":
        "ffcd841e425f78a58fa455dec78cab94e6d1c739b96346322c9a017f5b136b81");
}
template<class Memory> bool Meta(Memory& m,const owner::Observation& o,Recoil& r) {
    uint32_t secondary=0,camera=0,tags[2]{};
    if(!Word(m,o.owner+0x380,r.arc)||!Range(r.arc,0x78)||!Word(m,r.arc,r.count)||r.count<26||r.count>4096||
       !Word(m,r.arc+0x6C,r.fcv_offset)||!Word(m,r.arc+0x70,r.seq_offset)||!Word(m,r.arc+0x74,r.next_offset)||
       r.fcv_offset<0x10+8*r.count||r.fcv_offset>=r.seq_offset||r.seq_offset>=r.next_offset||r.seq_offset-r.fcv_offset!=12224||r.next_offset-r.seq_offset!=320||r.next_offset>64*1024*1024||
       !Range(r.arc,r.next_offset)||!m.Read(r.arc+0x10+4*r.count+23*4,tags,8)||tags[0]!=0x00564346||tags[1]!=0x00514553||
       !Word(m,o.owner+0x2A8,secondary)||secondary||!Word(m,o.owner+0x2A4,camera)||camera||
       !Word(m,o.owner+0x1D8,r.original_motion)||!Word(m,o.owner+0x280,r.original_seq)||
       !Word(m,o.owner+0x21C,r.motion_flags)||(r.motion_flags&0x30000000)||
       !m.Read(o.owner+0x218,&r.original_attrs,2)||!m.Read(o.owner+0x102,&r.parts,1)||r.parts<31||r.parts>128)return false;
    r.fcv=r.arc+r.fcv_offset;r.seq=r.arc+r.seq_offset;
    uint32_t node=0;if(!Word(m,o.owner+0xF4,node))return false;
    for(unsigned i=0;i<r.parts;++i) {
        if(!Range(node,0x1C0))return false;
        for(unsigned j=0;j<i;++j)if(r.nodes[j]==node)return false;
        r.nodes[i]=node;unsigned char part[0x1C0]{};
        if(!m.Read(node,part,sizeof(part)))return false;node=U32(part+0xF4);
    }
    return node==0;
}
}
template<class Memory> bool Prepare(Memory& memory,const owner::Observation& o,Recoil& out) {
    out={};Recoil first{},last{};
    if(!o.QualifiedRoute()||!detail::Meta(memory,o,first)||!detail::Asset(memory,first.fcv,true)||
       !detail::Asset(memory,first.seq,false)||!detail::Meta(memory,o,last)||!Equal(first,last))return false;
    out=first;return true;
}
struct Scope {bool tainted=false;};
inline __declspec(thread) Scope* scope=nullptr;
#ifdef LINK_PARRY_COMMIT_FIXTURE
// Standalone test uses the real setter's bytes with only its initializer call
// redirected to an argument/effect probe. Production has no override field.
ModelSet FixtureSetter();
#endif
inline ModelSet Setter(const Runtime& runtime) {
#ifdef LINK_PARRY_COMMIT_FIXTURE
    (void)runtime;return FixtureSetter();
#else
    return reinterpret_cast<ModelSet>(runtime.code.image_base+0x25CF80);
#endif
}
inline bool Confirm(owner::Memory& memory,const Candidate& c,const owner::Observation& before,const Recoil& r) {
    const auto invocation=owner::detail::Current(c);if(!invocation.wrapper)return false;
    uint32_t motion=0,seq=0,arc=0,routines=0;uint16_t attrs=0,blend=0;unsigned char latch=0;
    return Word(memory,before.owner+0x1D8,motion)&&motion==r.fcv&&Word(memory,before.owner+0x280,seq)&&seq==r.seq+4&&
        Word(memory,before.owner+0x380,arc)&&arc==r.arc&&Word(memory,before.owner+0xFC,routines)&&routines==U32(before.routines.data())&&
        memory.Read(before.owner+0x218,&attrs,2)&&attrs==1&&memory.Read(before.owner+0x29C,&blend,2)&&blend==0x0606&&
        memory.Read(before.owner+0x7CC,&latch,1)&&latch==1;
}
inline Result CommitCurrent(Runtime& runtime,const Candidate& candidate,const owner::Observation& expected,Approve approve) {
    const DWORD error=GetLastError();Result result=Result::NotCommitted;
    if(scope){scope->tainted=true;SetLastError(error);return result;}
    if(!runtime.enabled||InterlockedCompareExchange(&runtime.quarantined,0,0)||!runtime.code.verified||
       runtime.code.image_base!=runtime.access.image_base||!approve){SetLastError(error);return result;}
    const uint32_t image_base=runtime.code.image_base;
    Scope current;scope=&current;bool irreversible=false;
    __try {
        __try {
            owner::Memory memory;owner::Observation before{},fresh{};Recoil recoil{},again{};
            if(owner::ObserveCurrent(runtime.access,runtime.code.owner_code,candidate,before)&&owner::Equal(before,expected)&&
               Prepare(memory,before,recoil)&&approve(candidate,before)&&!current.tainted&&
               owner::ObserveCurrent(runtime.access,runtime.code.owner_code,candidate,fresh)&&owner::Equal(fresh,before)&&
               Prepare(memory,fresh,again)&&Equal(recoil,again)&&!current.tainted&&runtime.enabled&&
               !InterlockedCompareExchange(&runtime.quarantined,0,0)&&runtime.code.verified&&runtime.code.image_base==image_base) {
                // Irreversible boundary: no arbitrary/native call between the last
                // owner/resource validation and this byte compare-and-exchange.
                if(_InterlockedCompareExchange8(reinterpret_cast<volatile CHAR*>(fresh.owner+0x7CC),1,0)==0) {
                    irreversible=true;
                    Setter(runtime)(reinterpret_cast<void*>(fresh.owner),reinterpret_cast<void*>(recoil.fcv),6,0,1,reinterpret_cast<void*>(recoil.seq));
                    result=!current.tainted&&Confirm(memory,candidate,before,recoil)?Result::Committed:Result::PartialFailure;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {result=irreversible?Result::PartialFailure:Result::NotCommitted;}
        if(result==Result::PartialFailure){InterlockedExchange(&runtime.quarantined,1);runtime.enabled=false;}
    } __finally {scope=nullptr;SetLastError(error);}
    return result;
}
} // namespace link_runtime::parry::commit

#pragma once
#include "parry_sword_contact.h"
#include "parry_sword_normals.h"
#include "parry_campaign_sword_data.h"
#include <cstdint>
#include <array>
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")

// Evidence reader, not a contact gate. Memory.Read must be checked and read-only.
namespace link_runtime::parry::sword::reader {
inline uint32_t U32(const unsigned char* p) {uint32_t v;std::memcpy(&v,p,4);return v;}
inline bool Range(uint32_t p,size_t n) {return p>=0x10000&&n&&uint64_t(p)+n<0x7fff0000;}
inline bool Pointer(uint32_t p) {return Range(p,4)&&!(p&3);}
struct Slots {uint32_t player,global;}; // Resolved by the existing native finder.
struct Observation {
    uint32_t player=0,global=0,info=0,resource=0,part0=0,part10=0;
    Profile profile=Profile::Unknown;
    Pose pose{};Blade blade{};
    // Deliberately cannot be toggled by caller-provided epoch/valid flags.
    bool ContactQualified() const {return false;}
};
struct DigestRange {unsigned field,size;const char* sha;};
inline constexpr DigestRange kCampaignPl00[]={
    {0x30,3072,"9b16618feea3f2e6972dac742b01cc7ab155a8c0caacc5b74c2f9a8b357c6432"},
    {0x34,3072,"b7c8b5e5ac93a1dbd23bfe47d62bed09328cae8d988396dd662187838077dbd4"},
    {0x10,2048,"19e6e1617fb851878af037aa56dc65235b06996332b7ad974c245d20363aab25"},
    {0x14,8,"deb9cc7f1a9cec4f23ddee703a60695deedd0255003e2d3fd3e1d35e4193820b"},
    {0x48,512,"076a27c79e5ace2a3d47f9dd2e83e4ff6ea8872b3c2218f66c92b89b55f36560"},
    {0x1c,144,"75877d657a2ee136bc1736bab3872f4b1915320b663bea183f199595317acefc"}};
inline constexpr DigestRange kOrdon[]={
    {0x30,4368,"e5aa83955b804c975043b9c5079b1db46660c5f30af93c5d03023697dea07370"},
    {0x34,4368,"0af267bab80788bfffcfbf78ab3b75fed5560845ff0975e26c00840b6fe9093f"},
    {0x10,2912,"13062ae08fb52627759bc09eb45b08a33c1b50678dec4416295259f5492a16d2"},
    {0x14,8,"deb9cc7f1a9cec4f23ddee703a60695deedd0255003e2d3fd3e1d35e4193820b"},
    {0x48,728,"e6ff459ebc86a128b3e37b46d41efd52eedbe5c955acf3d20dfbf99a33fb2557"},
    {0x1c,176,"41d855fd37c3eb4cf6c36ce02ec668c17b490174063e3b3cfbca70ff4a8ab7a7"}};
inline constexpr DigestRange kMaster[]={
    {0x30,12576,"2ef9019bff8626ddf9573b107d9fa13953ec63979221f60974ca6dad59ac223a"},
    {0x34,12576,"79d3c37e5372ebbc221f139360d47155047d0dc9adbf28b4f543b5d7e1ee4923"},
    {0x10,8384,"45a4066035d4e542782e0e2fdba2207ad589e242972a52fe33c29944d1bb6a41"},
    {0x14,8,"deb9cc7f1a9cec4f23ddee703a60695deedd0255003e2d3fd3e1d35e4193820b"},
    {0x48,2096,"e2276a8c27e9236753ab1a5c7a7ba6e39405d6f00160997ed91f84e64f775c28"},
    {0x1c,272,"2ffe26389ebb8b0560df0880271bb70bb2006494e4595fcd4d854eaf0f2c9b0a"}};
class Hasher {
    BCRYPT_ALG_HANDLE algorithm_=nullptr;BCRYPT_HASH_HANDLE hash_=nullptr;
public:
    Hasher() {
        if(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0)
            if(BCryptCreateHash(algorithm_,&hash_,nullptr,0,nullptr,0,0)<0)hash_=nullptr;
    }
    Hasher(const Hasher&)=delete;Hasher& operator=(const Hasher&)=delete;
    ~Hasher(){if(hash_)BCryptDestroyHash(hash_);if(algorithm_)BCryptCloseAlgorithmProvider(algorithm_,0);}
    bool Add(unsigned char* b,unsigned n){return hash_&&BCryptHashData(hash_,b,n,0)>=0;}
    bool Matches(const char* expected) {
        unsigned char digest[32]{};char hex[65]{};constexpr char digits[]="0123456789abcdef";
        if(!hash_||BCryptFinishHash(hash_,digest,32,0)<0)return false;
        for(unsigned i=0;i<32;++i){hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15];}
        return std::strcmp(hex,expected)==0;
    }
};
// UHD calcModelAddr normalizes +34 vec3s in-place (25CB97 -> EEDF ->
// 575FE0). Compare to pinned source directions, never normalize unknown input.
// A 5e-7 component bound covers float32 sqrt/reciprocal/product roundoff;
// separate unit-length validation rejects scaled copies and malformed values.
inline bool LoadedNormal(const float* actual,const float* expected) {
    double length2=0;
    for(unsigned j=0;j<3;++j){
        if(!std::isfinite(actual[j])||std::abs(double(actual[j])-expected[j])>5e-7)return false;
        length2+=double(actual[j])*actual[j];
    }
    return std::abs(length2-1.)<=1e-6;
}
template<class Memory> bool MatchLoadedNormals(Memory& memory,uint32_t address,const DigestRange* manifest) {
    const bool ordon=manifest==kOrdon;
    const bool campaign=manifest==kCampaignPl00;
    if(!ordon&&!campaign&&manifest!=kMaster)return false;
    const unsigned count=campaign?256:ordon?364:1048;
    const float (*expected)[3]=campaign?kCampaignPl00UnitNormals:ordon?kOrdonUnitNormals:kMasterUnitNormals;
    if(!Pointer(address)||!Range(address,count*12))return false;
    float block[16][3]{};
    for(unsigned at=0;at<count;){
        const unsigned n=count-at<16?count-at:16;
        if(!memory.Read(address+at*12,block,n*12))return false;
        for(unsigned i=0;i<n;++i)if(!LoadedNormal(block[i],expected[at+i]))return false;
        at+=n;
    }
    return true;
}
template<class Memory> bool MatchResource(Memory& memory,const unsigned char* header,const DigestRange* manifest) {
    for(unsigned i=0;i<6;++i){
        const auto& entry=manifest[i];const uint32_t p=U32(header+entry.field);
        if(!Pointer(p)||!Range(p,entry.size))return false;
        if(entry.field==0x34){if(!MatchLoadedNormals(memory,p,manifest))return false;continue;}
        Hasher hash;unsigned char buffer[256]{};
        for(unsigned at=0;at<entry.size;){
            const unsigned remaining=entry.size-at,n=remaining<sizeof(buffer)?remaining:sizeof(buffer);
            if(!memory.Read(p+at,buffer,n)||!hash.Add(buffer,n))return false;
            at+=n;
        }
        if(!hash.Matches(entry.sha))return false;
    }
    return true;
}
template<class Memory> class Watched {
    struct Block {uint32_t address=0;unsigned size=0;std::array<unsigned char,80> bytes{};};
    Memory& memory_;std::array<Block,64> blocks_{};unsigned count_=0;
public:
    explicit Watched(Memory& memory):memory_(memory){}
    bool Read(uint32_t p,void* out,unsigned n) {
        if(count_==blocks_.size()||!n||n>80||!Range(p,n))return false;
        auto& block=blocks_[count_];block.address=p;block.size=n;
        if(!memory_.Read(p,block.bytes.data(),n))return false;
        std::memcpy(out,block.bytes.data(),n);++count_;return true;
    }
    bool Word(uint32_t p,uint32_t& out){return Read(p,&out,4);}
    bool Matrix(uint32_t p,Mat34& out){
        float native[12]{};if(!Read(p,native,48))return false;
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<4;++j)out.v[i][j]=native[i*4+j];
        return Rigid(out);
    }
    bool Unchanged(){
        unsigned char now[80]{};
        for(unsigned i=0;i<count_;++i){const auto& b=blocks_[i];if(!memory_.Read(b.address,now,b.size)||std::memcmp(now,b.bytes.data(),b.size))return false;}
        return true;
    }
};
// What a full read established, kept by value to recognise the same sword on later frames (sword_trail_log.h).
// The addresses are compared with a fresh walk, never dereferenced.
struct Identity {uint32_t player=0,resource=0;Profile profile=Profile::Unknown;};
inline Identity IdentityOf(const Observation& o) {return {o.player,o.resource,o.profile};}
// The walk both reads share, every word through Watched: owners, model resource header, part chain and the four
// pose matrices. identify(header,found,manifest) names found.profile; a non-null manifest is hashed again after
// BuildBlade (ReadObservation), a null one is not (ReadPose).
template<class Memory,class Identify> bool Walk(Memory& memory,Slots slots,Observation& out,Identify&& identify) {
    out={};Observation found;Watched<Memory> watch(memory);
    if(!Pointer(slots.player)||!Pointer(slots.global)||!watch.Word(slots.player,found.player)||!Pointer(found.player)||
       !watch.Word(slots.global,found.global)||!Pointer(found.global))return false;
    uint32_t character=0,model_flags=0;unsigned char parts=0;
    if(!watch.Word(found.global+0x4fc8,character)||(character&255)!=0||((character>>8)&255)>3||
       !watch.Read(found.player+0x102,&parts,1)||parts!=119||!watch.Word(found.player+4,model_flags)||(model_flags&0x4000))return false;
    uint32_t nodes[16]{},node=0;unsigned count=0;
    if(!watch.Word(found.player+0x15c,node))return false;
    while(node){
        if(count==16||!Pointer(node))return false;
        for(unsigned i=0;i<count;++i)if(nodes[i]==node)return false;
        nodes[count++]=node;if(!watch.Word(node+0x14,node))return false;
    }
    if(count<3)return false;found.info=nodes[2];uint32_t info_flags=0;
    unsigned char header[80]{};
    if(!watch.Word(found.info+4,info_flags)||(info_flags&2)||!(info_flags&8)||
       !watch.Word(found.info+0xc,found.resource)||!Pointer(found.resource)||!watch.Read(found.resource,header,80)||
       header[0x18]!=1||header[0x19]!=119||(header[0x1a]!=1&&header[0x1a]!=2)||header[0x1b]!=0||header[0x2a]!=1||header[0x2b]!=0)return false;
    const DigestRange* manifest=nullptr;
    if(!identify(header,found,manifest))return false;
    uint32_t part=0,visited[11]{};
    if(!watch.Word(found.player+0xf4,part))return false;
    for(unsigned i=0;i<11;++i){
        if(!Pointer(part))return false;
        for(unsigned j=0;j<i;++j)if(visited[j]==part)return false;
        visited[i]=part;
        if(i<10&&!watch.Word(part+0xf4,part))return false;
    }
    found.part0=visited[0];found.part10=visited[10];
    if(!watch.Matrix(found.part0+0xc,found.pose.body0)||!watch.Matrix(found.part10+0xc,found.pose.bone10)||
       !watch.Matrix(found.part10+0xf8,found.pose.inverse_bind10)||!watch.Matrix(found.info+0x5c,found.pose.info))return false;
    const Binding binding{119,1,1,10,100,true,model_flags,info_flags};
    if(!BuildBlade(found.profile,found.pose,binding,found.blade)||(manifest&&!MatchResource(memory,header,manifest))||
       !watch.Unchanged())return false;
    out=found;return true; // Stable read only; no pose/attack synchronization claim.
}
template<class Memory> bool ReadObservation(Memory& memory,Slots slots,Observation& out) {
    return Walk(memory,slots,out,[&](const unsigned char* header,Observation& found,const DigestRange*& manifest) {
        if(header[0x1a]==1&&MatchResource(memory,header,kCampaignPl00)){found.profile=Profile::CampaignPl00;manifest=kCampaignPl00;}
        else if(header[0x1a]==2&&MatchResource(memory,header,kOrdon)){found.profile=Profile::Ordon;manifest=kOrdon;}
        else if(header[0x1a]==2&&MatchResource(memory,header,kMaster)){found.profile=Profile::MasterSword;manifest=kMaster;}
        else return false;
        return true;
    });
}
// Per-frame re-read after a full ReadObservation (the sword trail samples every frame of a spin): the same walk,
// matrices and Watched re-check, but the resource is accepted by identity (same player, resource address and
// material count as `known`) instead of hashed again. No bcrypt call.
template<class Memory> bool ReadPose(Memory& memory,Slots slots,const Identity& known,Observation& out) {
    out={};
    if(known.profile==Profile::Unknown||!known.player||!known.resource)return false;
    const unsigned char materials=known.profile==Profile::CampaignPl00?1:2;
    return Walk(memory,slots,out,[&](const unsigned char* header,Observation& found,const DigestRange*&) {
        if(found.player!=known.player||found.resource!=known.resource||header[0x1a]!=materials)return false;
        found.profile=known.profile;return true;
    });
}
// Callable in-process adapter. Does not install itself, call native code or write.
struct ProcessMemory {
    bool Read(uint32_t at,void* out,size_t n){
        SIZE_T read=0;return Range(at,n)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,n,&read)&&read==n;
    }
};
inline bool ObserveCurrent(Slots slots,Observation& out){ProcessMemory memory;return ReadObservation(memory,slots,out);}
} // namespace link_runtime::parry::sword::reader

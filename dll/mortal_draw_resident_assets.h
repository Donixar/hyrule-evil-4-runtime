#pragma once
#include "mortal_draw_dispatch.h"
#include <windows.h>
#include <bcrypt.h>
#include <limits>

// One pinned candidate per loaded module. There is deliberately no release,
// reallocate or replace API: engine references may outlive action completion.
namespace link_runtime::mortal_draw::resident_assets {
constexpr size_t fcv_size=196000,seq_size=160,total_size=fcv_size+seq_size;
constexpr char fcv_sha[]="12b34b14c7d443419d9ac8892bc270373b3c5cdda2bfd5110d6e8525eb6460de";
constexpr char seq_sha[]="2d1f3bf45dbbd082265b7062feeecc2e34f0a2727f6c86fec0982b03bde9dc1b";
enum class Result {Prepared,Reused,Invalid,CryptoUnavailable,AllocationFailed};
struct Status {unsigned allocations=0;size_t bytes=0;uint64_t last_token=0;};
namespace detail {
inline SRWLOCK lock=SRWLOCK_INIT;
inline unsigned char* base=nullptr;
inline uint64_t token=0;
struct Guard {Guard(){AcquireSRWLockExclusive(&lock);}~Guard(){ReleaseSRWLockExclusive(&lock);}Guard(const Guard&)=delete;Guard& operator=(const Guard&)=delete;};
inline bool Hash(const void* data,size_t size,unsigned char (&digest)[32]){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    bool ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,static_cast<PUCHAR>(const_cast<void*>(data)),static_cast<ULONG>(size),0)>=0;
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
inline bool Matches(const unsigned char (&digest)[32],const char* expected){
    constexpr char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32;++i)if(hex[digest[i]>>4]!=expected[2*i]||hex[digest[i]&15]!=expected[2*i+1])return false;
    return true;
}
inline uint64_t Next(uint64_t& previous){if(previous==(std::numeric_limits<uint64_t>::max)())return 0;return ++previous;}
}
// Inputs must be readable for the stated exact sizes throughout this call.
// They are original raw file bytes, never a copy of an already-relocated FCV.
inline Result Prepare(const void* fcv,size_t fcv_bytes,const void* seq,size_t seq_bytes){
    if(!fcv||!seq||fcv_bytes!=fcv_size||seq_bytes!=seq_size)return Result::Invalid;
    unsigned char a[32]{},b[32]{};
    if(!detail::Hash(fcv,fcv_bytes,a)||!detail::Hash(seq,seq_bytes,b))return Result::CryptoUnavailable;
    if(!detail::Matches(a,fcv_sha)||!detail::Matches(b,seq_sha))return Result::Invalid;
    detail::Guard guard;
    if(detail::base)return Result::Reused; // Do not copy over native relocation.
    auto* allocation=static_cast<unsigned char*>(VirtualAlloc(nullptr,total_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!allocation)return Result::AllocationFailed;
    std::memcpy(allocation,fcv,fcv_size);std::memcpy(allocation+fcv_size,seq,seq_size);
    // Validate the actual copied bytes before publishing any engine-visible pointer.
    const bool hashed=detail::Hash(allocation,fcv_size,a)&&detail::Hash(allocation+fcv_size,seq_size,b);
    if(!hashed||!detail::Matches(a,fcv_sha)||!detail::Matches(b,seq_sha)){
        VirtualFree(allocation,0,MEM_RELEASE);return hashed?Result::Invalid:Result::CryptoUnavailable;
    }
    detail::base=allocation;return Result::Prepared;
}
// Call at the admission boundary; a token is an action identity, not an actor
// generation or permission to dereference an old actor. Buffers are unchanged.
inline dispatch::Lease Acquire(){
    detail::Guard guard;
    if(!detail::base)return {};
    const auto next=detail::Next(detail::token);if(!next)return {};
    return {next,detail::base,detail::base+fcv_size,detail::base+fcv_size+4};
}
// A token from the same counter without a Mortal Draw buffer (the ocarina's own clips):
// dispatch::Runtime::Start only admits increasing tokens, so both users must share it.
inline uint64_t ReserveToken(){detail::Guard guard;return detail::Next(detail::token);}
inline Status Inspect(){detail::Guard guard;return {detail::base?1U:0U,detail::base?total_size:0,detail::token};}
}

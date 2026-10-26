#pragma once
// The game thread a hook serves, kept across the game replacing that thread (skills-fix-01, 22-oct).
//
// Cause. RE4 can end its game-logic thread and go on in a new one. Live, pid 34088 (22-oct): back at the title screen
// (global costume 0, never seen on an in-game load) and into a save, thread 3900 exited and 23228 (started 01:12:55)
// ran the player, the camera and the damage from then on. Every hook that bound the first game thread for the whole
// process (InterlockedCompareExchange(&thread,id,0), owner=thread) sent each later call straight to the native code:
// no player dispatch decide (spin requests expired, no Ending Blow, Back Slice, Shield Bash, Mortal Draw, the ocarina's
// O), no Z-target camera, no trail/charge/particle/arrow nodes, until the game restarted. In-game loads (Game Over,
// typewriter) keep the thread; earlier sessions never changed it.
//
// A Binding keeps the first thread, as before, and hands over to another one only once that thread has exited. The exit
// is read from a SYNCHRONIZE handle the bound thread opened on itself, never from its id (Windows reuses thread ids).
// Id and handle change together in one 64-bit word (x86: 32 bits each). A replaced handle stays open (another thread may
// still be testing it): one handle per game thread the game ever starts. No game memory, no allocation, no lock; the
// calling thread's LastError is kept.
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace link_runtime::thread_binding {
static_assert(sizeof(HANDLE)==4,"x86 only: the thread id and its handle share one 64-bit word");
static_assert(std::atomic<uint64_t>::is_always_lock_free,"the id and handle word must be lock free");
// Owner: the bound thread. Bound: the first thread, bound now. Rebound: the bound thread had exited and this one took
// over. Foreign: another thread is bound and alive (or its exit cannot be read: OpenThread failed when it bound).
enum class Claim {Owner,Bound,Rebound,Foreign};
inline bool Serves(Claim c){return c!=Claim::Foreign;}
inline const char* ClaimName(Claim c){
    return c==Claim::Owner?"owner":c==Claim::Bound?"bound":c==Claim::Rebound?"rebound":"foreign";
}
class Binding {
    std::atomic<uint64_t> word_{0};   // low 32 bits: the thread id (0 = none); high 32 bits: its SYNCHRONIZE handle
    std::atomic<unsigned> rebinds_{0};
    std::atomic<DWORD> previous_{0};  // the thread the last rebind replaced (log only)
    static uint64_t Pack(DWORD id,HANDLE h){return uint64_t(id)|uint64_t(reinterpret_cast<uintptr_t>(h))<<32;}
    static HANDLE Handle(uint64_t word){return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(word>>32));}
    static bool Signaled(uint64_t word){const HANDLE h=Handle(word);return h&&WaitForSingleObject(h,0)==WAIT_OBJECT_0;}
public:
    DWORD Id() const {return static_cast<DWORD>(word_.load(std::memory_order_acquire));}
    unsigned Rebinds() const {return rebinds_.load(std::memory_order_acquire);}
    DWORD Previous() const {return previous_.load(std::memory_order_acquire);}
    // Has thread `id` exited, by the handle this binding holds for it? False for any thread it does not hold.
    bool Exited(DWORD id) const {
        const uint64_t now=word_.load(std::memory_order_acquire);
        if(!id||static_cast<DWORD>(now)!=id)return false;
        const DWORD error=GetLastError();const bool exited=Signaled(now);SetLastError(error);return exited;
    }
    // The calling thread: the one this binding serves? The first caller binds; a later one takes over only from an
    // exited thread. The owner's path is one atomic load.
    Claim Take(){
        const DWORD self=GetCurrentThreadId();
        uint64_t now=word_.load(std::memory_order_acquire);
        const DWORD bound=static_cast<DWORD>(now);
        if(bound==self)return Claim::Owner;
        const DWORD error=GetLastError();
        if(bound&&!Signaled(now)){SetLastError(error);return Claim::Foreign;}
        const HANDLE mine=OpenThread(SYNCHRONIZE,FALSE,self);
        if(!word_.compare_exchange_strong(now,Pack(self,mine),std::memory_order_acq_rel)){
            if(mine)CloseHandle(mine);
            SetLastError(error);return Claim::Foreign;   // another thread bound in between: it is running
        }
        SetLastError(error);
        if(!bound)return Claim::Bound;
        previous_.store(bound,std::memory_order_release);rebinds_.fetch_add(1,std::memory_order_acq_rel);
        return Claim::Rebound;
    }
    // Forget the thread. Only while no Take can run (a runtime's Start, before its hooks or callers).
    void Reset(){
        const uint64_t word=word_.exchange(0,std::memory_order_acq_rel);
        if(const HANDLE h=Handle(word))CloseHandle(h);
        rebinds_.store(0,std::memory_order_release);previous_.store(0,std::memory_order_release);
    }
};
// A one-thread latch kept in a LONG that other code reads as it is (the parry's owner_thread): the first caller binds
// it, as before; another thread takes it over only from a thread `exits` holds and that has exited. A latch written by
// anything else (a fixture) is honoured as it is: its thread's exit is never assumed.
inline bool ClaimLatch(volatile LONG& latch,Binding& exits){
    const LONG self=static_cast<LONG>(GetCurrentThreadId());
    const LONG bound=InterlockedCompareExchange(&latch,self,0);
    if(bound==self)return true;
    if(bound&&(!exits.Exited(static_cast<DWORD>(bound))||InterlockedCompareExchange(&latch,self,bound)!=bound))return false;
    exits.Take();return true;   // Bound (first) or Rebound (from the exited thread): this thread's exit handle
}
// Would ClaimLatch take the calling thread? No write: the latch is free, already this thread's, or held by an exited one.
inline bool LatchAdmits(volatile LONG& latch,const Binding& exits){
    const LONG bound=InterlockedCompareExchange(&latch,0,0);
    return !bound||bound==static_cast<LONG>(GetCurrentThreadId())||exits.Exited(static_cast<DWORD>(bound));
}
} // namespace link_runtime::thread_binding

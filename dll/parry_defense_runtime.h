#pragma once
#include "parry_commit.h"
#include "parry_contact_diagnostic.h"
#include "parry_input_runtime.h"

// Manual frontal timing parry, approved by Donald after trial15. Exact blade
// intersection remains diagnostic, not admission. Rear shield policy is separate.
namespace link_runtime::parry::defense_runtime {
namespace detail {
template<class Sources> bool Approve(Sources& source,const Candidate& candidate,const owner::Observation& expected){
    owner::Observation before,after;contact_diagnostic::Result contact;
    return source.Owner(candidate,before)&&owner::Equal(before,expected)&&
        source.Contact(candidate,contact)&&contact.front_known&&contact.front.frontal&&
        source.Owner(candidate,after)&&owner::Equal(after,expected)&&source.Consume();
}
}
enum class Stage:LONG {Idle,Input,Owner,RecoilPrepare,ApprovalOwner,Contact,Consume,Approved,Committed,Partial};
struct Runtime {commit::Runtime reaction;front_native::Code front;volatile LONG last_stage=0;};
inline void Mark(Runtime& r,Stage s){InterlockedExchange(&r.last_stage,static_cast<LONG>(s));}
struct Scope {Runtime* runtime=nullptr;bool tainted=false;};
inline __declspec(thread) Scope* scope=nullptr;
namespace detail {
struct NativeSources {
    Runtime& runtime;
    bool Owner(const Candidate& c,owner::Observation& out){Mark(runtime,Stage::ApprovalOwner);return owner::ObserveCurrent(runtime.reaction.access,runtime.reaction.code.owner_code,c,out);}
    bool Contact(const Candidate& c,contact_diagnostic::Result& out){Mark(runtime,Stage::Contact);return contact_diagnostic::ObserveCurrent(runtime.reaction.access,runtime.reaction.code.owner_code,c,out,runtime.front);}
    bool Consume(){Mark(runtime,Stage::Consume);const bool ok=scope&&!scope->tainted&&input::ConsumeCurrent(true);if(ok)Mark(runtime,Stage::Approved);return ok;}
};
inline bool __cdecl ApproveCurrent(const Candidate& c,const owner::Observation& expected){
    if(!scope||scope->tainted||!scope->runtime)return false;
    NativeSources source{*scope->runtime};
    return Approve(source,c,expected)&&!scope->tainted;
}
}
// Only a confirmed native commit may
// authorize EAX=0; failed or partial commits preserve the incoming damage.
inline commit::Result AttemptCurrent(Runtime& runtime,const Candidate& candidate){
    const DWORD error=GetLastError();commit::Result result=commit::Result::NotCommitted;
    if(scope){scope->tainted=true;if(commit::scope)commit::scope->tainted=true;SetLastError(error);return result;}
    if(!runtime.reaction.enabled||!runtime.front.verified||runtime.front.image_base!=runtime.reaction.access.image_base){SetLastError(error);return result;}
    __declspec(align(16)) unsigned char fp[512];const unsigned mxcsr=0x1F80;
    __asm {fxsave fp
        fninit
        ldmxcsr mxcsr}
    Scope current{&runtime,false};scope=&current;
    __try {
        __try {
            owner::Observation expected;
            Mark(runtime,Stage::Input);
            if(input::RefreshAtContact()&&!current.tainted){
                Mark(runtime,Stage::Owner);
                if(owner::ObserveCurrent(runtime.reaction.access,runtime.reaction.code.owner_code,candidate,expected)&&!current.tainted){
                    Mark(runtime,Stage::RecoilPrepare);
                    result=commit::CommitCurrent(runtime.reaction,candidate,expected,detail::ApproveCurrent);
                }
            }
            if(current.tainted&&result==commit::Result::Committed){
                result=commit::Result::PartialFailure;
                InterlockedExchange(&runtime.reaction.quarantined,1);runtime.reaction.enabled=false;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){result=commit::Result::NotCommitted;}
    } __finally {scope=nullptr;__asm {fxrstor fp}
        SetLastError(error);}
    if(result==commit::Result::Committed)Mark(runtime,Stage::Committed);
    if(result==commit::Result::PartialFailure)Mark(runtime,Stage::Partial);
    return result;
}
}

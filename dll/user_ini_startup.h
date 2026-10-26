#pragma once
// user-ini-02 (22-oct; docs/panel-ajustes-2026-10-22.md): every startup read of Bin32/link_runtime.ini honours
// Bin32/link_runtime.user.ini first, key by key, in every section ([LinkRuntime], [ZTarget], [Ocarina],
// [FairyRevive], [Rupees], ...). The F8 panel's "Juego" tab writes its switches there.
//
// How: link_runtime.cpp includes this header before the first header that reads the INI (ztarget_runtime.h), and the
// two macros at the end send every later GetPrivateProfileIntW / GetPrivateProfileStringW through IntW / StringW
// below: one lookup for every key, whatever helper or ReadConfig lambda asks for it. A read of a file named
// link_runtime.ini (any folder) first asks its sibling link_runtime.user.ini for the same section and key; a
// non-empty value there is read from that file with the same API (so integers, quotes, truncation and the return
// value are exactly the API's), missing or empty falls back to the main INI unchanged. Any other file, a null section
// or key (enumeration) or a sibling path that does not fit MAX_PATH passes through untouched.
//
// Startup-only semantics stay: these keys are read once, as before; only the 28 cosmetic keys reload live
// (user_ini.h). Each key served from the override is remembered (deduplicated, first-seen order) for the
// `user_ini startup overrides=N keys=...` line (FormatStartup). No game access; usable from the native tests.
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <cwchar>

namespace link_runtime::user_ini::startup {
inline constexpr const wchar_t* kMainName=L"link_runtime.ini";
inline constexpr const wchar_t* kUserName=L"link_runtime.user.ini";
constexpr size_t kMaxKeys=256;     // more than every key the DLL reads (about 180 in all sections)
constexpr size_t kNameChars=64;    // "Section.Key", cut (still deduplicated) beyond this

inline wchar_t Fold(wchar_t c){return c>=L'A'&&c<=L'Z'?wchar_t(c+(L'a'-L'A')):c;}
inline bool SameNoCase(const wchar_t* a,const wchar_t* b){
    if(!a||!b)return false;
    for(;*a&&*b;++a,++b)if(Fold(*a)!=Fold(*b))return false;
    return !*a&&!*b;
}
// The file name of `path`: after its last \ or /, or the whole of it.
inline const wchar_t* FileName(const wchar_t* path){
    const wchar_t* name=path;
    for(const wchar_t* p=path;p&&*p;++p)if(*p==L'\\'||*p==L'/')name=p+1;
    return name;
}
// true when `path` names a file called link_runtime.ini (ASCII case-insensitive), in a folder.
inline bool IsMainIni(const wchar_t* path){
    if(!path||!*path)return false;
    const wchar_t* name=FileName(path);
    return name!=path&&SameNoCase(name,kMainName);
}
// `path` with its file name replaced by link_runtime.user.ini. false (and `out` empty) when it has no folder part or
// the result does not fit `count`.
inline bool UserPath(const wchar_t* path,wchar_t* out,size_t count){
    if(!out||!count)return false;
    out[0]=0;
    if(!path)return false;
    const wchar_t* name=FileName(path);
    if(name==path)return false;
    const size_t dir=size_t(name-path),file=std::wcslen(kUserName);
    if(dir+file+1>count)return false;
    for(size_t i=0;i<dir;++i)out[i]=path[i];
    for(size_t i=0;i<=file;++i)out[dir+i]=kUserName[i];
    return true;
}

// The keys served from the override file since the process started (deduplicated, case-insensitive).
class Served {
    wchar_t names_[kMaxKeys][kNameChars]{};size_t count_=0;bool full_=false;SRWLOCK lock_=SRWLOCK_INIT;
    // "Key" for [LinkRuntime] (as the other user_ini lines name them), "Section.Key" for the other sections.
    static void Name(const wchar_t* section,const wchar_t* key,wchar_t (&out)[kNameChars]){
        size_t n=0;
        auto put=[&](const wchar_t* s){for(;s&&*s&&n+1<kNameChars;++s)out[n++]=*s;};
        if(!SameNoCase(section,L"LinkRuntime")){put(section);put(L".");}
        put(key);out[n]=0;
    }
public:
    void Add(const wchar_t* section,const wchar_t* key){
        wchar_t name[kNameChars];Name(section,key,name);
        AcquireSRWLockExclusive(&lock_);
        bool seen=false;
        for(size_t i=0;i<count_&&!seen;++i)seen=SameNoCase(names_[i],name);
        if(!seen){
            if(count_<kMaxKeys){for(size_t i=0;i<kNameChars;++i)names_[count_][i]=name[i];++count_;}
            else full_=true;
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    // "user_ini startup overrides=N keys=A,B,Section.C" ("-" when none). N counts every key served; a name that does not
    // fit `size` whole ends the list with "+". Returns the snprintf length (<= 0 or >= size: not written whole).
    int Format(char* out,size_t size){
        if(!out||!size)return -1;
        char keys[900];size_t used=0;keys[0]=0;
        AcquireSRWLockShared(&lock_);
        const size_t count=count_;const bool full=full_;
        for(size_t i=0;i<count;++i){
            const wchar_t* w=names_[i];size_t n=std::wcslen(w);
            const size_t sep=used?1:0;
            if(used+sep+n+1>sizeof(keys)){
                if(used+sep+2<=sizeof(keys)){if(sep)keys[used++]=',';keys[used++]='+';keys[used]=0;}
                break;
            }
            if(sep)keys[used++]=',';
            for(size_t k=0;k<n;++k)keys[used++]=w[k]>0x20&&w[k]<0x7F?char(w[k]):'?';
            keys[used]=0;
        }
        ReleaseSRWLockShared(&lock_);
        return std::snprintf(out,size,"user_ini startup overrides=%u%s keys=%s",unsigned(count),full?"+":"",used?keys:"-");
    }
    size_t Count(){AcquireSRWLockShared(&lock_);const size_t n=count_;ReleaseSRWLockShared(&lock_);return n;}
    void Reset(){AcquireSRWLockExclusive(&lock_);count_=0;full_=false;ReleaseSRWLockExclusive(&lock_);}   // tests
};
inline Served& served(){static Served s;return s;}

// When this read should come from the override file: `file` is a link_runtime.ini, its sibling override fits
// `count`, and that file has a non-empty value for section/key. Then `user` holds the override's path and the key is
// remembered. A probe of two characters is enough: the API returns 1 for any non-empty value (cut to fit).
inline bool FromOverride(const wchar_t* section,const wchar_t* key,const wchar_t* file,wchar_t* user,size_t count){
    if(!section||!*section||!key||!*key||!IsMainIni(file)||!UserPath(file,user,count))return false;
    wchar_t probe[2]{};
    if(!::GetPrivateProfileStringW(section,key,L"",probe,2,user))return false;
    served().Add(section,key);
    return true;
}
// GetPrivateProfileIntW with the override first (same arguments, same result rules).
inline UINT IntW(LPCWSTR section,LPCWSTR key,INT fallback,LPCWSTR file){
    wchar_t user[MAX_PATH];
    return ::GetPrivateProfileIntW(section,key,fallback,FromOverride(section,key,file,user,MAX_PATH)?user:file);
}
// GetPrivateProfileStringW with the override first (same arguments, same result rules).
inline DWORD StringW(LPCWSTR section,LPCWSTR key,LPCWSTR fallback,LPWSTR out,DWORD size,LPCWSTR file){
    wchar_t user[MAX_PATH];
    return ::GetPrivateProfileStringW(section,key,fallback,out,size,FromOverride(section,key,file,user,MAX_PATH)?user:file);
}
inline int FormatStartup(char* out,size_t size){return served().Format(out,size);}
} // namespace link_runtime::user_ini::startup

// From here on in the including file every INI read asks the override first (see the top of this file).
#define GetPrivateProfileIntW ::link_runtime::user_ini::startup::IntW
#define GetPrivateProfileStringW ::link_runtime::user_ini::startup::StringW

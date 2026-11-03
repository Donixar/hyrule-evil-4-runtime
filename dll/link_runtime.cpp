#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <unknwn.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <bcrypt.h>
#include <dbghelp.h>
#pragma comment(lib,"bcrypt.lib")
#include "link_runtime_core.h"
#include "link_runtime_log.h"
#include "runtime_paths.h"
#include "link_runtime_version.h"
#include "link_runtime_health.h"
#include "low_health.h"
#include "link_runtime_trace.h"
#include "link_runtime_damage.h"
#include "shield_runtime.h"
#include "parry_observation_runtime.h"
#include "parry_defense_runtime.h"
#include "chapter_runtime.h"
#include "mortal_draw_trial_control.h"
#include "mortal_draw_dispatch_hooks.h"
#include "mortal_draw_voice.h"
#include "parry_audio.h"
#include "parry_spark_runtime.h"
#include "parry_general_runtime.h"
#include "parry_pushback.h"
#include "parry_observe_log.h"
#include "parry_line.h"
#include "parry_direct_runtime.h"
#include "ocarina_session.h"
#include "ocarina_voice.h"
#include "ocarina_pitch_assets.h"
#include "ocarina_freeze.h"
#include "ocarina_mount.h"
#include "ocarina_weapon.h"
#include "ocarina_item.h"
#include "ocarina_manual.h"
#include "ocarina_runtime.h"
#include "ocarina_songs.h"
#include "ocarina_staff.h"
#include "hud_tp.h"
#include "ocarina_song_effects.h"
#include "save_anywhere.h"
#include "combat_runtime.h"
#include "mortal_draw_runtime.h"
#include "mortal_draw_magnet.h"
#include "parry_reaction.h"
#include "shield_guard.h"
#include "shield_guard_assets.h"
#include "parry_riposte.h"
#include "native_hitpart.h"
#include "native_pins.h"
#include "fairy_revive.h"
#include "boss_music_duck.h"
#include "event_sheath.h"
#include "rupee_colors.h"
#include "case_items.h"
#include "foreign_patch.h"
#include "game_clock.h"
#include "mortal_draw_start_gate.h"
#include "mortal_draw_prompt.h"
#define LINK_RUNTIME_CAMERA_INTERP_INSTALL 1
#include "camera_interp_fix.h"
#include "re4fps_guard.h"
#include "gigante_tree_guard.h"
#include "grab_hit_once.h"
#include "plaga_parasite_once.h"
#include "chain_sound_once.h"
#include "em3f_attack_timer.h"
#include "case_key_debounce.h"
#include "krauser_bow_pilot.h"
#include "crash_dump.h"
// user-ini-02: from here on every GetPrivateProfileIntW/StringW of link_runtime.ini asks link_runtime.user.ini first
// (the first INI reader, ztarget_runtime.h, comes next; every read in this file is below).
#include "user_ini_startup.h"
#include "ztarget_runtime.h"
#include "rear_shield_observe.h"
#include "rear_shield_audio.h"
#include "sword_trail_probe.h"
#include "user_ini.h"
#include "MinHook.h"

static_assert(sizeof(void*) == 4, "RE4 UHD proxy must be built for x86");
static HMODULE g_module = nullptr;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static INIT_ONCE g_forward_once = INIT_ONCE_STATIC_INIT;
using DirectInputCreate = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInputCreate g_forward = nullptr;
static volatile LONG g_input_ready = 0;
static bool g_rear_observe=false,g_rear_active=false;
// [LinkRuntime] LogLevel (link_runtime_log.h). Fixed by the worker before the log opens and before
// any hook exists; read-only afterwards on every thread.
static unsigned g_log_level = link_runtime::log::kNormal;

static void LogWrite(unsigned line_level, const char* format, va_list args) {
    if (g_log == INVALID_HANDLE_VALUE || !link_runtime::log::Writes(g_log_level, line_level)) return;
    char message[1024] = {}, line[1100] = {};
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    int length = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%lu] %s\r\n", GetTickCount(), message);
    DWORD written = 0;
    if (length > 0) WriteFile(g_log, line, static_cast<DWORD>(length), &written, nullptr);
}
// Startup, errors and feature events: LogLevel 1 and 2. Arguments are always evaluated by the
// caller (some lines submit a sound in an argument), so the level never changes behaviour.
static void Log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogWrite(link_runtime::log::kNormal, format, args);
    va_end(args);
}
// Periodic stats and observer/diagnostic samples: LogLevel 2 only.
static void LogV(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogWrite(link_runtime::log::kVerbose, format, args);
    va_end(args);
}

// ---- re4_tweaks coexistence (docs/fps-coexistence-2026-10-12.md) ------------------------------
// A foreign form is accepted only when its target is code of another LOADED module: committed
// MEM_IMAGE, executable protection, inside an IMAGE_SCN_MEM_EXECUTE section, not bio4.exe, not us.
static bool ForeignModule(uint32_t target,HMODULE& module,uint32_t& offset){
    module=nullptr;offset=0;HMODULE m=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(target)),&m)||!m)return false;
    if(m==GetModuleHandleW(nullptr)||m==g_module)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(target)),&info,sizeof(info))||
       info.State!=MEM_COMMIT||info.Type!=MEM_IMAGE)return false;
    if(!(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    const uint32_t base=reinterpret_cast<uint32_t>(m);bool code=false;
    __try{code=link_runtime::foreign::CodeRva(reinterpret_cast<const unsigned char*>(m),0x1000,target-base);}
    __except(EXCEPTION_EXECUTE_HANDLER){code=false;}
    if(!code)return false;
    module=m;offset=target-base;return true;
}
static bool ForeignCodeProbe(uint32_t target,void*){HMODULE m=nullptr;uint32_t o=0;return ForeignModule(target,m,o);}
static const link_runtime::shield::ForeignProbe g_foreign_probe{&ForeignCodeProbe,nullptr};
static void LogForeignPatch(const char* who,const char* form,uint32_t site_rva,uint32_t target){
    HMODULE m=nullptr;uint32_t offset=0;wchar_t path[MAX_PATH]{};
    const bool ok=ForeignModule(target,m,offset)&&GetModuleFileNameW(m,path,MAX_PATH);
    const wchar_t* name=path;for(const wchar_t* c=path;*c;++c)if(*c==L'\\'||*c==L'/')name=c+1;
    Log("foreign_patch site=0x%X form=%s who=%s module=%ls offset=0x%X target=0x%08X accepted=%u",
        site_rva,form,who,ok?name:L"?",offset,target,ok?1U:0U);
}

using DamageValue = int(__cdecl*)(void*, unsigned, int);
static DamageValue g_damage_original = nullptr;
static volatile LONG g_trace_remaining = 0;
static uint32_t g_damage_player_slot = 0, g_damage_global_slot = 0;
static uintptr_t g_damage_caller = 0, g_damage_image = 0;
static unsigned g_damage_image_size = 0;

struct LocalMemory {
    bool Read32(uint32_t address, uint32_t& value) {
        __try { value = *reinterpret_cast<volatile uint32_t*>(address); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Compare32(uint32_t address, uint32_t expected, uint32_t desired) {
        if (address & 3) return false;
        __try {
            LONG previous = InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(address),
                                                        static_cast<LONG>(desired), static_cast<LONG>(expected));
            return static_cast<uint32_t>(previous) == expected;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
};

// Game time (game_clock.h, docs/fps-coexistence-2026-10-12.md 3): frame-counted timers count
// 60 Hz frames of the native deltaTime (GLOBAL_WK+0x70) instead of logic ticks (+0x530C).
// At 60 FPS the clock equals the counter; under re4_fps 165 a tick is 30/165*2 frames.
// Game thread only (arbiter exit, player handler, MotionMove detour).
static link_runtime::game_clock::Clock g_game_clock;
static bool GameGlobalRead(uint32_t global_slot,uint32_t offset,uint32_t& out){
    LocalMemory m;uint32_t g=0;out=0;
    return global_slot&&m.Read32(global_slot,g)&&link_runtime::IsPointer(g)&&m.Read32(g+offset,out);
}
static float GameDeltaTime(uint32_t global_slot){
    uint32_t bits=0;float dt=link_runtime::game_clock::kDefaultDt;
    if(GameGlobalRead(global_slot,link_runtime::game_clock::kDeltaTimeOffset,bits))std::memcpy(&dt,&bits,4);
    return dt;
}
// This tick in 60 Hz frames (1.0 at 60 FPS; unreadable or out-of-range deltaTime counts 1.0).
static double GameStep(uint32_t global_slot){return link_runtime::game_clock::Step60(GameDeltaTime(global_slot));}
// Game time now. An unreadable counter observes 0, like CmbFrame (the legacy comparisons saw 0).
static double GameNow(uint32_t global_slot){
    uint32_t frame=0;GameGlobalRead(global_slot,link_runtime::game_clock::kFrameCounterOffset,frame);
    return g_game_clock.Observe(frame,GameDeltaTime(global_slot));
}

// Combat runtime (definitions below the ocarina block). Every entry is inert while all of its
// [LinkRuntime] keys are 0 (combat_runtime.h).
static bool CmbAdapterOn();
static int CmbAdapterResult(uintptr_t caller,void* target,unsigned weapon,int original);

static bool TakeTraceSlot() {
    LONG remaining = InterlockedCompareExchange(&g_trace_remaining, 0, 0);
    while (remaining > 0) {
        LONG seen = InterlockedCompareExchange(&g_trace_remaining, remaining - 1, remaining);
        if (seen == remaining) return true;
        remaining = seen;
    }
    return false;
}

static int TargetKind(void* target) {
    __try { return *reinterpret_cast<volatile unsigned char*>(reinterpret_cast<uintptr_t>(target) + 0x100); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static int __cdecl TraceDamage(void* target, unsigned weapon, int proximity) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool verified = caller == g_damage_caller && g_damage_caller != 0;
    LocalMemory memory;
    link_runtime::DamageSnapshot before, after;
    const DWORD initial_error = GetLastError();
    link_runtime::ReadDamageSnapshot(memory, g_damage_player_slot, g_damage_global_slot,
                                     reinterpret_cast<uint32_t>(target), verified, before);
    SetLastError(initial_error);
    // Original exactly once, preserving every argument. No HP/table/range writes.
    const int original = g_damage_original(target, weapon, proximity);
    const DWORD original_error = GetLastError();
    link_runtime::ReadDamageSnapshot(memory, g_damage_player_slot, g_damage_global_slot,
                                     reinterpret_cast<uint32_t>(target), verified, after);
    // Shared native-hit adapter (MortalDrawDamage / ParryRiposte). Without those keys the native
    // value is returned unchanged; TraceKnife only observes.
    const int result = CmbAdapterOn() ? CmbAdapterResult(caller, target, weapon, original) : original;
    if (TakeTraceSlot()) {
        const unsigned rva = caller >= g_damage_image && caller - g_damage_image < g_damage_image_size
            ? static_cast<unsigned>(caller - g_damage_image) : 0xFFFFFFFFU;
        Log("GetWepDmVal target=%p id=%d weapon=0x%X proximity=%d caller=%p rva=0x%X original=%d result=%d "
            "snapshot=%u gameplay=%u caller_ok=%u native_weapon=0x%X hp=%d/%d",
            target, TargetKind(target), weapon, proximity, reinterpret_cast<void*>(caller), rva, original, result,
            before.valid && after.valid ? 1U : 0U, before.gameplay && after.gameplay ? 1U : 0U,
            verified ? 1U : 0U, before.native_weapon, before.hp, before.max_hp);
    }
    SetLastError(original_error);
    return result;
}

static void* FindDamageValue(unsigned char* base, unsigned image_size) {
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    // UHD cdecl GetWepDmVal prologue, weapon range guard and target kind read.
    static const unsigned char pattern[] = {
        0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x0C, 0x57, 0x83, 0xFE, 0x31, 0x72, 0x05,
        0xBE, 0x02, 0x00, 0x00, 0x00, 0x8B, 0x45, 0x08, 0x0F, 0xB6, 0x80, 0x00, 0x01, 0x00, 0x00
    };
    void* found = nullptr;
    unsigned hits = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        unsigned begin = section[s].VirtualAddress, size = section[s].Misc.VirtualSize;
        if (begin >= image_size || size > image_size - begin) continue;
        for (unsigned i = 0; i + sizeof(pattern) <= size; ++i)
            if (!std::memcmp(base + begin + i, pattern, sizeof(pattern))) {
                found = base + begin + i; ++hits;
            }
    }
    Log("GetWepDmVal pattern matches=%u", hits);
    return hits == 1 ? found : nullptr;
}

static bool StartDamageHook(unsigned char* base, unsigned image_size, unsigned limit, bool trace,
                            uint32_t player_slot, uint32_t global_slot, bool adapter = false) {
    void* target = FindDamageValue(base, image_size);
    if (!target) { Log("Damage hook disabled: GetWepDmVal pattern not unique"); return false; }
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    const size_t callee = static_cast<unsigned char*>(target) - base;
    std::vector<link_runtime::ExecutableRegion> executable_regions;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        executable_regions.push_back({section[s].VirtualAddress, section[s].Misc.VirtualSize});
    }
    const size_t caller_rva = link_runtime::FindSwordExecutableCaller(base, image_size, callee,
        executable_regions.data(), executable_regions.size());
    // Immutable context is published BEFORE the hook can run on game threads.
    g_damage_player_slot = player_slot; g_damage_global_slot = global_slot;
    g_damage_image = reinterpret_cast<uintptr_t>(base); g_damage_image_size = image_size;
    g_damage_caller = caller_rva ? reinterpret_cast<uintptr_t>(base + caller_rva) : 0;
    Log("Damage hook knife caller_rva=0x%X slots=%u", static_cast<unsigned>(caller_rva), player_slot && global_slot ? 1U : 0U);
    if (!trace && !adapter) { Log("no valid damage feature/trace; no damage hook installed"); return false; }
    MH_STATUS status = MH_Initialize();
    // The trace and the combat adapter both accept a MinHook already initialised by an earlier feature (e.g. CaseKeyDebounce).
    const bool own_minhook = status == MH_OK;
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) { Log("TraceKnife disabled: MinHook init %s", MH_StatusToString(status)); return false; }
    status = MH_CreateHook(target, reinterpret_cast<void*>(&TraceDamage), reinterpret_cast<void**>(&g_damage_original));
    if (status != MH_OK) {
        Log("TraceKnife disabled: MinHook create %s", MH_StatusToString(status)); if (own_minhook) MH_Uninitialize(); return false;
    }
    InterlockedExchange(&g_trace_remaining, trace ? static_cast<LONG>(limit) : 0);
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        Log("TraceKnife disabled: MinHook enable %s", MH_StatusToString(status));
        MH_RemoveHook(target); if (own_minhook) MH_Uninitialize(); g_damage_original = nullptr; return false;
    }
    Log("Damage hook enabled: GetWepDmVal=%p trace_limit=%u; original called once",
        target, trace ? limit : 0);
    if (adapter) Log("Damage hook carries the shared native-hit adapter: Resolve, native value otherwise");
    return true;
}

static bool QualifyChapterImage(unsigned char* base,unsigned size,link_runtime::chapter::NativeCode& out) {
    // Startup-only qualification. Read faults cannot escape into the game; originals
    // are never called in this boundary and are never caught/retried by chapter hooks.
    __try {out=link_runtime::chapter::FindNative(base,size,reinterpret_cast<uint32_t>(base),&g_foreign_probe);return out.verified;}
    __except(EXCEPTION_EXECUTE_HANDLER) {out={};return false;}
}
static bool StartChapterHooks(unsigned char* base,unsigned size,const link_runtime::chapter::Config& settings) {
    using namespace link_runtime::chapter;
    try {
        NativeCode native;
        if(!QualifyChapterImage(base,size,native)||!detail::Complete(native)) {
            Log("ChapterProgression disabled: native chapter/mode/caller/ABI qualification failed");return false;
        }
        for(unsigned i=0;i<native.foreign_windows&&i<3;++i)
            LogForeignPatch("ChapterProgression","inline_call",native.foreign_site_rva[i],native.foreign_target[i]);
        const MH_STATUS status=runtime::Start(native,settings);
        if(status!=MH_OK) {Log("ChapterProgression disabled: MinHook %s; owned rollback only",MH_StatusToString(status));return false;}
        Log("chapter hooks enabled progression=%u master_from_castle=%u trace=%u selector=0x%X loader=0x%X; lifecycle thread unattributed",
            settings.enabled?1U:0U,settings.master_from_castle?1U:0U,settings.trace?settings.limit:0,native.selector_rva,native.loader_rva);return true;
    } catch(...) {Log("ChapterProgression disabled: startup qualification exception");return false;}
}
static bool ChapterIniSwitch(const wchar_t* path,const wchar_t* name) {
    wchar_t value[32]{};
    return GetPrivateProfileStringW(L"LinkRuntime",name,L"",value,32,path)==1&&link_runtime::chapter::ExactOne(value);
}
static void PollChapter() {
    link_runtime::chapter::TraceRecord record;
    // Bounded copied records only. No game-memory read or write on this worker path.
    for(unsigned i=0;i<64&&link_runtime::chapter::runtime::Pop(record);++i)
        Log("chapter entry=%u caller=0x%X thread=%u attributed=%u valid=%u chapter=%u "
            "raw=%u/%u effective=%u/%u expected=%016llX desired=%016llX applied=%u original_calls=%u completed=%u",
            record.event.entry==link_runtime::chapter::Entry::room_loader?1U:0U,record.event.caller_return,record.thread,
            record.attributed?1U:0U,record.snapshot.valid?1U:0U,record.snapshot.chapter,
            record.event.type,record.event.costume,record.effective_type,record.effective_costume,
            record.plan.expected,record.plan.desired,record.applied?1U:0U,record.original_calls,record.completed?1U:0U);
}

static link_runtime::parry::defense_runtime::Runtime g_parry_defense;
static link_runtime::parry::audio::Events g_parry_audio;
static link_runtime::parry::spark::Code g_parry_sparks;
static volatile LONG g_parry_spark_submitted=0;
static volatile LONG g_parry_defense_active=0,g_parry_attempts=0,g_parry_committed=0,g_parry_partial=0;
// Tier 2 (general block, docs/parry-two-tier-2026-10-12.md): settings fixed before the resolver.
static link_runtime::parry::general::Runtime g_gen;
static void __cdecl ParryDirectAudio(){g_parry_audio.Publish(true,GetTickCount64());}
static bool g_parry_line_requested=true;
static bool __cdecl ResolveParryLine(const link_runtime::parry::Candidate& candidate){
    if(!link_runtime::parry::line::Resolve(candidate))return false;
    g_parry_audio.Publish(true,GetTickCount64());
    return true;
}
static uint32_t g_parry_window_ms=link_runtime::defense::kParryWindowDefault;   // ParryWindowMs, read before the transport starts
static void CombatCommitted(const link_runtime::parry::Candidate& candidate,LONG serial);
static void CombatGeneralBlocked(uint32_t serial,uint32_t owner_id);
static void ParryHeavyBlock(const link_runtime::parry::general::Applied& applied);   // parry-heavy-01
static bool GuardResolve(const link_runtime::parry::Candidate& candidate);   // tier 3, shield-guard-01
// Order (section 2.1): tier 1 first. Committed ends it; PartialFailure keeps the damage and skips
// tier 2 for this invocation; NotCommitted falls to tier 2, which may use the same press only when
// tier 1 consumed it and wrote nothing (Approved + NotCommitted). Tier 1's quarantine does not
// switch tier 2 off: tier 2 writes nothing in the game except the thrown-weapon knife record.
// Game time for the post-parry grace window (game thread, inside the resolver).
static uint32_t g_parry_global_slot=0;
static double ParryGameNow(){return GameNow(g_parry_global_slot);}
static bool __cdecl ResolveParry(const link_runtime::parry::Candidate& candidate){
    namespace dr=link_runtime::parry::defense_runtime;
    namespace g=link_runtime::parry::general;
    g::TierOne tier_one=g::TierOne::Inactive;
    if(InterlockedCompareExchange(&g_parry_defense_active,0,0)){
        InterlockedIncrement(&g_parry_attempts);
        dr::Mark(g_parry_defense,dr::Stage::Idle);   // a stale Approved can never become a handoff
        const auto result=dr::AttemptCurrent(g_parry_defense,candidate);
        if(result==link_runtime::parry::commit::Result::Committed){
            const LONG serial=InterlockedIncrement(&g_parry_committed);g_parry_audio.Publish(true,GetTickCount64());
            InterlockedExchangeAdd(&g_parry_spark_submitted,static_cast<LONG>(link_runtime::parry::spark::EmitCurrent(g_parry_sparks,candidate)));
            CombatCommitted(candidate,serial);   // after the approved effects; inert with every combat key 0
            g::OpenGraceAfterCommit(g_gen,static_cast<uint32_t>(serial));   // post-parry grace for a second attacker
            return true;
        }
        if(result==link_runtime::parry::commit::Result::PartialFailure){InterlockedIncrement(&g_parry_partial);InterlockedExchange(&g_parry_defense_active,0);tier_one=g::TierOne::Partial;}
        else tier_one=InterlockedCompareExchange(&g_parry_defense.last_stage,0,0)==static_cast<LONG>(dr::Stage::Approved)?
            g::TierOne::NotCommittedSpent:g::TierOne::NotCommitted;
    }
    bool handoff=false;
    if(g_gen.settings.mode!=g::Mode::Off&&g::TierTwoRuns(tier_one,handoff)){
        // hunt-findings P2-1: a leg-sphere sample of a Krauser kick the box route already blocked is that kick (silent).
        if(link_runtime::parry::direct::FollowTierTwo(g_gen,candidate))return true;
        const auto applied=g::Resolve(g_gen,candidate,handoff);
        link_runtime::parry::direct::ShareTierTwo(g_gen,candidate,applied);
        if(applied.cancel&&applied.effects&&!applied.grace){   // a grace block is the same parry: sparks only
            g_parry_audio.Publish(true,GetTickCount64());
            CombatGeneralBlocked(applied.serial,applied.owner_id);   // Link's reaction only; no riposte, no stagger
            ParryHeavyBlock(applied);   // power >= ParryHeavyPower: Link slides back (TP)
        }
        if(applied.cancel)return true;
    }
    // Tier 3 (shield-guard-01): the raised shield. Never after a tier-1 partial write.
    if(tier_one==g::TierOne::Partial)return false;
    return GuardResolve(candidate);
}
static void StopParryObservation() {
    link_runtime::parry::direct::Stop();
    link_runtime::parry::line::Stop();
    InterlockedExchange(&g_parry_defense_active,0);
    link_runtime::parry::observation::Stop();
    const auto pose=link_runtime::parry::sword::epoch::Stop();
    const auto input=link_runtime::parry::input::Stop();
    if(pose!=MH_OK||input!=MH_OK)
        Log("ParryObserve stop: pose=%s input=%s; admission disabled, trampolines retained",MH_StatusToString(pose),MH_StatusToString(input));
}
// Tier 2 startup: pins, front, sparks and the thrown deflect route. Fails closed to Off.
static void StartGeneralBlock(const link_runtime::shield::NativeAccess& native,const link_runtime::parry::front_native::Code& front){
    namespace g=link_runtime::parry::general;
    if(g_gen.settings.mode==g::Mode::Off){Log("ParryGeneral ready=0 mode=off");return;}
    const auto* image=reinterpret_cast<const unsigned char*>(native.image_base);
    g::Memory memory;
    g_gen.access=native;g_gen.front=front;
    g_gen.code=g::Qualify(memory,image,native.image_size,native);
    g_gen.sparks=link_runtime::parry::spark::FindCode(image,native);
    g_gen.deflect=g_gen.settings.thrown&&g::thrown::Qualify(memory,native.image_base);
    if(!g_gen.code.verified||!front.verified){
        Log("ParryGeneral ready=0: pre-effects site or native front not qualified; tier 2 off");g_gen.settings.mode=g::Mode::Off;return;
    }
    Log("ParryGeneral ready=1 mode=%s producers=%u/%u bosses=%u (%u/%u) thrown=%u (%u/%u) deflect=%u sparks=%u",
        g_gen.settings.mode==g::Mode::Active?"active":"observe",g_gen.code.live_a,g_gen.code.total_a,
        g_gen.settings.bosses?1U:0U,g_gen.code.live_b,g_gen.code.total_b,g_gen.settings.thrown?1U:0U,g_gen.code.live_t,g_gen.code.total_t,
        g_gen.deflect?1U:0U,g_gen.sparks.verified?1U:0U);
}
// Diagnostic only (12-oct, ParryObserve failed with re4_fps and no static re4_tweaks pattern
// touches these windows): when a parry qualification fails, dump the live bytes of exactly the
// windows that component pins, so the session log can be diffed offline against the pinned
// image. Read-only, SEH-guarded, once per session.
static void LogImageBytes(const char* tag,uint32_t base,uint32_t rva,unsigned length){
    static const char digits[]="0123456789ABCDEF";
    for(unsigned at=0;at<length;at+=256){
        const unsigned n=length-at<256?length-at:256;char hex[513]{};bool ok=true;
        __try{const auto* p=reinterpret_cast<const volatile unsigned char*>(base+rva+at);
            for(unsigned i=0;i<n;++i){const unsigned char b=p[i];hex[2*i]=digits[b>>4];hex[2*i+1]=digits[b&15];}}
        __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
        Log("parry qualification bytes %s rva=0x%X len=0x%X %s",tag,rva+at,n,ok?hex:"unreadable");
    }
}
static void LogParryQualificationBytes(uint32_t base,bool owner,bool input,bool pose,bool front){
    struct Window{const char* tag;uint32_t rva;unsigned length;};
    static const Window owner_windows[]={{"owner",0x55BE0,0x102},{"owner",0x7581,5},{"owner",0x7F3AFC,4},{"owner",0x29120,0x2F},{"owner",0x6E610,0x7A6}};
    static const Window input_windows[]={{"input",0x254B80,0x63},{"input",0x56A4F0,0x47},{"input",0x7509,5},{"input",0xA169,5}};
    static const Window pose_windows[]={{"pose",0x364D20,1246},{"pose",0x25EDB0,202},{"pose",0x283870,17},{"pose",0x5DA3,5},{"pose",0x49EE,5},
        {"pose",0x68BB,5},{"pose",0xCCED,5},{"pose",0xC635,5}};
    static const Window front_windows[]={{"front",0x2D06E0,0x43},{"front",0x25AD80,0x168},{"front",0x572540,0x74},{"front",0xDCFB,5},
        {"front",0x4C3C,5},{"front",0x66EB40,0x4F},{"front",0x66F1B0,0x4F}};
    if(!owner)for(const auto& w:owner_windows)LogImageBytes(w.tag,base,w.rva,w.length);
    if(!input)for(const auto& w:input_windows)LogImageBytes(w.tag,base,w.rva,w.length);
    if(!pose)for(const auto& w:pose_windows)LogImageBytes(w.tag,base,w.rva,w.length);
    if(!front)for(const auto& w:front_windows)LogImageBytes(w.tag,base,w.rva,w.length);
}
static bool StartParryObservation(const link_runtime::shield::NativeAccess& native,bool defense_requested) {
    namespace p=link_runtime::parry;
    const auto* image=reinterpret_cast<const unsigned char*>(native.image_base);
    p::owner::detail::foreign_probe=&g_foreign_probe;p::owner::detail::foreign_calls={};
    const auto owner=p::owner::FindCode(image,native.image_size,native);
    for(unsigned i=0;i<p::owner::detail::foreign_calls.count;++i)
        LogForeignPatch("ParryOwner","call_redirected",p::owner::detail::foreign_calls.site_rva[i],p::owner::detail::foreign_calls.target[i]);
    const auto input=p::input::FindInputCode(image,native.image_size,native);
    const auto epoch=p::sword::epoch::Find(image,native.image_size,native);
    const auto front=p::front_native::FindCode(image,native.image_size,native);
    if(!owner.verified||!input.verified||!epoch.verified||!front.verified||!p::observation::Start(native,owner,true,60,front)) {
        Log("ParryObserve disabled: owner/input/pose/front qualification failed owner=%u input=%u pose=%u front=%u",
            owner.verified?1U:0U,input.verified?1U:0U,epoch.verified?1U:0U,front.verified?1U:0U);
        LogParryQualificationBytes(native.image_base,owner.verified,input.verified,epoch.verified,front.verified);
        return false;
    }
    p::observer=p::observation::OnCandidate;
    p::sample_observer=p::observation::OnSample;
    if(defense_requested){
        const auto reaction=p::commit::FindCode(image,native.image_size,native);
        if(!reaction.verified){StopParryObservation();Log("ParryActive disabled: native recoil qualification failed");return false;}
        g_parry_defense.reaction.access=native;g_parry_defense.reaction.code=reaction;
        g_parry_defense.front=front;g_parry_defense.reaction.enabled=true;
        g_parry_sparks=p::spark::FindCode(reinterpret_cast<const unsigned char*>(native.image_base),native);
        Log("ParrySpark ready=%u cosmetic_only=1",g_parry_sparks.verified?1U:0U);
        p::resolver=ResolveParry;InterlockedExchange(&g_parry_defense_active,1);
    }
    StartGeneralBlock(native,front);
    if(g_gen.settings.mode!=p::general::Mode::Off)p::resolver=ResolveParry;   // observe mode without ParryActive
    p::sword::epoch::after_tail=p::observation::Tick;
    p::sword::epoch::before_entry=p::input::RecordPlayerUpdate;
    // Trial16 front contacts at343/359ms missed the300ms window, including draw. 24-oct logs: held
    // presses refused at 453..734ms; ParryWindowMs (default 750) bounds it; holding never renews the press.
    const uint32_t trial_window_ms=g_parry_window_ms;
    auto status=p::input::StartPlayerUpdate(native,{true,trial_window_ms});
    if(status==MH_OK)status=p::sword::epoch::Start(native,true);
    if(status!=MH_OK) {
        StopParryObservation();Log("ParryObserve disabled: hook startup %s",MH_StatusToString(status));return false;
    }
    p::direct::on_block=ParryDirectAudio;
    for(unsigned i=0;i<p::direct::kCount;++i){
        // A route the INI leaves off is never tried: status=off instead of the hook's NOT_EXECUTABLE (pending 12).
        if(!p::direct::requested[i]){Log("ParryDirect ready route=%s ok=0 enabled=0 status=off",p::direct::kSpecs[i].name);continue;}
        const auto route=static_cast<p::direct::Route>(i);
        const auto direct_status=p::direct::Start(g_gen,route);
        Log("ParryDirect ready route=%s ok=%u enabled=1 status=%s",p::direct::kSpecs[i].name,
            direct_status==MH_OK?1U:0U,MH_StatusToString(direct_status));
    }
    if(g_parry_line_requested){
        const auto line_status=p::line::Start(g_gen,ResolveParryLine);
        Log("ParryLine ready=%u site=0x3007FE deflect=%u status=%s (needs active tier 2, thrown and exact native pins)",
            line_status==MH_OK?1U:0U,p::line::deflect?1U:0U,MH_StatusToString(line_status));
    }else Log("ParryLine ready=0 reason=config");
    Log("Parry transport ready; input_window_ms=%u active_requested=%u routes=axe%s",trial_window_ms,defense_requested?1U:0U,owner.suki?",suki":"");return true;
}
static void PollParryObservation(DWORD& previous) {
    namespace o=link_runtime::parry::observation;
    o::Record r;
    link_runtime::parry::general::Drain([](const char* line){Log("%s",line);});
    if(const unsigned lost=link_runtime::parry::general::dropped.exchange(0))Log("parry general records dropped=%u",lost);
    static link_runtime::parry::observe_log::Gate quiet;   // worker thread only (pending 13)
    for(unsigned i=0;i<32&&o::Pop(r);++i){
        link_runtime::parry::observe_log::Facts facts;
        facts.kind=static_cast<uint32_t>(r.kind);facts.on=r.on;facts.trg=r.trg;facts.armed=(r.flags&(o::WindowArmed|o::InputSampleArmed))!=0;
        facts.contact=r.contact_valid&&(r.contact.endpoint_intersects||r.contact.spatial_segment_intersects||r.contact.body_hit_reported);
        facts.reasons=r.reasons;facts.flags=r.flags;facts.ready=r.epoch_ready;facts.poisoned=r.epoch_poisoned;facts.failure=r.epoch_failure;
        facts.reader_failure=r.epoch_reader_failure;facts.room=r.room;facts.weapon=r.weapon;facts.character=r.character;
        if(!quiet.Admit(facts,GetTickCount64()))continue;
        LogV("parry observe kind=%u reasons=0x%X flags=0x%X thread=%u frame=%u input_frame=%u owner_frame=%u completed_frame=%u "
            "pose_ready=%u pose_poisoned=%u depth=%u serial=%llu profile=%u room=0x%X weapon=%u character=%u pose_failure=%u pose_reader_failure=%u "
            "input_on=0x%llX input_trg=0x%llX window_age_ms=%llu remaining_ms=%u "
            "producer_return_rva=0x%X owner_weapon=%u owner_r1=0x%X blade_base=%.3f,%.3f,%.3f blade_tip=%.3f,%.3f,%.3f observer_only=1",
            static_cast<unsigned>(r.kind),r.reasons,r.flags,r.thread,r.frame,r.input_frame,r.owner_frame,r.completed_frame,
            r.epoch_ready,r.epoch_poisoned,r.epoch_depth,r.epoch_serial,r.profile,r.room,r.weapon,r.character,r.epoch_failure,r.epoch_reader_failure,
            r.on,r.trg,r.window_age_ms,r.window_remaining_ms,r.producer_return_rva,r.owner_weapon_type,static_cast<unsigned>(r.owner_routines[1]),
            r.blade_base[0],r.blade_base[1],r.blade_base[2],r.blade_tip[0],r.blade_tip[1],r.blade_tip[2]);
        if(r.kind==o::Kind::Candidate||r.kind==o::Kind::AttackSample){
            const auto& c=r.contact;
            LogV("parry contact frame=%u serial=%llu valid=%u caller=0x%X radius=%.3f sample_local_z=%.3f "
                "endpoint_intersects=%u spatial_segment_intersects=%u endpoint_distance_squared=%.3f "
                "spatial_segment_distance_squared=%.3f direction_dot_estimate=%.3f short_direction=%u "
                "front_known=%u frontal=%u native_dot=%.3f native_short=%u body_hit_reported=%u body_order=unknown observer_only=1",
                r.frame,r.epoch_serial,r.contact_valid?1U:0U,c.current_caller,c.radius,c.sample_local_z,
                c.endpoint_intersects?1U:0U,c.spatial_segment_intersects?1U:0U,c.endpoint_distance_squared,
                c.spatial_segment_distance_squared,c.native_direction_dot_estimate,c.native_short_direction?1U:0U,
                c.front_known?1U:0U,c.front.frontal?1U:0U,c.front.dot,c.front.short_direction?1U:0U,c.body_hit_reported?1U:0U);
        }
    }
    const DWORD now=GetTickCount();
    if(now-previous>=5000) {
        previous=now;const auto c=o::SnapshotCounters();
        LogV("parry observation totals tails=%u ready=%u candidates=%u published=%u dropped_full=%u dropped_busy=%u reentered=%u quiet_skipped=%u observer_only=1",
            c.tails,c.epoch_ready_tails,c.candidates,c.published,c.dropped_full,c.dropped_busy,c.reentered,quiet.TakeSkipped());
        for(uint32_t kind=0;kind<link_runtime::parry::observe_log::Gate::kKinds;++kind)   // identical records not written (pending 13)
            if(const uint32_t n=quiet.TakeRepeated(kind))LogV("parry observe repeated kind=%u n=%u window_ms=5000 observer_only=1",kind,n);
        LogV("parry defense active=%ld attempts=%ld committed=%ld partial_failure=%ld",
            InterlockedCompareExchange(&g_parry_defense_active,0,0),InterlockedCompareExchange(&g_parry_attempts,0,0),
            InterlockedCompareExchange(&g_parry_committed,0,0),InterlockedCompareExchange(&g_parry_partial,0,0));
        LogV("ParrySpark submitted=%ld",InterlockedCompareExchange(&g_parry_spark_submitted,0,0));
        LogV("parry last_stage=%ld (0idle 1input 2owner 3recoil_prepare 4approval_owner 5contact 6consume 7approved 8committed 9partial)",
            InterlockedCompareExchange(&g_parry_defense.last_stage,0,0));
        if(g_gen.settings.mode!=link_runtime::parry::general::Mode::Off){
            const auto& k=g_gen.counters;
            LogV("parry general totals candidates=%ld blocked=%ld follow=%ld handoff=%ld pass=%ld would_block=%ld deflect=%ld deflect_failed=%ld faults=%ld grace_open=%ld grace_blocked=%ld follow_long=%ld",
                k.candidates,k.blocked,k.follows,k.handoffs,k.passes,k.would_block,k.deflects,k.deflect_failed,k.faults,k.grace_open,k.grace_blocked,k.follow_long);
        }
    }
}
static bool StartShieldHook(unsigned char* base,unsigned size,const link_runtime::shield::ObserverConfig& settings,
                            uint32_t player_slot,uint32_t global_slot,bool preclamp_requested,bool parry_requested,bool defense_requested) {
    using namespace link_runtime::shield;
    try {
        NativeAccess native;uint32_t preferred_base=0;
        if(!MappedRegions(base,size,preferred_base,native.regions)) {
            Log("ShieldObserve disabled: invalid mapped PE bounds");return false;
        }
        native.image_base=reinterpret_cast<uint32_t>(base);native.image_size=size;
        native.code=FindNative(base,size,native.image_base,native.regions.data(),native.regions.size(),&g_foreign_probe);
        native.verified=native.code.verified;
        if(native.verified&&native.code.key_site_foreign)
            LogForeignPatch("ShieldObserve","call_redirected",static_cast<uint32_t>(native.code.key_site_rva),native.code.key_site_target);
        if(!Complete(native)||!player_slot||!global_slot||
            native.image_base+native.code.player_slot_rva!=player_slot||
            native.image_base+native.code.global_slot_rva!=global_slot) {
            Log("ShieldObserve disabled: unique cdecl/input/knife/player/global qualification failed");return false;
        }
        PreClampCode preclamp;
        if(preclamp_requested||g_rear_observe||g_rear_active) {
            preclamp=FindPreClamp(base,size,native);
            if(!preclamp.verified) {
                if(preclamp_requested){Log("ShieldPreClampObserve disabled: exact native site qualification failed");return false;}
                Log("RearShieldObserve final_damage_unavailable: native preclamp pins; continuing existing observer/parry transport");
            }
        }
        const auto producer=FindOwnerCode(base,size,native);
        const auto preeffects=parry_requested?link_runtime::parry::FindCode(base,size,native):link_runtime::parry::Code{};
        if(parry_requested&&(!preeffects.verified||!StartParryObservation(native,defense_requested)))return false;
        const MH_STATUS status=runtime::Start(native,settings,preclamp.verified?&preclamp:nullptr,{},producer.verified?&producer:nullptr,
            parry_requested?&preeffects:nullptr);
        if(status!=MH_OK) { if(parry_requested)StopParryObservation();Log("ShieldObserve disabled: MinHook %s",MH_StatusToString(status));return false; }
        Log("ShieldPreClampObserve=%u site_rva=0x%X; observation only",preclamp.verified?1U:0U,static_cast<unsigned>(preclamp.site_rva));
        if(g_rear_observe||g_rear_active)Log("RearShieldObserve ready=1 final_damage_reader=%u active_requested=%u scope=angle_height qualified_per_impact=1",preclamp.verified?1U:0U,g_rear_active?1U:0U);
        Log("producer context enabled=%u return_rva=0x%X ordinary=0 active_block=0",producer.verified?1U:0U,producer.return_rva);
        Log("ShieldObserve enabled wrapper_rva=0x%X classifier_rva=0x%X expected_return_rva=0x%X events=%u poses=%u pose_ms=%u "
            "ordinary_routes=0 active_block=0; candidates only, native originals unchanged",
            static_cast<unsigned>(native.code.wrapper_rva),static_cast<unsigned>(native.code.classifier_rva),
            static_cast<unsigned>(native.code.classifier_return_rva),settings.events,settings.poses,settings.pose_ms);
        return true;
    } catch(...) { if(parry_requested)StopParryObservation();Log("ShieldObserve disabled: observer setup exception");return false; }
}
struct ShieldPollState {
    DWORD pose_tick=0,stats_tick=0;unsigned poses=0;bool have_pose=false,pose_polled=false,limit_logged=false;
    link_runtime::shield::Frame previous;
};
// Worker-only manifest: hash the files at their declared module/INI paths, never on the attack thread.
static bool ShieldFileHash(const wchar_t* path,char (&hex)[65]) {
    hex[0]=0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    unsigned char digest[32]={},buffer[4096]={};bool ok=false;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&
       BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0) {
        DWORD read=0;ok=true;
        for(;;) {
            if(!ReadFile(file,buffer,sizeof(buffer),&read,nullptr)) {ok=false;break;}
            if(!read)break;
            if(BCryptHashData(hash,buffer,read,0)<0) {ok=false;break;}
        }
        if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    }
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);
    if(ok)for(unsigned i=0;i<32;++i)sprintf_s(hex+2*i,65-2*i,"%02x",digest[i]);
    return ok;
}
static void ShieldManifest(const wchar_t* ini) {
    FILETIME creation{},exit{},kernel{},user{};
    const bool timed=GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)!=0;
    const uint64_t created=(uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
    wchar_t exe[MAX_PATH]={},dll[MAX_PATH]={};char exe_hash[65]={},dll_hash[65]={},ini_hash[65]={};
    const DWORD exe_length=GetModuleFileNameW(nullptr,exe,MAX_PATH),dll_length=GetModuleFileNameW(g_module,dll,MAX_PATH);
    const bool exe_ok=exe_length&&exe_length<MAX_PATH&&ShieldFileHash(exe,exe_hash);
    const bool dll_ok=dll_length&&dll_length<MAX_PATH&&ShieldFileHash(dll,dll_hash);
    const bool ini_ok=ShieldFileHash(ini,ini_hash);
    Log("shield session pid=%lu creation=%llu creation_valid=%u exe_sha256=%s dll_sha256=%s ini_sha256=%s dropped_total=%ld",
        GetCurrentProcessId(),created,timed?1U:0U,exe_ok?exe_hash:"unavailable",dll_ok?dll_hash:"unavailable",
        ini_ok?ini_hash:"unavailable",InterlockedCompareExchange(&link_runtime::shield::runtime::dropped_total,0,0));
    // Basenames only: the hashes identify the files, the player's folders stay out of the log.
    using link_runtime::log::BaseName;
    Log("shield manifest artifact=EXE path=%ls hash_valid=%u file_bytes_sha256=1",BaseName(exe),exe_ok?1U:0U);
    Log("shield manifest artifact=DLL path=%ls hash_valid=%u file_bytes_sha256=1",BaseName(dll),dll_ok?1U:0U);
    Log("shield manifest artifact=INI path=%ls hash_valid=%u file_bytes_sha256=1",BaseName(ini),ini_ok?1U:0U);
}
// ---- re4_fps: session manifest and FpsProbe (worker only, read-only) ----------------------------
// re4_fps 1.5.1 is re4_tweaks loaded as Bin32/winmm.dll (docs/fps-coexistence-2026-10-12.md).
// The session manifest hashes the winmm.dll actually loaded and its winmm.ini, so a log names the
// exact FPS package; a System32 winmm means re4_fps is absent.
static int GameVariableFrameRate(){
    int value=0;
    __try{value=*reinterpret_cast<volatile int*>(reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr))+
        link_runtime::game_clock::kVariableFrameRateRva);}
    __except(EXCEPTION_EXECUTE_HANDLER){value=-1;}
    return value;
}
static void FpsManifest(){
    wchar_t exe[MAX_PATH]{},module[MAX_PATH]{},ini[MAX_PATH]{};char module_hash[65]{},ini_hash[65]{};
    const HMODULE winmm=GetModuleHandleW(L"winmm.dll");
    const DWORD exe_length=GetModuleFileNameW(nullptr,exe,MAX_PATH);
    const DWORD module_length=winmm?GetModuleFileNameW(winmm,module,MAX_PATH):0;
    if(!winmm||!module_length||module_length>=MAX_PATH||!exe_length||exe_length>=MAX_PATH){
        Log("session manifest artifact=WINMM loaded=%u re4_fps=0",winmm?1U:0U);return;
    }
    const wchar_t* exe_slash=std::wcsrchr(exe,L'\\');const wchar_t* module_slash=std::wcsrchr(module,L'\\');
    const bool game_dir=exe_slash&&module_slash&&exe_slash-exe==module_slash-module&&
        !_wcsnicmp(exe,module,static_cast<size_t>(exe_slash-exe));
    const bool module_ok=ShieldFileHash(module,module_hash);
    Log("session manifest artifact=WINMM path=%ls hash_valid=%u sha256=%s game_dir=%u",link_runtime::log::BaseName(module),module_ok?1U:0U,
        module_ok?module_hash:"unavailable",game_dir?1U:0U);
    if(!game_dir)return;
    if(link_runtime::paths::Copy(ini,module))return;wchar_t* dot=std::wcsrchr(ini,L'.');
    const bool ini_ok=dot&&!link_runtime::paths::Copy(dot,MAX_PATH-static_cast<size_t>(dot-ini),L".ini")&&ShieldFileHash(ini,ini_hash);
    Log("session manifest artifact=WINMM_INI path=%ls hash_valid=%u sha256=%s",link_runtime::log::BaseName(ini),ini_ok?1U:0U,ini_ok?ini_hash:"unavailable");
    Log("re4_fps proxy present variableframerate_static=%d (EXE default before config.ini is read; FpsProbe lines show the live value)",GameVariableFrameRate());
}
// re4_fps Del Lago / harpoon null calls (re4fps_guard.h): fill its empty VecAdd and Vec-scale slots once,
// only when every checked byte of every slot matches. Reads/writes are SEH-guarded; POD only (no unwinding).
static bool Re4fpsGuardRead(link_runtime::re4fps_guard::Facts& f,const unsigned char* w,const unsigned char* b){
    namespace g=link_runtime::re4fps_guard;
    __try{
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(w);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(w+dos->e_lfanew);
        f.winmm_size=nt->OptionalHeader.SizeOfImage;
        if(f.winmm_size!=g::kWinmmImageSize)return true;
        for(size_t i=0;i<g::kSlots;++i){
            const g::Spec& sp=g::kSpecs[i];g::SlotFacts& s=f.s[i];
            for(size_t k=0;k<g::kMaxSites&&sp.sites[k].len;++k)std::memcpy(s.site[k],w+sp.sites[k].rva,sp.sites[k].len);
            s.slot=*reinterpret_cast<const volatile uint32_t*>(w+sp.slot);
            std::memcpy(s.thunk,b+sp.thunk,sizeof(s.thunk));std::memcpy(s.head,b+sp.func,sizeof(s.head));
        }
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void Re4fpsGuard(){
    namespace g=link_runtime::re4fps_guard;
    const HMODULE winmm=GetModuleHandleW(L"winmm.dll");
    const auto* b=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if(!winmm){Log("re4fps_guard winmm=absent");return;}
    const auto* w=reinterpret_cast<const unsigned char*>(winmm);
    g::Facts f;f.winmm_base=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w));f.bio4_base=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b));
    if(!Re4fpsGuardRead(f,w,b)){Log("re4fps_guard read_fault");return;}
    const bool all=g::AllKnown(f);
    for(size_t i=0;i<g::kSlots;++i){
        const g::Spec& sp=g::kSpecs[i];const g::Verdict v=g::DecideSlot(f,i);
        if(v!=g::Verdict::Repair||!all){
            Log("re4fps_guard slot=%s verdict=%s%s value=0x%08X slot=winmm+0x%X winmm=0x%08X size=0x%X",sp.name,g::Name(v),
                v==g::Verdict::Repair?" skipped=other_slot_unknown":"",f.s[i].slot,sp.slot,f.winmm_base,f.winmm_size);
            continue;
        }
        auto* slot=reinterpret_cast<volatile LONG*>(const_cast<unsigned char*>(w)+sp.slot);
        DWORD old=0;const bool prot=VirtualProtect(const_cast<LONG*>(slot),4,PAGE_READWRITE,&old)!=0;
        const LONG prev=InterlockedCompareExchange(slot,static_cast<LONG>(g::Target(f,i)),0);
        if(prot){DWORD unused=0;VirtualProtect(const_cast<LONG*>(slot),4,old,&unused);}
        Log("re4fps_guard slot=%s verdict=repair wrote=%u slot=winmm+0x%X value=bio4+0x%X prev=0x%08X",sp.name,prev==0?1U:0U,
            sp.slot,sp.thunk,static_cast<uint32_t>(prev));
    }
}
// CrashDump (crash_dump.h): one minidump + register line for a null call or an access violation in winmm/bio4
// code. Everything the handler needs is prepared at install (dbghelp loaded from System32, folder, ranges);
// the handler itself only formats, opens one file and calls MiniDumpWriteDump once.
using MiniDumpWriteDumpFn=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
    PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
static MiniDumpWriteDumpFn g_crash_write=nullptr;
static wchar_t g_crash_dir[MAX_PATH]{};
static link_runtime::crash_dump::Range g_crash_winmm{},g_crash_bio4{};
static uint32_t g_crash_winmm_base=0,g_crash_winmm_size=0,g_crash_bio4_base=0,g_crash_bio4_size=0;
static volatile LONG g_crash_taken=0;
// XAudio2_7.dll code: filled once by the worker (CrashDumpPollAudio) when the game has loaded it; size last.
static link_runtime::crash_dump::Range g_crash_xaudio{};static uint32_t g_crash_xaudio_base=0;
static volatile LONG g_crash_xaudio_size=0;
static link_runtime::crash_dump::Range CrashCodeRange(HMODULE m,uint32_t& image_size){
    link_runtime::crash_dump::Range r{};image_size=0;if(!m)return r;
    const auto* p=reinterpret_cast<const unsigned char*>(m);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(p+reinterpret_cast<const IMAGE_DOS_HEADER*>(p)->e_lfanew);
    r.base=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p))+nt->OptionalHeader.BaseOfCode;r.size=nt->OptionalHeader.SizeOfCode;
    image_size=nt->OptionalHeader.SizeOfImage;
    return r;
}
static bool CrashReadStack(uint32_t esp,uint32_t& top){
    __try{top=*reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(esp));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){top=0;return false;}
}
static void CrashWhere(uint32_t a,char* out,size_t n){
    if(g_crash_winmm_base&&a>=g_crash_winmm_base&&a-g_crash_winmm_base<g_crash_winmm_size)
        _snprintf_s(out,n,_TRUNCATE,"winmm+0x%X",a-g_crash_winmm_base);
    else if(g_crash_bio4_base&&a>=g_crash_bio4_base&&a-g_crash_bio4_base<g_crash_bio4_size)
        _snprintf_s(out,n,_TRUNCATE,"bio4+0x%X",a-g_crash_bio4_base);
    else if(g_crash_xaudio_size&&a>=g_crash_xaudio_base&&a-g_crash_xaudio_base<static_cast<uint32_t>(g_crash_xaudio_size))
        _snprintf_s(out,n,_TRUNCATE,"xaudio+0x%X",a-g_crash_xaudio_base);
    else _snprintf_s(out,n,_TRUNCATE,"0x%08X",a);
}
static LONG CALLBACK CrashDumpHandler(EXCEPTION_POINTERS* info){
    namespace c=link_runtime::crash_dump;
    if(!info||!info->ExceptionRecord||!info->ContextRecord)return EXCEPTION_CONTINUE_SEARCH;
    const uint32_t code=info->ExceptionRecord->ExceptionCode;
    const uint32_t at=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress));
    const c::Range xaudio=g_crash_xaudio_size?g_crash_xaudio:c::Range{};
    const c::Kind kind=c::Classify(code,at,g_crash_winmm,g_crash_bio4,xaudio);
    // One dump per process; the flag also stops recursion if anything below faults.
    if(kind==c::Kind::None||InterlockedCompareExchange(&g_crash_taken,1,0)!=0)return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT& x=*info->ContextRecord;uint32_t top=0;const bool top_ok=CrashReadStack(x.Esp,top);
    char where[32]{},ret[32]{};CrashWhere(at,where,sizeof(where));CrashWhere(top,ret,sizeof(ret));
    const bool av=info->ExceptionRecord->NumberParameters>=2;
    const unsigned long rw=av?static_cast<unsigned long>(info->ExceptionRecord->ExceptionInformation[0]):9UL;
    const unsigned long va=av?static_cast<unsigned long>(info->ExceptionRecord->ExceptionInformation[1]):0UL;
    Log("crash_dump kind=%s code=0x%08X eip=%s access=%lu va=0x%08lX esp0=%s esp0_read=%u eax=%08lX ebx=%08lX ecx=%08lX "
        "edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX thread=%lu",c::Name(kind),code,where,rw,va,ret,top_ok?1U:0U,
        x.Eax,x.Ebx,x.Ecx,x.Edx,x.Esi,x.Edi,x.Ebp,x.Esp,GetCurrentThreadId());
    if(!g_crash_write){Log("crash_dump written=0 reason=no_dbghelp");return EXCEPTION_CONTINUE_SEARCH;}
    wchar_t file[MAX_PATH]{};
    if(_snwprintf_s(file,MAX_PATH,_TRUNCATE,L"%slink_crash_%lu.dmp",g_crash_dir,GetTickCount())<0){
        Log("crash_dump written=0 reason=path");return EXCEPTION_CONTINUE_SEARCH;
    }
    const HANDLE h=CreateFileW(file,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){Log("crash_dump written=0 reason=create error=%lu",GetLastError());return EXCEPTION_CONTINUE_SEARCH;}
    MINIDUMP_EXCEPTION_INFORMATION mei{};mei.ThreadId=GetCurrentThreadId();mei.ExceptionPointers=info;mei.ClientPointers=FALSE;
    const BOOL ok=g_crash_write(GetCurrentProcess(),GetCurrentProcessId(),h,
        static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory|MiniDumpWithDataSegs),&mei,nullptr,nullptr);
    const DWORD err=ok?0:GetLastError();CloseHandle(h);
    const wchar_t* name=std::wcsrchr(file,L'\\');
    Log("crash_dump written=%u file=%ls error=%lu",ok?1U:0U,name?name+1:file,err);
    return EXCEPTION_CONTINUE_SEARCH;
}
static void CrashDumpInstall(const wchar_t* ini){
    if(GetPrivateProfileIntW(L"LinkRuntime",L"CrashDump",1,ini)!=1){Log("crash_dump off");return;}
    const wchar_t* slash=std::wcsrchr(ini,L'\\');
    if(!slash||static_cast<size_t>(slash-ini)+1>=MAX_PATH){Log("crash_dump off reason=path");return;}
    const size_t dir=static_cast<size_t>(slash-ini)+1;
    wmemcpy(g_crash_dir,ini,dir);g_crash_dir[dir]=L'\0';
    const HMODULE dbghelp=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    g_crash_write=dbghelp?reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp,"MiniDumpWriteDump")):nullptr;
    const HMODULE winmm=GetModuleHandleW(L"winmm.dll"),bio4=GetModuleHandleW(nullptr);
    g_crash_winmm=CrashCodeRange(winmm,g_crash_winmm_size);g_crash_bio4=CrashCodeRange(bio4,g_crash_bio4_size);
    g_crash_winmm_base=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(winmm));
    g_crash_bio4_base=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bio4));
    const bool added=AddVectoredExceptionHandler(0,&CrashDumpHandler)!=nullptr;
    Log("crash_dump on handler=%u dbghelp=%u winmm_code=0x%08X+0x%X bio4_code=0x%08X+0x%X",added?1U:0U,g_crash_write?1U:0U,
        g_crash_winmm.base,g_crash_winmm.size,g_crash_bio4.base,g_crash_bio4.size);
}
// XAudio2_7.dll arrives after CrashDumpInstall: look for it every 2 s until found (worker thread, never in the handler).
static void CrashDumpPollAudio(){
    static uint64_t next=0;if(!g_crash_write||g_crash_xaudio_size)return;
    const uint64_t now=GetTickCount64();if(now<next)return;next=now+2000;
    const HMODULE xa=GetModuleHandleW(L"XAudio2_7.dll");if(!xa)return;
    uint32_t image=0;const link_runtime::crash_dump::Range r=CrashCodeRange(xa,image);if(!r.size)return;
    g_crash_xaudio=r;g_crash_xaudio_base=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(xa));
    InterlockedExchange(&g_crash_xaudio_size,static_cast<LONG>(image));
    Log("crash_dump xaudio_code=0x%08X+0x%X",r.base,r.size);
}
// FpsProbe: logic ticks per real second (+0x530C) and simulation speed (sum of deltaTime/30 per
// real second; 1.00 = real time, below 1 the PC cannot hold the fixed step and the game slows).
struct FpsProbe {
    link_runtime::game_clock::Rate rate;uint64_t next_ms=0;bool on=false;uint32_t global_slot=0;
    static uint64_t NowMs(){LARGE_INTEGER f{},c{};QueryPerformanceFrequency(&f);QueryPerformanceCounter(&c);
        return f.QuadPart?static_cast<uint64_t>(c.QuadPart/(f.QuadPart/1000)):GetTickCount64();}
    void Start(uint32_t slot,bool enabled){
        global_slot=slot;on=enabled&&slot;
        Log("FpsProbe enabled=%u interval_ms=5000 global=%p variableframerate=%d",on?1U:0U,reinterpret_cast<void*>(slot),GameVariableFrameRate());
        if(on){const uint64_t now=NowMs();rate.Sample(now);next_ms=now+5000;}
    }
    void Poll(){
        if(!on)return;
        uint32_t frame=0,bits=0;float dt=0.f;
        if(GameGlobalRead(global_slot,link_runtime::game_clock::kFrameCounterOffset,frame)&&
           GameGlobalRead(global_slot,link_runtime::game_clock::kDeltaTimeOffset,bits)){std::memcpy(&dt,&bits,4);rate.Observe(frame,dt,NowMs());}
        const uint64_t now=NowMs();
        if(now<next_ms)return;
        next_ms=now+5000;
        const auto s=rate.Sample(now);
        if(s.valid)LogV("FpsProbe ticks_per_s=%.1f speed=%.3f dt=%.5f ticks=%u secs=%.2f invalid_dt=%u resets=%u variableframerate=%d",
            s.ticks_per_s,s.speed,s.dt,s.ticks,s.seconds,s.invalid_dt,s.resets,GameVariableFrameRate());
    }
};
static void PollShield(ShieldPollState& state) {
    using namespace link_runtime::shield;
    HitRecord record;
    static link_runtime::rear_shield::Reporter rear_reporter;
    // Consume only owned copies. No event/player/ATK pointer from the queue is dereferenced here.
    for(unsigned drained=0;drained<64&&runtime::Pop(record);++drained) {
        if(g_rear_observe||g_rear_active){
            const auto rear=link_runtime::rear_shield::Inspect(record,runtime::access.image_base);
            const auto& pose=record.rear_pose;const auto& impact=record.rear_impact;
            link_runtime::rear_shield::measure::LogPacket(&LogV,rear.invocation,record.rear_measure);
            if(rear_reporter.Take(rear))LogV("rear_shield observe invocation=%u thread=%u producer=0x%X model=%s direction_candidate=%u rear_candidate=%u source_dot=%.6f final_known=%u final_D=%d ceil_half_preview=%d active=%u contact_qualified=%u ordinary_qualified=%u sound=%u native_hit=%d travel=%.6f final_reason=%s direction_reason=%s",
                rear.invocation,rear.thread,rear.producer,link_runtime::rear_shield::Name(rear.model),rear.direction?1U:0U,rear.rear?1U:0U,
                rear.source_dot,rear.final_known?1U:0U,rear.damage,rear.half_preview,impact.applied?1U:0U,impact.applied?1U:0U,
                impact.applied?1U:0U,impact.sound?1U:0U,record.event.native_hit,rear.travel,rear.final_reason,rear.direction_reason);
            Log("rear_shield decision invocation=%u requested=%u applied=%u damage_in=%d damage_out=%d sound_requested=%u reason=%s pose_stable=%u transform_candidate=%u frame=%u part2=0x%X info0=0x%X resource=0x%X parts=%u",
                rear.invocation,g_rear_active?1U:0U,impact.applied?1U:0U,impact.original,impact.result,impact.sound?1U:0U,
                link_runtime::rear_shield::Name(impact.reason),pose.stable?1U:0U,pose.transform_candidate?1U:0U,pose.frame,pose.part2,pose.info0,pose.resource,pose.parts);
            if(record.rear_band.evaluated){const auto& b=record.rear_band;
                Log("rear_shield band invocation=%u stage=%u qualified=%u reason=%s family=%s caller=0x%X weapon_type=%u attack_no=%u routine=0x%X native_hit=%d bearing=%.2f front_dot=%.4f height=%.1f band=%.1f..%.1f local_z=%.1f",
                    rear.invocation,b.stage,b.qualified?1U:0U,link_runtime::rear_shield::Name(b.reason),link_runtime::rear_shield::band::Name(b.family),
                    b.caller,b.weapon_type,b.attack_no,b.routine,record.event.native_hit,b.bearing,b.front_dot,b.height,b.band_low,b.band_high,b.local_z);}
            if(pose.stable){
                const std::array<unsigned char,48>* matrices[]={&pose.body0,&pose.bone2,&pose.inverse_bind2,&pose.info_matrix,&record.frame.matrix};
                const char* names[]={"body0","bone2","inverse_bind2","info0","root"};
                for(unsigned i=0;i<5;++i){const auto m=link_runtime::rear_shield::Matrix(*matrices[i]);
                    LogV("rear_shield matrix invocation=%u kind=%s values=%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g",
                        rear.invocation,names[i],m.v[0][0],m.v[0][1],m.v[0][2],m.v[0][3],m.v[1][0],m.v[1][1],m.v[1][2],m.v[1][3],m.v[2][0],m.v[2][1],m.v[2][2],m.v[2][3]);}
            }
        }
        const auto& c=record.completion;
        const auto& p=record.preclamp;
        LogV("shield completion invocation=%u thread=%u wrapper_return=%d captured=%u native_hit=%d comparable=%u before_valid=%u after_valid=%u "
            "player_before=0x%X player_after=0x%X global_before=0x%X global_after=0x%X character_before=%u character_after=%u "
            "routine_before=%u routine_after=%u hp_before=%d hp_after=%d max_before=%d max_after=%d hp_delta_observed=%d ordinary=0 would_block=0 "
            "preclamp_captures=%u preclamp_valid=%u preclamp_rejected=%u preclamp_damage=%d preclamp_hp=%d "
            "preclamp_raw_flags=0x%X preclamp_global=0x%X preclamp_return_rva=0x%X",
            c.invocation,c.thread,c.wrapper_return,record.captured?1U:0U,record.captured?record.event.native_hit:0,
            c.comparable?1U:0U,c.before.stable?1U:0U,c.after.stable?1U:0U,
            c.before.player,c.after.player,c.before.global,c.after.global,c.before.character,c.after.character,c.before.routine,c.after.routine,
            c.before.hp,c.after.hp,c.before.max_hp,c.after.max_hp,c.hp_delta_observed,
            p.captures,p.valid?1U:0U,p.rejected?1U:0U,p.damage,p.hp,p.raw_flags,p.global,p.return_rva);
        for(unsigned index=0;index<p.audit_count&&index<2;++index) {
            const auto& a=p.audit[index];
            const bool in_image=a.frame_read&&a.return_address>=runtime::access.image_base&&
                a.return_address-runtime::access.image_base<runtime::access.image_size;
            LogV("preclamp audit invocation=%u attempt=%u thread=%u frame_read=%u return_va=0x%X return_rva=0x%X "
                "global=0x%X damage=%d raw_flags=0x%X overflow=%u native_return=0x%X bridged=%u",
                c.invocation,index+1,a.thread,a.frame_read?1U:0U,a.return_address,
                in_image?a.return_address-runtime::access.image_base:0xFFFFFFFFU,
                a.global,a.damage,a.raw_flags,p.audit_overflow?1U:0U,a.native_return,a.bridged?1U:0U);
        }
        if(p.secondary_zero_seen)LogV("preclamp separation invocation=%u known_zero_followup=1 primary_sample_only=1",c.invocation);
        const auto& owner=record.owner;
        LogV("producer context invocation=%u thread=%u caller_rva=0x%X valid=%u owner=0x%X attack_no=%u parts=%u "
            "power=%u raw_flags=0x%X owner_flags=0x%X status=0x%X difficulty=%u attack_connected=%u ordinary=0 active_block=0",
            c.invocation,c.thread,record.event.outer_return-runtime::access.image_base,owner.valid?1U:0U,
            owner.owner,owner.attack_no,owner.parts,owner.power,owner.raw_flags,owner.owner_flags,owner.status,
            static_cast<unsigned>(owner.difficulty),static_cast<unsigned>(owner.attack_connected));
        if(!record.captured)continue;
        const auto view=DescribeFrame(record.frame,record.event,false);
        const auto& s=view.decision;
        const Vec position=Vector(record.frame.position_angles.data()),angles=Vector(record.frame.position_angles.data()+12);
        const Vec attack=Vector(record.frame.current.data()),old=Vector(record.frame.previous.data());
        const unsigned rva=record.event.outer_return>=runtime::access.image_base&&
            record.event.outer_return-runtime::access.image_base<runtime::access.image_size
            ? record.event.outer_return-runtime::access.image_base:0xFFFFFFFFU;
        LogV("shield sphere tick=%u caller_rva=0x%X caller_shape=%u companion_filter=%d native_hit=%d "
            "player=%p routines=0x%X costume=%u on=0x%llX trg=0x%llX hp=%d/%d gameplay=%u leon=%u "
            "ready_candidate=%u power=%u flags=0x%X radius=%.3f pos=(%.3f,%.3f,%.3f) yaw=%.6f "
            "current=(%.3f,%.3f,%.3f) old=(%.3f,%.3f,%.3f) geometry=%u dot=%.6f ordinary=0 would_block=0",
            record.tick,rva,record.caller_shape?1U:0U,record.companion_filter,record.event.native_hit,
            reinterpret_cast<void*>(view.player),view.routines,(record.frame.character>>8)&0xFF,
            view.keys_on,view.keys_trg,view.hp,view.max_hp,s.gameplay?1U:0U,s.leon?1U:0U,s.ready?1U:0U,
            s.power,s.attack_flags,Float(record.frame.attack.data()),position.x,position.y,position.z,angles.y,
            attack.x,attack.y,attack.z,old.x,old.y,old.z,s.geometry_valid?1U:0U,s.front_dot);
    }
    const DWORD now=GetTickCount();
    if(state.poses<runtime::config.poses&&(!state.pose_polled||now-state.pose_tick>=runtime::config.pose_ms)) {
        state.pose_tick=now;state.pose_polled=true;runtime::Memory memory;Frame pose;
        if(ReadPose(memory,runtime::access,pose)&&(!state.have_pose||!Equal(pose,state.previous))) {
            state.previous=pose;state.have_pose=true;++state.poses;
            const Vec position=Vector(pose.position_angles.data()),angles=Vector(pose.position_angles.data()+12);
            const uint64_t on=uint64_t(U32(pose.input.data()))|(uint64_t(U32(pose.input.data()+4))<<32);
            const uint64_t trg=uint64_t(U32(pose.input.data()+8))|(uint64_t(U32(pose.input.data()+12))<<32);
            LogV("shield pose sample=%u player=%p routines=0x%X char=%u costume=%u kind=%u damage=0x%X "
                "on=0x%llX trg=0x%llX hp=%d/%d gameplay=%u system=0x%X stop=0x%X "
                "pos=(%.3f,%.3f,%.3f) angles=(%.6f,%.6f,%.6f) visual_pose_approved=0",
                state.poses,reinterpret_cast<void*>(pose.player),U32(pose.routines.data()),pose.character&0xFF,
                (pose.character>>8)&0xFF,pose.kind[0],U16(pose.damage.data()),on,trg,
                static_cast<int16_t>(pose.health&0xFFFF),static_cast<int16_t>(pose.health>>16),Gameplay(pose)?1U:0U,
                pose.system,pose.stop,position.x,position.y,position.z,angles.x,angles.y,angles.z);
        }
    }
    if(now-state.stats_tick>=1000U) {
        state.stats_tick=now;
        const LONG dropped=InterlockedExchange(&runtime::dropped,0),failed=InterlockedExchange(&runtime::failed,0);
        const LONG unscoped=InterlockedCompareExchange(&runtime::unscoped,0,0);
        LogV("shield observer discarded_since_last dropped=%ld unstable_or_unreadable=%ld unscoped_total=%ld dropped_total=%ld nested_total=%ld",
            dropped,failed,unscoped,InterlockedCompareExchange(&runtime::dropped_total,0,0),InterlockedCompareExchange(&runtime::nested,0,0));
        if(!state.limit_logged&&InterlockedCompareExchange(&runtime::remaining,0,0)==0) {
            Log("shield sphere log budget exhausted; rear_active_tracking=%u",g_rear_active?1U:0U);state.limit_logged=true;
        }
    }
}

static BOOL CALLBACK LoadSystemInput(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH] = {};
    UINT count = GetSystemDirectoryW(path, MAX_PATH);
    if (!count || count > MAX_PATH - 14) return TRUE;
    wcscat_s(path, L"\\dinput8.dll");
    HMODULE original = LoadLibraryW(path);
    if (original) {
#pragma warning(push)
#pragma warning(disable: 4191)
        g_forward = reinterpret_cast<DirectInputCreate>(GetProcAddress(original, "DirectInput8Create"));
#pragma warning(pop)
    }
    return TRUE;
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid,
                                            LPVOID* output, LPUNKNOWN outer) {
    InitOnceExecuteOnce(&g_forward_once, LoadSystemInput, nullptr, nullptr);
    const HRESULT result = g_forward ? g_forward(instance, version, iid, output, outer) : E_FAIL;
    if (SUCCEEDED(result) && output && *output) InterlockedExchange(&g_input_ready, 1);
    return result;
}

static uint32_t FindPlayerSlot(unsigned char* base, unsigned image_size) {
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    static const unsigned char pattern[] = {
        0xA1, 0, 0, 0, 0, 0xD8, 0xCC, 0xD8, 0xC9, 0xD8, 0xCA, 0xD9, 0x5D, 0, 0xD9, 0x45
    };
    static const char mask[] = "x????xxxxxxxx?xx";
    uint32_t slot = 0;
    unsigned hits = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        unsigned begin = section[s].VirtualAddress, size = section[s].Misc.VirtualSize;
        if (begin >= image_size || size > image_size - begin) continue;
        for (unsigned i = 0; i + sizeof(pattern) <= size; ++i) {
            bool match = true;
            for (unsigned j = 0; j < sizeof(pattern); ++j)
                if (mask[j] != '?' && base[begin + i + j] != pattern[j]) { match = false; break; }
            if (match) { std::memcpy(&slot, base + begin + i + 1, 4); ++hits; }
        }
    }
    Log("player pattern matches=%u", hits);
    auto image_start = reinterpret_cast<uint32_t>(base);
    return hits == 1 && slot >= image_start && slot - image_start <= image_size - 4 ? slot : 0;
}

static uint32_t FindGlobalSlot(unsigned char* base, unsigned image_size) {
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    static const unsigned char pattern[] = {0xA1, 0, 0, 0, 0, 0xB9, 0xFF, 0xFF, 0xFF, 0x7F, 0x21, 0x48, 0, 0xA1};
    static const char mask[] = "x????xxxxxxx?x";
    uint32_t slot = 0;
    unsigned hits = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        unsigned begin = section[s].VirtualAddress, size = section[s].Misc.VirtualSize;
        if (begin >= image_size || size > image_size - begin) continue;
        for (unsigned i = 0; i + sizeof(pattern) <= size; ++i) {
            bool match = true;
            for (unsigned j = 0; j < sizeof(pattern); ++j)
                if (mask[j] != '?' && base[begin + i + j] != pattern[j]) { match = false; break; }
            if (match) { std::memcpy(&slot, base + begin + i + 1, 4); ++hits; }
        }
    }
    Log("global pattern matches=%u", hits);
    auto image_start = reinterpret_cast<uint32_t>(base);
    return hits == 1 && slot >= image_start && slot - image_start <= image_size - 4 ? slot : 0;
}

class AlertVoice {
    HWAVEOUT output = nullptr;
    WAVEHDR header = {};
    link_runtime::WaveData wave;
public:
    ~AlertVoice() {
        if (output) {
            waveOutReset(output);
            waveOutUnprepareHeader(output, &header, sizeof(header));
            waveOutClose(output);
        }
    }
    bool Load(const wchar_t* path, unsigned volume,const char* expected_sha=nullptr) {
        FILE* file = nullptr;
        if (_wfopen_s(&file, path, L"rb") || !file) return false;
        std::fseek(file, 0, SEEK_END);
        long length = std::ftell(file);
        std::rewind(file);
        if (length < 44 || length > 8 * 1024 * 1024) { std::fclose(file); return false; }
        std::vector<unsigned char> bytes(static_cast<size_t>(length));
        size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
        std::fclose(file);
        if (read != bytes.size())return false;
        if(expected_sha){unsigned char digest[32]{};if(!link_runtime::mortal_draw::resident_assets::detail::Hash(bytes.data(),bytes.size(),digest)||!link_runtime::mortal_draw::resident_assets::detail::Matches(digest,expected_sha))return false;}
        if(!link_runtime::ParseWave(bytes,wave))return false;
        // Scale this voice's samples, never the system/device mixer volume.
        link_runtime::ScaleWave(wave, volume);
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = static_cast<WORD>(wave.channels);
        format.nSamplesPerSec = wave.rate; format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(wave.channels * 2);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        MMRESULT result = waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
        if (result != MMSYSERR_NOERROR) { output = nullptr; Log("alert audio open failed=%u", result); return false; }
        header.lpData = reinterpret_cast<LPSTR>(wave.pcm.data());
        header.dwBufferLength = static_cast<DWORD>(wave.pcm.size());
        result = waveOutPrepareHeader(output, &header, sizeof(header));
        if (result != MMSYSERR_NOERROR) {
            waveOutClose(output); output = nullptr; Log("alert audio prepare failed=%u", result); return false;
        }
        return true;
    }
    bool Play() {
        if (!output || (header.dwFlags & WHDR_INQUEUE)) return false;
        MMRESULT result = waveOutWrite(output, &header, sizeof(header));
        if (result != MMSYSERR_NOERROR) Log("alert audio write failed=%u", result);
        return result == MMSYSERR_NOERROR;
    }
    void Stop() { if (output && (header.dwFlags & WHDR_INQUEUE)) waveOutReset(output); }
};


class ParryVoices {
    AlertVoice voices[3];
    link_runtime::parry::audio::Variation variation{GetTickCount()};
public:
    bool Load(const wchar_t* directory){
        const char* pins[]={
            "48ce343134f913e5ef8711c582cfe9626c56149543749a5b99ec06851200ddc5",
            "197c1c6ca81ba52aebdf880d8d1b8e00f4458a253467e7372cec9e8f2c9ecb46",
            "9bd5320f3d2ba1b8262ef930b46232404e583a7b274fbc508e994fa043a94d65"};
        for(unsigned i=0;i<3;++i){
            wchar_t path[MAX_PATH]{};
            if(link_runtime::paths::Format(path,L"%slink_parry_%u.wav",directory,i)<=0||!voices[i].Load(path,65,pins[i]))return false;
        }
        return true;
    }
    bool Play(){const auto selected=variation.Next();const bool ok=voices[selected].Play();Log("ParryAudio variant=%u submitted=%u",selected,ok?1U:0U);return ok;}
    void Stop(){for(auto& voice:voices)voice.Stop();}
};

// Shared Mortal Draw dispatch, clip and voice (MortalDrawContextual, ocarina, sword actions).
// Immutable setup precedes enabling hooks; all game mutation stays in the qualified dispatch
// callback, audio on the worker.
namespace mortal=link_runtime::mortal_draw;
static link_runtime::shield::NativeAccess g_mortal_native;
static mortal::voice::Mailbox g_mortal_voice;
struct MortalRecord {unsigned kind=0,status=0,routine=0;uint64_t token=0;};
static MortalRecord g_mortal_records[16];
static std::atomic<unsigned> g_mortal_write{0},g_mortal_read{0},g_mortal_dropped{0};
static void MortalRecordPush(MortalRecord value){
    const unsigned w=g_mortal_write.load(std::memory_order_relaxed),r=g_mortal_read.load(std::memory_order_acquire);
    if(w-r==16){g_mortal_dropped.fetch_add(1);return;}
    g_mortal_records[w%16]=value;g_mortal_write.store(w+1,std::memory_order_release);
}
static void CombatLifecycle(mortal::dispatch::Event event,uint64_t token);
static bool SwordLifecycle(mortal::dispatch::Event event,uint64_t token);
static void __cdecl MortalLifecycle(void*,mortal::dispatch::Event event,uint64_t token){
    if(SwordLifecycle(event,token))return; // own clips/voices never enter the Mortal Draw mailbox
    mortal::voice::Bridge(&g_mortal_voice,event,token);
    CombatLifecycle(event,token);
    MortalRecordPush({2,static_cast<unsigned>(event),0,token});
}
static bool __cdecl MortalCleanup(void* player){
    // Called before ending our owned clip. Never clear a foreign event/pose.
    return mortal::trial::CleanupPose(player,reinterpret_cast<mortal::trial::Clear>(g_mortal_native.image_base+0x25EF10));
}
// Bug #1 (24-oct): the pose an abandoned own clip left (a grab mid Back Slice), reset right before the native clip
// that took the model is evaluated (mortal_draw_dispatch.h Runtime::Residue). Game thread; the worker logs it.
static bool __cdecl MortalRestore(void* player){
    const auto& run=mortal::dispatch_hooks::runtime;
    const unsigned parts=mortal::trial::RestorePose(player);
    MortalRecordPush({4,parts,mortal::dispatch::Read<uint32_t>(static_cast<unsigned char*>(player)+0xFC),run.residue_token});
    return parts!=0;
}
static bool MortalRead(const wchar_t* directory,const wchar_t* file,size_t expected,std::vector<unsigned char>& out){
    wchar_t path[MAX_PATH]{};if(link_runtime::paths::Format(path,L"%s%ls",directory,file)<0)return false;
    FILE* input=nullptr;if(_wfopen_s(&input,path,L"rb")||!input)return false;
    std::fseek(input,0,SEEK_END);const long length=std::ftell(input);std::rewind(input);
    bool ok=length>=0&&static_cast<size_t>(length)==expected;
    if(ok){out.resize(expected);ok=std::fread(out.data(),1,expected,input)==expected;}
    std::fclose(input);return ok;
}
static bool CaptureMortalNative(unsigned char* base,unsigned size){
    using namespace link_runtime::shield;
    try{
        NativeAccess n;uint32_t preferred=0;
        if(!MappedRegions(base,size,preferred,n.regions))return false;
        n.image_base=reinterpret_cast<uint32_t>(base);n.image_size=size;
        n.code=FindNative(base,size,n.image_base,n.regions.data(),n.regions.size(),&g_foreign_probe);n.verified=n.code.verified;
        if(n.verified&&n.code.key_site_foreign)
            LogForeignPatch("MortalNative","call_redirected",static_cast<uint32_t>(n.code.key_site_rva),n.code.key_site_target);
        if(!Complete(n))return false;
        g_mortal_native=n;return true;
    }catch(...){return false;}
}
// ---------------------------------------------------------------------------------------------
// Ocarina of Time, phase 1 (Task 10). Everything below is created only when [Ocarina] Enabled=1.
// Game thread: OcarinaDecide (inside the Mortal Draw player-handler dispatch), the Use hook and
// the dispatch callbacks. Worker: logs, load epoch, focus latch, pad poll and note audio.
namespace oca=link_runtime::ocarina;
namespace ocr=link_runtime::ocarina::runtime;
static ocr::Config g_oca_cfg;
static std::atomic<bool> g_oca_on{false};
static ocr::LoadEpoch g_oca_epoch;
static ocr::FocusLatch g_oca_focus;
static oca::NoteMailbox g_oca_notes;
static std::atomic<uint64_t> g_oca_pad{0};          // bit 48 connected | ly 16 | lx 16 | buttons 16
static std::atomic<uint64_t> g_oca_pad_right{0};    // lt 8 << 32 | ry 16 | rx 16 (18-oct: N64 ocarina style)
static std::atomic<uint32_t> g_oca_mounted_bin{0};  // worker skips SwordStep while the knife slot shows it
static uint32_t g_oca_player_slot=0,g_oca_global_slot=0,g_oca_input=0,g_oca_base=0;
struct OcaRecord {unsigned kind=0;int code=0;unsigned a=0,b=0;uint64_t token=0;char text[40]{};};
static OcaRecord g_oca_records[64];
static std::atomic<unsigned> g_oca_write{0},g_oca_read{0},g_oca_dropped{0};
static void OcaRecordPush(unsigned kind,const char* text,int code=0,unsigned a=0,unsigned b=0,uint64_t token=0){
    const unsigned w=g_oca_write.load(std::memory_order_relaxed),r=g_oca_read.load(std::memory_order_acquire);
    if(w-r==64){g_oca_dropped.fetch_add(1);return;}
    OcaRecord& out=g_oca_records[w%64];
    out.kind=kind;out.code=code;out.a=a;out.b=b;out.token=token;
    strncpy_s(out.text,text?text:"",_TRUNCATE);
    g_oca_write.store(w+1,std::memory_order_release);
}
// Resident ocarina files: copied once, never released (the engine relocates FCV/BIN/TPL in place).
struct OcaFile {const wchar_t* name;size_t size;const char* sha;};
static const OcaFile kOcaClips[6]={
    {L"link_ocarina_enter.fcv",324096,"dc45586361158ea71e1919fb9e9bfb1907bddfa71e0d0a29afef2916ab9d1d37"},
    {L"link_ocarina_enter.seq",96,"d428923dd942e320b5caa2eeddeb048962e6636c1dc5ae08b9698ecf0ee8fd93"},
    {L"link_ocarina_loop.fcv",501504,"ec07b0d9d15e9b33707844e80f3b4e0481f4fda8c7e23b02032e1996fdd0fa4c"},
    {L"link_ocarina_loop.seq",128,"f0b34b875ea07a7fc7f921be6b177e4c048c45a11263ea3e6acd9cd65a4af7e1"},
    {L"link_ocarina_exit.fcv",324096,"294ca33ada4486ec97cb2bc50fd9190606a8705f994609aab86e5cbb17cc8f9b"},
    {L"link_ocarina_exit.seq",96,"d428923dd942e320b5caa2eeddeb048962e6636c1dc5ae08b9698ecf0ee8fd93"}};
static const OcaFile kOcaBin={L"link_ocarina.bin",21184,"51289de233eaa5ea4873145f753e5fe9251349f6062518222c95c5c55ad9815d"};
// Costume 0..3 (pl00, pl08, pl09, pl10): TPL pointing at the ocarina texture appended to that pack.
static const OcaFile kOcaTpls[4]={
    {L"link_ocarina_pl00.tpl",64,"a1ed56e37ed77cb65ec42512f992ef79f36e5ea721574dacb493b4d792d11503"},
    {L"link_ocarina_pl08.tpl",64,"468d47d453d1faffb2fa87055a4b6ffedbdc55b03280c3e494e3e19aceacbf2e"},
    {L"link_ocarina_pl09.tpl",64,"8c3c5ff3c350b1d0a6cde75ca1efad6b9fe46e9a71f815d06aed13804d947905"},
    {L"link_ocarina_pl10.tpl",64,"e152617b2d604356db9bf7bb656aa477ea4a6b59293938377189df442468e721"}};
static const char* const kOcaNoteSha[5]={
    "626bc2a975f9f9d078c16f92091188dc9b36f5d192a1726c77609a295ba87968","6bef91b992e2aae9108ef5c64609fa0bf43c23a8a43004918dfd3813d8990faf",
    "cbdb2a7d0443c363de6372ade06ecb24e659dab746a3185a505af8c543c343bf","92677493fad4efcc4177026c62398834acfe285299f0961824ff2ad09129f7f1",
    "70e109aee83ca42e90fe4193a5d8b3d45560f04ac1d253662100278a3a4d04b6"};
static unsigned char* g_oca_clip[6]{};
static unsigned char* g_oca_bin=nullptr;
static unsigned char* g_oca_tpl[4]{};
static oca::OcarinaAsset g_oca_asset[4];
static bool g_oca_asset_tried[4]{};
static unsigned char* OcaResident(const wchar_t* directory,const OcaFile& file,bool quiet=false){
    std::vector<unsigned char> bytes;unsigned char digest[32]{};
    if(!MortalRead(directory,file.name,file.size,bytes)||!mortal::resident_assets::detail::Hash(bytes.data(),bytes.size(),digest)||
       !mortal::resident_assets::detail::Matches(digest,file.sha)){if(!quiet)Log("Ocarina asset missing or changed: %ls",file.name);return nullptr;}
    auto* out=static_cast<unsigned char*>(VirtualAlloc(nullptr,file.size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(out)std::memcpy(out,bytes.data(),file.size);
    return out;
}
static bool OcaCopy(uint32_t address,unsigned char* out,size_t n){
    __try{std::memcpy(out,reinterpret_cast<const void*>(address),n);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// SEH-guarded image reader for native_pins / image_relocation (copies only).
struct ImageMem {bool Read(uint32_t address,void* out,size_t n){return address&&OcaCopy(address,static_cast<unsigned char*>(out),n);}};
// Native code is called only after its exact bytes match the pins of the offline attribution.
// Relocated operands are normalized to the preferred base first (native_pins.h), so any image base works.
template<size_t N> static bool PinsAt(uint32_t base,const link_runtime::native_pins::Pin (&pins)[N]){
    ImageMem m;return link_runtime::native_pins::All(m,base,pins);
}
struct OcaNative {   // ocarina_mount.h Native: calcModelAddr 25CB50, calcTplAddr 25CCF0, getBoundingBox 25D120
    static bool Call1(uint32_t function,uint32_t argument){
        __try{reinterpret_cast<void(__cdecl*)(uint32_t)>(function)(argument);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Box(uint32_t bin,uint32_t* box){
        __try{reinterpret_cast<void(__cdecl*)(uint32_t,void*)>(g_oca_base+0x25D120)(bin,box);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool CalcModelAddr(uint32_t bin){return Call1(g_oca_base+0x25CB50,bin);}
    bool CalcTplAddr(uint32_t tpl){return Call1(g_oca_base+0x25CCF0,tpl);}
    bool BoundingBox(uint32_t bin,uint32_t (&out)[6]){
        uint32_t box[9]={};
        if(!Box(bin,box))return false;
        for(unsigned i=0;i<6;++i)out[i]=box[3+i];
        return true;
    }
};
// Item 0x0D (ocarina_item.h). The pinned EXE at any image base: VAs are rebased from 0x400000.
using OcaItemGet=bool(__thiscall*)(void*,uint32_t,uint32_t);
using OcaItemSearch=void*(__thiscall*)(void*,uint32_t);
using OcaItemUse=bool(__thiscall*)(void*,void*);
static OcaItemUse g_oca_use_original=nullptr;
static oca::OcarinaUse g_oca_use;
static bool OcaItemCall(bool get,uint16_t id,bool& result){
    void* manager=reinterpret_cast<void*>(g_oca_base+(oca::rva::kItemMgrVa-link_runtime::image_reloc::kPreferredBase));
    __try{
        if(get)result=reinterpret_cast<OcaItemGet>(g_oca_base+oca::rva::kGetThunk)(manager,id,1);
        else result=reinterpret_cast<OcaItemSearch>(g_oca_base+oca::rva::kSearchThunk)(manager,id)!=nullptr;
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static uint16_t OcaItemId(void* item){
    if(!item)return 0xFFFF;
    __try{return *static_cast<volatile uint16_t*>(item);}__except(EXCEPTION_EXECUTE_HANDLER){return 0xFFFF;}
}
static bool __fastcall OcaUseDetour(void* manager,void*,void* item){
    const DWORD error=GetLastError();
    const uint16_t id=OcaItemId(item);
    if(g_oca_on.load(std::memory_order_acquire)&&g_oca_use.OnUse(id,g_oca_epoch.Current())){
        OcaRecordPush(6,"use taken",id,g_oca_epoch.Current());SetLastError(error);return true;
    }
    SetLastError(error);
    return g_oca_use_original(manager,item);
}
static bool OcaPatchUse(){
    if(!g_oca_base||!PinsAt(g_oca_base,link_runtime::native_pins::kOcarinaItem)){
        Log("Ocarina item disabled: native item pins (base 0x%X)",g_oca_base);return false;
    }
    // Thunks: e9 rel32 to the pinned bodies.
    const struct {uint32_t thunk,body;} thunks[]={{oca::rva::kGetThunk,oca::rva::kGetBody},{oca::rva::kSearchThunk,oca::rva::kSearchBody},{oca::rva::kUseThunk,0x306C90}};
    for(const auto& t:thunks){
        unsigned char code[5]{};
        ImageMem m;if(!link_runtime::image_reloc::ReadPreferred(m,g_oca_base,t.thunk,code,5))return false;
        int32_t rel=0;std::memcpy(&rel,code+1,4);
        if(code[0]!=0xE9||t.thunk+5+static_cast<uint32_t>(rel)!=t.body){Log("Ocarina item disabled: thunk 0x%X",t.thunk);return false;}
    }
    // Patched once on the worker at process start, before any hook is enabled (the game thread is
    // not in the case menu yet). It stays in place even if a later dispatch step fails: the detour
    // forwards every call to the original while g_oca_on is false (review M5).
    g_oca_use_original=reinterpret_cast<OcaItemUse>(g_oca_base+oca::rva::kUseThunk);
    auto* site=reinterpret_cast<unsigned char*>(g_oca_base+oca::rva::kUseCall);
    const int32_t rel=static_cast<int32_t>(reinterpret_cast<uint32_t>(&OcaUseDetour)-(g_oca_base+oca::rva::kUseCall+5));
    DWORD old=0;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old)){Log("Ocarina item disabled: protect");return false;}
    std::memcpy(site+1,&rel,4);
    DWORD ignored=0;VirtualProtect(site,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),site,5);
    Log("Ocarina item Use hook: base=0x%X call 0x%X -> detour; other ids tail-call 0x%X",g_oca_base,oca::rva::kUseCall,oca::rva::kUseThunk);
    return true;
}
struct OcaItemHost {
    bool gameplay=false;
    uint32_t LoadEpoch(){return g_oca_epoch.Current();}
    bool GameplayValid(){return gameplay;}
    bool HasItem(uint16_t id){bool found=false;return OcaItemCall(false,id,found)&&found;}
    bool GetItem(uint16_t id,int){bool made=false;return OcaItemCall(true,id,made)&&made;}
    void Log(const char* text){OcaRecordPush(6,text);}
};
static oca::OcarinaGrant<OcaItemHost> g_oca_grant;
// Ocarina Manual (ocarina_manual.h): File_31 listed, its rows pointed at the four pages, given with the ocarina.
namespace omn=link_runtime::ocarina::manual;
static bool g_oca_manual_on=false;
static omn::ManualGrant<OcaItemHost> g_oca_manual;
static omn::RowSync g_oca_manual_rows;
struct OcaPatchMem {
    bool Read(uint32_t a,void* out,size_t n){return a&&OcaCopy(a,static_cast<unsigned char*>(out),n);}
    bool Write(uint32_t a,const void* in,size_t n){
        DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(a),n,PAGE_EXECUTE_READWRITE,&old))return false;
        std::memcpy(reinterpret_cast<void*>(a),in,n);DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(a),n,old,&ignored);
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(a),n);return true;
    }
};
// El Gigante's tree attack (gigante_tree_guard.h, gigante-tree-guard-01): null checks for the held tree in em2b r1
// routine 0x0E and a watcher of its entries. Written once on the worker at process start (no em2b runs yet); all
// pins or nothing. Skips log at LogLevel 1, the watcher's transitions at LogLevel 2.
static void GiganteTreeLine(int level,const char* text){if(level<=1)Log("%s",text);else LogV("%s",text);}
static uint32_t GiganteTreeNow(){return GetTickCount();}
static void StartGiganteTreeGuard(const wchar_t* ini){
    namespace gt=link_runtime::gigante_tree;
    if(GetPrivateProfileIntW(L"LinkRuntime",L"GiganteTreeGuard",1,ini)!=1){Log("gigante_tree_guard installed=0 reason=off");return;}
    const uint32_t base=reinterpret_cast<uint32_t>(GetModuleHandleW(nullptr));
    gt::g_line=&GiganteTreeLine;gt::g_now=&GiganteTreeNow;
    OcaPatchMem m;uint32_t bad=0,written=0;
    const gt::Verdict v=gt::Install(m,base,bad,written);
    if(v==gt::Verdict::Installed)Log("gigante_tree_guard installed=1 base=0x%X release=0x%X hp=0x%X watch=0x%X",base,
        gt::rva::kReleaseSite,gt::rva::kHpSite,gt::rva::kWatchSite);
    else Log("gigante_tree_guard installed=0 reason=%s pin=0x%X written=%u base=0x%X",gt::Name(v),bad,written,base);
}
// Grab damage windows that hit once (grab_hit_once.h): El Gigante's grab (gigante-grab-once-01, GiganteGrabOnce) and
// Salazar's tentacle grab, Saddler's two grabs and the Ganado neck grab. One hit block per window, as at 30 FPS. Each site
// is written once on the worker at process start, all of its pins or nothing. Every hit and every skipped repeat logs at
// LogLevel 1 (`grab_once hit|skip site=...`).
static void StartGrabOnceSite(const wchar_t* ini,uint32_t site){
    namespace go=link_runtime::grab_once;
    const go::Site& s=go::kSiteTable[site];
    const wchar_t* keys[go::kSites]={L"GiganteGrabOnce",L"SalazarGrabOnce",L"SaddlerGrabOnce",L"SaddlerGrabOnce",L"GanadoNeckGrabOnce"};
    const char* tags[go::kSites]={"gigante_grab_once","salazar_grab_once","saddler_grab_once","saddler_grab_once","ganado_neck_grab_once"};
    const wchar_t* key=keys[site];const char* tag=tags[site];
    if(GetPrivateProfileIntW(L"LinkRuntime",key,1,ini)!=1){Log("%s installed=0 reason=off",tag);return;}
    const uint32_t base=reinterpret_cast<uint32_t>(GetModuleHandleW(nullptr));
    go::g_line=&GiganteTreeLine;
    OcaPatchMem m;uint32_t bad=0;
    const go::Verdict v=go::Install(m,site,base,bad);
    if(v==go::Verdict::Installed){
        if(site==go::kGigante)Log("gigante_grab_once installed=1 base=0x%X site=0x%X skip=0x%X window=%.4f..%.4f damage=%u max_gap_ticks=%u",
            base,s.site,s.skip,double(go::g_low[site]),double(go::g_high[site]),s.damage,go::kMaxGapTicks);
        else if(site==go::kSalazar)Log("salazar_grab_once installed=1 base=0x%X site=0x%X skip=0x%X window=%.4f..%.4f damage=%u max_gap_ticks=%u",
            base,s.site,s.skip,double(go::g_low[site]),double(go::g_high[site]),s.damage,go::kMaxGapTicks);
        else Log("%s installed=1 base=0x%X site=0x%X skip=0x%X window=%.4f..%.4f damage=dynamic max_gap_ticks=%u",
            tag,base,s.site,s.skip,double(go::g_low[site]),double(go::g_high[site]),go::kMaxGapTicks);
    }else Log("%s installed=0 reason=%s pin=0x%X base=0x%X",tag,go::Name(v),bad,base);
}
static void StartGiganteGrabOnce(const wchar_t* ini){StartGrabOnceSite(ini,link_runtime::grab_once::kGigante);}
static void StartSalazarGrabOnce(const wchar_t* ini){StartGrabOnceSite(ini,link_runtime::grab_once::kSalazar);}
static void StartGanadoNeckGrabOnce(const wchar_t* ini){StartGrabOnceSite(ini,link_runtime::grab_once::kGanadoNeck);}
static void StartSaddlerGrabOnce(const wchar_t* ini){
    namespace go=link_runtime::grab_once;
    if(GetPrivateProfileIntW(L"LinkRuntime",L"SaddlerGrabOnce",1,ini)!=1){Log("saddler_grab_once installed=0 reason=off");return;}
    const uint32_t base=reinterpret_cast<uint32_t>(GetModuleHandleW(nullptr));
    go::g_line=&GiganteTreeLine;
    OcaPatchMem m;uint32_t bad=0;
    const go::Verdict v=go::InstallSaddler(m,base,bad);
    if(v==go::Verdict::Installed)Log("saddler_grab_once installed=1 base=0x%X sites=0x%X,0x%X window=85.7..86.3 damage=900 max_gap_ticks=%u",
        base,go::kSiteTable[go::kSaddler].site,go::kSiteTable[go::kSaddlerStep].site,go::kMaxGapTicks);
    else Log("saddler_grab_once installed=0 reason=%s pin=0x%X base=0x%X",go::Name(v),bad,base);
}
static void StartChainSoundOnce(const wchar_t* ini){
    namespace cs=link_runtime::chain_sound_once;
    if(GetPrivateProfileIntW(L"LinkRuntime",L"ChainSoundOnce",1,ini)!=1){Log("chain_sound_once installed=0 reason=off");return;}
    const uint32_t base=reinterpret_cast<uint32_t>(GetModuleHandleW(nullptr));
    cs::g_line=&GiganteTreeLine;
    OcaPatchMem m;uint32_t bad=0,foreign=0;
    const cs::Verdict v=cs::Install(m,base,bad,&g_foreign_probe,&foreign);
    if(foreign&&v==cs::Verdict::Installed)LogForeignPatch("ChainSoundOnce","jmp_redirected",cs::rva::kSound,foreign);
    if(v==cs::Verdict::Installed)Log("chain_sound_once installed=1 base=0x%X site=0x%X target=0x%X thunk=%s",base,cs::rva::kSite,
        cs::rva::kSound,foreign?"foreign":"native");
    else Log("chain_sound_once installed=0 reason=%s pin=0x%X base=0x%X",cs::Name(v),bad,base);
}
static void StartEm3fAttackTimer(const wchar_t* ini){
    namespace et=link_runtime::em3f_attack_timer;
    if(GetPrivateProfileIntW(L"LinkRuntime",L"Em3fAttackTimerFix",1,ini)!=1){Log("em3f_attack_timer installed=0 reason=off");return;}
    const uint32_t base=reinterpret_cast<uint32_t>(GetModuleHandleW(nullptr));
    et::g_line=&GiganteTreeLine;
    OcaPatchMem m;uint32_t bad=0;
    const et::Verdict v=et::Install(m,base,bad);
    if(v==et::Verdict::Installed)Log("em3f_attack_timer installed=1 base=0x%X site=0x%X target=0x%X",base,et::rva::kSite,et::rva::kAttack);
    else Log("em3f_attack_timer installed=0 reason=%s pin=0x%X base=0x%X",et::Name(v),bad,base);
}
// --- case-key-debounce-01 (21-oct): one TAB press = one attache-case toggle (case_key_debounce.h). MinHook on PadRead,
// filter on the game thread right after it; all pins or nothing. Masked edges log at LogLevel 1, passes at 2.
static uint64_t CaseKeyNow(){return GetTickCount64();}
static void StartCaseKeyDebounce(const wchar_t* ini){
    namespace ck=link_runtime::case_key_debounce;
    if(GetPrivateProfileIntW(L"LinkRuntime",L"CaseKeyDebounce",1,ini)!=1){Log("case_key_debounce installed=0 reason=off");return;}
    unsigned ms=GetPrivateProfileIntW(L"LinkRuntime",L"CaseKeyDebounceMs",ck::kDefaultMs,ini);
    if(!ck::ValidMs(ms)){Log("CaseKeyDebounceMs=%u invalid (0..%u); %u used",ms,ck::kMaxMs,ck::kDefaultMs);ms=ck::kDefaultMs;}
    const uint32_t base=reinterpret_cast<uint32_t>(GetModuleHandleW(nullptr));
    MH_STATUS s=MH_Initialize();
    if(s!=MH_OK&&s!=MH_ERROR_ALREADY_INITIALIZED){Log("case_key_debounce installed=0 reason=minhook_%s",MH_StatusToString(s));return;}
    ck::g_line=&GiganteTreeLine;ck::g_now=&CaseKeyNow;
    OcaPatchMem m;uint32_t bad=0;
    const ck::Verdict v=ck::Install(m,base,ms,bad,[](void* target,void* detour,void** original){
        if(MH_CreateHook(target,detour,original)!=MH_OK)return false;
        if(MH_EnableHook(target)==MH_OK)return true;
        MH_RemoveHook(target);return false;});
    if(v==ck::Verdict::Installed)Log("case_key_debounce installed=1 base=0x%X hook=0x%X key=0x%X debounce_ms=%u max_latch_ms=%u clock=real",
        base,ck::rva::kPadRead,ck::layout::kKeySscrn,ms,static_cast<unsigned>(ck::kMaxLatchMs));
    else Log("case_key_debounce installed=0 reason=%s pin=0x%X base=0x%X",ck::Name(v),bad,base);
}
// --- end case-key-debounce-01
// Krauser's bow for Link, pilot without art (krauser_bow_pilot.h, KrauserBowPilot=1; off by default): Leon's weapon
// file row 0x1C gets Krauser's wep28 and Leon's setLeftHand turns the bow's hand 7 into Leon's 1. Startup only.
using KbpSetLeftHand=uint32_t(__fastcall*)(void*,void*,int);
static KbpSetLeftHand g_kbp_original=nullptr;
static std::atomic<unsigned> g_kbp_remaps{0};
static uint32_t __fastcall KbpSetLeftHandDetour(void* self,void* edx,int type){
    namespace kbp=link_runtime::krauser_bow_pilot;
    const int mapped=kbp::RemapLeftHand(type);
    if(mapped!=type&&g_kbp_remaps.fetch_add(1)<8)LogV("krauser_bow_pilot left_hand %d->%d",type,mapped);
    return g_kbp_original(self,edx,mapped);
}
using KbpWeaponFilename=void(__cdecl*)(char*,int);
static KbpWeaponFilename g_kbp_filename_original=nullptr;
static uint32_t g_kbp_base=0;
static void __cdecl KbpWeaponFilenameDetour(char* name,int no){
    namespace kbp=link_runtime::krauser_bow_pilot;
    g_kbp_filename_original(name,no);
    int pl_type=-1;
    __try{const uint32_t g=*reinterpret_cast<uint32_t*>(g_kbp_base+kbp::rva::kGlobalSlot);
          if(g)pl_type=*reinterpret_cast<unsigned char*>(g+kbp::rva::kPlType);}__except(EXCEPTION_EXECUTE_HANDLER){pl_type=-1;}
    if(name&&kbp::BowCaseFile(pl_type,no)){
        std::memcpy(name,kbp::kBowCaseFile,sizeof(kbp::kBowCaseFile));
        LogV("krauser_bow_pilot case_file weapon=0x%X -> %s",no,kbp::kBowCaseFile);
    }
}
using KbpLeonModelInit=void(__cdecl*)(uint32_t,uint32_t);
static KbpLeonModelInit g_kbp_view_original=nullptr;
static void __cdecl KbpLeonModelInitDetour(uint32_t no,uint32_t type){
    namespace kbp=link_runtime::krauser_bow_pilot;
    const uint32_t mapped=kbp::LeonViewWeapon(no);
    if(mapped!=no)LogV("krauser_bow_pilot case_view weapon=0x%X -> 0x%X",no&0xFFFF,mapped&0xFFFF);
    g_kbp_view_original(mapped,type);
}
static void StartKrauserBowPilot(const wchar_t* ini){
    namespace kbp=link_runtime::krauser_bow_pilot;
    if(GetPrivateProfileIntW(L"LinkRuntime",L"KrauserBowPilot",0,ini)!=1){Log("krauser_bow_pilot installed=0 reason=off");return;}
    const uint32_t base=reinterpret_cast<uint32_t>(GetModuleHandleW(nullptr));
    if(!PinsAt(base,kbp::kPins)){Log("krauser_bow_pilot installed=0 reason=pins base=0x%X",base);return;}
    MH_STATUS s=MH_Initialize();
    if(s!=MH_OK&&s!=MH_ERROR_ALREADY_INITIALIZED){Log("krauser_bow_pilot installed=0 reason=minhook_%s",MH_StatusToString(s));return;}
    void* target=reinterpret_cast<void*>(base+kbp::rva::kSetLeftHand);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&KbpSetLeftHandDetour),reinterpret_cast<void**>(&g_kbp_original))!=MH_OK){
        Log("krauser_bow_pilot installed=0 reason=hook");return;}
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);Log("krauser_bow_pilot installed=0 reason=hook_enable");return;}
    void* filename=reinterpret_cast<void*>(base+kbp::rva::kWeaponFilename);
    g_kbp_base=base;
    if(MH_CreateHook(filename,reinterpret_cast<void*>(&KbpWeaponFilenameDetour),reinterpret_cast<void**>(&g_kbp_filename_original))!=MH_OK||
       MH_EnableHook(filename)!=MH_OK){
        MH_RemoveHook(filename);MH_DisableHook(target);MH_RemoveHook(target);Log("krauser_bow_pilot installed=0 reason=hook_filename");return;}
    void* view=reinterpret_cast<void*>(base+kbp::rva::kLeonModelInit);
    if(MH_CreateHook(view,reinterpret_cast<void*>(&KbpLeonModelInitDetour),reinterpret_cast<void**>(&g_kbp_view_original))!=MH_OK||
       MH_EnableHook(view)!=MH_OK){
        MH_RemoveHook(view);MH_DisableHook(filename);MH_RemoveHook(filename);MH_DisableHook(target);MH_RemoveHook(target);
        Log("krauser_bow_pilot installed=0 reason=hook_view");return;}
    OcaPatchMem m;
    const auto v=kbp::PatchRow(m,base);
    if(v!=kbp::Verdict::Installed){MH_DisableHook(target);MH_RemoveHook(target);MH_DisableHook(filename);MH_RemoveHook(filename);
        MH_DisableHook(view);MH_RemoveHook(view);
        Log("krauser_bow_pilot installed=0 reason=%s (Leon row 0x%X / Krauser row 0x%X)",kbp::Name(v),kbp::rva::kLeonRow,kbp::rva::kKrauserRow);return;}
    const auto piece=kbp::PatchPiece(m,base);   // the case piece 7x3 -> the Hero's Bow's 5x2 (cosmetic: a failure keeps 7x3)
    Log("krauser_bow_pilot installed=1 base=0x%X leon_row=0x%X <- wep28 (0xC3/0xC4) left_hand 7->1 hook=0x%X case_piece=%s case_view=0x1C->0x0E/ss_wep14",base,
        kbp::rva::kLeonRow,kbp::rva::kSetLeftHand,piece==kbp::Verdict::Installed?"5x2":kbp::Name(piece));
}
// A Ganado's head bursts into ONE Plaga (plaga_parasite_once.h, plaga-parasite-once-01): at 165 FPS the Parasite_wait
// countdown of cEm10::move calls em10SetParasite twice and the orphaned Plaga squelches forever. Written once on the
// worker at process start; all pins or nothing. Every burst logs at LogLevel 1 (first 16, then every 64th).
static void StartPlagaParasiteOnce(const wchar_t* ini){
    namespace po=link_runtime::plaga_once;
    if(GetPrivateProfileIntW(L"LinkRuntime",L"PlagaParasiteOnce",1,ini)!=1){Log("plaga_once installed=0 reason=off");return;}
    const uint32_t base=reinterpret_cast<uint32_t>(GetModuleHandleW(nullptr));
    po::g_line=&GiganteTreeLine;
    OcaPatchMem m;uint32_t bad=0;
    const po::Verdict v=po::Install(m,base,bad);
    if(v==po::Verdict::Installed)Log("plaga_once installed=1 base=0x%X site=0x%X target=0x%X",base,po::rva::kSite,po::rva::kSetParasite);
    else Log("plaga_once installed=0 reason=%s pin=0x%X base=0x%X",po::Name(v),bad,base);
}
static void OcaManualRows(){   // game thread: manual rows in the main game, native rows in Separate Ways
    uint32_t global=0,flags=0;LocalMemory m;bool manual=true;
    if(!m.Read32(g_oca_base+omn::kGlobalSlotRva,global)||!global||!m.Read32(global+omn::kSeparateWaysOffset,flags))return;
    if(!g_oca_manual_rows.Want((flags&omn::kSeparateWaysBit)!=0,manual))return;
    OcaPatchMem pm;const int n=omn::Apply(pm,g_oca_base,omn::kRows,manual);
    if(n<0){g_oca_manual_on=false;OcaRecordPush(6,"ocarina manual rows unknown, manual off");return;}
    g_oca_manual_rows.Done(manual);OcaRecordPush(6,manual?"ocarina manual rows":"ocarina manual native rows (Separate Ways)",n);
}
// ---------------------------------------------------------------------------------------------
// The fairy revives Link (fairy_revive.h, docs/fairy-revive-2026-10-12.md). Created only when
// [FairyRevive] Enabled=1, and last: its pins must see LifeDownSet2 and cItemMgr::use unhooked, and
// ShieldPreClampObserve fingerprints the whole LifeDownSet2 player path before this entry hook.
// Game thread: the LifeDownSet2 detour (search/use/HP write). Worker: log and the optional sound.
namespace fairy=link_runtime::fairy;
using FairyLifeDown=int(__cdecl*)(void*,int,int,int);
static FairyLifeDown g_fairy_original=nullptr;
static std::atomic<bool> g_fairy_on{false};
static uint32_t g_fairy_base=0;
static fairy::Event g_fairy_events[16];
static std::atomic<unsigned> g_fairy_write{0},g_fairy_read{0},g_fairy_dropped{0};
static bool FairyStore(uint32_t address,const void* in,size_t n){
    __try{std::memcpy(reinterpret_cast<void*>(address),in,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool FairySearch(uint16_t id,uint32_t& item){
    void* manager=reinterpret_cast<void*>(g_fairy_base+fairy::rva::kItemMgr);
    __try{item=reinterpret_cast<uint32_t>(reinterpret_cast<OcaItemSearch>(g_fairy_base+fairy::rva::kSearchThunk)(manager,id));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool FairyUse(uint32_t item,bool& used){
    void* manager=reinterpret_cast<void*>(g_fairy_base+fairy::rva::kItemMgr);
    __try{used=reinterpret_cast<OcaItemUse>(g_fairy_base+fairy::rva::kUseThunk)(manager,reinterpret_cast<void*>(item));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct FairyHost {
    bool Enabled(){return g_fairy_on.load(std::memory_order_acquire);}
    bool Read(uint32_t address,void* out,size_t n){return address&&OcaCopy(address,static_cast<unsigned char*>(out),n);}
    bool Write(uint32_t address,const void* in,size_t n){return address&&FairyStore(address,in,n);}
    bool Search(uint16_t id,uint32_t& item){return FairySearch(id,item);}
    bool Use(uint32_t item,bool& used){return FairyUse(item,used);}
    void Note(const fairy::Event& e){
        const unsigned w=g_fairy_write.load(std::memory_order_relaxed),r=g_fairy_read.load(std::memory_order_acquire);
        if(w-r==16){g_fairy_dropped.fetch_add(1);return;}
        g_fairy_events[w%16]=e;g_fairy_write.store(w+1,std::memory_order_release);
    }
};
static fairy::Reviver<FairyHost> g_fairy;
// MinHook target LifeDownSet2 0x1B5AF0 (cdecl, 4 args, EAX = remaining HP). Reached by jmp (thunk 0x71C1,
// then MinHook), so _AddressOfReturnAddress() is the caller's return slot that fairy_revive.h indexes.
static int __cdecl FairyLifeDownDetour(void* em,int damage,int rnd,int flag){
    const DWORD error=GetLastError();
    FairyHost host;
    const uint32_t ret_slot=reinterpret_cast<uint32_t>(_AddressOfReturnAddress());
    const uint32_t ret_rva=reinterpret_cast<uint32_t>(_ReturnAddress())-g_fairy_base;
    const fairy::Entry entry=g_fairy.Enter(host,reinterpret_cast<uint32_t>(em),damage,rnd,static_cast<uint32_t>(flag),ret_rva,ret_slot);
    SetLastError(error);
    const int result=link_runtime::shield::runtime::CallLifeOriginal(g_fairy_original,em,damage,rnd,flag,
        ret_rva+g_fairy_base,link_runtime::shield::runtime::current_scope);
    const DWORD after=GetLastError();
    const int out=g_fairy.Leave(host,entry,result);
    // Every life-down on Link with its caller: a live check attributes damage (which attack landed after a parry)
    // instead of guessing from lock or HUD side effects. LifeDownSet2 also takes enemies' damage (em10DmCk, the
    // riposte): only Link's own entries are logged.
    if(entry.player)Log("life_down ret=0x%X damage=%d rnd=%d flag=0x%X hp=%d out=%d em=0x%08X player=1",
        ret_rva,damage,rnd,flag,result,out,reinterpret_cast<uint32_t>(em));
    SetLastError(after);
    return out;
}
static bool StartFairyRevive(unsigned char* base,uint32_t player_slot,uint32_t global_slot){
    const uint32_t address=reinterpret_cast<uint32_t>(base);
    if(player_slot!=address+fairy::rva::kPlayerSlot||global_slot!=address+fairy::rva::kGlobalSlot){
        Log("FairyRevive disabled: player/global slots 0x%X/0x%X are not the pinned RVAs",player_slot,global_slot);return false;
    }
    if(!PinsAt(address,link_runtime::native_pins::kFairyRevive)){Log("FairyRevive disabled: native pins (LifeDownSet2/EmAtk*/cItemMgr::use) at base 0x%X",address);return false;}
    g_fairy_base=address;g_fairy.Reset(address);
    void* target=base+fairy::rva::kLifeDownBody;
    MH_STATUS status=MH_Initialize();
    if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){Log("FairyRevive disabled: MinHook init %s",MH_StatusToString(status));return false;}
    status=MH_CreateHook(target,reinterpret_cast<void*>(&FairyLifeDownDetour),reinterpret_cast<void**>(&g_fairy_original));
    if(status!=MH_OK){Log("FairyRevive disabled: MinHook create %s",MH_StatusToString(status));return false;}
    g_fairy_on.store(true,std::memory_order_release);
    status=MH_EnableHook(target);
    if(status!=MH_OK){
        g_fairy_on.store(false,std::memory_order_release);MH_RemoveHook(target);g_fairy_original=nullptr;
        Log("FairyRevive disabled: MinHook enable %s",MH_StatusToString(status));return false;
    }
    Log("FairyRevive enabled: base=0x%X LifeDownSet2 0x%X detour; item 0x%02X; native death for damage>=%d, ATK flag 8, return 0x%X; grace=%.0f frames60",
        address,fairy::rva::kLifeDownBody,fairy::kFairyItem,fairy::kInstantKill,fairy::rva::kZeroingReturn,fairy::kGraceFrames60);
    return true;
}
static void PollFairy(AlertVoice* sound){
    unsigned r=g_fairy_read.load(std::memory_order_relaxed);
    const unsigned w=g_fairy_write.load(std::memory_order_acquire);
    while(r!=w){
        const fairy::Event e=g_fairy_events[r%16];
        char gate[48]="";   // verdict=gates names the fairy gate that refused and the bits that blocked
        if(e.verdict==fairy::Verdict::Gates)sprintf_s(gate," gate=%s bits=0x%X",fairy::Name(e.gate),e.gate_bits);
        Log("FairyRevive verdict=%s%s hp=%d->%d/%d damage=%d return_rva=0x%X atk_flag=0x%X tick=%u item=0x%X num=%u->%u to_whom=%u consume_verified=%u",
            fairy::Name(e.verdict),gate,e.hp_before,e.hp_after,e.max_hp,e.damage,e.return_rva,e.atk_flag,e.tick,e.item,
            e.num_before,e.num_after,e.to_whom,e.consume_verified?1U:0U);
        if(e.verdict==fairy::Verdict::Revived&&sound)Log("FairyRevive sound submitted=%u",sound->Play()?1U:0U);
        g_fairy_read.store(++r,std::memory_order_release);
    }
    const unsigned dropped=g_fairy_dropped.exchange(0);
    if(dropped)Log("FairyRevive dropped=%u records",dropped);
}
// ---------------------------------------------------------------------------------------------
// Boss music duck (boss_music_duck.h, docs/dll-music-eventclock-01-2026-10-18.md). [LinkRuntime]
// BossMusicDuck=1 and/or CombatMusicDuck=1 (Zelda combat streams, same claims). MinHook on SndWatcher's entry; the detour runs the native watcher, then one
// Ducker tick on the game thread (pSnd slot read + SndRoomBgmVolSet). The worker only logs.
namespace bmd=link_runtime::boss_music;
using BmdWatcher=void(__cdecl*)();
using BmdVolSet=char(__cdecl*)(uint32_t,int,int);
static BmdWatcher g_bmd_original=nullptr;
static uint32_t g_bmd_base=0;
static std::atomic<bool> g_bmd_on{false};
static bmd::Event g_bmd_events[32];
static std::atomic<unsigned> g_bmd_write{0},g_bmd_read{0},g_bmd_dropped{0},g_bmd_bad_reads{0};
struct BmdHost {
    bool Read(bmd::Snapshot& out){
        uint32_t snd=0;unsigned char raw[bmd::off::kSlotBytes]{};
        if(!OcaCopy(g_bmd_base+bmd::rva::kSndSlot,reinterpret_cast<unsigned char*>(&snd),4)||!snd||
           !OcaCopy(snd+bmd::off::kSlots,raw,sizeof(raw))){g_bmd_bad_reads.fetch_add(1,std::memory_order_relaxed);return false;}
        out=bmd::Decode(raw);return true;
    }
    uint64_t NowMs(){return GetTickCount64();}
    bool VolSet(unsigned slot,int vol,int time){
        __try{return reinterpret_cast<BmdVolSet>(g_bmd_base+bmd::rva::kBgmVolSet)(slot,vol,time)!=0;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    void Note(const bmd::Event& e){
        const unsigned w=g_bmd_write.load(std::memory_order_relaxed),r=g_bmd_read.load(std::memory_order_acquire);
        if(w-r==32){g_bmd_dropped.fetch_add(1);return;}
        g_bmd_events[w%32]=e;g_bmd_write.store(w+1,std::memory_order_release);
    }
};
static bmd::Ducker g_bmd;
static void __cdecl BmdWatcherDetour(){
    g_bmd_original();
    if(!g_bmd_on.load(std::memory_order_acquire))return;
    const DWORD error=GetLastError();
    BmdHost host;g_bmd.Tick(host);
    SetLastError(error);
}
static bool StartBossMusicDuck(uint32_t base,bool boss,bool combat){
    if(!PinsAt(base,link_runtime::native_pins::kBossMusicDuck)){Log("%s disabled: native pins (SndWatcher/SndRoomBgmVolSet/SndRoomStrStop) at base 0x%X",boss?"BossMusicDuck":"CombatMusicDuck",base);return false;}
    g_bmd_base=base;
    bmd::Config cfg;cfg.boss=boss;cfg.combat=combat;g_bmd.Configure(cfg);
    void* target=reinterpret_cast<void*>(base+bmd::rva::kWatcher);
    MH_STATUS status=MH_Initialize();
    if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){Log("BossMusicDuck disabled: MinHook init %s",MH_StatusToString(status));return false;}
    status=MH_CreateHook(target,reinterpret_cast<void*>(&BmdWatcherDetour),reinterpret_cast<void**>(&g_bmd_original));
    if(status!=MH_OK){Log("BossMusicDuck disabled: MinHook create %s",MH_StatusToString(status));return false;}
    g_bmd_on.store(true,std::memory_order_release);
    status=MH_EnableHook(target);
    if(status!=MH_OK){
        g_bmd_on.store(false,std::memory_order_release);MH_RemoveHook(target);g_bmd_original=nullptr;
        Log("BossMusicDuck disabled: MinHook enable %s",MH_StatusToString(status));return false;
    }
    if(boss)Log("BossMusicDuck enabled: base=0x%X SndWatcher 0x%X detour; SndRoomBgmVolSet 0x%X; boss cues 04 05 06 23 26; duck vol=%d time=%d, restore def_vol time=%d (60/s), quiet<=%d",
        base,bmd::rva::kWatcher,bmd::rva::kBgmVolSet,bmd::kDuckVolume,bmd::kDuckTime,bmd::kRestoreTime,bmd::kQuiet);
    else Log("BossMusicDuck=0; boss streams do not duck");
    if(combat)Log("CombatMusicDuck enabled: base=0x%X SndWatcher 0x%X detour; %u tp-zone-music-02 combat cues + village 0x%02X; duck vol=%d time=%d, restore def_vol time=%d (60/s), quiet<=%d",
        base,bmd::rva::kWatcher,static_cast<unsigned>(sizeof(bmd::kCombatCues)),bmd::kVillageCombatCue,bmd::kDuckVolume,bmd::kDuckTime,bmd::kRestoreTime,bmd::kQuiet);
    return true;
}
// ---------------------------------------------------------------------------------------------
// Event sheath (event_sheath.h, docs/dll-sheath-events-01-2026-10-18.md). [LinkRuntime] EventSheathHide=1
// only. MinHook on Event::ControlTransFlag; the detour runs the native one, then one Step for that Event on
// the game thread (hides the event Link's "pl000d" back sword while "evm7700" is drawn). The worker only logs.
namespace esh=link_runtime::event_sheath;
using EshControlTrans=void(__thiscall*)(void*);
static EshControlTrans g_esh_original=nullptr;
static std::atomic<bool> g_esh_on{false};
static esh::Owners g_esh_owners;
struct EshRecord{esh::Result r;char name[33];};
static EshRecord g_esh_events[32];
static std::atomic<unsigned> g_esh_write{0},g_esh_read{0},g_esh_dropped{0};
static uint32_t g_esh_last_event=0;static esh::Kind g_esh_last_kind=esh::Kind::None;
struct EshMemory{
    bool Read32(uint32_t a,uint32_t& v){return a&&OcaCopy(a,reinterpret_cast<unsigned char*>(&v),4);}
    bool Read(uint32_t a,void* out,size_t n){return a&&OcaCopy(a,static_cast<unsigned char*>(out),n);}
    bool Compare32(uint32_t a,uint32_t expected,uint32_t desired){LocalMemory m;return m.Compare32(a,expected,desired);}
};
static void EshNote(const esh::Result& r){
    const unsigned w=g_esh_write.load(std::memory_order_relaxed),rd=g_esh_read.load(std::memory_order_acquire);
    if(w-rd==32){g_esh_dropped.fetch_add(1);return;}
    EshRecord& slot=g_esh_events[w%32];slot.r=r;std::memset(slot.name,0,sizeof(slot.name));
    OcaCopy(r.event+esh::layout::kEventName,reinterpret_cast<unsigned char*>(slot.name),32);slot.name[32]=0;
    for(char& c:slot.name)if(c&&(c<0x20||c>0x7E))c='?';
    g_esh_write.store(w+1,std::memory_order_release);
}
static void __fastcall EshControlTransDetour(void* self,void*){
    g_esh_original(self);
    if(!g_esh_on.load(std::memory_order_acquire))return;
    const DWORD error=GetLastError();
    EshMemory m;const esh::Result r=esh::Step(m,reinterpret_cast<uint32_t>(self),g_esh_owners);
    // Hide/Restore/Drop always; repeated diagnostics (foreign, not_on_body, invalid, race) once per change.
    if(r.kind!=esh::Kind::None&&(r.kind==esh::Kind::Hide||r.kind==esh::Kind::Restore||r.kind==esh::Kind::Drop||
       r.kind!=g_esh_last_kind||r.event!=g_esh_last_event))EshNote(r);
    if(r.kind!=esh::Kind::None){g_esh_last_kind=r.kind;g_esh_last_event=r.event;}
    SetLastError(error);
}
static bool StartEventSheath(uint32_t base){
    if(!PinsAt(base,link_runtime::native_pins::kEventSheath)){Log("EventSheathHide disabled: native pins (Event::ControlTransFlag/DatTbl/SetParts/draw bits) at base 0x%X",base);return false;}
    void* target=reinterpret_cast<void*>(base+esh::layout::kControlTransRva);
    MH_STATUS status=MH_Initialize();
    if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){Log("EventSheathHide disabled: MinHook init %s",MH_StatusToString(status));return false;}
    status=MH_CreateHook(target,reinterpret_cast<void*>(&EshControlTransDetour),reinterpret_cast<void**>(&g_esh_original));
    if(status!=MH_OK){Log("EventSheathHide disabled: MinHook create %s",MH_StatusToString(status));return false;}
    g_esh_on.store(true,std::memory_order_release);
    status=MH_EnableHook(target);
    if(status!=MH_OK){
        g_esh_on.store(false,std::memory_order_release);MH_RemoveHook(target);g_esh_original=nullptr;
        Log("EventSheathHide disabled: MinHook enable %s",MH_StatusToString(status));return false;
    }
    Log("EventSheathHide enabled: base=0x%X Event::ControlTransFlag 0x%X detour; hides event part \"%s\" (pl00_005, sheathed sword) on PPl while \"%s\" is drawn",
        base,esh::layout::kControlTransRva,esh::kSheath,esh::kKnife);
    return true;
}
static void PollEventSheath(){
    unsigned r=g_esh_read.load(std::memory_order_relaxed);
    const unsigned w=g_esh_write.load(std::memory_order_acquire);
    while(r!=w){
        const EshRecord& e=g_esh_events[r%32];
        Log("event_sheath %s event=%s evt=0x%X part=0x%X flags=0x%X->0x%X knife=0x%X knife_flags=0x%X owners=%u",
            esh::Name(e.r.kind),e.name,e.r.event,e.r.part,e.r.before,e.r.after,e.r.knife,e.r.knife_flags,g_esh_owners.Count());
        g_esh_read.store(++r,std::memory_order_release);
    }
    const unsigned dropped=g_esh_dropped.exchange(0);
    if(dropped)Log("event_sheath dropped=%u records",dropped);
}
static void PollBossMusicDuck(){
    unsigned r=g_bmd_read.load(std::memory_order_relaxed);
    const unsigned w=g_bmd_write.load(std::memory_order_acquire);
    while(r!=w){
        const bmd::Event e=g_bmd_events[r%32];
        const char* tag=e.source==bmd::Source::Combat?"combat_music":"boss_music";
        if(e.kind==bmd::Kind::BossOn||e.kind==bmd::Kind::BossOff||e.kind==bmd::Kind::CombatOn||e.kind==bmd::Kind::CombatOff)
            Log("%s %s cue=0x%02X str_slot=%u play_id=0x%X",tag,bmd::Name(e.kind),static_cast<unsigned>(e.cue&0xFF),e.slot,e.play_id);
        else Log("%s %s bgm_slot=%u cue=0x%02X vol=%d->%d play_id=0x%X sent=%u",tag,bmd::Name(e.kind),e.slot,
            static_cast<unsigned>(e.cue&0xFF),e.vol_before,e.volume,e.play_id,e.sent?1U:0U);
        g_bmd_read.store(++r,std::memory_order_release);
    }
    const unsigned dropped=g_bmd_dropped.exchange(0);
    if(dropped)Log("boss_music dropped=%u records",dropped);
    static unsigned bad_logged=0;
    const unsigned bad=g_bmd_bad_reads.load(std::memory_order_relaxed);
    if(bad!=bad_logged&&(bad_logged==0||bad-bad_logged>=600)){LogV("boss_music unreadable pSnd ticks=%u",bad);bad_logged=bad;}
}
// ---------------------------------------------------------------------------------------------
// Rupee colour by value (rupee_colors.h, docs/rupees-colors-2026-10-12.md). [Rupees] Enabled=1 only.
// The two generic calls to ItemGetBinTplAddr that carry a pickup record (SceAtCreateItemAt 0x2BA110:
// drops, barrels, saved respawns; room item set 0x2BAF3A) get their rel32 pointed at a stub that
// remembers the record and jumps to the detour (same cdecl arguments, same return address). The
// detour asks for the colour's alias slot first and falls back to the native lookup of the id.
// Game thread only. A room ITM without the alias slots keeps today's green/blue/red by id.
namespace rup=link_runtime::rupees;
using RupLookup=int(__cdecl*)(uint32_t,void**,void**);
static RupLookup g_rup_lookup=nullptr;                  // base + rva::kLookupThunk
static const volatile uint16_t* g_rup_rec=nullptr;      // &item_id; item_num at +4 bytes
static volatile LONG g_rup_colour=0;                    // alias lookups served (log)
static int __cdecl RupLookupDetour(uint32_t id,void** bin,void** tpl){
    const volatile uint16_t* rec=g_rup_rec;g_rup_rec=nullptr;
    uint16_t rec_id=0,rec_num=0;bool have=false;
    if(rec){__try{rec_id=rec[0];rec_num=rec[2];have=true;}__except(EXCEPTION_EXECUTE_HANDLER){have=false;}}
    const uint8_t slot=have&&static_cast<uint8_t>(rec_id)==static_cast<uint8_t>(id)?rup::SlotFor(rec_id,rec_num):static_cast<uint8_t>(id);
    const int out=rup::Lookup(g_rup_lookup,id,have,rec_id,rec_num,bin,tpl);
    if(slot!=static_cast<uint8_t>(id))InterlockedIncrement(&g_rup_colour);
    return out;
}
// Stubs: eax is caller-saved and free at both sites (the call's own result register).
static __declspec(naked) void RupCreateStub(){__asm{
    lea eax,[edi+0x78]
    mov g_rup_rec,eax
    jmp RupLookupDetour}}
static __declspec(naked) void RupRoomStub(){__asm{
    lea eax,[esi+0x1C]
    mov g_rup_rec,eax
    jmp RupLookupDetour}}
static bool StartRupees(uint32_t base){
    if(!PinsAt(base,link_runtime::native_pins::kRupeeColours)){Log("Rupees disabled: native pins (ItemGetBinTplAddr/SceAt calls) at base 0x%X",base);return false;}
    g_rup_lookup=reinterpret_cast<RupLookup>(base+rup::rva::kLookupThunk);
    const struct {uint32_t at;void* to;} sites[]={
        {rup::rva::kCreateCall,reinterpret_cast<void*>(&RupCreateStub)},{rup::rva::kRoomCall,reinterpret_cast<void*>(&RupRoomStub)}};
    DWORD olds[2]{};
    for(unsigned i=0;i<2;++i){   // both writable first: all or nothing
        if(!VirtualProtect(reinterpret_cast<void*>(base+sites[i].at),5,PAGE_EXECUTE_READWRITE,&olds[i])){
            if(i){DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(base+sites[0].at),5,olds[0],&ignored);}
            Log("Rupees disabled: protect");return false;
        }
    }
    // Write both before restoring any protection: the two sites can share a page, and restoring the first
    // site's old protection would make the second write fault (12-oct crash at startup).
    for(unsigned i=0;i<2;++i){
        auto* site=reinterpret_cast<unsigned char*>(base+sites[i].at);
        const int32_t rel=static_cast<int32_t>(reinterpret_cast<uint32_t>(sites[i].to)-(base+sites[i].at+5));
        std::memcpy(site+1,&rel,4);
        FlushInstructionCache(GetCurrentProcess(),site,5);
    }
    for(unsigned i=2;i-->0;){DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(base+sites[i].at),5,olds[i],&ignored);}
    Log("Rupees enabled: base=0x%X calls 0x%X and 0x%X -> colour slots (0x78,0x79,0xA0,0x71,0xAC,0xAD,0xFC by value)",
        base,rup::rva::kCreateCall,rup::rva::kRoomCall);
    return true;
}
static void PollRupees(){
    static LONG logged=0;
    const LONG n=InterlockedCompareExchange(&g_rup_colour,0,0);
    if(n!=logged&&(n<4||n-logged>=16)){Log("Rupees colour lookups=%ld",n);logged=n;}
}
// World freeze by Stop_flg SPF_EM|SPF_SUBCHAR (ocarina_freeze.h): ReadHealth ignores those bits,
// so the gates need no masking. Released from the session AND from the dispatch lease end.
static oca::FreezeOwner g_oca_freeze;
static oca::HandMount g_oca_mount;
static oca::WeaponHide g_oca_weapon;   // equipped weapon hidden while the pose owns Link (ocarina_weapon.h)
static bool g_oca_weapon_ok=false,g_oca_weapon_pending=false;
static ocr::ClipChain g_oca_chain;
static oca::NoteInput g_oca_keys;
static mortal::dispatch::Runtime* g_oca_run=nullptr;
static ocr::LeaseRoute g_oca_route;   // armed before TryStartClip: Started is published inside Start (review I1)
static ocr::KnifeLatch g_oca_knife;
static bool g_oca_native=false,g_oca_alt_knife_logged=false;
static uint32_t g_oca_session_player=0;
static bool g_oca_unmount_pending=false,g_oca_lease_lost=false,g_oca_stopping=false,g_oca_nopose_done=false;
static bool OcaOwnsLease(){return g_oca_run&&g_oca_run->active&&g_oca_route.Token()&&g_oca_run->lease.token==g_oca_route.Token();}
static uint32_t OcaRead(uint32_t address){uint32_t v=0;LocalMemory m;return m.Read32(address,v)?v:0;}
static uint32_t OcaKnifeInfo(uint32_t player){
    if(!link_runtime::IsPointer(player))return 0;
    const uint32_t body=OcaRead(player+oca::mount_layout::kBody);
    return link_runtime::IsPointer(body)?OcaRead(body+oca::mount_layout::kKnifeInfo):0;
}
struct OcaHost;
static oca::Session<OcaHost> g_oca_session;
// Ocarina phase 2 (ocarina_songs.h, save_anywhere.h): [Ocarina] Serenity=1. The song flow runs on
// the game thread next to the session; the jingle plays on the worker; CardSave runs at the very
// end of CombinedDecide (the last thing of that game frame, like the typewriter's ActBtn.move).
namespace osg=link_runtime::ocarina::songs;
namespace sav=link_runtime::save_anywhere;
static osg::Config g_oca_song_cfg;
static bool g_oca_save_ok=false,g_oca_save_request=false;
static std::atomic<bool> g_oca_jingle{false};
// Combat songs (ocarina_song_effects.h, 17-oct): [Ocarina] Sheik=1 / Epona=1. The effect publishes native flash
// or kick records on the game frame the flow runs it (OcarinaDecide); the worker plays its sound.
namespace ose=link_runtime::ocarina::song_effects;
static bool g_oca_strike_ok=false;                 // publisher pins (native_pins.h kOcarinaSongStrike)
static std::atomic<int> g_oca_effect_sound{0};     // 1 flash (Sheik), 2 howl (Epona); taken by the worker
// Sheik's Theme replay (link_ocarina_sheik_theme.wav, the ROM's own music): 1 play, 2 fade out; taken by the worker.
static std::atomic<int> g_oca_theme{0};
static bool g_oca_theme_ok=false;                 // the recording loaded (else the note replay)
// OcarinaSongPlayback (18-oct): the whole song (link_ocarina_play_<song>.wav, backing + ocarina lead) as the replay.
// 1..3 play Serenity/Sheik/Epona, 4 fade all out; taken by the worker.
static std::atomic<int> g_oca_play{0};
static bool g_oca_play_ok[3]={};                  // that song's recording loaded (else the 17-oct replay)
static int g_oca_play_hp=-1;                      // Link's HP when the song matched (damage cuts the playback)
static bool g_oca_song_was_busy=false;
static bool OcaSongStrike(osg::Song song);
struct OcaSongHost {
    bool SongAllowed(osg::Song,const char*& why);
    void PlayJingle(){g_oca_jingle.store(true,std::memory_order_release);}
    bool RunEffect(osg::Song song){if(song!=osg::Song::Serenity)return OcaSongStrike(song);g_oca_save_request=true;return true;}
    void SongLog(const char* text,int code){OcaRecordPush(9,text,code,0,0,g_oca_session.token());}
    // Replay (ocarina_staff.h): the melody as written, through the note mailbox (same voices and fades).
    void ReplayNote(uint64_t token,int8_t note){const bool pushed=g_oca_cfg.notes&&g_oca_notes.Push(token,note);OcaRecordPush(9,"replay note",note,pushed?1:0,0,token);}
    // Sheik: the real Sheik's Theme instead of the note WAVs (false: no recording, the flow replays the notes).
    bool ReplayAudio(uint64_t token,osg::Song song){
        const int i=static_cast<int>(song);
        if(g_oca_song_cfg.playback&&i>=0&&i<3&&g_oca_play_ok[i]){
            g_oca_play.store(1+i,std::memory_order_release);OcaRecordPush(9,"playback audio",i,0,0,token);return true;}
        if(song!=osg::Song::Sheik||!g_oca_theme_ok)return false;
        g_oca_theme.store(1,std::memory_order_release);OcaRecordPush(9,"replay audio",static_cast<int>(song),0,0,token);return true;
    }
    // A cut playback (ReplayFlow::Cancel): silence it now, not after the exit clip.
    void StopReplay(uint64_t token){
        g_oca_play.store(4,std::memory_order_release);g_oca_theme.store(2,std::memory_order_release);OcaRecordPush(9,"playback stop",0,0,0,token);}
};
static OcaSongHost g_oca_song_host;
// OoT staff and song replay (ocarina_staff.h, docs/ocarina-staff-2026-10-12.md): [Ocarina] Staff=1 draws the
// staff through the native IdSys; the flow (match -> replay -> chime in hand -> stow -> save) replaces songs::Flow.
namespace ostf=link_runtime::ocarina::staff;
using OcaSongFlow=ostf::ReplayFlow<OcaSongHost,osg::Melody,osg::Song>;
static OcaSongFlow g_oca_song;
static bool g_oca_staff_on=false;
static unsigned char* g_oca_staff_block=nullptr;
static const OcaFile kOcaStaffBlock={L"link_ocarina_staff.eff",672,"886af2cd6cdcc5b446f4692837d3cebab8fefc8028a568b0729ae03c21874385"};
// Chromatic staff (29-oct, scripts/ocarina_staff.py chromatic): the same 6 frames plus 22 chromatic note frames appended
// to 07000000 at 369..390. With the 6-frame block the staff still works and only the chromatic notes stay hidden.
static const OcaFile kOcaStaffBlockChroma={L"link_ocarina_staff.eff",1792,"0c8cb9da7bf0d666ea50dab2974f8cd216eec28af4d946654994d011b7ecded9"};
struct OcaStaffNative {   // IdSys 0x859788: set 0x4449, kill 0xCE9B, unitPtr 0x95F7; IdTexDataLoad 0x1F46, IdTexRelease 0xBF50
    uint32_t base=g_oca_base;
    bool Read(uint32_t a,void* out,size_t n){return a&&OcaCopy(a,static_cast<unsigned char*>(out),n);}
    bool Write(uint32_t a,const void* in,size_t n){__try{std::memcpy(reinterpret_cast<void*>(a),in,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
    bool TexLoad(void* block){__try{return reinterpret_cast<int(__cdecl*)(void*,int)>(base+ostf::layout::tex_load_thunk_rva)(block,int(ostf::layout::tex_owner))!=0;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
    void TexRelease(){__try{reinterpret_cast<void(__cdecl*)(int)>(base+ostf::layout::tex_release_thunk_rva)(int(ostf::layout::tex_owner));}__except(EXCEPTION_EXECUTE_HANDLER){}}
    void Set(const void* table){__try{reinterpret_cast<void(__thiscall*)(void*,const void*,uint8_t,int,uint8_t,uint8_t,uint32_t)>(base+ostf::layout::set_thunk_rva)(
        reinterpret_cast<void*>(base+ostf::layout::idsys_rva),table,0xFF,ostf::layout::id_class,ostf::layout::ot_type,ostf::layout::ot_no,0);}__except(EXCEPTION_EXECUTE_HANDLER){}}
    void Kill(){__try{reinterpret_cast<void(__thiscall*)(void*,uint8_t,int)>(base+ostf::layout::kill_thunk_rva)(reinterpret_cast<void*>(base+ostf::layout::idsys_rva),0xFF,ostf::layout::id_class);}__except(EXCEPTION_EXECUTE_HANDLER){}}
    uint32_t UnitPtr(uint8_t mark){__try{return reinterpret_cast<uint32_t(__thiscall*)(void*,uint8_t,int)>(base+ostf::layout::unit_ptr_thunk_rva)(reinterpret_cast<void*>(base+ostf::layout::idsys_rva),mark,ostf::layout::id_class);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
};
static ostf::Painter<OcaStaffNative> g_oca_painter;
static ostf::PaintWhy g_oca_paint_last=ostf::PaintWhy::Idle;
static float g_oca_paint_y=0;
static void OcaStaffPaint(bool show){
    if(!g_oca_staff_on)return;
    OcaStaffNative n;const ostf::PaintWhy why=g_oca_painter.Tick(n,g_oca_song.staff(),show);
    // Where Fit put the staff: crop c from the backbuffer W x H, or from the post matrix (fallback).
    char text[40];const auto& g=g_oca_painter.geometry();const auto& p=g_oca_painter;
    const auto fit=[&](const char* what){
        if(p.from_screen())sprintf_s(text,"%s y=%.1f %dx%d c=%.3f",what,g.staff_y,static_cast<int>(p.screen_w()),static_cast<int>(p.screen_h()),p.post_y());
        else sprintf_s(text,"%s y=%.1f post=%.3f",what,g.staff_y,p.post_y());};
    if(why!=g_oca_paint_last&&why!=ostf::PaintWhy::Shown){
        if(why==ostf::PaintWhy::Armed)fit("armed");
        else strncpy_s(text,ostf::PaintWhyName(why),_TRUNCATE);
        OcaRecordPush(10,text,static_cast<int>(why),static_cast<unsigned>(g_oca_song.staff().mode()),g_oca_song.staff().count(),g_oca_session.token());}
    if(why==ostf::PaintWhy::Armed)g_oca_paint_y=g.staff_y;
    else if(why==ostf::PaintWhy::Shown&&g.staff_y!=g_oca_paint_y){   // the crop changed after arming
        g_oca_paint_y=g.staff_y;fit("refit");
        OcaRecordPush(10,text,static_cast<int>(why),static_cast<unsigned>(g_oca_song.staff().mode()),g_oca_song.staff().count(),g_oca_session.token());}
    g_oca_paint_last=why;
}
struct OcaHost {
    bool Foreground(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return g_oca_focus.Take(pid==GetCurrentProcessId());}
    // Pending release work, every frame on either handler path (ocr::Frame).
    void Service(){
        LocalMemory m;
        LogFreezeEnd(g_oca_freeze.Service(m,g_oca_epoch.Current()));
        if(g_oca_unmount_pending)Unmount();
        if(g_oca_weapon_pending)ShowWeapon();
        else if(g_oca_weapon.Held())g_oca_weapon.Hold(m,g_oca_player_slot);
    }
    void SetNativeFrame(bool native){g_oca_native=native;}
    bool GatesOk(){
        if(g_oca_native)return false;      // a native event, door or cutscene owns the player this frame
        const uint32_t player=OcaRead(g_oca_player_slot);
        if(!link_runtime::IsPointer(player))return false;
        if(g_oca_session.state()==oca::State::Idle){
            LocalMemory m;link_runtime::HealthState health;unsigned costume=0;
            if(!link_runtime::ReadHealth(m,g_oca_player_slot,g_oca_global_slot,health)||!health.eligible||health.player!=player)return false;
            if(!link_runtime::ReadSwordCostume(m,g_oca_player_slot,g_oca_global_slot,costume))return false;
            if(g_oca_cfg.mount&&ocr::CostumeAsset(costume)<0)return false;
            if(g_oca_run&&g_oca_run->active)return false;              // Mortal Draw (or anything) owns the body
            if(g_oca_freeze.Active()||g_oca_unmount_pending||g_oca_weapon_pending)return false;
            const uint32_t routine=OcaRead(player+0xFC);
            return (routine&0xFF)==0&&(((routine>>8)&0xFF)==0||((routine>>8)&0xFF)==0x0B);  // standing or knife ready
        }
        if(g_oca_session_player&&player!=g_oca_session_player)return false;
        LocalMemory m;link_runtime::HealthState health;
        if(!link_runtime::ReadHealth(m,g_oca_player_slot,g_oca_global_slot,health)||!health.eligible)return false;
        return !(g_oca_cfg.pose&&g_oca_lease_lost);
    }
    // Ready = knife in hand (bit 8 of m_pKnife) or, with Pose=1, outside steady
    // standing idle. Share dispatch's qualified idle variants, including native
    // pose-end motion continuity, so waiting and starting cannot disagree.
    bool SwordReady(){
        const uint32_t player=OcaRead(g_oca_player_slot);const uint32_t info=OcaKnifeInfo(player);
        const bool knife=link_runtime::IsPointer(info)&&(OcaRead(info+oca::mount_layout::kFlags)&oca::mount_layout::kVisible);
        return knife||(g_oca_cfg.pose&&!mortal::dispatch::NormalIdleRoutine(OcaRead(player+0xFC)));
    }
    void RequestSheathe(){g_oca_knife.Hold();g_oca_alt_knife_logged=false;OcaRecordPush(1,"sheathe requested",0,0,0,g_oca_session.token());}
    bool FreezeBegin(){
        g_oca_knife.Hold();g_oca_session_player=OcaRead(g_oca_player_slot);
        if(!g_oca_cfg.freeze)return true;
        LocalMemory m;const auto r=g_oca_freeze.Begin(m,g_oca_player_slot,g_oca_global_slot,g_oca_epoch.Current());
        // a = refusal code, b = Stop_flg bits taken (SPF_EM 0x20000000 | SPF_SUBCHAR 0x1000).
        OcaRecordPush(2,"freeze begin",static_cast<int>(r),static_cast<unsigned>(g_oca_freeze.Freeze().RefusedAt()),g_oca_freeze.Freeze().Owned());
        return r==oca::FreezeResult::Frozen;
    }
    // Idempotent: the lease end, the session release and the per-frame retry all call it.
    void FreezeEnd(){LocalMemory m;LogFreezeEnd(g_oca_freeze.Release(m,g_oca_epoch.Current()));}
    static void LogFreezeEnd(oca::FreezeResult r){if(oca::FreezeOwner::Finished(r))OcaRecordPush(2,"freeze end",static_cast<int>(r));}
    // Session step "mount": the ocarina in the knife slot (Mount=1), then the equipped weapon hidden
    // (Pose=1). The weapon is hidden only after everything that can refuse succeeded, so every
    // session that hid it reaches Unmount, on every release path.
    bool Mount(){
        if(g_oca_cfg.mount&&!MountOcarina())return false;
        HideWeapon();
        return true;
    }
    void HideWeapon(){
        if(!g_oca_cfg.pose||!g_oca_weapon_ok)return;
        LocalMemory m;const auto r=g_oca_weapon.Hide(m,g_oca_player_slot);
        OcaRecordPush(8,"weapon hide",static_cast<int>(r),static_cast<unsigned>(g_oca_weapon.Count()),0,g_oca_session.token());
    }
    void ShowWeapon(){
        if(!g_oca_weapon.Held()){g_oca_weapon_pending=false;return;}
        LocalMemory m;const unsigned reasserts=g_oca_weapon.Reasserts();const auto r=g_oca_weapon.End(m,g_oca_player_slot);
        g_oca_weapon_pending=r==oca::WeaponResult::Pending;
        if(!g_oca_weapon_pending)OcaRecordPush(8,"weapon show",static_cast<int>(r),reasserts);
    }
    bool MountOcarina(){
        LocalMemory m;unsigned costume=0;
        if(!link_runtime::ReadSwordCostume(m,g_oca_player_slot,g_oca_global_slot,costume))return false;
        const int index=ocr::CostumeAsset(costume);if(index<0)return false;
        if(!g_oca_asset_tried[index]){
            g_oca_asset_tried[index]=true;OcaNative native;
            g_oca_asset[index]=oca::PrepareAsset(m,native,reinterpret_cast<uint32_t>(g_oca_bin),static_cast<uint32_t>(kOcaBin.size),reinterpret_cast<uint32_t>(g_oca_tpl[index]));
            OcaRecordPush(3,"asset prepared",g_oca_asset[index].ready?1:0,costume);
        }
        if(!g_oca_asset[index].ready)return false;
        // Published first (review M2): the worker must not run SwordStep between bit 8 and the store.
        g_oca_mounted_bin.store(g_oca_asset[index].bin,std::memory_order_release);
        const auto r=g_oca_mount.Mount(m,g_oca_player_slot,g_oca_asset[index]);
        OcaRecordPush(3,"mount",static_cast<int>(r),costume);
        if(r!=oca::MountResult::Mounted){g_oca_mounted_bin.store(0,std::memory_order_release);return false;}
        return true;
    }
    void Unmount(){
        ShowWeapon();
        if(!g_oca_mount.Mounted())return;
        LocalMemory m;const auto r=g_oca_mount.Unmount(m,g_oca_player_slot);
        g_oca_unmount_pending=r==oca::MountResult::RaceLost||r==oca::MountResult::InvalidMemory;
        if(!g_oca_unmount_pending){g_oca_mounted_bin.store(0,std::memory_order_release);OcaRecordPush(3,"unmount",static_cast<int>(r));}
    }
    bool StartClip(int index){
        if(!g_oca_cfg.pose){g_oca_nopose_done=index!=1;OcaRecordPush(4,"clip skipped (Pose=0)",index);return true;}
        if(!g_oca_run)return false;
        if(index==0){
            const mortal::dispatch::Lease lease{mortal::resident_assets::ReserveToken(),g_oca_clip[0],g_oca_clip[1],g_oca_clip[1]+4};
            const uint32_t routine=OcaRead(OcaRead(g_oca_player_slot)+0xFC);
            g_oca_route.Arm(lease.token);g_oca_lease_lost=false;
            const auto result=mortal::trial::TryStartClip(*g_oca_run,g_mortal_native,lease);
            OcaRecordPush(4,"clip start",static_cast<int>(result.status),0,routine,lease.token);
            if(result.status!=mortal::trial::Status::Started){g_oca_route.Disarm();return false;}
            g_oca_chain.Started(ocr::Clip::Enter);return true;
        }
        if(index==1&&g_oca_chain.Current()==ocr::Clip::Loop){g_oca_chain.Started(ocr::Clip::Loop);return true;}
        const unsigned at=index==1?2:4;
        const bool ok=OcaOwnsLease()&&g_oca_run->Replace(g_oca_clip[at],g_oca_clip[at+1],g_oca_clip[at+1]+4);
        OcaRecordPush(4,"clip replace",ok?1:0,static_cast<unsigned>(index),0,g_oca_route.Token());
        if(ok)g_oca_chain.Started(index==1?ocr::Clip::Loop:ocr::Clip::Exit);
        return ok;
    }
    bool ClipFinished(){return g_oca_cfg.pose?g_oca_chain.Finished():g_oca_nopose_done;}
    void StopClip(){
        if(g_oca_cfg.pose&&OcaOwnsLease()){
            const uint64_t token=g_oca_route.Token();g_oca_stopping=true;const bool ok=g_oca_run->Cancel(token);g_oca_stopping=false;
            OcaRecordPush(4,"clip stop",ok?1:0,0,0,token);
        }
        g_oca_chain.Reset();g_oca_nopose_done=false;
    }
    void Ended(uint64_t token,oca::Reason reason){
        g_oca_notes.Close(token);g_oca_knife.Ended();
        if(g_oca_song.audio()){g_oca_theme.store(2,std::memory_order_release);g_oca_play.store(4,std::memory_order_release);}   // a cut replay silences it
        g_oca_session_player=0;g_oca_lease_lost=false;g_oca_chain.Reset();
        // b = last notes of the session (NoteHistory::Packed: note+1 per nibble, oldest lowest).
        OcaRecordPush(1,"ended",static_cast<int>(reason),g_oca_session.history().Size(),g_oca_session.history().Packed(),token);
        g_oca_song.OnEnded(token,reason==oca::Reason::Stowed?1:0,g_oca_song_host);
    }
    void PlayNote(uint64_t token,int8_t note){
        const bool pushed=g_oca_cfg.notes&&g_oca_notes.Push(token,note);
        OcaRecordPush(5,"note",note,pushed?1:0,0,token);
        g_oca_song.OnNote(token,note,g_oca_song_host);
    }
    void Log(const char* text){OcaRecordPush(1,text,static_cast<int>(g_oca_session.state()),0,0,g_oca_session.token());}
};
static OcaHost g_oca_host;
// Song rules: the save rules (save_anywhere.h) plus "the ocarina left nothing behind". While the session
// is still out (the match itself), Link is in the ocarina pose, so only the routine check is waived there:
// enemies, binoculars and tasks still refuse the song before it plays (PID 36944, em 0x51 at 5.3 m).
// Combat songs (Sheik, Epona): the ocarina's own gates, no enemy rule. Not while the ocarina left work
// behind, not while another lease (Mortal Draw, sword actions) owns Link's body, and the save rules'
// gameplay checks (no cutscene, event, menu, sub-screen, QTE/action lock, card, binoculars) with the
// routine relaxed to R0 == 0 (Link not hit, grabbed or in an action) once the ocarina is away.
static bool OcaCombatSongAllowed(const char*& why){
    if(!g_oca_strike_ok){why="song blocked: publisher pins";return false;}
    const bool out=g_oca_session.state()!=oca::State::Idle;
    if(!out&&(g_oca_freeze.Active()||g_oca_unmount_pending||g_oca_weapon_pending)){why="song blocked: ocarina release pending";return false;}
    if(g_oca_run&&g_oca_run->active&&!OcaOwnsLease()){why="song blocked: sword lease";return false;}
    ImageMem m;sav::Rules rules;rules.enemies=false;rules.aware=false;rules.waive_player=out;rules.player_r0_only=true;
    const sav::Result r=sav::Allowed(m,g_oca_base,rules);
    if(r.block==sav::Block::None)return true;
    why=sav::WhySong(r.block);return false;
}
bool OcaSongHost::SongAllowed(osg::Song song,const char*& why){
    if(song!=osg::Song::Serenity)return OcaCombatSongAllowed(why);
    if(!g_oca_save_ok){why="serenity blocked: save pins";return false;}
    const bool out=g_oca_session.state()!=oca::State::Idle;
    if(!out&&(g_oca_freeze.Active()||g_oca_unmount_pending||g_oca_weapon_pending)){why="serenity blocked: ocarina release pending";return false;}
    ImageMem m;sav::Rules rules;rules.waive_player=out;const sav::Result r=sav::Allowed(m,g_oca_base,rules);
    if(r.block==sav::Block::None)return true;
    why=sav::Why(r.block);
    if(r.enemy&&(r.block==sav::Block::EnemyNear||r.block==sav::Block::EnemyAware)){
        // Game thread only; SongLog copies the text into the record right away.
        static char blocked[64];
        // OcaRecord text holds 39 characters: "serenity blocked: enemy near em28" (distance is in "serenity enemy" b).
        std::snprintf(blocked,sizeof(blocked),"%s em%02x",why,static_cast<unsigned>(r.enemy_id));
        why=blocked;
    }
    // a = enemy work, b = distance in mm, code = em id (EnemyNear / EnemyAware).
    if(r.enemy)OcaRecordPush(9,"serenity enemy",r.enemy_id,r.enemy,static_cast<unsigned>(std::sqrt(r.distance2)),g_oca_session.token());
    return false;
}
// Sheik / Epona effect, game thread, gameplay frame (the flow ran SongAllowed this frame). The same native
// publisher as the Mortal Draw adapter (0x1AEEF0), called once per target, SEH-guarded.
static bool OcaPublish(const ose::dmg::PublishArgs& a){
    __try{ose::dmg::CallPublisher(g_oca_base+ose::dmg::layout::publisher_rva,a);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool OcaSongStrike(osg::Song song){
    const bool sheik=song==osg::Song::Sheik;
    if(!g_oca_strike_ok||(!sheik&&song!=osg::Song::Epona))return false;
    const ose::Blow blow=sheik?ose::Blow::Flash:ose::Blow::Charge;
    const float radius=static_cast<float>(sheik?g_oca_song_cfg.sheik_radius:g_oca_song_cfg.epona_radius);
    ImageMem m;const uint32_t player=OcaRead(g_oca_player_slot);float pos[3]{};
    if(!link_runtime::IsPointer(player)||!m.Read(player+0x94,pos,sizeof(pos))){OcaRecordPush(11,"song strike: no Link",0);return false;}
    const ose::Report r=ose::Run(m,g_oca_base,{pos[0],pos[1],pos[2]},blow,radius,[](const ose::dmg::PublishArgs& a){return OcaPublish(a);});
    g_oca_effect_sound.store(sheik?1:2,std::memory_order_release);   // the flash / howl plays even with no target
    // a = targets picked, b = enemy slots scanned; code = records published. Then one record per target:
    // code = id, a = actor, b = distance mm | type << 24, text = the shot's result.
    OcaRecordPush(11,sheik?"sheik flash":"epona charge",static_cast<int>(r.published),r.pick.count,r.pick.scanned,g_oca_session.token());
    for(unsigned i=0;i<r.pick.count;++i){
        const ose::Target& t=r.pick.items[i];char text[40];
        sprintf_s(text,"%s %s",ose::BlowName(blow),ose::ShotName(r.result[i]));
        OcaRecordPush(11,text,t.id,t.actor,static_cast<unsigned>(t.distance)|(static_cast<unsigned>(t.type)<<24),g_oca_session.token());
    }
    // Why the others were left out (only the reasons that occurred): code = Skip, a = count.
    for(unsigned s=1;s<static_cast<unsigned>(ose::Skip::Count);++s)
        if(r.pick.skips[s]&&s!=static_cast<unsigned>(ose::Skip::NotEm10)&&s!=static_cast<unsigned>(ose::Skip::Slot)){
            char text[40];sprintf_s(text,"%s skip %s",ose::BlowName(blow),ose::SkipName(static_cast<ose::Skip>(s)));
            OcaRecordPush(11,text,static_cast<int>(s),r.pick.skips[s]);
        }
    return r.shots[static_cast<unsigned>(ose::Shot::Failed)]==0;   // a faulted publisher is the only failure
}
// The dispatch reports the end of an owned ocarina clip: entry -> loop, loop -> loop again.
static bool __cdecl OcaProceed(mortal::dispatch::Runtime& run,unsigned char*){
    if(!g_oca_route.Token()||!run.active||run.lease.token!=g_oca_route.Token())return false;
    if(g_oca_chain.OnEnd()!=ocr::Clip::Loop)return false;
    return run.Replace(g_oca_clip[2],g_oca_clip[3],g_oca_clip[3]+4);
}
static void OcaLifecycle(mortal::dispatch::Event event,uint64_t token){
    if(event==mortal::dispatch::Event::Started)return;
    const bool exit_done=event==mortal::dispatch::Event::Finished&&g_oca_chain.Current()==ocr::Clip::Exit;
    g_oca_chain.Ended(exit_done);g_oca_route.Disarm();
    if(!exit_done&&!g_oca_stopping)g_oca_lease_lost=true;
    OcaRecordPush(7,"lease end",static_cast<int>(event),exit_done?1:0,0,token);
    // 12-oct bug 2: the world must thaw with the lease itself (exit clip done or lease lost), not
    // only on a later session tick. Game thread (dispatch lifecycle); idempotent.
    g_oca_host.FreezeEnd();
}
static void __cdecl CombinedLifecycle(void* context,mortal::dispatch::Event event,uint64_t token){
    if(g_oca_route.Ocarina(token)){OcaLifecycle(event,token);return;}
    MortalLifecycle(context,event,token);
}
// Knife hold (logical bit 0x800 in ON/TRG) is hidden from the native handler while latched, so a
// held button cannot keep the sword ready during the session ("no vuelve a prepararla sola").
static void OcaSuppressKnife(){
    LocalMemory m;
    for(uint32_t at:{g_oca_input,g_oca_input+8}){
        for(int i=0;i<4;++i){uint32_t v=0;if(!m.Read32(at,v)||!(v&0x800))break;if(m.Compare32(at,v,v&~0x800U))break;}
    }
}
static void __cdecl OcarinaDecide(mortal::dispatch::Runtime& run,void* player){
    g_oca_run=&run;
    LocalMemory m;
    link_runtime::HealthState health;
    const bool gameplay=link_runtime::ReadHealth(m,g_oca_player_slot,g_oca_global_slot,health)&&health.eligible&&
        health.player==reinterpret_cast<uint32_t>(player);
    if(g_oca_cfg.item&&g_oca_session.state()==oca::State::Idle){OcaItemHost item;item.gameplay=gameplay;g_oca_grant.Tick(item);
        if(g_oca_manual_on){OcaManualRows();if(g_oca_manual_on)g_oca_manual.Tick(item);}}
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    const bool focused=pid==GetCurrentProcessId();
    const uint64_t packed=g_oca_pad.load(std::memory_order_acquire);
    ocr::Pad pad;pad.connected=((packed>>48)&1)!=0;pad.buttons=static_cast<uint16_t>(packed);
    pad.lx=static_cast<int16_t>(packed>>16);pad.ly=static_cast<int16_t>(packed>>32);
    const uint64_t right=g_oca_pad_right.load(std::memory_order_acquire);
    pad.rx=static_cast<int16_t>(right);pad.ry=static_cast<int16_t>(right>>16);pad.rt=static_cast<uint8_t>(right>>40);
    const auto raw=ocr::Assemble(g_oca_cfg.keys,[](uint8_t vk){return (GetAsyncKeyState(vk)&0x8000)!=0;},pad,g_oca_cfg.pad,g_oca_cfg.pad_n64);
    auto events=g_oca_keys.Poll(raw,focused);
    if(g_oca_cfg.item&&g_oca_use.TakeToggle(gameplay,g_oca_epoch.Current())){events.toggle=true;OcaRecordPush(6,"use toggles");}
    // OcarinaSongPlayback (18-oct): damage (HP below the match, or a damage record on Link) or the player's toggle /
    // stow cut the song while it is being answered: audio stops at once, the session leaves, no effect.
    if(!g_oca_song.Busy())g_oca_song_was_busy=false;
    else if(!g_oca_song_was_busy){g_oca_song_was_busy=true;g_oca_play_hp=gameplay?health.hp:-1;}
    if(g_oca_song.CancelOpen()){
        const uint32_t pl=OcaRead(g_oca_player_slot);
        const uint16_t damage=link_runtime::IsPointer(pl)?static_cast<uint16_t>(OcaRead(pl+0x32C)&0xFFFF):0;
        const bool hurt=(gameplay&&g_oca_play_hp>=0&&health.hp<g_oca_play_hp)||damage!=0;
        if(hurt){OcaRecordPush(9,"playback hurt",health.hp,static_cast<unsigned>(g_oca_play_hp),damage,g_oca_session.token());
            g_oca_song.Cancel(g_oca_song_host,"damage");events.stow=true;}
        else if(events.toggle||events.stow){g_oca_song.Cancel(g_oca_song_host,"player");events.note=-1;}
    }
    if(g_oca_song.InputLocked()){events.note=-1;events.stow=false;events.toggle=false;}   // OoT: no input during the replay
    if(g_oca_song.WantStow())events.stow=true;   // Serenity replayed: the session runs its own exit
    ocr::Frame(g_oca_session,g_oca_host,events,false,GameStep(g_oca_global_slot));
    if(g_oca_session.state()==oca::State::Playing)g_oca_song.OnOpen(g_oca_session.token());
    g_oca_song.Tick(g_oca_song_host,false,GameStep(g_oca_global_slot));
    OcaStaffPaint(gameplay);
    uint32_t on=0;m.Read32(g_oca_input,on);
    if(g_oca_knife.Suppress(on)){
        OcaSuppressKnife();
        // Alternate control config readies through ON 0x10 + player stat 0x1000 instead of 0x800
        // (mortal-draw-native-qualification); not suppressed, only reported once per session.
        if((on&0x10)&&!(on&ocr::KnifeLatch::kKnife)&&!g_oca_alt_knife_logged){g_oca_alt_knife_logged=true;OcaRecordPush(1,"knife ON 0x10 held (alt config not suppressed)",0,on);}
    }
    link_runtime::parry::input::SetOcarinaBlocks(g_oca_session.BlocksParry());
}
// Event-handler frames without our lease (review I4): no input, gates lost, pending work serviced.
static void __cdecl OcarinaObserve(mortal::dispatch::Runtime& run,void*){
    g_oca_run=&run;
    ocr::Frame(g_oca_session,g_oca_host,{},true,GameStep(g_oca_global_slot));
    g_oca_song.Tick(g_oca_song_host,true,GameStep(g_oca_global_slot));
    OcaStaffPaint(false);   // native frames: units and texture freed
    link_runtime::parry::input::SetOcarinaBlocks(g_oca_session.BlocksParry());
}
static void __cdecl CombinedObserve(mortal::dispatch::Runtime& run,void* player){
    if(g_oca_on.load(std::memory_order_acquire))OcarinaObserve(run,player);
    link_runtime::ztarget::runtime::PlayerNative();
}
// ---------------------------------------------------------------------------------------------
// Combat runtime (12-oct-2026): contextual Mortal Draw, its native damage, the parry reaction and
// riposte, and the hit-part observer. Every piece is created only by its own [LinkRuntime] key
// (combat_runtime.h, all default 0); with every key 0 nothing below runs or hooks anything.
// Game thread: the arbiter detour, the player dispatch decide/lifecycle, the MotionMove and neck
// detours, ResolveParry and the GetWepDmVal detour. Worker: only drains the log ring.
// Docs: docs/combat-runtime-2026-10-12.md.
namespace cmb=link_runtime::combat;
namespace mdr=link_runtime::mortal_draw::runtime;
namespace mtg=link_runtime::mortal_draw::target;
namespace mctx=link_runtime::mortal_draw::context;
namespace mdmg=link_runtime::mortal_draw::damage;
namespace mmag=link_runtime::mortal_draw::magnet;
namespace prx=link_runtime::parry::reaction;
namespace prp=link_runtime::parry::riposte;
namespace nhp=link_runtime::native_hitpart;
static cmb::Config g_cmb;
static cmb::Plan g_cmb_plan;
static uint32_t g_cmb_base=0,g_cmb_player_slot=0,g_cmb_global_slot=0;
static bool g_cmb_pinned=false;
// SEH-guarded accessor for the pure templates: copies only, never a raw pointer.
struct CmbMem {
    bool Read(uint32_t address,void* out,size_t n){
        if(!address)return false;
        __try{std::memcpy(out,reinterpret_cast<const void*>(address),n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Write(uint32_t address,const void* in,size_t n){
        if(!address)return false;
        __try{std::memcpy(reinterpret_cast<void*>(address),in,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
template<class T> static T CmbGet(uint32_t address){T v{};CmbMem m;m.Read(address,&v,sizeof(T));return v;}
// Copied log lines from the game thread, written by the worker (never a game pointer).
struct CmbLine {char text[768];};   // md_strike carries strike-time geometry and latch state
static CmbLine g_cmb_lines[128];
static std::atomic<unsigned> g_cmb_write{0},g_cmb_read{0},g_cmb_dropped{0};
// A line the configured LogLevel would not write never takes a ring slot (link_runtime_log.h).
// The ring is also the TP HUD's (HudLog): its lines are written with only [Hud] on (PollCombat runs for either).
static void CmbRing(unsigned line_level,const char* format,va_list args){
    if(!link_runtime::log::Writes(g_log_level,line_level))return;
    const unsigned w=g_cmb_write.load(std::memory_order_relaxed),r=g_cmb_read.load(std::memory_order_acquire);
    if(w-r==128){g_cmb_dropped.fetch_add(1);return;}
    vsnprintf_s(g_cmb_lines[w%128].text,sizeof(g_cmb_lines[0].text),_TRUNCATE,format,args);
    g_cmb_write.store(w+1,std::memory_order_release);
}
static void CmbPush(unsigned line_level,const char* format,va_list args){if(g_cmb.AnyOn())CmbRing(line_level,format,args);}
// Feature events (LogLevel 1 and 2).
static void CmbLog(const char* format,...){va_list args;va_start(args,format);CmbPush(link_runtime::log::kNormal,format,args);va_end(args);}
// Periodic stats and the MortalDrawObserve census/decisions (LogLevel 2 only).
static void CmbLogV(const char* format,...){va_list args;va_start(args,format);CmbPush(link_runtime::log::kVerbose,format,args);va_end(args);}
static void PollCombat(){
    unsigned r=g_cmb_read.load(std::memory_order_relaxed);const unsigned w=g_cmb_write.load(std::memory_order_acquire);
    while(r!=w){Log("%s",g_cmb_lines[r%128].text);g_cmb_read.store(++r,std::memory_order_release);}
    const unsigned dropped=g_cmb_dropped.exchange(0);if(dropped)Log("combat records dropped=%u",dropped);
}
// Native logic-frame counter: the outer input loop increments [global+530C] once per frame
// (docs/parry-input-tick-2026-10-11.md). Feeds the trigger, the reaction (World::frame) and the pump.
static uint32_t CmbFrame(){const uint32_t g=CmbGet<uint32_t>(g_cmb_global_slot);return link_runtime::IsPointer(g)?CmbGet<uint32_t>(g+0x530C):0;}
// Scene epoch for the target tracker: a new room (global+4FAC) or a new player object.
static mtg::Tracker g_cmb_tracker;
static mtg::StaggerWindow g_cmb_stagger;   // parry -> step behind -> Mortal Draw (game thread)
static uint32_t g_cmb_room=0xFFFFFFFF,g_cmb_scene_player=0,g_cmb_epoch=0;
static void CmbScene(uint32_t player){
    const uint32_t g=CmbGet<uint32_t>(g_cmb_global_slot);
    const uint32_t room=link_runtime::IsPointer(g)?CmbGet<uint16_t>(g+0x4FAC):0xFFFF;
    if(room!=g_cmb_room||player!=g_cmb_scene_player){
        g_cmb_room=room;g_cmb_scene_player=player;
        if(++g_cmb_epoch==0)g_cmb_epoch=1;
        g_cmb_tracker.BeginScene(g_cmb_epoch);
        g_cmb_stagger.Clear();
    }
}
// Mortal Draw from walk/run/turn (mortal_draw_start_gate.h): set once at startup when the move
// table pin passes and MortalDrawFromMove is not 0; otherwise the start stays idle-only.
static bool g_cmb_move_start=false;
static bool CmbMoveTablePinnedSeh(const unsigned char* base,size_t size,mortal::start_gate::Foreign& foreign){
    __try{return mortal::start_gate::Pinned(base,size,reinterpret_cast<uint32_t>(base),&g_foreign_probe,&foreign);}
    __except(EXCEPTION_EXECUTE_HANDLER){foreign={};return false;}
}
static bool CmbMoveTablePinned(const unsigned char* base,size_t size){
    mortal::start_gate::Foreign foreign;
    const bool pinned=CmbMoveTablePinnedSeh(base,size,foreign);
    for(unsigned i=0;pinned&&i<foreign.count&&i<2;++i)
        LogForeignPatch("MortalDrawFromMove","inline_call",foreign.site_rva[i],foreign.target[i]);
    return pinned;
}
static bool CmbStartRoutine(uint32_t routine){
    return g_cmb_move_start?mortal::dispatch::NormalMoveRoutine(routine):mortal::dispatch::NormalIdleRoutine(routine);
}
// Link's facts for the trigger and the policy. player_free: normal routine (r0 0, r1 0; with
// g_cmb_move_start also walk/back/run/turn) and no pending damage; sword_sheathed: the knife slot is hidden (cPlBody m_pKnife flags bit 8 clear).
struct CmbPlayer {uint32_t player=0,routine=0;mtg::Vec3 pos;mdr::PlayerFacts facts;bool damaged=false;};
// STA_INTO_SHOP (status2 0x40000) can stay latched after the Merchant (r11c, r31d 31-oct): with it every combat skill
// read "no_gameplay" (shield guard, sword actions). CombatShopFlagFix=1 (absent = 1) reads past that one bit, as
// LowHealthShopFlagFix does; a real shop or case is still gated by SSCRN/SUSPEND/STOP and the other shared bits.
static bool g_cmb_shop_flag_fix=true;
static bool CmbReadPlayer(CmbPlayer& out){
    out=CmbPlayer{};LocalMemory memory;link_runtime::HealthState health;
    if(!link_runtime::low_health::ReadHealth(memory,g_cmb_player_slot,g_cmb_global_slot,g_cmb_shop_flag_fix,health)||
        !link_runtime::IsPointer(health.player))return false;
    out.player=health.player;CmbMem m;float pos[3]{};
    if(!m.Read(out.player+0x94,pos,12))return false;
    out.pos={pos[0],pos[1],pos[2]};out.routine=CmbGet<uint32_t>(out.player+0xFC);
    const uint16_t damage=CmbGet<uint16_t>(out.player+0x32C);out.damaged=damage!=0;
    const uint32_t body=CmbGet<uint32_t>(out.player+0x7E4);
    const uint32_t knife=link_runtime::IsPointer(body)?CmbGet<uint32_t>(body+0x2C):0;
    const bool knife_known=link_runtime::IsPointer(knife);
    out.facts.stable=true;out.facts.gameplay=health.eligible;
    const bool still=(out.routine&0xFFFF)==0;
    const bool moving=g_cmb_move_start&&(out.routine&0xFF)==0&&((out.routine>>8)&0xFF)>=1&&((out.routine>>8)&0xFF)<=4;
    out.facts.player_free=health.eligible&&(still||moving)&&!out.damaged;
    out.facts.sword_sheathed=knife_known&&!(CmbGet<uint32_t>(knife+4)&0x8);
    return true;
}
// N-1: our publisher `from` is Link's position with this tag on Y (1/64 unit), a value the native
// knife sweep (whose `from` is its own, unattributed argument) does not produce, so a native
// re-post never matches our record bit for bit.
constexpr float kCmbFromMark=0.015625f;
static void CmbFrom(const mtg::Vec3& p,float (&out)[3]){out[0]=p.x;out[1]=p.y+kCmbFromMark;out[2]=p.z;}
static mtg::Config CmbTargetConfig(){
    mtg::Config c;c.max_horizontal=float(g_cmb.max_range);c.strike_max_horizontal=float(g_cmb.strike_range);
    c.require_line=!g_cmb.line_waiver;return c;
}
// ---- shared native-hit adapter -----------------------------------------------------------------
static mdmg::Adapter g_cmb_adapter;
static bool g_cmb_adapter_on=false;   // set before the GetWepDmVal detour is enabled; then constant
enum class CmbCaller {None,Mortal,Riposte,Sword,Zelda};   // Zelda: the partner's ray (zelda_companion_runtime.h)
static CmbCaller g_cmb_caller=CmbCaller::None;   // who is inside NativeHit (decoder by call site)
static bool g_cmb_mortal_same=false;             // target::Revalidate of this frame (Mortal Draw)
static uint64_t g_cmb_mortal_identity=0;
static bool CmbFacts(void*,const mdmg::HitTarget& t,mdmg::Facts& out){
    CmbMem m;bool same=false;
    if(g_cmb_caller==CmbCaller::Mortal||g_cmb_caller==CmbCaller::Sword||g_cmb_caller==CmbCaller::Zelda)same=g_cmb_mortal_same&&t.identity==g_cmb_mortal_identity;
    else if(g_cmb_caller==CmbCaller::Riposte&&(t.identity>>56)==prp::kIdentityTag){
        // Riposte identity (review I2 minor 4: decoded because the riposte is the caller, not by tag):
        // the parried owner's actor, id and type, re-read now.
        const uint8_t id=CmbGet<uint8_t>(t.actor+0x100),type=CmbGet<uint8_t>(t.actor+0x101);
        same=t.identity==prp::OwnerIdentity(t.actor,id,type);
    }
    return mdmg::ReadFacts(m,g_cmb_base,t,same,t.from,out);
}
static bool CmbPublish(void*,const mdmg::PublishArgs& a){
    __try{mdmg::CallPublisher(g_cmb_base+mdmg::layout::publisher_rva,a);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool CmbRecord(void*,uint32_t actor,mdmg::Record& out){CmbMem m;return mdmg::ReadRecord(m,actor,out);}
// GetWepDmVal detour body: the adapter's Resolve; a non-final resolution keeps the native value.
static bool CmbAdapterOn(){return g_cmb_adapter_on;}
static void SwordBeforeResolve();
static bool BashClaim(uint32_t rva,uint32_t actor,unsigned weapon,int original,int hp);   // spin_attack_runtime.h
static int CmbAdapterResult(uintptr_t caller,void* target,unsigned weapon,int original){
    const uint32_t actor=reinterpret_cast<uint32_t>(target);
    const int hp=CmbGet<int16_t>(actor+mdmg::layout::hp);
    const uint32_t rva=caller>=g_cmb_base?static_cast<uint32_t>(caller-g_cmb_base):0;
    SwordBeforeResolve();
    // spin-fix-01: the Shield Bash's own kick record knocks back without damage (shield_bash_scan.h Claims).
    if(BashClaim(rva,actor,weapon,original,hp))return link_runtime::shield_bash::KnockbackDamage(hp);
    const mdmg::Resolution r=g_cmb_adapter.Resolve({rva,actor,weapon,original,hp});
    const int result=mdmg::Compose(r,[](int v){return v;});
    if(r.owned||r.mismatch)
        CmbLog("damage resolve actor=0x%08X token=%llu kind=%u owned=%u final=%u capped=%u mismatch=0x%X original=%d result=%d hp=%d outcome=%s",
            actor,r.token,static_cast<unsigned>(r.kind),r.owned?1U:0U,r.final?1U:0U,r.capped?1U:0U,r.mismatch,original,result,hp,
            mdr::OutcomeName(g_cmb_adapter.Last()));
    return result;
}
// ---- hit-part observer ---------------------------------------------------------------------------
static uint64_t g_cmb_hp_identity=0;
static uint32_t g_cmb_hp_owner=0;
static void CmbObserveHitPart(uint32_t actor,const char* source){
    CmbMem m;char line[480]{};
    const nhp::Source s=nhp::ManagedSource(m,g_cmb_base);
    nhp::FormatObservation(line,sizeof(line),s,m,actor);
    CmbLog("%s source=%s id=0x%02X type=0x%02X",line,source,CmbGet<uint8_t>(actor+0x100),CmbGet<uint8_t>(actor+0x101));
}
// ---- parry reaction (Link's secondary layer) ------------------------------------------------------
static prx::Reaction* g_rx=nullptr;
static unsigned char* g_rx_fcv=nullptr;
static unsigned char* g_rx_seq=nullptr;
alignas(16) static unsigned char g_rx_work[0x200];
static ULONGLONG g_rx_last_tick=0;
static uint8_t g_rx_weapon=0;static uint32_t g_rx_room=0;
using RxInit=void(__cdecl*)(void*,void*,void*,void*,int,int,float,int);
static bool CmbRxInit(uint32_t player){
    __try{reinterpret_cast<RxInit>(g_cmb_base+0x2611A0)(reinterpret_cast<void*>(player),g_rx_work,g_rx_fcv,g_rx_seq,0,0,0.f,0);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Port of parry_reaction.h. Install mirrors the executed fixture (test_parry_secondary_route.cpp):
// work+44 = 10000000 only during init, init(player, work, FCV, SEQ, hokan 0, attr 0, 0.0, keep 0),
// then 2A8 = work, speed C0 = 1 and weight C8 = 0 (override mode, no 80000000).
// arm-fix-01 (secondary_slot.h blend_history): a hand-back to a native work (the stance, the neck) clears the parts'
// flipped key histories our clip left, as MotionSetCore does, so the work's next sample searches its own keys from 0.
struct HistMemory {
    bool Read32(uint32_t a,uint32_t& v){CmbMem m;return m.Read(a,&v,4);}
    bool Write(uint32_t a,const void* in,size_t n){CmbMem m;return m.Write(a,in,n);}
};
static unsigned g_hist_resets=0;
static bool CmbGiveSecondary(uint32_t player,uint32_t expected,uint32_t value){
    LocalMemory m;if(!m.Compare32(player+0x2A8,expected,value))return false;
    if(value){HistMemory h;if(link_runtime::secondary_slot::blend_history::Reset(h,player))++g_hist_resets;}
    return true;
}
struct CmbReactionPort {
    bool Install(uint32_t player,uint32_t& work){
        if(!g_rx_fcv||!g_rx_seq)return false;
        std::memset(g_rx_work,0,sizeof(g_rx_work));
        uint32_t flags=0x10000000U;std::memcpy(g_rx_work+0x44,&flags,4);
        if(!CmbRxInit(player))return false;
        std::memcpy(&flags,g_rx_work+0x44,4);flags&=~0x10000000U;std::memcpy(g_rx_work+0x44,&flags,4);
        const float one=1.f,zero=0.f;std::memcpy(g_rx_work+0xC0,&one,4);std::memcpy(g_rx_work+0xC8,&zero,4);
        work=reinterpret_cast<uint32_t>(g_rx_work);
        CmbMem m;return m.Write(player+0x2A8,&work,4);
    }
    uint32_t Secondary(uint32_t player){return CmbGet<uint32_t>(player+0x2A8);}
    bool ReplaceSecondary(uint32_t player,uint32_t expected,uint32_t value){return CmbGiveSecondary(player,expected,value);}
    float Weight(uint32_t work){return CmbGet<float>(work+0xC8);}
    void SetWeight(uint32_t work,float w){CmbMem m;m.Write(work+0xC8,&w,4);}
    uint16_t Status(uint32_t work){return CmbGet<uint16_t>(work+0x42);}
};
// ---- shield guard (shield-guard-01, shield_guard.h) -----------------------------------------------
// Game thread: GuardTick (MotionMove, before the reaction), GuardResolve (tier 3 of ResolveParry) and the
// neck gate. Worker: StartGuard (files, before any hook) and GuardPollAudio (sounds). Off unless ShieldGuard=1.
namespace sg=link_runtime::shield_guard;
static bool CmbResident(const wchar_t* directory,const wchar_t* name,size_t size,const char* sha,unsigned char*& out);
namespace sga=link_runtime::shield_guard::assets;
static bool g_sg_on=false;
static sg::GuardLayer* g_sg_layer=nullptr;
static sg::Durability g_sg_dur;
static sg::Clock g_sg_clock;
static sg::BodySwap g_sg_swap;
static uint32_t g_sg_refused_key=0;   // shield-guard-02: last fingerprint refusal logged (costume, field)
static link_runtime::parry::general::Follow g_sg_follow;
static unsigned char* g_sg_fcv[3]{};static unsigned char* g_sg_seq[3]{};
static unsigned char* g_sg_fcv_fk[3]{};static unsigned char* g_sg_seq_fk[3]{};static bool g_sg_fk_ok=false;   // shield-fk-guard-01
static sg::ClipVariant g_sg_use=sg::ClipVariant::Effector,g_sg_running=sg::ClipVariant::Effector;   // for the next install / installed
alignas(16) static unsigned char g_sg_work[0x200];
static unsigned char* g_sg_bin[4][2]{};           // [costume][0 hand, 1 none], page aligned, never released
static sg::Variant g_sg_variant[4][2];
static bool g_sg_prepared[4][2]{};
static bool g_sg_native_ok=false;
static uint32_t g_sg_frame=0,g_sg_room=0xFFFFFFFFu,g_sg_player=0;
static unsigned g_sg_costume=0xFF;
static bool g_sg_wanted=false,g_sg_supported=false,g_sg_broken_logged=false;
static int g_sg_arm_ik_logged=2;   // last primary arm-IK verdict logged (sword-hand-01), 2 = none yet
static sg::Placement g_sg_shown=sg::Placement::Back;
static ULONGLONG g_sg_last_tick=0;
static uint64_t g_sg_last_pass[16]{};
// Sound events (game thread -> worker): 1 block wood, 2 block metal, 3 break wood, 4 break metal, 5 back.
static uint8_t g_sg_sounds[16]{};static std::atomic<unsigned> g_sg_sound_w{0},g_sg_sound_r{0};
static void GuardSound(uint8_t code){
    const unsigned w=g_sg_sound_w.load(std::memory_order_relaxed);
    if(w-g_sg_sound_r.load(std::memory_order_acquire)>=16)return;
    g_sg_sounds[w%16]=code;g_sg_sound_w.store(w+1,std::memory_order_release);
}
static bool GuardSecondaryInit(uint32_t player,sg::Clip clip,bool fk){
    unsigned char* const f=fk?g_sg_fcv_fk[int(clip)]:g_sg_fcv[int(clip)];unsigned char* const q=fk?g_sg_seq_fk[int(clip)]:g_sg_seq[int(clip)];
    __try{reinterpret_cast<RxInit>(g_cmb_base+0x2611A0)(reinterpret_cast<void*>(player),g_sg_work,f,q,0,0,0.f,0);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// shield_guard.h Port: the reaction port's exact install contract (CmbReactionPort) on our own work.
struct GuardPort {
    bool Install(uint32_t player,sg::Clip clip,uint32_t& work){
        // The guard install takes the variant this primary needs; hit and break keep the running layer's variant.
        const sg::ClipVariant v=clip==sg::Clip::Guard?g_sg_use:g_sg_running;const bool fk=v==sg::ClipVariant::Fk;
        const int i=int(clip);
        if(fk?(!g_sg_fcv_fk[i]||!g_sg_seq_fk[i]):(!g_sg_fcv[i]||!g_sg_seq[i]))return false;
        float weight=0.f;std::memcpy(&weight,g_sg_work+0xC8,4);
        std::memset(g_sg_work,0,sizeof(g_sg_work));
        uint32_t flags=0x10000000U;std::memcpy(g_sg_work+0x44,&flags,4);
        if(!GuardSecondaryInit(player,clip,fk))return false;
        g_sg_running=v;
        std::memcpy(&flags,g_sg_work+0x44,4);flags&=~0x10000000U;std::memcpy(g_sg_work+0x44,&flags,4);
        const float one=1.f;std::memcpy(g_sg_work+0xC0,&one,4);
        if(clip==sg::Clip::Guard)weight=0.f;
        std::memcpy(g_sg_work+0xC8,&weight,4);   // a re-install (hit, break) keeps the weight in effect
        work=reinterpret_cast<uint32_t>(g_sg_work);
        CmbMem m;return m.Write(player+0x2A8,&work,4);
    }
    uint32_t Secondary(uint32_t player){return CmbGet<uint32_t>(player+0x2A8);}
    bool ReplaceSecondary(uint32_t player,uint32_t expected,uint32_t value){return CmbGiveSecondary(player,expected,value);}
    float Weight(uint32_t work){return CmbGet<float>(work+0xC8);}
    void SetWeight(uint32_t work,float w){CmbMem m;m.Write(work+0xC8,&w,4);}
    uint16_t Status(uint32_t work){return CmbGet<uint16_t>(work+0x42);}
};
struct GuardNative {   // shield_guard.h PrepareVariant: calcModelAddr 25CB50, getBoundingBox 25D120 (kOcarinaMount pins)
    bool CalcModelAddr(uint32_t bin){
        __try{reinterpret_cast<void(__cdecl*)(uint32_t)>(g_cmb_base+0x25CB50)(bin);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool BoundingBox(uint32_t bin,uint32_t (&out)[6]){
        uint32_t box[9]={};
        __try{reinterpret_cast<void(__cdecl*)(uint32_t,void*)>(g_cmb_base+0x25D120)(bin,box);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
        for(unsigned i=0;i<6;++i)out[i]=box[3+i];
        return true;
    }
};
// Startup (worker, inside StartCombat before the MotionMove hook): clips, variants and their native pins.
static void StartGuard(const wchar_t* directory){
    if(!g_cmb.guard.enabled)return;
    bool clips=true;
    for(unsigned i=0;i<3;++i){
        const auto& c=sga::kClips[i];
        clips&=CmbResident(directory,c.fcv.name,c.fcv.size,c.fcv.sha,g_sg_fcv[i])&&CmbResident(directory,c.seq.name,c.seq.size,c.seq.sha,g_sg_seq[i]);
    }
    if(!clips){Log("ShieldGuard disabled: link_shield_guard*.fcv/.seq missing or changed");return;}
    g_sg_fk_ok=true;
    for(unsigned i=0;i<3;++i){
        const auto& c=sga::kClipsFk[i];
        g_sg_fk_ok&=CmbResident(directory,c.fcv.name,c.fcv.size,c.fcv.sha,g_sg_fcv_fk[i])&&CmbResident(directory,c.seq.name,c.seq.size,c.seq.sha,g_sg_seq_fk[i]);
    }
    if(!g_sg_fk_ok)Log("ShieldGuard fk clips missing or changed (link_shield_guard*_fk.fcv/.seq): no guard over primaries without arm IK");
    g_sg_native_ok=PinsAt(g_cmb_base,link_runtime::native_pins::kOcarinaMount);
    unsigned bodies=0;
    for(unsigned c=0;c<4&&g_sg_native_ok;++c){
        const sga::File files[2]={sga::kBodies[c].hand,sga::kBodies[c].none};
        for(unsigned k=0;k<2;++k)bodies+=CmbResident(directory,files[k].name,files[k].size,files[k].sha,g_sg_bin[c][k])?1U:0U;
    }
    g_sg_dur.Configure(g_cmb.guard.blocks,g_cmb.guard.regen_seconds);
    g_sg_layer=new sg::GuardLayer();
    g_sg_on=true;
    Log("ShieldGuard ready=1 blocks=%u regen_s=%u body_variants=%u/8 native=%u fk=%u tier3=%s problems=%u",g_sg_dur.Max(),g_cmb.guard.regen_seconds,
        bodies,g_sg_native_ok?1U:0U,g_sg_fk_ok?1U:0U,g_gen.settings.mode!=link_runtime::parry::general::Mode::Off?"after_tier2":"needs_ParryGeneralBlock",g_cmb.guard_problems);
}
static const sg::Variant* GuardVariant(unsigned costume,unsigned k){
    if(costume>3||!g_sg_bin[costume][k])return nullptr;
    if(!g_sg_prepared[costume][k]){
        g_sg_prepared[costume][k]=true;LocalMemory m;GuardNative native;
        g_sg_variant[costume][k]=sg::PrepareVariant(m,native,reinterpret_cast<uint32_t>(g_sg_bin[costume][k]),
            k?sga::kBodies[costume].none.size:sga::kBodies[costume].hand.size);
        CmbLog("shield_guard variant costume=%u kind=%s ready=%u",costume,k?"none":"hand",g_sg_variant[costume][k].ready?1U:0U);
    }
    return g_sg_variant[costume][k].ready?&g_sg_variant[costume][k]:nullptr;
}
static void GuardPlace(sg::Placement want,unsigned costume){
    LocalMemory m;sg::SwapResult r=sg::SwapResult::Unchanged;sg::Placement now=g_sg_shown;
    if(want==sg::Placement::Back){
        if(g_sg_swap.Holding()){r=g_sg_swap.Restore(m,g_cmb_player_slot);if(r==sg::SwapResult::Restored||!g_sg_swap.Holding())now=sg::Placement::Back;}
        else now=sg::Placement::Back;
    }else{
        const sg::Variant* v=GuardVariant(costume,want==sg::Placement::Hand?0:1);
        if(!v){if(g_sg_swap.Holding()){r=g_sg_swap.Restore(m,g_cmb_player_slot);}now=g_sg_swap.Holding()?g_sg_shown:sg::Placement::Back;}
        else{
            r=g_sg_swap.Show(m,g_cmb_player_slot,*v,sga::kBodies[costume].fingerprint);
            if(r==sg::SwapResult::Shown||r==sg::SwapResult::Unchanged)now=want;
            else if(!g_sg_swap.Holding())now=sg::Placement::Back;
        }
    }
    // shield-guard-02: a refusal is logged once per costume/field (with the differing field), not every frame.
    if(r==sg::SwapResult::Fingerprint){
        const auto& live=g_sg_swap.Refused();const auto& b=sga::kBodies[costume];
        const uint32_t field=sg::PrintMismatch(live,b.header,b.positions);
        const uint32_t key=(costume<<24)^(field<<8)^1U;
        if(key!=g_sg_refused_key){
            g_sg_refused_key=key;
            unsigned k=0;for(;k<sg::body_layout::kPrintHeaderCount&&sg::body_layout::kPrintHeader[k]!=field;++k){}
            if(k<sg::body_layout::kPrintHeaderCount)
                CmbLog("shield_guard place want=%s shown=%s result=fingerprint costume=%u field=0x%02X live=0x%08X want=0x%08X print=0x%08X expected=0x%08X",
                    sg::PlacementName(want),sg::PlacementName(now),costume,field,live.header[k],b.header[k],live.print,b.fingerprint);
            else
                CmbLog("shield_guard place want=%s shown=%s result=fingerprint costume=%u field=%s live=0x%08X want=0x%08X print=0x%08X expected=0x%08X",
                    sg::PlacementName(want),sg::PlacementName(now),costume,field?"positions":"none",live.positions,b.positions,live.print,b.fingerprint);
        }
    }else if(r!=sg::SwapResult::Unchanged||now!=g_sg_shown){
        if(r==sg::SwapResult::Shown||r==sg::SwapResult::Restored)g_sg_refused_key=0;
        CmbLog("shield_guard place want=%s shown=%s result=%s costume=%u",sg::PlacementName(want),sg::PlacementName(now),sg::SwapName(r),costume);
    }
    g_sg_shown=now;
}
// ---- secondary slot guard (slot-guard-01, secondary_slot.h) ----------------------------------------
// Game thread, from the MotionMove detour: the stance check our layers' hand-back uses, the watchdog that releases a
// stale stance or an orphaned work of ours from player+2A8, and the read-only arm probe (31-oct: no arms after a parry
// against Krauser). Only the watchdog writes: a compare-exchange of 2A8 to 0.
namespace ssl=link_runtime::secondary_slot;
struct SlotMemory {
    bool Read32(uint32_t a,uint32_t& v){CmbMem m;return m.Read(a,&v,4);}
    bool Read(uint32_t a,void* out,size_t n){CmbMem m;return m.Read(a,out,n);}
};
static ssl::Watchdog g_slot_dog;
static uint32_t g_slot_foreign=0;   // last foreign 2A8 value logged (once per value)
static uint32_t g_arm_frame=0,g_arm_logged=0;static unsigned g_arm_part=0;
static ssl::arm::Health g_arm_last=ssl::arm::Health::Ok;
static ssl::Works SlotWorks(){return {reinterpret_cast<uint32_t>(g_rx_work),reinterpret_cast<uint32_t>(g_sg_work)};}
static const char* SlotKind(uint32_t value,uint32_t player){return ssl::Name(ssl::Classify(value,player,g_cmb_base,SlotWorks()));}
static ssl::StanceState SlotStance(uint32_t player){SlotMemory m;return ssl::Check(ssl::ReadStance(m,g_cmb_base,player),player);}
static bool SlotStanceLive(uint32_t player){return SlotStance(player)==ssl::StanceState::Live;}
static bool BadArm(ssl::arm::Health h){return h!=ssl::arm::Health::Ok&&h!=ssl::arm::Health::Unread;}
// arm-trace (2-nov, docs/combate-2026-11-01.md §1): the arm probe runs every frame from a layer's install until 48 frames
// after it hands 2A8 back, and dumps the first bone that goes bad (cCoord/cParts fields, re4-decomp model.h). Read-only.
static uint32_t g_arm_trace_until=0;
static void ArmTraceArm(uint32_t frame){g_arm_trace_until=frame+48;if(!g_arm_trace_until)g_arm_trace_until=1;}
static uint32_t ArmPartAddr(uint32_t model,unsigned index){
    uint32_t node=CmbGet<uint32_t>(model+0xF4);
    for(unsigned i=0;i<index&&node>=0x10000;++i)node=CmbGet<uint32_t>(node+0xF4);
    return node>=0x10000?node:0;
}
static void ArmBadDump(uint32_t who,unsigned part){
    const uint32_t a=ArmPartAddr(who,part);
    if(!a){CmbLog("arm_bad part=%u unreadable",part);return;}
    float f[0x1C4/4]{};CmbMem mem;
    if(!mem.Read(a,f,sizeof(f))){CmbLog("arm_bad part=%u unreadable",part);return;}
    auto v=[&](uint32_t off,unsigned k){return f[off/4+k];};
    uint32_t flags=0;std::memcpy(&flags,&f[0x1C0/4],4);
    const uint16_t* hist=reinterpret_cast<const uint16_t*>(&f[0x19C/4]);
    CmbLog("arm_bad part=%u at=0x%08X pos=%g,%g,%g ang=%g,%g,%g scale=%g,%g,%g r_scale=%g,%g,%g lmat=%g,%g,%g,%g|%g,%g,%g,%g|%g,%g,%g,%g "
        "prev=%g,%g,%g,%g|%g,%g,%g,%g|%g,%g,%g,%g mp_pos=%g,%g,%g mp_rot=%g,%g,%g mp_scale=%g,%g,%g ik=%g flags=0x%08X "
        "hist=%u,%u,%u %u,%u,%u %u,%u,%u",part,a,v(0x94,0),v(0x94,1),v(0x94,2),v(0xA0,0),v(0xA0,1),v(0xA0,2),v(0xAC,0),v(0xAC,1),v(0xAC,2),
        v(0xB8,0),v(0xB8,1),v(0xB8,2),v(0x3C,0),v(0x3C,1),v(0x3C,2),v(0x3C,3),v(0x3C,4),v(0x3C,5),v(0x3C,6),v(0x3C,7),v(0x3C,8),v(0x3C,9),
        v(0x3C,10),v(0x3C,11),v(0xC4,0),v(0xC4,1),v(0xC4,2),v(0xC4,3),v(0xC4,4),v(0xC4,5),v(0xC4,6),v(0xC4,7),v(0xC4,8),v(0xC4,9),
        v(0xC4,10),v(0xC4,11),v(0x174,0),v(0x174,1),v(0x174,2),v(0x180,0),v(0x180,1),v(0x180,2),v(0x18C,0),v(0x18C,1),v(0x18C,2),
        v(0x198,0),flags,hist[0],hist[1],hist[2],hist[3],hist[4],hist[5],hist[6],hist[7],hist[8]);
}
static void SlotWatch(uint32_t frame,const CmbPlayer& p,bool player,void* model){
    if(!player||!model)return;
    const uint32_t who=reinterpret_cast<uint32_t>(model);
    const uint32_t slot=CmbGet<uint32_t>(who+ssl::kSlot);
    ssl::Watch w;w.player=who;w.slot=slot;w.image_base=g_cmb_base;w.frame=frame;w.works=SlotWorks();
    w.reaction_active=g_rx&&g_rx->Active();w.guard_active=g_sg_layer&&g_sg_layer->Active();
    SlotMemory mem;ssl::Stance stance{};
    if(ssl::Classify(slot,who,g_cmb_base,w.works)==ssl::Kind::Stance){stance=ssl::ReadStance(mem,g_cmb_base,who);w.stance=ssl::Check(stance,who);}
    ssl::Verdict verdict=ssl::Verdict::Owned;
    if(g_slot_dog.Release(w,verdict)){
        LocalMemory lm;const bool won=lm.Compare32(who+ssl::kSlot,slot,0);
        if(won)g_slot_dog.Released(verdict);
        CmbLog("secondary_slot release why=%s slot=0x%08X won=%u frame=%u routine=0x%08X stance=%s mot3_model=0x%08X mot0=0x%08X clip=0x%08X "
            "mot1=0x%08X mot2=0x%08X primary=0x%08X weight=%.3f reaction=%u guard=%u releases=%u",
            ssl::Name(verdict),slot,won?1U:0U,frame,p.routine,ssl::Name(w.stance),stance.model,stance.mot0,stance.clip,stance.mot1,stance.mot2,
            CmbGet<uint32_t>(who+ssl::kPrimaryMotion),CmbGet<float>(slot+ssl::kWeight),w.reaction_active?1U:0U,w.guard_active?1U:0U,
            g_slot_dog.stats().releases);
    }else if(verdict==ssl::Verdict::Foreign&&slot!=g_slot_foreign){
        g_slot_foreign=slot;
        CmbLog("secondary_slot foreign slot=0x%08X weight=%.3f frame=%u routine=0x%08X primary=0x%08X (not ours: left alone)",
            slot,CmbGet<float>(slot+ssl::kWeight),frame,p.routine,CmbGet<uint32_t>(who+ssl::kPrimaryMotion));
    }
    // Arm probe (read-only), every 8 frames in gameplay (every frame inside an arm trace window): a line when an
    // arm bone goes bad, changes, recovers, and every ~3000 frames while it stays bad, with what 2A8 holds.
    const bool tracing=g_arm_trace_until&&frame<=g_arm_trace_until&&frame+64>=g_arm_trace_until;
    if(!p.facts.gameplay||(!tracing&&frame-g_arm_frame<8&&frame>=g_arm_frame))return;
    g_arm_frame=frame;
    ssl::arm::Part parts[ssl::arm::kLast+1]{};ssl::arm::Report r;
    if(ssl::arm::ReadParts(mem,who,parts))r=ssl::arm::Judge(parts);
    if(r.health==ssl::arm::Health::Unread)return;
    const bool bad=BadArm(r.health),was=BadArm(g_arm_last);
    if(bad&&!was)ArmBadDump(who,r.part);
    const bool log=(bad&&(!was||r.health!=g_arm_last||r.part!=g_arm_part))||(!bad&&was)||(bad&&frame-g_arm_logged>=3000);
    g_arm_last=r.health;g_arm_part=bad?r.part:0;
    if(!log)return;
    g_arm_logged=frame;
    const uint32_t now=CmbGet<uint32_t>(who+ssl::kSlot);
    CmbLog("arm_probe health=%s part=%u value=%.3f flags=0x%08X scale=%.3f,%.3f,%.3f slot=0x%08X (%s) weight=%.3f primary=0x%08X routine=0x%08X "
        "frame=%u releases=%u",ssl::arm::Name(r.health),r.part,r.value,r.flags,r.scale[0],r.scale[1],r.scale[2],now,SlotKind(now,who),
        now?CmbGet<float>(now+ssl::kWeight):0.f,CmbGet<uint32_t>(who+ssl::kPrimaryMotion),p.routine,frame,g_slot_dog.stats().releases);
}
static bool SwordShieldInHand();   // spin_attack_runtime.h (shield-guard-02: Shield Bash / Back Slice hold the shield)
static void GuardTick(uint32_t frame,const CmbPlayer& p,bool player,void* model){
    if(!g_sg_on||!g_sg_layer)return;
    namespace zt=link_runtime::ztarget::runtime;
    const uint32_t g=CmbGet<uint32_t>(g_cmb_global_slot);
    const uint32_t room=link_runtime::IsPointer(g)?CmbGet<uint16_t>(g+0x4FAC):0xFFFF;
    const double step=GameStep(g_cmb_global_slot);
    const bool fresh=frame!=g_sg_frame;
    if(fresh){g_sg_frame=frame;g_sg_clock.Advance(step);}
    if(fresh&&g_sg_dur.Regen(g_sg_clock.Now())){
        GuardSound(5);g_sg_broken_logged=false;
        CmbLog("shield_guard regen blocks=%u at=%.0f",g_sg_dur.Left(),g_sg_clock.Now());
    }
    LocalMemory lm;unsigned costume=0xFF;
    const bool costume_ok=player&&link_runtime::ReadSwordCostume(lm,g_cmb_player_slot,g_cmb_global_slot,costume);
    const bool gameplay=player&&p.facts.gameplay&&costume_ok;
    g_sg_supported=gameplay&&sg::CostumeSupported(costume);
    const bool lock=zt::controller.held&&(zt::controller.phase==link_runtime::ztarget::Phase::Locked||zt::controller.phase==link_runtime::ztarget::Phase::Recenter);
    const bool knife=((p.routine>>8)&0xFF)==0x0B||((p.routine)&0xFF)!=0;
    const bool broken=g_sg_dur.Broken();
    const bool scene=room!=g_sg_room||p.player!=g_sg_player;
    g_sg_wanted=g_sg_supported&&lock&&!knife&&!broken&&!p.damaged;
    sg::LayerWorld w;w.player=reinterpret_cast<uint32_t>(model);w.frame=frame;w.step=static_cast<float>(step);w.image_base=g_cmb_base;
    w.wanted=g_sg_wanted;w.stance_live=player&&SlotStanceLive(w.player);   // slot-guard-01: the exit's stance check
    // sword-hand-01: the clips drive the left hand as the native arm IK target; only over a primary with that chain.
    CmbMem arm_mem;const int arm_ik=player?sg::PrimaryArmIk(arm_mem,reinterpret_cast<uint32_t>(model)):-1;
    if(player&&g_sg_wanted&&arm_ik!=g_sg_arm_ik_logged){
        g_sg_arm_ik_logged=arm_ik;
        CmbLog("shield_guard primary arm_ik=%d motion=0x%08X%s",arm_ik,CmbGet<uint32_t>(reinterpret_cast<uint32_t>(model)+sg::kPrimaryMotion),
            arm_ik==1?" (ik clips)":arm_ik==0&&g_sg_fk_ok?" (fk clips: the hand is not an IK effector)":" (layer held back: no clip variant for this primary)");
    }
    const sg::ClipVariant variant=sg::VariantFor(arm_ik,g_sg_fk_ok);
    if(variant!=sg::ClipVariant::None)g_sg_use=variant;
    const bool mismatch=sg::VariantMismatch(g_sg_layer->phase()!=sg::Phase::Idle,g_sg_running,variant);
    w.blocked=!player||p.damaged||!gameplay||scene||mortal::dispatch_hooks::runtime.active||g_oca_session.state()!=oca::State::Idle||
        variant==sg::ClipVariant::None||mismatch;
    GuardPort port;
    w.lent=g_rx&&g_rx->Active()&&port.Secondary(w.player)!=g_sg_layer->Work();
    const sg::Phase before=g_sg_layer->phase();const sg::Clip clip_before=g_sg_layer->clip();
    const unsigned gives_before=g_sg_layer->stats().completed+g_sg_layer->stats().cancelled;
    g_sg_layer->Tick(w,port);g_sg_last_tick=GetTickCount64();
    if(g_sg_layer->stats().completed+g_sg_layer->stats().cancelled!=gives_before){   // slot-guard-01: what the exit gave 2A8 back to
        const auto& e=g_sg_layer->last_exit();ArmTraceArm(frame);
        CmbLog("shield_guard layer exit previous=0x%08X (%s) restored=0x%08X (%s) wrote=%u cleared=%u stance=%s frame=%u",
            e.previous,SlotKind(e.previous,w.player),e.restored,SlotKind(e.restored,w.player),e.wrote?1U:0U,e.cleared?1U:0U,
            ssl::Name(SlotStance(w.player)),frame);
    }
    if(before!=g_sg_layer->phase()||clip_before!=g_sg_layer->clip()){
        const auto& s=g_sg_layer->stats();
        CmbLog("shield_guard layer %s->%s clip=%s variant=%s frame=%u installs=%u lost=%u refused=%u failed=%u lent=%u contested=%u",
            sg::PhaseName(before),sg::PhaseName(g_sg_layer->phase()),sg::ClipName(g_sg_layer->clip()),sg::VariantName(g_sg_running),frame,s.installs,s.lost_owner,
            s.refused,s.install_failed,s.lent,s.contested);
    }
    // The body swap follows on frames where the player object is the one we know; a room/player change
    // first drops the lease (Restore detects it) and the native body of the new room is untouched until
    // the next wanted placement.
    const bool shield_action=g_sg_supported&&!p.damaged&&SwordShieldInHand();
    // A primary with no clip variant (unreadable, or no arm IK and the FK clips missing) can't play the guard: keep the
    // shield on the back instead of hanging it in the hand with no pose (the rear shield keeps working there).
    const sg::Placement want=sg::Want(g_sg_supported&&!scene,broken,g_sg_wanted&&variant!=sg::ClipVariant::None,g_sg_layer->Weight(),shield_action);
    // shield-weapon-log-01 (Donald 29-oct: Striker shows no shield, grenades show it without the guard pose): one line
    // per change of the guard inputs, with the equipped weapon (global+0x4FC0), to tell per weapon why it fails.
    if(player){
        const uint8_t weapon=link_runtime::IsPointer(g)?CmbGet<uint8_t>(g+0x4FC0):0xFF;
        const uint32_t motion=CmbGet<uint32_t>(reinterpret_cast<uint32_t>(model)+sg::kPrimaryMotion);
        const uint32_t key=weapon|(lock?1U<<8:0)|(knife?1U<<9:0)|(g_sg_wanted?1U<<10:0)|(static_cast<uint32_t>(arm_ik+1)<<11)|
            (w.blocked?1U<<13:0)|(static_cast<uint32_t>(variant)<<25)|(mismatch?1U<<27:0)|(static_cast<uint32_t>(want)<<14)|(static_cast<uint32_t>(g_sg_layer->phase())<<16)|
            (g_sg_supported?1U<<20:0)|(broken?1U<<21:0)|(p.damaged?1U<<22:0)|(shield_action?1U<<23:0)|(w.lent?1U<<24:0);
        static uint32_t last_key=0xFFFFFFFF;
        if(key!=last_key){
            last_key=key;
            const char* why=!gameplay?"no_gameplay":p.damaged?"damaged":scene?"scene":mortal::dispatch_hooks::runtime.active?"mortal_draw":
                g_oca_session.state()!=oca::State::Idle?"ocarina":variant==sg::ClipVariant::None?"no_variant":mismatch?"variant_flip":"-";
            CmbLog("shield_guard state weapon=0x%02X lock=%u knife=%u routine=0x%08X supported=%u wanted=%u arm_ik=%d motion=0x%08X "
                "variant=%s blocked=%u why=%s want=%s shown=%s phase=%s weight=%.2f action=%u lent=%u broken=%u",
                weapon,lock?1U:0U,knife?1U:0U,p.routine,g_sg_supported?1U:0U,g_sg_wanted?1U:0U,arm_ik,motion,sg::VariantName(variant),w.blocked?1U:0U,
                w.blocked?why:"-",sg::PlacementName(want),sg::PlacementName(g_sg_shown),sg::PhaseName(g_sg_layer->phase()),
                g_sg_layer->Weight(),shield_action?1U:0U,w.lent?1U:0U,broken?1U:0U);
        }
    }
    if(costume_ok)GuardPlace(want,costume);
    else if(g_sg_swap.Holding())GuardPlace(sg::Placement::Back,0);
    InterlockedExchange(&link_runtime::shield::runtime::rear_suppressed,sg::RearAllowed(true,g_sg_shown,broken)?0:1);
    if(costume_ok)g_sg_costume=costume;
    g_sg_room=room;g_sg_player=p.player;
}
// Tier-3 sources: tier 2's native reads, plus the native front's forward vector and the attacker position.
struct GuardSources {
    link_runtime::parry::general::NativeSources base{g_gen};
    bool Invocation(const link_runtime::parry::Candidate& c){return base.Invocation(c);}
    uint32_t ImageBase(){return base.ImageBase();}
    bool Live(size_t i){return base.Live(i);}
    bool Descriptor(uint32_t a,link_runtime::parry::general::Descriptor& d){return base.Descriptor(a,d);}
    bool Front(const link_runtime::parry::Candidate& c,link_runtime::parry::general::Front& f,float (&forward)[3]){
        namespace pp=link_runtime::parry;
        link_runtime::parry::general::Memory m;uint32_t player=0;float pa[6]{};pp::owner::Observation o;
        if(!m.Read(c.current,o.current.data(),12)||!m.Read(c.old,o.previous.data(),12)||
           !m.Read(g_gen.access.image_base+static_cast<uint32_t>(g_gen.access.code.player_slot_rva),&player,4)||
           !link_runtime::shield::Range(player,0xAC)||!m.Read(player+0x94,pa,sizeof(pa)))return false;
        pp::front_math::Result res;
        if(!pp::front_native::Evaluate(g_gen.front,g_gen.access,o,pa,pa+3,res,forward))return false;
        std::memcpy(f.sample,o.current.data(),12);std::memcpy(f.player,pa,12);
        f.frontal=res.frontal;f.dot=res.dot;
        // Unit length along Link's horizontal forward, as general::NativeSources::Front (the cone reads forward, not half).
        const float flat=std::sqrt(forward[0]*forward[0]+forward[2]*forward[2]);
        f.half=(forward[0]*(f.sample[0]-pa[0])+forward[2]*(f.sample[2]-pa[2]))/(flat>1e-3f?flat:1.f);
        return std::isfinite(f.half);
    }
    uint32_t Owner(const link_runtime::parry::Candidate& c){return base.Owner(c);}
    bool Routine(uint32_t owner,uint8_t (&out)[2]){return base.Routine(owner,out);}
    bool OwnerPosition(uint32_t owner,float (&out)[3]){link_runtime::parry::general::Memory m;return m.Read(owner+0x94,out,12)&&std::isfinite(out[0])&&std::isfinite(out[2]);}
    uint64_t Now(){return GetTickCount64();}
};
static bool GuardResolve(const link_runtime::parry::Candidate& c){
    namespace g=link_runtime::parry::general;
    if(!g_sg_on||!g_sg_layer||!g_gen.code.verified||!g_gen.front.verified)return false;
    GuardSources s;sg::Outcome o;
    const bool up=g_sg_wanted&&g_sg_layer->Up();
    const unsigned costume=g_sg_costume;
    const bool cancel=sg::Run(s,c,g_gen.settings,g_sg_follow,g_sg_dur,g_sg_supported,up,g_sg_clock.Now(),o);
    if(o.follow)return cancel;   // same swing: silent
    if(!cancel){
        // Rejections worth seeing while the guard is up (1/s per reason).
        const unsigned key=static_cast<unsigned>(o.reason);const uint64_t now=GetTickCount64();
        if((up||o.reason==sg::Reason::Broken)&&o.reason!=sg::Reason::Structure&&key<16&&now-g_sg_last_pass[key]>=1000){
            g_sg_last_pass[key]=now;
            CmbLog("shield_guard pass reason=%s producer=0x%X class=%c verdict=%s cos=%.3f owner_source=%u left=%u owner=0x%08X",sg::ReasonName(o.reason),
                o.ret_rva,o.cls,g::VerdictName(o.verdict),o.cos,o.owner_source?1U:0U,g_sg_dur.Left(),o.owner);
        }
        return false;
    }
    const unsigned sparks=link_runtime::parry::spark::EmitAt(g_gen.sparks,c,o.front.sample,o.front.sample);
    int deflected=-1;
    if(o.cls=='T'){
        deflected=0;g::Memory m;uint32_t object=0;
        if(g_gen.deflect&&g::thrown::Identify(m,g_gen.access.image_base,c,o.ret_rva,object)&&g::thrown::Deflect(m,object,o.front.player))deflected=1;
    }
    const bool wood=costume==0;
    if(o.broke){
        g_sg_layer->OnBroken();GuardSound(wood?3:4);
        CmbLog("shield_guard break costume=%u regen_s=%.0f",costume,g_sg_dur.RegenFrames()/60.0);
    }else{g_sg_layer->OnBlocked();GuardSound(wood?1:2);}
    CmbLog("shield_guard block producer=0x%X class=%c power=%u cos=%.3f owner_source=%u left=%u/%u broke=%u sparks=%u deflect=%d owner=0x%08X",
        o.ret_rva,o.cls,o.descriptor.power,o.cos,o.owner_source?1U:0U,o.left,g_sg_dur.Max(),o.broke?1U:0U,sparks,deflected,o.owner);
    return true;
}
// Worker: the block reuses the rear shield's TP hit sounds; break and return are their own files.
static AlertVoice* g_sg_voice[5]{};
static void GuardLoadAudio(const wchar_t* directory){
    if(!g_sg_on)return;
    struct Row{const wchar_t* name;const char* sha;};
    const Row rows[5]={{L"link_rear_shield_wood.wav",sga::kRearWoodSha},{L"link_rear_shield_metal.wav",sga::kRearMetalSha},
        {sga::kBreakWood.name,sga::kBreakWood.sha},{sga::kBreakMetal.name,sga::kBreakMetal.sha},{sga::kBack.name,sga::kBack.sha}};
    unsigned loaded=0;
    for(unsigned i=0;i<5;++i){
        wchar_t path[MAX_PATH]{};if(link_runtime::paths::Format(path,L"%s%s",directory,rows[i].name)<=0)continue;
        auto* v=new AlertVoice();
        if(v->Load(path,60,rows[i].sha)){g_sg_voice[i]=v;++loaded;}else delete v;
    }
    Log("ShieldGuard audio loaded=%u/5",loaded);
}
static void GuardPollAudio(){
    unsigned r=g_sg_sound_r.load(std::memory_order_relaxed);const unsigned w=g_sg_sound_w.load(std::memory_order_acquire);
    while(r!=w){
        const uint8_t code=g_sg_sounds[r%16];g_sg_sound_r.store(++r,std::memory_order_release);
        const bool played=code>=1&&code<=5&&g_sg_voice[code-1]&&g_sg_voice[code-1]->Play();
        Log("shield_guard sound code=%u submitted=%u",code,played?1U:0U);
    }
}
// ---- riposte ------------------------------------------------------------------------------------
static prp::Riposte* g_rip=nullptr;
static uint8_t g_rip_id=0,g_rip_type=0;
static nhp::Verdict g_rip_verdict=nhp::Verdict::Empty;
struct CmbRipostePort {
    bool ReadTarget(uint32_t actor,prp::Target& t){
        CmbMem m;nhp::Ranges r;r.image_base=g_cmb_base;
        if(!nhp::ReadPool(m,g_cmb_base,r))return false;
        const nhp::Source s(r);
        t=prp::Target{};
        t.valid=s.ActorAddress(actor)==nhp::Verdict::Ok&&s.ActorLive(m,actor,nullptr)==nhp::Verdict::Ok;
        return m.Read(actor+0x100,&t.id,1)&&m.Read(actor+0x101,&t.type,1)&&m.Read(actor+0x324,&t.hp,2)&&
               m.Read(actor+0x32C,&t.dmg_flags,1)&&m.Read(actor+0x32D,&t.dmg_timer,1);
    }
    bool ResolveHitPart(uint32_t actor,prp::HitPart& out){
        CmbMem m;CmbPlayer p;if(!CmbReadPlayer(p))return false;
        float from[3]{};CmbFrom(p.pos,from);
        const nhp::Source s=nhp::ManagedSource(m,g_cmb_base);
        const nhp::Expect owner{true,g_rip_id,g_rip_type};
        return prp::ResolveHitPart(s,m,actor,from,out,&g_rip_verdict,&owner);
    }
};
// ---- contextual Mortal Draw -----------------------------------------------------------------------
static mdr::Controller* g_cmb_md=nullptr;
static mtg::Selection g_cmb_sel;
struct CmbObserveState {uint64_t identity=0;unsigned reject=0,block=0,gate=0;bool available=false,stagger=false,window=false;uint32_t census_frame=0;char census[400]{};};
// Magnetism of the current action (log): Link's yaw before and the yaw written at the start.
struct CmbFaceState {uint64_t lease=0;bool written=false;float before=0,set=0;};
static CmbFaceState g_cmb_face;
// Magnet (mortal_draw_magnet.h): per-frame facing and the contact clamp of the clip's lunge, applied
// in the MotionMove detour right after the dispatch's own api.move of our lease. Null when off.
static mmag::Config g_cmb_magnet_cfg;
static mmag::Magnet* g_cmb_magnet=nullptr;
static void CmbMagnetLog(const char* when){
    char line[sizeof(CmbLine::text)];
    if(mmag::Format(line,sizeof(line),when,g_cmb_magnet->config(),g_cmb_magnet->summary())>0)CmbLog("%s",line);
}
static CmbObserveState g_cmb_obs;
static bool SwordBusy();
static mdr::Gate CmbGate(){
    if(g_oca_session.state()!=oca::State::Idle)return mdr::Gate::Ocarina;
    if(g_rx&&g_rx->Active())return mdr::Gate::Reaction;
    if(SwordBusy())return mdr::Gate::Busy;
    if(g_cmb_md&&g_cmb_md->Active())return mdr::Gate::Active;
    if(mortal::dispatch_hooks::runtime.active)return mdr::Gate::Busy;
    return mdr::Gate::None;
}
// MortalDrawObserve census: id/type, routine, HP and the verdict of the nearest em slots (the first
// in-game test confirms the ID list from this line). Logged when it changes, at most every 30 frames.
static void CmbCensus(uint32_t frame,const CmbPlayer& p){
    if(frame-g_cmb_obs.census_frame<30)return;
    g_cmb_obs.census_frame=frame;
    CmbMem m;mtg::Manager manager;const mtg::Config c=CmbTargetConfig();
    if(mtg::ReadManager(m,g_cmb_base,c,manager)!=mtg::Reject::None)return;
    struct Row {float d;uint32_t i;mtg::Raw r;mtg::Reject why;float cosine;};
    Row rows[6];unsigned n=0;
    for(uint32_t i=0;i<manager.count;++i){
        mtg::Raw r;if(mtg::ReadRaw(m,manager.array+i*manager.block,r)!=mtg::Reject::None||(r.be_flag&0x201)!=1)continue;
        const float dx=p.pos.x-r.pos.x,dz=p.pos.z-r.pos.z,d=std::sqrt(dx*dx+dz*dz);
        if(!(d<4000.f))continue;
        const mtg::Unit u=mtg::Classify(r,g_cmb_base,c,g_cmb_stagger.Actor()==manager.array+i*manager.block);
        const mtg::Geometry g=mtg::Measure(p.pos,r,c,mtg::Line{});
        const mtg::Reject why=u.actor!=mtg::Reject::None?u.actor:u.surprise!=mtg::Reject::None?u.surprise:g.reject;
        unsigned at=n<6?n++:6;
        if(at==6){unsigned worst=0;for(unsigned k=1;k<6;++k)if(rows[k].d>rows[worst].d)worst=k;if(rows[worst].d<=d)continue;at=worst;}
        rows[at]={d,i,r,why,g.cosine};
    }
    char text[400]{};int k=0;
    for(unsigned j=0;j<n&&k>=0&&k<static_cast<int>(sizeof(text));++j){
        const Row& w=rows[j];
        k+=_snprintf_s(text+k,sizeof(text)-k,_TRUNCATE," [%u id=0x%02X type=0x%02X r=%u/0x%02X hp=%d/%d d=%.0f cos=%.2f vt=%u why=%s]",
            w.i,w.r.id,w.r.type,w.r.r0,w.r.r1,w.r.hp,w.r.hp_max,w.d,w.cosine,w.r.vtable==g_cmb_base+mtg::layout::em10_vtable_rva?1U:0U,mdr::RejectName(w.why));
    }
    if(std::strcmp(text,g_cmb_obs.census)){
        std::memcpy(g_cmb_obs.census,text,sizeof(text));
        CmbLogV("md_census frame=%u room=0x%X n=%u%s",frame,g_cmb_room,n,text);
    }
}
// Mortal Draw prompt (mortal_draw_prompt.h, docs/mortal-draw-prompt-2026-10-12.md, option a): a native
// cActionButton offer with func NULL, queued at the arbiter entry only when nothing native is queued;
// the arbiter draws "[E] MORTAL DRAW" (message 0x16+kind) and never runs anything for it. Game thread.
namespace mdp=link_runtime::mortal_draw::prompt;
struct CmbPromptCfg {bool on=false,mask_check=true;mdp::KeyChoice key;};
static CmbPromptCfg g_cmb_prompt_cfg;
static mdp::Prompt g_cmb_prompt;
static mdp::DedicatedKey g_cmb_key;
static mdp::OfferArgs g_cmb_offer;
static mdp::Why g_cmb_prompt_why=mdp::Why::Off,g_cmb_prompt_logged_why=mdp::Why::Off;
static bool g_cmb_prompt_logged_shown=false;
static uint8_t g_cmb_prompt_max_native=0;
using CmbActSet=void(__thiscall*)(void*,int,int,void*,void*,int,int,int,void*);
static bool CmbPromptSet(void* manager,const mdp::OfferArgs& a){
    __try{
        reinterpret_cast<CmbActSet>(g_cmb_base+mdp::layout::set_rva)(manager,a.kind,a.slot,nullptr,nullptr,static_cast<int>(a.flags),a.btn,a.type,nullptr);
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void CmbPromptEntry(void* self){
    CmbMem m;mdp::EntryFacts e;
    if(!mdp::ReadEntry(m,g_cmb_base,e))e.readable=false;
    g_cmb_prompt_max_native=e.max_native_kind;
    if(e.max_native_kind>=mdp::layout::kind_action){   // a native offer uses our message range: stop for good
        g_cmb_prompt_cfg.on=false;g_cmb_prompt.Cancel();g_cmb_prompt_why=mdp::Why::Off;
        CmbLog("md_prompt disabled: native offer kind=0x%02X >= 0x%02X uses the prompt's message range",e.max_native_kind,mdp::layout::kind_action);
        return;
    }
    mdp::Why why=g_cmb_prompt.Entry(e,g_cmb_md!=nullptr);
    if(why==mdp::Why::Shown){
        const uint32_t manager=g_cmb_base+mctx::layout::action_manager_rva;
        if(reinterpret_cast<uint32_t>(self)!=manager||!CmbPromptSet(self,g_cmb_offer)||!mdp::ReadOwnWork(m,g_cmb_base,g_cmb_offer)){
            g_cmb_prompt.Cancel();why=mdp::Why::Unreadable;
        }
    }
    g_cmb_prompt_why=why;
}
static void CmbPromptLog(uint32_t frame,bool displayed){
    if(g_cmb_prompt_why==g_cmb_prompt_logged_why&&displayed==g_cmb_prompt_logged_shown)return;
    g_cmb_prompt_logged_why=g_cmb_prompt_why;g_cmb_prompt_logged_shown=displayed;
    CmbLog("md_prompt frame=%u why=%s shown=%u kind=0x%02X key=%s native_max_kind=0x%02X",frame,mdp::WhyName(g_cmb_prompt_why),
        displayed?1U:0U,g_cmb_offer.kind,g_cmb_prompt_cfg.key.mode==mdp::KeyMode::Action?"action":"dedicated",g_cmb_prompt_max_native);
}
#include "spin_attack_runtime.h"
#include "zelda_cast_pose_runtime.h"   // Zelda's cast pose (zelda-cast-pose): arm, stop, hand
#include "zelda_light_runtime.h"   // Zelda's light ray (pl_type 1), called once per player dispatch
static void CmbArbiterExit(bool pre,mctx::Sample sample){
    CmbMem m;
    if(!pre||!mctx::ReadPostArbiter(m,g_cmb_base,sample))sample.readable=sample.after_arbiter=false;  // N-3
    const uint32_t frame=CmbFrame();sample.frame=frame;
    CmbPlayer p;const bool player=CmbReadPlayer(p);
    if(player)CmbScene(p.player);
    sample.player_free=player&&p.facts.player_free;sample.sword_sheathed=player&&p.facts.sword_sheathed;
    SwordArbiter(sample,player,p);
    g_cmb_stagger.Tick(GetTickCount64());
    g_cmb_sel=player&&p.facts.gameplay?mtg::Select(m,g_cmb_base,g_cmb_tracker,p.pos,CmbTargetConfig(),mtg::Line{},&g_cmb_stagger):mtg::Selection{};
    if(g_cmb.hitpart_observe&&g_cmb_sel.found&&g_cmb_sel.identity!=g_cmb_hp_identity){
        g_cmb_hp_identity=g_cmb_sel.identity;CmbObserveHitPart(g_cmb_sel.pointer,"mortal_target");
    }
    if(!g_cmb_md)return;
    const bool prompt=g_cmb_prompt_cfg.on;
    if(prompt){
        CmbPromptLog(frame,g_cmb_prompt.Displayed(sample.active));
        mdp::ClaimOwnOffer(sample,g_cmb_prompt.Injected());   // our NULL-func offer is not a native one
        if(g_cmb_prompt_cfg.key.mode==mdp::KeyMode::Physical){
            DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
            sample=mdp::WithDedicatedKey(sample,g_cmb_key.Step((GetAsyncKeyState(g_cmb_prompt_cfg.key.vk)&0x8000)!=0,false,
                foreground==GetCurrentProcessId()));
        }
    }
    const mdr::Gate gate=CmbGate();
    const double now=GameNow(g_cmb_global_slot);
    const mctx::Decision d=g_cmb_md->Arbiter(sample,g_cmb_sel,gate,now);
    if(prompt)g_cmb_prompt.Exit(d,g_cmb_md->Active(),now);
    bool masked=false;
    if(d.mask_trg)masked=prompt?mdp::MaskTrg(m,g_cmb_base,mdp::MaskBits(g_cmb_prompt_cfg.key.mode,d,g_cmb_prompt_cfg.mask_check))
                               :mctx::MaskPress(m,g_cmb_base);
    // stagger = picked as the parried owner in its window; window = armed, owner, serial, ms left.
    if(d.press)
        CmbLog("md_press frame=%u target=%llu actor=0x%08X id=0x%02X type=0x%02X r1=0x%02X d=%.0f dy=%.0f cos=%.2f stagger=%u "
               "window=%u owner=0x%08X serial=%u rem_ms=%u contextual=%u masked=%u buffered=%u",
            frame,g_cmb_sel.identity,g_cmb_sel.pointer,g_cmb_sel.raw.id,g_cmb_sel.raw.type,g_cmb_sel.raw.r1,g_cmb_sel.horizontal,
            g_cmb_sel.vertical,g_cmb_sel.cosine,g_cmb_sel.stagger?1U:0U,g_cmb_stagger.Armed()?1U:0U,g_cmb_stagger.Actor(),
            g_cmb_stagger.Serial(),g_cmb_stagger.RemainingMs(),g_cmb.md_contextual?1U:0U,masked?1U:0U,d.buffered?1U:0U);
    else if(d.buffered)CmbLog("md_press buffered frame=%u block=%s window=%u rem_ms=%u",frame,mdr::BlockName(d.block),
            g_cmb_stagger.Armed()?1U:0U,g_cmb_stagger.RemainingMs());
    if(g_cmb.md_observe){
        auto& o=g_cmb_obs;
        if(o.identity!=g_cmb_sel.identity||o.reject!=static_cast<unsigned>(g_cmb_sel.reject)||o.block!=static_cast<unsigned>(d.block)||
           o.gate!=static_cast<unsigned>(gate)||o.available!=d.available||o.stagger!=g_cmb_sel.stagger||o.window!=g_cmb_stagger.Armed()){
            o.identity=g_cmb_sel.identity;o.reject=static_cast<unsigned>(g_cmb_sel.reject);o.block=static_cast<unsigned>(d.block);
            o.gate=static_cast<unsigned>(gate);o.available=d.available;o.stagger=g_cmb_sel.stagger;o.window=g_cmb_stagger.Armed();
            const auto& ws=g_cmb_stagger.stats();
            CmbLogV("md_observe frame=%u prompt=%u block=%s gate=%s target=%llu actor=0x%08X id=0x%02X type=0x%02X r1=0x%02X "
                   "plaga_timer=%.1f d=%.0f dy=%.0f cos=%.2f eligible=%u examined=%u reject=%s offers=%u lock=%u active=%u "
                   "free=%u sheathed=%u on=0x%llX pre=%u stagger=%u window=%u owner=0x%08X serial=%u rem_ms=%u window_end=%u "
                   "admitted=%u expired=%u left=%u",
                frame,d.available?1U:0U,mdr::BlockName(d.block),mdr::GateName(gate),g_cmb_sel.identity,g_cmb_sel.pointer,
                g_cmb_sel.raw.id,g_cmb_sel.raw.type,g_cmb_sel.raw.r1,g_cmb_sel.raw.plaga_timer,g_cmb_sel.horizontal,g_cmb_sel.vertical,
                g_cmb_sel.cosine,g_cmb_sel.eligible,g_cmb_sel.examined,mdr::RejectName(g_cmb_sel.reject),sample.offers,sample.lock,
                sample.active,sample.player_free?1U:0U,sample.sword_sheathed?1U:0U,sample.on,pre?1U:0U,g_cmb_sel.stagger?1U:0U,
                g_cmb_stagger.Armed()?1U:0U,g_cmb_stagger.Actor(),g_cmb_stagger.Serial(),g_cmb_stagger.RemainingMs(),
                static_cast<unsigned>(ws.last),ws.admitted,ws.expired,ws.left);
        }
        if(player)CmbCensus(frame,p);
    }
}
using CmbArbiterFn=void(__thiscall*)(void*);
static CmbArbiterFn g_cmb_arbiter_original=nullptr;
static void __fastcall CmbArbiterDetour(void* self,void*){
    const DWORD error=GetLastError();
    CmbMem m;mctx::Sample sample;const bool pre=mctx::ReadPreArbiter(m,g_cmb_base,sample);
    if(g_cmb_prompt_cfg.on)CmbPromptEntry(self);   // after the native sample: its offers count only native works
    SetLastError(error);
    g_cmb_arbiter_original(self);
    const DWORD after=GetLastError();
    CmbArbiterExit(pre,sample);
    SetLastError(after);
}
static void CmbStrike(uint32_t frame){
    CmbPlayer p;CmbMem m;
    if(!CmbReadPlayer(p))p=CmbPlayer{};
    const mtg::Selection& sel=g_cmb_md->Target();
    const mtg::PressLatch& latch=g_cmb_md->Latch();
    const mtg::Config cfg=CmbTargetConfig();
    // Strike tolerance (reach, rear and stagger latched from the start); identity/actor/vertical as before.
    const mtg::Recheck v=mtg::RevalidateStrike(m,g_cmb_base,g_cmb_tracker,sel,latch,p.pos,cfg,mtg::Line{},&g_cmb_stagger);
    const link_runtime::mortal_draw::Snapshot snap=mdr::MakeSnapshot(sel,v,g_cmb_tracker,p.facts);
    mdmg::HitTarget t;t.identity=sel.identity;t.actor=sel.pointer;CmbFrom(p.pos,t.from);
    g_cmb_mortal_same=v.same;g_cmb_mortal_identity=sel.identity;g_cmb_caller=CmbCaller::Mortal;
    // 16-oct damage limit: a lethal cut on a heavy target (max HP > 1500, chainsaw or claw type) removes at most 1000 HP.
    const int cap=v.same?mtg::LethalCap(v.raw):0;
    const auto effect=g_cmb_md->Strike(snap,g_cmb_adapter,t,cap);
    const bool capped=effect==link_runtime::mortal_draw::Effect::LethalStrike&&mdmg::Capped(v.raw.hp,cap);
    g_cmb_caller=CmbCaller::None;
    const auto& s=g_cmb_md->stats();
    // d/dy/cos now vs press_d/press_cos at the start; reach = strike tolerance; rear_latched = turned
    // during the windup but kept rear by the start latch; stagger_latched = owner kept after the window.
    const float yaw_now=CmbGet<float>(p.player+0xA4);
    CmbLog("md_strike frame=%u lease=%llu target=%llu actor=0x%08X id=0x%02X type=0x%02X r0=%u r1=0x%02X effect=%s same=%u "
           "actor_ok=%s surprise=%s geometry=%s d=%.0f dy=%.0f cos=%.2f reach=%.0f press_d=%.0f press_cos=%.2f latch=%u "
           "rear_latched=%u stagger=%u stagger_latched=%u window=%u rem_ms=%u window_end=%u yaw_set=%.3f yaw_now=%.3f "
           "pos=(%.0f,%.0f,%.0f) hp=%d hp_max=%d heavy=%u cap=%d capped=%u damage=%u status=%s token=%llu plaga_timer=%.1f counter_7bc=%u",
        frame,g_cmb_md->Lease(),sel.identity,sel.pointer,v.raw.id,v.raw.type,v.raw.r0,v.raw.r1,mdr::EffectName(effect),v.same?1U:0U,
        mdr::RejectName(v.actor),mdr::RejectName(v.surprise),mdr::RejectName(v.geometry),v.horizontal,v.vertical,v.cosine,
        cfg.strike_max_horizontal>cfg.max_horizontal?cfg.strike_max_horizontal:cfg.max_horizontal,latch.horizontal,latch.cosine,
        latch.valid?1U:0U,v.rear_latched?1U:0U,v.stagger?1U:0U,v.stagger_latched?1U:0U,g_cmb_stagger.Armed()?1U:0U,
        g_cmb_stagger.RemainingMs(),static_cast<unsigned>(g_cmb_stagger.stats().last),
        g_cmb_face.written&&g_cmb_face.lease==g_cmb_md->Lease()?g_cmb_face.set:yaw_now,yaw_now,p.pos.x,p.pos.y,p.pos.z,
        v.raw.hp,v.raw.hp_max,cap?1U:0U,cap,capped?1U:0U,g_cmb.md_damage?1U:0U,
        g_cmb.md_damage&&(effect==link_runtime::mortal_draw::Effect::LethalStrike||effect==link_runtime::mortal_draw::Effect::OrdinaryStrike)?mdr::StatusName(s.last_status):"-",
        s.last_token,v.raw.plaga_timer,static_cast<unsigned>(CmbGet<uint32_t>(sel.pointer+0x7BC)));
    if(g_cmb_magnet&&g_cmb_magnet->summary().lease==g_cmb_md->Lease()){
        mmag::Vec2 target;const bool target_ok=mmag::ReadTarget(m,sel.pointer,sel.raw.id,sel.raw.type,target);
        g_cmb_magnet->NoteStrike(g_cmb_md->Lease(),{p.pos.x,p.pos.z},yaw_now,target,target_ok);
        CmbMagnetLog("strike");
    }
}
// Player handler (inside the dispatch, ocarina idle): strike timing and the start of a request.
static void CombatDecide(mortal::dispatch::Runtime& run,void* player){
    if(!g_cmb_md)return;
    const uint32_t frame=CmbFrame();
    const double now=GameNow(g_cmb_global_slot);   // StrikeTick and the request budget: 60 Hz frames
    if(g_cmb_adapter_on)g_cmb_md->Settle(g_cmb_adapter);
    if(g_cmb_md->Active()){
        if(run.active&&run.lease.token==g_cmb_md->Lease()&&g_cmb_md->StrikeDue(now))CmbStrike(frame);
        return;
    }
    mdr::Request request;
    if(!g_cmb_md->TakeRequest(now,request))return;
    if(run.active){g_cmb_md->DropRequest();CmbLog("md_start refused=dispatch_busy frame=%u",frame);return;}
    if(CmbGate()!=mdr::Gate::None){g_cmb_md->DropRequest();CmbLog("md_start refused=%s frame=%u",mdr::GateName(CmbGate()),frame);return;}
    const uint32_t routine=CmbGet<uint32_t>(reinterpret_cast<uint32_t>(player)+0xFC);
    if(!CmbStartRoutine(routine))return;   // not idle (or walk/run/turn when pinned): wait (the request expires)
    CmbPlayer p;CmbMem m;
    if(!CmbReadPlayer(p)||p.player!=reinterpret_cast<uint32_t>(player)){g_cmb_md->DropRequest();return;}
    const mtg::Config cfg=CmbTargetConfig();
    const mtg::Recheck v=mtg::Revalidate(m,g_cmb_base,g_cmb_tracker,request.sel,p.pos,cfg,mtg::Line{},&g_cmb_stagger);
    const auto snap=mdr::MakeSnapshot(request.sel,v,g_cmb_tracker,p.facts);
    if(!g_cmb_md->Admit(snap)){
        CmbLog("md_start refused=policy frame=%u same=%u actor=%s surprise=%s geometry=%s free=%u sheathed=%u",frame,v.same?1U:0U,
            mdr::RejectName(v.actor),mdr::RejectName(v.surprise),mdr::RejectName(v.geometry),p.facts.player_free?1U:0U,p.facts.sword_sheathed?1U:0U);
        return;
    }
    const auto lease=mortal::resident_assets::Acquire();
    const auto result=lease.token?mortal::trial::TryStartClip(run,g_mortal_native,lease,
        g_cmb_move_start?mortal::dispatch::StartFrom::NormalMove:mortal::dispatch::StartFrom::NormalIdle):
                                  mortal::trial::Attempt{mortal::trial::Status::AssetsMissing};
    if(result.status!=mortal::trial::Status::Started){
        g_cmb_md->StartFailed();CmbLog("md_start failed status=%u frame=%u",static_cast<unsigned>(result.status),frame);return;
    }
    const mtg::PressLatch latch=mtg::Latch(request.sel,v,cfg);
    g_cmb_md->Started(lease.token,now,request.sel,latch);
    link_runtime::parry::input::SetMortalBlocks(true);
    // Magnetism (MortalDrawFaceTarget=1): face the target now, on the start frame, so the clip's
    // root-motion lunge (~1.2-1.5 m, log 12-oct) carries Link to it. Only the native yaw +0xA4 of
    // the player model is written, once, after the clip started. No translation: there is no
    // attributed collision query, so a scripted lunge could pass through walls.
    g_cmb_face=CmbFaceState{};g_cmb_face.lease=lease.token;
    const uint32_t pl=reinterpret_cast<uint32_t>(player);
    float yaw=0.f,before=0.f;CmbMem w;
    const bool have_before=w.Read(pl+0xA4,&before,4)&&std::isfinite(before);
    // With the magnet the turn is spread over the windup (rate-limited, tracking the target); the
    // one-shot snap below stays as the fallback when the magnet is off or cannot begin.
    const bool magnet=g_cmb_magnet&&have_before&&
        g_cmb_magnet->Begin(lease.token,frame,before,{p.pos.x,p.pos.z},{v.raw.pos.x,v.raw.pos.z},now);
    if(!magnet&&g_cmb.face_target&&have_before&&mtg::FaceYaw(p.pos,v.raw.pos,yaw)&&w.Write(pl+0xA4,&yaw,4)){
        g_cmb_face.written=true;g_cmb_face.before=before;g_cmb_face.set=yaw;
    }
    CmbLog("md_start started lease=%llu frame=%u request_frame=%llu target=%llu actor=0x%08X id=0x%02X r1=0x%02X d=%.0f dy=%.0f "
           "cos=%.2f stagger=%u latch=%u latch_rear=%u latch_stagger=%u face=%u yaw_before=%.3f yaw_set=%.3f turn=%.3f "
           "pos=(%.0f,%.0f,%.0f) target_pos=(%.0f,%.0f,%.0f) strike_tick=%u reach=%.0f window=%u rem_ms=%u "
           "magnet=%u magnet_turn=%.3f contact=%.0f",
        lease.token,frame,request.frame,request.sel.identity,request.sel.pointer,request.sel.raw.id,v.raw.r1,v.horizontal,v.vertical,
        v.cosine,v.stagger?1U:0U,latch.valid?1U:0U,latch.rear?1U:0U,latch.stagger?1U:0U,g_cmb_face.written?1U:0U,before,
        g_cmb_face.written?yaw:before,g_cmb_face.written?mtg::Turn(before,yaw):0.f,p.pos.x,p.pos.y,p.pos.z,
        v.raw.pos.x,v.raw.pos.y,v.raw.pos.z,g_cmb.strike_tick,
        cfg.strike_max_horizontal>cfg.max_horizontal?cfg.strike_max_horizontal:cfg.max_horizontal,
        g_cmb_stagger.Armed()?1U:0U,g_cmb_stagger.RemainingMs(),magnet?1U:0U,magnet?g_cmb_magnet->summary().start_turn:0.f,
        g_cmb_magnet?g_cmb_magnet->config().contact:0.f);
}
static void CombatLifecycle(mortal::dispatch::Event event,uint64_t token){
    if(!g_cmb_md||event==mortal::dispatch::Event::Started||!g_cmb_md->Active()||token!=g_cmb_md->Lease())return;
    g_cmb_md->Ended(token,event==mortal::dispatch::Event::Finished,g_cmb_adapter);
    if(g_cmb_magnet&&g_cmb_magnet->End(token))CmbMagnetLog("end");
    link_runtime::parry::input::SetMortalBlocks(false);
    const auto& s=g_cmb_md->stats();
    CmbLog("md_end lease=%llu event=%u finished=%u interrupted=%u early_end=%u last_effect=%s outcome=%s",
        token,static_cast<unsigned>(event),s.finished,s.interrupted,s.early_end,mdr::EffectName(s.last_effect),mdr::OutcomeName(s.last_outcome));
}
// ---- per-frame pump and the MotionMove / neck detours ------------------------------------------
static uint32_t g_cmb_pump_frame=0xFFFFFFFF,g_cmb_stats_frame=0;
static char g_cmb_stats_last[720]{};
static void CmbStats(uint32_t frame){
    if(frame-g_cmb_stats_frame<300)return;
    g_cmb_stats_frame=frame;char text[720]{};int k=0;
    if(g_cmb_md){const auto& s=g_cmb_md->stats();
        k+=_snprintf_s(text+k,sizeof(text)-k,_TRUNCATE,"md presses=%u requests=%u expired=%u refused=%u starts=%u start_failed=%u strikes=%u lethal=%u ordinary=%u published=%u applied=%u unconfirmed=%u; ",
            s.presses,s.requests,s.expired,s.refused,s.starts,s.start_failed,s.strikes,s.lethal,s.ordinary,s.published,s.applied,s.unconfirmed);}
    if(g_rx&&k>=0){const auto& s=g_rx->stats();
        k+=_snprintf_s(text+k,sizeof(text)-k,_TRUNCATE,"reaction starts=%u completed=%u cancelled=%u lost_owner=%u dropped=%u refused=%u install_failed=%u overlap=%u foreign_weight=%u timeouts=%u neck_suppressed=%u repeated_frame=%u cleared=%u owner_id=0x%X; ",
            s.starts,s.completed,s.cancelled,s.lost_owner,s.dropped,s.refused,s.install_failed,s.ignored_overlap,s.foreign_weight,s.timeouts,s.neck_suppressed,s.repeated_frame,s.cleared,s.owner_id);}
    if(k>=0){const auto& s=g_slot_dog.stats();   // slot-guard-01
        if(s.releases||s.foreign)k+=_snprintf_s(text+k,sizeof(text)-k,_TRUNCATE,"slot releases=%u stale_stance=%u orphan_reaction=%u orphan_guard=%u foreign=%u; ",
            s.releases,s.stale_stance,s.orphan_reaction,s.orphan_guard,s.foreign);}
    if(k>=0&&g_hist_resets)k+=_snprintf_s(text+k,sizeof(text)-k,_TRUNCATE,"hist_resets=%u; ",g_hist_resets);   // arm-fix-01
    if(g_rip&&k>=0){const auto& s=g_rip->stats();
        k+=_snprintf_s(text+k,sizeof(text)-k,_TRUNCATE,"riposte armed=%u published=%u applied=%u unconfirmed=%u expired=%u busy=%u no_hitpart=%u no_record=%u identity=%u dead=%u excluded=%u rejected=%u lost=%u owner_id=0x%X last_status=%s last_outcome=%s verdict=%s expired_flags=0x%X/0x%X",
            s.armed,s.published,s.applied,s.unconfirmed,s.expired,s.busy,s.no_hitpart,s.no_record,s.identity,s.dead,s.excluded,s.rejected,s.lost,
            s.owner_id,mdr::StatusName(s.last_status),mdr::OutcomeName(s.last_outcome),nhp::Name(g_rip_verdict),s.expired_flags,s.expired_timer);}
    if(k>0&&std::strcmp(text,g_cmb_stats_last)){std::memcpy(g_cmb_stats_last,text,sizeof(text));CmbLogV("combat stats frame=%u %s",frame,text);}
}
static void CmbPump(uint32_t frame,const CmbPlayer& p,bool player){
    if(g_cmb_adapter_on){g_cmb_adapter.Tick();BashClaimsTick();}   // once per logic frame (N-1: with the from mark above)
    if(g_rip){
        CmbScene(player?p.player:0);
        if(!player||!p.facts.gameplay||p.damaged)g_rip->Cancel(g_cmb_adapter);   // damage/grab/death/scene: withdraw only
        else {CmbRipostePort port;g_cmb_caller=CmbCaller::Riposte;g_rip->Pump(GetTickCount64(),port,g_cmb_adapter);g_cmb_caller=CmbCaller::None;}
    }
    CmbStats(frame);
}
using CmbMoveFn=int(__cdecl*)(void*);
static CmbMoveFn g_cmb_move_original=nullptr;
static void CmbPlayerMove(void* model){
    const uint32_t frame=CmbFrame();
    CmbPlayer p;const bool player=CmbReadPlayer(p)&&p.player==reinterpret_cast<uint32_t>(model);
    if(frame!=g_cmb_pump_frame){g_cmb_pump_frame=frame;CmbPump(frame,p,player);}
    GuardTick(frame,p,player,model);   // shield-guard-01: before the reaction, which may borrow 2A8 from it
    SwordHandTick(p,player);           // sword-hand-01: hand the gun back the frame a sword action stops being valid
    if(!g_rx){SlotWatch(frame,p,player,model);return;}
    // Contract 1 (parry_reaction.h): the weight is written right before the original MotionMove.
    const uint32_t g=CmbGet<uint32_t>(g_cmb_global_slot);
    const uint8_t weapon=link_runtime::IsPointer(g)?CmbGet<uint8_t>(g+0x4FC0):0;
    const uint32_t room=link_runtime::IsPointer(g)?CmbGet<uint16_t>(g+0x4FAC):0xFFFF;
    if(!g_rx->Active()){g_rx_weapon=weapon;g_rx_room=room;}
    prx::World w;w.player=reinterpret_cast<uint32_t>(model);w.frame=frame;w.step=static_cast<float>(GameStep(g_cmb_global_slot));
    w.damaged=!player||p.damaged;w.grabbed=player&&(p.routine&0xFF)!=0;w.dead=!player||!p.facts.gameplay;
    w.scene=room!=g_rx_room;w.weapon_switch=weapon!=g_rx_weapon;w.image_base=g_cmb_base;
    w.mortal_draw=mortal::dispatch_hooks::runtime.active;w.ocarina=g_oca_session.state()!=oca::State::Idle;
    w.guest=g_sg_layer&&g_sg_layer->Active()?g_sg_layer->Work():0;
    w.stance_live=player&&SlotStanceLive(w.player);   // slot-guard-01: the exit gives the stance back only while it is live
    const bool was=g_rx->Active();CmbReactionPort port;g_rx->Tick(w,port);
    g_rx_last_tick=GetTickCount64();
    if(was!=g_rx->Active()){
        ArmTraceArm(frame);   // arm-trace: the whole reaction and 48 frames after its hand-back
        if(g_rx->Active())
            CmbLog("parry reaction start frame=%u owner_id=0x%X previous=0x%08X (%s) primary=0x%08X routine=0x%08X",frame,g_rx->stats().owner_id,
                g_rx->previous(),SlotKind(g_rx->previous(),w.player),CmbGet<uint32_t>(w.player+ssl::kPrimaryMotion),p.routine);
        else{
            // slot-guard-01: how it ended and what 2A8 went back to (a stale stance or guest is given back as 0).
            const auto& e=g_rx->last_exit();const uint32_t now=CmbGet<uint32_t>(w.player+ssl::kSlot);
            CmbLog("parry reaction end frame=%u owner_id=0x%X exit=%s previous=0x%08X (%s) restored=0x%08X (%s) wrote=%u cleared=%u stance=%s "
                "slot=0x%08X (%s) primary=0x%08X routine=0x%08X",frame,g_rx->stats().owner_id,prx::ExitName(e.exit),e.previous,
                SlotKind(e.previous,w.player),e.restored,SlotKind(e.restored,w.player),e.wrote?1U:0U,e.cleared?1U:0U,ssl::Name(SlotStance(w.player)),
                now,SlotKind(now,w.player),CmbGet<uint32_t>(w.player+ssl::kPrimaryMotion),p.routine);
        }
    }
    SlotWatch(frame,p,player,model);   // slot-guard-01: after both layers, right before the original MotionMove
}
// Magnet gate: this MotionMove is the dispatch's own api.move (0x25CFD0 -> thunk 0x6BF9 -> 0x262B90)
// inside Runtime::Handle, for the lease our controller and the magnet hold. Other models, other
// player MotionMove callers, the observe path and any foreign lease pass through untouched.
static bool CmbMagnetOwns(void* model){
    const auto& run=mortal::dispatch_hooks::runtime;
    if(!g_cmb_magnet||g_cmb_magnet->phase()==mmag::Phase::Off||!g_cmb_md||!g_cmb_md->Active())return false;
    const uint64_t lease=g_cmb_md->Lease();
    return run.busy&&!run.observing&&run.active&&run.current==static_cast<unsigned char*>(model)&&
        run.lease.token==lease&&g_cmb_magnet->lease()==lease&&run.Owns(run.current);
}
// After the original MotionMove (root delta added to +0x94 at 0x262D40, root rotation to +0xA0 at
// 0x262D54) and before cPlayer::move propagates the position into the matrices (0x25EDB0 at
// 0x3650D8) and runs collision (0x1955B0 at 0x36516E): filter this step, steer the yaw.
static void CmbMagnetAfterMove(uint32_t player,bool have_before,const mmag::Vec2& before){
    CmbMem m;mmag::Frame f;f.frame=CmbFrame();
    f.time=GameNow(g_cmb_global_slot);f.step=static_cast<float>(GameStep(g_cmb_global_slot));
    float yaw=0.f;
    const bool have_after=mmag::ReadPlayer(m,player,f.after,yaw);
    f.before=have_before&&have_after?before:mmag::Vec2{NAN,NAN};   // unreadable: Step stops (bad_input)
    f.yaw=yaw;f.yaw_ok=have_after&&std::isfinite(yaw);
    const mtg::Selection& sel=g_cmb_md->Target();
    f.target_ok=mmag::ReadTarget(m,sel.pointer,sel.raw.id,sel.raw.type,f.target);
    const mmag::Output o=g_cmb_magnet->Step(f);
    if((o.write_pos||o.write_yaw)&&!mmag::Apply(m,player,o))g_cmb_magnet->Abort(mmag::Stop::WriteFailed);
    if(g_cmb_magnet->phase()==mmag::Phase::Off)CmbMagnetLog("stop");
}
// Ending Blow leap gate (ending_blow_leap.h), mirroring CmbMagnetOwns: the dispatch's own api.move of
// our Ending Blow sword lease while the leap is active. The magnet needs the Mortal Draw lease and the
// dispatch owns one lease at a time, so the two never own the same frame (test_mortal_draw_dispatch).
static bool CmbLeapOwns(void* model){
    const auto& a=g_sword_action;
    return g_sword_leap.phase()!=ending::leap::Phase::Off&&a.lease&&a.request.kind==SwordKind::Ending&&   // Active or Settle
        g_sword_leap.lease()==a.lease&&ending::leap::MoveOwner(mortal::dispatch_hooks::runtime,model,a.lease);
}
// Around the original MotionMove: Link before (last frame's collided position) and after, the target
// re-read (live slot, same id/type, alive, still down or getting up: lethal on rise, 16-oct night), the
// scenery-check flag; then X/Z of this tick's command, written before cPlayer::move runs the native
// collision on it. Death, despawn, another actor or any other routine is target_lost as before.
static int CmbLeapMove(void* model){
    namespace lp=ending::leap;
    const uint32_t player=reinterpret_cast<uint32_t>(model);
    CmbMem m;lp::Body before,after;
    const bool have_before=lp::ReadPos(m,player,before);
    const int result=g_cmb_move_original(model);
    const DWORD error=GetLastError();
    lp::Frame f;f.frame=CmbFrame();f.time=GameNow(g_cmb_global_slot);f.step=static_cast<float>(GameStep(g_cmb_global_slot));
    const bool have_after=lp::ReadPos(m,player,after);
    f.before=have_before&&have_after?before:lp::Body{NAN,NAN,NAN};   // unreadable: Step stops (bad_input)
    f.after=have_after?after:lp::Body{NAN,NAN,NAN};
    f.collision=lp::ReadCollision(m,player);
    const auto& t=g_sword_action.request.target;mtg::Raw raw;
    f.target_ok=mtg::ReadRaw(m,t.pointer,raw)==mtg::Reject::None&&(raw.be_flag&0x201)==1&&raw.id==t.id&&raw.type==t.type&&
        raw.hp>0&&(ending::Grounded(raw)||ending::Rising(raw));
    f.rising=f.target_ok&&!ending::Grounded(raw);
    f.target={raw.pos.x,raw.pos.y,raw.pos.z};
    const bool moving=g_sword_leap.Moving();
    const lp::Output o=g_sword_leap.Step(f);
    link_runtime::ztarget::runtime::Memory w;   // SEH reads/writes plus the committed-writable preflight
    const bool aborted=(o.write||o.write_y)&&!lp::Apply(w,player,o)&&g_sword_leap.Abort(lp::Stop::WriteFailed);
    // The leap's summary when it stops moving (it may go on settling: Y only), the settle line when that ends.
    if(moving&&(aborted||!g_sword_leap.Moving()))SwordLeapLog();
    if(!moving&&g_sword_leap.phase()==lp::Phase::Off)SwordLeapSettleLog();   // only called while Active or Settle
    SetLastError(error);
    return result;
}
// After the lease (review 24-oct M1): a Settle that the lease's end left running (SwordLifecycle) goes on from Link's own
// MotionMove, the same seam as CmbLeapMove (our Y write before cPlayer::move's floor snap), only for the player and room
// it was bound to and with gameplay on; anything else ends it (end=lost). Below the magnet and the Back Slice.
static bool CmbLeapSettleOwns(void* model){
    if(!g_sword_leap.Detached())return false;
    const uint32_t g=CmbGet<uint32_t>(g_cmb_global_slot);
    const uint32_t room=link_runtime::IsPointer(g)?CmbGet<uint16_t>(g+0x4FAC):0xFFFF;
    const uint32_t player=reinterpret_cast<uint32_t>(model);
    CmbPlayer p;
    if(g_sword_leap.SettlesFor(player,room)&&CmbReadPlayer(p)&&p.player==player&&p.facts.gameplay)return true;
    g_sword_leap.End(ending::leap::SettleEnd::Lost);SwordLeapSettleLog();
    return false;
}
// Back Slice arc gate (back_slice.h), mirroring CmbLeapOwns: the dispatch's own api.move of our Back Slice lease.
static bool CmbBackOwns(void* model){
    const auto& a=g_sword_action;
    return g_back_circle.Active()&&a.lease&&a.request.kind==SwordKind::Back&&g_back_circle.lease()==a.lease&&
        ending::leap::MoveOwner(mortal::dispatch_hooks::runtime,model,a.lease);
}
// Around the original MotionMove: Link before/after, the locked target re-read (same id/type, alive), then X/Z and the
// yaw of this tick's arc point, written before cPlayer::move runs the native collision on it (CmbLeapMove's seam).
static int CmbBackMove(void* model){
    namespace lp=ending::leap;
    const uint32_t player=reinterpret_cast<uint32_t>(model);
    CmbMem m;lp::Body before,after;
    const bool have_before=lp::ReadPos(m,player,before);
    const int result=g_cmb_move_original(model);
    const DWORD error=GetLastError();
    bks::Frame f;f.frame=CmbFrame();f.time=GameNow(g_cmb_global_slot);f.step=static_cast<float>(GameStep(g_cmb_global_slot));
    const bool have_after=lp::ReadPos(m,player,after);
    f.before=have_before&&have_after?bks::Vec{before.x,before.y,before.z}:bks::Vec{NAN,NAN,NAN};
    f.after=have_after?bks::Vec{after.x,after.y,after.z}:bks::Vec{NAN,NAN,NAN};
    f.collision=lp::ReadCollision(m,player);
    const auto& t=g_sword_action.request.target;mtg::Raw raw;
    f.target_ok=mtg::ReadRaw(m,t.pointer,raw)==mtg::Reject::None&&(raw.be_flag&0x201)==1&&raw.id==t.id&&raw.type==t.type&&raw.hp>0;
    f.target={raw.pos.x,raw.pos.y,raw.pos.z};
    const bks::Output o=g_back_circle.Step(f);
    link_runtime::ztarget::runtime::Memory w;
    if(o.write&&!bks::Apply(w,player,o))g_back_circle.Abort(bks::Stop::WriteFailed);
    if(!g_back_circle.Active()){char line[sizeof(CmbLine::text)];if(bks::FormatEnd(line,sizeof(line),g_back_circle.summary())>0)CmbLog("%s",line);}
    SetLastError(error);
    return result;
}
// Backflip travel gate (backflip.h), as CmbBackOwns: the dispatch's own api.move of our backflip lease.
static bool CmbFlipOwns(void* model){
    const auto& a=g_sword_action;
    return g_flip_move.Active()&&a.lease&&a.request.kind==SwordKind::Flip&&g_flip_move.lease()==a.lease&&
        ending::leap::MoveOwner(mortal::dispatch_hooks::runtime,model,a.lease);
}
// Around the original MotionMove, as CmbBackMove: Link before/after, the locked Ganado (yaw only), then X/Z and the yaw
// of this tick's travel point, written before cPlayer::move runs the native collision on it.
static int CmbFlipMove(void* model){
    namespace lp=ending::leap;
    const uint32_t player=reinterpret_cast<uint32_t>(model);
    CmbMem m;lp::Body before,after;
    const bool have_before=lp::ReadPos(m,player,before);
    const int result=g_cmb_move_original(model);
    const DWORD error=GetLastError();
    bfl::Frame f;f.frame=CmbFrame();f.time=GameNow(g_cmb_global_slot);f.step=static_cast<float>(GameStep(g_cmb_global_slot));
    const bool have_after=lp::ReadPos(m,player,after);
    f.before=have_before&&have_after?bfl::Vec{before.x,before.y,before.z}:bfl::Vec{NAN,NAN,NAN};
    f.after=have_after?bfl::Vec{after.x,after.y,after.z}:bfl::Vec{NAN,NAN,NAN};
    f.collision=lp::ReadCollision(m,player);
    const auto& t=g_sword_action.request.target;mtg::Raw raw;
    f.target_ok=t.pointer&&mtg::ReadRaw(m,t.pointer,raw)==mtg::Reject::None&&(raw.be_flag&0x201)==1&&raw.id==t.id&&raw.type==t.type&&raw.hp>0;
    if(f.target_ok)f.target={raw.pos.x,raw.pos.y,raw.pos.z};
    const bfl::Output o=g_flip_move.Step(f);
    link_runtime::ztarget::runtime::Memory w;
    if(o.write&&!bks::Apply(w,player,o))g_flip_move.Abort(bfl::Stop::WriteFailed);
    if(!g_flip_move.Active()){char line[sizeof(CmbLine::text)];if(bfl::FormatEnd(line,sizeof(line),g_flip_move.summary())>0)CmbLog("%s",line);}
    SetLastError(error);
    return result;
}
// parry-heavy-01 (Donald 31-oct): a blocked hit with power >= ParryHeavyPower still does nothing to Link, but he slides
// back TP-style, away from the hit point (parry_pushback.h). Game thread: ParryHeavyBlock runs in the damage resolver,
// CmbPushMove around the player's MotionMove when no sword action owns the move; the X/Z written there go through the
// native collision of cPlayer::move like the backflip's.
namespace ppb=link_runtime::parry::pushback;
static ppb::Config g_push_cfg;
static ppb::Slide g_push;
static void ParryHeavyBlock(const link_runtime::parry::general::Applied& applied){
    if(!ppb::Heavy(g_push_cfg,applied.power))return;
    const uint32_t player=CmbGet<uint32_t>(g_cmb_player_slot);
    if(!link_runtime::IsPointer(player))return;
    float ux=0,uz=0;
    if(!ppb::Direction(applied.hit,applied.player,CmbGet<float>(player+0xA4),ux,uz)||!g_push.Start(g_push_cfg,player,ux,uz,CmbFrame()))return;
    CmbLog("parry heavy pushback serial=0x%08X power=%u distance=%.0f frames=%.0f dir=%.2f,%.2f",applied.serial,applied.power,
        static_cast<double>(g_push_cfg.distance),static_cast<double>(g_push_cfg.frames),static_cast<double>(ux),static_cast<double>(uz));
}
static int CmbPushMove(void* model){
    const uint32_t player=reinterpret_cast<uint32_t>(model);
    const int result=g_cmb_move_original(model);
    const DWORD error=GetLastError();
    const uint32_t routine=CmbGet<uint32_t>(player+0xFC);
    if((routine&0xFF)!=0)g_push.Abort(ppb::Stop::Damaged);   // damage / death routine: the native reaction moves Link
    float dx=0,dz=0;
    // Game time only: the counter jumping (event, menu, load) since the last step cancels it (pending 14).
    if(g_push.Step(player,static_cast<float>(GameStep(g_cmb_global_slot)),dx,dz,CmbFrame())){
        link_runtime::ztarget::runtime::Memory w;
        if(!ppb::Apply(w,player,dx,dz))g_push.Abort(ppb::Stop::Write);
    }
    if(!g_push.Active())CmbLog("parry heavy pushback end stop=%s moved=%.0f",ppb::StopName(g_push.stop()),static_cast<double>(g_push.moved()));
    SetLastError(error);
    return result;
}
static int __cdecl CmbMoveDetour(void* model){
    const bool player=model&&reinterpret_cast<uint32_t>(model)==CmbGet<uint32_t>(g_cmb_player_slot);
    if(player){
        const DWORD error=GetLastError();CmbPlayerMove(model);SetLastError(error);
    }
    if(!player)return g_cmb_move_original(model);
    // Magnet first, then the leap: exclusive by lease kind.
    if(!CmbMagnetOwns(model))return CmbLeapOwns(model)?CmbLeapMove(model):CmbBackOwns(model)?CmbBackMove(model):
        CmbFlipOwns(model)?CmbFlipMove(model):CmbLeapSettleOwns(model)?CmbLeapMove(model):
        g_push.Active()?CmbPushMove(model):g_cmb_move_original(model);
    CmbMem m;mmag::Vec2 before;float yaw_before=0.f;
    const bool have_before=mmag::ReadPlayer(m,reinterpret_cast<uint32_t>(model),before,yaw_before);
    const int result=g_cmb_move_original(model);
    const DWORD error=GetLastError();
    CmbMagnetAfterMove(reinterpret_cast<uint32_t>(model),have_before,before);
    SetLastError(error);
    return result;
}
using CmbNeckFn=void(__thiscall*)(void*,int);
static CmbNeckFn g_cmb_neck_original=nullptr;
// Contract 2: the head-look update (thunk 4836 -> 27E6E0, thiscall ECX = player+7DC, RET 4) is
// skipped while the reaction owns 2A8. Wall-clock guard (re-review N1): never longer than 500 ms
// after the last reaction tick, in case MotionMove stops being called mid-reaction.
static void __fastcall CmbNeckDetour(void* self,void*,int look){
    if(g_rx&&g_rx->Active()&&GetTickCount64()-g_rx_last_tick<500&&g_rx->SuppressNeck(CmbGet<uint32_t>(g_cmb_player_slot)))return;
    if(g_sg_layer&&g_sg_layer->Active()&&GetTickCount64()-g_sg_last_tick<500&&g_sg_layer->SuppressNeck(CmbGet<uint32_t>(g_cmb_player_slot)))return;
    g_cmb_neck_original(self,look);
}
// Committed parry (ResolveParry, game thread): serial, owner id/type, observer line.
static void CombatCommitted(const link_runtime::parry::Candidate& candidate,LONG serial){
    if(!g_cmb.AnyOn())return;
    const uint32_t owner=candidate.producer_owner;
    const uint8_t id=CmbGet<uint8_t>(owner+0x100),type=CmbGet<uint8_t>(owner+0x101);
    CmbLog("parry commit serial=%ld owner=0x%08X owner_id=0x%X reaction=%u riposte=%u",serial,owner,id|uint32_t(type)<<8,g_rx?1U:0U,g_rip?1U:0U);
    if(g_cmb.hitpart_observe&&owner!=g_cmb_hp_owner){g_cmb_hp_owner=owner;CmbObserveHitPart(owner,"parry_owner");}
    if(g_rx)g_rx->OnCommitted(static_cast<uint32_t>(serial),id|uint32_t(type)<<8);
    if(g_rip){g_rip_id=id;g_rip_type=type;g_rip->OnCommitted(static_cast<uint32_t>(serial),owner,id,type,GetTickCount64());}
    if(g_cmb_md)g_cmb_stagger.OnCommitted(static_cast<uint32_t>(serial),owner,id,type,GetTickCount64(),CmbTargetConfig());
}
// Tier-2 block (ResolveParry, game thread): Link's reaction with its own serial space
// (0x40000000|n, never a tier-1 commit serial). No riposte and no Mortal Draw stagger window.
static void CombatGeneralBlocked(uint32_t serial,uint32_t owner_id){
    if(!g_cmb.AnyOn())return;
    if(g_rx)g_rx->OnCommitted(serial,owner_id);
}
// ---- startup (worker, before any combat hook is enabled) -----------------------------------------
// Exact bytes of every native entry the combat runtime hooks or calls (native_pins.h kCombat:
// the pinned bio4.exe bytes, relocated operands normalized to 0x400000, so any image base).
static bool CmbPins(){return PinsAt(g_cmb_base,link_runtime::native_pins::kCombat);}
static bool CmbResident(const wchar_t* directory,const wchar_t* name,size_t size,const char* sha,unsigned char*& out){
    std::vector<unsigned char> bytes;unsigned char digest[32]{};
    if(!MortalRead(directory,name,size,bytes)||!mortal::resident_assets::detail::Hash(bytes.data(),bytes.size(),digest)||
       !mortal::resident_assets::detail::Matches(digest,sha))return false;
    out=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));   // never released
    if(out)std::memcpy(out,bytes.data(),size);
    return out!=nullptr;
}
// Before the damage hook: EXE pin, native pins (any image base), adapter binding and the riposte.
static void CombatPrepare(unsigned char* base,uint32_t player_slot,uint32_t global_slot,const char* exe_hash){
    if(!g_cmb.AnyOn())return;
    g_cmb_base=reinterpret_cast<uint32_t>(base);g_cmb_player_slot=player_slot;g_cmb_global_slot=global_slot;
    g_cmb_pinned=g_cmb_base&&player_slot&&global_slot&&exe_hash&&
        !std::strcmp(exe_hash,"19aed4af0ab06a748ff8744d45ac5580fcd6be6b6b7e944b1ab8822a00c8ee4a")&&CmbPins();
    if(!g_cmb_pinned){Log("Combat disabled: EXE pin or native pins (base 0x%X)",g_cmb_base);g_cmb=cmb::Config{};return;}
    if(g_cmb.ending_blow){CmbMem m;if(!ending::NativePins(m,g_cmb_base)){g_cmb.ending_blow=false;Log("EndingBlow disabled: down-state native pins");}}
    if(g_cmb_plan.adapter){
        g_cmb_adapter.Bind({nullptr,&CmbFacts,&CmbPublish,&CmbRecord});
        g_cmb_adapter_on=g_cmb_adapter.Overrides();
    }
    if(g_cmb.parry_riposte)g_rip=new prp::Riposte(prp::MakeConfig(1,g_cmb.riposte_milli));
    Log("Combat prepared: base=0x%X adapter=%u (facts+publish+record) riposte=%u milli=%u",g_cmb_base,g_cmb_adapter_on?1U:0U,g_rip?1U:0U,g_cmb.riposte_milli);
}
static bool CmbHook(uint32_t rva,void* detour,void** original,const char* name){
    void* target=reinterpret_cast<void*>(g_cmb_base+rva);
    MH_STATUS s=MH_CreateHook(target,detour,original);
    if(s==MH_OK&&(s=MH_EnableHook(target))!=MH_OK)MH_RemoveHook(target);
    if(s!=MH_OK){Log("Combat hook %s 0x%X failed: %s",name,rva,MH_StatusToString(s));return false;}
    return true;
}
// After the dispatch: assets, the controller and the arbiter / MotionMove / neck hooks.
static bool StartCombat(const wchar_t* directory,bool dispatch_up){
    if(!g_cmb.AnyOn())return false;
    auto status=MH_Initialize();
    if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){Log("Combat disabled: MinHook %s",MH_StatusToString(status));return false;}
    if(g_cmb.parry_reaction){
        if(CmbResident(directory,L"link_parry_reaction.fcv",424160,"34e0a6bca1901de45fc17c3872f195d4c0a83036a18559e8fe23f6941e28271d",g_rx_fcv)&&
           CmbResident(directory,L"link_parry_reaction.seq",96,"4dca7ca3fa101cfb5f3f2ddf811ea3884ee554e772ca40f42318226ab878a0a5",g_rx_seq)){
            prx::Config c;c.enabled=true;g_rx=new prx::Reaction(c);
        }else Log("ParryReaction disabled: link_parry_reaction.fcv/.seq missing or changed");
    }
    StartGuard(directory);
    bool ok=true;
    // The per-frame pump (adapter Tick, riposte) lives in MotionMove: without it the adapter is
    // switched off (the GetWepDmVal detour passes through) and the riposte is dropped.
    // The magnet also needs MotionMove (0x262B90, pinned in kCombat): it corrects right after the original.
    const bool magnet_wanted=g_cmb.md_contextual&&dispatch_up&&g_cmb_magnet_cfg.enabled;
    const bool move_wanted=(g_cmb_plan.motion_move&&(g_rx||g_cmb_adapter_on||g_sg_on))||magnet_wanted;
    const bool move_ok=move_wanted&&CmbHook(0x262B90,reinterpret_cast<void*>(&CmbMoveDetour),reinterpret_cast<void**>(&g_cmb_move_original),"MotionMove");
    if(move_wanted&&!move_ok){ok=false;g_rx=nullptr;g_rip=nullptr;g_cmb_adapter_on=false;g_sg_on=false;Log("Combat: MotionMove hook failed; reaction, riposte, adapter and magnet off");}
    if(g_cmb.md_contextual){
        if(!dispatch_up){Log("MortalDrawContextual disabled: player dispatch not started");g_cmb.md_contextual=g_cmb.md_damage=false;}
        else g_cmb_md=new mdr::Controller({true,g_cmb.md_damage&&g_cmb_adapter_on,g_cmb.strike_tick,15});
    }
    // Created before the dispatch can start a lease; without the hook the one-shot snap stays.
    if(magnet_wanted&&move_ok&&g_cmb_md&&g_cmb.md_contextual)g_cmb_magnet=new mmag::Magnet(g_cmb_magnet_cfg);
    if(!g_cmb_md&&g_cmb.md_observe)g_cmb_md=new mdr::Controller({false,false,g_cmb.strike_tick,15});
    if(g_cmb.md_damage&&!g_cmb_adapter_on)Log("MortalDrawDamage disabled: adapter or GetWepDmVal detour not available");
    SwordPrepare(directory,dispatch_up,move_ok);
    zlr::Prepare(directory,dispatch_up);   // Zelda's light ray: after the bullets' line-of-sight pins
    if(g_cmb_plan.arbiter)ok&=CmbHook(0x18AB40,reinterpret_cast<void*>(&CmbArbiterDetour),reinterpret_cast<void**>(&g_cmb_arbiter_original),"arbiter");
    if(g_cmb_plan.neck&&(g_rx||g_sg_on))ok&=CmbHook(0x27E6E0,reinterpret_cast<void*>(&CmbNeckDetour),reinterpret_cast<void**>(&g_cmb_neck_original),"neck");
    Log("Combat ready=%u HitPartObserve=%u MortalDrawObserve=%u MortalDrawContextual=%u MortalDrawDamage=%u ParryReaction=%u ParryRiposte=%u "
        "milli=%u line_waiver=%u max_range=%u strike_tick=%u ids=0x10-0x20!0x18 adapter=%u magnet=%u contact=%.0f turn_rate=%.1f "
        "lateral_keep=%.2f redirect=%u follow=%u",
        ok?1U:0U,g_cmb.hitpart_observe?1U:0U,g_cmb.md_observe?1U:0U,g_cmb_md&&g_cmb.md_contextual?1U:0U,
        g_cmb_md&&g_cmb.md_contextual&&g_cmb.md_damage&&g_cmb_adapter_on?1U:0U,g_rx?1U:0U,g_rip?1U:0U,g_cmb.riposte_milli,
        g_cmb.line_waiver?1U:0U,g_cmb.max_range,g_cmb.strike_tick,g_cmb_adapter_on?1U:0U,g_cmb_magnet?1U:0U,
        g_cmb_magnet_cfg.contact,g_cmb_magnet_cfg.turn_rate*57.29578f,g_cmb_magnet_cfg.lateral_keep,
        g_cmb_magnet_cfg.redirect?1U:0U,g_cmb_magnet_cfg.follow?1U:0U);
    return ok;
}
// CardSave(0,1) through thunk 0x279D: returns when the save screen closes (TaskSleep on the game task).
static bool OcaCardSave(uint32_t base){
    __try{sav::CardSave(base)(sav::kTerminal,sav::kFlags);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void OcaOpenSave(){
    g_oca_save_request=false;
    ImageMem m;const sav::Result r=sav::Allowed(m,g_oca_base);   // same frame, re-checked right before the call
    if(!g_oca_save_ok||r.block!=sav::Block::None||g_oca_session.state()!=oca::State::Idle){
        OcaRecordPush(9,g_oca_save_ok?sav::Why(r.block):"serenity blocked: save pins",6);return;}
    OcaRecordPush(9,"save open",0,g_oca_base+sav::rva::kCardSaveThunk);
    const bool ok=OcaCardSave(g_oca_base);
    OcaRecordPush(9,ok?"save returned":"save faulted",ok?0:7);
}
// --- user-ini-01 (22-oct): live cosmetic overrides (user_ini.h; docs/panel-ajustes-2026-10-22.md). Bin32/
// link_runtime.user.ini ([LinkRuntime]; the F8 panel scripts/link_panel.pyw writes it) overrides the main INI key by
// key for the trail, charge glow and particle keys (uini::kKeys) at startup and live. The worker checks the file's
// stamp at most once per second and, when it changed, reads both sections (one GetPrivateProfileSectionW each: a file
// read whole in one open, never a mix of two versions the panel is replacing) and builds the configs: off the game
// thread, because the INI API opens and parses the file per call (milliseconds of file I/O: a visible hitch at 165
// FPS). The snapshot crosses to the game thread through a lock-free triple buffer (uini::Mailbox); the game thread
// takes it at the top of CombinedDecide. Every reader of g_trail_cfg / g_charge_cfg / g_part_cfg runs on the game
// thread (player dispatch, arbiter, sword lifecycle) and the worker never touches them after startup, so the whole
// copy happens between two reads on the same thread: no reader ever sees half of two snapshots. Gameplay keys stay
// startup-only. A feature that did not start (key off, gate or pins failed, no WAV) stays off: `refused` names it.
namespace uini=link_runtime::user_ini;
struct UserIniSnapshot {uini::Cosmetic cfg;unsigned n=0;};
static uini::Mailbox<UserIniSnapshot> g_user_box;   // worker -> game thread
static std::atomic<unsigned> g_user_applied{0};     // the n of the last snapshot the game thread took
// Worker only (the startup path, then UserIniPoll in the worker loop).
static bool g_user_ready=false,g_user_main_whole=false,g_user_file=false;
static unsigned g_user_reloads=0,g_user_applied_logged=0;
static uini::Started g_user_started;
static uini::Cosmetic g_user_last;   // what the game thread has, or takes at its next dispatch
static uini::Watch g_user_watch;
static wchar_t g_user_main_path[MAX_PATH],g_user_path[MAX_PATH];
static wchar_t g_user_main_text[uini::kSectionChars],g_user_text[uini::kSectionChars];
static uini::Section g_user_main_section,g_user_section;
// Game thread, first thing in CombinedDecide: a published snapshot replaces the three configs whole (one atomic load
// when there is none).
static void UserIniApply(){
    if(!g_user_box.Pending())return;
    UserIniSnapshot s;
    if(!g_user_box.Take(s))return;
    g_trail_cfg=s.cfg.trail;g_charge_cfg=s.cfg.charge;g_part_cfg=s.cfg.particles;
    g_user_applied.store(s.n,std::memory_order_release);
}
// The override file's stamp, taken before each read of it (a write after the stamp is seen at the next poll).
static void UserIniStamp(bool& exists,uint64_t& time,uint64_t& size){
    WIN32_FILE_ATTRIBUTE_DATA a{};
    exists=GetFileAttributesExW(g_user_path,GetFileExInfoStandard,&a)!=0&&!(a.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    time=exists?(uint64_t(a.ftLastWriteTime.dwHighDateTime)<<32|a.ftLastWriteTime.dwLowDateTime):0;
    size=exists?(uint64_t(a.nFileSizeHigh)<<32|a.nFileSizeLow):0;
}
// Both [LinkRuntime] sections into the worker's buffers. A main section that does not fit falls back to
// GetPrivateProfileStringW per key; an override section that does not fit is ignored whole. Each counts a problem.
static void UserIniLoad(unsigned& file_problems){
    const DWORD size=static_cast<DWORD>(uini::kSectionChars);
    const DWORD main=GetPrivateProfileSectionW(uini::kSectionName,g_user_main_text,size,g_user_main_path);
    g_user_main_whole=main+2<size;
    if(!g_user_main_whole)++file_problems;
    g_user_main_section=uini::Section(g_user_main_text,g_user_main_whole?main:0);
    const DWORD user=g_user_file?GetPrivateProfileSectionW(uini::kSectionName,g_user_text,size,g_user_path):0;
    if(user+2>=size)++file_problems;
    g_user_section=uini::Section(g_user_text,user+2<size?user:0);
}
// ReadConfig's get: the override's value, else the main INI's.
static void UserIniGet(const wchar_t* key,wchar_t* out,size_t count){
    uini::GetMerged(g_user_section,[](const wchar_t* k,wchar_t* o,size_t n){
        if(g_user_main_whole)g_user_main_section.Find(k,o,n);
        else GetPrivateProfileStringW(uini::kSectionName,k,L"",o,static_cast<DWORD>(n),g_user_main_path);},key,out,count);
}
// Startup (worker, before the hooks): the paths, the stamp, then both sections. false: the override path does not
// fit (the cosmetic keys then come from the main INI only, as before user-ini-01; no live reload).
static bool UserIniOpen(const wchar_t* main_path,unsigned& file_problems){
    if(link_runtime::paths::Copy(g_user_main_path,main_path)||!uini::SiblingPath(main_path,uini::kFile,g_user_path,MAX_PATH)){
        g_user_main_path[0]=g_user_path[0]=0;return false;
    }
    bool exists=false;uint64_t time=0,size=0;UserIniStamp(exists,time,size);
    g_user_watch.Seen(exists,time,size);g_user_file=exists;
    UserIniLoad(file_problems);
    return true;
}
// After StartSwordTrail / StartSwordCharge / StartSwordParticles: what started, and the configs the game thread has.
static void UserIniStart(bool opened,unsigned file_problems){
    if(!opened){Log("user_ini off: Bin32/link_runtime.user.ini path too long; the cosmetic keys are startup-only");return;}
    g_user_started.trail=g_trail_on.load(std::memory_order_acquire);
    g_user_started.charge=g_charge_on.load(std::memory_order_acquire);
    g_user_started.charge_sound=g_user_started.charge&&g_charge_sound.Loaded();
    g_user_started.particles=g_part_on.load(std::memory_order_acquire);
    g_user_started.particles_wall=g_user_started.particles&&g_part_wall_ok;
    g_user_last.trail=g_trail_cfg;g_user_last.charge=g_charge_cfg;g_user_last.particles=g_part_cfg;
    const unsigned problems=uini::Problems(g_user_last)+file_problems;
    char refused[160],line[512];
    uini::Admit(g_user_last,g_user_started,refused,sizeof(refused));
    if(uini::FormatReady(line,sizeof(line),g_user_file,uini::Overrides(g_user_section),problems,refused)>0)Log("%s",line);
    g_user_ready=true;
}
// The file changed: read both, admit, and hand the snapshot over when any live key's value changed. Logged each time.
static void UserIniReload(){
    unsigned file_problems=0;
    UserIniLoad(file_problems);
    uini::Cosmetic next=uini::Read([](const wchar_t* key,wchar_t* out,size_t count){UserIniGet(key,out,count);});
    const unsigned problems=uini::Problems(next)+file_problems;
    char refused[160],changed[640],line[1000];
    uini::Admit(next,g_user_started,refused,sizeof(refused));
    const unsigned n=++g_user_reloads;
    if(uini::Diff(g_user_last,next,changed,sizeof(changed))){g_user_last=next;g_user_box.Publish({next,n});}
    if(uini::FormatReload(line,sizeof(line),n,changed,problems,refused,g_user_file,uini::Overrides(g_user_section))>0)Log("%s",line);
}
// Worker loop: the game thread's take is logged once; the stamp at most once per second; a reload when it changed.
static void UserIniPoll(){
    static bool startup_logged=false;   // user-ini-02: first loop pass, after every startup read (ZTarget's included)
    if(!startup_logged){
        startup_logged=true;char line[1000];
        if(link_runtime::user_ini::startup::FormatStartup(line,sizeof(line))>0)Log("%s",line);
    }
    if(!g_user_ready)return;
    const unsigned applied=g_user_applied.load(std::memory_order_acquire);
    if(applied!=g_user_applied_logged){g_user_applied_logged=applied;Log("user_ini applied n=%u",applied);}
    if(!g_user_watch.Due(GetTickCount64()))return;
    bool exists=false;uint64_t time=0,size=0;UserIniStamp(exists,time,size);
    if(!g_user_watch.Changed(exists,time,size))return;
    g_user_file=exists;
    UserIniReload();
}
// --- end user-ini-01
// ---- TP HUD (hud_tp.h, docs/hud-tp-2026-10-30.md) -------------------------------------------------
// [Hud] TpHearts / TpShieldMeter draw TP's hearts and the shield meter with native IdSys units (the staff's path,
// class 0x3D, tex 0xEE, owner 0xCA2, Bin32/link_hud_tp.eff); HideNativeLife hides Leon's ring groups while the hearts
// are shown. Game thread only: HudFrame runs once per native logic frame (global+0x530C) from the player's core
// (dispatch_hooks::player_frame: damage and death frames included) and from CombinedDecide. Worker: StartHud. Lines go
// through the combat ring (HudLog: written with combat off too). Every key 0: nothing is read, loaded or written.
namespace htp=link_runtime::hud_tp;
static htp::Config g_hud_cfg;
static std::atomic<bool> g_hud_ready{false};
static bool g_hud_on=false;   // worker: StartHud succeeded (poll / exit checks)
static htp::Hud g_hud;
static uint32_t g_hud_base=0,g_hud_player_slot=0,g_hud_global_slot=0,g_hud_frame=0xFFFFFFFFu;
static unsigned g_hud_miss=0;
static htp::Inputs g_hud_in;
// scripts/hud_tp.py build: 10 frames whose TPL points at 07000000 entries 391..400 (after the staff 363..368 and the
// chromatic frames 369..390). A different pack count gives a different block: rebuild both (hud_tp.py freeze checks).
static const OcaFile kHudBlock={L"link_hud_tp.eff",864,"6b4d30d538452fc8ad60d3925d70edefef28f3bfcb765f2b1a8166988350c8d6"};
struct HudNative {   // the staff's IdSys / IdTex calls (ocarina_staff.h layout) with our class, tex id and owner
    uint32_t base=g_hud_base;
    bool Read(uint32_t a,void* out,size_t n){return a&&OcaCopy(a,static_cast<unsigned char*>(out),n);}
    bool Write(uint32_t a,const void* in,size_t n){if(!a)return false;__try{std::memcpy(reinterpret_cast<void*>(a),in,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
    bool TexLoad(void* block){__try{return reinterpret_cast<int(__cdecl*)(void*,int)>(base+htp::layout::tex_load_thunk_rva)(block,int(htp::layout::tex_owner))!=0;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
    void TexRelease(){__try{reinterpret_cast<void(__cdecl*)(int)>(base+htp::layout::tex_release_thunk_rva)(int(htp::layout::tex_owner));}__except(EXCEPTION_EXECUTE_HANDLER){}}
    void Set(const void* table){__try{reinterpret_cast<void(__thiscall*)(void*,const void*,uint8_t,int,uint8_t,uint8_t,uint32_t)>(base+htp::layout::set_thunk_rva)(
        reinterpret_cast<void*>(base+htp::layout::idsys_rva),table,0xFF,htp::layout::id_class,htp::layout::ot_type,htp::layout::ot_no,0);}__except(EXCEPTION_EXECUTE_HANDLER){}}
    void Kill(){__try{reinterpret_cast<void(__thiscall*)(void*,uint8_t,int)>(base+htp::layout::kill_thunk_rva)(reinterpret_cast<void*>(base+htp::layout::idsys_rva),0xFF,htp::layout::id_class);}__except(EXCEPTION_EXECUTE_HANDLER){}}
    uint32_t UnitPtr(uint8_t mark){__try{return reinterpret_cast<uint32_t(__thiscall*)(void*,uint8_t,int)>(base+htp::layout::unit_ptr_thunk_rva)(reinterpret_cast<void*>(base+htp::layout::idsys_rva),mark,htp::layout::id_class);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
};
static htp::Painter<HudNative> g_hud_painter;
static htp::RingHide<HudNative> g_hud_ring;
static htp::PaintWhy g_hud_why=htp::PaintWhy::Idle;
static void HudLog(const char* format,...){va_list args;va_start(args,format);CmbRing(link_runtime::log::kNormal,format,args);va_end(args);}
// --- heart-refill-01 (3.5.0 F-01, hud_tp.h Refill): the drawn hearts chase the life a quarter per 2 frames; one
// Z2SE_HP_GAUGE_INC tick per whole heart. Game thread: HudRefillFrame logs and hands the ticks to the poll thread
// (atomic); PollHudRefill paces them (60 ms) on two alternating voices and latches SYS_LOAD_GAME (global+0x54 0x100,
// GameLoad 0x23D816) for the load snap even while no player frame runs. Inert unless [Hud] HeartRefill=1.
static std::atomic<unsigned> g_hud_refill_ticks{0},g_hud_refill_played{0};
static std::atomic<bool> g_hud_load_seen{false};
static link_runtime::ztarget::voice::Voice g_hud_tick_voice[2];
static htp::TickPacer g_hud_tick_pacer;
static bool g_hud_tick_sound=false;
static bool HudLoadFlag(uint32_t g){LocalMemory m;uint32_t sys=0;return link_runtime::IsPointer(g)&&m.Read32(g+0x54,sys)&&(sys&0x100U)!=0;}
static void HudRefillFrame(){
    if(!g_hud_cfg.refill)return;
    const htp::Snap snap=g_hud.snap();
    if(snap!=htp::Snap::None)HudLog("hud refill snap why=%s q=%d hp=%d/%d ticks=0",htp::SnapName(snap),g_hud.quarters(),g_hud_in.hp,g_hud_in.max_hp);
    const unsigned ticks=g_hud.refill().TakeTicks();
    if(ticks&&g_hud_tick_sound)g_hud_refill_ticks.fetch_add(ticks,std::memory_order_acq_rel);
    htp::RefillDone done;
    if(g_hud.refill().TakeDone(done))HudLog("hud refill from=%d to=%d ticks=%u t_ms=%.0f hearts=%d hp=%d/%d sound=%u",done.from,done.to,done.ticks,done.ms,
        g_hud.hearts(),g_hud_in.hp,g_hud_in.max_hp,g_hud_tick_sound?1U:0U);
}
static void PollHudRefill(){   // poll thread
    if(!g_hud_cfg.refill)return;
    LocalMemory m;uint32_t g=0;
    if(m.Read32(g_hud_global_slot,g)&&HudLoadFlag(g))g_hud_load_seen.store(true,std::memory_order_release);
    if(!g_hud_tick_sound)return;
    const int v=g_hud_tick_pacer.Poll(g_hud_refill_ticks.exchange(0,std::memory_order_acq_rel),GetTickCount64());
    if(v>=0&&g_hud_tick_voice[v].Play())g_hud_refill_played.fetch_add(1,std::memory_order_relaxed);
}
// --- end heart-refill-01
// --- ztarget-letterbox-01 (3.5.0 F-09, hud_tp.h BarView): TP's cinema bars while Z-locked or in neutral Z, two
// fixed units of the hearts' painter (UnitCount never changes with a lock). Lock read on the game thread from the
// ztarget runtime (PlayerDecide runs before HudFrame in CombinedDecide; the core's player_frame may see it one frame
// late). LetterboxOrder=end|start is re-read by the poll thread every 2 s (the bot's A/B) and applied with no bar up.
// Inert unless [Hud] ZTargetLetterbox=1.
static wchar_t g_hud_ini[MAX_PATH]{};
static std::atomic<int> g_hud_lb_order{-1};   // poll -> game: -1 none, 0 end, 1 start
static uint64_t g_hud_lb_next=0;static bool g_hud_lb_said=false;
static bool HudLock(){
    namespace zt=link_runtime::ztarget::runtime;
    return g_hud_cfg.letterbox&&zt::live.load(std::memory_order_acquire)&&
        (zt::controller.phase==link_runtime::ztarget::Phase::Locked||zt::NeutralNow());
}
static void HudLetterboxOrder(){   // game thread, before Update
    if(!g_hud_cfg.letterbox)return;
    const int want=g_hud_lb_order.load(std::memory_order_acquire);
    if(want<0||(want==1)==g_hud.config().letterbox_start||g_hud.bar_on()||g_hud.bar()>0)return;
    g_hud.SetOrder(want==1);
    HudLog("hud letterbox order=%s (live; the painter re-arms)",want==1?"start":"end");
}
static void HudLetterboxFrame(){   // game thread, after Update
    if(!g_hud_cfg.letterbox)return;
    double full=0,clear=0;const unsigned e=g_hud.TakeBarEvents(full,clear);
    if(!e)return;
    const auto& geo=g_hud_painter.geometry();
    if(e&htp::BarIn)HudLog("hud letterbox in h=%u rows=%.1f order=%s clip=%u bottom=%u instant=%u units=%d",g_hud_cfg.letterbox_height,
        static_cast<double>(g_hud.BarRows(geo,false)),g_hud.config().letterbox_start?"start":"end",g_hud_cfg.letterbox_clip?1U:0U,
        g_hud_cfg.letterbox_no_bottom?0U:1U,g_hud_cfg.letterbox_instant?1U:0U,g_hud.UnitCount());
    if(e&htp::BarFull)HudLog("hud letterbox full t_ms=%.0f",full);
    if(e&htp::BarOut)HudLog("hud letterbox out play=%u",g_hud_in.play?1U:0U);
    if(e&htp::BarClear)HudLog("hud letterbox clear t_ms=%.0f",clear);
}
static void PollHudLetterbox(){   // poll thread
    if(!g_hud_cfg.letterbox)return;
    if(!g_hud_lb_said){
        g_hud_lb_said=true;
        Log("hud letterbox ztarget_live=%u (no lock without [LinkRuntime] ZTarget=1)",link_runtime::ztarget::runtime::live.load()?1U:0U);
    }
    const uint64_t now=GetTickCount64();
    if(now<g_hud_lb_next||!g_hud_ini[0])return;
    g_hud_lb_next=now+2000;
    wchar_t word[16]{};bool start=false;
    GetPrivateProfileStringW(L"Hud",L"LetterboxOrder",L"",word,16,g_hud_ini);
    if(htp::ParseOrder(word,start))g_hud_lb_order.store(start?1:0,std::memory_order_release);
}
// --- end ztarget-letterbox-01
// hud-pause-01 (31-oct, Donald's r31c crash, bio4+0x55B729 on taking the second crest, gone with the HUD off): outside
// play (item get, case, menus, events, death; the in_play gates of link_runtime_health.h) the painter drops its units and
// releases its HD texture, so the native screens get that memory back, and re-arms when play resumes. Leon's ring stays
// hidden meanwhile (the hearts come back) instead of flashing on.
static bool g_hud_play=true;
static void HudFrame(){
    if(!g_hud_ready.load(std::memory_order_acquire))return;
    LocalMemory m;uint32_t g=0,frame=0;
    if(!m.Read32(g_hud_global_slot,g)||!link_runtime::IsPointer(g)||!m.Read32(g+0x530C,frame)||frame==g_hud_frame)return;
    g_hud_frame=frame;
    link_runtime::HealthState health;
    if(link_runtime::low_health::ReadHealth(m,g_hud_player_slot,g_hud_global_slot,g_cmb_shop_flag_fix,health)){   // past a latched STA_INTO_SHOP
        g_hud_miss=0;g_hud_in.leon=health.character==0;g_hud_in.hp=health.hp;g_hud_in.max_hp=health.max_hp;
        g_hud_play=health.in_play;g_hud_in.character=health.character;
    }else if(++g_hud_miss>=30)g_hud_in.leon=false;   // a transient unreadable frame keeps the last view (no re-arm flicker)
    // The meter follows shield_guard's own state (GuardTick, MotionMove): drawn only with a shield costume and ShieldGuard ready.
    htp::MeterState& mt=g_hud_in.meter;
    mt.on=g_sg_on&&g_sg_supported;mt.left=g_sg_dur.Left();mt.max=g_sg_dur.Max();mt.broken=g_sg_dur.Broken();
    mt.remaining=g_sg_dur.Remaining(g_sg_clock.Now());mt.regen=g_sg_dur.RegenFrames();
    uint32_t room=0xFFFFFFFFu;   // the room (TrailRoom's u16 at +0x4FAC): a new one retries a failed paint (and snaps the refill)
    if(m.Read32(g+0x4FAC,room))room&=0xFFFFu;
    g_hud_in.play=g_hud_play;g_hud_in.room=room;   // heart-refill-01: read only with HeartRefill=1
    g_hud_in.load=g_hud_cfg.refill&&(g_hud_load_seen.exchange(false,std::memory_order_acq_rel)||HudLoadFlag(g));
    g_hud_in.lock=HudLock();HudLetterboxOrder();   // ztarget-letterbox-01: no-op unless ZTargetLetterbox=1
    g_hud.Update(g_hud_in,GameStep(g_hud_global_slot));
    HudRefillFrame();   // heart-refill-01: no-op unless HeartRefill=1
    HudLetterboxFrame();   // ztarget-letterbox-01
    HudNative n;
    g_hud_painter.Scene(room);
    const htp::PaintWhy why=g_hud_painter.Tick(n,g_hud,g_hud_play);
    // Leon's ring hides only while our hearts are (being) drawn; any failure or the Zelda segment puts it back.
    const bool failed=why==htp::PaintWhy::NoBlock||why==htp::PaintWhy::TexTaken||why==htp::PaintWhy::TexFailed||
        why==htp::PaintWhy::ClassTaken||why==htp::PaintWhy::SetFailed||(why==htp::PaintWhy::Idle&&!g_hud_painter.Live()&&g_hud_play);
    const bool hide=g_hud_cfg.hide_native&&g_hud.HeartsShown()&&!failed;
    const int ring=g_hud_ring.Apply(n,hide,g_hud_cfg.hide_disc);
    if(why!=g_hud_why&&why!=htp::PaintWhy::Shown){
        const auto& geo=g_hud_painter.geometry();
        HudLog("hud_tp paint=%s units=%d hearts=%d quarters=%d hp=%d/%d meter=%u %u/%u broken=%u y0=%.1f crop=%.3f screen=%u",ostf::PaintWhyName(why),
            g_hud_painter.units(),g_hud.hearts(),g_hud.quarters(),g_hud_in.hp,g_hud_in.max_hp,g_hud.MeterShown()?1U:0U,mt.left,mt.max,mt.broken?1U:0U,
            static_cast<double>(geo.y0),static_cast<double>(g_hud_painter.post_y()),g_hud_painter.from_screen()?1U:0U);
    }
    if(ring)HudLog("hud_tp native_ring %s units=%d disc=%u",hide?"hidden":"restored",ring,g_hud_cfg.hide_disc?1U:0U);
    g_hud_why=why;
}
// 2-nov: the player frame does not run while the case or a menu is up, so HudFrame never saw in_play go false there
// and the hearts stayed drawn over the case (live, r100). After each PadRead (menus included) a frame outside play
// drops the units and the texture as HudFrame's pause would; play re-arms them through HudFrame as before.
static void HudMenuFrame(){
    if(!g_hud_ready.load(std::memory_order_acquire)||!g_hud_painter.Live())return;
    __try{
        LocalMemory m;link_runtime::HealthState health;
        if(!link_runtime::low_health::ReadHealth(m,g_hud_player_slot,g_hud_global_slot,g_cmb_shop_flag_fix,health)||health.in_play)return;
        HudNative n;
        const htp::PaintWhy why=g_hud_painter.Tick(n,g_hud,false);
        g_hud_play=false;
        if(why!=g_hud_why)HudLog("hud_tp paint=%s units=%d (outside play, from PadRead)",ostf::PaintWhyName(why),g_hud_painter.units());
        g_hud_why=why;
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}
// dispatch_hooks::player_frame: once the player's core has run, Normal, damage and death frames alike. Each part is
// inert until its own startup succeeded (g_hud_ready; zlr g_on and the companion's g_comp_on) and runs once per native
// frame however often it is called (CombinedDecide runs both too). Published only when [Hud] or the companion is asked for.
static void __cdecl PlayerFrame(void*){
    HudFrame();
    zlr::CompanionTick();   // the partner's cast and ray keep going while Link is hit (review 30-oct)
}
// Worker, after StartCombat (the meter reads ShieldGuard). player_frame was published before Observe; HudFrame stays
// inert until g_hud_ready.
static void StartHud(unsigned char* base,uint32_t player_slot,uint32_t global_slot,bool dispatch_up,const wchar_t* directory){
    if(!g_hud_cfg.Any()){Log("Hud TpHearts=0 TpShieldMeter=0; no HUD block, units or ring writes");return;}
    if(!dispatch_up||!player_slot||!global_slot||!directory){Log("Hud disabled: player dispatch not running or player/global slot missing");return;}
    g_hud_base=reinterpret_cast<uint32_t>(base);g_hud_player_slot=player_slot;g_hud_global_slot=global_slot;
    if(!PinsAt(g_hud_base,htp::kPins)){Log("Hud disabled: IdSys native pins (base 0x%X)",g_hud_base);return;}
    unsigned char* block=OcaResident(directory,kHudBlock);
    if(!block){Log("Hud disabled: link_hud_tp.eff missing or changed");return;}
    g_hud_painter.SetBlock(block);g_hud.Configure(g_hud_cfg);
    g_hud_on=true;g_hud_ready.store(true,std::memory_order_release);
    link_runtime::case_key_debounce::g_after=&HudMenuFrame;   // inert without CaseKeyDebounce (no PadRead hook)
    Log("Hud ready: hearts=%u meter=%u hide_native=%u disc=%u heart_life=%u class=0x%02X tex=0x%02X owner=0x%X core_frame=%u shield_guard=%u",
        g_hud_cfg.hearts?1U:0U,g_hud_cfg.meter?1U:0U,g_hud_cfg.hide_native?1U:0U,g_hud_cfg.hide_disc?1U:0U,g_hud_cfg.heart_life,
        htp::layout::id_class,htp::layout::tex_id,htp::layout::tex_owner,mortal::dispatch_hooks::player_frame?1U:0U,g_sg_on?1U:0U);
    if(g_hud_cfg.meter&&!g_sg_on)Log("Hud shield meter idle: ShieldGuard is not ready (the meter draws only with it)");
    if(g_hud_cfg.hide_native&&!g_hud_cfg.hearts)Log("Hud HideNativeLife ignored: TpHearts=0 (Leon's ring stays)");
    // --- heart-refill-01
    if(g_hud_cfg.refill){
        unsigned voices=0;
        if(g_hud_cfg.refill_sound){
            wchar_t wav[MAX_PATH]{};
            if(link_runtime::paths::Format(wav,L"%ls%ls",directory,htp::kRefillSoundFile)>0)
                for(auto& v:g_hud_tick_voice)if(v.Load(wav,g_hud_cfg.refill_volume,htp::kRefillSoundSha))++voices;
            g_hud_tick_sound=voices==2;
            if(!g_hud_tick_sound)Log("hud refill sound: Bin32/link_heart_refill.wav missing or changed (the refill still draws)");
        }
        Log("hud refill ready=1 sound=%u end_tick=%u voices=%u volume=%u quarter_frames=%.0f gap_ms=%u",g_hud_tick_sound?1U:0U,
            g_hud_cfg.refill_sound_end?1U:0U,voices,g_hud_cfg.refill_volume,htp::kRefillFrames,htp::kTickGapMs);
    }else Log("hud refill off (HeartRefill=0 or TpHearts=0): hearts jump as in 3.4.3");
    // --- end heart-refill-01
    // --- ztarget-letterbox-01
    if(g_hud_cfg.letterbox)Log("hud letterbox ready=1 h=%u order=%s instant=%u bottom=%u clip=%u clip_x=%u top_rows=%u units_max=%d",
        g_hud_cfg.letterbox_height,g_hud_cfg.letterbox_start?"start":"end",g_hud_cfg.letterbox_instant?1U:0U,g_hud_cfg.letterbox_no_bottom?0U:1U,
        g_hud_cfg.letterbox_clip?1U:0U,g_hud_cfg.letterbox_clip_x,g_hud_cfg.letterbox_top_rows,htp::kMaxUnits);
    else Log("hud letterbox off (ZTargetLetterbox=0 or TpHearts=0)");
    // --- end ztarget-letterbox-01
}
static void __cdecl CombinedDecide(mortal::dispatch::Runtime& run,void* player){
    UserIniApply();   // user-ini-01: a new cosmetic snapshot from the worker, before this dispatch's readers (the arbiter and the lifecycle may read the previous one for one frame: same thread, cosmetic)
    zlr::Frame();   // Zelda's light ray: shares the freshly applied particle switch and gain
    link_runtime::sword_trail::probe::runtime::Frame();   // Task 3b probe: no-op unless SwordTrailProbe=1 started it
    if(g_oca_on.load(std::memory_order_acquire))OcarinaDecide(run,player);
    if(g_oca_session.state()==oca::State::Idle&&!OcaOwnsLease()){
        SwordDecide(run,player);CombatDecide(run,player);
    }
    link_runtime::ztarget::runtime::PlayerDecide(run.enabled&&run.busy&&!run.active&&!run.observing&&
        !run.transitioned&&!g_oca_save_request&&run.current==player&&run.Current(player),reinterpret_cast<uint32_t>(player));
    TrailFrame(run);   // Task 4a sword trail: no-op unless SwordTrail is on (native slash watch, live samples)
    ChargeFrame();     // charge-glow-01: no-op unless SwordCharge is on and a charge shows or fades
    ParticlesFrame();  // sword-particles-01: no-op unless SwordParticles is on and a move or a particle is live
    link_runtime::ztarget::runtime::ArrowFrame();   // ztarget-3d-01: no-op unless Reticle3D=1 is live
    HudFrame();        // hud-tp-01: no-op unless [Hud] is on; once per logic frame (the core may have run it already)
    if(g_oca_save_request)OcaOpenSave();   // last: nothing of this frame runs after the menu closes
}
// Worker-thread setup before any hook is enabled: resident files, audio and native pins.
// One note on its own waveOut device. FadeStop queues the rest of the sounding note under a
// 15 ms ramp to zero (ocarina::FadeTail) instead of cutting it; a new note on the same device
// is queued after that ramp, so it starts once the fade ends (review I2).
class OcaNoteVoice {
    HWAVEOUT out=nullptr;WAVEHDR main{},ramp{};link_runtime::WaveData wave;std::vector<int16_t> tail;
    unsigned block=2;DWORD base=0;bool ramp_prepared=false;
    void DropRamp(){if(ramp_prepared){waveOutUnprepareHeader(out,&ramp,sizeof(ramp));ramp_prepared=false;}}
public:
    ~OcaNoteVoice(){if(out){waveOutReset(out);DropRamp();waveOutUnprepareHeader(out,&main,sizeof(main));waveOutClose(out);}}
    bool Load(const wchar_t* path,unsigned volume,const char* expected_sha){
        std::vector<unsigned char> bytes;FILE* file=nullptr;
        if(_wfopen_s(&file,path,L"rb")||!file)return false;
        std::fseek(file,0,SEEK_END);const long length=std::ftell(file);std::rewind(file);
        if(length<44||length>8*1024*1024){std::fclose(file);return false;}
        bytes.resize(static_cast<size_t>(length));const size_t read=std::fread(bytes.data(),1,bytes.size(),file);std::fclose(file);
        unsigned char digest[32]{};
        if(read!=bytes.size()||!link_runtime::mortal_draw::resident_assets::detail::Hash(bytes.data(),bytes.size(),digest)||
           !link_runtime::mortal_draw::resident_assets::detail::Matches(digest,expected_sha)||!link_runtime::ParseWave(bytes,wave)||!wave.channels)return false;
        link_runtime::ScaleWave(wave,volume);
        block=wave.channels*2;
        tail.resize(oca::FadeFrames(wave.rate)*wave.channels);
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=static_cast<WORD>(wave.channels);
        format.nSamplesPerSec=wave.rate;format.wBitsPerSample=16;format.nBlockAlign=static_cast<WORD>(block);
        format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
        if(waveOutOpen(&out,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){out=nullptr;return false;}
        main.lpData=reinterpret_cast<LPSTR>(wave.pcm.data());main.dwBufferLength=static_cast<DWORD>(wave.pcm.size());
        if(waveOutPrepareHeader(out,&main,sizeof(main))!=MMSYSERR_NOERROR){waveOutClose(out);out=nullptr;return false;}
        return true;
    }
    bool Play(){
        if(!out||(main.dwFlags&WHDR_INQUEUE))return false;
        if(ramp_prepared&&(ramp.dwFlags&WHDR_INQUEUE))base=ramp.dwBufferLength;   // plays right after the fade
        else {waveOutReset(out);DropRamp();base=0;}                              // position counter back to 0
        return waveOutWrite(out,&main,sizeof(main))==MMSYSERR_NOERROR;
    }
    void FadeStop(){
        if(!out||!(main.dwFlags&WHDR_INQUEUE))return;
        MMTIME t{};t.wType=TIME_BYTES;
        const bool known=waveOutGetPosition(out,&t,sizeof(t))==MMSYSERR_NOERROR&&t.wType==TIME_BYTES&&t.u.cb>=base;
        waveOutReset(out);DropRamp();
        if(!known)return;                       // no byte position: plain cut, as before
        const size_t at=(t.u.cb-base)/block,frames=wave.pcm.size()/block;
        const size_t n=oca::FadeTail(reinterpret_cast<const int16_t*>(wave.pcm.data()),frames,wave.channels,at,oca::FadeFrames(wave.rate),tail.data());
        if(!n)return;
        ramp={};ramp.lpData=reinterpret_cast<LPSTR>(tail.data());ramp.dwBufferLength=static_cast<DWORD>(n*block);
        if(waveOutPrepareHeader(out,&ramp,sizeof(ramp))!=MMSYSERR_NOERROR)return;
        ramp_prepared=true;
        if(waveOutWrite(out,&ramp,sizeof(ramp))!=MMSYSERR_NOERROR)DropRamp();
    }
};
// Chromatic ocarina (29-oct): the 31 pitch WAVs share ONE waveOut (31 more devices would be a lot of handles); a new
// pitch swaps the buffer of that voice after the 15 ms fade NoteConsumer already queued.
class OcaPitchVoice {
    HWAVEOUT out=nullptr;WAVEHDR main{},ramp{};link_runtime::WaveData waves[oca::kPitchCount];bool have[oca::kPitchCount]{};
    std::vector<int16_t> tail;unsigned block=2,rate=0,channels=0;DWORD base=0;bool main_prepared=false,ramp_prepared=false;int cur=-1;
    void DropRamp(){if(ramp_prepared){waveOutUnprepareHeader(out,&ramp,sizeof(ramp));ramp_prepared=false;}}
    void DropMain(){if(main_prepared){waveOutUnprepareHeader(out,&main,sizeof(main));main_prepared=false;}}
public:
    unsigned loaded=0;
    ~OcaPitchVoice(){if(out){waveOutReset(out);DropRamp();DropMain();waveOutClose(out);}}
    // Every non-button pitch, or none: a missing or changed file leaves the piano silent (the staff still shows it).
    bool Load(const wchar_t* directory,unsigned volume){
        for(int i=0;i<oca::kPitchCount;++i){
            if(!oca::kPitchSha[i])continue;
            wchar_t path[MAX_PATH]{};std::vector<unsigned char> bytes;FILE* file=nullptr;
            if(link_runtime::paths::Format(path,L"%slink_ocarina_pitch_%u.wav",directory,unsigned(oca::kPitchLo+i))<=0||_wfopen_s(&file,path,L"rb")||!file)return false;
            std::fseek(file,0,SEEK_END);const long length=std::ftell(file);std::rewind(file);
            if(length<44||length>8*1024*1024){std::fclose(file);return false;}
            bytes.resize(static_cast<size_t>(length));const size_t read=std::fread(bytes.data(),1,bytes.size(),file);std::fclose(file);
            unsigned char digest[32]{};
            if(read!=bytes.size()||!link_runtime::mortal_draw::resident_assets::detail::Hash(bytes.data(),bytes.size(),digest)||
               !link_runtime::mortal_draw::resident_assets::detail::Matches(digest,oca::kPitchSha[i])||!link_runtime::ParseWave(bytes,waves[i])||!waves[i].channels)return false;
            if(!rate){rate=waves[i].rate;channels=waves[i].channels;}
            else if(waves[i].rate!=rate||waves[i].channels!=channels)return false;
            link_runtime::ScaleWave(waves[i],volume);have[i]=true;++loaded;
        }
        if(!rate)return false;
        block=channels*2;tail.resize(oca::FadeFrames(rate)*channels);
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=static_cast<WORD>(channels);
        format.nSamplesPerSec=rate;format.wBitsPerSample=16;format.nBlockAlign=static_cast<WORD>(block);
        format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
        if(waveOutOpen(&out,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){out=nullptr;return false;}
        return true;
    }
    bool Play(int midi){
        const int i=midi-oca::kPitchLo;
        if(!out||i<0||i>=oca::kPitchCount||!have[i])return false;
        if(main_prepared&&(main.dwFlags&WHDR_INQUEUE))return false;   // FadeStop always runs first (NoteConsumer)
        if(ramp_prepared&&(ramp.dwFlags&WHDR_INQUEUE))base=ramp.dwBufferLength;   // plays right after the fade
        else {waveOutReset(out);DropRamp();base=0;}
        DropMain();
        main={};main.lpData=reinterpret_cast<LPSTR>(waves[i].pcm.data());main.dwBufferLength=static_cast<DWORD>(waves[i].pcm.size());
        if(waveOutPrepareHeader(out,&main,sizeof(main))!=MMSYSERR_NOERROR)return false;
        main_prepared=true;cur=i;
        return waveOutWrite(out,&main,sizeof(main))==MMSYSERR_NOERROR;
    }
    void FadeStop(){
        if(!out||!main_prepared||!(main.dwFlags&WHDR_INQUEUE)||cur<0)return;
        MMTIME t{};t.wType=TIME_BYTES;
        const bool known=waveOutGetPosition(out,&t,sizeof(t))==MMSYSERR_NOERROR&&t.wType==TIME_BYTES&&t.u.cb>=base;
        waveOutReset(out);DropRamp();
        if(!known)return;
        const link_runtime::WaveData& wave=waves[cur];
        const size_t at=(t.u.cb-base)/block,frames=wave.pcm.size()/block;
        const size_t n=oca::FadeTail(reinterpret_cast<const int16_t*>(wave.pcm.data()),frames,channels,at,oca::FadeFrames(rate),tail.data());
        if(!n)return;
        ramp={};ramp.lpData=reinterpret_cast<LPSTR>(tail.data());ramp.dwBufferLength=static_cast<DWORD>(n*block);
        if(waveOutPrepareHeader(out,&ramp,sizeof(ramp))!=MMSYSERR_NOERROR)return;
        ramp_prepared=true;
        if(waveOutWrite(out,&ramp,sizeof(ramp))!=MMSYSERR_NOERROR)DropRamp();
    }
};
class OcaNoteAudio {
    OcaNoteVoice voices[5];OcaPitchVoice pitches;bool piano=false;
public:
    bool Load(const wchar_t* directory,unsigned volume){
        for(unsigned i=0;i<5;++i){
            wchar_t path[MAX_PATH]{};
            if(link_runtime::paths::Format(path,L"%slink_ocarina_note_%u.wav",directory,i)<=0||!voices[i].Load(path,volume,kOcaNoteSha[i]))return false;
        }
        piano=pitches.Load(directory,volume);
        return true;
    }
    bool PianoLoaded() const {return piano;}
    // Note code (ocarina_pitch.h): a button, or a chromatic pitch; a pitch equal to a button plays that button's WAV
    // (the same file). NoteConsumer fades the previous note first.
    bool Play(int note){
        if(oca::IsButtonNote(note))return voices[note].Play();
        const int button=oca::ButtonOfMidi(note);
        if(button>=0)return voices[button].Play();
        return piano&&pitches.Play(note);
    }
    void FadeStop(){for(auto& v:voices)v.FadeStop();pitches.FadeStop();}
};
static OcaNoteAudio g_oca_audio;
static OcaNoteVoice g_oca_jingle_voice;   // link_ocarina_song_ok.wav (TP Z2SE_READ_RIDDLE_B, ocarina-serenity-01)
static const char* const kOcaJingleSha="1fc1036c08378526d9641e2006103725b66d471c644836f6fc1d5c5b5339fc7c";
// Sheik / Epona effect sounds (scripts/ocarina_song_sounds.py from TP's Audiores): Z2SE_EN_AI_FLASH and a cut of
// Z2SE_WL_V_HOWL (Wolf Link's howl). Optional: without them the effects run silent.
static OcaNoteVoice g_oca_flash_voice,g_oca_howl_voice,g_oca_theme_voice;
// OoT's Sheik's Theme, first two phrases (8.8 s), cut from the PAL 1.1 ROM's sequence render (sheik-music-source-01).
static const char* const kOcaSheikThemeSha="4e3ae40c240147f84152f71e0ce255b9a24715bfbdcd9dde6deedc7f44818bdd";
constexpr uint16_t kOcaSheikThemeFrames=528;   // 8.8 s at 60 Hz
// OcarinaSongPlayback (18-oct, scripts/ocarina_song_playback.py, 48 kHz stereo): Serenity, Sheik, Epona (Donald's
// own ocarina recording of OoT's Epona's Song, no backing).
static OcaNoteVoice g_oca_play_voice[3];
static const wchar_t* const kOcaPlayFile[3]={L"link_ocarina_play_serenity.wav",L"link_ocarina_play_sheik.wav",L"link_ocarina_play_epona.wav"};
static const char* const kOcaPlaySha[3]={"cdf1774aa23109fc0f832049dd31468c1ff1e1157375a8173fc4e786f31b7e11",
    "1cf4cef0b679c23eccfcae95716983ed3681839b659f551c03d6a7cadc2f5b12","09ca253a254e4951483e8e146467cb41ffcc9a1578af5417de9bd8774832c257"};
constexpr uint16_t kOcaPlayFrames[3]={468,528,446};   // 7.8 / 8.8 / 7.43 s at 60 Hz (report.json frames60)
static const char* const kOcaFlashSha="0d2e1cf3c26edc64a1a0eb1b14ed01e525703dbd1748f68fcf2a2e259a4e89d5";
static const char* const kOcaHowlSha="e52888a388bfad5db96c575b7c8a83569e80e0086b7d1ae0ec1a686dd38c22ac";
static void OcaFreeResident(){
    for(auto*& c:g_oca_clip)if(c){VirtualFree(c,0,MEM_RELEASE);c=nullptr;}
    for(auto*& t:g_oca_tpl)if(t){VirtualFree(t,0,MEM_RELEASE);t=nullptr;}
    if(g_oca_bin){VirtualFree(g_oca_bin,0,MEM_RELEASE);g_oca_bin=nullptr;}
}
static bool OcaLoad(const link_runtime::shield::NativeAccess& n,const wchar_t* directory);
// Nothing has used the copies yet (no hook enabled), so a failed start can release them.
static bool StartOcarina(const link_runtime::shield::NativeAccess& n,const wchar_t* directory){
    if(OcaLoad(n,directory))return true;
    OcaFreeResident();return false;
}
static bool OcaLoad(const link_runtime::shield::NativeAccess& n,const wchar_t* directory){
    g_oca_base=n.image_base;
    g_oca_player_slot=n.image_base+static_cast<uint32_t>(n.code.player_slot_rva);
    g_oca_global_slot=n.image_base+static_cast<uint32_t>(n.code.global_slot_rva);
    g_oca_input=n.image_base+static_cast<uint32_t>(n.code.input_rva);
    if(g_oca_cfg.pose)for(unsigned i=0;i<6;++i)if(!(g_oca_clip[i]=OcaResident(directory,kOcaClips[i]))){Log("Ocarina disabled: pose clips");return false;}
    if(g_oca_cfg.mount){
        if(!PinsAt(g_oca_base,link_runtime::native_pins::kOcarinaMount)){Log("Ocarina disabled: mount native pins");return false;}
        if(!(g_oca_bin=OcaResident(directory,kOcaBin))){Log("Ocarina disabled: model");return false;}
        for(unsigned i=0;i<4;++i)if(!(g_oca_tpl[i]=OcaResident(directory,kOcaTpls[i]))){Log("Ocarina disabled: TPL %u",i);return false;}
    }
    // Weapon hide (Pose=1 only): fails closed. Without its pins the pose still runs, the gun stays shown.
    g_oca_weapon_ok=g_oca_cfg.pose&&PinsAt(g_oca_base,link_runtime::native_pins::kOcarinaWeapon);
    if(g_oca_cfg.pose&&!g_oca_weapon_ok)Log("Ocarina weapon hide disabled: native pins");
    if(g_oca_cfg.notes&&!g_oca_audio.Load(directory,g_oca_cfg.volume)){Log("Ocarina disabled: note WAVs or audio device");return false;}
    if(g_oca_cfg.notes&&g_oca_cfg.piano&&!g_oca_audio.PianoLoaded())Log("Ocarina piano notes silent: link_ocarina_pitch_*.wav missing or changed");
    if(g_oca_cfg.item&&!OcaPatchUse())return false;
    if(g_oca_manual_on&&g_oca_cfg.item){   // optional: a failure leaves the Files menu native, the ocarina runs
        OcaPatchMem pm;
        g_oca_manual_on=PinsAt(g_oca_base,omn::kPins)&&omn::SitesKnown(pm,g_oca_base,omn::kRows)&&omn::Apply(pm,g_oca_base,omn::kList,true)>=0;
        Log(g_oca_manual_on?"Ocarina manual ready: File_31 (0x%X) in Files row 13 (files-menu-fix-01: 9 size copies), messages 0x%X.., pictures f31a..d":"Ocarina manual disabled: native pins or patch sites (item 0x%X, base 0x%X)",
            omn::kManualItem,g_oca_manual_on?omn::kMessageBase:g_oca_base);
    }else g_oca_manual_on=false;
    bool jingle_sound=false;
    if(g_oca_song_cfg.serenity||g_oca_song_cfg.sheik||g_oca_song_cfg.epona){
        wchar_t jingle[MAX_PATH]{};
        jingle_sound=link_runtime::paths::Format(jingle,L"%slink_ocarina_song_ok.wav",directory)>0&&g_oca_jingle_voice.Load(jingle,g_oca_cfg.volume,kOcaJingleSha);
    }
    if(g_oca_song_cfg.serenity){
        ImageMem image;g_oca_save_ok=sav::PinsOk(image,g_oca_base);
        if(!g_oca_save_ok){g_oca_song_cfg.serenity=false;Log("Ocarina Serenity disabled: save native pins (base 0x%X)",g_oca_base);}
        else Log("Ocarina Serenity ready: melody=%c save_pins=1 jingle=%u cardsave=0x%X",g_oca_song_cfg.melody.notes[2]==4?'B':'A',jingle_sound?1U:0U,g_oca_base+sav::rva::kCardSaveThunk);
    }
    if(g_oca_song_cfg.sheik||g_oca_song_cfg.epona){
        ImageMem image;g_oca_strike_ok=sav::PinsOk(image,g_oca_base)&&PinsAt(g_oca_base,link_runtime::native_pins::kOcarinaSongStrike);
        if(!g_oca_strike_ok){g_oca_song_cfg.sheik=g_oca_song_cfg.epona=false;Log("Ocarina Sheik/Epona disabled: save rules or publisher native pins (base 0x%X)",g_oca_base);}
        else{
            // Optional sounds: a missing or changed file only mutes the effect (logged); the effect still runs.
            wchar_t path[MAX_PATH]{};
            g_oca_theme_ok=g_oca_song_cfg.sheik&&link_runtime::paths::Format(path,L"%slink_ocarina_sheik_theme.wav",directory)>0&&g_oca_theme_voice.Load(path,g_oca_cfg.volume,kOcaSheikThemeSha);
            const bool flash=g_oca_song_cfg.sheik&&link_runtime::paths::Format(path,L"%slink_ocarina_sheik_flash.wav",directory)>0&&g_oca_flash_voice.Load(path,g_oca_cfg.volume,kOcaFlashSha);
            const bool howl=g_oca_song_cfg.epona&&link_runtime::paths::Format(path,L"%slink_ocarina_epona_howl.wav",directory)>0&&g_oca_howl_voice.Load(path,g_oca_cfg.volume,kOcaHowlSha);
            Log("Ocarina combat songs ready: sheik=%u radius=%u theme=%u flash_sound=%u epona=%u radius=%u howl_sound=%u jingle=%u publisher=0x%X",
                g_oca_song_cfg.sheik?1U:0U,g_oca_song_cfg.sheik_radius,g_oca_theme_ok?1U:0U,flash?1U:0U,g_oca_song_cfg.epona?1U:0U,g_oca_song_cfg.epona_radius,howl?1U:0U,
                jingle_sound?1U:0U,g_oca_base+ose::dmg::layout::publisher_rva);
        }
    }
    if(g_oca_song_cfg.playback){
        const bool on[3]={g_oca_song_cfg.serenity,g_oca_song_cfg.sheik,g_oca_song_cfg.epona};
        for(unsigned i=0;i<3;++i){wchar_t path[MAX_PATH]{};
            g_oca_play_ok[i]=on[i]&&link_runtime::paths::Format(path,L"%ls%ls",directory,kOcaPlayFile[i])>0&&g_oca_play_voice[i].Load(path,g_oca_cfg.volume,kOcaPlaySha[i]);}
        Log("Ocarina song playback ready=1 serenity=%u sheik=%u epona=%u cancel=toggle/stow/damage (a missing or changed WAV keeps the 17-oct replay)",
            g_oca_play_ok[0]?1U:0U,g_oca_play_ok[1]?1U:0U,g_oca_play_ok[2]?1U:0U);
    }else Log("Ocarina song playback ready=0 (OcarinaSongPlayback=0)");
    {
        OcaSongFlow::Config c;c.serenity=g_oca_song_cfg.serenity;c.melody=g_oca_song_cfg.melody;
        c.extra[0]={osg::Song::Sheik,g_oca_song_cfg.sheik,osg::kSheik,ostf::kSheikRhythm,"sheik",kOcaSheikThemeFrames};
        c.extra[1]={osg::Song::Epona,g_oca_song_cfg.epona,osg::kEpona,ostf::kEponaRhythm,"epona"};
        if(g_oca_song_cfg.playback){
            c.cancelable=true;
            if(g_oca_play_ok[0]){c.audio_frames=kOcaPlayFrames[0];c.audio_rhythm=ostf::kSerenityPlayRhythm;}
            if(g_oca_play_ok[1]){c.extra[0].audio_frames=kOcaPlayFrames[1];c.extra[0].audio_rhythm=ostf::kSheikPlayRhythm;}
            if(g_oca_play_ok[2]){c.extra[1].audio_frames=kOcaPlayFrames[2];c.extra[1].audio_rhythm=ostf::kEponaPlayRhythm;}
        }
        c.extra_count=2;g_oca_song.Configure(c);
        if(g_oca_song.dropped())Log("Ocarina songs: %u melody clashes, those songs off",g_oca_song.dropped());
    }
    if(g_oca_staff_on){
        if(!PinsAt(g_oca_base,ostf::kPins)){g_oca_staff_on=false;Log("Ocarina staff disabled: IdSys native pins (base 0x%X)",g_oca_base);}
        else{
            const bool chroma=(g_oca_staff_block=OcaResident(directory,kOcaStaffBlockChroma,true))!=nullptr;
            if(!chroma)g_oca_staff_block=OcaResident(directory,kOcaStaffBlock);
            if(!g_oca_staff_block){g_oca_staff_on=false;Log("Ocarina staff disabled: link_ocarina_staff.eff");}
            else{g_oca_painter.SetBlock(g_oca_staff_block);g_oca_song.SetChromaticArt(chroma);
                Log("Ocarina staff ready: class=0x%02X tex=0x%02X owner=0x%X idsys=0x%X chromatic_frames=%u",ostf::layout::id_class,ostf::layout::tex_id,ostf::layout::tex_owner,g_oca_base+ostf::layout::idsys_rva,chroma?1U:0U);}
        }
    }
    unsigned piano_keys=0;for(uint8_t k:g_oca_cfg.keys.piano)if(k)++piano_keys;
    Log("Ocarina ready: item=%u freeze=%u mount=%u pose=%u weapon_hide=%u notes=%u pad=%u pad_style=%s keys toggle=0x%02X stow=0x%02X A=0x%02X arrows=0x%02X/0x%02X/0x%02X/0x%02X volume=%u piano=%u piano_keys=%u piano_wavs=%u",
        g_oca_cfg.item?1U:0U,g_oca_cfg.freeze?1U:0U,g_oca_cfg.mount?1U:0U,g_oca_cfg.pose?1U:0U,g_oca_weapon_ok?1U:0U,g_oca_cfg.notes?1U:0U,g_oca_cfg.pad?1U:0U,
        g_oca_cfg.pad_n64?"n64(LB+RB,A,B,right stick)":"legacy(left stick)",g_oca_cfg.keys.toggle,g_oca_cfg.keys.stow,g_oca_cfg.keys.note_a,g_oca_cfg.keys.up,g_oca_cfg.keys.down,g_oca_cfg.keys.left,g_oca_cfg.keys.right,g_oca_cfg.volume,
        g_oca_cfg.piano?1U:0U,piano_keys,g_oca_audio.PianoLoaded()?1U:0U);
    return true;
}
using OcaXInputGetState=DWORD(WINAPI*)(DWORD,void*);
struct OcaPadState {DWORD packet;WORD buttons;BYTE lt,rt;SHORT lx,ly,rx,ry;};
struct OcaWorker {
    OcaXInputGetState xinput=nullptr;ULONGLONG next_pad=0;oca::NoteConsumer consumer;
    void Start(){
        if(!g_oca_cfg.pad&&!link_runtime::ztarget::runtime::PadWanted())return;
        HMODULE module=LoadLibraryW(L"xinput1_4.dll");if(!module)module=LoadLibraryW(L"xinput9_1_0.dll");
#pragma warning(push)
#pragma warning(disable: 4191)
        if(module)xinput=reinterpret_cast<OcaXInputGetState>(GetProcAddress(module,"XInputGetState"));
#pragma warning(pop)
        Log("Ocarina pad XInput=%u",xinput?1U:0U);
        if(link_runtime::ztarget::runtime::PadWanted())Log("ZTarget pad XInput=%u shared_reader=1 index=0 hold=RS",xinput?1U:0U);
    }
    void Poll(){
        LocalMemory m;uint32_t global=0,routine=0;
        const bool readable=m.Read32(g_oca_global_slot,global)&&link_runtime::IsPointer(global)&&m.Read32(global+0x20,routine);
        g_oca_epoch.Observe(readable,routine);
        DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        const bool focused=pid==GetCurrentProcessId();
        g_oca_focus.Publish(focused);
        if(xinput&&GetTickCount64()>=next_pad){
            OcaPadState state{};
            const bool connected=xinput(0,&state)==ERROR_SUCCESS;
            link_runtime::ztarget::runtime::PublishPad(connected,focused,state.buttons,state.rx,state.lt);
            g_oca_pad_right.store(connected?(uint64_t(state.rt)<<40)|(uint64_t(state.lt)<<32)|(uint64_t(uint16_t(state.ry))<<16)|uint16_t(state.rx):0,std::memory_order_release);
            if(connected)
                g_oca_pad.store((1ULL<<48)|(uint64_t(uint16_t(state.ly))<<32)|(uint64_t(uint16_t(state.lx))<<16)|state.buttons,std::memory_order_release);
            else {g_oca_pad.store(0,std::memory_order_release);next_pad=GetTickCount64()+1000;}  // disconnected pads are slow to poll
        }
        if(g_oca_cfg.notes){if(!focused)g_oca_audio.FadeStop();consumer.Poll(g_oca_notes,g_oca_audio);}
        if(g_oca_jingle.exchange(false,std::memory_order_acq_rel))g_oca_jingle_voice.Play();
        switch(g_oca_theme.exchange(0,std::memory_order_acq_rel)){case 1:g_oca_theme_voice.Play();break;case 2:g_oca_theme_voice.FadeStop();break;default:break;}
        if(!focused)g_oca_theme_voice.FadeStop();
        if(const int pv=g_oca_play.exchange(0,std::memory_order_acq_rel)){
            if(pv>=1&&pv<=3)g_oca_play_voice[pv-1].Play();else for(auto& v:g_oca_play_voice)v.FadeStop();}
        if(!focused)for(auto& v:g_oca_play_voice)v.FadeStop();
        switch(g_oca_effect_sound.exchange(0,std::memory_order_acq_rel)){case 1:g_oca_flash_voice.Play();break;case 2:g_oca_howl_voice.Play();break;default:break;}
        unsigned r=g_oca_read.load(std::memory_order_relaxed);const unsigned w=g_oca_write.load(std::memory_order_acquire);
        while(r!=w){
            const OcaRecord record=g_oca_records[r%64];
            Log("ocarina kind=%u status=\"%s\" code=%d a=0x%X b=0x%X token=%llu",record.kind,record.text,record.code,record.a,record.b,record.token);
            g_oca_read.store(++r,std::memory_order_release);
        }
        const unsigned dropped=g_oca_dropped.exchange(0);if(dropped)Log("ocarina records dropped=%u",dropped);
    }
};
// contextual: MortalDrawContextual loads the shared clip and voice. The "MortalDrawTrial" log
// prefix is historical (the retired F8 pilot); this setup serves every dispatch user.
static bool StartMortalTrial(unsigned char* base,unsigned size,const wchar_t* directory,AlertVoice& audio,bool ocarina_wanted,bool contextual=false,bool sword_wanted=false,bool ztarget_face=false,bool ztarget_strafe=false){
    using namespace link_runtime::shield;
    try {
        wchar_t executable[MAX_PATH]{};char exe_hash[65]{};
        const DWORD path_size=GetModuleFileNameW(nullptr,executable,MAX_PATH);
        if(!path_size||path_size>=MAX_PATH||!ShieldFileHash(executable,exe_hash)||std::strcmp(exe_hash,"19aed4af0ab06a748ff8744d45ac5580fcd6be6b6b7e944b1ab8822a00c8ee4a")){Log("MortalDrawTrial disabled: EXE pin");return false;}
        const NativeAccess n=g_mortal_native;
        if(n.image_base!=reinterpret_cast<uint32_t>(base)||n.image_size!=size||!Complete(n)||!mortal::dispatch_hooks::Qualify(n)||!mortal::trial::Native(n)){Log("MortalDrawTrial disabled: native qualification");return false;}
        if(contextual){
        std::vector<unsigned char> fcv,seq,wav;
        if(!MortalRead(directory,L"link_mortal_draw.fcv",mortal::resident_assets::fcv_size,fcv)||
           !MortalRead(directory,L"link_mortal_draw.seq",mortal::resident_assets::seq_size,seq)||
           !MortalRead(directory,L"link_mortal_draw_voice.wav",80220,wav)){Log("MortalDrawTrial disabled: exact-size assets missing");return false;}
        unsigned char digest[32]{};
        if(!mortal::resident_assets::detail::Hash(wav.data(),wav.size(),digest)||!mortal::resident_assets::detail::Matches(digest,"de80acce33c0757dc941b9d764f967a55d02300ad527999d30584283a53c1e7d")){Log("MortalDrawTrial disabled: voice pin");return false;}
        const auto prepared=mortal::resident_assets::Prepare(fcv.data(),fcv.size(),seq.data(),seq.size());
        if(prepared!=mortal::resident_assets::Result::Prepared&&prepared!=mortal::resident_assets::Result::Reused){Log("MortalDrawTrial disabled: asset pins %u",static_cast<unsigned>(prepared));return false;}
        wchar_t path[MAX_PATH]{};if(link_runtime::paths::Format(path,L"%slink_mortal_draw_voice.wav",directory)<0||!audio.Load(path,35,"de80acce33c0757dc941b9d764f967a55d02300ad527999d30584283a53c1e7d")){Log("MortalDrawTrial disabled: voice backend");return false;}
        }
        // Ocarina (Task 10): its own resident files and pins; a failure leaves Mortal Draw as before.
        if(ocarina_wanted&&!StartOcarina(n,directory)){ocarina_wanted=false;if(!contextual&&!sword_wanted&&!ztarget_face)return false;}
        auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
        status=mortal::dispatch_hooks::Prepare(n);if(status!=MH_OK){Log("MortalDrawTrial disabled: prepare %s",MH_StatusToString(status));return false;}
        g_mortal_native=n;auto& run=mortal::dispatch_hooks::runtime;
        run.decide=CombinedDecide;run.cleanup=MortalCleanup;run.restore=MortalRestore;
        run.lifecycle=ocarina_wanted?CombinedLifecycle:MortalLifecycle;run.lifecycle_context=nullptr;
        run.proceed=ocarina_wanted?OcaProceed:nullptr;run.observe=ocarina_wanted||ztarget_face?CombinedObserve:nullptr;run.enabled=true;
        if(ztarget_strafe&&link_runtime::ztarget::runtime::PrepareStrafe(reinterpret_cast<uint32_t>(base),[](uint32_t actor){
            auto& dispatch=mortal::dispatch_hooks::runtime;
            return mortal::dispatch_hooks::OwnedThread()&&dispatch.enabled&&!dispatch.busy&&!dispatch.current&&
                !dispatch.active&&!dispatch.observing&&!dispatch.transitioned&&!g_oca_save_request&&
                dispatch.Current(reinterpret_cast<void*>(actor));},&Log)){
            mortal::dispatch_hooks::normal_forward=&link_runtime::ztarget::runtime::StrafeNormal;
            mortal::dispatch_hooks::before_core=&link_runtime::ztarget::runtime::StrafeCore;
        }
        status=mortal::dispatch_hooks::Observe();
        if(status!=MH_OK){Log("MortalDrawTrial hook enable failed=%s; resident state retained",MH_StatusToString(status));return false;}
        if(ocarina_wanted){g_oca_on.store(true,std::memory_order_release);Log("Ocarina armed: toggle through the player dispatch; Mortal Draw waits while it is out");}
        if(contextual)Log("MortalDrawContextual dispatch ready: action-button start from the sheath, shared clip+voice");
        return true;
    }catch(...){Log("MortalDrawTrial disabled: setup exception");return false;}
}
static void PollMortalTrial(mortal::voice::Consumer& consumer,AlertVoice& audio){
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground!=GetCurrentProcessId())g_mortal_voice.Stop(g_mortal_voice.Current()); // atomic exact-token drop, no game pointer
    consumer.Poll(g_mortal_voice,audio);
    unsigned r=g_mortal_read.load(std::memory_order_relaxed),w=g_mortal_write.load(std::memory_order_acquire);
    while(r!=w){const auto record=g_mortal_records[r%16];
        if(record.kind==4)Log("pose_residue restored lease=%llu parts=%u routine=0x%08X",record.token,record.status,record.routine);
        else Log("MortalDrawTrial kind=%u status=%u routine=0x%08X token=%llu damage=0",record.kind,record.status,record.routine,record.token);
        g_mortal_read.store(++r,std::memory_order_release);}
}

// Case heart items (dll/case_items.h, docs/heart-items-case-2026-10-12.md): 2 x 1 rows, models, save turn.
namespace cit=link_runtime::case_items;
struct CaseImage {
    unsigned char* base; unsigned size;
    bool Read(uint32_t rva,void* out,size_t n) const {
        if(uint64_t(rva)+n>size)return false;
        __try{std::memcpy(out,base+rva,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Write(uint32_t rva,const void* in,size_t n){
        if(uint64_t(rva)+n>size)return false;
        DWORD old=0;if(!VirtualProtect(base+rva,n,PAGE_READWRITE,&old))return false;
        std::memcpy(base+rva,in,n);DWORD ignored=0;VirtualProtect(base+rva,n,old,&ignored);return true;
    }
};
using CaseTblInit=void(__cdecl*)(void*);
using CasePzlInit=void*(__fastcall*)(void*,void*,int);      // thiscall, one int, ret 4
static CaseTblInit g_case_tbl=nullptr;
static CasePzlInit g_case_pzl=nullptr;
static unsigned char* g_case_base=nullptr;
static volatile LONG g_case_models=-1,g_case_turned=0,g_case_mixed=0;
using CaseModelSet=void(__cdecl*)(void*);
using CaseSetDir=void(__fastcall*)(void*,void*,int);       // thiscall (piece, s8 turn), ret 4
static CaseModelSet g_case_modelset=nullptr;
static CaseSetDir g_case_setdir=nullptr;
static void __cdecl CaseTblInitDetour(void* wk){
    g_case_tbl(wk);
    __try{
        const uint8_t* dat=*reinterpret_cast<const uint8_t* const*>(static_cast<unsigned char*>(wk)+cit::rva::kWkDat);
        const cit::Models m=cit::DecideModels(dat,cit::DatExtent(dat));
        LONG mask=0;
        for(unsigned i=0;i<4;++i)if(m.offset[i]){
            *reinterpret_cast<const void**>(g_case_base+cit::kItems[i].row_rva+cit::rva::kRowModel)=dat+m.offset[i];
            mask|=1L<<i;
        }
        InterlockedExchange(&g_case_models,mask);
    }__except(EXCEPTION_EXECUTE_HANDLER){InterlockedExchange(&g_case_models,0x100);}
}
static void* __fastcall CasePzlInitDetour(void* self,void* edx,int kind){
    __try{
        unsigned char* mgr=g_case_base+cit::rva::kItemMgr;
        uint8_t* items=*reinterpret_cast<uint8_t**>(mgr+cit::rva::kItemArray);
        const int count=*reinterpret_cast<int*>(mgr+cit::rva::kItemCount);
        const unsigned turned=cit::MigrateAll(items,count);
        if(turned)InterlockedExchangeAdd(&g_case_turned,static_cast<LONG>(turned));
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    return g_case_pzl(self,edx,kind);
}
// A heart mixed on a herb or jar: before its model is set the piece takes the heart row and a quarter turn
// (case_items.h, part 4).
static void __cdecl CaseModelSetDetour(void* piece_ptr){
    __try{
        uint8_t* piece=static_cast<uint8_t*>(piece_ptr);
        const uint8_t* item=piece?*reinterpret_cast<uint8_t**>(piece+cit::kPieceItem):nullptr;
        const uint8_t* row=piece?*reinterpret_cast<uint8_t**>(piece+cit::kPieceRow):nullptr;
        if(item&&row){
            const cit::CombineFix f=cit::DecideCombineFix(*reinterpret_cast<const uint16_t*>(item),
                reinterpret_cast<uint32_t>(row),static_cast<int8_t>(row[0]),static_cast<int8_t>(row[1]),
                static_cast<int8_t>(piece[cit::kPieceTurn]),reinterpret_cast<uint32_t>(g_case_base));
            if(f.row){
                *reinterpret_cast<uint32_t*>(piece+cit::kPieceRow)=f.row_ptr;
                if(f.turn)g_case_setdir(piece,nullptr,f.dir);
                InterlockedIncrement(&g_case_mixed);
            }
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    g_case_modelset(piece_ptr);
}
static bool StartCaseItems(unsigned char* base,unsigned size){
    CaseImage image{base,size};
    const cit::Plan plan=cit::Qualify(image,reinterpret_cast<uint32_t>(base));
    if(!plan.ok){Log("CaseHeartItems disabled: %s",plan.reason);return false;}
    MH_STATUS s=MH_Initialize();
    if(s!=MH_OK&&s!=MH_ERROR_ALREADY_INITIALIZED){Log("CaseHeartItems disabled: MinHook init %s",MH_StatusToString(s));return false;}
    g_case_base=base;
    void* tbl=base+cit::rva::kPieceTblInit; void* pzl=base+cit::rva::kPzlPlayerInit;
    s=MH_CreateHook(tbl,reinterpret_cast<void*>(&CaseTblInitDetour),reinterpret_cast<void**>(&g_case_tbl));
    if(s!=MH_OK){Log("CaseHeartItems disabled: hook pieceTblInit %s",MH_StatusToString(s));return false;}
    s=MH_CreateHook(pzl,reinterpret_cast<void*>(&CasePzlInitDetour),reinterpret_cast<void**>(&g_case_pzl));
    if(s!=MH_OK){MH_RemoveHook(tbl);Log("CaseHeartItems disabled: hook pzlPlayer %s",MH_StatusToString(s));return false;}
    if(MH_EnableHook(tbl)!=MH_OK||MH_EnableHook(pzl)!=MH_OK){
        MH_DisableHook(tbl);MH_DisableHook(pzl);MH_RemoveHook(tbl);MH_RemoveHook(pzl);
        Log("CaseHeartItems disabled: enable");return false;
    }
    // the mix fix is optional: its pins missing leave the rest as before
    void* mix=nullptr;
    if(cit::QualifyCombine(image)){
        mix=base+cit::rva::kModelSet;
        g_case_setdir=reinterpret_cast<CaseSetDir>(base+cit::rva::kSetDir);
        if(MH_CreateHook(mix,reinterpret_cast<void*>(&CaseModelSetDetour),reinterpret_cast<void**>(&g_case_modelset))!=MH_OK){
            mix=nullptr;Log("CaseHeartItems mix fix off: hook");
        }else if(MH_EnableHook(mix)!=MH_OK){MH_RemoveHook(mix);mix=nullptr;Log("CaseHeartItems mix fix off: enable");}
    }else Log("CaseHeartItems mix fix off: pieceModelSet/setDir pins");
    // rows last: once they are 2 x 1, the models and the save turn are already live
    if(!cit::ApplyRows(image,plan)){
        MH_DisableHook(tbl);MH_DisableHook(pzl);MH_RemoveHook(tbl);MH_RemoveHook(pzl);
        if(mix){MH_DisableHook(mix);MH_RemoveHook(mix);}
        Log("CaseHeartItems disabled: row write");return false;
    }
    Log("CaseHeartItems ready: rows=0x%X (bit = 0x1C,0x15,0x16,0xA8 written now) tbl=%p pzl=%p mix=%p",plan.write_mask,tbl,pzl,mix);
    return true;
}
static void PollCaseItems(){
    static LONG last=-1;
    const LONG m=InterlockedCompareExchange(&g_case_models,0,0);
    if(m!=last&&m!=-1){Log("CaseHeartItems models=0x%lX%s",m&0xF,m&0x100?" (DAT read fault)":"");last=m;}
    const LONG t=InterlockedExchange(&g_case_turned,0);
    if(t)Log("CaseHeartItems turned %ld saved record(s) to 2 x 1",t);
    const LONG x=InterlockedExchange(&g_case_mixed,0);
    if(x)Log("CaseHeartItems mix: %ld heart piece(s) given the heart row",x);
}
// MortalDrawPrompt/Key/PadButton/MaskCheck ([LinkRuntime], read only with MortalDrawContextual=1) and
// BIO4/input.ini (read only) for the key resolution. Pins are checked in the worker once the base is known.
static void CmbPromptConfig(const wchar_t* ini){
    const unsigned on=GetPrivateProfileIntW(L"LinkRuntime",L"MortalDrawPrompt",1,ini);
    if(on>1)Log("MortalDrawPrompt invalid switch; only exact 1 enables it");
    g_cmb_prompt_cfg.on=on==1;
    g_cmb_prompt_cfg.mask_check=GetPrivateProfileIntW(L"LinkRuntime",L"MortalDrawMaskCheck",1,ini)==1;
    auto narrow=[&](const wchar_t* name,const wchar_t* fallback,char (&out)[32]){
        wchar_t wide[32]{};GetPrivateProfileStringW(L"LinkRuntime",name,fallback,wide,32,ini);
        size_t i=0;for(;wide[i]&&i<31;++i)out[i]=wide[i]<128?static_cast<char>(wide[i]):'?';out[i]=0;
    };
    char key[32]{},pad[32]{};narrow(L"MortalDrawKey",L"E",key);narrow(L"MortalDrawPadButton",L"None",pad);
    std::vector<char> input;   // <game>/BIO4/input.ini, sibling of Bin32
    wchar_t input_path[MAX_PATH]{};
    if(GetModuleFileNameW(nullptr,input_path,MAX_PATH)){
        wchar_t* slash=std::wcsrchr(input_path,L'\\');if(slash)*slash=0;
        slash=std::wcsrchr(input_path,L'\\');
        if(slash&&link_runtime::paths::Copy(slash+1,MAX_PATH-static_cast<size_t>(slash+1-input_path),L"BIO4\\input.ini")==0){
            FILE* file=nullptr;
            if(!_wfopen_s(&file,input_path,L"rb")&&file){
                char chunk[4096];size_t n=0;
                while((n=std::fread(chunk,1,sizeof(chunk),file))>0&&input.size()<1024*1024)input.insert(input.end(),chunk,chunk+n);
                std::fclose(file);
            }
        }
    }
    input.push_back(0);
    g_cmb_prompt_cfg.key=mdp::ResolveKey(key,input.size()>1?input.data():nullptr);
    g_cmb_offer=mdp::OfferFor(g_cmb_prompt_cfg.key.mode);
    static const char* const why[]={"default","same_as_native","unbound","unverified","invalid","reserved","clash"};
    Log("md_key mode=%s vk=0x%02X why=%s setting=%s input_ini=%u prompt=%u kind=0x%02X mask_check=%u",
        g_cmb_prompt_cfg.key.mode==mdp::KeyMode::Action?"action":"dedicated",g_cmb_prompt_cfg.key.vk,
        why[static_cast<unsigned>(g_cmb_prompt_cfg.key.why)],key,input.size()>1?1U:0U,g_cmb_prompt_cfg.on?1U:0U,g_cmb_offer.kind,
        g_cmb_prompt_cfg.mask_check?1U:0U);
    if(g_cmb_prompt_cfg.key.mode==mdp::KeyMode::Physical)
        Log("md_key dedicated: the prompt text must be rebuilt for this key (message 0x5B, text_rename --mortal-key)");
    if(mdp::ParsePadButton(pad)!=0)Log("MortalDrawPadButton=%s ignored: no pad reader is wired for the dedicated key",pad);
}
// Task 3b probe (sword_trail_probe.h): SwordTrailProbe exact 1; its 5 s counters from the worker.
static bool g_sword_trail_probe=false;
static uint64_t g_sword_trail_probe_next=0;
static void StartSwordTrailProbe(unsigned char* base,uint32_t player_slot,uint32_t global_slot,bool dispatch_up,bool ot22,bool restore){
    namespace stp=link_runtime::sword_trail::probe;
    stp::runtime::Mem m;const uint32_t image=reinterpret_cast<uint32_t>(base);
    const auto why=stp::Gate(m,image,true,dispatch_up,player_slot&&global_slot);
    if(why==stp::Why::Ready)stp::runtime::Start(image,player_slot,global_slot,stp::runtime::ResolveNatives(image),ot22,restore);
    g_sword_trail_probe=why==stp::Why::Ready;
    Log("sword_trail_probe ready=%d world_table=%u ot22=%d",g_sword_trail_probe?1:0,unsigned(stp::kWorldTable),g_sword_trail_probe&&ot22?1:0);
    if(g_sword_trail_probe&&!restore)Log("sword_trail_probe restore=0: prior GX state not re-issued (debug)");
    if(!g_sword_trail_probe)Log("sword_trail_probe off: %s",stp::WhyName(why));
    g_sword_trail_probe_next=GetTickCount64()+5000;
}
static void PollSwordTrailProbe(){
    if(!g_sword_trail_probe||GetTickCount64()<g_sword_trail_probe_next)return;
    g_sword_trail_probe_next=GetTickCount64()+5000;
    const auto s=link_runtime::sword_trail::probe::runtime::Snapshot();
    Log("sword_trail_probe frames=%u world_regs=%u ot22_regs=%u fails=%u",s.frames,s.world_regs,s.ot22_regs,s.fails);
    Log("sword_trail_probe draws world=%u ot22=%u draw_fails=%u restore_missing=%u",s.world_draws,s.ot22_draws,s.draw_fails,s.restore_missing);
}
// bughunt-01 (ot_world_place.h): whether the table-17 drawers read the camera AddOtWorldPos uses (CameraGetLookVecInverse
// pins), from the first drawer that starts; one line per process.
static void LogOtPlace(const link_runtime::sword_trail::ot::runtime::Natives& natives){
    static bool logged=false;
    if(logged)return;
    logged=true;
    Log("ot_world_place camera=%d (table-17 nodes at the centroid, clamped past zlimit, or skipped wholly behind/near the eye)",
        natives.place?1:0);
}
// Task 4a sword trail (sword_trail_draw_ot.h): SwordTrail (missing or 1 = on) with SpinAttack=1, the player dispatch
// and kRuntimePins; its stats every 5 s from the worker while it drew anything.
static uint64_t g_trail_next=0;
static unsigned g_trail_last_drawn=0,g_trail_last_fails=0,g_trail_last_identity=0,g_trail_last_hidden=0;
static void StartSwordTrail(unsigned char* base,uint32_t player_slot,uint32_t global_slot,bool dispatch_up){
    namespace gx=link_runtime::sword_trail::gx;namespace sot=link_runtime::sword_trail::ot;
    const uint32_t image=reinterpret_cast<uint32_t>(base);
    const char* reason=nullptr;
    if(!g_trail_cfg.on)reason="SwordTrail=0";
    else if(!g_cmb.spin_attack)reason="needs SpinAttack=1";
    else {gx::SehMem m;const auto why=gx::Gate(m,image,true,dispatch_up,player_slot&&global_slot);if(why!=gx::Why::Ready)reason=gx::WhyName(why);}
    if(!reason){const auto natives=sot::runtime::ResolveNatives(image);sot::runtime::Start(image,global_slot,natives);LogOtPlace(natives);}
    g_trail_on.store(!reason,std::memory_order_release);
    char moves[64]{};link_runtime::sword_trail::feed::FormatMoves(moves,sizeof(moves),g_trail_cfg);
    Log("sword_trail ready=%d moves=%s length_ms=%u back_ms=%u ending_ms=%u slash_ms=%u",reason?0:1,moves,g_trail_cfg.length_ms,
        g_trail_cfg.back_length_ms,g_trail_cfg.ending_length_ms,g_trail_cfg.slash_length_ms);
    if(!reason&&g_trail_cfg.Wants(link_runtime::sword_trail::feed::Move::Slash))
        Log("sword_trail slash=1: native knife slash = knife routine 0x0B with r_no_2 2 (fire), our clips win");
    if(reason)Log("sword_trail off: %s",reason);
    if(g_trail_cfg.problems)Log("sword_trail: %u invalid values; their defaults kept",g_trail_cfg.problems);
    g_trail_next=GetTickCount64()+5000;
}
static void PollSwordTrail(){
    if(!g_trail_on.load(std::memory_order_acquire)||GetTickCount64()<g_trail_next)return;
    g_trail_next=GetTickCount64()+5000;
    const auto s=link_runtime::sword_trail::ot::runtime::Snapshot();
    const unsigned fails=s.reg_fails+s.draw_fails,identity=g_trail.IdentityFails(),hidden=s.reg_behind+s.reg_near;
    // Nothing drawn, failed or hidden in these 5 s. A spin whose sword was never identified draws nothing, so it is news too.
    if(s.frames_drawn==g_trail_last_drawn&&fails==g_trail_last_fails&&identity==g_trail_last_identity&&
       hidden==g_trail_last_hidden)return;
    g_trail_last_drawn=s.frames_drawn;g_trail_last_fails=fails;g_trail_last_identity=identity;g_trail_last_hidden=hidden;
    char line[512];
    if(link_runtime::sword_trail::ot::runtime::FormatStats(line,sizeof(line),g_trail.Spins(),g_trail.Samples(),g_trail.Cuts(),
        identity,s)>0)Log("%s",line);
    if(link_runtime::sword_trail::feed::FormatMoveCounts(line,sizeof(line),g_trail)>0)Log("%s",line);   // trail-moves-01: per move
}
// Sword charge glow (sword_charge_draw.h): SwordCharge (missing or 1 = on) with SpinAttack=1, the player dispatch and
// kRuntimePins; SwordChargeSound loads Bin32/link_sword_charge.wav (SHA-pinned). Its stats every 5 s from the worker
// while it changes.
static uint64_t g_charge_next=0;
static unsigned g_charge_last_charges=0,g_charge_last_drawn=0,g_charge_last_fails=0,g_charge_last_place=0;
static void StartSwordCharge(unsigned char* base,uint32_t player_slot,uint32_t global_slot,bool dispatch_up,const wchar_t* directory){
    namespace gx=link_runtime::sword_trail::gx;namespace sot=link_runtime::sword_trail::ot;
    namespace scd=link_runtime::sword_charge::draw;
    const uint32_t image=reinterpret_cast<uint32_t>(base);
    const char* reason=nullptr;
    if(!g_charge_cfg.on)reason="SwordCharge=0";
    else if(!g_cmb.spin_attack)reason="needs SpinAttack=1";
    else if(!g_cmb.sword_hand)reason="needs SwordInHand=1";   // the glow follows the Master Sword in Link's hand
    else {gx::SehMem m;const auto why=gx::Gate(m,image,true,dispatch_up,player_slot&&global_slot);if(why!=gx::Why::Ready)reason=gx::WhyName(why);}
    if(!reason){const auto natives=sot::runtime::ResolveNatives(image);scd::runtime::Start(image,global_slot,natives);LogOtPlace(natives);}
    bool sound=false;
    if(!reason&&g_charge_cfg.sound&&directory){
        wchar_t path[MAX_PATH]{};
        sound=link_runtime::paths::Format(path,L"%ls%ls",directory,link_runtime::sword_charge::kSoundFile)>0&&
            g_charge_sound.Load(path,kGssbVolume,link_runtime::sword_charge::kSoundSha);
        if(!sound)Log("sword_charge sound: Bin32/link_sword_charge.wav missing or changed (the glow still shows)");
    }
    g_charge_on.store(!reason,std::memory_order_release);
    char line[128];
    if(link_runtime::sword_charge::FormatReady(line,sizeof(line),!reason,g_charge_cfg,sound)>0)Log("%s",line);
    if(reason)Log("sword_charge off: %s",reason);
    if(g_charge_cfg.problems)Log("sword_charge: %u invalid values; their defaults kept",g_charge_cfg.problems);
    g_charge_next=GetTickCount64()+5000;
}
static void PollSwordCharge(){
    if(!g_charge_on.load(std::memory_order_acquire)||GetTickCount64()<g_charge_next)return;
    g_charge_next=GetTickCount64()+5000;
    const auto s=link_runtime::sword_charge::draw::runtime::Snapshot();
    const unsigned charges=g_charge.Charges(),fails=s.reg_fails+s.draw_fails,place=s.reg_behind+s.reg_near+s.reg_clamped;
    if(charges==g_charge_last_charges&&s.frames_drawn==g_charge_last_drawn&&fails==g_charge_last_fails&&
       place==g_charge_last_place)return;
    g_charge_last_charges=charges;g_charge_last_drawn=s.frames_drawn;
    char line[256];
    if(link_runtime::sword_charge::FormatStats(line,sizeof(line),charges,g_charge.Fulls(),s.frames_drawn,s.draw_fails,
        s.restore_missing)>0)Log("%s",line);
    if(fails!=g_charge_last_fails||place!=g_charge_last_place)
        Log("sword_charge fails reg_fails=%u draw_fails=%u restore_missing=%u restore_skipped=%u batch_closed=%u reg_refused=%u "
            "reg_behind=%u reg_near=%u reg_clamped=%u",s.reg_fails,s.draw_fails,s.restore_missing,s.restore_skipped,s.batch_closed,
            s.reg_refused,s.reg_behind,s.reg_near,s.reg_clamped);
    g_charge_last_fails=fails;g_charge_last_place=place;
}
// --- sword-particles-01 (21-oct): TP sword particles (sword_particles.h, sword_particles_draw.h). SwordParticles (missing
// or 1 = on) with SwordTrail on (the contact search reads its blade samples), the player dispatch and kRuntimePins; the
// wall group also needs the EatMgr nearest-hit pins (ztarget_los.h kNearestPins, sword-particles-02). Its stats every 5 s
// from the worker while they change.
static uint64_t g_part_next=0;
static unsigned g_part_last_sum=0,g_part_last_fails=0,g_part_last_place=0;
static void StartSwordParticles(unsigned char* base,uint32_t player_slot,uint32_t global_slot,bool dispatch_up){
    namespace gx=link_runtime::sword_trail::gx;namespace sot=link_runtime::sword_trail::ot;
    namespace spd=link_runtime::sword_particles::draw;
    const uint32_t image=reinterpret_cast<uint32_t>(base);
    const char* reason=nullptr;
    if(!g_part_cfg.on)reason="SwordParticles=0";
    else if(!g_trail_on.load(std::memory_order_acquire))reason="needs SwordTrail=1 (its blade samples)";
    else {gx::SehMem m;const auto why=gx::Gate(m,image,true,dispatch_up,player_slot&&global_slot);if(why!=gx::Why::Ready)reason=gx::WhyName(why);}
    const bool los=!reason&&PinsAt(image,link_runtime::ztarget::los::kNearestPins);
    g_part_wall_ok=los;
    if(!reason){const auto natives=sot::runtime::ResolveNatives(image);spd::runtime::Start(image,global_slot,natives);LogOtPlace(natives);}
    g_part_on.store(!reason,std::memory_order_release);
    char line[256];
    if(link_runtime::sword_particles::FormatReady(line,sizeof(line),!reason,g_part_cfg,los)>0)Log("%s",line);
    if(!reason&&g_part_cfg.hit)Log("sword_particles hit_range=%.0f reach=%.0f",link_runtime::sword_particles::HitRange(ParticlesReach()),
        ParticlesReach());   // the farthest configured move reach + margin (spin_attack_runtime.h ParticlesReach)
    if(reason)Log("sword_particles off: %s",reason);
    else if(g_part_cfg.wall&&!los)Log("sword_particles wall off: EatMgr nearest-hit pins failed (hits and spins only)");
    if(g_part_cfg.problems)Log("sword_particles: %u invalid values; their defaults kept",g_part_cfg.problems);
    if(g_part_cfg.wood_attr_ignored)
        Log("sword_particles: SwordParticlesWoodAttr is ignored; list wall effect types 0..7 in SwordParticlesWoodTypes");
    g_part_next=GetTickCount64()+5000;
}
static void PollSwordParticles(){
    if(!g_part_on.load(std::memory_order_acquire)||GetTickCount64()<g_part_next)return;
    g_part_next=GetTickCount64()+5000;
    const auto s=link_runtime::sword_particles::draw::runtime::Snapshot();
    const unsigned hits=g_part_hits.load(),walls=g_part_walls.load(),spins=g_part_spins.load();
    namespace spr=link_runtime::sword_particles::draw::runtime;
    const unsigned sum=hits+walls+spins+s.frames_drawn,
        fails=s.reg_fails+s.draw_fails+s.arena_cuts+s.quad_cuts+g_part_wall_faults.load()+g_part.Overflow()+g_part.Refused(),
        place=spr::reg_behind.load()+spr::reg_near.load()+spr::reg_clamped.load();
    if(sum==g_part_last_sum&&fails==g_part_last_fails&&place==g_part_last_place)return;
    g_part_last_sum=sum;
    char line[256];
    if(link_runtime::sword_particles::FormatStats(line,sizeof(line),hits,walls,spins,g_part_alive.load(),s.quads,s.reg_fails,
        s.draw_fails)>0)Log("%s",line);
    if(fails!=g_part_last_fails)
        Log("sword_particles fails reg_fails=%u draw_fails=%u restore_missing=%u restore_skipped=%u batch_closed=%u arena_cuts=%u "
            "quad_cuts=%u wall_faults=%u overflow=%u refused=%u",s.reg_fails,s.draw_fails,s.restore_missing,s.restore_skipped,
            s.batch_closed,s.arena_cuts,s.quad_cuts,g_part_wall_faults.load(),g_part.Overflow(),g_part.Refused());
    if(fails!=g_part_last_fails||place!=g_part_last_place)
        Log("sword_particles reg_reasons wrong_thread=%u no_view=%u build=%u refused=%u behind=%u near=%u clamped=%u",
            spr::reg_wrong_thread.load(),spr::reg_no_view.load(),spr::reg_build.load(),spr::reg_refused.load(),
            spr::reg_behind.load(),spr::reg_near.load(),spr::reg_clamped.load());
    g_part_last_fails=fails;g_part_last_place=place;
}
// --- end sword-particles-01
// Z-target 3D arrow (ztarget_arrow3d.h): [ZTarget] Reticle3D exact 1 with ZTarget=1, Reticle=1 (it is that cursor drawn
// in 3D: Reticle=0 keeps no cursor at all), the player dispatch and kRuntimePins; the drawer starts here, before
// ztarget::runtime::Start, which turns it on with the camera and installs the 2D cursor muted. Reticle3D=0: nothing
// read, no line. Its stats every 5 s from the worker while they change.
static bool g_arrow_requested=false;
static uint64_t g_arrow_next=0;
static unsigned g_arrow_last_drawn=0,g_arrow_last_fails=0,g_arrow_last_feed=0,g_arrow_last_place=0;
static void StartZTargetArrow(unsigned char* base,uint32_t player_slot,uint32_t global_slot,bool dispatch_up,const wchar_t* path){
    namespace gx=link_runtime::sword_trail::gx;namespace sot=link_runtime::sword_trail::ot;
    namespace zt=link_runtime::ztarget::runtime;namespace za=link_runtime::ztarget::arrow3d;
    g_arrow_requested=zt::Switch(path,L"ZTarget",L"Reticle3D");
    if(!g_arrow_requested)return;
    const uint32_t image=reinterpret_cast<uint32_t>(base);
    const char* reason=nullptr;
    if(!zt::Switch(path,L"LinkRuntime",L"ZTarget"))reason="needs ZTarget=1";
    else if(!zt::Switch(path,L"ZTarget",L"Reticle"))reason="needs Reticle=1";
    else {gx::SehMem m;const auto why=gx::Gate(m,image,true,dispatch_up,player_slot&&global_slot);if(why!=gx::Why::Ready)reason=gx::WhyName(why);}
    if(!reason){const auto natives=sot::runtime::ResolveNatives(image);za::runtime::Start(image,global_slot,natives);LogOtPlace(natives);}
    zt::arrow_request={true,reason};
    g_arrow_next=GetTickCount64()+5000;
}
// After ztarget::runtime::Start: on, or why not (the 2D cursor then paints as before).
static void ZTargetArrowReady(){
    if(!g_arrow_requested)return;
    namespace zt=link_runtime::ztarget::runtime;
    const bool on=zt::arrow_on&&zt::live.load(std::memory_order_acquire);
    const char* reason=on?"ok":zt::arrow_request.refused?zt::arrow_request.refused:"ZTarget not ready";
    const bool hud2d=link_runtime::ztarget::hud::ready.load(std::memory_order_acquire);
    const char* reticle2d=!hud2d?"off":on?"muted":"on";
    char line[256];
    if(link_runtime::ztarget::arrow3d::FormatReady(line,sizeof(line),on,reason,reticle2d)>0)Log("%s",line);
    if(!on)Log("ztarget_arrow3d off: %s; %s",reason,hud2d?"the 2D cursor stays":"no 2D cursor either");
}
static void PollZTargetArrow(){
    if(!link_runtime::ztarget::runtime::arrow_on||GetTickCount64()<g_arrow_next)return;
    g_arrow_next=GetTickCount64()+5000;
    const auto s=link_runtime::ztarget::arrow3d::runtime::Snapshot();
    const unsigned fails=s.reg_fails+s.draw_fails,feed=s.stale+s.room+s.unsafe+s.wrong_thread,
        place=s.reg_behind+s.reg_near+s.reg_clamped;
    if(s.frames_drawn==g_arrow_last_drawn&&fails==g_arrow_last_fails&&feed==g_arrow_last_feed&&place==g_arrow_last_place)return;
    const bool place_news=place!=g_arrow_last_place||s.reg_refused;
    g_arrow_last_drawn=s.frames_drawn;g_arrow_last_fails=fails;g_arrow_last_feed=feed;g_arrow_last_place=place;
    char line[256];
    if(link_runtime::ztarget::arrow3d::FormatStats(line,sizeof(line),s.frames_drawn,s.quads,s.reg_fails,s.draw_fails,
        s.restore_missing)>0)Log("%s",line);
    if(link_runtime::ztarget::arrow3d::FormatFeed(line,sizeof(line),s.stale,s.room,s.unsafe,s.wrong_thread,s.restore_skipped,
        s.batch_closed)>0)Log("%s",line);
    if(place_news&&link_runtime::ztarget::arrow3d::FormatPlace(line,sizeof(line),s.reg_refused,s.reg_behind,s.reg_near,
        s.reg_clamped)>0)Log("%s",line);
}
// skills-fix-01 (thread_binding.h): a hook's game thread handed over because the game replaced its thread (after the
// title screen). One line per hand-over; nothing is read from the game.
static void PollThreadBindings(){
    struct Watched {const char* hook;const link_runtime::thread_binding::Binding* binding;unsigned seen;};
    static Watched watched[]={{"dispatch",&mortal::dispatch_hooks::binding,0},
        {"camera",&link_runtime::ztarget::runtime::front_binding,0},{"trail",&link_runtime::sword_trail::ot::runtime::owner,0},
        {"charge",&link_runtime::sword_charge::draw::runtime::owner,0},{"particles",&link_runtime::sword_particles::draw::runtime::owner,0},
        {"arrow3d",&link_runtime::ztarget::arrow3d::runtime::owner,0},{"probe",&link_runtime::sword_trail::probe::runtime::owner,0},
        {"parry_input",&link_runtime::parry::input::binding,0},{"parry_epoch",&link_runtime::parry::sword::epoch::binding,0},
        {"zelda_cast",&link_runtime::zelda_cast::detail::binding,0},{"reticle",&link_runtime::ztarget::hud::render_binding,0}};
    for(auto& w:watched){
        const unsigned n=w.binding->Rebinds();if(n==w.seen)continue;
        w.seen=n;Log("game_thread rebound hook=%s from=%lu to=%lu n=%u",w.hook,w.binding->Previous(),w.binding->Id(),n);
    }
}
static DWORD WINAPI Worker(LPVOID) {
    // Keep this process-lifetime worker from executing code in an unloaded proxy.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                      reinterpret_cast<LPCWSTR>(&Worker), &pinned);
    wchar_t path[MAX_PATH] = {}, process_path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(g_module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return 0;
    wchar_t* name = std::wcsrchr(path, L'\\');
    if (!name) return 0;
    // LogLevel is read before the log opens (link_runtime_log.h): 0 opens no log file at all.
    wchar_t log_level_text[16] = {}, old_log[MAX_PATH] = {};
    if(link_runtime::paths::Copy(name + 1, MAX_PATH - static_cast<size_t>(name + 1 - path), L"link_runtime.ini"))return 0;
    GetPrivateProfileStringW(L"LinkRuntime", L"LogLevel", L"", log_level_text, 16, path);
    const bool log_level_valid = link_runtime::log::ParseLevel(log_level_text, g_log_level);
    link_runtime::log::Prepared log_start;
    if (g_log_level != link_runtime::log::kOff) {
        if(link_runtime::paths::Copy(name + 1, MAX_PATH - static_cast<size_t>(name + 1 - path), L"link_runtime.old.log"))return 0;
        if(link_runtime::paths::Copy(old_log, path))return 0;
        if(link_runtime::paths::Copy(name + 1, MAX_PATH - static_cast<size_t>(name + 1 - path), L"link_runtime.log"))return 0;
        log_start = link_runtime::log::PrepareFile(path, old_log);
        g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    Log("link runtime x86 loaded, pid=%lu", GetCurrentProcessId());
    Log("link runtime version=%s file_version=%s log_level=%u previous_log_bytes=%llu rotated=%u truncated=%u cap_bytes=%llu",
        LINK_RUNTIME_PRODUCT_VERSION, LINK_RUNTIME_FILE_VERSION, g_log_level, log_start.previous_bytes,
        log_start.rotated ? 1U : 0U, log_start.truncated ? 1U : 0U, link_runtime::log::kRotateBytes);
    if (!log_level_valid && log_level_text[0]) Log("LogLevel invalid; only exact 0, 1 or 2; 1 used");
    // Steam initializes CNG while holding the loader lock. Starting our hashes
    // concurrently can invert CNG's provider lock and the loader lock. The game
    // creates DirectInput after Steam startup; wait for that real lifecycle edge,
    // without blocking the host thread or relying on a fixed startup delay.
    Log("startup waiting for DirectInput");
    while (!InterlockedCompareExchange(&g_input_ready, 0, 0)) Sleep(10);
    Log("startup DirectInput ready");
    if(link_runtime::paths::Copy(name + 1, MAX_PATH - static_cast<size_t>(name + 1 - path), L"link_runtime.ini"))return 0;
    bool enabled = GetPrivateProfileIntW(L"LinkRuntime", L"SwordSheath", 0, path) == 1;
    bool low_health = GetPrivateProfileIntW(L"LinkRuntime", L"LowHealth", 0, path) == 1;
    const bool low_health_shop_fix = GetPrivateProfileIntW(L"LinkRuntime", L"LowHealthShopFlagFix", 1, path) == 1;
    g_cmb_shop_flag_fix = GetPrivateProfileIntW(L"LinkRuntime", L"CombatShopFlagFix", 1, path) == 1;
    Log("combat shop_flag_fix=%u (CombatShopFlagFix)", g_cmb_shop_flag_fix ? 1U : 0U);
    bool trace = GetPrivateProfileIntW(L"LinkRuntime", L"TraceKnife", 0, path) == 1;
    link_runtime::chapter::Config chapter_settings;
    chapter_settings.enabled=ChapterIniSwitch(path,L"ChapterProgression");
    chapter_settings.trace=ChapterIniSwitch(path,L"TraceChapter");
    chapter_settings.master_from_castle=ChapterIniSwitch(path,L"MasterSwordFromCastle");
    chapter_settings.limit=GetPrivateProfileIntW(L"LinkRuntime",L"ChapterTraceMaxEvents",100,path);
    if(!chapter_settings.limit||chapter_settings.limit>1000)chapter_settings.limit=100;
    const unsigned shield_switch=GetPrivateProfileIntW(L"LinkRuntime",L"ShieldObserve",0,path);
    const bool defense_requested=ChapterIniSwitch(path,L"ParryActive");
    const bool parry_requested=defense_requested||ChapterIniSwitch(path,L"ParryObserve");
    for(unsigned i=0;i<link_runtime::parry::direct::kCount;++i)
        link_runtime::parry::direct::requested[i]=GetPrivateProfileIntW(L"LinkRuntime",link_runtime::parry::direct::kSpecs[i].key,link_runtime::parry::direct::kSpecs[i].on?1:0,path)==1;
    g_parry_line_requested=GetPrivateProfileIntW(L"LinkRuntime",L"ParryLineBlock",1,path)==1;   // 1-nov: on (missing = on), as both INIs
    unsigned general_problems=0;
    g_gen.settings=link_runtime::parry::general::ReadSettings([&](const wchar_t* key,wchar_t* out,size_t count){
        GetPrivateProfileStringW(L"LinkRuntime",key,L"",out,static_cast<DWORD>(count),path);},defense_requested,parry_requested,general_problems);
    if(general_problems)Log("ParryGeneral: invalid ParryGeneralBlock/Bosses/Thrown value or unmet dependency (1 needs ParryActive=1, 2 needs the parry transport); tier 2 off");
    {   // Heavy parry pushback (parry-heavy-01): ParryHeavyPower / ParryHeavyPushback / ParryHeavyFrames
        unsigned push_problems=0;
        g_push_cfg=link_runtime::parry::pushback::ReadConfig([&](const wchar_t* key,wchar_t* out,size_t count){
            GetPrivateProfileStringW(L"LinkRuntime",key,L"",out,static_cast<DWORD>(count),path);},push_problems);
        Log("ParryHeavy power>=%u pushback=%.0f frames=%.0f problems=%u",g_push_cfg.heavy_power,static_cast<double>(g_push_cfg.distance),
            static_cast<double>(g_push_cfg.frames),push_problems);
    }
    {   // Post-parry grace window (game time; 0 = off; only with ParryGeneralBlock=1)
        const unsigned grace_raw=GetPrivateProfileIntW(L"LinkRuntime",L"ParryGraceMs",200,path);
        g_gen.grace.Configure(grace_raw);
        if(link_runtime::parry::general::GraceMs(grace_raw)!=grace_raw)Log("ParryGraceMs=%u invalid (0..1000); grace window off",grace_raw);
        Log("ParryGrace window=%.1f frames of game time (ParryGraceMs=%u); active only with ParryGeneralBlock=1",g_gen.grace.window,grace_raw);
    }
    {   // Parry press window (wall ms; one press per hit; 100..1000, else 750)
        const unsigned window_raw=GetPrivateProfileIntW(L"LinkRuntime",L"ParryWindowMs",750,path);
        g_parry_window_ms=link_runtime::defense::ParryWindowMs(window_raw);
        if(g_parry_window_ms!=window_raw)Log("ParryWindowMs=%u invalid (100..1000); using %u",window_raw,g_parry_window_ms);
    }
    const unsigned fairy_switch=GetPrivateProfileIntW(L"FairyRevive",L"Enabled",0,path);
    const bool fairy_requested=fairy_switch==1;
    const bool fairy_sound_requested=fairy_requested&&GetPrivateProfileIntW(L"FairyRevive",L"Sound",1,path)==1;
    unsigned fairy_volume=GetPrivateProfileIntW(L"FairyRevive",L"SoundVolumePercent",60,path);
    if(fairy_volume>100)fairy_volume=60;
    if(fairy_switch>1)Log("FairyRevive invalid switch; only exact 1 enables it");
    const unsigned rupee_switch=GetPrivateProfileIntW(L"Rupees",L"Enabled",0,path);
    const bool rupees_requested=rupee_switch==1;
    if(rupee_switch>1)Log("Rupees invalid switch; only exact 1 enables it");
    // Boss stream ducks the zone MIDI blocks (boss_music_duck.h). Exact 1 only.
    // CombatMusicDuck: the Zelda combat streams (tp-zone-music-02 cues + village 0x02) duck them too. Exact 1 only.
    const bool boss_key=ChapterIniSwitch(path,L"BossMusicDuck"),combat_key=ChapterIniSwitch(path,L"CombatMusicDuck");
    const bool boss_duck_requested=boss_key||combat_key;
    // Event Link's sheathed sword hidden while the event knife (Master Sword) is drawn (event_sheath.h). Exact 1 only.
    const bool sheath_requested=ChapterIniSwitch(path,L"EventSheathHide");
    unsigned ocarina_problems=0;
    g_oca_cfg=ocr::ReadConfig([&](const wchar_t* key,wchar_t* out,size_t count){
        GetPrivateProfileStringW(L"Ocarina",key,L"",out,static_cast<DWORD>(count),path);},ocarina_problems);
    if(ocarina_problems)Log("Ocarina: %u invalid binding/volume values or Freeze without Pose; defaults kept, Freeze=%u",ocarina_problems,g_oca_cfg.freeze?1U:0U);
    g_oca_song_cfg=osg::ReadConfig([&](const wchar_t* key,wchar_t* out,size_t count){
        GetPrivateProfileStringW(L"Ocarina",key,L"",out,static_cast<DWORD>(count),path);});
    if(g_oca_song_cfg.problems)Log("Ocarina songs: %u invalid values (radius / OcarinaSongPlayback); defaults kept",g_oca_song_cfg.problems);
    g_oca_staff_on=GetPrivateProfileIntW(L"Ocarina",L"Staff",0,path)==1;
    g_oca_manual_on=GetPrivateProfileIntW(L"Ocarina",L"Manual",1,path)==1;   // default on; needs Item=1
    g_hud_cfg=htp::ReadConfig([&](const wchar_t* key,unsigned fallback){return static_cast<unsigned>(GetPrivateProfileIntW(L"Hud",key,static_cast<INT>(fallback),path));});
    {   // ztarget-letterbox-01: LetterboxOrder is a word; the INI path for the poll thread's live re-read
        wchar_t word[16]{};bool start=false;
        GetPrivateProfileStringW(L"Hud",L"LetterboxOrder",L"",word,16,path);
        if(!htp::ParseOrder(word,start))++g_hud_cfg.problems;
        g_hud_cfg.letterbox_start=start;
        if(link_runtime::paths::Copy(g_hud_ini,path)!=0)g_hud_ini[0]=0;
    }
    if(g_hud_cfg.problems)Log("Hud: %u invalid values (switches take only exact 1; HeartLife 40..600, multiple of 4; HeartRefillVolumePercent 0..100; ZTargetLetterboxHeight 4..10; LetterboxOrder end|start; LetterboxClipX 0..320; LetterboxTopRows 4..40); those keep their defaults",g_hud_cfg.problems);
    g_cmb=cmb::ReadConfig([&](const wchar_t* key,wchar_t* out,size_t count){
        GetPrivateProfileStringW(L"LinkRuntime",key,L"",out,static_cast<DWORD>(count),path);},defense_requested);
    if(g_cmb.problems)Log("Combat: %u invalid values or unmet dependencies (Damage needs Contextual, parry keys need ParryActive); switches, unmet dependencies and a bad ParryRiposteMilli stay off, other numbers keep their defaults",g_cmb.problems);
    if(g_cmb.leap_problems)Log("EndingBlowLeap: %u invalid values; their defaults kept",g_cmb.leap_problems);
    if(g_cmb.lift_problems)Log("EndingBlowCameraLift: %u invalid values; their defaults kept",g_cmb.lift_problems);
    // E1 sword trail log (spin_attack_runtime.h SwordTrail*): exact 1, with SpinAttack=1 and LogLevel=2 (lines every frame).
    if(ChapterIniSwitch(path,L"SwordTrailLog")){
        g_sword_trail_log=g_cmb.spin_attack&&link_runtime::log::Writes(g_log_level,link_runtime::log::kVerbose);
        Log(g_sword_trail_log?"sword_trail log on: blade and camera every frame of a spin":"sword_trail log off: needs SpinAttack=1 and LogLevel=2");
    }
    if(g_cmb.md_contextual){
        g_cmb_magnet_cfg=mmag::ReadConfig([&](const wchar_t* key,wchar_t* out,size_t count){
            GetPrivateProfileStringW(L"LinkRuntime",key,L"",out,static_cast<DWORD>(count),path);});
        if(g_cmb_magnet_cfg.problems)Log("MortalDrawMagnet: %u invalid values; their defaults kept",g_cmb_magnet_cfg.problems);
        CmbPromptConfig(path);
    }else g_cmb_magnet_cfg.enabled=false;
    // user-ini-01 (22-oct): the cosmetic keys below (trail, charge glow, particles) read Bin32/link_runtime.user.ini
    // first (the F8 panel's overrides, key by key) and are re-read live by the worker (UserIniPoll); a path that does
    // not fit keeps the main INI only, as before.
    unsigned user_file_problems=0;
    const bool user_ini=UserIniOpen(path,user_file_problems);
    auto cosmetic=[&](const wchar_t* key,wchar_t* out,size_t count){
        if(user_ini)UserIniGet(key,out,count);
        else GetPrivateProfileStringW(L"LinkRuntime",key,L"",out,static_cast<DWORD>(count),path);};
    // Task 4a sword trail (sword_trail_feed.h): SwordTrail* keys, read always; started after the player dispatch.
    g_trail_cfg=link_runtime::sword_trail::feed::ReadConfig(cosmetic);
    // Sword charge glow (sword_charge.h): SwordCharge* keys, read always; started after the player dispatch.
    g_charge_cfg=link_runtime::sword_charge::ReadConfig(cosmetic);
    // sword-particles-01 (sword_particles.h): SwordParticles* keys, read always; started after the trail.
    g_part_cfg=link_runtime::sword_particles::ReadConfig(cosmetic);
    // ---- F-11 Cucco feathers (cucco_feathers.h): CuccoFeathers* keys, main INI only; exact 1, started after the particles.
    g_cucco_cfg=link_runtime::cucco_feathers::ReadConfig([&](const wchar_t* key,wchar_t* out,size_t count){
        GetPrivateProfileStringW(L"LinkRuntime",key,L"",out,static_cast<DWORD>(count),path);});
    // ---- end F-11
    // Task 3b probe (sword_trail_probe.h): exact 1, default 0; started after the player dispatch, never otherwise.
    const bool sword_trail_probe_requested=ChapterIniSwitch(path,L"SwordTrailProbe");
    const bool sword_trail_probe_ot22=ChapterIniSwitch(path,L"SwordTrailProbeOt22");   // exact 1, default 0
    const bool sword_trail_probe_restore=GetPrivateProfileIntW(L"LinkRuntime",L"SwordTrailProbeRestore",1,path)!=0;
    g_rear_observe=ChapterIniSwitch(path,L"RearShieldObserve");
    g_rear_active=ChapterIniSwitch(path,L"RearShield");
    link_runtime::shield::runtime::rear_observe=g_rear_observe;
    link_runtime::shield::runtime::rear_active=g_rear_active;
    {const unsigned raw=GetPrivateProfileIntW(L"LinkRuntime",L"RearShieldDamagePercent",link_runtime::defense::kRearPercentDefault,path);
     link_runtime::shield::runtime::rear_kept_percent=link_runtime::defense::RearPercent(raw);
     if(raw!=link_runtime::shield::runtime::rear_kept_percent)Log("RearShieldDamagePercent=%u invalid (50..100): %u",raw,link_runtime::shield::runtime::rear_kept_percent);}
    if(g_rear_active)Log("RearShield adapter requested: angle+height per hit (rear 120 deg, shield band, standing/walking/running, "
        "Ganado axe/sickle/hoe/scythe; %u%% of the damage kept); each impact is qualified separately, other hits keep native damage",
        link_runtime::shield::runtime::rear_kept_percent);
    const bool preclamp_requested=ChapterIniSwitch(path,L"ShieldObserve")&&ChapterIniSwitch(path,L"ShieldPreClampObserve");
    const auto shield_settings=link_runtime::shield::MakeObserverConfig((parry_requested||g_rear_observe||g_rear_active)?1:shield_switch,
        GetPrivateProfileIntW(L"LinkRuntime",L"ShieldTraceMaxEvents",500,path),
        GetPrivateProfileIntW(L"LinkRuntime",L"ShieldPoseMaxEvents",200,path),
        GetPrivateProfileIntW(L"LinkRuntime",L"ShieldPoseIntervalMs",200,path));
    if(shield_settings.enabled)ShieldManifest(path);
    if(shield_switch>1)Log("ShieldObserve invalid switch; only exact1 enables observation");
    const bool case_items=ChapterIniSwitch(path,L"CaseHeartItems");
    unsigned trace_limit = link_runtime::TraceLimit(GetPrivateProfileIntW(L"LinkRuntime", L"TraceMaxEvents", 500, path));
    unsigned threshold = GetPrivateProfileIntW(L"LinkRuntime", L"LowHealthThreshold", 360, path);
    unsigned alert_interval = GetPrivateProfileIntW(L"LinkRuntime", L"LowHealthIntervalMs", 4000, path);
    unsigned volume = GetPrivateProfileIntW(L"LinkRuntime", L"LowHealthVolumePercent", 35, path);
    if (!threshold || threshold > 32767) threshold = 360;
    if (alert_interval < 500 || alert_interval > 60000) alert_interval = 4000;
    if (volume > 100) volume = 35;
    UINT interval = GetPrivateProfileIntW(L"LinkRuntime", L"PollMs", 5, path);
    if (interval < 1 || interval > 100) interval = 5;
    const unsigned fps_probe_switch = GetPrivateProfileIntW(L"LinkRuntime", L"FpsProbe", 1, path);
    // Kick camera fly-away at non-dyadic FPS (camera_interp_fix.h). Off unless the INI asks; never
    // hooks when the pins of the native CameraInterpolation::move do not match.
    const int camera_interp_mode = static_cast<int>(GetPrivateProfileIntW(L"LinkRuntime", L"CameraInterpFix", 0, path));
    const bool camera_interp = link_runtime::camera_interp::install::Install(
        reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr)), camera_interp_mode,
        [](const char* fmt, auto... args) { Log(fmt, args...); });
    const bool ztarget_requested=link_runtime::ztarget::runtime::Switch(path,L"LinkRuntime",L"ZTarget");
    const bool ztarget_face_requested=ztarget_requested&&link_runtime::ztarget::runtime::Switch(path,L"ZTarget",L"FaceTarget");
    const bool ztarget_strafe_requested=ztarget_face_requested&&link_runtime::ztarget::runtime::Switch(path,L"ZTarget",L"Strafe")&&
        !link_runtime::ztarget::runtime::Switch(path,L"ZTarget",L"Observe");
    GetModuleFileNameW(nullptr, process_path, MAX_PATH);
    const wchar_t* process_name = std::wcsrchr(process_path, L'\\');
    if (!process_name || _wcsicmp(process_name + 1, L"bio4.exe")) {
        Log("unsupported host; forwarding only, no game memory writes");
        return 0;
    }
    FpsManifest();
    CrashDumpInstall(path);
    Re4fpsGuard();
    StartGiganteTreeGuard(path);
    StartKrauserBowPilot(path);
    StartGiganteGrabOnce(path);
    StartPlagaParasiteOnce(path);
    StartSalazarGrabOnce(path);
    StartSaddlerGrabOnce(path);
    StartGanadoNeckGrabOnce(path);
    StartChainSoundOnce(path);
    StartEm3fAttackTimer(path);
    StartCaseKeyDebounce(path);
    if (!enabled && !low_health && !trace && !shield_settings.enabled &&
        !chapter_settings.enabled && !chapter_settings.trace && !g_oca_cfg.enabled && !g_cmb.AnyOn() && !fairy_requested && !rupees_requested && !case_items && !ztarget_requested && !boss_duck_requested && !sheath_requested && !g_hud_cfg.Any()) { Log("all features off; forwarding only"); return 0; }
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        Log("unsupported PE image; no game memory writes"); return 0;
    }
    uint32_t slot = FindPlayerSlot(base, nt->OptionalHeader.SizeOfImage);
    uint32_t global_slot = FindGlobalSlot(base, nt->OptionalHeader.SizeOfImage);
    g_parry_global_slot=global_slot;g_gen.game_now=global_slot?&ParryGameNow:nullptr;   // before the resolver exists
    // Rupee colours: two call rel32 writes, before the first scene loads; independent of every hook.
    const bool rupees=rupees_requested&&StartRupees(reinterpret_cast<uint32_t>(base));
    if(!rupees_requested)Log("Rupees=0; ItemGetBinTplAddr calls untouched");
    const bool boss_duck=boss_duck_requested&&StartBossMusicDuck(reinterpret_cast<uint32_t>(base),boss_key,combat_key);
    if(!boss_duck_requested)Log("BossMusicDuck=0 CombatMusicDuck=0; SndWatcher untouched");
    const bool sheath=sheath_requested&&StartEventSheath(reinterpret_cast<uint32_t>(base));
    if(!sheath_requested)Log("EventSheathHide=0; Event::ControlTransFlag untouched");
    // Case heart items: rows 2 x 1, models after pieceTblInit, saved records turned at pzlPlayer init.
    const bool case_items_on=case_items&&StartCaseItems(base,nt->OptionalHeader.SizeOfImage);
    if(!case_items)Log("CaseHeartItems=0; case rows and models untouched");
    g_cmb_plan=cmb::MakePlan(g_cmb,trace,g_oca_cfg.enabled);
    if(g_cmb.AnyOn()){
        wchar_t exe_path[MAX_PATH]{};char exe_hash[65]{};
        const DWORD exe_length=GetModuleFileNameW(nullptr,exe_path,MAX_PATH);
        const bool hashed=exe_length&&exe_length<MAX_PATH&&ShieldFileHash(exe_path,exe_hash);
        CombatPrepare(base,slot,global_slot,hashed?exe_hash:nullptr);
        if(g_cmb_prompt_cfg.on&&!PinsAt(reinterpret_cast<uint32_t>(base),link_runtime::native_pins::kMortalDrawPrompt)){
            g_cmb_prompt_cfg.on=false;Log("MortalDrawPrompt disabled: native pins (cActionButton::set) at base 0x%X",reinterpret_cast<uint32_t>(base));
        }
        if(g_cmb.md_contextual||g_cmb.SwordActions()){
            const bool wanted=GetPrivateProfileIntW(L"LinkRuntime",L"MortalDrawFromMove",1,path)==1;
            const bool move_pinned=CmbMoveTablePinned(base,nt->OptionalHeader.SizeOfImage);
            g_cmb_move_start=wanted&&move_pinned;
            Log("MortalDrawFromMove enabled=%u requested=%u move_table_pinned=%u (walk/back/run/turn start; idle-only otherwise)",
                g_cmb_move_start?1U:0U,wanted?1U:0U,move_pinned?1U:0U);
        }
        g_cmb_plan=cmb::MakePlan(g_cmb,trace,g_oca_cfg.enabled);
    } else Log("Combat keys all 0; no combat hooks, files or adapter");
    const bool mortal_native=(g_oca_cfg.enabled||g_cmb.md_contextual||g_cmb.SwordActions()||ztarget_face_requested||g_hud_cfg.Any())&&CaptureMortalNative(base,nt->OptionalHeader.SizeOfImage);
    if(g_oca_cfg.enabled&&!mortal_native)Log("Ocarina disabled: pre-hook native capture");
    bool chapter=false;
    if(chapter_settings.enabled||chapter_settings.trace)chapter=StartChapterHooks(base,nt->OptionalHeader.SizeOfImage,chapter_settings);
    else Log("ChapterProgression=0 TraceChapter=0; no chapter hooks installed");
    if (trace || g_cmb_adapter_on) {
        if (!StartDamageHook(base, nt->OptionalHeader.SizeOfImage, trace_limit, trace, slot, global_slot, g_cmb_adapter_on) && g_cmb_adapter_on) {
            g_cmb_adapter_on = false; Log("Combat adapter disabled: GetWepDmVal detour not installed");
        }
    }
    else Log("TraceKnife=0; no damage hook installed");
    bool shield=false;
    if(shield_settings.enabled)shield=StartShieldHook(base,nt->OptionalHeader.SizeOfImage,shield_settings,slot,global_slot,preclamp_requested,parry_requested,defense_requested);
    else Log("ShieldObserve=0; no shield hooks installed");
    if (!slot) {
        Log("player pattern not unique/valid; sword/health disabled");enabled=false;low_health=false;
        if(!shield&&!(chapter&&chapter_settings.trace))return 0;
    }
    if (enabled) Log("sword enabled: slot=%p poll=%u ms", reinterpret_cast<void*>(slot), interval);
    if (enabled && !global_slot) {
        Log("SwordSheath disabled: global pattern invalid; costume selection unavailable"); enabled = false;
    }
    AlertVoice mortal_audio; mortal::voice::Consumer mortal_consumer;
    wchar_t mortal_directory[MAX_PATH]{};const bool directory_ok=link_runtime::paths::Copy(mortal_directory,path)==0;
    wchar_t* mortal_name=directory_ok?std::wcsrchr(mortal_directory,L'\\'):nullptr;if(mortal_name)mortal_name[1]=0;
    // Published before Observe; inert until StartHud / zlr::Prepare. Null when neither the HUD nor the companion is asked for.
    const bool companion_frame=g_cmb.AnyOn()&&zlr::CompanionRequested(path);
    if(g_hud_cfg.Any()||companion_frame)mortal::dispatch_hooks::player_frame=&PlayerFrame;
    const bool dispatch_up=mortal_native&&mortal_name&&StartMortalTrial(base,nt->OptionalHeader.SizeOfImage,mortal_directory,mortal_audio,g_oca_cfg.enabled,g_cmb.md_contextual,g_cmb.SwordActions(),ztarget_face_requested,ztarget_strafe_requested);
    const bool combat=g_cmb.AnyOn()&&mortal_name&&StartCombat(mortal_directory,dispatch_up);
    StartHud(base,slot,global_slot,dispatch_up,mortal_name?mortal_directory:nullptr);   // after StartCombat: the meter reads ShieldGuard
    if(sword_trail_probe_requested)StartSwordTrailProbe(base,slot,global_slot,dispatch_up,sword_trail_probe_ot22,sword_trail_probe_restore);
    StartSwordTrail(base,slot,global_slot,dispatch_up);
    StartSwordCharge(base,slot,global_slot,dispatch_up,mortal_name?mortal_directory:nullptr);
    StartSwordParticles(base,slot,global_slot,dispatch_up);   // sword-particles-01: after the trail (it reads its samples)
    StartCuccoFeathers(dispatch_up,mortal_name?mortal_directory:nullptr);   // F-11: after the particles (it emits through them)
    if(zlr::g_on&&!g_trail_on.load(std::memory_order_acquire))Log("ZeldaLight inert: sword_trail is off (its reason above); the ray is drawn with the trail's node");
    UserIniStart(user_ini,user_file_problems);   // user-ini-01: after the three: what started, then live reloads
    StartZTargetArrow(base,slot,global_slot,dispatch_up,path);   // before ztarget Start: it mutes the 2D cursor
    if(combat)GuardLoadAudio(mortal_directory);
    link_runtime::ztarget::runtime::stats_logger=&LogV;   // its 5 s stats: LogLevel 2 only
    SwordCameraPrepare();   // the Ending Blow camera lift shares these hooks, admitted on its own (ztarget_runtime.h)
    SwordBackCameraPrepare();   // so does the Back Slice camera (backslice-cam-01), plus its fall hold inside Check
    const bool ztarget=link_runtime::ztarget::runtime::Start(reinterpret_cast<uint32_t>(base),path,[](){
        return g_oca_session.state()!=oca::State::Idle||mortal::dispatch_hooks::runtime.active||SwordBusy()||
            (g_rx&&g_rx->Active());},&Log,dispatch_up);
    ZTargetArrowReady();
    const bool mortal_voice=dispatch_up&&g_cmb_md&&g_cmb.md_contextual;
    const bool ocarina=dispatch_up&&g_oca_on.load(std::memory_order_acquire);
    if(!g_oca_cfg.enabled)Log("Ocarina=0; no ocarina hooks, files or devices");
    OcaWorker ocarina_worker;if(ocarina||link_runtime::ztarget::runtime::PadWanted())ocarina_worker.Start();
    ParryVoices parry_audio;
    link_runtime::rear_shield::Voices<AlertVoice> rear_audio;
    const bool rear_sound=g_rear_active&&mortal_name&&rear_audio.Load(mortal_directory);
    if(g_rear_active)Log("RearShield audio ready=%u; worker_only=1; no sound without applied impact",rear_sound?1U:0U);
    const bool parry_sound=parry_requested&&mortal_name&&parry_audio.Load(mortal_directory);
    Log("ParryAudio ready=%u variants=3 volume=65 external_waveout=1",parry_sound?1U:0U);
    AlertVoice voice;
    if (low_health) {
        const bool alert_path=link_runtime::paths::Copy(name + 1, MAX_PATH - static_cast<size_t>(name + 1 - path), L"link_low_health_1.wav")==0;
        if (!alert_path || !global_slot || !voice.Load(path, volume)) {
            Log("LowHealth disabled: global pattern invalid or PCM16 WAV/audio device unavailable"); low_health = false;
        } else Log("LowHealth enabled: global=%p threshold=%u interval=%u volume=%u shop_flag_fix=%u; health read only",
                   reinterpret_cast<void*>(global_slot), threshold, alert_interval, volume, low_health_shop_fix ? 1U : 0U);
    } else Log("LowHealth=0; no alert audio device opened");
    // Last hook of the process: after shield (ShieldPreClampObserve) and combat.
    const bool fairy_up=fairy_requested&&slot&&global_slot&&StartFairyRevive(base,slot,global_slot);
    if(!fairy_requested)Log("FairyRevive=0; no LifeDownSet2 hook");
    AlertVoice fairy_audio;bool fairy_sound=false;
    if(fairy_up&&fairy_sound_requested){
        fairy_sound=link_runtime::paths::Copy(name + 1, MAX_PATH - static_cast<size_t>(name + 1 - path), L"link_fairy_revive.wav")==0&&fairy_audio.Load(path,fairy_volume);
        Log("FairyRevive sound ready=%u volume=%u",fairy_sound?1U:0U,fairy_volume);
    }
    if (!enabled && !low_health && !shield && !(chapter&&chapter_settings.trace) && !ocarina && !combat && !g_cmb.AnyOn() && !fairy_up && !rupees && !case_items_on && !camera_interp && !ztarget && !boss_duck && !sheath && !g_hud_on) return 0;
    LocalMemory memory;
    unsigned changes = 0;
    link_runtime::AlertClock clock;
    bool previous_eligible = false;
    unsigned previous_costume = 255;
    ShieldPollState shield_poll;
    DWORD parry_poll=0;
    FpsProbe fps_probe;fps_probe.Start(global_slot,fps_probe_switch==1);
    uint64_t camera_interp_next = GetTickCount64() + 5000;
    for (;;) {
        fps_probe.Poll();
        CrashDumpPollAudio();
        if (camera_interp && GetTickCount64() >= camera_interp_next) {
            camera_interp_next = GetTickCount64() + 5000;
            const auto& cs = link_runtime::camera_interp::install::g_stats;
            LogV("CameraInterp moves=%u native_snaps=%u fix_snaps=%u bad_dt=%u last_fix counter=%g dt=%g dist=%g",
                cs.moves.load(), cs.native_snaps.load(), cs.fix_snaps.load(), cs.bad_dt.load(),
                static_cast<double>(cs.last_fix_counter.load()), static_cast<double>(cs.last_fix_dt.load()),
                static_cast<double>(cs.last_fix_dist.load()));
        }
        if(ztarget)link_runtime::ztarget::runtime::Poll();
        PollSwordTrailProbe();
        PollSwordTrail();
        PollSwordCharge();
        PollSwordParticles();   // sword-particles-01
        PollCuccoFeathers();    // F-11: the cry and wing request, its stats
        UserIniPoll();          // user-ini-01: the override file, at most once per second
        PollThreadBindings();   // skills-fix-01: a game thread handed over (one line each)
        PollZTargetArrow();
        if(parry_sound){
            DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
            g_parry_audio.Poll(GetTickCount64(),foreground==GetCurrentProcessId()&&
                (InterlockedCompareExchange(&g_parry_defense_active,0,0)!=0||g_gen.settings.mode==link_runtime::parry::general::Mode::Active),parry_audio);
        }
        if(mortal_voice)PollMortalTrial(mortal_consumer,mortal_audio);
        if(g_cmb.AnyOn()||g_hud_on)PollCombat();
        if(g_hud_on){PollHudRefill();PollHudLetterbox();}   // heart-refill-01 / ztarget-letterbox-01: no-op unless on
        if(g_cmb.SwordActions())SwordPollAudio();
        zlr::PollAudio();
        if(g_sg_on)GuardPollAudio();
        if(ocarina||link_runtime::ztarget::runtime::PadWanted())ocarina_worker.Poll();
        if(fairy_up)PollFairy(fairy_sound?&fairy_audio:nullptr);
        if(rupees)PollRupees();
        if(boss_duck)PollBossMusicDuck();
        if(sheath)PollEventSheath();
        if(case_items_on)PollCaseItems();
        if(chapter&&chapter_settings.trace)PollChapter();
        if(shield)PollShield(shield_poll);
        if(shield&&g_rear_active){
            link_runtime::shield::runtime::RearSound event;link_runtime::shield::runtime::Memory read;
            while(link_runtime::shield::runtime::PopRearSound(event)){
                const bool played=rear_sound&&link_runtime::rear_shield::CurrentSound(read,event,GetTickCount())&&rear_audio.Play(event.material);
                Log("rear_shield sound material=%u submitted=%u",static_cast<unsigned>(event.material),played?1U:0U);
            }
        }
        if(shield&&parry_requested)PollParryObservation(parry_poll);
        if (enabled) {
            unsigned costume = 0;
            if (link_runtime::ReadSwordCostume(memory, slot, global_slot, costume)) {
                if (costume != previous_costume) {
                    Log("sword costume=%u knife=%u back=%u", costume, costume == 4 ? 1U : 2U, costume == 4 ? 4U : 5U);
                    previous_costume = costume;
                }
                // While the ocarina borrows the knife slot (bit 8 on), the back sword must stay shown.
                link_runtime::ocarina::OcarinaAsset mounted;mounted.bin=g_oca_mounted_bin.load(std::memory_order_acquire);mounted.ready=mounted.bin!=0;
                const bool ocarina_in_hand = mounted.ready && link_runtime::ocarina::KnifeSlotHolds(memory, slot, mounted);
                if (!ocarina_in_hand && link_runtime::SwordStep(memory, slot, costume, global_slot) == link_runtime::StepResult::Changed)
                    Log("sword visibility update %u", ++changes);
            }
        }
        if (low_health) {
            link_runtime::HealthState health;
            bool valid = link_runtime::low_health::ReadHealth(memory, slot, global_slot, low_health_shop_fix, health);
            if (!valid) health = {};
            if (health.eligible != previous_eligible) {
                Log("LowHealth gameplay eligible=%u hp=%d/%d", health.eligible ? 1U : 0U, health.hp, health.max_hp);
                previous_eligible = health.eligible;
            }
            if (!health.eligible || health.hp > static_cast<int>(threshold)) voice.Stop();
            if (clock.Due(health, true, static_cast<int>(threshold), alert_interval, GetTickCount64()))
                Log("LowHealth alert hp=%d/%d submitted=%u", health.hp, health.max_hp, voice.Play() ? 1U : 0U);
        }
        Sleep(interval);
    }
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}

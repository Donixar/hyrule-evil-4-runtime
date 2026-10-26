// dll/camera_interp_fix.h
#pragma once
// Kick-camera fly-away at non-dyadic frame rates (re4_fps 144/150/165/180): opt-in fix for the
// native CameraInterpolation::move (bio4.exe 1.1.0, SHA 19aed4af..., RVA 0x2F2AD0).
// Investigation: N:/link-in-re4-work/kick-camera-investigation-20261015/REPORT.md (15-oct-2026).
//
// Native facts (static, from the binary; GC decomp src/game/cam_ctrl.cpp for the semantics):
//  - CamCtrl (0x8702D0) + 0x254 is CameraInterpolation { CAMERA_POINT param (0x20); float counter }.
//  - set(n, p) [0x2F2A60, thunk 0x80D0]: counter = n; if (n > 1) counter = (1 - deltaTime) + n.
//    Called when a motion-attached camera (melee kick, r0 0x11) ends: set(ac->frame, &pG->Camera)
//    at 0x2F4A67, with ac->frame the u8 of the kick motion's camera track.
//  - move(p) [0x2F2AD0, thunk 0xC9B4, only caller CameraControl::Move 0x2F70D5], every logic tick:
//      if (counter == 0.0f) { param = *p; return; }           <- exact float equality
//      r = 1.0f / counter; s = 1.0f - r;                      <- 1/counter, NOT deltaTime/counter
//      param *= s; *p *= r; param += *p;  (Campos, Target, Roll, Fovy; *p is scaled in place)
//      counter -= deltaTime;                                  <- GLOBAL_WK (*0x806F3C) + 0x70
//    At 30/60/120 FPS every counter value is a multiple of deltaTime (1, 0.5, 0.25): the chain
//    reaches 0.0f exactly and snaps. At 30/165 = 2/11 (also 5/24, 1/5, 1/6) the counter skips
//    zero: the last positive value is < deltaTime, so r = 1/counter > 5 (one-tick overshoot), then
//    the counter goes negative, r < 0, s = 1 - r > 1 and |param - p| grows every tick (x12, x4.7,
//    x3.2, ... at 165): the camera leaves the map until something re-sets the interpolation
//    (switchCamera on a battle/calm area change, a new attach camera, a room change).
//    re4_fps 1.5.1 does not touch 0x2F2AD0 (its 635 hook patterns resolve elsewhere).
//
// Fix (hook of the move body; the caller and the thunk stay native):
//  Mode 1 "terminate": same arithmetic as native while counter >= deltaTime; when
//    0 < counter < deltaTime, counter < 0, counter is NaN or param is non-finite: snap (param = *p,
//    counter = 0). At 30/60/120 the last positive counter equals deltaTime, so the native path runs
//    unchanged: bit-identical there (tests/native/test_camera_interp_fix.cpp).
//  Mode 2 "scaled": like mode 1 but r = deltaTime / counter (the rate the 30 FPS design implies);
//    identical to native at 30 FPS only. Reserved for fidelity work; mode 1 is the proposal.
//  INI: [LinkRuntime] CameraInterpFix=0 (missing or 0: off, nothing hooked). Not wired yet.
// Pure part first (no Windows headers); the detour/installer is under
// LINK_RUNTIME_CAMERA_INTERP_INSTALL (needs MinHook, as the other runtime hooks).
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace link_runtime::camera_interp {
namespace rva {
constexpr uint32_t kMoveFn = 0x2F2AD0, kSetFn = 0x2F2A60, kMoveThunk = 0xC9B4, kSetThunk = 0x80D0;
constexpr uint32_t kMoveCallSite = 0x2F70D5;   // CameraControl::Move: call thunk (ecx = &CamCtrl.m_Inter)
constexpr uint32_t kAttachEndSet = 0x2F4A67;   // checkAttachCamera: set(inter_frame, &pG->Camera.param)
constexpr uint32_t kCamCtrl = 0x8702D0, kGlobalSlot = 0x806F3C, kAttachInterFrame = 0x870E08;
}
namespace off {
constexpr uint32_t kInter = 0x254, kCounter = 0x20, kDeltaTime = 0x70, kR0 = 0x34, kSystemFlag = 0x2C;
}

struct CameraPoint { float campos[3]; float target[3]; float roll; float fovy; };  // CAMERA_POINT
static_assert(sizeof(CameraPoint) == 0x20, "CAMERA_POINT is 0x20 bytes");
struct Interpolation { CameraPoint param; float counter; };                     // CameraInterpolation
static_assert(sizeof(Interpolation) == 0x24, "CameraInterpolation is 0x24 bytes");

enum class Mode : int { Off = 0, Terminate = 1, Scaled = 2 };
enum class Outcome { NativeSnap, Interpolated, FixSnap };

constexpr float kDefaultDt = 0.5f, kMinDt = 0.03f, kMaxDt = 2.0f;
inline float ValidDt(float raw) { return std::isfinite(raw) && raw >= kMinDt && raw <= kMaxDt ? raw : kDefaultDt; }
inline bool Finite(const CameraPoint& c) {
    for (int i = 0; i < 3; ++i) if (!std::isfinite(c.campos[i]) || !std::isfinite(c.target[i])) return false;
    return std::isfinite(c.roll) && std::isfinite(c.fovy);
}

namespace detail {
inline void Scale(float* v, float f) { v[0] = v[0] * f; v[1] = v[1] * f; v[2] = v[2] * f; }
inline void Add(float* d, const float* a) { d[0] = d[0] + a[0]; d[1] = d[1] + a[1]; d[2] = d[2] + a[2]; }
// The native body for a given ratio r (float ops in the native order; every intermediate is a float).
inline void Blend(Interpolation& it, CameraPoint& p, float r, float dt) {
    const float s = 1.0f - r;
    Scale(it.param.campos, s); Scale(it.param.target, s);
    it.param.roll = it.param.roll * s; it.param.fovy = it.param.fovy * s;
    Scale(p.campos, r); Scale(p.target, r);
    p.roll = p.roll * r; p.fovy = p.fovy * r;
    Add(it.param.campos, p.campos); Add(it.param.target, p.target);
    it.param.roll = it.param.roll + p.roll; it.param.fovy = it.param.fovy + p.fovy;
    it.counter = it.counter - dt;
}
inline void Snap(Interpolation& it, const CameraPoint& p) { it.counter = 0.0f; it.param = p; }
}

// Exact emulation of the native move (reference for the tests and Mode::Off).
inline Outcome Reference(Interpolation& it, CameraPoint& p, float dt) {
    if (it.counter == 0.0f) { detail::Snap(it, p); return Outcome::NativeSnap; }
    detail::Blend(it, p, 1.0f / it.counter, dt);
    return Outcome::Interpolated;
}

// The fixed move. `dt` must already be ValidDt()-checked by the caller.
inline Outcome Step(Interpolation& it, CameraPoint& p, float dt, Mode mode) {
    if (mode == Mode::Off) return Reference(it, p, dt);
    if (it.counter == 0.0f) { detail::Snap(it, p); return Outcome::NativeSnap; }
    // counter < 0, NaN, 0 < counter < dt (would overshoot then diverge) or a poisoned param: snap.
    if (!(it.counter >= dt) || !Finite(it.param)) { detail::Snap(it, p); return Outcome::FixSnap; }
    const float r = mode == Mode::Scaled ? dt / it.counter : 1.0f / it.counter;
    detail::Blend(it, p, r, dt);
    return Outcome::Interpolated;
}

// Ticks a native-fresh interpolation of `n` frames would run before the native snap (exact dt only).
inline float SetCounter(int n, float dt) { return n > 1 ? (1.0f - dt) + static_cast<float>(n) : static_cast<float>(n); }

// Pins: exact bytes at the hooked body, its thunk, the only call site and the set body. Absolute
// operands (mov eax,[0xC06F3C]; and dword [0xC70278],..) are left out of the chunks so the pins hold
// at any image base without relocation handling. None of these ranges carries a re4_fps pattern.
struct Pin { uint32_t rva; const char* hex; };
inline constexpr Pin kPins[] = {
    {0x2F2AD0, "558becd9ee83ec0856578bf9d85720dfe0f6c4050f8ac7000000ddd853d9472051d9e8d9c0def2d9c9d95dfcd865fcd95df8d945f8d91c245757e8916cd1ffd945f883c4088d5f0cd91c245353e87e6cd1ffd94718d945f88b7508d9c083c408decad9c9d95f18d84f1cd95f1cd945fcd91c245656e8566cd1ffd945fc83c4088d460cd91c245050e8436cd1ffd94618d945fc57d9c057deca56d9c9d95e18d84e1cd95e1ce8e953d1ff538d460c5350e8de53d1ffd94618d8471883c4245bd95f18d9461cd8471cd95f1c"},
    {0x2F2BA0, "d94720d86070d95f205f5e8be55dc204008b7508d95f20b908000000f3a55f5e8be55dc20400"},
    {0x2F2A60, "558bec51837d0801db45085657d95dfc8bf9d945fcd957207e23"},
    {0x2F2A7F, "d940708b750cd9e8b908000000dee1dec1d95f20f3a55f5e8be55dc208008b750cddd8b908000000f3a55f5e8be55dc20800"},
    {0xC9B4, "e917612e00"},
    {0x80D0, "e98ba92e00"},
    {0x2F70C9, "8d4b388db354020000518bcee8da58d1ffd9eed89b74020000dfe0f6c4057a07"}};
inline int HexNibble(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
// `read(rva, out, len)` returns false when the bytes cannot be read.
template <class Read> bool PinOk(const Pin& pin, Read read) {
    const size_t len = std::strlen(pin.hex) / 2;
    uint8_t got[256];
    if (len > sizeof(got) || !read(pin.rva, got, len)) return false;
    for (size_t i = 0; i < len; ++i) {
        const int hi = HexNibble(pin.hex[2 * i]), lo = HexNibble(pin.hex[2 * i + 1]);
        if (hi < 0 || lo < 0 || got[i] != static_cast<uint8_t>(hi * 16 + lo)) return false;
    }
    return true;
}
template <class Read> bool PinsOk(Read read) {
    for (const Pin& p : kPins) if (!PinOk(p, read)) return false;
    return true;
}

// Session counters for the worker log (read-only there): moves seen, native snaps, fix snaps, bad dt.
struct Stats {
    std::atomic<uint32_t> moves{0}, native_snaps{0}, fix_snaps{0}, bad_dt{0};
    std::atomic<float> last_fix_counter{0.0f}, last_fix_dt{0.0f}, last_fix_dist{0.0f};
};
inline float Distance(const CameraPoint& a, const CameraPoint& b) {
    float s = 0.0f;
    for (int i = 0; i < 3; ++i) { const float d = a.campos[i] - b.campos[i]; s += d * d; }
    return std::sqrt(s);
}
// The complete per-tick policy with bookkeeping; the detour calls this.
inline Outcome Tick(Interpolation& it, CameraPoint& p, float raw_dt, Mode mode, Stats& st) {
    const float dt = ValidDt(raw_dt);
    if (dt != raw_dt) st.bad_dt.fetch_add(1, std::memory_order_relaxed);
    const CameraPoint before = it.param;
    const float counter = it.counter;
    const Outcome o = Step(it, p, dt, mode);
    st.moves.fetch_add(1, std::memory_order_relaxed);
    if (o == Outcome::NativeSnap) st.native_snaps.fetch_add(1, std::memory_order_relaxed);
    if (o == Outcome::FixSnap) {
        st.fix_snaps.fetch_add(1, std::memory_order_relaxed);
        st.last_fix_counter.store(counter, std::memory_order_relaxed);
        st.last_fix_dt.store(dt, std::memory_order_relaxed);
        st.last_fix_dist.store(Finite(before) ? Distance(before, it.param) : -1.0f, std::memory_order_relaxed);
    }
    return o;
}
}  // namespace link_runtime::camera_interp

#if defined(LINK_RUNTIME_CAMERA_INTERP_INSTALL)
// Installer (DLL only). Hooks the move body with MinHook; the native function is never called back
// (the detour reproduces it). Rollback: MH_DisableHook/MH_RemoveHook on the same target.
#include <windows.h>
#include "MinHook.h"
namespace link_runtime::camera_interp::install {
inline Stats g_stats;
inline Mode g_mode = Mode::Off;
inline const uint8_t* g_base = nullptr;
inline void* g_target = nullptr;
inline void* g_original = nullptr;  // MinHook trampoline, kept only for a clean remove
inline float ReadDeltaTime() {
    const uint8_t* pG = *reinterpret_cast<uint8_t* const*>(g_base + rva::kGlobalSlot);
    if (!pG) return kDefaultDt;
    float dt = kDefaultDt;
    std::memcpy(&dt, pG + off::kDeltaTime, sizeof(dt));
    return dt;
}
// __thiscall(this = ecx, p on the stack, callee cleans 4 bytes) == __fastcall with an unused edx.
inline void __fastcall MoveDetour(Interpolation* self, void* /*edx*/, CameraPoint* p) {
    Tick(*self, *p, ReadDeltaTime(), g_mode, g_stats);
}
inline bool ReadImage(uint32_t rva, uint8_t* out, size_t len) {
    __try { std::memcpy(out, g_base + rva, len); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// `log` takes a printf-style message. Returns true when the hook is live.
template <class Log> bool Install(const uint8_t* image_base, int ini_mode, Log log) {
    if (ini_mode != 1 && ini_mode != 2) { if (ini_mode != 0) log("CameraInterpFix ignored: mode %d unknown", ini_mode); return false; }
    g_base = image_base;
    if (!PinsOk(ReadImage)) { log("CameraInterpFix disabled: native CameraInterpolation pins failed"); return false; }
    g_mode = static_cast<Mode>(ini_mode);
    MH_STATUS status = MH_Initialize();
    const bool own = status == MH_OK;
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) { log("CameraInterpFix disabled: MinHook init %s", MH_StatusToString(status)); return false; }
    g_target = const_cast<uint8_t*>(image_base) + rva::kMoveFn;
    status = MH_CreateHook(g_target, reinterpret_cast<void*>(&MoveDetour), &g_original);
    if (status != MH_OK) { log("CameraInterpFix disabled: MinHook create %s", MH_StatusToString(status)); if (own) MH_Uninitialize(); return false; }
    status = MH_EnableHook(g_target);
    if (status != MH_OK) { log("CameraInterpFix disabled: MinHook enable %s", MH_StatusToString(status)); MH_RemoveHook(g_target); if (own) MH_Uninitialize(); return false; }
    log("CameraInterpFix enabled mode=%d target=0x%X (CameraInterpolation::move)", ini_mode, rva::kMoveFn);
    return true;
}
inline void Remove() {
    if (!g_target) return;
    MH_DisableHook(g_target); MH_RemoveHook(g_target); g_target = nullptr; g_mode = Mode::Off;
}
}  // namespace link_runtime::camera_interp::install
#endif

// dll/re4fps_guard.h
#pragma once
// re4_fps 1.5.1 (re4_tweaks v1.9.1.0-9b165e04-re4fps, Bin32/winmm.dll SHA 6fa69c56..., SizeOfImage 0xBA9000)
// null calls in its Del Lago / harpoon frame-rate fix (docs/re4fps-slots-2026-10-17.md).
//
// winmm +0x31D2B0 (FrameRateFixes, fixed RVAs for bio4 1.1.0) redirects two VecAdd calls (thunk 0x7F63)
//   bio4 +0x3269A4 (em2F routine)  -> winmm +0x3269D0
//   bio4 +0x4ED26D                  -> winmm +0x326A80
// Both stubs scale the velocity by the frame delta and then add it:
//   mov esi/edi,[winmm+0xB6135C] ... call esi/edi      (Vec scale)
//   call [winmm+0xB6134C]                               (VecAdd)
// Those two slots are written only by winmm +0x38A310, the EnableFastMath block, which is skipped when
// EnableFastMath=false (our winmm.ini). So both stay 0 and the game jumps to EIP 0:
//   crashes 16-oct 22:31 / 17-oct 04:18 at the first call (ret winmm+0x326A3F), slot B6135C;
//   crash 17-oct 14:32 after the 04:34 fix filled only B6135C: the second call, slot B6134C.
// What the EnableFastMath block would have written (patterns run against bio4.exe 1.1.0, SHA 19aed4af...):
//   B6135C: "83 C4 08 8D 55 ? D9 1C ? 52 8B C2 50 E8" +0xD, 13 matches, all E8 -> 0x97A0 -> 0x5761D0 Vec scale
//   B6134C: "A1 ? ? ? ? 8D 4D ? 51 8B D1 52 05 94 00 00 00 50 E8" +0x12, 2 matches, both E8 -> 0x7F63 ->
//           0x575E50 VecAdd (the very function the redirected sites called).
// Repair: once re4_fps has initialised, write bio4 + thunk into each slot that still holds 0. Every byte the
// decision relies on is checked first (image size, each load/call site in both stubs addressing exactly that
// slot, the bio4 thunk and the target prologue). All or nothing: if any slot fails its identity checks no
// slot is written. Pure decision here (testable); the live read/write is in link_runtime.cpp.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace link_runtime::re4fps_guard {
constexpr uint32_t kWinmmImageSize = 0xBA9000, kWinmmPreferredBase = 0x10000000;
// Shared prologue of Vec scale and VecAdd (push ebp; mov ebp,esp; sub esp,10h; mov eax,[cookie]); the cookie
// address is absolute, so only the opcode bytes are compared.
constexpr uint8_t kHead[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0xA1};

// One winmm code site that reads the slot. len 6: two opcode bytes + the absolute slot address;
// len 2: a register call (no address).
struct Site { uint32_t rva; uint8_t len; uint8_t op[2]; };
constexpr size_t kMaxSites = 4;
struct Spec {
    const char* name;
    uint32_t slot;              // winmm RVA of the function pointer
    Site sites[kMaxSites];
    uint32_t thunk, func;       // bio4 RVAs: E9 rel32 -> func
};
constexpr size_t kSlots = 2;
// Order = write order: VecAdd first, so a stub never passes the first call and dies at the second.
constexpr Spec kSpecs[kSlots] = {
    {"vec_add", 0xB6134C,
     {{0x326A49, 6, {0xFF, 0x15}}, {0x326AA9, 6, {0xFF, 0x15}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}},
     0x7F63, 0x575E50},
    {"vec_scale", 0xB6135C,
     {{0x326A22, 6, {0x8B, 0x35}}, {0x326A3D, 2, {0xFF, 0xD6}}, {0x326A85, 6, {0x8B, 0x3D}}, {0x326AA0, 2, {0xFF, 0xD7}}},
     0x97A0, 0x5761D0},
};
// Kept for the older log line and tests.
constexpr uint32_t kSlot = 0xB6135C, kThunk = 0x97A0, kScale = 0x5761D0;

enum class Verdict { Repair, AlreadySet, WrongWinmm, WrongBio4 };
inline const char* Name(Verdict v) {
    switch (v) {
    case Verdict::Repair: return "repair"; case Verdict::AlreadySet: return "already_set";
    case Verdict::WrongWinmm: return "wrong_winmm"; default: return "wrong_bio4";
    }
}
// Bytes read live for one slot.
struct SlotFacts {
    uint8_t site[kMaxSites][6]{};
    uint32_t slot = 0;          // current value of winmm + spec.slot
    uint8_t thunk[5]{};         // bio4 + spec.thunk
    uint8_t head[sizeof(kHead)]{};  // bio4 + spec.func
};
struct Facts {
    uint32_t winmm_base = 0, winmm_size = 0, bio4_base = 0;
    SlotFacts s[kSlots];
};
inline uint32_t U32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint32_t Target(const Facts& f, size_t i) { return f.bio4_base + kSpecs[i].thunk; }
inline Verdict DecideSlot(const Facts& f, size_t i) {
    const Spec& sp = kSpecs[i]; const SlotFacts& s = f.s[i];
    if (f.winmm_size != kWinmmImageSize) return Verdict::WrongWinmm;
    for (size_t k = 0; k < kMaxSites && sp.sites[k].len; ++k) {
        const Site& site = sp.sites[k];
        if (s.site[k][0] != site.op[0] || s.site[k][1] != site.op[1]) return Verdict::WrongWinmm;
        if (site.len == 6 && U32(s.site[k] + 2) != f.winmm_base + sp.slot) return Verdict::WrongWinmm;
    }
    if (s.thunk[0] != 0xE9 || sp.thunk + 5 + U32(s.thunk + 1) != sp.func ||
        std::memcmp(s.head, kHead, sizeof(kHead)) != 0)
        return Verdict::WrongBio4;
    return s.slot ? Verdict::AlreadySet : Verdict::Repair;
}
// All or nothing: true only when no slot fails its identity checks.
inline bool AllKnown(const Facts& f) {
    for (size_t i = 0; i < kSlots; ++i) {
        const Verdict v = DecideSlot(f, i);
        if (v == Verdict::WrongWinmm || v == Verdict::WrongBio4) return false;
    }
    return true;
}
}  // namespace link_runtime::re4fps_guard

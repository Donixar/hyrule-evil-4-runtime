#pragma once
// Foreign-patch acceptance (docs/fps-coexistence-2026-10-12.md 2a/2b/2d, §6). re4_tweaks (re4_fps
// 1.5.1, loaded as winmm.dll) patches bio4.exe in its DllMain, before our worker qualifies.
// Our finders accept exactly the known forms, and only when the jump/call target lies in an
// executable section of another LOADED module (never anonymous memory, never bio4.exe or us).
// Pure: the runtime feeds the module's mapped headers; the Win32 lookup lives in link_runtime.cpp.
#include <cstddef>
#include <cstdint>

namespace link_runtime::foreign {
// Injected by the runtime: true only when `target` (absolute) lies in an executable section of
// another loaded module (never anonymous memory). Pure fixtures pass a synthetic probe.
struct Probe { bool (*accept)(uint32_t target, void* context) = nullptr; void* context = nullptr; };
inline bool Accept(const Probe* p, uint32_t target) { return p && p->accept && p->accept(target, p->context); }
inline uint32_t U32(const unsigned char* p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
inline unsigned U16(const unsigned char* p) { return unsigned(p[0])|(unsigned(p[1])<<8); }
// `headers`: the first `n` bytes of a mapped x86 PE module. True when `rva` falls inside a
// section flagged IMAGE_SCN_MEM_EXECUTE (and inside SizeOfImage).
inline bool CodeRva(const unsigned char* headers, size_t n, uint32_t rva) {
    if (!headers || n < 64 || U16(headers) != 0x5A4D) return false;
    const size_t pe = U32(headers + 0x3C);
    if (pe < 64 || pe > n || n - pe < 24 || U32(headers + pe) != 0x4550 || U16(headers + pe + 4) != 0x14C) return false;
    const unsigned count = U16(headers + pe + 6), optional = U16(headers + pe + 20);
    const size_t opt = pe + 24;
    if (!count || count >= 96 || optional < 96 || optional > n - opt || U16(headers + opt) != 0x10B) return false;
    const size_t first = opt + optional;
    if (n - first < size_t(count) * 40) return false;
    const uint32_t image_size = U32(headers + opt + 56);
    if (rva >= image_size) return false;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char* s = headers + first + size_t(i) * 40;
        const uint32_t va = U32(s + 12), span = U32(s + 8) ? U32(s + 8) : U32(s + 16), flags = U32(s + 36);
        if ((flags & 0x20000000U) && rva >= va && rva - va < span) return true;
    }
    return false;
}
// The re4_tweaks MakeInline form (injector::MakeInline<T>(at, end)): the range is filled with NOPs
// and `at` gets E8 rel32, a CALL into a reg_pack wrapper that returns to at+5 (the wrapper in
// re4_fps 1.5.1 is `pushfd; pushad; add [esp+0Ch],8; push esp; call; ...; popad; popfd; ret`).
// It is never E9: the 12-oct in-game log proved the earlier E9 assumption wrong. `nops` is the
// exact NOP fill after the call (end - at - 5). `target` is the absolute call target; true only
// when it falls outside the image (unsigned wrap covers below-base).
inline bool InlineCall(const unsigned char* at, unsigned nops, uint32_t address, uint32_t image_base, uint32_t image_size, uint32_t& target) {
    target = 0;
    if (at[0] != 0xE8) return false;
    for (unsigned i = 0; i < nops; ++i) if (at[5 + i] != 0x90) return false;
    target = address + 5 + U32(at + 1);
    return target - image_base >= image_size;
}
}  // namespace link_runtime::foreign

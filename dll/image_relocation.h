// dll/image_relocation.h
#pragma once
// Reads native bytes as they are in the pinned file at its preferred base, whatever base the
// loader chose. bio4.exe has DYNAMIC_BASE (DllCharacteristics 0x8140, relocations kept), so
// Windows may map it elsewhere (seen: 0x7E0000) and every HIGHLOW operand in a pinned window
// then holds preferred+delta. ReadPreferred copies [base+rva, base+rva+n) and subtracts delta
// from exactly the dwords the module's own base-relocation directory (still mapped) lists,
// including a fixup that starts before the window or ends after it. Pure: an injected accessor
// `bool Read(uint32_t address, void* out, size_t n)`; no process access here.
// Fails closed: unreadable headers or directory, a malformed block, a fixup type other than
// ABSOLUTE/HIGHLOW touching the window, or a fixup outside the image. delta 0: plain copy.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace link_runtime::image_reloc {
// The pinned bio4.exe (SHA 19aed4af...) file ImageBase. The loader rewrites ImageBase in the
// mapped header to the actual base, so it is never read back from memory.
constexpr uint32_t kPreferredBase = 0x400000;
constexpr size_t kMaxWindow = 4096;

namespace detail {
inline uint32_t U32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
inline unsigned U16(const unsigned char* p) { return unsigned(p[0]) | unsigned(p[1]) << 8; }
struct Directory { uint32_t image_size = 0, rva = 0, size = 0; };
// PE32 i386 header of the mapped module at `base`: SizeOfImage and the reloc data directory.
template <class Mem> bool ReadDirectory(Mem& m, uint32_t base, Directory& out) {
    unsigned char dos[64]{};
    if (!m.Read(base, dos, sizeof(dos)) || U16(dos) != 0x5A4D) return false;
    const uint32_t pe = U32(dos + 0x3C);
    if (pe < 64 || pe > 0x1000) return false;
    unsigned char h[24 + 144]{};   // signature, file header, optional header through data directory 5
    if (!m.Read(base + pe, h, sizeof(h))) return false;
    const unsigned char* opt = h + 24;
    if (U32(h) != 0x4550 || U16(h + 4) != 0x14C || U16(h + 20) < 144 || U16(opt) != 0x10B || U32(opt + 92) < 6) return false;
    out.image_size = U32(opt + 56); out.rva = U32(opt + 136); out.size = U32(opt + 140);
    if (!out.image_size || uint64_t(base) + out.image_size > 0xFFFFFFFFULL) return false;
    return out.rva && out.size >= 8 && out.rva < out.image_size && out.size <= out.image_size - out.rva;
}
}  // namespace detail

template <class Mem> bool ReadPreferred(Mem& m, uint32_t base, uint32_t rva, unsigned char* out, size_t n) {
    using namespace detail;
    if (!out || !n || n > kMaxWindow || uint64_t(base) + rva + n > 0xFFFFFFFFULL) return false;
    if (!m.Read(base + rva, out, n)) return false;
    const uint32_t delta = base - kPreferredBase;
    if (!delta) return true;
    Directory d;
    if (!ReadDirectory(m, base, d) || rva >= d.image_size || n > d.image_size - rva) return false;
    const uint64_t lo = rva, hi = uint64_t(rva) + n;   // window [lo, hi)
    for (uint32_t cursor = 0; cursor < d.size;) {
        if (d.size - cursor < 8) return false;
        unsigned char head[8]{};
        if (!m.Read(base + d.rva + cursor, head, 8)) return false;
        const uint32_t page = U32(head), block = U32(head + 4);
        if (block < 8 || (block & 1) || block > d.size - cursor || page >= d.image_size) return false;
        // Entries of this block land in [page, page+0xFFF]; a dword there covers up to page+0x1002.
        if (uint64_t(page) + 0x1003 > lo && page < hi) {
            for (uint32_t at = 8; at < block; at += 2) {
                unsigned char e[2]{};
                if (!m.Read(base + d.rva + cursor + at, e, 2)) return false;
                const unsigned entry = U16(e), type = entry >> 12;
                const uint32_t target = page + (entry & 0xFFF);
                if (!type) continue;                                   // IMAGE_REL_BASED_ABSOLUTE padding
                if (uint64_t(target) + 4 <= lo || target >= hi) continue;
                if (type != 3 || uint64_t(target) + 4 > d.image_size) return false;   // only HIGHLOW
                unsigned char v[4]{};
                if (!m.Read(base + target, v, 4)) return false;
                const uint32_t preferred = U32(v) - delta;
                for (unsigned k = 0; k < 4; ++k) {
                    const uint64_t at_byte = uint64_t(target) + k;
                    if (at_byte >= lo && at_byte < hi) out[at_byte - lo] = static_cast<unsigned char>(preferred >> (8 * k));
                }
            }
        }
        cursor += block;
    }
    return true;
}
}  // namespace link_runtime::image_reloc

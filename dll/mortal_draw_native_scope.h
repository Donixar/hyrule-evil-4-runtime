#pragma once
#include <cstdint>
#include <cstring>

// Isolated native-call primitive. This header installs no hook and owns no
// engine object. The caller must hold an exclusive, synchronous live-object
// lifetime across the complete call. Runtime action/lifetime integration is
// intentionally absent until the engine barrier is attributed.
namespace link_runtime::mortal_draw::native_scope {
using Core = void(__cdecl*)(void*, void*, void*);
using ModelSet = void(__thiscall*)(void*, void*, uint32_t, uint32_t, uint32_t, void*);
constexpr uintptr_t model_set_rva = 0x25CF80;
constexpr uintptr_t core_rva = 0x261D60;

template<class T> inline T Read(const unsigned char* p) {
    T value{};
    std::memcpy(&value, p, sizeof(value));
    return value;
}
template<class T> inline void Write(unsigned char* p, T value) {
    std::memcpy(p, &value, sizeof(value));
}

// Every nonmatching call is forwarded once with its original arguments and
// fields. The matching case modifies only the primary consumer's three mode
// bits; cancellation after this function returns leaves no mode lease behind.
inline bool Invoke(Core original, void* model, void* work, void* camera,
                   void* owned_model, void* owned_fcv, void* owned_seq_entries) {
    auto* p = static_cast<unsigned char*>(model);
    if (!original) return false;
    if (!model || model != owned_model || !owned_fcv || !owned_seq_entries ||
        work != p + 0x1D8 || Read<void*>(p + 0x1D8) != owned_fcv ||
        Read<void*>(p + 0x280) != owned_seq_entries) {
        original(model, work, camera);
        return false;
    }
    const uint16_t attrs = Read<uint16_t>(p + 0x218);
    const uint32_t flags = Read<uint32_t>(p + 0x21C);
    __try {
        Write<uint16_t>(p + 0x218, static_cast<uint16_t>((attrs & ~0xC080u) | 0x4080u));
        Write<uint32_t>(p + 0x21C, flags | 0x400000u);
        original(model, work, camera);
    } __finally {
        // Preserve unrelated state written by the native call.
        Write<uint16_t>(p + 0x218, static_cast<uint16_t>((Read<uint16_t>(p + 0x218) & ~0xC080u) | (attrs & 0xC080u)));
        Write<uint32_t>(p + 0x21C, (Read<uint32_t>(p + 0x21C) & ~0x400000u) | (flags & 0x400000u));
    }
    return true;
}
}

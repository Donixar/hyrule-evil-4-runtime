// dll/crash_dump.h
#pragma once
// Diagnostic crash dump ([LinkRuntime] CrashDump=1, default on). WER LocalDumps needs admin, so the DLL
// writes its own minidump for the crashes we are hunting: a vectored handler (registered last) that, for an
// access violation at a null-call address (< 0x10000) or inside winmm.dll / bio4.exe / XAudio2_7.dll code, writes ONE
// Bin32/link_crash_<tick>.dmp and logs registers and esp[0], then lets the exception continue unchanged.
// Pure decision here (testable); the handler is in link_runtime.cpp.
#include <cstdint>

namespace link_runtime::crash_dump {
constexpr uint32_t kAccessViolation = 0xC0000005u;
constexpr uint32_t kNullCallLimit = 0x10000;
struct Range { uint32_t base = 0, size = 0; };
inline bool Inside(const Range& r, uint32_t a) { return r.size && a >= r.base && a - r.base < r.size; }
enum class Kind { None, NullCall, Winmm, Bio4, Xaudio };
inline const char* Name(Kind k) {
    switch (k) {
    case Kind::NullCall: return "null_call"; case Kind::Winmm: return "winmm";
    case Kind::Bio4: return "bio4"; case Kind::Xaudio: return "xaudio"; default: return "none";
    }
}
// winmm / bio4 ranges are their code sections only (data faults inside the image are not matched by EIP
// anyway, but this keeps the rule exact). XAudio2_7.dll (28-oct: bio4 crashed at XAudio2_7+0x3DA72 ~14 s after a
// load, with no dump) is loaded by the game after the DLL starts; its range stays empty until the worker finds it.
inline Kind Classify(uint32_t code, uint32_t address, const Range& winmm_code, const Range& bio4_code,
                     const Range& xaudio_code = Range{}) {
    if (code != kAccessViolation) return Kind::None;
    if (address < kNullCallLimit) return Kind::NullCall;
    if (Inside(winmm_code, address)) return Kind::Winmm;
    if (Inside(bio4_code, address)) return Kind::Bio4;
    if (Inside(xaudio_code, address)) return Kind::Xaudio;
    return Kind::None;
}
}  // namespace link_runtime::crash_dump

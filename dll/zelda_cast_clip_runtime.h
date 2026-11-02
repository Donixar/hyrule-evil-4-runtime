#pragma once
// Zelda's cast clip, the process side (zelda_cast_clip.h has the logic). Game thread only. One Layer per model that
// plays the clip (the player Ashley, the partner Zelda): our own MOTION_INFO work, initialised by the native secondary
// init 0x2611A0 exactly as the parry reaction does for Link (link_runtime.cpp CmbReactionPort::Install: work+44 =
// 0x10000000 during init, init(model, work, FCV, SEQ, hokan 0, attr 0, 0.0, keep 0), speed C0 = 1, weight C8 = 0), then
// written into model+0x2A8. Each tick sets the weight; a freed slot (or the player's neck takeover) is taken back with
// our same work, a foreign work wins. Release gives the slot back to what was there at install, by compare-exchange.
#include "zelda_cast_clip.h"
#include "native_pins.h"
#include <windows.h>
#include <cstring>

namespace link_runtime::zelda_cast_clip::runtime {
// The native secondary init (native_pins.h kCombat, the same bytes).
inline constexpr native_pins::Pin kPins[] = {{0x2611A0, "558bec83ec5ca1105ec30033c58945fc8b450853568b750c33c933d2f7464400"}};
constexpr uint32_t kInitRva = 0x2611A0;
constexpr unsigned kMaxRetakes = 8;
struct Memory {
    bool Read(uint32_t a, void* p, size_t n) {
        if (a < 0x10000 || !p || !n || uint64_t(a) + n >= 0x7FFF0000) return false;
        __try { std::memcpy(p, reinterpret_cast<const void*>(a), n); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Write(uint32_t a, const void* p, size_t n) {
        if (a < 0x10000 || !p || !n || uint64_t(a) + n >= 0x7FFF0000) return false;
        __try { std::memcpy(reinterpret_cast<void*>(a), p, n); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
};
inline uint32_t base = 0;
inline unsigned char* fcv = nullptr;
inline unsigned char* seq = nullptr;
inline bool Ready() { return base && fcv && seq; }
// Startup: the pin and the resident clip (SHA-checked by the caller's loader). False leaves the clip off.
inline bool Prepare(uint32_t image_base, unsigned char* clip_fcv, unsigned char* clip_seq) {
    Memory m;
    if (!image_base || !clip_fcv || !clip_seq || !native_pins::All(m, image_base, kPins)) return false;
    base = image_base; fcv = clip_fcv; seq = clip_seq;
    return true;
}

struct Layer {
    alignas(16) unsigned char work[work::size]{};
    uint32_t model = 0, previous = 0, neck = 0;
    bool on = false;
    unsigned retakes = 0;
    uint32_t Work() const { return reinterpret_cast<uint32_t>(work); }
};
using Init = void(__cdecl*)(void*, void*, void*, void*, int, int, float, int);
inline bool CallInit(uint32_t model, Layer& l) {
    __try { reinterpret_cast<Init>(base + kInitRva)(reinterpret_cast<void*>(model), l.work, fcv, seq, 0, 0, 0.f, 0); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// neck: the model's own neck work (player + 0x474), 0 for the partner.
inline bool Install(Layer& l, uint32_t model, uint32_t neck) {
    Memory m; uint32_t current = 0;
    l.on = false;
    if (!Ready() || !m.Read(model + kSecondary, &current, 4) || !Installable(current, neck)) return false;
    std::memset(l.work, 0, sizeof l.work);
    uint32_t flags = 0x10000000U; std::memcpy(l.work + work::flags, &flags, 4);
    if (!CallInit(model, l)) { m.Write(model + kSecondary, &current, 4); return false; }   // init may have cleared it
    std::memcpy(&flags, l.work + work::flags, 4); flags &= ~0x10000000U; std::memcpy(l.work + work::flags, &flags, 4);
    const float one = 1.f, zero = 0.f;
    std::memcpy(l.work + work::speed, &one, 4); std::memcpy(l.work + work::weight, &zero, 4);
    const uint32_t ours = l.Work();
    if (!m.Write(model + kSecondary, &ours, 4)) { m.Write(model + kSecondary, &current, 4); return false; }
    l.model = model; l.previous = current; l.neck = neck; l.on = true; l.retakes = 0;
    return true;
}
// One tick before the model's MotionMove: the weight for this age. Returns false once the layer is gone (yielded to a
// native work or never installed).
inline bool Tick(Layer& l, double age_ms) {
    if (!l.on) return false;
    Memory m; uint32_t current = 0;
    if (!m.Read(l.model + kSecondary, &current, 4)) { l.on = false; return false; }
    const uint32_t ours = l.Work();
    switch (Decide(current, ours, l.neck)) {
    case Slot::Ours: break;
    case Slot::Retake:
        if (++l.retakes > kMaxRetakes || InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(l.model + kSecondary),
                                                                   LONG(ours), LONG(current)) != LONG(current)) {
            l.on = false; return false;
        }
        break;
    default: l.on = false; return false;
    }
    const float w = LayerWeight(age_ms);
    std::memcpy(l.work + work::weight, &w, 4);
    return true;
}
// Where the clip is (SEQ entries), while the slot is ours.
inline bool SeqFrame(const Layer& l, double& out) {
    if (!l.on) return false;
    Memory m; uint32_t current = 0; float f = 0;
    if (!m.Read(l.model + kSecondary, &current, 4) || current != l.Work()) return false;
    std::memcpy(&f, l.work + work::seq_frame, 4);
    if (!std::isfinite(f)) return false;
    out = f; return true;
}
// The slot back to what was there at install (only if it is still ours).
inline void Release(Layer& l) {
    if (!l.on) return;
    l.on = false;
    const float zero = 0.f; std::memcpy(l.work + work::weight, &zero, 4);
    __try {
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(l.model + kSecondary), LONG(l.previous), LONG(l.Work()));
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
}  // namespace link_runtime::zelda_cast_clip::runtime

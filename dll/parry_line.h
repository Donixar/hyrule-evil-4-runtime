#pragma once
// Zealot bolt (cEmWep R1=6): cancel the line contact before Shot's effects and damage.
// Only this producer is hooked. No spherical invocation is fabricated for the line route.
#include "parry_general_runtime.h"

namespace link_runtime::parry::line {
inline constexpr uint32_t kSite = 0x3007FE;
inline constexpr uint32_t kNoContact = 0x300C73;
inline constexpr native_pins::Pin kPins[] = {
    {0x815DE0, "80047000"},
    {0x300480, "558bec83ec68a1105ec30033c58945fc568b75080fb686fe000000"},
    {0x3005F6, "8dbe94000000578d4db45157e85c79d0ff8d9e1001000057"},
    {0x3007E0, "83be20060000000f84860400006a008d559c528d45a8505753e88615d0ff83c41489459885c00f8439020000"},
    {0x3008B0, "8b862006000083c41457538b5d985053e81960d0ffb90040000083c41066854b24"},
    {0x300A45, "8d459c508d4da8515753e81caed0ff8bd883c410895d9885db0f840f020000"},
    {0x300C73, "8d460c5750e8d6d7d0ff83c4088bcee81c51d0ff80be1e06000000755a6a00"},
    {0x1D84, "e997a81b00"},
    {0x1BC620, "558bec83ec4ca1105ec30033c58945fc8b45088b4d18538b5d0c568b7510"}
};
// Shot (index 6) selects the same emWepDmCk default deflection as Throw (index 5).
inline constexpr native_pins::Pin kShotDeflect[] = {
    {0x2FFD5F, "0fb686fd00000083f80c0f87920200000fb6881c017000ff248d04017000"},
    {0x300118, "0100700000050005050505"}
};
template <class Mem> bool Qualify(Mem& m, uint32_t image_base) {
    return native_pins::All(m, image_base, kPins);
}
enum class Reason : unsigned {Blocked, Structure, Descriptor, InputBlocked, Front, Rear, Pose, Window, Fault, Reentry, Count};
inline const char* Name(Reason r) {
    switch (r) {
    case Reason::Blocked: return "blocked";
    case Reason::Structure: return "structure";
    case Reason::Descriptor: return "descriptor";
    case Reason::InputBlocked: return "input_blocked";
    case Reason::Front: return "front";
    case Reason::Rear: return "rear";
    case Reason::Pose: return "pose";
    case Reason::Window: return "window";
    case Reason::Reentry: return "reentry";
    default: return "fault";
    }
}
struct Outcome {
    bool cancel = false, pose_fallback = false;
    Reason reason = Reason::Descriptor;
    general::Front front{};
    general::Descriptor descriptor{};
    defense::PressAge press{};
};
// Same active-press, pose/fallback and cone rules as tier 2. No grace/follow admission:
// each separate bolt needs a press. No borrowed sphere-wrapper token or owner identity.
template <class Sources> Outcome Decide(Sources& s, const Candidate& c) {
    Outcome o;
    general::Descriptor again{};
    if (!s.Descriptor(c.attack, o.descriptor) || !s.Descriptor(c.attack, again) ||
        !general::Same(o.descriptor, again) || !std::isfinite(o.descriptor.radius) || o.descriptor.radius < 0 ||
        !o.descriptor.power || o.descriptor.power >= 9999 || (o.descriptor.flag & 8)) return o;
    o.reason = Reason::InputBlocked;
    if (s.Blocked()) return o;
    const bool pose = s.Pose();
    o.reason = Reason::Front;
    if (!s.Front(c, o.front)) return o;
    o.reason = Reason::Rear;
    if (!general::Parryable(o.front)) return o;
    o.reason = Reason::Pose;
    if (!pose) {
        if (!s.Guard(true)) return o;
        o.pose_fallback = true;
    }
    o.reason = Reason::Window;
    if (!s.Refresh()) return o;
    o.press = s.Press();
    if (!o.press.down || !s.Consume()) return o;
    o.reason = Reason::Blocked;
    o.cancel = true;
    return o;
}
inline volatile LONG active = 0;
inline uint32_t base = 0;
inline void* original_site = nullptr;
inline void* no_contact = nullptr;
inline void* target = nullptr;
inline Resolver resolver = nullptr;
inline general::Runtime* runtime = nullptr;
inline bool deflect = false;
inline __declspec(thread) bool busy = false, tainted = false;
inline uint64_t last_pass[static_cast<unsigned>(Reason::Count)]{};
inline void Pass(Reason reason, uint32_t owner = 0) {
    const uint64_t now = GetTickCount64();
    auto& last = last_pass[static_cast<unsigned>(reason)];
    if (!last || now - last >= 1000) {
        last = now;
        const auto press = defense::PressAt(input::state.window, now);
        general::Log("parry line pass reason=%s producer=0x%X owner=0x%08X press_age_ms=%llu",
                     Name(reason), kSite, owner, press.age_ms);
    }
}
// Every process read goes through Memory::Read (range plus SEH). Validate the actual
// Shot argument stack and ESI/EDI/EBX identity, not just an untrusted return address.
template <class Mem> bool ReadHit(Mem& m, const MachineFrame& f, uint32_t stack, uint32_t image_base, Candidate& c) {
    const uint32_t low = __readfsdword(8), high = __readfsdword(4);
    if (!f.eax || !shield::Range(f.esi, 0x624) || f.edi != f.esi + 0x94 || f.ebx != f.esi + 0x110 ||
        f.ebp < 0x68 || !StackSpan(f.ebp - 0x68, 0x74, low, high) || !StackSpan(stack, 20, low, high)) return false;
    uint32_t args[5]{}, vtable = 0, attack = 0, owner = 0;
    unsigned char routine[2]{};
    if (!m.Read(stack, args, sizeof(args)) || args[0] != f.ebx || args[1] != f.edi ||
        args[2] != f.ebp - 0x58 || args[3] != f.ebp - 0x64 || args[4] ||
        !m.Read(f.ebp + 8, &owner, 4) || owner != f.esi ||
        !m.Read(f.esi, &vtable, 4) || vtable != image_base + general::thrown::kVtable ||
        !m.Read(f.esi + 0xFC, routine, 2) || routine[0] != 1 || routine[1] != 6 ||
        !m.Read(f.esi + 0x620, &attack, 4) || !shield::Range(attack, sizeof(general::Descriptor))) return false;
    c = {f.eax, attack, f.edi, f.ebx, image_base + kSite, f.ebp, f.esi, f.esi};
    return true;
}
struct NativeSources : general::NativeSources {
    explicit NativeSources(general::Runtime& r) : general::NativeSources{r} {}
    defense::PressAge Press() {return defense::PressAt(input::state.window, Now());}
    bool Front(const Candidate& c, general::Front& f) {
        general::Memory m;
        uint32_t player = 0;
        float pa[6]{}, forward[3]{};
        owner::Observation o;
        if (!m.Read(c.current, o.current.data(), 12) || !m.Read(c.old, o.previous.data(), 12) ||
            !m.Read(r.access.image_base + static_cast<uint32_t>(r.access.code.player_slot_rva), &player, 4) ||
            !shield::Range(player, 0xAC) || !m.Read(player + 0x94, pa, sizeof(pa))) return false;
        front_math::Result res;
        // Close to Link's centre the bolt's own travel decides (current - old, at any step length; hunt-findings P2-4):
        // the native short rule (Link - current under 100 units) flipped front and back for a bolt whose short step
        // had already passed his centre.
        if (!front_native::Evaluate(r.front, r.access, o, pa, pa + 3, res, forward, true)) return false;
        std::memcpy(f.sample, o.previous.data(), 12);
        std::memcpy(f.player, pa, 12);
        f.frontal = res.frontal;
        f.dot = res.dot;
        const float flat = std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
        if (!(flat > 1e-3f)) return false;
        f.half = (forward[0] * (f.sample[0] - pa[0]) + forward[2] * (f.sample[2] - pa[2])) / flat;
        return std::isfinite(f.half);
    }
};
inline bool __cdecl Resolve(const Candidate& c) {
    if (!runtime || runtime->settings.mode != general::Mode::Active || !runtime->settings.thrown) return false;
    NativeSources source{*runtime};
    const auto o = Decide(source, c);
    if (!o.cancel || tainted) {
        Pass(tainted ? Reason::Reentry : o.reason, c.producer_owner);
        return false;
    }
    // Effects cannot turn a successful block into damage if a cosmetic native call faults.
    unsigned sparks = 0;
    bool redirected = false;
    __try {
        general::Memory memory;
        if (deflect) redirected = general::thrown::Deflect(memory, c.producer_owner, o.front.player);
        float points[1][3], rotations[1][3]{}, contact[3]{};
        if (shield::Range(c.body_part, 0x18) && memory.Read(c.body_part + 0xC, contact, 12) &&
            front_math::detail::Finite(contact)) {
            std::memcpy(points[0], contact, 12);
            if (source.pose_ok) {
                const auto& blade = source.pose.blade;
                const double a[3] = {blade.base.x, blade.base.y, blade.base.z};
                const double b[3] = {blade.tip.x, blade.tip.y, blade.tip.z};
                spark::BladePoint(a, b, contact, o.descriptor.radius, points[0]);
            }
            sparks = spark::EmitFree(runtime->sparks, points, rotations, 1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    general::Log("parry line block producer=0x%X owner=0x%08X front_dot=%.3f press_age_ms=%llu pose_fallback=%u sparks=%u deflect=%u",
                 kSite, c.producer_owner, o.front.dot, o.press.age_ms, o.pose_fallback ? 1U : 0U, sparks, redirected ? 1U : 0U);
    return true;
}
// Replay the displaced stack cleanup, then bypass both player and partner contacts.
__declspec(naked) inline void BlockedTail() {
    __asm {
        add esp,14h
        mov dword ptr [ebp-68h],0
        jmp dword ptr [no_contact]
    }
}
__declspec(noinline) inline void __cdecl Record(MachineFrame* frame) {
    const DWORD error = GetLastError();
    if (busy) {
        tainted = true;
        SetLastError(error);
        return;
    }
    busy = true;
    tainted = false;
    auto* next = frame ? reinterpret_cast<uint32_t*>(reinterpret_cast<unsigned char*>(frame) + sizeof(MachineFrame)) : nullptr;
    if (next) *next = reinterpret_cast<uint32_t>(original_site);
    __try {
        if (InterlockedCompareExchange(&active, 0, 0) && resolver && frame && frame->eax) {
            general::Memory memory;
            Candidate c{};
            if (!ReadHit(memory, *frame, reinterpret_cast<uint32_t>(next + 1), base, c)) Pass(Reason::Structure);
            else if (resolver(c) && !tainted && InterlockedCompareExchange(&active, 0, 0)) {
                frame->eax = 0;
                *next = reinterpret_cast<uint32_t>(&BlockedTail);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {Pass(Reason::Fault);}
    busy = false;
    tainted = false;
    SetLastError(error);
}
// Identical register/flags/x87/SSE preservation to the sphere pre-effects shim.
__declspec(naked) inline void Detour() {
    __asm {
        push dword ptr [original_site]
        pushfd
        pushad
        cld
        mov ebx,esp
        sub esp,528
        and esp,0fffffff0h
        fxsave [esp]
        fninit
        mov dword ptr [esp+512],1f80h
        ldmxcsr [esp+512]
        push ebx
        call Record
        add esp,4
        fxrstor [esp]
        mov esp,ebx
        popad
        popfd
        ret
    }
}
inline MH_STATUS Start(general::Runtime& r, Resolver callback, const input::Operations& ops = {}) {
    if (target) return MH_ERROR_ALREADY_CREATED;
    general::Memory memory;
    // No MinHook operation (including thread suspension) occurs on any pin mismatch.
    if (!callback || r.settings.mode != general::Mode::Active || !r.settings.thrown ||
        !r.front.verified || r.front.image_base != r.access.image_base || !Qualify(memory, r.access.image_base))
        return MH_ERROR_NOT_EXECUTABLE;
    deflect = general::thrown::Qualify(memory, r.access.image_base) &&
        native_pins::All(memory, r.access.image_base, kShotDeflect);
    if (!deflect) return MH_ERROR_NOT_EXECUTABLE;
    const auto initialized = ops.initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return initialized;
    base = r.access.image_base;
    no_contact = reinterpret_cast<void*>(base + kNoContact);
    runtime = &r;
    resolver = callback;
    target = reinterpret_cast<void*>(base + kSite);
    auto status = ops.create(target, reinterpret_cast<void*>(&Detour), &original_site);
    if (status != MH_OK) {
        target = nullptr;
        return status;
    }
    InterlockedExchange(&active, 1);
    status = ops.enable(target);
    if (status != MH_OK) InterlockedExchange(&active, 0);
    // Retain trampoline/metadata after creation: disabling admission is not quiescence.
    return status;
}
inline void Stop() {InterlockedExchange(&active, 0);}
} // namespace link_runtime::parry::line

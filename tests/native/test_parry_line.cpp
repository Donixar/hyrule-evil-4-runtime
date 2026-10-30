// Offline Shot contact transport, policy and exact native pin regression.
#define main GeneralFixtureMain
#include "test_parry_general_block.cpp"
#undef main
#include "../../dll/parry_line.h"
namespace line = link_runtime::parry::line;
namespace defense = link_runtime::defense;
struct LineSources : Fake {
    defense::ParryWindow window{};
    LineSources() {
        defense::UpdateParryButton(window, false, true, 990, 750);
        defense::UpdateParryButton(window, true, true, 1000, 750);
    }
    defense::PressAge Press() {return defense::PressAt(window, now);}
    bool Consume() {
        ++consumes;
        return defense::ConsumeParry(window, now, true);
    }
};
// Exercise the production reader and native-direction math on an isolated image.
static void __cdecl RotateFixture(const float* in, const float*, float* out) {std::memcpy(out, in, 12);}
struct GeometrySources : LineSources {
    line::NativeSources native;
    explicit GeometrySources(g::Runtime& r) : native(r) {}
    bool Front(const p::Candidate& c, g::Front& f) {return native.Front(c, f);}
};
static void Overshoot() {
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1310000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Check(image != nullptr, "allocate isolated geometry image");
    const auto base = reinterpret_cast<uint32_t>(image);
    image[0x2D06E0] = 0xE9;
    const uint32_t jump = reinterpret_cast<uint32_t>(&RotateFixture) - (base + 0x2D06E5);
    std::memcpy(image + 0x2D06E1, &jump, 4);
    unsigned char player[0xB0]{};
    const uint32_t who = reinterpret_cast<uint32_t>(player);
    std::memcpy(image + 0x100, &who, 4);
    g::Runtime r;
    r.access.image_base = base;
    r.access.code.player_slot_rva = 0x100;
    r.front = {true, base};
    float current[3] = {0, 0, -250}, old[3] = {0, 0, 500};
    p::Candidate c{};
    c.current = reinterpret_cast<uint32_t>(current);
    c.old = reinterpret_cast<uint32_t>(old);
    GeometrySources front(r);
    Check(line::Decide(front, c).cancel, "frontal bolt overshoots centre by 250 and still blocks");
    current[2] = 250;
    old[2] = -500;
    GeometrySources rear(r);
    Check(!line::Decide(rear, c).cancel, "rear bolt overshooting to front retains native damage");
    old[0] = -300;
    old[2] = -50;
    current[0] = 0;
    current[2] = -60;
    GeometrySources side(r);
    Check(line::Decide(side, c).cancel, "lateral incoming segment behind axis still blocks");
    current[2] = -200;
    old[0] = 0;
    old[2] = 25;
    GeometrySources centre(r);
    Check(line::Decide(centre, c).cancel, "near-centre incoming side keeps native current-old direction");
    VirtualFree(image, 0, MEM_RELEASE);
}
static void Policy() {
    const p::Candidate c = Hit(0x3007FE);
    LineSources s;
    auto o = line::Decide(s, c);
    Check(o.cancel && s.consumes == 1, "fresh frontal Shot contact blocks once");
    Check(!line::Decide(s, c).cancel, "another bolt needs another press");
    for (unsigned bad = 0; bad < 10; ++bad) {
        LineSources n;
        if (bad == 0) n.window = {};
        if (bad == 1) n.now = 1751;
        if (bad == 2) n.At(0, -500);
        if (bad == 3) n.pose = false;
        if (bad == 4) n.blocked = true;
        if (bad == 5) n.refresh = false;
        if (bad == 6) n.d.power = 9999;
        if (bad == 7) n.d.flag = 8;
        if (bad == 8) n.d.power = 0;
        if (bad == 9) n.front_ok = false;
        Check(!line::Decide(n, c).cancel, "unsafe or unpressed contact leaves native damage");
    }
    LineSources edge;
    edge.now = 1750;
    Check(line::Decide(edge, c).cancel, "750 ms boundary remains inclusive");
    LineSources side;
    side.At(500, 0);
    Check(line::Decide(side, c).cancel, "side uses the tier-2 cone");
    LineSources draw;
    draw.pose = false;
    draw.guard = true;
    Check(line::Decide(draw, c).pose_fallback && draw.consumes == 1, "stable drawing pose fallback uses the same press");
    LineSources released;
    defense::UpdateParryButton(released.window, false, true, 1001, 750);
    released.now = 1001;
    Check(!line::Decide(released, c).cancel, "released press cannot block");
}
static unsigned hook_calls = 0;
static MH_STATUS WINAPI InitFixture() {++hook_calls; return MH_ERROR_MEMORY_ALLOC;}
static void DeflectRequired(Image img) {
    auto* bytes = static_cast<unsigned char*>(VirtualAlloc(nullptr, img.bytes.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(bytes != nullptr, "allocate pinned image copy");
    Relocate(img, reinterpret_cast<uint32_t>(bytes));
    std::memcpy(bytes, img.bytes.data(), img.bytes.size());
    g::Memory memory;
    Check(line::Qualify(memory, img.base) && g::thrown::Qualify(memory, img.base), "all line and deflection pins initially match");
    bytes[g::thrown::kDeflect[0].rva] ^= 1;
    Check(line::Qualify(memory, img.base), "line pins still match after independent deflection mismatch");
    g::Runtime r;
    r.settings.mode = g::Mode::Active;
    r.settings.thrown = true;
    r.front = {true, img.base};
    r.access.image_base = img.base;
    p::input::Operations ops;
    ops.initialize = InitFixture;
    hook_calls = 0;
    Check(line::Start(r, line::Resolve, ops) == MH_ERROR_NOT_EXECUTABLE && !hook_calls,
          "deflection mismatch refuses Start before any MinHook operation");
    VirtualFree(bytes, 0, MEM_RELEASE);
}
static void Pins(const char* path) {
    Image img;
    FILE* file = nullptr;
    Check(!fopen_s(&file, path, "rb") && file, "open pinned image");
    std::fseek(file, 0, SEEK_END);
    img.bytes.resize(static_cast<size_t>(std::ftell(file)));
    std::rewind(file);
    Check(std::fread(img.bytes.data(), 1, img.bytes.size(), file) == img.bytes.size(), "read full image");
    std::fclose(file);
    DeflectRequired(img);
    for (unsigned relocated = 0; relocated < 2; ++relocated) {
        if (relocated) Relocate(img, 0x1000000);
        Check(line::Qualify(img, img.base), "real Shot pins qualify at preferred and relocated bases");
        Check(link_runtime::native_pins::All(img, img.base, line::kShotDeflect), "real Shot damage switch reaches the pinned thrown deflection");
        for (const auto& pin : line::kPins) {
            for (unsigned i = 0; i < std::strlen(pin.hex) / 2; ++i) {
                img.bytes[pin.rva + i] ^= 1;
                Check(!line::Qualify(img, img.base), "every changed pin byte disables the complete line route");
                img.bytes[pin.rva + i] ^= 1;
            }
        }
    }
}
static unsigned damage_calls = 0, effect_calls = 0, decisions = 0, partner_calls = 0;
static bool allow = true;
static bool decision_fault = false, decision_nested = false;
static uint32_t native_hit = 0x123450;
static unsigned char bolt[0x700]{};
static g::Descriptor bolt_attack{50, 8, 300, 0, 0};
static void* shot_site = nullptr;
__declspec(align(16)) static unsigned char fp_before[512]{}, fp_after[512]{};
static p::MachineFrame machine_after{};
static const unsigned short fp_control = 0x077F;
static const uint32_t fp_mxcsr = 0x5F80;
static bool __cdecl ResolveFixture(const p::Candidate& c) {
    ++decisions;
    if (decision_fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    if (decision_nested) line::Record(nullptr);
    Check(c.current == reinterpret_cast<uint32_t>(bolt) + 0x94 && c.old == reinterpret_cast<uint32_t>(bolt) + 0x110,
          "Shot EDI=current and EBX=old preserved through shim");
    LineSources s;
    if (!allow) s.window = {};
    return line::Decide(s, c).cancel;
}
// Real displaced instructions and the following native test/branch; stand-ins count damage/effects.
__declspec(naked) static void NativeTail() {
    __asm {
        add esp,14h
        mov [ebp-68h],eax
        test eax,eax
        jz miss
        inc damage_calls
        inc effect_calls
        jmp done
    miss:
        inc partner_calls
    done:
        pop edi
        pop ebx
        pop esi
        mov esp,ebp
        pop ebp
        ret
    }
}
__declspec(naked) static void CaptureTail() {
    __asm {
        pushfd
        pushad
        cld
        mov esi,esp
        mov edi,offset machine_after
        mov ecx,9
        rep movsd
        fxsave fp_after
        popad
        popfd
        jmp NativeTail
    }
}
__declspec(naked) static void NoContactTail() {
    __asm {
        pushfd
        pushad
        cld
        mov esi,esp
        mov edi,offset machine_after
        mov ecx,9
        rep movsd
        fxsave fp_after
        popad
        popfd
        pop edi
        pop ebx
        pop esi
        mov esp,ebp
        pop ebp
        ret
    }
}
__declspec(naked) static void Shot(void*) {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,68h
        push esi
        push ebx
        push edi
        mov esi,[ebp+8]
        lea edi,[esi+94h]
        lea ebx,[esi+110h]
        push 0
        lea edx,[ebp-64h]
        push edx
        lea eax,[ebp-58h]
        push eax
        push edi
        push ebx
        fninit
        fldcw fp_control
        fld1
        ldmxcsr fp_mxcsr
        pcmpeqd xmm0,xmm0
        pcmpeqd xmm7,xmm7
        fxsave fp_before
        mov eax,native_hit
        push 0AD7h
        popfd
        jmp dword ptr [shot_site]
    }
}
static void Transport() {
    const uint32_t vtable = 0x400000 + 0x716ED0;
    const uint32_t descriptor = reinterpret_cast<uint32_t>(&bolt_attack);
    std::memcpy(bolt, &vtable, 4);
    std::memcpy(bolt + 0x620, &descriptor, 4);
    bolt[0xFC] = 1;
    bolt[0xFD] = 6;
    line::no_contact = reinterpret_cast<void*>(&NoContactTail);
    line::base = 0x400000;
    line::resolver = ResolveFixture;
    line::original_site = reinterpret_cast<void*>(&CaptureTail);
    shot_site = reinterpret_cast<void*>(&line::Detour);
    InterlockedExchange(&line::active, 1);
    SetLastError(1234);
    Shot(bolt);
    __asm fninit
    const unsigned normal_mxcsr = 0x1F80;
    __asm ldmxcsr normal_mxcsr
    Check(!partner_calls, "blocked bolt never tests the same segment against Ashley");
    Check(!damage_calls && !effect_calls && decisions == 1 && GetLastError() == 1234, "actual x86 shim skips damage/effects and preserves LastError");
    Check(!std::memcmp(fp_before, fp_after, 512), "full x87/SSE/MXCSR image is restored before native continuation");
    Check(machine_after.esi == reinterpret_cast<uint32_t>(bolt) && machine_after.ebx == reinterpret_cast<uint32_t>(bolt) + 0x110 &&
          machine_after.edi == reinterpret_cast<uint32_t>(bolt) + 0x94 && (machine_after.eflags & 0x400) == (0xAD7 & 0x400),
          "producer registers and direction flag survive native stack cleanup");
    allow = false;
    Shot(bolt);
    Check(damage_calls == 1 && effect_calls == 1, "no press retains exact native hit path");
    allow = true;
    bolt[0xFD] = 5;
    Shot(bolt);
    Check(damage_calls == 2 && decisions == 2, "non-Shot object is never offered to resolver");
    bolt[0xFD] = 6;
    InterlockedExchange(&line::active, 0);
    Shot(bolt);
    Check(damage_calls == 3 && decisions == 2, "disabled transport never makes a decision");
    InterlockedExchange(&line::active, 1);
    native_hit = 0;
    Shot(bolt);
    Check(damage_calls == 3 && decisions == 2 && partner_calls == 1, "native miss stays a miss without input consumption");
    native_hit = 0x123450;
    decision_fault = true;
    Shot(bolt);
    Check(damage_calls == 4, "SEH failure preserves native damage");
    decision_fault = false;
    decision_nested = true;
    Shot(bolt);
    Check(damage_calls == 5, "nested entry taints the outer decision and retains damage");
    decision_nested = false;
    const uint32_t invalid = 1;
    std::memcpy(bolt + 0x620, &invalid, 4);
    Shot(bolt);
    Check(damage_calls == 6 && decisions == 4, "invalid descriptor pointer rejected before resolver");
    std::memcpy(bolt + 0x620, &descriptor, 4);
    // Install MinHook over the exact six displaced bytes and exercise its actual trampoline.
    auto* site = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Check(site != nullptr, "allocate standalone native site");
    const unsigned char displaced[] = {0x83, 0xC4, 0x14, 0x89, 0x45, 0x98, 0xE9};
    std::memcpy(site, displaced, sizeof(displaced));
    const uint32_t jump = reinterpret_cast<uint32_t>(&NativeTail) + 6 - reinterpret_cast<uint32_t>(site + 11);
    std::memcpy(site + 7, &jump, 4);
    Check(MH_Initialize() == MH_OK, "initialize fixture MinHook");
    Check(MH_CreateHook(site, reinterpret_cast<void*>(&line::Detour), &line::original_site) == MH_OK, "MinHook relocates exact six-byte Shot site");
    Check(MH_EnableHook(site) == MH_OK, "enable fixture Shot hook");
    shot_site = site;
    Shot(bolt);
    Check(damage_calls == 6 && partner_calls == 1, "real MinHook hook bypasses player and partner on blocked Shot");
    allow = false;
    Shot(bolt);
    Check(damage_calls == 7, "real MinHook trampoline retains unpressed Shot damage");
    Check(MH_DisableHook(site) == MH_OK && MH_RemoveHook(site) == MH_OK && MH_Uninitialize() == MH_OK, "remove standalone fixture hook");
    VirtualFree(site, 0, MEM_RELEASE);
    __asm fninit
    __asm ldmxcsr normal_mxcsr
    InterlockedExchange(&line::active, 0);
}
static void DisabledPins() {
    line::target = nullptr;
    g::Runtime r;
    r.settings.mode = g::Mode::Active;
    r.front = {true, 0x10000};
    r.access.image_base = 0x10000;
    p::input::Operations ops;
    ops.initialize = InitFixture;
    Check(line::Start(r, ResolveFixture, ops) == MH_ERROR_NOT_EXECUTABLE && !hook_calls,
          "unreadable or mismatched pins call no hook operation and never suspend threads");
}
int main(int argc, char** argv) {
    Check(argc <= 2, "optional pinned mapped image argument");
    Overshoot();
    Policy();
    if (argc == 2) Pins(argv[1]);
    Transport();
    DisabledPins();
    std::printf("parry line: %u checks passed\n", checks);
    return 0;
}

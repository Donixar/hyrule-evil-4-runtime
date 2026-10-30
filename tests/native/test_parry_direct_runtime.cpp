// Execute production x86 shims and real MinHook trampolines in an isolated process.
#define DIRECT_ROUTE_FIXTURE_INCLUDED
#include "test_parry_direct_routes.cpp"
static unsigned decisions = 0, damages = 0, effects = 0, partner_checks = 0;
static bool allow = true, fault = false, nested = false;
static unsigned route_index = 0;
static uint32_t frame_address = 0;
static void* fixture_entry = nullptr;
static void* native_trampoline = nullptr;
static unsigned char actor[0xB00]{}, fixture_tree[0x200]{};
static uint32_t slot_value = 0xABCDE123;
static p::MachineFrame frame_before{}, frame_after{};
__declspec(align(16)) static unsigned char fp_before[512]{}, fp_after[512]{};
static const uint32_t test_mxcsr = 0x5F80, normal_mxcsr = 0x1F80;
static const unsigned short test_control = 0x077F;
static bool __cdecl DecisionFixture(const d::Contact& c) {
    ++decisions;
    Check(c.actor.owner == reinterpret_cast<uint32_t>(actor), "shim attributes actual register owner");
    if (fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    if (nested) {
        struct FrameAndNext {p::MachineFrame f; uint32_t next;} inner{};
        d::Record(&inner.f, route_index);
    }
    SetLastError(999);
    return allow;
}
__declspec(naked) static void ReturnTail() {
    __asm {
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) static void PassTail() {
    __asm {
        cmp route_index, 3
        jne no_pushes
        add esp, 8
    no_pushes:
        inc damages
        inc effects
        inc partner_checks
        jmp ReturnTail
    }
}
__declspec(naked) static void SkipTail() {
    __asm {
        pushfd
        pushad
        cld
        mov esi, esp
        mov edi, offset frame_after
        mov ecx, 9
        rep movsd
        fxsave fp_after
        popad
        popfd
        inc partner_checks
        jmp ReturnTail
    }
}
__declspec(naked) static void CapturePass() {
    __asm {
        pushfd
        pushad
        cld
        mov esi, esp
        mov edi, offset frame_after
        mov ecx, 9
        rep movsd
        fxsave fp_after
        popad
        popfd
        jmp dword ptr [native_trampoline]
    }
}
__declspec(naked) static void Exercise() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp, frame_address
        mov esi, offset actor
        mov edi, offset actor
        mov eax, 12345678h
        mov ecx, 87654321h
        mov edx, 24681234h
        mov ebx, 10203040h
        fninit
        fldcw test_control
        fld1
        ldmxcsr test_mxcsr
        pcmpeqd xmm0, xmm0
        pcmpeqd xmm7, xmm7
        fxsave fp_before
        push 0ED7h
        popfd
        pushfd
        pushad
        cld
        mov esi, esp
        mov edi, offset frame_before
        mov ecx, 9
        rep movsd
        popad
        popfd
        jmp dword ptr [fixture_entry]
    }
}
static void RunExercise() {
    Exercise();
    __asm cld
    __asm fninit
    __asm ldmxcsr normal_mxcsr
}
static void ActorFor(d::Route route) {
    std::memset(actor, 0, sizeof(actor));
    const auto& spec = d::kSpecs[static_cast<unsigned>(route)];
    const uint32_t vt = 0x400000 + spec.vtable, flags = 1, parts = reinterpret_cast<uint32_t>(fixture_tree);
    std::memcpy(actor, &vt, 4);
    std::memcpy(actor + 4, &flags, 4);
    std::memcpy(actor + 0xF4, &parts, 4);
    actor[0xFC] = 1;
    actor[0x100] = spec.id;
    actor[0x101] = 1;
    if (route == d::Route::Krauser) actor[0xFD] = 0x2B;
    if (route == d::Route::Bats) {actor[0xFD] = 5; actor[0xFE] = 2;}
    if (route == d::Route::Cart) {actor[0xFD] = 4; actor[0xFE] = 1;}
    if (route == d::Route::Tree) {
        actor[0xFD] = 0xE;
        std::memcpy(actor + 0x908, &parts, 4);
        fixture_tree[0x100] = 0x49;
    }
}
static void TransportAll() {
    uint32_t native_frame[2] = {0, 0x400000 + 0x152D24};
    frame_address = reinterpret_cast<uint32_t>(native_frame);
    d::image_base = 0x400000;
    d::decision = DecisionFixture;
    Check(MH_Initialize() == MH_OK, "initialize real fixture MinHook");
    for (unsigned i : {0U, 2U, 3U, 4U}) {
        route_index = i;
        const auto route = static_cast<d::Route>(i);
        auto& hook = d::hooks[i];
        ActorFor(route);
        auto* site = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        Check(site != nullptr, "allocate isolated contact site");
        unsigned length = 0;
        if (i == 0 || i == 2) {
            const unsigned char bytes[] = {0x8B, 0x0D, 0, 0, 0, 0};
            std::memcpy(site, bytes, sizeof(bytes));
            const uint32_t slot = reinterpret_cast<uint32_t>(&slot_value);
            std::memcpy(site + 2, &slot, 4);
            length = 6;
        } else if (i == 3) {
            const unsigned char bytes[] = {0x6A, 0, 0x68, 0xF4, 1, 0, 0};
            std::memcpy(site, bytes, sizeof(bytes));
            length = 7;
        } else {
            const unsigned char bytes[] = {0xC6, 0x86, 0x56, 0xA, 0, 0, 1};
            std::memcpy(site, bytes, sizeof(bytes));
            length = 7;
        }
        site[length] = 0xE9;
        const uint32_t jump = reinterpret_cast<uint32_t>(&PassTail) - reinterpret_cast<uint32_t>(site + length + 5);
        std::memcpy(site + length + 1, &jump, 4);
        Check(MH_CreateHook(site, d::Detour(route), &hook.original) == MH_OK, "MinHook relocates real displaced route instructions");
        native_trampoline = hook.original;
        hook.original = reinterpret_cast<void*>(&CapturePass);
        Check(MH_EnableHook(site) == MH_OK, "enable isolated native hook");
        hook.skip = reinterpret_cast<uint32_t>(&SkipTail);
        hook.active = 1;
        fixture_entry = site;
        damages = effects = partner_checks = decisions = 0;
        allow = true;
        SetLastError(1234);
        RunExercise();
        Check(!damages && !effects && decisions == 1 && partner_checks == 1 && GetLastError() == 1234,
              "block skips all damage/effects but retains continuation and LastError");
        Check(!std::memcmp(fp_before, fp_after, sizeof(fp_before)), "production shim restores full x87/SSE/MXCSR image");
        // PUSHAD's saved ESP and every GPR/flag must match at the selected continuation.
        Check(!std::memcmp(&frame_before, &frame_after, sizeof(frame_before)), "all integer registers, original ESP and flags survive cancellation");
        Check(!actor[0xA56], "block does not publish the fixture_tree contact latch");
        allow = false;
        RunExercise();
        Check(damages == 1 && effects == 1 && partner_checks == 2, "rejection executes original displaced instructions and effects");
        Check(!std::memcmp(&frame_before, &frame_after, sizeof(frame_before)) && !std::memcmp(fp_before, fp_after, sizeof(fp_before)) &&
              GetLastError() == 1234, "rejection restores all state before executing the real native trampoline");
        if (i == 4) Check(actor[0xA56] == 1, "native fixture_tree latch still writes on pass");
        actor[0xA56] = 0;
        allow = true;
        hook.active = 0;
        RunExercise();
        Check(damages == 2 && decisions == 2, "Off leaves native path unchanged");
        actor[0xA56] = 0;
        hook.active = 1;
        fault = true;
        RunExercise();
        Check(damages == 3 && !d::busy, "SEH unwind retains native path and clears admission scope");
        actor[0xA56] = 0;
        fault = false;
        nested = true;
        RunExercise();
        Check(damages == 4, "nested call taints outer cancellation");
        actor[0xA56] = 0;
        nested = false;
        const unsigned old_decisions = decisions;
        actor[0x100] ^= 1;
        RunExercise();
        Check(damages == 5 && decisions == old_decisions, "wrong actor identity rejected frame_before policy");
        ActorFor(route);
        if (i == 0) {
            native_frame[1] = 0x400000 + 0x152E48;
            actor[0xFD] = 0x2C;
            RunExercise();
            Check(damages == 5, "low kick caller and matching routine admitted");
            native_frame[1] = 0x400000 + 0x152D24;
            RunExercise();
            Check(damages == 6, "mismatched kick caller leaves effects intact");
        }
        if (i == 3) {
            actor[0x101] = 0;
            RunExercise();
            Check(damages == 6, "truck type zero never reaches policy");
        }
        Check(MH_DisableHook(site) == MH_OK && MH_RemoveHook(site) == MH_OK, "remove only fixture hook");
        hook.active = 0;
        VirtualFree(site, 0, MEM_RELEASE);
    }
    Check(MH_Uninitialize() == MH_OK, "fixture hook cleanup");
}
static unsigned hook_operations = 0;
static MH_STATUS WINAPI InitDirectFixture() {++hook_operations; return MH_OK;}
static void NoPartialInstall() {
    g::Runtime r;
    r.settings.mode = g::Mode::Active;
    r.front = {true, 0x10000};
    r.access.image_base = 0x10000;
    p::input::Operations ops;
    ops.initialize = InitDirectFixture;
    for (unsigned i = 0; i < d::kCount; ++i)
        Check(d::Start(r, static_cast<d::Route>(i), DecisionFixture, ops) == MH_ERROR_NOT_EXECUTABLE && !hook_operations,
              "unreadable pins or unproven Novistador invoke no hook operations");
}
int main() {
    DirectRoutesFixtureMain(1, nullptr);
    TransportAll();
    NoPartialInstall();
    std::printf("parry direct runtime: %u checks passed\n", checks);
    return 0;
}

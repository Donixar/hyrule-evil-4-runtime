#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <initializer_list>
#include "../../dll/link_runtime_trace.h"
#include "../../dll/link_runtime_damage.h"
#include "MinHook.h"

struct Target { int kind; };
static Target* seen_target = nullptr;
static unsigned seen_weapon = 0;
static int seen_near = 0, expected_result = 0, calls = 0, observations = 0;
static int __cdecl Original(Target* target, unsigned weapon, int proximity) {
    seen_target = target; seen_weapon = weapon; seen_near = proximity; ++calls;
    return expected_result;
}
static void Observe(Target* target, unsigned weapon, int proximity, int result) {
    if (target != seen_target || weapon != seen_weapon || proximity != seen_near || result != expected_result)
        std::abort();
    ++observations;
}
static void Require(bool ok, const char* text) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", text); std::exit(1); }
}

// Real x86 patch/trampoline fixture: no RE4 process, input or game memory is accessed.
__declspec(noinline) static int __cdecl Fixture(Target* target, unsigned weapon, int proximity) {
    return target->kind + static_cast<int>(weapon) * 3 + proximity;
}
using Damage = int(__cdecl*)(Target*, unsigned, int);
static Damage trampoline = nullptr;
static int hooked_observations = 0;
static int __cdecl Detour(Target* target, unsigned weapon, int proximity) {
    const int result = link_runtime::ObserveDamage(trampoline, [](Target*, unsigned, int, int) {
        ++hooked_observations;
    }, target, weapon, proximity);
    return result;
}

int main() {
    Target targets[] = {{0}, {1}, {14}, {16}, {79}};
    for (auto& target : targets) {
        for (unsigned weapon : {0U, 0x10U, 0x14U, 0x30U, 0xFFFFFFFFU}) {
            for (int result : {0, 200, -3, INT_MAX, INT_MIN}) {
                expected_result = result;
                Require(link_runtime::ObserveDamage(Original, Observe, &target, weapon, -2) == result,
                        "original arguments and return preserved for every weapon/target");
            }
        }
    }
    Require(calls == 125 && observations == calls, "one original call and one observation per invocation");
    Require(link_runtime::TraceLimit(0) == 500 && link_runtime::TraceLimit(5001) == 500 &&
            link_runtime::TraceLimit(1) == 1 && link_runtime::TraceLimit(5000) == 5000, "trace limit bounds");
    Require(MH_Initialize() == MH_OK, "MinHook initialized");
    Require(MH_CreateHook(reinterpret_cast<void*>(&Fixture), reinterpret_cast<void*>(&Detour),
                         reinterpret_cast<void**>(&trampoline)) == MH_OK, "MinHook fixture created");
    Require(MH_EnableHook(reinterpret_cast<void*>(&Fixture)) == MH_OK, "MinHook fixture enabled");
    Damage volatile call = &Fixture;
    Target target{19};
    Require(call(&target, 0x10, -4) == 63 && call(&target, 0x14, -4) == 75 && hooked_observations == 2,
            "real x86 knife and kick calls retain exact return via trampoline");
    Require(call(&target, 0x10, 0) == 67 && call(&target, 0x14, 0) == 79 && hooked_observations == 4,
            "real x86 knife and kick stay native through the observer");
    Require(MH_DisableHook(reinterpret_cast<void*>(&Fixture)) == MH_OK, "MinHook fixture disabled");
    Require(call(&target, 0x14, -4) == 75 && hooked_observations == 4, "disabled hook remains original");
    Require(MH_Uninitialize() == MH_OK, "MinHook uninitialized");
    std::puts("trace: 125 unchanged calls + real x86 MinHook knife/kick observation passed");
}

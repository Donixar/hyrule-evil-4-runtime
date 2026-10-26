// LowHealth-only regression for the latched STA_INTO_SHOP seen in r11c.
// LOW_HEALTH_LEGACY_READER replays the pre-fix production reader, so the first
// r11c assertion must fail. No native process, memory writes or audio device.
#include "../../dll/link_runtime_health.h"
#ifndef LOW_HEALTH_LEGACY_READER
#include "../../dll/low_health.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

static unsigned checks = 0;
static void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Memory {
    std::unordered_map<uint32_t, uint32_t> values;
    uint32_t replace_slot = 0;
    unsigned slot_reads = 0;
    bool Read32(uint32_t address, uint32_t& value) {
        if (address == replace_slot && ++slot_reads == 2) values[address] += 0x10000;
        auto found = values.find(address);
        if (found == values.end()) return false;
        value = found->second;
        return true;
    }
};

constexpr uint32_t kPlayerSlot = 0x10000, kGlobalSlot = 0x10004, kGlobal = 0x30000;
static Memory R11c() {
    Memory m;
    m.values[kPlayerSlot] = 0x20000;
    m.values[kGlobalSlot] = kGlobal;
    m.values[kGlobal + 0x20] = 3;
    m.values[kGlobal + 0x54] = 0x010008E4;
    m.values[kGlobal + 0x170] = 0;
    m.values[kGlobal + 0x4FB4] = (1440U << 16) | 300U;
    m.values[kGlobal + 0x4FC8] = 0;
    m.values[kGlobal + 0x501C] = 0x03000110;
    m.values[kGlobal + 0x5020] = 0x02000600;
    m.values[kGlobal + 0x5024] = 0x00040000;
    m.values[kGlobal + 0x5028] = 0;
    return m;
}

static bool AlertHealth(Memory& m, link_runtime::HealthState& state, bool fix = true) {
#ifdef LOW_HEALTH_LEGACY_READER
    (void)fix;
    return link_runtime::ReadHealth(m, kPlayerSlot, kGlobalSlot, state);
#else
    return link_runtime::low_health::ReadHealth(m, kPlayerSlot, kGlobalSlot, fix, state);
#endif
}

static void R11cAlertAndCadence() {
    auto m = R11c();
    const auto original = m.values;
    link_runtime::HealthState old, state;
    Check(link_runtime::ReadHealth(m, kPlayerSlot, kGlobalSlot, old) && !old.eligible,
          "shared ReadHealth still rejects r11c's latched STA_INTO_SHOP");
    link_runtime::AlertClock legacy_clock, clock;
    Check(!legacy_clock.Due(old, true, 360, 4000, 100), "pre-fix reader makes r11c's low HP silent");
    Check(AlertHealth(m, state) && state.eligible && state.hp == 300 && state.max_hp == 1440,
          "LowHealth must accept r11c's combat snapshot with STA_INTO_SHOP latched");
    Check(clock.Due(state, true, 360, 4000, 100), "r11c low HP starts the alert");
    Check(!clock.Due(state, true, 360, 4000, 4099), "r11c low HP cannot spam before 4000 ms");
    Check(clock.Due(state, true, 360, 4000, 4100), "r11c low HP repeats at the configured interval");
    Check(AlertHealth(m, state, false) && !state.eligible, "fix=0 restores the legacy gate");
    Check(m.values == original, "LowHealth never clears the real STA_INTO_SHOP or writes health");

    m.values[kGlobal + 0x5024] |= 0x04000000;  // actual sub-screen request (shop/case)
    Check(AlertHealth(m, state) && !state.eligible && !clock.Due(state, true, 360, 4000, 4200),
          "shop/case request cancels a running alert");
    m.values[kGlobal + 0x5024] = 0x00040000;
    Check(AlertHealth(m, state) && clock.Due(state, true, 360, 4000, 4300),
          "leaving a real shop/case starts a fresh alert if HP is still low");
    m.values[kGlobal + 0x4FB4] = (1440U << 16) | 361U;
    Check(AlertHealth(m, state) && !clock.Due(state, true, 360, 4000, 4400), "healing above threshold silences alert");
    m.values[kGlobal + 0x4FB4] = (1440U << 16) | 360U;
    Check(AlertHealth(m, state) && clock.Due(state, true, 360, 4000, 4500), "threshold itself remains eligible");
    Check(!clock.Due(state, false, 360, 4000, 8500), "LowHealth=0 remains silent");
}

static void EveryOtherGate() {
    struct Gate { uint32_t offset, bit; const char* name; };
    // Each bit is literal from the existing UHD policy. Keep SPF_KEY here:
    // this fix relaxes only STA_INTO_SHOP, unlike the fairy's own policy.
    const Gate gates[] = {
        {0x54, 0x20000000, "SYS_EXCEPTION"}, {0x54, 0x04000000, "SYS_SOFT_RESET"},
        {0x54, 0x02000000, "SYS_DATA_READ"}, {0x54, 0x00200000, "SYS_DOORDEMO"},
        {0x54, 0x00100000, "SYS_TRANS_STOP"}, {0x54, 0x00080000, "SYS_CONTINUE"},
        {0x54, 0x00008000, "SYS_SSCRN_EXEC"}, {0x54, 0x00000400, "SYS_SCREEN_STOP"},
        {0x54, 0x00000100, "SYS_LOAD_GAME"},
        {0x170, 0x80000000, "SPF_KEY"}, {0x170, 0x10000000, "SPF_PL"}, {0x170, 0x02000000, "SPF_CTRL"},
        {0x501C, 0x10000000, "STA_MOVIE_ON"}, {0x501C, 0x08000000, "STA_CUTCHG"},
        {0x501C, 0x04000000, "STA_MOVIE2_ON"}, {0x501C, 0x00100000, "STA_DIEDEMO"},
        {0x501C, 0x00040000, "STA_SUB_SCRN"}, {0x501C, 0x00001000, "STA_EVENT"},
        {0x501C, 0x00000080, "STA_BLACKOUT"},
        {0x5020, 0x10000000, "STA_SUSPEND"}, {0x5020, 0x00000002, "STA_ITEM_GET"},
        {0x5024, 0x04000000, "STA_SSCRN_REQUEST"}, {0x5024, 0x00080000, "STA_EVENT_SYSYTEM"},
        {0x5024, 0x00008000, "STA_TITLE"},
        {0x5028, 0x00008000, "STA_DIV_EVENT_SET"}, {0x5028, 0x00004000, "STA_DIV_EVENT_ING"},
        {0x5028, 0x00002000, "STA_NOW_LOADING"}, {0x5028, 0x00001000, "STA_SCREEN_STOP_RESERVE"},
        {0x5028, 0x00000800, "STA_TYPEWRITER"},
    };
    for (const auto& gate : gates) {
        auto m = R11c();
        m.values[kGlobal + gate.offset] |= gate.bit;
        link_runtime::HealthState state;
        link_runtime::AlertClock clock;
        Check(AlertHealth(m, state) && !state.eligible && !clock.Due(state, true, 360, 4000, 0), gate.name);
    }
    // Real menu is independently blocked by request, suspension and native stop,
    // including when the stale combat/shop flag is absent.
    for (bool latched : {false, true}) {
        auto m = R11c();
        m.values[kGlobal + 0x5024] = latched ? 0x04040000 : 0x04000000;
        m.values[kGlobal + 0x5020] |= 0x10000000;
        m.values[kGlobal + 0x170] = 0xFFFFFFFF;
        link_runtime::HealthState state;
        Check(AlertHealth(m, state) && !state.eligible, "actual shop/case stays silent with or without STA_INTO_SHOP");
    }
}

static void BadSnapshots() {
    for (uint32_t routine : {0U, 4U}) {
        auto m = R11c(); m.values[kGlobal + 0x20] = routine;
        link_runtime::HealthState state;
        Check(AlertHealth(m, state) && !state.eligible, "title/room transition remains silent");
    }
    for (uint32_t character : {1U, 2U}) {
        auto m = R11c(); m.values[kGlobal + 0x4FC8] = character;
        link_runtime::HealthState state;
        Check(AlertHealth(m, state) && !state.eligible, "other characters remain silent");
    }
    {
        auto m = R11c(); m.values[kGlobal + 0x4FB4] = 1440U << 16;
        link_runtime::HealthState state;
        Check(AlertHealth(m, state) && !state.eligible, "dead player remains silent");
    }
    for (uint32_t packed : {300U, (1440U << 16) | 1441U, (1440U << 16) | 0xFFFFU, (0xFFFFU << 16) | 300U}) {
        auto m = R11c(); m.values[kGlobal + 0x4FB4] = packed;
        link_runtime::HealthState state;
        Check(!AlertHealth(m, state), "impossible signed HP/max rejected");
    }
    for (uint32_t slot : {kPlayerSlot, kGlobalSlot}) {
        auto m = R11c(); m.replace_slot = slot;
        link_runtime::HealthState state;
        Check(!AlertHealth(m, state), "player/global slot replacement invalidates the snapshot");
        m = R11c(); m.values[slot] = 0;
        Check(!AlertHealth(m, state), "missing player/global pointer rejected");
    }
    for (uint32_t offset : {0x20U, 0x54U, 0x170U, 0x4FB4U, 0x4FC8U, 0x501CU, 0x5020U, 0x5024U, 0x5028U}) {
        auto m = R11c(); m.values.erase(kGlobal + offset);
        link_runtime::HealthState state;
        Check(!AlertHealth(m, state), "any unreadable state word rejects the snapshot");
    }
}

int main() {
    R11cAlertAndCadence();
    EveryOtherGate();
    BadSnapshots();
    std::printf("LowHealth r11c: %u checks passed; shared gates unchanged; read only\n", checks);
}

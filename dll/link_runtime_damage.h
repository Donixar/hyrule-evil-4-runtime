#pragma once
#include "link_runtime_health.h"

namespace link_runtime {
struct DamageSnapshot {
    bool valid = false, gameplay = false, caller_verified = false;
    uint32_t player = 0, target = 0;
    unsigned id = 0, native_weapon = 0;
    int hp = 0, max_hp = 0;
};
template <class Memory>
bool ReadDamageSnapshot(Memory& memory, uint32_t player_slot, uint32_t global_slot,
                        uint32_t target, bool caller_verified, DamageSnapshot& result) {
    result = {};
    HealthState health;
    if (!IsPointer(target) || !ReadHealth(memory, player_slot, global_slot, health)) return false;
    uint32_t id = 0, life = 0, damage = 0, current_player = 0;
    if (target == health.player || !memory.Read32(target + 0x100, id) ||
        !memory.Read32(target + 0x324, life) || !memory.Read32(target + 0x32C, damage) ||
        !memory.Read32(player_slot, current_player) || current_player != health.player) return false;
    const int hp = static_cast<int16_t>(life & 0xFFFF), maximum = static_cast<int16_t>(life >> 16);
    if (hp < 0 || maximum <= 0 || hp > maximum) return false;
    result = {true, health.eligible, caller_verified, health.player, target,
              id & 0xFF, (damage >> 16) & 0xFF, hp, maximum};
    return true;
}
inline bool RelativeTarget(const unsigned char* image, size_t size, size_t instruction,
                           unsigned char opcode, size_t& target) {
    if (instruction > size || size - instruction < 5 || image[instruction] != opcode) return false;
    int32_t displacement = 0;
    std::memcpy(&displacement, image + instruction + 1, 4);
    const int64_t value = int64_t(instruction) + 5 + displacement;
    if (value < 0 || uint64_t(value) >= size) return false;
    target = static_cast<size_t>(value); return true;
}
// Aligned UHD sequence: weapon=[EDI+32E], push proximity, weapon, same EDI.
// Resolve rel32 CALL -> optional rel32 thunk JMP -> the uniquely located callee.
// No absolute executable addresses are copied into an ASLR process.
inline size_t FindSwordCaller(const unsigned char* image, size_t size, size_t begin,
                              size_t span, size_t callee, unsigned* prefix_count = nullptr) {
    static const unsigned char prefix[] = {0x8A,0x87,0x2E,0x03,0,0,0xBE,0x64,0,0,0,0x89,0x75,0xF4,
        0x3C,0x31,0x73,0x17,0x0F,0xB6,0x4D,0xFF,0x0F,0xB6,0xD0,0x51,0x52,0x57,0xE8};
    if (prefix_count) *prefix_count = 0;
    if (!image || begin >= size || span > size - begin || callee >= size) return 0;
    size_t found = 0; unsigned hits = 0;
    for (size_t pos = begin; pos + 36 <= begin + span; ++pos) {
        if (std::memcmp(image + pos, prefix, sizeof(prefix))) continue;
        ++hits;
        if (image[pos + 33] != 0x83 || image[pos + 34] != 0xC4 || image[pos + 35] != 0x0C) continue;
        size_t target = 0;
        if (!RelativeTarget(image, size, pos + 28, 0xE8, target)) continue;
        if (target != callee && !RelativeTarget(image, size, target, 0xE9, target)) continue;
        if (target == callee) found = pos + 33;
    }
    if (prefix_count) *prefix_count = hits;
    return hits == 1 ? found : 0;
}
struct ExecutableRegion { size_t begin, span; };
inline size_t FindSwordExecutableCaller(const unsigned char* image, size_t size, size_t callee,
                                        const ExecutableRegion* regions, size_t count) {
    if (!regions || !count) return 0;
    // Count EVERY prefix across executable sections, including sections with
    // duplicates. Never read data/discardable sections or mapped-image gaps.
    size_t found = 0; unsigned total_prefixes = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto& region = regions[i];
        if (region.begin >= size || region.span > size - region.begin) return 0;
        unsigned prefixes = 0;
        const size_t candidate = FindSwordCaller(image, size, region.begin, region.span, callee, &prefixes);
        total_prefixes += prefixes;
        if (total_prefixes > 1) return 0;
        if (candidate) found = candidate;
    }
    return total_prefixes == 1 ? found : 0;
}
}  // namespace link_runtime

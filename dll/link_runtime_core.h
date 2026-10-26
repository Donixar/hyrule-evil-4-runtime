#pragma once
#include <cstdint>

namespace link_runtime {
enum class StepResult { NoPlayer, NotLink, InvalidMemory, Unchanged, Changed };

inline bool IsPointer(uint32_t value) { return value > 0x10000 && value < 0x7FFF0000; }

template <class Memory>
bool ReadSwordCostume(Memory& memory, uint32_t player_slot, uint32_t global_slot, unsigned& costume) {
    uint32_t player = 0, global = 0, character = 0, current = 0;
    if (!memory.Read32(player_slot, player) || !IsPointer(player) ||
        !memory.Read32(global_slot, global) || !IsPointer(global) ||
        !memory.Read32(global + 0x4FC8, character) || (character & 0xFF) != 0 ||
        !memory.Read32(global_slot, current) || current != global ||
        !memory.Read32(player_slot, current) || current != player) return false;
    costume = (character >> 8) & 0xFF;
    return costume <= 4;
}

// Memory supplies checked Read32 and Compare32. Offsets/guards match re4_sword.py.
// Compare32 avoids overwriting unrelated flags changed by the game between reads.
template <class Memory>
StepResult SwordStep(Memory& memory, uint32_t player_slot, unsigned costume = 0, uint32_t global_slot = 0) {
    if (costume > 4) return StepResult::NotLink;
    unsigned knife_index = costume == 4 ? 1 : 2, back_index = costume == 4 ? 4 : 5;
    uint32_t player = 0;
    if (!memory.Read32(player_slot, player)) return StepResult::InvalidMemory;
    if (!IsPointer(player)) return StepResult::NoPlayer;
    uint32_t node = 0;
    if (!memory.Read32(player + 0x15C, node)) return StepResult::InvalidMemory;
    uint32_t models[16] = {};
    unsigned count = 0;
    while (IsPointer(node)) {
        if (count == 16) return StepResult::InvalidMemory;
        for (unsigned i = 0; i < count; ++i)
            if (models[i] == node) return StepResult::InvalidMemory;
        models[count++] = node;
        if (!memory.Read32(node + 0x14, node)) return StepResult::InvalidMemory;
    }
    if (count <= back_index) return StepResult::NotLink;
    uint32_t knife_flags = 0, back_flags = 0;
    if (!memory.Read32(models[knife_index] + 4, knife_flags) ||
        !memory.Read32(models[back_index] + 4, back_flags)) return StepResult::InvalidMemory;
    if (!(back_flags & 0x40)) return StepResult::NotLink;
    uint32_t desired = knife_flags & 8 ? back_flags & ~8U : back_flags | 8U;
    if (desired == back_flags) return StepResult::Unchanged;
    uint32_t current_player = 0;
    if (!memory.Read32(player_slot, current_player) || current_player != player)
        return StepResult::InvalidMemory;
    unsigned current_costume = 0;
    if (global_slot && (!ReadSwordCostume(memory, player_slot, global_slot, current_costume) || current_costume != costume))
        return StepResult::InvalidMemory;
    return memory.Compare32(models[back_index] + 4, back_flags, desired) ?
        StepResult::Changed : StepResult::InvalidMemory;
}
}  // namespace link_runtime

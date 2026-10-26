#pragma once
#include "link_runtime_core.h"
#include <vector>
#include <cstring>

namespace link_runtime {
struct HealthState {
    uint32_t player = 0;
    int hp = 0;
    int max_hp = 0;
    bool eligible = false;     // in play as Link (pl_type 0): every Link-only feature reads this
    uint8_t character = 0;     // pl_type (GLOBAL_WK+0x4FC8 low byte): 0 Leon/Link, 1 Ashley/Zelda
    bool in_play = false;      // the same in-play gates without the character (Zelda's light ray, zelda_light_runtime.h)
};

// UHD GLOBAL_WK offsets and MSB-first flag masks from re4_tweaks SDK/GameFlags.
// This interface only reads memory; it cannot write health or flags.
template <class Memory>
bool ReadHealth(Memory& memory, uint32_t player_slot, uint32_t global_slot, HealthState& result) {
    result = {};
    uint32_t player = 0, global = 0;
    if (!memory.Read32(player_slot, player) || !IsPointer(player) ||
        !memory.Read32(global_slot, global) || !IsPointer(global)) return false;
    uint32_t routine = 0, system = 0, stop = 0, health = 0, character = 0, status[4] = {};
    if (!memory.Read32(global + 0x20, routine) || !memory.Read32(global + 0x54, system) ||
        !memory.Read32(global + 0x170, stop) || !memory.Read32(global + 0x4FB4, health) ||
        !memory.Read32(global + 0x4FC8, character)) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!memory.Read32(global + 0x501C + i * 4, status[i])) return false;
    uint32_t current_player = 0, current_global = 0;
    if (!memory.Read32(player_slot, current_player) || current_player != player ||
        !memory.Read32(global_slot, current_global) || current_global != global) return false;
    int hp = static_cast<int16_t>(health & 0xFFFF), maximum = static_cast<int16_t>(health >> 16);
    if (maximum <= 0 || hp < 0 || hp > maximum) return false;
    // SYS_EXCEPTION/SOFT_RESET/DATA_READ/DOORDEMO/TRANS_STOP/CONTINUE/
    // SSCRN_EXEC/SCREEN_STOP/LOAD_GAME. Ordinary rendering and difficulty bits are allowed.
    // SYS_ROOMJUMP (0x01000000) remains set during normal play after room entry;
    // MainLoop/STOP/data-read/trans-stop/loading gates detect the active transition.
    constexpr uint32_t system_block = 0x20000000U | 0x04000000U | 0x02000000U |
        0x00200000U | 0x00100000U | 0x00080000U | 0x00008000U | 0x00000400U | 0x00000100U;
    constexpr uint32_t stop_block = 0x80000000U | 0x10000000U | 0x02000000U;  // KEY/PL/CTRL
    constexpr uint32_t status0_block = 0x10000000U | 0x08000000U | 0x04000000U |
        0x00100000U | 0x00040000U | 0x00001000U | 0x00000080U;  // movie/cut/death/case/event/blackout
    constexpr uint32_t status1_block = 0x10000000U | 0x00000002U;  // suspend/item-get
    constexpr uint32_t status2_block = 0x04000000U | 0x00080000U | 0x00040000U | 0x00008000U;
    constexpr uint32_t status3_block = 0x00008000U | 0x00004000U | 0x00002000U | 0x00001000U | 0x00000800U;
    const bool in_play = hp > 0 && (routine & 0xFF) == 3 &&
        !(system & system_block) && !(stop & stop_block) && !(status[0] & status0_block) &&
        !(status[1] & status1_block) && !(status[2] & status2_block) && !(status[3] & status3_block);
    result = {player, hp, maximum, in_play && (character & 0xFF) == 0, static_cast<uint8_t>(character & 0xFF), in_play};
    return true;
}

// Zelda's light ray: the player is Ashley (reskinned as Zelda) and in play.
inline bool ZeldaInPlay(const HealthState& s) { return s.in_play && s.character == 1; }

struct AlertClock {
    uint32_t player = 0;
    uint64_t last = 0;
    bool active = false;
    bool Due(const HealthState& state, bool enabled, int threshold, unsigned interval, uint64_t now) {
        if (!enabled || !state.eligible || state.hp > threshold) { active = false; player = 0; return false; }
        if (!active || player != state.player || now - last >= interval) {
            active = true; player = state.player; last = now; return true;
        }
        return false;
    }
};

struct WaveData {
    uint32_t rate = 0;
    unsigned channels = 0;
    std::vector<unsigned char> pcm;
};

inline uint32_t Little32(const unsigned char* value) {
    return uint32_t(value[0]) | (uint32_t(value[1]) << 8) | (uint32_t(value[2]) << 16) | (uint32_t(value[3]) << 24);
}
inline unsigned Little16(const unsigned char* value) { return value[0] | (unsigned(value[1]) << 8); }

inline void ScaleWave(WaveData& wave, unsigned volume) {
    if (volume > 100) volume = 100;
    for (size_t pos = 0; pos + 1 < wave.pcm.size(); pos += 2) {
        int16_t value = 0;
        std::memcpy(&value, wave.pcm.data() + pos, 2);
        value = static_cast<int16_t>(int(value) * static_cast<int>(volume) / 100);
        std::memcpy(wave.pcm.data() + pos, &value, 2);
    }
}

inline bool ParseWave(const std::vector<unsigned char>& wav, WaveData& result) {
    result = {};
    if (wav.size() < 12 || std::memcmp(wav.data(), "RIFF", 4) ||
        std::memcmp(wav.data() + 8, "WAVE", 4)) return false;
    size_t end = size_t(Little32(wav.data() + 4)) + 8;
    if (end != wav.size()) return false;
    bool format = false, data = false;
    unsigned block = 0;
    for (size_t pos = 12; pos < end;) {
        if (end - pos < 8) return false;
        size_t length = Little32(wav.data() + pos + 4), body = pos + 8;
        if (length > end - body) return false;
        if (!std::memcmp(wav.data() + pos, "fmt ", 4)) {
            if (format || length < 16) return false;
            const auto* f = wav.data() + body;
            result.channels = Little16(f + 2); result.rate = Little32(f + 4); block = Little16(f + 12);
            if (Little16(f) != 1 || Little16(f + 14) != 16 || result.channels < 1 || result.channels > 2 ||
                result.rate < 8000 || result.rate > 96000 || block != result.channels * 2 ||
                Little32(f + 8) != result.rate * block) return false;
            format = true;
        } else if (!std::memcmp(wav.data() + pos, "data", 4)) {
            if (data || !length) return false;
            result.pcm.assign(wav.begin() + body, wav.begin() + body + length); data = true;
        }
        pos = body + length + (length & 1);
        if (pos > end) return false;
    }
    return format && data && result.pcm.size() % block == 0 && result.pcm.size() <= result.rate * block * 10;
}
}  // namespace link_runtime

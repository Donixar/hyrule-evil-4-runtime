#include "../../dll/link_runtime_core.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

struct Memory {
    std::unordered_map<uint32_t, uint32_t> values;
    unsigned writes = 0;
    unsigned slot_reads = 0;
    bool replace_player = false;
    bool Read32(uint32_t address, uint32_t& value) {
        if (address == 0x10000 && ++slot_reads == 2 && replace_player)
            values[address] = 0x90000;
        auto found = values.find(address);
        if (found == values.end()) return false;
        value = found->second;
        return true;
    }
    bool Compare32(uint32_t address, uint32_t expected, uint32_t value) {
        auto found = values.find(address);
        if (found == values.end() || found->second != expected) return false;
        found->second = value;
        ++writes;
        return true;
    }
};

static Memory Fixture(uint32_t knife_flags, uint32_t back_flags) {
    Memory memory;
    memory.values[0x10000] = 0x20000;
    memory.values[0x2015C] = 0x30000;
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t node = 0x30000 + i * 0x100;
        memory.values[node + 0x14] = i == 5 ? 0 : node + 0x100;
    }
    memory.values[0x30204] = knife_flags;
    memory.values[0x30504] = back_flags;
    return memory;
}

static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    {
        auto m = Fixture(9, 0x149);
        auto result = link_runtime::SwordStep(m, 0x10000);
        Require(result == link_runtime::StepResult::Changed, "draw hides back sword");
        Require(m.values[0x30504] == 0x141, "only visible bit changes");
        Require(m.values[0x30204] == 9, "knife flags untouched");
    }
    {
        auto m = Fixture(1, 0x141);
        Require(link_runtime::SwordStep(m, 0x10000) == link_runtime::StepResult::Changed,
                "sheathing restores back sword");
        Require(m.values[0x30504] == 0x149, "back sword visible");
        Require(link_runtime::SwordStep(m, 0x10000) == link_runtime::StepResult::Unchanged,
                "stable state does not rewrite memory");
        Require(m.writes == 1, "one update only");
    }
    {
        auto m = Fixture(9, 9);
        Require(link_runtime::SwordStep(m, 0x10000) == link_runtime::StepResult::NotLink,
                "hair mark guard rejects other model set");
        Require(m.writes == 0, "unrecognized costume untouched");
    }
    {
        auto m = Fixture(9, 0x149);
        m.values[0x10000] = 0;
        Require(link_runtime::SwordStep(m, 0x10000) == link_runtime::StepResult::NoPlayer,
                "title without player is skipped");
        Require(m.writes == 0, "title writes nothing");
    }
    {
        auto m = Fixture(9, 0x149);
        m.values[0x30514] = 0x30000;
        Require(link_runtime::SwordStep(m, 0x10000) == link_runtime::StepResult::InvalidMemory,
                "cyclic list is rejected");
        Require(m.writes == 0, "cyclic list writes nothing");
    }
    {
        auto m = Fixture(9, 0x149);
        m.values.erase(0x30204);
        Require(link_runtime::SwordStep(m, 0x10000) == link_runtime::StepResult::InvalidMemory,
                "unreadable knife is rejected");
        Require(m.writes == 0, "unreadable list writes nothing");
    }
    {
        auto m = Fixture(9, 0x149);
        m.replace_player = true;
        Require(link_runtime::SwordStep(m, 0x10000) == link_runtime::StepResult::InvalidMemory,
                "player replacement cancels stale update");
        Require(m.writes == 0, "replacement writes nothing");
    }
    {
        // Native Special2 snapshot: body, knife, head, face, hair/back, hands.
        auto m = Fixture(9, 9);
        m.values[0x30104] = 9;
        m.values[0x30404] = 0x49;
        Require(link_runtime::SwordStep(m, 0x10000, 4) == link_runtime::StepResult::Changed,
                "Special2 uses knife1/back4 while drawn");
        Require(m.values[0x30404] == 0x41 && m.values[0x30504] == 9 && m.values[0x30204] == 9,
                "Special2 changes only its back visibility");
        m.values[0x30104] = 1;
        Require(link_runtime::SwordStep(m, 0x10000, 4) == link_runtime::StepResult::Changed &&
                m.values[0x30404] == 0x49, "Special2 sheath restores back4");
        m.values[0x30404] = 9;
        Require(link_runtime::SwordStep(m, 0x10000, 4) == link_runtime::StepResult::NotLink,
                "Special2 still requires hair mark");
    }
    {
        auto m = Fixture(1, 0x49);
        m.values[0x10004] = 0x40000;
        m.values[0x44FC8] = 4U << 8;
        unsigned costume = 0;
        Require(link_runtime::ReadSwordCostume(m, 0x10000, 0x10004, costume) && costume == 4,
                "Special2 costume is read from global without writes");
        Require(m.writes == 0, "costume lookup is read only");
        m.values[0x44FC8] |= 1;
        Require(!link_runtime::ReadSwordCostume(m, 0x10000, 0x10004, costume),
                "Ashley costume does not select Leon indices");
        m.values[0x44FC8] = 9U << 8;
        Require(!link_runtime::ReadSwordCostume(m, 0x10000, 0x10004, costume),
                "unknown Leon costume is rejected");
    }
    std::puts("9 sword core cases passed, including native Special2 layout");
}

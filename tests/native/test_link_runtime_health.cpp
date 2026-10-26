#include "../../dll/link_runtime_health.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Memory {
    std::unordered_map<uint32_t, uint32_t> values;
    unsigned reads = 0;
    bool replace_player = false;
    bool Read32(uint32_t address, uint32_t& value) {
        if (address == 0x10000 && ++reads == 2 && replace_player) values[address] = 0x40000;
        auto item = values.find(address);
        if (item == values.end()) return false;
        value = item->second;
        return true;
    }
};

static Memory Fixture() {
    Memory m;
    m.values[0x10000] = 0x20000;
    m.values[0x10004] = 0x30000;
    for (uint32_t offset : {0x20U, 0x54U, 0x170U, 0x4FB4U, 0x4FC8U,
                            0x501CU, 0x5020U, 0x5024U, 0x5028U}) m.values[0x30000 + offset] = 0;
    m.values[0x30020] = 3;
    m.values[0x34FB4] = (1200U << 16) | 240U;
    return m;
}

int main() {
    using namespace link_runtime;
    HealthState state;
    {
        auto m = Fixture();
        Require(ReadHealth(m, 0x10000, 0x10004, state), "valid Leon HP snapshot");
        Require(state.hp == 240 && state.max_hp == 1200 && state.eligible, "current and max HP decoded");
    }
    {
        // Fresh real UHD Special2 gameplay: ROOMJUMP stays latched after room entry.
        // It is not a live loading signal; the other routine/STOP/event gates still apply.
        auto m = Fixture();
        m.values[0x30054] = 0x01000804;
        m.values[0x3501C] = 0x03000110;
        m.values[0x35020] = 0x08000200;
        m.values[0x34FB4] = (1200U << 16) | 300U;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && state.eligible,
                "real UHD normal gameplay with latched ROOMJUMP is eligible");
        m.values[0x30020] = 4;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && !state.eligible, "room transition routine stays silent");
        m.values[0x30020] = 3; m.values[0x30054] |= 0x8000;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && !state.eligible, "inventory still silent with ROOMJUMP");
    }
    for (auto flag : {0x80000000U, 0x10000000U, 0x02000000U}) {
        auto m = Fixture(); m.values[0x30170] = flag;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && !state.eligible, "paused input/player/control gate");
    }
    for (auto offset : {0x501CU, 0x5020U, 0x5024U, 0x5028U}) {
        auto m = Fixture();
        m.values[0x30000 + offset] = offset == 0x501C ? 0x10000000U :
            offset == 0x5020 ? 0x10000000U : offset == 0x5024 ? 0x00040000U : 0x00002000U;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && !state.eligible, "movie/suspend/shop/loading gate");
    }
    for (auto flag : {0x8000U, 0x400U, 0x200000U}) {
        auto m = Fixture(); m.values[0x30054] = flag;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && !state.eligible, "case/pause/door gate");
    }
    {
        auto m = Fixture(); m.values[0x30020] = 0;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && !state.eligible, "title routine gate");
        m.values[0x30020] = 3; m.values[0x34FB4] &= 0xFFFF0000U;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && !state.eligible, "dead HP gate");
        m.values[0x34FB4] = (1200U << 16) | 1300U;
        Require(!ReadHealth(m, 0x10000, 0x10004, state), "impossible HP rejected");
    }
    {
        auto m = Fixture(); m.values[0x34FC8] = 1;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && !state.eligible, "Ashley not a Leon alert");
        Require(state.in_play && state.character == 1 && ZeldaInPlay(state), "Zelda light: Ashley in play");
        m.values[0x35024] = 0x00040000U;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && !state.in_play && !ZeldaInPlay(state), "Zelda light: the case blocks Ashley too");
        m.values[0x35024] = 0; m.values[0x34FC8] = 0;
        Require(ReadHealth(m, 0x10000, 0x10004, state) && state.eligible && state.in_play && !ZeldaInPlay(state), "Leon in play is not Zelda");
        m.values[0x10000] = 0;
        Require(!ReadHealth(m, 0x10000, 0x10004, state), "missing player rejected");
    }
    {
        auto m = Fixture();
        m.values.erase(0x35024);
        Require(!ReadHealth(m, 0x10000, 0x10004, state), "unreadable state rejected");
    }
    {
        auto m = Fixture(); m.replace_player = true;
        Require(!ReadHealth(m, 0x10000, 0x10004, state), "player swap cancels stale HP");
    }
    {
        AlertClock clock;
        state = {0x20000, 240, 1200, true};
        Require(!clock.Due(state, false, 360, 4000, 100), "off means silent");
        Require(clock.Due(state, true, 360, 4000, 101), "first low HP triggers");
        Require(!clock.Due(state, true, 360, 4000, 4100), "interval respected");
        Require(clock.Due(state, true, 360, 4000, 4101), "interval elapsed");
        state.eligible = false;
        Require(!clock.Due(state, true, 360, 4000, 4200), "pause cancels low alert state");
        state.eligible = true;
        Require(clock.Due(state, true, 360, 4000, 4300), "resume schedules fresh alert");
        state.hp = 800;
        Require(!clock.Due(state, true, 360, 4000, 4400), "healing cancels");
    }
    {
        AlertClock clock;
        state = {0x20000, 120, 1200, true};
        Require(clock.Due(state, true, 360, 4000, 0xFFFFFF00ULL), "large timer initial trigger");
        Require(clock.Due(state, true, 360, 4000, 0x100000F00ULL), "64 bit timer crosses 32 bit boundary");
    }
    {
        // RIFF PCM16 mono, 8 kHz, four samples. Unrelated chunks are legal.
        std::vector<unsigned char> wav = {
            'R','I','F','F',44,0,0,0,'W','A','V','E',
            'f','m','t',' ',16,0,0,0,1,0,1,0,0x40,0x1F,0,0,0x80,0x3E,0,0,2,0,16,0,
            'd','a','t','a',8,0,0,0,0,0,0,0,0,0,0,0};
        WaveData parsed;
        Require(ParseWave(wav, parsed) && parsed.rate == 8000 && parsed.channels == 1 && parsed.pcm.size() == 8,
                "valid PCM16 parsed");
        parsed.pcm = {0,128,255,127,0,0,128,0};
        ScaleWave(parsed, 35);
        Require(static_cast<int16_t>(Little16(parsed.pcm.data())) == -11468 &&
                static_cast<int16_t>(Little16(parsed.pcm.data() + 2)) == 11468,
                "volume scales positive and negative samples without unsigned wrap");
        ScaleWave(parsed, 0);
        for (auto sample : parsed.pcm) Require(sample == 0, "zero voice volume does not change device mixer");
        wav[20] = 3;
        Require(!ParseWave(wav, parsed), "float input rejected"); wav[20] = 1;
        wav[22] = 3;
        Require(!ParseWave(wav, parsed), "multichannel input rejected"); wav[22] = 1;
        wav[40] = 9;
        Require(!ParseWave(wav, parsed), "truncated data rejected"); wav[40] = 8;
        wav.pop_back();
        Require(!ParseWave(wav, parsed), "truncated RIFF rejected");
    }
    std::puts("health gates, cadence and WAV parser cases passed; mock memory is read only");
}

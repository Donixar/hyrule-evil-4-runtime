#include "../../dll/link_runtime_damage.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <unordered_map>
#include <vector>

static void Require(bool value, const char* why) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
struct Memory {
    std::unordered_map<uint32_t, uint32_t> values;
    bool Read32(uint32_t address, uint32_t& value) {
        auto found = values.find(address);
        if (found == values.end()) return false;
        value = found->second; return true;
    }
};
static Memory Fixture() {
    Memory m;
    m.values[0x10000] = 0x20000; m.values[0x10004] = 0x30000;
    for (uint32_t offset : {0x20U, 0x54U, 0x170U, 0x4FB4U, 0x4FC8U,
                            0x501CU, 0x5020U, 0x5024U, 0x5028U}) m.values[0x30000 + offset] = 0;
    m.values[0x30020] = 3; m.values[0x34FB4] = (1200U << 16) | 1200U;
    m.values[0x40100] = 0x10; m.values[0x40324] = (300U << 16) | 300U;
    m.values[0x4032C] = 0x10U << 16;
    return m;
}
static void Relative(std::vector<unsigned char>& image, size_t from, size_t to) {
    int32_t displacement = static_cast<int32_t>(to) - static_cast<int32_t>(from + 5);
    std::memcpy(image.data() + from + 1, &displacement, 4);
}
int main(int argc, char** argv) {
    using namespace link_runtime;
    auto memory = Fixture();
    DamageSnapshot before, after;
    Require(ReadDamageSnapshot(memory, 0x10000, 0x10004, 0x40000, true, before), "native enemy snapshot");
    after = before;
    Require(after.valid && after.gameplay && after.caller_verified && after.id == 0x10 && after.native_weapon == 0x10 &&
            after.hp == 300 && after.max_hp == 300, "snapshot fields decoded");
    for (auto address : {0x10000U, 0x10004U, 0x30020U, 0x34FC8U, 0x40100U, 0x40324U, 0x4032CU}) {
        auto invalid = memory; invalid.values.erase(address);
        Require(!ReadDamageSnapshot(invalid, 0x10000, 0x10004, 0x40000, true, after), "failed memory read cannot admit damage");
    }
    memory.values[0x34FC8] = 1;
    Require(ReadDamageSnapshot(memory, 0x10000, 0x10004, 0x40000, true, after) && !after.gameplay, "other character rejected");
    std::vector<unsigned char> image(512, 0x90);
    const unsigned char prefix[] = {0x8A,0x87,0x2E,0x03,0,0,0xBE,0x64,0,0,0,0x89,0x75,0xF4,
        0x3C,0x31,0x73,0x17,0x0F,0xB6,0x4D,0xFF,0x0F,0xB6,0xD0,0x51,0x52,0x57,0xE8};
    std::memcpy(image.data() + 16, prefix, sizeof(prefix));
    image[49] = 0x83; image[50] = 0xC4; image[51] = 0x0C;
    Relative(image, 44, 200); image[200] = 0xE9; Relative(image, 200, 400);
    Require(FindSwordCaller(image.data(), image.size(), 0, 128, 400) == 49, "same-object argument caller and thunk resolve");
    image[200] = 0x90;
    Require(!FindSwordCaller(image.data(), image.size(), 0, 128, 400), "wrong thunk rejects caller");
    image[200] = 0xE9; image[43] = 0x56;
    Require(!FindSwordCaller(image.data(), image.size(), 0, 128, 400), "different target register rejected");
    image[43] = 0x57; std::memcpy(image.data() + 70, image.data() + 16, 36);
    Relative(image, 98, 200);
    Require(!FindSwordCaller(image.data(), image.size(), 0, 128, 400), "ambiguous caller rejected");
    Require(!FindSwordCaller(image.data(), image.size(), 500, 128, 400), "outside image bounds rejected");
    const ExecutableRegion regions[] = {{0,64}, {70,110}};
    // One valid prefix in section1 plus two in section2 must not lose ambiguity.
    std::memcpy(image.data() + 110, image.data() + 16, 36); Relative(image, 138, 200);
    Require(!FindSwordExecutableCaller(image.data(), image.size(), 400, regions, 2),
            "duplicate prefix ambiguity is retained globally across sections");
    std::memset(image.data() + 70, 0x90, 110);
    Require(FindSwordExecutableCaller(image.data(), image.size(), 400, regions, 2) == 49,
            "sole valid caller inside executable region admitted");
    const ExecutableRegion elsewhere[] = {{70,110}};
    Require(!FindSwordExecutableCaller(image.data(), image.size(), 400, elsewhere, 1),
            "sole valid pattern outside executable regions rejected");
    if (argc == 2) {
        FILE* file = nullptr;
        Require(fopen_s(&file, argv[1], "rb") == 0 && file, "open prepared native PE memory image");
        std::fseek(file, 0, SEEK_END); const long bytes = std::ftell(file); std::rewind(file);
        Require(bytes > 0 && bytes < 64 * 1024 * 1024, "native memory image bounds");
        std::vector<unsigned char> native(static_cast<size_t>(bytes));
        Require(std::fread(native.data(), 1, native.size(), file) == native.size(), "native memory image read");
        std::fclose(file);
        Require(FindSwordCaller(native.data(), native.size(), 0, native.size(), 0x1B1A90) == 0x4AD87,
                "actual UHD bytes admit only native return RVA4AD87 through correct callee thunk");
        const size_t pe = Little32(native.data() + 0x3C);
        Require(pe + 24 < native.size(), "actual PE header bounds");
        const unsigned sections = Little16(native.data() + pe + 6);
        const size_t first = pe + 24 + Little16(native.data() + pe + 20);
        Require(sections <= 96 && first + size_t(sections) * 40 <= native.size(), "actual PE section table bounds");
        std::vector<ExecutableRegion> native_regions;
        for (unsigned s = 0; s < sections; ++s) {
            const auto* section = native.data() + first + s * 40;
            if (Little32(section + 36) & 0x20000000U)
                native_regions.push_back({Little32(section + 12), Little32(section + 8)});
        }
        Require(FindSwordExecutableCaller(native.data(), native.size(), 0x1B1A90,
                native_regions.data(), native_regions.size()) == 0x4AD87,
                "actual UHD global uniqueness and executable region gate passed");
        std::puts("actual UHD memory-image caller RVA4AD87 -> thunkCE14 -> callee1B1A90 passed");
    }
    std::puts("damage: snapshot/read failures/callee-qualified caller passed");
}

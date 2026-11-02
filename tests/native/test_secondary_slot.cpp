// slot-guard-01 (dll/secondary_slot.h): stance check, hand-back, watchdog and arm probe. Pure logic; with the pinned
// mapped image (argv[1], SHA bb59e550...) also the native facts the module relies on, byte for byte at the preferred
// base. Never opens the game process.
#include "../../dll/secondary_slot.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace ss = link_runtime::secondary_slot;
static unsigned checks = 0;
static void Check(bool ok, const char* why) { ++checks; if (!ok) { std::printf("FAIL: %s\n", why); std::exit(1); } }

struct Mem {
    std::map<uint32_t, uint32_t> words;
    std::map<uint32_t, std::vector<unsigned char>> blocks;
    bool fail = false;
    bool Read32(uint32_t a, uint32_t& v) { if (fail) return false; auto it = words.find(a); v = it == words.end() ? 0 : it->second; return true; }
    bool Read(uint32_t a, void* out, size_t n) {
        if (fail) return false;
        for (auto& [at, bytes] : blocks)
            if (a >= at && a + n <= at + bytes.size()) { std::memcpy(out, bytes.data() + (a - at), n); return true; }
        return false;
    }
};
constexpr uint32_t kBase = 0x540000, kPlayer = 0x2000000;
constexpr uint32_t kMot0 = 0x30000000, kMot1 = 0x30100000, kMot2 = 0x30200000;
static Mem LiveStance() {
    Mem m; const uint32_t at = kBase + ss::kMot3Rva;
    m.words[at + ss::mot3::model] = kPlayer; m.words[at + ss::mot3::mot0] = kMot0; m.words[at + ss::mot3::mot1] = kMot1;
    m.words[at + ss::mot3::mot2] = kMot2; m.words[at + ss::mot3::work] = kMot1; m.words[kPlayer + ss::kPrimaryMotion] = kMot0;
    return m;
}

static void TestStance() {
    Mem m = LiveStance();
    Check(ss::kMot3Rva + ss::mot3::work == ss::kStanceWorkRva, "stance work is cMot3+0x18");
    Check(ss::Check(ss::ReadStance(m, kBase, kPlayer), kPlayer) == ss::StanceState::Live, "knife stance live: model, clip mot1, primary mot0");
    m.words[kBase + ss::kMot3Rva + ss::mot3::work] = kMot2;
    Check(ss::Check(ss::ReadStance(m, kBase, kPlayer), kPlayer) == ss::StanceState::Live, "cMot3::move swapped to mot2: still live");
    m.words[kPlayer + ss::kPrimaryMotion] = 0x31000000;
    Check(ss::Check(ss::ReadStance(m, kBase, kPlayer), kPlayer) == ss::StanceState::PrimaryChanged, "primary is no longer mot0: stale");
    m = LiveStance(); m.words[kBase + ss::kMot3Rva + ss::mot3::work] = 0x32000000;
    Check(ss::Check(ss::ReadStance(m, kBase, kPlayer), kPlayer) == ss::StanceState::ForeignClip, "work clip neither mot1 nor mot2");
    m = LiveStance(); m.words[kBase + ss::kMot3Rva + ss::mot3::work] = 0;
    Check(ss::Check(ss::ReadStance(m, kBase, kPlayer), kPlayer) == ss::StanceState::NoClip, "work without a clip");
    m = LiveStance();
    Check(ss::Check(ss::ReadStance(m, kBase, kPlayer + 0x1000), kPlayer + 0x1000) == ss::StanceState::OtherModel, "mot3 set on another model");
    m.fail = true;
    Check(ss::Check(ss::ReadStance(m, kBase, kPlayer), kPlayer) == ss::StanceState::Unread, "unreadable: never live");
    Check(ss::Check(ss::ReadStance(m, 0, kPlayer), kPlayer) == ss::StanceState::Unread, "unknown image base: never live");
    for (auto s : {ss::StanceState::Live, ss::StanceState::Unread, ss::StanceState::OtherModel, ss::StanceState::NoClip,
                   ss::StanceState::ForeignClip, ss::StanceState::PrimaryChanged})
        Check(std::strlen(ss::Name(s)) > 2, "stance names");
}

static void TestClassifyAndHandBack() {
    const ss::Works works{0x70000000, 0x71000000};
    const uint32_t neck = kPlayer + ss::kNeckWork, stance = kBase + ss::kStanceWorkRva;
    Check(ss::Classify(0, kPlayer, kBase, works) == ss::Kind::Empty && ss::Classify(neck, kPlayer, kBase, works) == ss::Kind::Neck &&
          ss::Classify(stance, kPlayer, kBase, works) == ss::Kind::Stance && ss::Classify(works.reaction, kPlayer, kBase, works) == ss::Kind::Reaction &&
          ss::Classify(works.guard, kPlayer, kBase, works) == ss::Kind::Guard && ss::Classify(0x12345678, kPlayer, kBase, works) == ss::Kind::Foreign,
          "slot owners");
    Check(ss::Classify(stance, kPlayer, 0, works) == ss::Kind::Foreign, "no image base: the stance is not recognised");
    bool cleared = true;
    Check(ss::HandBack(0, kPlayer, kBase, false, 0, cleared) == 0 && !cleared, "nothing before: 0, not a clear");
    Check(ss::HandBack(neck, kPlayer, kBase, false, 0, cleared) == neck && !cleared, "neck always back");
    Check(ss::HandBack(stance, kPlayer, kBase, true, 0, cleared) == stance && !cleared, "live stance back");
    Check(ss::HandBack(stance, kPlayer, kBase, false, 0, cleared) == 0 && cleared, "stale stance: 0");
    Check(ss::HandBack(works.guard, kPlayer, kBase, false, works.guard, cleared) == works.guard && !cleared, "active guard back");
    Check(ss::HandBack(works.guard, kPlayer, kBase, true, 0, cleared) == 0 && cleared, "idle guard: 0");
    Check(ss::HandBack(neck, kPlayer + 4, kBase, true, 0, cleared) == 0 && cleared, "another player's neck: 0");
    Check(ss::HandBack(0x12345678, kPlayer, kBase, true, 0, cleared) == 0 && cleared, "unknown previous: 0");
}

static void TestWatchdog() {
    const ss::Works works{0x70000000, 0x71000000};
    const uint32_t stance = kBase + ss::kStanceWorkRva;
    auto watch = [&](uint32_t slot, uint32_t frame) {
        ss::Watch w; w.player = kPlayer; w.slot = slot; w.image_base = kBase; w.works = works; w.frame = frame; return w;
    };
    ss::Watchdog dog; ss::Verdict v{};
    Check(!dog.Release(watch(0, 1), v) && v == ss::Verdict::Owned, "empty slot");
    Check(!dog.Release(watch(kPlayer + ss::kNeckWork, 1), v) && v == ss::Verdict::Owned, "neck");
    ss::Watch live = watch(stance, 2); live.stance = ss::StanceState::Live;
    Check(!dog.Release(live, v) && v == ss::Verdict::Owned, "live stance kept");
    ss::Watch stale = watch(stance, 3); stale.stance = ss::StanceState::PrimaryChanged;
    Check(!dog.Release(stale, v) && v == ss::Verdict::StaleStance, "stale stance: not on its first sighting");
    Check(!dog.Release(stale, v), "nor on a second MotionMove of the same frame");
    stale.frame = 4;
    Check(dog.Release(stale, v) && v == ss::Verdict::StaleStance, "stale stance released on a later frame");
    dog.Released(v);
    stale.frame = 5;
    Check(!dog.Release(stale, v), "a new sighting starts over");
    live.frame = 6;
    Check(!dog.Release(live, v), "live again: kept");
    stale.frame = 7;
    Check(!dog.Release(stale, v), "a live frame in between resets the stale count");
    ss::Watch orphan = watch(works.reaction, 8);
    Check(dog.Release(orphan, v) && v == ss::Verdict::OrphanReaction, "reaction work with the reaction idle: released at once");
    dog.Released(v);
    orphan.reaction_active = true;
    Check(!dog.Release(orphan, v) && v == ss::Verdict::Owned, "active reaction kept");
    ss::Watch guard = watch(works.guard, 9);
    Check(dog.Release(guard, v) && v == ss::Verdict::OrphanGuard, "guard work with the guard idle: released");
    dog.Released(v);
    guard.guard_active = true;
    Check(!dog.Release(guard, v) && v == ss::Verdict::Owned, "active guard kept");
    Check(!dog.Release(watch(0x12345678, 10), v) && v == ss::Verdict::Foreign, "a work we cannot attribute is never touched");
    const auto& s = dog.stats();
    Check(s.releases == 3 && s.stale_stance == 1 && s.orphan_reaction == 1 && s.orphan_guard == 1 && s.foreign == 1, "watchdog counters");
    for (auto x : {ss::Verdict::Owned, ss::Verdict::StaleStance, ss::Verdict::OrphanReaction, ss::Verdict::OrphanGuard, ss::Verdict::Foreign})
        Check(std::strlen(ss::Name(x)) > 4, "verdict names");
    for (auto k : {ss::Kind::Empty, ss::Kind::Neck, ss::Kind::Stance, ss::Kind::Reaction, ss::Kind::Guard, ss::Kind::Foreign})
        Check(std::strlen(ss::Name(k)) > 3, "kind names");
}

// Link at the origin: chest at y 1300, arm parts 300..900 to the side, unit axes.
static void Pose(ss::arm::Part (&p)[ss::arm::kLast + 1]) {
    for (unsigned i = 0; i <= ss::arm::kLast; ++i) {
        float* m = p[i].mat;
        std::memset(m, 0, sizeof(p[i].mat));
        m[0] = m[5] = m[10] = 1.f;
        m[3] = i == ss::arm::kChest ? 0.f : 60.f * i; m[7] = 1300.f; m[11] = 0.f;
        p[i].scale[0] = p[i].scale[1] = p[i].scale[2] = 1.f; p[i].flags = 0x10010000;
    }
}
static void TestArmProbe() {
    namespace a = ss::arm;
    a::Part parts[a::kLast + 1]{};
    Pose(parts);
    Check(a::Judge(parts).health == a::Health::Ok, "healthy arms");
    Pose(parts); parts[14].scale[1] = NAN;
    a::Report r = a::Judge(parts);
    Check(r.health == a::Health::NonFinite && r.part == 14, "NaN scale on the left forearm");
    Pose(parts); parts[9].mat[0] = parts[9].mat[4] = parts[9].mat[8] = 0.f; parts[9].scale[0] = 0.f;
    r = a::Judge(parts);
    Check(r.health == a::Health::Collapsed && r.part == 9 && r.value < 0.05f && r.scale[0] == 0.f && r.flags == 0x10010000, "collapsed right hand");
    Pose(parts); parts[13].mat[5] = 50.f;
    r = a::Judge(parts);
    Check(r.health == a::Health::Huge && r.part == 13, "huge axis");
    Pose(parts); parts[16].mat[3] = 9000.f;
    r = a::Judge(parts);
    Check(r.health == a::Health::Detached && r.part == 16 && r.value > 3000.f, "left hand left behind in the world");
    Pose(parts); parts[a::kChest].mat[7] = INFINITY;
    Check(a::Judge(parts).health == a::Health::NonFinite && a::Judge(parts).part == a::kChest, "chest itself broken");
    Pose(parts); parts[6].mat[0] = 0.f; parts[6].mat[4] = 0.f; parts[6].mat[8] = 0.f;   // part 6 is not an arm bone
    Check(a::Judge(parts).health == a::Health::Ok, "non-arm parts are not judged");
    for (auto h : {a::Health::Ok, a::Health::Unread, a::Health::NonFinite, a::Health::Collapsed, a::Health::Huge, a::Health::Detached})
        Check(std::strlen(a::Name(h)) > 1, "health names");
    // ReadParts walks the parts chain (model+0xF4, part+0xF4).
    Mem m;
    const uint32_t model = 0x4000000, first = 0x4100000;
    std::vector<unsigned char> model_bytes(0x100, 0);
    std::memcpy(model_bytes.data() + a::kFirstPart, &first, 4);
    m.blocks[model] = model_bytes;
    Pose(parts);
    for (unsigned i = 0; i <= a::kLast; ++i) {
        std::vector<unsigned char> b(0x1D8, 0);
        std::memcpy(b.data() + a::kMatrix, parts[i].mat, 48); std::memcpy(b.data() + a::kScale, parts[i].scale, 12);
        std::memcpy(b.data() + a::kFlags, &parts[i].flags, 4);
        const uint32_t next = first + (i + 1) * 0x1D8;
        std::memcpy(b.data() + a::kNextPart, &next, 4);
        m.blocks[first + i * 0x1D8] = b;
    }
    a::Part read[a::kLast + 1]{};
    Check(a::ReadParts(m, model, read) && a::Judge(read).health == a::Health::Ok && read[9].mat[3] == 540.f, "parts read along the chain");
    m.blocks.erase(first + 10 * 0x1D8);
    Check(!a::ReadParts(m, model, read), "a broken chain is unread, not judged");
    Check(!a::ReadParts(m, 0, read), "no model");
}

// The native facts behind the stance check, at the preferred base of the pinned mapped image.
static void Pins(const char* path) {
    FILE* f = nullptr;
    Check(!fopen_s(&f, path, "rb") && f, "open pinned image");
    std::fseek(f, 0, SEEK_END);
    std::vector<unsigned char> img(static_cast<size_t>(std::ftell(f)));
    std::rewind(f);
    Check(std::fread(img.data(), 1, img.size(), f) == img.size(), "read pinned image");
    std::fclose(f);
    struct Pin { uint32_t rva; const char* hex; const char* what; };
    const Pin pins[] = {
        {0x2611BC, "f74644000000208945b8894df466894df88955ec668955f075068988a8020000",
         "MotionSetCore: 2A8 cleared unless the work being set has Mot_flag bit 29"},
        {0x27E1A9, "8956100fb755286a008955088b551c894e0c", "cMot3::set stores mot2 at +0x10 and mot1 at +0xC"},
        {0x27E1D2, "8d97d80100005257893e894608e866cfd8ff8b06", "cMot3::set: primary = mot0 on model+0x1D8, model at +0, mot0 at +8"},
        {0x27E1FF, "8d5e18db4528d91c246a04516a00525350e8", "cMot3::set0: the work is cMot3+0x18"},
        {0x27CE00, "558bec83ec08568bf18b0e85c90f84f900000083b9a802000000", "cMot3::move does nothing while model+2A8 is 0"},
        {0x27CEFE, "8b068b88a8020000d999c8000000", "cMot3::move writes |rate| into [model+2A8]+C8, whoever owns it"},
        {0x3537AF, "b9e8a7c700", "knife_r3_ready00: mot3 is bio4+0x87A7E8"},
    };
    for (const auto& pin : pins) {
        const size_t n = std::strlen(pin.hex) / 2;
        Check(pin.rva + n <= img.size(), "pin inside the image");
        bool same = true;
        for (size_t i = 0; i < n; ++i) {
            const char digits[3] = {pin.hex[2 * i], pin.hex[2 * i + 1], 0};
            same = same && img[pin.rva + i] == std::strtoul(digits, nullptr, 16);
        }
        if (!same) std::printf("pin mismatch: %s\n", pin.what);
        Check(same, pin.what);
    }
    std::printf("secondary slot: %zu native pins match\n", sizeof(pins) / sizeof(pins[0]));
}

int main(int argc, char** argv) {
    Check(argc <= 2, "optional pinned mapped image");
    TestStance();
    TestClassifyAndHandBack();
    TestWatchdog();
    TestArmProbe();
    if (argc == 2) Pins(argv[1]);
    std::printf("secondary slot: %u checks passed%s\n", checks, argc == 2 ? "" : " (no image: native pins skipped)");
    return 0;
}

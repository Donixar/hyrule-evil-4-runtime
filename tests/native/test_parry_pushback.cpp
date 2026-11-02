// parry-heavy-01 (31-oct): heavy parry pushback (dll/parry_pushback.h). Keys, the power threshold, the direction away
// from the hit point (and the facing fallback), the eased slide at 60 and 165 FPS, restart, player change and Apply.
// Synthetic data only: no process, no hooks, no game files.
#include "../../dll/parry_pushback.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>

namespace pb = link_runtime::parry::pushback;
static unsigned checks;
static void Check(bool ok, const char* why) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); } }
static bool Near(double a, double b, double e = 1e-3) { return std::fabs(a - b) <= e; }
using Ini = std::map<std::wstring, std::wstring>;
static auto Getter(const Ini& ini) {
    return [&ini](const wchar_t* key, wchar_t* out, size_t n) {
        out[0] = 0; const auto it = ini.find(key);
        if (it != ini.end()) wcsncpy_s(out, n, it->second.c_str(), _TRUNCATE);
    };
}

static void Keys() {
    unsigned p = 0;
    auto c = pb::ReadConfig(Getter({}), p);
    Check(p == 0 && c.heavy_power == 1000 && Near(c.distance, 700) && Near(c.frames, 14), "defaults");
    c = pb::ReadConfig(Getter({{L"ParryHeavyPower", L"1500"}, {L"ParryHeavyPushback", L"0"}, {L"ParryHeavyFrames", L"20"}}), p);
    Check(p == 0 && c.heavy_power == 1500 && c.distance == 0.f && Near(c.frames, 20), "explicit values");
    Check(!pb::Heavy(c, 5000), "distance 0 = off");
    p = 0; c = pb::ReadConfig(Getter({{L"ParryHeavyPower", L"0"}, {L"ParryHeavyPushback", L"x"}, {L"ParryHeavyFrames", L"99"}}), p);
    Check(p == 3 && c.heavy_power == 1000 && Near(c.distance, 700) && Near(c.frames, 14), "bad values: defaults, counted");
}

static void Threshold() {
    const pb::Config c;
    Check(!pb::Heavy(c, 800), "Krauser's 800 is not heavy");
    Check(pb::Heavy(c, 1000) && pb::Heavy(c, 1200) && pb::Heavy(c, 9998), "1000 and above are heavy");
}

static void Directions() {
    float ux = 0, uz = 0;
    const float hit[3] = {0, 0, 0}, link[3] = {300, 50, 400};
    Check(pb::Direction(hit, link, 0.f, ux, uz) && Near(ux, 0.6) && Near(uz, 0.8), "away from the hit point, XZ only");
    const float same[3] = {300, 0, 400};
    Check(pb::Direction(same, link, 0.f, ux, uz) && Near(ux, 0) && Near(uz, -1), "hit at Link: backward from the facing (yaw 0)");
    Check(pb::Direction(same, link, 1.5707963f, ux, uz) && Near(ux, -1) && Near(uz, 0, 1e-3), "facing +X: backward is -X");
    Check(!pb::Direction(same, link, NAN, ux, uz), "no hit direction and no yaw: no slide");
}

static double Run(float step) {
    pb::Slide s; const pb::Config c;
    Check(s.Start(c, 0x1000, 0.f, -1.f), "start");
    double total = 0, first = -1; float dx = 0, dz = 0; int ticks = 0;
    while (s.Active() && ticks < 1000) { s.Step(0x1000, step, dx, dz); if (first < 0) first = -dz; total += -dz; ++ticks; Check(dx == 0.f, "straight"); }
    Check(!s.Active() && s.stop() == pb::Stop::Finished, "finished");
    Check(first > 0 && Near(total, 700, 0.01), "whole distance travelled");
    return ticks * double(step);
}

static void Slides() {
    Check(Near(Run(1.f), 14, 1e-6), "60 FPS: 14 frames");
    Check(Near(Run(60.f / 165.f), 14, 0.5), "165 FPS: same game time");
    pb::Slide s; const pb::Config c; float dx = 0, dz = 0;
    s.Start(c, 0x1000, 1.f, 0.f); s.Step(0x1000, 1.f, dx, dz);
    const float early = dx; s.Step(0x1000, 1.f, dx, dz);
    Check(early > dx && dx > 0, "eases out: faster at the start");
    s.Start(c, 0x1000, 0.f, 1.f); s.Step(0x1000, 1.f, dx, dz);
    Check(dx == 0.f && dz > 0 && Near(s.moved(), dz), "a new block restarts the slide from where Link is");
    Check(!s.Step(0x2000, 1.f, dx, dz) && !s.Active() && s.stop() == pb::Stop::Player, "another player: ends without a move");
    pb::Config off; off.distance = 0;
    Check(!s.Start(off, 0x1000, 1.f, 0.f) && !s.Active(), "off: never starts");
    s.Start(c, 0x1000, 1.f, 0.f); s.Abort(pb::Stop::Damaged);
    Check(!s.Active() && s.stop() == pb::Stop::Damaged && !s.Step(0x1000, 1.f, dx, dz), "abort");
}

struct Mem {
    unsigned char bytes[0x200]{}; bool writable = true, flip = false; unsigned reads = 0;
    bool Read(uint32_t a, void* out, size_t n) { if (flip && ++reads == 2) bytes[a - 0x1000] ^= 1; std::memcpy(out, bytes + (a - 0x1000), n); return true; }
    bool Writable(uint32_t, size_t) { return writable; }
    bool Write(uint32_t a, const void* in, size_t n) { std::memcpy(bytes + (a - 0x1000), in, n); return true; }
    float F(uint32_t off) { float v; std::memcpy(&v, bytes + off, 4); return v; }
    void Set(uint32_t off, float v) { std::memcpy(bytes + off, &v, 4); }
};

static void Writes() {
    Mem m; m.Set(0x94, 10); m.Set(0x98, 20); m.Set(0x9C, 30);
    Check(pb::Apply(m, 0x1000, 5.f, -3.f) && Near(m.F(0x94), 15) && Near(m.F(0x98), 20) && Near(m.F(0x9C), 27), "adds X/Z, keeps Y");
    Check(pb::Apply(m, 0x1000, 0.f, 0.f), "nothing to move: no write, ok");
    Mem r; r.writable = false; Check(!pb::Apply(r, 0x1000, 1.f, 1.f), "not writable: fails");
    Mem f; f.flip = true; Check(!pb::Apply(f, 0x1000, 1.f, 1.f), "position changed between reads: fails");
    Check(!pb::Apply(m, 0x1000, NAN, 0.f) && !pb::Apply(m, 0, 1.f, 0.f), "bad input: fails");
}

int main() {
    Keys(); Threshold(); Directions(); Slides(); Writes();
    std::printf("parry_pushback: %u checks passed\n", checks);
    return 0;
}

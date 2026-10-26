#pragma once
// Time-based sword trail core (docs/estela-espada-diseno-2026-10-20.md). Pure: no game access.
// Units: Sample.t, `now` and Style.length_ms are game time in MILLISECONDS, so the visual length
// of the trail is the same at 30/60/120/165 FPS. Ring keeps at most 64 samples; Expire drops
// those older than length_ms. A "cut" marks a gap: the sample is never joined to the next one.
// Quad layout: v[0]=old inner, v[1]=old tip, v[2]=new tip, v[3]=new inner; a0/a1 = alpha at
// the old / new edge (linear in age: Style.alpha at age 0, 0 at age length_ms).
#include <cmath>
#include <cstdint>

namespace sword_trail {
struct Vec3 { double x, y, z; };
struct Sample { double t; Vec3 base, tip; bool cut; };
struct Style { double length_ms; double inner; uint32_t rgb; uint8_t alpha; };
struct Quad { Vec3 v[4]; uint8_t a0, a1; };

constexpr int kCapacity = 64;
constexpr int kMaxQuadsPerPair = 128;
constexpr double kMaxStepDeg = 6.0;   // subdivide until each step turns less than this

inline bool Finite(const Vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline Vec3 Lerp(const Vec3& a, const Vec3& b, double u) {
    return Vec3{a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u};
}
// Uniform Catmull-Rom between p1 and p2.
inline Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double u) {
    const double u2 = u * u, u3 = u2 * u;
    auto c = [&](double a, double b, double cc, double d) {
        return 0.5 * ((2 * b) + (-a + cc) * u + (2 * a - 5 * b + 4 * cc - d) * u2 + (-a + 3 * b - 3 * cc + d) * u3);
    };
    return Vec3{c(p0.x, p1.x, p2.x, p3.x), c(p0.y, p1.y, p2.y, p3.y), c(p0.z, p1.z, p2.z, p3.z)};
}

class Ring {
    Sample s_[kCapacity] = {};
    int head_ = 0, count_ = 0;   // head_ = index of the oldest sample
public:
    void Reset() { head_ = 0; count_ = 0; }
    int Count() const { return count_; }
    // i = 0 is the oldest, Count()-1 the newest.
    const Sample& At(int i) const { return s_[(head_ + i) % kCapacity]; }
    void Push(double t, Vec3 base, Vec3 tip) {
        if (!std::isfinite(t) || !Finite(base) || !Finite(tip)) { Cut(t); return; }
        if (count_ == kCapacity) { head_ = (head_ + 1) % kCapacity; --count_; }
        s_[(head_ + count_) % kCapacity] = Sample{t, base, tip, false};
        ++count_;
    }
    // Ends the strip at the newest sample; the next Push starts a new one.
    void Cut(double) { if (count_) s_[(head_ + count_ - 1) % kCapacity].cut = true; }
    void Expire(double now, double length_ms) {
        while (count_ && now - s_[head_].t > length_ms) { head_ = (head_ + 1) % kCapacity; --count_; }
    }
};

inline uint8_t AlphaAt(double age, const Style& st) {
    if (!(st.length_ms > 0.0)) return 0;
    double f = 1.0 - age / st.length_ms;
    f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    return uint8_t(std::lround(f * st.alpha));
}

// Segments needed to keep each step under kMaxStepDeg, from the angle between blade directions.
inline int Subdivisions(const Sample& a, const Sample& b) {
    const Vec3 da{a.tip.x - a.base.x, a.tip.y - a.base.y, a.tip.z - a.base.z};
    const Vec3 db{b.tip.x - b.base.x, b.tip.y - b.base.y, b.tip.z - b.base.z};
    const double la = std::sqrt(da.x * da.x + da.y * da.y + da.z * da.z);
    const double lb = std::sqrt(db.x * db.x + db.y * db.y + db.z * db.z);
    if (!(la > 1e-9) || !(lb > 1e-9)) return 1;
    double c = (da.x * db.x + da.y * db.y + da.z * db.z) / (la * lb);
    c = c < -1.0 ? -1.0 : (c > 1.0 ? 1.0 : c);
    const double deg = std::acos(c) * 180.0 / 3.14159265358979323846;
    int n = int(std::ceil(deg / kMaxStepDeg));
    return n < 1 ? 1 : (n > kMaxQuadsPerPair ? kMaxQuadsPerPair : n);
}

// Quads the strip has in all (BuildStrip without a cap).
inline int StripQuads(const Ring& r) {
    int n = 0;
    for (int i = 0; i + 1 < r.Count(); ++i)
        if (!r.At(i).cut) n += Subdivisions(r.At(i), r.At(i + 1));
    return n;
}

// Writes up to max_quads quads, oldest first; returns how many. Never joins across a cut. Over the cap the OLDEST
// quads are dropped, so the newest part (right behind the blade) is always drawn.
inline int BuildStrip(const Ring& r, double now, const Style& st, Quad* out, int max_quads) {
    if (max_quads <= 0) return 0;
    const int total = StripQuads(r);
    int skip = total > max_quads ? total - max_quads : 0;
    int n = 0;
    for (int i = 0; i + 1 < r.Count() && n < max_quads; ++i) {
        const Sample& a = r.At(i);
        const Sample& b = r.At(i + 1);
        if (a.cut) continue;
        const int steps = Subdivisions(a, b);
        if (skip >= steps) { skip -= steps; continue; }
        const bool has_prev = i > 0 && !r.At(i - 1).cut;
        const bool has_next = i + 2 < r.Count() && !b.cut;
        const Sample& pa = has_prev ? r.At(i - 1) : a;
        const Sample& nb = has_next ? r.At(i + 2) : b;
        Vec3 old_base = a.base, old_tip = a.tip;
        double old_t = a.t;
        for (int k = 1; k <= steps && n < max_quads; ++k) {
            const double u = double(k) / steps;
            const Vec3 nbase = k == steps ? b.base : CatmullRom(pa.base, a.base, b.base, nb.base, u);
            const Vec3 ntip = k == steps ? b.tip : CatmullRom(pa.tip, a.tip, b.tip, nb.tip, u);
            const double nt = a.t + (b.t - a.t) * u;
            if (skip > 0) { --skip; old_base = nbase; old_tip = ntip; old_t = nt; continue; }
            Quad& q = out[n++];
            q.v[0] = Lerp(old_base, old_tip, st.inner);
            q.v[1] = old_tip;
            q.v[2] = ntip;
            q.v[3] = Lerp(nbase, ntip, st.inner);
            q.a0 = AlphaAt(now - old_t, st);
            q.a1 = AlphaAt(now - nt, st);
            old_base = nbase; old_tip = ntip; old_t = nt;
        }
    }
    return n;
}
}  // namespace sword_trail

#pragma once

namespace link_runtime {
// This observer always calls the original exactly once with the original arguments.
// Its return is never scaled, clamped or replaced, including non-knife attacks.
template<class Original, class Observer, class Target>
int ObserveDamage(Original original, Observer observer, Target target, unsigned weapon, int proximity) {
    const int result = original(target, weapon, proximity);
    observer(target, weapon, proximity, result);
    return result;
}

inline unsigned TraceLimit(unsigned value) {
    return value >= 1 && value <= 5000 ? value : 500;
}
}

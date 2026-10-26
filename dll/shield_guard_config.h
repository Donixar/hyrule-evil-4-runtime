// dll/shield_guard_config.h
#pragma once
// ShieldGuard INI values (shield_guard.h), apart so combat_runtime.h stays free of windows.h.
namespace link_runtime::shield_guard {
struct Config {
    bool enabled = false;
    unsigned blocks = 5, regen_seconds = 45;
};
constexpr unsigned kMinBlocks = 1, kMaxBlocks = 99, kMinRegen = 1, kMaxRegen = 600;
}  // namespace link_runtime::shield_guard

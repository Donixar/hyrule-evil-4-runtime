#pragma once
#include <windows.h>
#include <cstdint>

// Runtime log policy for players (public package, 16-oct-2026): the [LinkRuntime] LogLevel switch,
// which lines each level writes, the startup size cap of Bin32/link_runtime.log and basenames for
// every logged path. The decisions are pure functions; PrepareFile is the only file operation and
// runs once on the worker, before the log handle opens and before any hook exists.
namespace link_runtime::log {
// 0 = no log file at all; 1 = startup, errors and feature events (default); 2 = also the periodic
// stats and the parry/shield/Mortal Draw observer samples.
constexpr unsigned kOff = 0, kNormal = 1, kVerbose = 2;
// A log larger than this at startup is kept as link_runtime.old.log and a fresh one is started.
constexpr uint64_t kRotateBytes = 16ull * 1024 * 1024;

// Exact "0", "1" or "2" (GetPrivateProfileString already trims spaces). Anything else, a missing
// key included, keeps the default 1, so a typo never silences or floods the log. Returns whether
// the text was one of the three exact values.
inline bool ParseLevel(const wchar_t* text, unsigned& level) {
    level = kNormal;
    if (!text || !text[0] || text[1]) return false;
    if (text[0] == L'0') level = kOff;
    else if (text[0] == L'1') level = kNormal;
    else if (text[0] == L'2') level = kVerbose;
    else return false;
    return true;
}
// Whether a line of line_level is written at the configured level. Level 0 writes nothing.
inline bool Writes(unsigned level, unsigned line_level) {
    return line_level != kOff && level >= line_level;
}
// Startup only: rotate when the existing log is strictly larger than the cap.
inline bool ShouldRotate(bool exists, uint64_t bytes) {
    return exists && bytes > kRotateBytes;
}
// A log that had to rotate but could not be renamed (another program holds it without delete
// sharing, or holds the old log) is truncated instead, so the cap still holds.
inline bool TruncateAfterMove(bool rotate, bool moved) {
    return rotate && !moved;
}
// The part after the last '\' or '/' (the whole text when there is none): logged paths never
// carry the player's folders.
template <class Char> inline const Char* BaseName(const Char* path) {
    if (!path) return path;
    const Char* name = path;
    for (const Char* c = path; *c; ++c)
        if (*c == Char('\\') || *c == Char('/')) name = c + 1;
    return name;
}

struct Prepared {
    bool existed = false, rotated = false, truncated = false;
    uint64_t previous_bytes = 0;
};
// Startup step before the append handle opens. Renames an oversized log to old_path, replacing any
// previous old log; nothing is deleted otherwise. Small or missing logs are left as they are.
inline Prepared PrepareFile(const wchar_t* log_path, const wchar_t* old_path) {
    Prepared p;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    p.existed = GetFileAttributesExW(log_path, GetFileExInfoStandard, &data) != 0 &&
        !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    if (p.existed) p.previous_bytes = (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    const bool rotate = ShouldRotate(p.existed, p.previous_bytes);
    if (!rotate) return p;
    p.rotated = MoveFileExW(log_path, old_path, MOVEFILE_REPLACE_EXISTING) != 0;
    if (TruncateAfterMove(rotate, p.rotated)) {
        HANDLE file = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) { p.truncated = true; CloseHandle(file); }
    }
    return p;
}
}

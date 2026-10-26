#include "../../dll/link_runtime_log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

// Offline units for dll/link_runtime_log.h: LogLevel parsing and gating, the startup rotation
// decision, basenames, and PrepareFile on real files inside the given work directory (never Bin32).
namespace lg = link_runtime::log;

static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static wchar_t g_dir[MAX_PATH], g_log[MAX_PATH], g_old[MAX_PATH];

// A file of exactly `bytes` whose first bytes are `marker`.
static void MakeFile(const wchar_t* path, uint64_t bytes, const char* marker) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "fixture file created");
    const DWORD length = static_cast<DWORD>(std::strlen(marker));
    DWORD written = 0;
    Require(length <= bytes, "marker fits the fixture size");
    Require(WriteFile(file, marker, length, &written, nullptr) && written == length, "fixture marker written");
    LARGE_INTEGER end{}; end.QuadPart = static_cast<LONGLONG>(bytes);
    Require(SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file), "fixture size set");
    CloseHandle(file);
}
static bool Exists(const wchar_t* path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}
static uint64_t Size(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return ~0ull;
    return (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}
static bool StartsWith(const wchar_t* path, const char* marker) {
    char text[64] = {};
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD read = 0;
    const bool ok = ReadFile(file, text, sizeof(text) - 1, &read, nullptr) != 0;
    CloseHandle(file);
    return ok && std::strncmp(text, marker, std::strlen(marker)) == 0;
}
static void Clean() {
    DeleteFileW(g_log);
    DeleteFileW(g_old);
}

int wmain(int argc, wchar_t** argv) {
    // ---- LogLevel: exact 0/1/2, anything else keeps 1 ------------------------------------------
    {
        unsigned level = 99;
        Require(lg::ParseLevel(L"0", level) && level == lg::kOff, "0 = off");
        Require(lg::ParseLevel(L"1", level) && level == lg::kNormal, "1 = normal");
        Require(lg::ParseLevel(L"2", level) && level == lg::kVerbose, "2 = verbose");
        const wchar_t* invalid[] = {L"", L"3", L"9", L"10", L"01", L"-1", L"a", L" 1", L"1 ", L"on"};
        for (const wchar_t* text : invalid) {
            level = 99;
            Require(!lg::ParseLevel(text, level) && level == lg::kNormal, "invalid or missing value keeps 1");
        }
        level = 99;
        Require(!lg::ParseLevel(nullptr, level) && level == lg::kNormal, "null keeps 1");
    }
    // ---- gating: level 0 writes nothing, 1 normal lines, 2 everything -----------------------------
    {
        Require(!lg::Writes(lg::kOff, lg::kNormal) && !lg::Writes(lg::kOff, lg::kVerbose), "level 0 writes nothing");
        Require(lg::Writes(lg::kNormal, lg::kNormal), "level 1 writes startup/error/event lines");
        Require(!lg::Writes(lg::kNormal, lg::kVerbose), "level 1 drops stats and observer samples");
        Require(lg::Writes(lg::kVerbose, lg::kNormal) && lg::Writes(lg::kVerbose, lg::kVerbose), "level 2 writes both");
        for (unsigned level = 0; level <= 2; ++level)
            Require(!lg::Writes(level, lg::kOff), "a line of level 0 is never written");
    }
    // ---- startup rotation decision ---------------------------------------------------------------
    {
        Require(lg::kRotateBytes == 16777216ull, "cap is 16 MB");
        Require(!lg::ShouldRotate(false, 0) && !lg::ShouldRotate(false, ~0ull), "missing log never rotates");
        Require(!lg::ShouldRotate(true, 0) && !lg::ShouldRotate(true, 1000), "small log appends");
        Require(!lg::ShouldRotate(true, lg::kRotateBytes), "exactly the cap appends");
        Require(lg::ShouldRotate(true, lg::kRotateBytes + 1), "one byte over the cap rotates");
        Require(lg::ShouldRotate(true, 334183632ull), "the 334 MB developer log rotates");
        Require(!lg::TruncateAfterMove(false, false) && !lg::TruncateAfterMove(false, true), "no rotation, no truncation");
        Require(!lg::TruncateAfterMove(true, true), "a renamed log is not truncated");
        Require(lg::TruncateAfterMove(true, false), "a log that cannot be renamed is truncated");
    }
    // ---- basenames for logged paths ---------------------------------------------------------------
    {
        Require(!std::wcscmp(lg::BaseName(L"C:\\Games\\RE4\\Bin32\\dinput8.dll"), L"dinput8.dll"), "backslash path");
        Require(!std::wcscmp(lg::BaseName(L"D:/Steam/steamapps/common/Resident Evil 4/Bin32/winmm.ini"), L"winmm.ini"), "slash path with spaces");
        Require(!std::wcscmp(lg::BaseName(L"C:\\a/b\\link_runtime.ini"), L"link_runtime.ini"), "mixed separators");
        Require(!std::wcscmp(lg::BaseName(L"bio4.exe"), L"bio4.exe"), "bare name unchanged");
        Require(!std::wcscmp(lg::BaseName(L"C:\\dir\\"), L""), "trailing separator gives empty name");
        Require(!std::wcscmp(lg::BaseName(L""), L""), "empty stays empty");
        Require(lg::BaseName(static_cast<const wchar_t*>(nullptr)) == nullptr, "null stays null");
        Require(!std::strcmp(lg::BaseName("E:\\x\\y.log"), "y.log"), "narrow paths too");
    }
    // ---- PrepareFile on real files (work directory from the runner) ------------------------------
    if (argc != 2) { std::fputs("usage: test_link_runtime_log <work-dir>\n", stderr); return 2; }
    Require(GetFullPathNameW(argv[1], MAX_PATH, g_dir, nullptr) != 0, "work directory path");
    Require(wcscat_s(g_dir, L"\\log-rotation-test") == 0, "test folder path");
    CreateDirectoryW(g_dir, nullptr);
    Require(swprintf_s(g_log, L"%ls\\link_runtime.log", g_dir) > 0 && swprintf_s(g_old, L"%ls\\link_runtime.old.log", g_dir) > 0,
            "fixture paths");
    Clean();
    {
        const auto p = lg::PrepareFile(g_log, g_old);
        Require(!p.existed && !p.rotated && !p.truncated && p.previous_bytes == 0, "missing log: nothing to do");
        Require(!Exists(g_log) && !Exists(g_old), "missing log: no file created, no old log");
    }
    {
        MakeFile(g_log, 100, "small");
        const auto p = lg::PrepareFile(g_log, g_old);
        Require(p.existed && !p.rotated && !p.truncated && p.previous_bytes == 100, "small log kept for append");
        Require(Size(g_log) == 100 && StartsWith(g_log, "small") && !Exists(g_old), "small log untouched");
    }
    {
        MakeFile(g_log, lg::kRotateBytes, "at-cap");
        const auto p = lg::PrepareFile(g_log, g_old);
        Require(p.existed && !p.rotated && !p.truncated && p.previous_bytes == lg::kRotateBytes, "log at the cap kept");
        Require(Size(g_log) == lg::kRotateBytes && !Exists(g_old), "log at the cap untouched");
    }
    {
        MakeFile(g_old, 10, "stale-old");
        MakeFile(g_log, lg::kRotateBytes + 1, "oversized");
        const auto p = lg::PrepareFile(g_log, g_old);
        Require(p.existed && p.rotated && !p.truncated && p.previous_bytes == lg::kRotateBytes + 1, "oversized log rotated");
        Require(!Exists(g_log), "the log name is free for a fresh log");
        Require(Size(g_old) == lg::kRotateBytes + 1 && StartsWith(g_old, "oversized"), "previous old log replaced by the oversized one");
    }
    {
        // Another program holds the log without delete sharing: rename fails, the log is truncated.
        Clean();
        MakeFile(g_old, 10, "keep-old");
        MakeFile(g_log, lg::kRotateBytes + 5, "held");
        HANDLE held = CreateFileW(g_log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(held != INVALID_HANDLE_VALUE, "holder opened");
        const auto p = lg::PrepareFile(g_log, g_old);
        CloseHandle(held);
        Require(p.existed && !p.rotated && p.truncated, "held log truncated instead of renamed");
        Require(Size(g_log) == 0 && Size(g_old) == 10 && StartsWith(g_old, "keep-old"), "held log emptied, old log kept");
    }
    {
        // Held without write sharing either: nothing can be done, the log is left as it is.
        Clean();
        MakeFile(g_log, lg::kRotateBytes + 7, "locked");
        HANDLE held = CreateFileW(g_log, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(held != INVALID_HANDLE_VALUE, "locker opened");
        const auto p = lg::PrepareFile(g_log, g_old);
        CloseHandle(held);
        Require(p.existed && !p.rotated && !p.truncated, "locked log neither renamed nor truncated");
        Require(Size(g_log) == lg::kRotateBytes + 7 && !Exists(g_old), "locked log untouched");
    }
    Clean();
    RemoveDirectoryW(g_dir);
    std::puts("link_runtime_log: level parse/gating, rotation decision, basenames and PrepareFile passed");
    return 0;
}

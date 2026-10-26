#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include "../../dll/link_runtime_log.h"

using CreateInput = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    wchar_t log_path[MAX_PATH] = {};
    if (!GetFullPathNameW(argv[1], MAX_PATH, log_path, nullptr)) return 1;
    wchar_t* name = std::wcsrchr(log_path, L'\\');
    if (!name) return 1;
    wcscpy_s(name + 1, MAX_PATH - static_cast<size_t>(name + 1 - log_path), L"link_runtime.log");
    DeleteFileW(log_path);  // This host test receives a build directory, never Bin32.
    // Startup rotation (link_runtime_log.h): a log one byte over the cap must become
    // link_runtime.old.log before the worker writes its first line into a fresh log.
    wchar_t old_path[MAX_PATH] = {};
    wcscpy_s(old_path, log_path);
    wchar_t* old_name = std::wcsrchr(old_path, L'\\');
    wcscpy_s(old_name + 1, MAX_PATH - static_cast<size_t>(old_name + 1 - old_path), L"link_runtime.old.log");
    DeleteFileW(old_path);
    const unsigned long long oversized = link_runtime::log::kRotateBytes + 1;
    {
        HANDLE file = CreateFileW(log_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) { std::fputs("oversized log fixture failed\n", stderr); return 1; }
        static const char marker[] = "previous session marker\r\n";
        DWORD written = 0; LARGE_INTEGER end{}; end.QuadPart = static_cast<LONGLONG>(oversized);
        const bool ok = WriteFile(file, marker, sizeof(marker) - 1, &written, nullptr) &&
            SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file);
        CloseHandle(file);
        if (!ok) { std::fputs("oversized log fixture failed\n", stderr); return 1; }
    }
    HMODULE proxy = LoadLibraryW(argv[1]);
    if (!proxy) { std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError()); return 1; }
#pragma warning(push)
#pragma warning(disable: 4191)
    auto create = reinterpret_cast<CreateInput>(GetProcAddress(proxy, "DirectInput8Create"));
#pragma warning(pop)
    if (!create) { std::fputs("undecorated DirectInput8Create export missing\n", stderr); return 1; }
    // Loading the proxy must not start provider loading or native hooks before
    // the host has successfully initialized DirectInput (Steam may hold the loader lock).
    auto read_log = [&]() {
        static char text[8192]; text[0] = 0;
        HANDLE file = CreateFileW(log_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD count = 0;
            if (ReadFile(file, text, sizeof(text) - 1, &count, nullptr)) text[count] = 0;
            CloseHandle(file);
        }
        return text;
    };
    const ULONGLONG startup_deadline = GetTickCount64() + 5000;
    while (!std::strstr(read_log(), "startup waiting for DirectInput") && GetTickCount64() < startup_deadline)
        Sleep(20);
    if (!std::strstr(read_log(), "startup waiting for DirectInput")) {
        std::fputs("startup gate missing before DirectInput\n", stderr); return 1;
    }
    {
        WIN32_FILE_ATTRIBUTE_DATA old_data{};
        const bool old_ok = GetFileAttributesExW(old_path, GetFileExInfoStandard, &old_data) != 0 &&
            ((static_cast<unsigned long long>(old_data.nFileSizeHigh) << 32) | old_data.nFileSizeLow) == oversized;
        if (!old_ok || std::strstr(read_log(), "previous session marker") ||
            !std::strstr(read_log(), "rotated=1 truncated=0")) {
            std::fputs("oversized log was not rotated to link_runtime.old.log at startup\n", stderr); return 1;
        }
        DeleteFileW(old_path);
    }
    IDirectInput8W* invalid = nullptr;
    if (SUCCEEDED(create(GetModuleHandleW(nullptr), 0, IID_IDirectInput8W,
                         reinterpret_cast<void**>(&invalid), nullptr))) return 1;
    Sleep(100);
    if (std::strstr(read_log(), "startup DirectInput ready") || std::strstr(read_log(), "shield session")) {
        std::fputs("failed DirectInput creation released startup\n", stderr); return 1;
    }
    IDirectInput8W* input = nullptr;
    HRESULT result = create(GetModuleHandleW(nullptr), 0x0800, IID_IDirectInput8W,
                            reinterpret_cast<void**>(&input), nullptr);
    if (FAILED(result) || !input) {
        std::fprintf(stderr, "system forwarding failed: %#lx\n", static_cast<unsigned long>(result)); return 1;
    }
    input->Release();
    // DllMain starts an asynchronous worker. INI/cache/disk timing can exceed
    // 100 ms, so wait for the actual guard, never assume a fixed delay proves it.
    bool logged = false;
    const ULONGLONG deadline = GetTickCount64() + 5000;
    do {
        HANDLE log = CreateFileW(log_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log != INVALID_HANDLE_VALUE) {
            char output[4096] = {}; DWORD read = 0;
            logged = ReadFile(log, output, sizeof(output) - 1, &read, nullptr) &&
                read && std::strstr(output, "unsupported host; forwarding only");
            CloseHandle(log);
        }
        if (!logged) Sleep(20);
    } while (!logged && GetTickCount64() < deadline);
    if (!logged) { std::fputs("live proxy guard log missing\n", stderr); return 1; }
    if (!std::strstr(read_log(), "startup DirectInput ready")) return 1;
    std::puts("proxy load + real DirectInput8Create forwarding passed");
    return 0;
}

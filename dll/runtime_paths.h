#pragma once
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cwchar>

// File paths remain bounded by the caller's buffer. Unlike the secure CRT's
// default overflow path, these operations reject a long name without invoking
// the invalid-parameter handler (which would terminate the game). A rejected
// path is empty, so no caller can accidentally open a truncated filename.
namespace link_runtime::paths {
inline int Copy(wchar_t* out, size_t count, const wchar_t* source) {
    if (!out || !count) return EINVAL;
    if (!source) { out[0] = 0; return EINVAL; }
    size_t length = 0;
    while (length < count && source[length]) ++length;
    if (length == count) { out[0] = 0; return ERANGE; }
    std::wmemmove(out, source, length + 1);
    return 0;
}
template<size_t N> int Copy(wchar_t (&out)[N], const wchar_t* source) { return Copy(out, N, source); }
template<size_t N> int Format(wchar_t (&out)[N], const wchar_t* format, ...) {
    if (!format) { out[0] = 0; return -1; }
    va_list args; va_start(args, format);
    const int result = _vsnwprintf_s(out, N, _TRUNCATE, format, args);
    va_end(args);
    if (result < 0) out[0] = 0;
    return result;
}
}

#pragma once
// Generated from spin_ending_assets.py; immutable resident files, offline candidate.
#include <cstddef>
namespace link_runtime::sword_actions::assets {
struct Clip {const wchar_t* name;size_t fcv_size,seq_size;const char* fcv_sha;const char* seq_sha;const char* voice_sha;unsigned strike_tick;const wchar_t* voice=nullptr;};   // voice: the link_<voice>_voice.wav stem when shared (else name)
inline constexpr Clip spin_attack{L"spin_attack",770080,224,
    "7d7ad2063101f86eaa5fb40536dbb7d409bbe8257cd35e7c42defbf2cdab6b41","2c296efc9f125c8a956ba25b7fdf6a0204cfbfbceb4b14e396fd1325eaf673f0",
    "6a7445cee73fd9df5539ec9fe9aca66dc7ae26706a4b8df106163e52edfccb29",16};
inline constexpr Clip ending_blow{L"ending_blow",737376,192,
    "42f3e4e26704a50e71ba2f11df5173cb75c3ceee936d04270469fd795597c3a5","c745a7cfeed80038f22b7f85df1c5585f76298410acf2193f00665567bbb51f3",
    "fe2e48f7d4a44dc6881c11025620958655249a6b488c3b95b94fa7335a962b37",60};
inline constexpr Clip shield_bash{L"shield_bash",283296,96,
    "b356aa32c248adb3f7e7b6b34f62f5b9fdcc2666384c7d905a0465e103346083","62611cd1e72259a546d673fbfd1ec38cfb4e0bf2e9b738a1f2a5fba2d92d7c8c",
    "af5136f118b666429e99e0c0a72fb703804ba3db23566ac496d840030a2f8ba7",11};
inline constexpr Clip back_slice_right{L"back_slice_right",835488,224,
    "1e03f3ef92a2393654856ea1aa85414b97e6acd8770b5d14c797c448c1093a47","7d3812aeee4bf2c613a1f77b5b4fcab45027d75c6eb9fb4e1c05b3a321eeff42",
    "83d175fbd8c37a4a5c262c81564af392889f4e72a50e728931c66aec3294ab30",49,L"back_slice"};
inline constexpr Clip back_slice_left{L"back_slice_left",835488,224,
    "bcd0e859632db55fceeb492e7efbf5a9b65862e8f34b5740719be04304bc3a56","7d3812aeee4bf2c613a1f77b5b4fcab45027d75c6eb9fb4e1c05b3a321eeff42",
    "83d175fbd8c37a4a5c262c81564af392889f4e72a50e728931c66aec3294ab30",49,L"back_slice"};
inline constexpr unsigned back_slice_arc_ticks=40;
inline constexpr Clip backflip{L"backflip",459392,128,
    "f890317dc07f0ff6e3fd1971e69db428057faf7b5728e0c549ab95c0d6dc9751","7f8c62c99ec01f4a6ca00d2c60147a6ec4479222a5c3442b540b3fc7813253c6",
    "233ee80255126c8023a63295e057116040e575e6fc4e9ac873c099c0b2cd277e",0};
inline constexpr unsigned backflip_travel_ticks=26;
inline constexpr unsigned backflip_land_ticks=30;
}

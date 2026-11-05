#pragma once
// Generated from scripts/sword_lock_anim.py (Blade Lock H2a); immutable resident files, offline candidate.
#include <cstddef>
namespace link_runtime::sword_lock::assets {
struct Clip {const wchar_t* name;size_t fcv_size,seq_size;const char* fcv_sha;const char* seq_sha;unsigned tp_frames;bool loop;};
inline constexpr Clip a{L"lock_a",673504,192,
    "47f7ea478184073f97ae0f02037c86ba71ddc8baee52f329c6355a8039bc2373","5c6b4e390dc107a6d0d54fb081523a530093600ef17f4c062fa51b058ab68d58",40,true};
inline constexpr Clip b{L"lock_b",673504,192,
    "d76c0850ed1e95ba3e97bc660c75ea7956e2d5eedd30dccda9483e6b18d4b438","5c6b4e390dc107a6d0d54fb081523a530093600ef17f4c062fa51b058ab68d58",40,true};
inline constexpr Clip c{L"lock_c",673504,192,
    "947adff26c4f79c07a43b30a054248b834fd623c018b8dabb2e3cdb7e22b5375","5c6b4e390dc107a6d0d54fb081523a530093600ef17f4c062fa51b058ab68d58",40,true};
inline constexpr Clip win{L"lock_win",656704,192,
    "185ef8eaa96c36969e591dd7d35548fc3069849c83d92cccae54b1b209d57192","5c6b4e390dc107a6d0d54fb081523a530093600ef17f4c062fa51b058ab68d58",39,false};
inline constexpr Clip lose{L"lock_lose",623104,320,
    "9f5396046e3431e5efe2667f4b5f45bc4d1753894c746dd1235df7b66bf93e82","ee0ac9433a16ab65fb68cf59f872705eaf0bccb17aa361d813802d297fadfdf9",74,false};
// lose: Z travel of TP's ankle midpoint (mm, negative = back) at each 60 Hz game frame, for the carry.
inline constexpr float lose_travel_mm[149]={0.0f,0.2f,0.1f,-0.2f,-0.5f,-0.5f,-0.6f,-0.4f,-0.4f,-0.2f,0.2f,0.1f,0.3f,0.1f,-0.1f,-0.3f,-0.4f,-0.5f,0.1f,0.2f,-0.2f,-0.6f,-1.0f,-0.9f,-0.3f,1.6f,0.4f,-2.4f,-7.2f,-12.9f,-22.6f,-38.4f,-77.7f,-139.4f,-214.1f,-299.4f,-388.0f,-477.3f,-563.9f,-639.3f,-700.5f,-737.7f,-756.1f,-758.4f,-771.5f,-802.9f,-860.8f,-938.0f,-1025.5f,-1157.7f,-1280.9f,-1290.6f,-1279.9f,-1321.4f,-1394.4f,-1504.0f,-1607.7f,-1676.5f,-1723.6f,-1731.8f,-1724.5f,-1724.4f,-1724.5f,-1724.2f,-1723.8f,-1723.6f,-1723.6f,-1723.4f,-1723.1f,-1723.1f,-1723.3f,-1723.7f,-1724.0f,-1724.3f,-1724.9f,-1724.9f,-1724.4f,-1724.0f,-1723.6f,-1723.0f,-1722.9f,-1722.9f,-1722.8f,-1722.7f,-1722.5f,-1722.4f,-1722.5f,-1723.0f,-1723.2f,-1723.8f,-1723.4f,-1723.8f,-1723.6f,-1721.3f,-1716.0f,-1707.2f,-1695.5f,-1681.6f,-1665.6f,-1648.6f,-1631.6f,-1614.2f,-1596.0f,-1581.2f,-1570.4f,-1566.3f,-1559.4f,-1543.6f,-1517.2f,-1473.4f,-1424.0f,-1375.7f,-1331.8f,-1305.0f,-1289.6f,-1287.1f,-1289.6f,-1290.0f,-1290.0f,-1289.9f,-1290.5f,-1290.6f,-1290.8f,-1290.6f,-1291.1f,-1290.7f,-1290.6f,-1290.5f,-1291.1f,-1290.8f,-1291.0f,-1291.1f,-1290.8f,-1290.5f,-1290.1f,-1289.9f,-1289.5f,-1289.6f,-1289.0f,-1289.2f,-1289.1f,-1289.5f,-1289.4f,-1289.4f,-1289.7f,-1289.7f,-1290.1f,-1290.0f,-1290.2f};
}

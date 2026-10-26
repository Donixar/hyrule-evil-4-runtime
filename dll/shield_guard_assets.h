// dll/shield_guard_assets.h
#pragma once
// Generated from scripts/shield_guard_anim.py, shield_guard_assets.py and shield_guard_sounds.py (shield-guard-01, bodies -02,
// clips sword-hand-01: part 16 as the native arm IK effector target).
// Every file is read from Bin32 and checked by size and SHA-256 before use; a mismatch turns the piece off (logged).
#include <cstdint>

namespace link_runtime::shield_guard::assets {
struct File {const wchar_t* name;uint32_t size;const char* sha;};
struct ClipFiles {File fcv,seq;};
// Clip order: shield_guard.h Clip (Guard, Hit, Break).
inline constexpr ClipFiles kClips[3]={
    {{L"link_shield_guard.fcv",71392u,"d4275aff86ac6e30d6675e320f669936daacda0905835117a412bbd729dd800e"},
     {L"link_shield_guard.seq",64u,"e34c5442db0e8be8307f95d2b62c8286b810c6a8731dd398035eb4e3b98b54dd"}},
    {{L"link_shield_guard_hit.fcv",111712u,"a35905e993ecac7bbc262de63661271e3037a15874fe2ce7099ff8d924404c85"},
     {L"link_shield_guard_hit.seq",64u,"b993c4a35148f508b3f9c932afcbb881872128909e284908de38911836cf4ee4"}},
    {{L"link_shield_guard_break.fcv",353632u,"ea5312a2214074d517470bd78f99558d61d98d57c331c0f19e0d6f37f9595d3e"},
     {L"link_shield_guard_break.seq",160u,"99a8714b6adaf711d507112a107abc11cdb103feaea25f39df40f8253c295d4c"}},
};
// Per costume 0..3: the installed body print (shield_guard.h FingerprintParts: combined, the five header words with
// 0x40 as an offset, the positions FNV; the parts name the differing field in the refusal log), the hand and none
// variants. shield-guard-02: hand variants rebuilt with the TP weaponR guard placement (scripts/shield_guard_assets.py).
struct Body {unsigned costume;uint32_t fingerprint;uint32_t header[5];uint32_t positions;File hand,none;};
inline constexpr Body kBodies[4]={
    {0,0xBC79E38Au,{0x000B776Fu,0x006F0000u,0x107F107Fu,0x20030818u,0x00000B60u},0x482E0F82u,{L"link_shield_guard_pl00_hand.bin",149280u,"20f63cdf0d9ffc36e0dd4edbf170ca67f987fe7a9dedf593c02e35e13ae9d57d"},{L"link_shield_guard_pl00_none.bin",136704u,"9d9e8a5065ec17e82c8aeda1a6fbabb57b9c8a959023376fce1e198d1695809e"}},
    {1,0x6E9E0CACu,{0x000D7772u,0x00720000u,0x27212721u,0x20030818u,0x00000B70u},0x482E0F82u,{L"link_shield_guard_pl08_hand.bin",347696u,"784ad1b495a7641f07ada557b88974e4c032753fcafe149dc77fe76a11b77ce5"},{L"link_shield_guard_pl08_none.bin",165872u,"bf5d558c5b416a3dabffbabb5a8e72942144cda5568d2c98ce294d1b539eef86"}},
    {2,0xBEAD9B02u,{0x000A7753u,0x00530000u,0x2D942D94u,0x20030818u,0x00000A80u},0xD957600Cu,{L"link_shield_guard_pl09_hand.bin",403312u,"d77ce3065a3b328aca2a182d1fe97265f99583aee551bb2bc28f294301d89aaf"},{L"link_shield_guard_pl09_none.bin",221504u,"5431cc1fccdea634bda6f7057ec7e2ca1575a9159b4885d17576684754156f96"}},
    {3,0x515ED069u,{0x000D7772u,0x00720000u,0x29BD29BDu,0x20030818u,0x00000B70u},0x0A0962A3u,{L"link_shield_guard_pl10_hand.bin",369968u,"b5fcb1af1224cc2432710703e98b52f6c73a65f6886ce515931956a864b6e440"},{L"link_shield_guard_pl10_none.bin",188160u,"4fef6c00d8e553bcfdaa91500a48d591f6360fabed419da28ac494b2116dc428"}},
};
// Break (wood: costume 0, metal: 1..3) and return chime. The block reuses link_rear_shield_wood/metal.wav.
inline constexpr File kBreakWood={L"link_shield_guard_break_wood.wav",145552u,"d9384f644417241f2ca5217cd0a84745156f5ffaf73c2e796f8d629cd3e55e8b"};
inline constexpr File kBreakMetal={L"link_shield_guard_break_metal.wav",65546u,"da4b2b175aad6ece11d086c3bb01cce668d8e95476e341eb82dbf5ff9cc9292f"};
inline constexpr File kBack={L"link_shield_guard_back.wav",15850u,"433bc95ac3c1741305b2f6387691ff05a1c7216f6a7d8b868e09c8372258ace5"};
inline constexpr const char* kRearWoodSha="c758da4574dfa7b5a08b16d1cce23a14c7d4eb8b83f6122cb6864a9d3de62b92";
inline constexpr const char* kRearMetalSha="7d620ab3350a14be9f8ed5f72769d6383bc89ac199388860ea4880217624ea69";
}  // namespace link_runtime::shield_guard::assets

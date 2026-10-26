// dll/native_pins.h
#pragma once
// Exact bytes of the native entries the ocarina and combat runtimes hook or call, as they are in
// the pinned bio4.exe (SHA 19aed4af...) at its preferred base 0x400000. Absolute operands inside
// a pin (mov ecx,0xC70FD4; jmp [eax*4+0x707D34]; the cookie 0xC35E10...) are compared after
// image_relocation.h undoes the loader's fixups, so the pins stay exact at any image base.
#include "image_relocation.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace link_runtime::native_pins {
struct Pin { uint32_t rva; const char* hex; };
// Ocarina item 0x0D (ocarina_item.h): Use call site, cItemMgr::get, search and use bodies.
inline constexpr Pin kOcarinaItem[] = {
    {0x371DD5, "8b1500b5c70052b9d40fc700e8b533c9ff3c010f850d010000"},
    {0x307A80, "558bec83ec10538b5d080fb7c383c08f568bf183f8080f8713010000ff2485347d7000"},
    {0x305E80, "558bec5356578bf98b771c8b471433d285f67e2bb9010000008da4240000000084480474118a58053a5f137509668b5d08"},
    {0x306C90, "558bec83ec0856578b7d088bf185ff750a5f32c05e8be55dc20400530fb71f8d45f85053e85b48d0ff0fb645fa83c408480f842a020000"}};
// Hand mount (ocarina_mount.h): calcModelAddr, calcTplAddr, getBoundingBox.
inline constexpr Pin kOcarinaMount[] = {
    {0x25CB50, "558bec568b75088b4620a9000010000f858100000001760c017610013601761401761c0176300176340d00001000894620"},
    {0x25CCF0, "558bec8b450885c0744c833800744701400833c9c70000000000394804"},
    {0x25D120, "558bec83ec20d90518c5b1008b45088b4830d955f0d955f8560fb77038d95d08"}};
// Weapon hide during the ocarina pose (ocarina_weapon.h): cPlayer::setNoSuspend (Wep +7D8 ->
// +34/+38), cObjWep::setDisp, the per-frame display check in cObjWep::move, interrupt (levels
// 1 and 2 only), the constructor's flag = 0x1C, and ModelTrans skipping models without bit 2.
inline constexpr Pin kOcarinaWeapon[] = {
    {0x27BF10, "558bec56578b7d088bf185ff7409814e0400080000eb07816604fff7ffff8b86d807000083783400740b8b48348b018b501457ffd28bb6d8070000837e3800740b8b4e388b018b501457ffd25f5e5dc20400"},
    {0x33E9B0, "558bec807d0c018b4508752c83e800741c48740e487549808954030000105dc20800808954030000085dc20800808954030000045dc2080083e800741c48740e48751d80a154030000ef5dc2080080a154030000f75dc2080080a154030000fb"},
    {0x33F36A, "8a8654030000a8047431a808742da81074298b864c03000085c074198b4004a802741885c07414a13c6fc000f74058000000407506834e0402eb04836604fd"},
    {0x33F0B0, "568bf1808e5403000018834e0402"},
    {0x33F1A3, "899e4c030000899e58030000889e54030000808e540300001c"},
    {0x2EB60B, "8b4604a8020f8499040000a8040f8491040000"}};
// Combat: arbiter, MotionMove, neck, secondary init, publisher, their thunks and the calc call.
inline constexpr Pin kCombat[] = {
    {0x18AB40, "578bf9c605b510c50000c605b710c50051c6474200a13c6fc000f78070010000"},
    {0x262B90, "558bec81ec38010000a1105ec30033c58945fc538b5d088b83a8020000565789"},
    {0x27E6E0, "558bec5153568bf10fb6460e33db2bc30f844803000083e8020f843b03000083"},
    {0x2611A0, "558bec83ec5ca1105ec30033c58945fc8b450853568b750c33c933d2f7464400"},
    {0x1AEEF0, "558bec8a4508d9ee0c01568bf18a4d0cd91ed945188846048a4510884e058b4d"},
    {0x6BF9, "e992bf2500"}, {0x4836, "e9a59e2700"}, {0x3224, "e9c7bc1a00"},
    {0x4AD82, "e88d20fcff83c40c8bf0"}};
// Ocarina combat songs (ocarina_song_effects.h, 17-oct): only the damage publisher and its thunk (same bytes
// as in kCombat), so Sheik and Epona do not depend on the hooks the combat runtime patches.
inline constexpr Pin kOcarinaSongStrike[] = {
    {0x1AEEF0, "558bec8a4508d9ee0c01568bf18a4d0cd91ed945188846048a4510884e058b4d"}, {0x3224, "e9c7bc1a00"}};
// Fairy revive (fairy_revive.h): LifeDownSet2 head/tail (skipping re4_tweaks 0x1B5B40/0x1B5B8C and the
// ShieldPreClamp site 0x1B5C57), thunk 0x71C1, EmAtkHitCk/EmAtkSetDamagePL frames and flag-8 tails,
// the zeroing caller 0x11A354, cItemMgr::use case 0x05, healing (to_whom), search and the two thunks.
inline constexpr Pin kFairyRevive[] = {
    // LifeDownSet2: prologue, RNG spread, player branch (+0x100), early "HP <= 0 -> 0".
    {0x1B5AF0, "558bec535657e83fbce4ff0fb6f0c1e608e834bce4ff8b4d108b7d0c0fb6c00bc685c9740d998d3409f7fe2bd103fa897d0c8b75088a9e0001000084db0f85a8010000a13c6fc0006683b8b44f000000"},
    // Clamp, subtract, keep-1, saturate 0, Ashley-player restore, return (s16)HP in EAX.
    {0x1B5C5E, "3bc77d028bf86629beb44f0000a13c6fc0006683b8b44f0000007f2ef6c3017411ba01000000668990b44f0000a13c6fc0006683b8b44f0000007d0e33c9668988b44f0000a13c6fc000"},
    {0x1B5CA8, "80b8c84f000001751c80b8cb4f0000027513668b90b64f0000668990b44f0000a13c6fc0000fbfb8b44f00008bc75f5e5b5dc3"},
    {0x71C1, "e92ae91a00"},
    // EmAtkHitCk: frame, ATK in ESI, LifeDownSet2(pPL, power, 0, flag&4), flag&8 -> HP 0.
    {0x1BDE26, "8b4d0c53568b75085750515633ffe83159e4ff8bd883c40c85db744b33c0f6460a047405b8010000000fb7560850a15470c5006a005250e85f93e4ff83c410f6460a08740f8b153c6fc00033c966898ab44f0000"},
    // EmAtkSetDamagePL: frame (0x20 locals, esi, edi), balanced calls, the same LifeDownSet2/flag&8 tail.
    {0x1B61A3, "83ec20a1105ec30033c58945fc8b4d148b45088b155470c500568b7510d94608898240030000d8610857d906d821dcc8d9c1decadec1dc1db008b100dfe0f6c4057a128b0d5470c5008d45e45081c194000000eb048d55e4525651e8f681e5ff"},
    {0x1B6203, "d9ee8b0d5470c500d955f083c40cd95df4d9e88d45f050d95df881c1a0000000518bd052e84429e5ff8d45e4508d4df051e815f1e4ffd81dccf4b00083c414dfe0f6c4017507bf01000000eb0233ffd946048b0d5470c500d8a198000000d95d"},
    {0x1B6263, "e0d945e0dc150802b100dfe0f6c4057a07ddd883c704eb10d81d1406b100dfe0f6c4057a0383c7028b750c33c0f6460a047405b8010000000fb75608506a005251e8180fe5ff83c410f6460a08740f8b0d3c6fc00033c0668981b44f0000"},
    // The zeroing caller: call LifeDownSet2 at 0x11A354, then mov word [ecx+4FB4],ax (ax 0) at 0x11A39C.
    {0x11A343, "e8cc76eeff8b155470c500536a006a0a52e868ceeeff83c410f6868b020000047416e8e3ddeeff83f828760cc686fe00000002e9f4000000849e8b0200000f84e80000008b0d3c6fc00033c0505650506a206a2c50506aff56668981b44f0000"},
    // cItemMgr::use: entry, item type/num checks, switch; id 5 -> table byte 1 -> 0x306F08:
    // healing(2400); success -> num += 0xFFFF; num == 0 -> flags = 0; al = 1.
    {0x306C90, "558bec83ec0856578b7d088bf185ff750a5f32c05e8be55dc20400530fb71f8d45f85053e85b48d0ff0fb645fa83c408480f842a0200000f"},
    {0x306CC8, "b757026685d20f841d0200008bc38d48ff81f9960000000f873f0200000fb689a06f7000ff248d786f7000"},
    {0x306ECF, "e81ce8ffff84c07419baffff000066015702eb7db920030000e803e8ffff84c075665b5f32c05e8be55dc20400"},
    {0x306F08, "b960090000ebc0"},
    {0x306F60, "66837f02007504c64704005b5fb0015e8be55dc20400"},
    {0x306F7C, "086f7000"},
    {0x306FA4, "01"},
    // healing(ecx): to_whom [0xC70FE6] 0 -> pl_life += n, clamped to pl_life_max; fails when full.
    {0x3056F0, "a0e60fc70084c0753ba13c6fc000668b90b44f0000663b90b64f00007d65660188b44f0000a13c6fc0000fb788b64f0000663988b44f00007e07668988b44f0000b001c3"},
    // cItemMgr::search and the two thunks.
    {0x305E80, "558bec5356578bf98b771c8b471433d285f67e2bb9010000008da4240000000084480474118a58053a5f137509668b5d08663918740b03d183c00e3bd67ce133c05f5e5b5dc20400"},
    {0xE791, "e9ea762f00"},
    {0x519B, "e9f01a3000"}};
// Rupee colours (rupee_colors.h): ItemGetBinTplAddr thunk/body (reads cItmSys work[id] at VA 0xC59B9C), the
// SceAtCreateItemAt record stores (id +0x78, num +0x7C) and its call, the room item set (id +0x1C) and its call.
// Both calls are redirected (rel32 only) to stubs that remember the record; the rest is read, never written.
inline constexpr Pin kRupeeColours[] = {
    {0x5A92, "e9196b2400"},
    {0x24C5B0, "558bec0fb645088b0d9c9bc5008b550c8d0cc18b01890285c075025dc38b49048b4510890833c085c90f95c05dc3"},
    {0x2BA070, "6689777866894f7e6689577c"},
    {0x2BA107, "518d55f8d95f645256e87db9d4ff83c40c"},
    {0x2BAEFD, "0fb7461c"},
    {0x2BAF2D, "0fb64e1c8d55f4528d45f85051e853abd4ff83c40c"}};
// Mortal Draw prompt (mortal_draw_prompt.h): cActionButton::set (thiscall, 8 args, RET 0x20) up to the work
// fields it writes, and its thunk. Called with func = NULL from the arbiter entry; never hooked.
inline constexpr Pin kMortalDrawPrompt[] = {
    {0xCFDB, "e930da1700"},
    {0x18AA10, "558bec8a41403c080f83c30000000fb6d08d1452568d74d144fec088414085f60f84aa0000008b450883f8517c05b8500000008b55148956088a552088460c8b451088560e8b55248946048b450c8956108b551888460d8956148a551c"}};
// Save anywhere (save_anywhere.h, Ocarina Serenity): CardSave thunk 0x279D -> 0x5641C0, the typewriter path,
// card task, cGameSave pl_pos, rule reads (ReadHealth, Status2/3, task slots, em list, em10 setFindPL).
// Exact windows of scripts/save_anywhere_pins.py PINS (tests/test_save_anywhere_header.py keeps them equal).
// None lies on a re4_tweaks (re4_fps 1.5.1) patch site; checked against its patterns and a live process (12-oct).
inline constexpr Pin kSaveAnywhere[] = {
    {0x23E636, "881d583ac600e888b5dcffb9c002c500e89ec7dcffa13c6fc000f78070010000000080007505e8e8d5dcff"},
    {0x18AB55, "a13c6fc000f78070010000000100000f8526010000f7801c500000000010000f8516010000807f41000f850c010000"},
    {0x18AC1A, "8b5e080fb64b3651e86f78e7ff8b56108b46088b4e045250ffd183c40c"},
    {0x18A930, "558bec8b0d5470c5006683b924030000007f0632c05dc20400568b7508f64614027509e84aabe7ff85c07438"},
    {0x27D800, "b204849168040000752880b9fc00000000751f8a89fd0000000fb6c183c0fb83f80b77110fb68058d86700ff248550d8670033c0c3a13c6fc0003890c84f0000750580f90a74ebb801000000c38d4900"},
    {0x27D850, "32d8670035d86700000100000001010001000100"},
    {0x282420, "8b155470c5000fb68afc00000033c083f9040f87c3000000ff248d10256800"},
    {0x282524, "56246800602468006a2468007424680060246800602468007e246800ca246800d8246800c3246800fb2468007e246800fb246800fb246800e6246800fb246800d1246800bc246800df246800"},
    {0x282456, "b801000000e9a0000000b802000000e996000000b804000000e98c000000b808000000e982000000"},
    {0x814510, "003d6b0001000000"},
    {0x2B3D00, "558bec833d6070c500007446a13c6fc000f78024500000000000207435"},
    {0x2B3D52, "8b4d088b515c6a0152e83dead4ff83c408b0015dc3"},
    {0x279D, "e91e1a5600"},
    {0x5641C0, "558bec8a450ca802740a8b0d3c6fc00083490408a804740a8b0d3c6fc00083490410a808740a8b0d3c6fc00083490420a810740a8b0d3c6fc00083490440a820740ca13c6fc00081480498000000"},
    {0x56420E, "8a45088b0d3c6fc0006a0168d34140006a018881a24f0000e8d428aaff6a01e85a41aaff83c4105dc3"},
    {0x6AFF, "e90cf12b00"},
    {0x838C, "e9cfd02b00"},
    {0x41D3, "e918fe5500"},
    {0x2C5C10, "558bec8b4d088bc169c04803000080b8447dc600007426"},
    {0x2C5C4D, "8b4d0c8b55108d80407dc600c78038030000653c400089883c030000895014c640070666c74004010f5dc3"},
    {0x2C5460, "558bec8b450885c00f84a5000000568b353c7dc60083f8017612894508db450885c0790ed80550fdb000eb06d9057cd2b100d95e0ca13c7dc6008a480480e18280c902884e04"},
    {0x563FF0, "558beca13c6fc000818828500000000800005356578b7d0833db83ff017506881d0d1bc500"},
    {0x5640E5, "83ff015f5e5b7507c6050d1bc50001a13c6fc00081a028500000fff7ffff5de927e5a9ff"},
    {0x55F450, "558bec5153568bf133db578b7d08"},
    {0x55F515, "3bfb7509a13c6fc000836004fbe80cc4aaff530fb6f8e8cdaeaaff"},
    {0x55F596, "a13c6fc0008b4858894e0c8b153c6fc000c74258ffffffffa13c6fc000816058fff7ffffa13c6fc000816058ffdfffff"},
    {0x55F5DE, "8b0d3c6fc0008b9170010000895608a13c6fc000c78070010000ffffffffa13c6fc00081a070010000ffffff7fa13c6fc00083a070010000bf"},
    {0xA3FD, "e9eeb22b00"},
    {0x2C56F0, "558bec8b45088b0d3c6fc00069c048030000d94170d880487dc600d998487dc6008088447dc600805dc3"},
    {0xE84F, "e9dc6e2b00"},
    {0x55E971, "8bcee87c91aaff8b4e088b153c6fc0006a00898a70010000e8c1feaaffa13c6fc00083c4048458040f85be0000008b4e0c6a006a3c894858"},
    {0x55E16A, "f6400460750ae8f091aaffa13c6fc0008b4004a820740cc7833020000002000000eb16a840740cc7833020000003000000eb0689b3302000008b83302000008b0d8495c5005051b9a596c500e8ac53aaff"},
    {0x3567, "e9248e2300"},
    {0x23C3CD, "8b0d3c6fc000807920037547a15470c5008b90940000008991d04f00008b90980000008991d44f00008b809c0000008981d84f00008b0d5470c500d981a40000008b153c6fc000d99adc4f00008b0d3c6fc000"},
    {0x23C420, "8b450c8981808400008b0d3c6fc0008b560868f836000081c1904f00005152e86c054300"},
    {0x3631F0, "568bf1814e0400002000a13c6fc000d980dc4f0000d99ea40000008b0d3c6fc00081c1d04f0000518bcee82aeac9ff"},
    {0x23D7E0, "a18495c50050b9a596c500e8719fdcffa13c6fc00081a028500000fff7ffff8b153c6fc000b90100000066898ace4f0000a13c6fc000"},
    {0x23D816, "81485400010000a13c6fc000816054fffff7ffa13c6fc000836054bfa13c6fc00080b87c84000006750683485420eb04836054dfa13c6fc000668b88ac4f000066894828a13c6fc0008a90ae4f000088502ac3"},
    {0x23D984, "8b0d3c6fc0008b91d04f000089512c8b91d44f00008951308b81d84f0000894134a13c6fc000d980dc4f0000d95838"},
    {0x23ACA5, "8b512c8991d04f00008b51308991d44f00008b41348981d84f0000a13c6fc000d94038d998dc4f0000a13c6fc000668b4828668988ac4f0000"},
    {0x23E4DA, "68d895c5006870da63006a01e81486dcff"},
    {0x23E71F, "ba0000001085967001000075238b0d5470c5008b4104a82074168596205000007407a90008000074078b118b4210ffd0"},
    {0x2C152C, "a13c6fc00081881c50000000100000a13c6fc00081882050000000000010"},
    {0x367B53, "a13c6fc000b900000004858824500000740432c05dc3098824500000"},
    {0x1AF5D0, "8b491433c0a35870c50085c9741c8bfff681cc030000018b4908740ab801000000a35870c50085c975e6c3"},
    {0x1AEE10, "558bec8b81cc0300008b4d08ba01000000d3e223c2f7d81bc0f7d85dc20400"},
    {0x1AED90, "558bec8bc18b4d08ba01000000d3e20990cc0300008b90cc030000"},
    {0x33B70, "568bf16683be24030000007e396a05e8861ffdff85c0742e80be010100000674258b86b0040000d9ee25ffff7fff0d000100008986b0040000d99e38070000c686f6070000015ec3"},
};
// Ocarina staff (ocarina_staff.h, docs/ocarina-staff-2026-10-12.md): IDSystem set/kill/unitPtr, IdTexDataLoad/Release,
// their thunks, plus the ID_UNIT field writes the staff relies on.
inline constexpr Pin kOcarinaStaff[] = {
    {0x4449, "e9720d2400"}, {0xCE9B, "e9904b2300"}, {0x95F7, "e9849f2300"},
    {0x1F46, "e9e5832400"}, {0xBF50, "e9bbe32300"},
    // set: prologue, class bit in m_set_flag (+0x0C), v2 copy texId..tex_flag/pow, RET 0x18.
    {0x2451C0, "558bec81ec980a0000"},
    {0x2451EF, "898548ffffff8d44900c83e11f57ba00000080d3ea8d7b086a008bf70910"},
    {0x24535C, "0fb64e0b884b78"},
    {0x245395, "8b4e14898b940000008b56188993980000008b461c89839c000000"},
    {0x24541C, "d94650d99bdc000000d94654d99be0000000"},
    {0x24542E, "0fb64e58888bec0000000fb656598893ed0000000fb6465a8883ee0000000fb64e5b888bef000000"},
    {0x2454B4, "0fb6466f88838b000000"},
    {0x248EB9, "c21800"},
    // kill (whole body) and unitPtr (whole body, dummy unit 0xC599D0 at base 0x400000).
    {0x241A30, "558bec538b5d0c56578bf18b565033ff393e7e31803aff742181fbff00000074113a5a0275148a45083cff74053a42037508528bcee85775dcff4781c2440100003b3e7ccf0fb6cb8bc183e11fba00000080d3eac1e8055f8d44860cf7d221105e5b5dc20800"},
    {0x243580, "558becf605549bc500018b41507525830d549bc5000133d26689154e9ac500668915509ac500668915529ac500668915549ac50053568b3133d285f67e1f8a4d0c8a5d088038ff740a3a580375053a4802740f4205440100003bd67ce7b8d099c5005e5b5dc20800"},
    // IdTexDataLoad (version 0xB check, TexRegist through g_pIdTexSys 0xC59B8C) and IdTexRelease.
    {0x24A330, "558bec8b450883ec1083380b7418"},
    {0x24A3D6, "8b0d8c9bc50050e86eb2dbff"},
    {0x24A310, "558bec8b45088b0d8c9bc50050e8ff09dcff5dc3"},
    // idSysMove04: tex_flag 0x2 (+0x8B) holds texNo (+0x7A).
    {0x242CBA, "f6868b00000002755e8a567c530fb6da88567a"},
    // IDSystem::roomInit: stride 0x144, units at +0x50, be_flag 0xFF.
    {0x2415E0, "53568bf133db895e04391e7e295733ff8b465003c7684401000050e859afdcff8b4e50c6040fff4383c40881c7440100003b1e7cdb5f"},
};
// Boss music duck (boss_music_duck.h, 18-oct): SndWatcher is the MinHook target (its 5-byte head), its two
// slot loops prove the UHD BGM_STAT layout (pSnd VA 0x1223A7C, bgm_state[2] +0x10, str_state[4] +0x30, stride
// 0x10, now_play_vol +2 refreshed every call), SndRoomBgmVolSet is the native volume call the duck makes (the
// same one r11f's Mendez event uses; SndWatcher's FSE record path inlines it), and SndRoomStrStop shows play_no
// +8 / play_blk +0xA against room_str_tbl[0] (+0x938). scripts/boss_music_duck_pins.py regenerates/re-verifies.
inline constexpr Pin kBossMusicDuck[] = {
    {0x570C80, "558beca13c6fc00083ec0c80b8ad4f0000057310f7801c500000000000100f85b002000053bb00080000859870010000750ae849f9ffffe863aca9ff57e804baa9ffe8a820a9ffe8f0c3a9ff33ff5690"},
    {0x570CD0, "a17c3a2201807c0710008d74071074338b4e0451e87824a9ff83c40485c0750d6a1056e861b8a9ff83c408eb168b560452e88c07a9ff83c40485c074068a406388460283c71083ff2072b5"},
    {0x570D1B, "bf300000008b0d7c3a2201803c0f008d340f74338b560452e8965ca9ff83c40485c0750d6a1056e812b8a9ff83c408eb168b460450e80346a9ff83c40485c074068a484b884e0283c71083ff7072b6"},
    {0x56C550, "558bec0fb6450840c1e00403057c3a22015332db38187421385801751c8b4d0c8b55108b400451526a0150e8c121aaff83c41085c0b00174028ac35b5dc3"},
    {0x5701B0, "558bec8b157c3a220133c98d4230569080380074196683780a0075128bb23809000081e6ff0000006639700874"},
};
// Event sheath (event_sheath.h, 18-oct): Event::ControlTransFlag is the MinHook target (thiscall Event*, its only
// caller is Event::Run through the thunk 0x40B915); DatTbl::GetDat proves the ModTbl (+0x48) layout (num +0,
// pWork +4, entries of 0x3C: in-use +0x30 bit 0, type +0x31, Dat +0x34); SetPartsSub/SetOm/addModel prove that the
// event parts hang on their oya's parts list (+0x15C/+0x14) and that PPl (+0x80) is the "pl0000" obj; ExeBeginEvt
// maps "em/pl00/pl000d.bin" to player arc entry 9 (pl00_005, Link's sheathed sword); the two draw tests prove the
// visibility bits (parts info +4 bit 3, model be_flag bits 1|2). scripts/event_sheath_pins.py regenerates/re-verifies.
inline constexpr Pin kEventSheath[] = {
    {0x2218A0, "558bec83ec1c8b8104010000d9ee8b51488945ecd85dec894df0c645ff008955e4dfe0f6c4050f8b060200005733ff897df485d20f8ef70100005356eb058bff8b4df0578d55ff528d45f88d7148508bcee8e66fdeff84c0"},
    {0x220D33, "538b590485db751c"},
    {0x220D9D, "8b3933c085ff7eed8bcbf6413001742e8b75108bd18a1a3a1e751a84db74128a5a013a5e01750e83c20283c60284db75e433d2eb051bd283daff85d274134083c13c3bc77cc45f5e32c05b8be55dc210008b4dfc8b71048b7d088bd0c1e2042bd003d203d28b74163489378b49048a5411318b4d0c88118b"},
    {0x2241D3, "8b75188b4d088d55e852568d45ff508d55f483c14852894dece8dcf1ddff84c0"},
    {0x224354, "568bcfe8ca00deff8d4de8518b4dec6a026a006a035653e82087deff"},
    {0x223CA6, "836704fd8b51045257e859b6deff8b9544fbffff83c408be58b8b1008bc28a083a0e751884c974108a48013a4e01750c03c303f384c975e633c0eb051bc083d8ff8bb540fbffff85c0750689be80000000"},
    {0x25C5B0, "558bec8b815c01000085c0750d8b450889815c0100005dc204008378140074098b40148378140075f78b4d088948145dc2"},
    {0x22692D, "8b46246a006a026a006a0703c650689cc1b100b9883dc000e84661deff"},
    {0x2EAA3F, "8b4e04f6c108750e8b761489b528ffffffe922070000"},
    {0x2EB60B, "8b4604a8020f8499040000a8040f8491040000"},
    {0x2220C0, "558bec83ec30a1105ec30033c58945fc8bc18b4d0c53568b7008894dd433c9578945d08975d885f6747b3bce720433ffeb098b780c0faff90378048b570481e20102000083fa017557395510740b8b5744c1ea13f6c20175478d47188d75dc2b"},
};
// Sword in hand (sword_hand.h, sword-hand-01, 19-oct): the native knife draw (knife_r3_ready10 frame 4) hides the gun
// with setDisp(1, 0) and inlines Body->setKnife(true) as `or dword [Body(+7E4)->m_pKnife(+2C) + 4], 8`; the knife-down
// inlines setKnife(false) as `and dword [..+4], ~8`; the model-info draw loop skips an info without bit 8. Together
// with kOcarinaWeapon (level 0 of cObjWep::flag) these are the two words the swap writes.
inline constexpr Pin kSwordHand[] = {
    {0x353902, "8b86d80700008b48346a006a01e8bf85cbff8b86e407000083782c0074078b402c83480408"},
    {0x353DC2, "8b86e407000039582c74078b402c836004f7"},
    {0x2EAA3F, "8b4e04f6c108750e"},
};
constexpr size_t kMaxPin = 128;   // ocarina staff kill/unitPtr bodies are 102 and 104 bytes

inline bool Hex(const char* hex, unsigned char* out, size_t capacity, size_t& n) {
    const size_t length = hex ? std::strlen(hex) : 0;
    n = length / 2;
    if (!n || n > capacity || length % 2) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned v = 0;
        for (unsigned k = 0; k < 2; ++k) {
            const char c = hex[2 * i + k];
            const unsigned d = c >= '0' && c <= '9' ? unsigned(c - '0') : c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10)
                             : c >= 'A' && c <= 'F' ? unsigned(c - 'A' + 10) : 99U;
            if (d > 15) return false;
            v = v * 16 + d;
        }
        out[i] = static_cast<unsigned char>(v);
    }
    return true;
}
// One pin at `base`: the in-memory bytes, normalized to the preferred base, equal the hex exactly.
template <class Mem> bool Matches(Mem& m, uint32_t base, uint32_t rva, const char* hex) {
    unsigned char want[kMaxPin]{}, have[kMaxPin]{};
    size_t n = 0;
    return base && Hex(hex, want, sizeof(want), n) && image_reloc::ReadPreferred(m, base, rva, have, n) && !std::memcmp(have, want, n);
}
template <class Mem, size_t N> bool All(Mem& m, uint32_t base, const Pin (&pins)[N]) {
    for (const auto& p : pins) if (!Matches(m, base, p.rva, p.hex)) return false;
    return true;
}
}  // namespace link_runtime::native_pins

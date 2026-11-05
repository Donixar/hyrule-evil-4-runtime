#pragma once
// The Master Sword Blade Lock's three builds (docs/debates/2026-11-04-forcejeo/acta.md, step 14: the graduation).
//   * public 3.5.x (LINK_SWORD_LOCK=0): no duel at all; the H0 probe and the glyph census stay in, inert without their keys.
//   * lab (LINK_SWORD_LOCK_LAB=1): the duel plus every lab tool (SwordLockLab, the force trigger, the probe and census
//     keys, the seed), its assets in Bin32\sword_lock_lab\.
//   * graduated 3.6.0 (no flag, or LINK_SWORD_LOCK=1 without LINK_SWORD_LOCK_LAB): the duel as a feature behind [Combat] SwordLock,
//     with Donald's decided defaults compiled in (docs/swordlock-decisiones-2026-11-04.md) and none of the lab tools, its
//     assets in Bin32\sword_lock\. scripts/package/build_package.py lab_dll_errors refuses any lab marker in it.
// Since 3.6.0 the plain build (no flag: scripts/build_link_runtime.bat, build_release.py) is the graduated one. The 3.5.x
// public DLL is LINK_SWORD_LOCK=0 (byte-identical to main's 4c3c21f9 at the graduation commit); the lab adds
// LINK_SWORD_LOCK_LAB=1 (byte-identical to the candidate 14 lab DLL 28bbe7a8 at that commit).
#ifndef LINK_SWORD_LOCK_LAB
#define LINK_SWORD_LOCK_LAB 0
#endif
#ifndef LINK_SWORD_LOCK
#define LINK_SWORD_LOCK 1
#endif
#if LINK_SWORD_LOCK_LAB && !LINK_SWORD_LOCK
#error "LINK_SWORD_LOCK_LAB=1 needs the feature (LINK_SWORD_LOCK=1)"
#endif
#if LINK_SWORD_LOCK && !LINK_SWORD_LOCK_LAB
#define LINK_SWORD_LOCK_GRADUATED 1
#else
#define LINK_SWORD_LOCK_GRADUATED 0
#endif

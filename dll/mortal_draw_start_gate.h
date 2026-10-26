#pragma once
// Mortal Draw from walk/run (12-oct, after build 08: "parry -> run behind -> cut" took 7 s
// because the start required Link to stand still). Pure; the runtime calls Pinned() once on
// the mapped image and only then starts from StartFrom::NormalMove (dispatch.h).
//
// Attribution (bio4.exe 1.1.0, SHA 19aed4af...; GC decomp game/player.cpp Pl_func_move_tbl):
// cPlayer::move (0x364D20) runs R0 (+0xFC) through 0x81BCB4; R0 0 = pl_R0_Move, which runs
// R1 (+0xFD) through the move table 0x81BCD0, laid out exactly as the GC table:
//   0 Footwork 0x365340 (idle; 0x00010000 = R2 1 steady), 1 Walk 0x363330, 2 Back 0x363620,
//   3 Run 0x363770, 4 Turn 0x363C80, 5 Turn180 0x363DB0, 6 Weapon 0x363FF0, 10 Aux (calls
//   +0x78C), 11 knife thunk 0xD1A7 -> 0x3546C0, 13 and 18 null.
// The four bodies read R2 (+0xFE) first. R3 carries motion-continuity flags (&1, &4), so it is
// not fixed while walking or running. Walk/Back/Run/Turn are admitted; Turn180 and the rest
// (weapon/aim/reload, knife, ladders, crouch, jumps, pushes, fences) are not.
// re4_fps 1.5.1 (re4_tweaks, MouseTurning.cpp TurnHookWalk/TurnHookRun) always rewrites the
// Walk and Run prologues: MakeInline(body+1, body+6) puts E8 rel32 over `mov ebp,esp; sub esp,8`
// (a CALL into its reg_pack wrapper that redoes those two instructions). Those two bodies accept
// exactly `55 E8 rel32` calling into another loaded module's code (probe), with the bytes after
// the window still original. The table, the other bodies and the R2 reads stay pinned.
#include <cstddef>
#include <cstdint>
#include "foreign_patch.h"

namespace link_runtime::mortal_draw::start_gate {
constexpr uint32_t kMoveTableRva = 0x81BCD0;
constexpr uint32_t kMoveBodies[6] = {0x365340, 0x363330, 0x363620, 0x363770, 0x363C80, 0x363DB0};
constexpr uint32_t kKnifeThunkRva = 0xD1A7, kKnifeMoveRva = 0x3546C0;
inline uint32_t U32(const unsigned char* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
// Original Walk/Run prologue: push ebp; mov ebp,esp; sub esp,8; push ebx; push esi; mov esi,[ebp+8].
constexpr unsigned char kWalkRunPrologue[11] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x53, 0x56, 0x8B, 0x75, 0x08};
// Accepted re4_tweaks forms (for the log): site RVA and absolute call target, 0 when original.
struct Foreign { uint32_t site_rva[2] = {}, target[2] = {}; unsigned count = 0; };
// `image`: the mapped bio4 image at `base`. True only for the attributed layout above.
inline bool Pinned(const unsigned char* image, size_t size, uint32_t base, const foreign::Probe* probe = nullptr, Foreign* out = nullptr) {
    if (out) *out = Foreign{};
    if (!image || size < kMoveTableRva + 21 * 4) return false;
    const unsigned char* t = image + kMoveTableRva;
    Foreign seen;
    for (unsigned i = 0; i < 6; ++i) {
        if (U32(t + 4 * i) != base + kMoveBodies[i]) return false;
        const unsigned char* body = image + kMoveBodies[i];
        if (body[0] != 0x55) return false;
        if (body[1] == 0x8B && body[2] == 0xEC) {  // push ebp; mov ebp,esp
            if ((i == 1 || i == 3) && (body[3] != 0x83 || body[4] != 0xEC || body[5] != 0x08)) return false;
        } else {
            uint32_t target = 0;
            if ((i != 1 && i != 3) || !foreign::InlineCall(body + 1, 0, base + kMoveBodies[i] + 1, base, static_cast<uint32_t>(size), target) ||
                !foreign::Accept(probe, target)) return false;
            seen.site_rva[seen.count] = kMoveBodies[i] + 1; seen.target[seen.count] = target; ++seen.count;
        }
        if (i == 1 || i == 3)
            for (unsigned k = 6; k < sizeof(kWalkRunPrologue); ++k) if (body[k] != kWalkRunPrologue[k]) return false;
    }
    for (unsigned i = 1; i <= 4; ++i) {  // each moving body reads R2 (movzx eax,byte [esi+0FEh] or cmp [esi+0FEh],bl)
        const unsigned char* body = image + kMoveBodies[i];
        bool reads_r2 = false;
        for (unsigned k = 0; k + 4 < 32; ++k)
            if (body[k + 1] == 0xFE && body[k + 2] == 0 && body[k + 3] == 0 && body[k + 4] == 0 && (body[k] == 0x86 || body[k] == 0x9E || body[k] == 0xBE))
                reads_r2 = true;
        if (!reads_r2) return false;
    }
    if (U32(t + 13 * 4) || U32(t + 18 * 4) || U32(t + 11 * 4) != base + kKnifeThunkRva) return false;
    const unsigned char* thunk = image + kKnifeThunkRva;
    if (thunk[0] != 0xE9 || kKnifeThunkRva + 5 + U32(thunk + 1) != kKnifeMoveRva) return false;
    if (out) *out = seen;
    return true;
}
}  // namespace link_runtime::mortal_draw::start_gate

/*
 * tt_guest.h -- guest addresses to host pointers for the game-side C code.
 *
 * `xbox_GetMemoryOffset() + va` is only right for the 64 MB of RAM when the
 * runtime uses the compact guest layout (PS Vita, RECOMP_COMPACT_GUEST): the
 * contiguous window at 0x80000000 is a block of its own there, so that sum
 * pointed outside any allocation (a crash in the hints' scan, kb_txt_apply,
 * on the Vita). xbox_HostPtr translates in either layout (on the desktop it
 * is the same sum).
 *
 *   TT_GP(va)      uint8_t * to guest va
 *   TT_GBIAS(at)   p such that p + va is va's host address, for every va in
 *                  the same region as `at` (RAM 0..0x03FFFFFF, or the window
 *                  0x80000000..0x83FFFFFF): for scans, recomputed when a
 *                  scan jumps from one region to the other
 */
#ifndef TT_GUEST_H
#define TT_GUEST_H
#include <stdint.h>
extern void *xbox_HostPtr(uint32_t va);
#define TT_GP(va) ((uint8_t *)xbox_HostPtr((uint32_t)(va)))
#define TT_GBIAS(at) ((const uint8_t *)xbox_HostPtr((uint32_t)(at)) - (uintptr_t)(uint32_t)(at))
#endif

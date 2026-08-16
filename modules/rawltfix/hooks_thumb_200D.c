#include <stdint.h>

extern void rawltfix_filter_set_size(uint32_t channel, void *info, uint32_t caller_lr);

/*
 * Thumb wrapper for Canon 200D 1.0.1 edmac_set_size at 0x00035506.
 * Saves all incoming registers, lets the ARM filter modify only the size
 * descriptor in-place, restores registers, replays the overwritten Canon
 * prologue, then resumes at 0x0003550f (Thumb).
 */
void __attribute__((noreturn, noinline, naked, aligned(4)))
rawltfix_hook_set_size_200D(void)
{
    asm volatile(
        "push {r0-r11, lr}\n"
        "mov  r2, lr\n"
        "sub  sp, #4\n"          /* keep AAPCS stack alignment for BLX */
        "ldr  r3, =rawltfix_filter_set_size\n"
        "blx  r3\n"
        "add  sp, #4\n"
        "pop  {r0-r11, lr}\n"

        /* exact overwritten Canon instructions */
        "push {r4,r5,r6,r7,lr}\n"
        "mov  r5, r0\n"
        "ldr  r6, =0x00066264\n"
        "sub  sp, #0x24\n"

        /* resume Canon function in Thumb state */
        "ldr  pc, =0x0003550f\n"
    );
}

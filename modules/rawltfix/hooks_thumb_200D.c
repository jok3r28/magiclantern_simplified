#include "rawltfix.h"

void __attribute__((noreturn,noinline,naked,aligned(4))) rawltfix_hook_set_size_200D(void)
{
    asm(
        "push { r0-r11, lr }\n"
        "mov r2, lr\n"
        "sub sp, #4\n"
        "ldr r3, =rawltfix_filter_set_size\n"
        "blx r3\n"
        "add sp, #4\n"
        "pop { r0-r11, lr }\n"

        /* overwritten edmac_set_size(200D.101) instructions */
        "push {r4,r5,r6,r7,lr}\n"
        "mov  r5, r0\n"
        "ldr  r6, =0x66264\n"
        "sub  sp, #0x24\n"

        /* resume immediately after the 8-byte entry patch */
        "ldr pc, =0x3550f\n"
    );
}

#pragma once
#include <stdint.h>

struct edmac_info_D4567
{
    uint32_t off1a;
    uint32_t off1b;
    uint32_t off2a;
    uint32_t off2b;
    uint32_t off3;
    uint32_t xa;
    uint32_t xb;
    uint32_t ya;
    uint32_t yb;
    uint32_t xn;
    uint32_t yn;
};

void rawltfix_filter_set_size(uint32_t channel,
                              struct edmac_info_D4567 *info,
                              uint32_t caller_lr);
void __attribute__((noreturn,noinline,naked,aligned(4))) rawltfix_hook_set_size_200D(void);

#include <stdint.h>
#include "module.h"
#include "raw.h"
#include "patch_mmu.h"

/*
 * EOS 200D / 1.0.1 lower-bit RAW EDMAC correction.
 *
 * Reconstructed from the exact working rawltfix.mo shipped in the user's
 * current build.  The filter constants and hook site below are verified
 * against that ELF's disassembly.
 */

#define EDMAC_SET_SIZE_PATCH_ADDR  0x00035506u
#define EDMAC_SET_SIZE_RETURN_ADDR 0x0003550fu
#define RAW_SET_SIZE_CALLER_LR      0xE0119F3Fu
#define RAW_EDMAC_CHANNEL           23u

#define RAW_WIDTH                   2096u
#define RAW_HEIGHT                  1164u
#define CANON_14BIT_XB              0x0E54u /* 3668 */
#define CANON_YB                    0x048Bu /* 1163; EDMAC y extent encoding */
#define RAW_12BIT_PITCH             0x0C48u /* 3144 */
#define RAW_12BIT_FRAME_SIZE        0x0037D760u
#define RAW_10BIT_PITCH             0x0A3Cu /* 2620 */
#define RAW_10BIT_FRAME_SIZE        0x002E88D0u

struct edmac_info_D4567_local
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

extern void rawltfix_hook_set_size_200D(void);

static uint32_t rawltfix_installed = 0;

/* Called by the Thumb wrapper before Canon edmac_set_size executes. */
void rawltfix_filter_set_size(
    uint32_t channel,
    struct edmac_info_D4567_local *e,
    uint32_t caller_lr)
{
    /* Strict caller/channel guard: do not touch unrelated EDMAC users. */
    if (caller_lr != RAW_SET_SIZE_CALLER_LR ||
        channel != RAW_EDMAC_CHANNEL ||
        e == 0)
        return;

    /* Strict RAW-mode geometry guard. */
    if ((uint32_t)raw_info.width  != RAW_WIDTH ||
        (uint32_t)raw_info.height != RAW_HEIGHT)
        return;

    /* Canon is still requesting the 14-bit row width in lower-bit mode. */
    if (e->xb != CANON_14BIT_XB || e->yb != CANON_YB)
        return;

    /* Match the exact simple size descriptor observed on this call path. */
    if (e->off1a || e->off1b || e->off2a || e->off2b || e->off3 ||
        e->xa || e->ya || e->xn || e->yn)
        return;

    if (raw_info.bits_per_pixel == 12)
    {
        if ((uint32_t)raw_info.pitch == RAW_12BIT_PITCH &&
            (uint32_t)raw_info.frame_size == RAW_12BIT_FRAME_SIZE)
        {
            e->xb = raw_info.pitch;
        }
        return;
    }

    if (raw_info.bits_per_pixel == 10)
    {
        if ((uint32_t)raw_info.pitch == RAW_10BIT_PITCH &&
            (uint32_t)raw_info.frame_size == RAW_10BIT_FRAME_SIZE)
        {
            e->xb = raw_info.pitch;
        }
    }
}

static unsigned int rawltfix_init(void)
{
    if (!is_camera("200D", "1.0.1"))
    {
        printf("RAWLTFIX: unsupported camera; no hook installed.\n");
        return -1;
    }

    struct function_hook_patch f_patch = {
        .patch_addr = EDMAC_SET_SIZE_PATCH_ADDR,
        .orig_content = { 0xf0, 0xb5, 0x05, 0x46, 0x55, 0x4e, 0x89, 0xb0 },
        .target_function_addr = (uint32_t)rawltfix_hook_set_size_200D,
        .description = "200D guarded 10/12-bit RAW EDMAC pitch",
    };

    struct patch patch = {0};
    uint8_t hook_mem[8] = {0};

    if (convert_f_patch_to_patch(&f_patch, &patch, hook_mem))
    {
        printf("RAWLTFIX: hook conversion failed.\n");
        return -1;
    }

    int rc = apply_patches(&patch, 1);
    if (rc)
    {
        printf("RAWLTFIX: hook installation failed (%x).\n", rc);
        return -1;
    }

    rawltfix_installed = 1;
    printf("RAWLTFIX: guarded 10/12-bit channel-23 correction loaded.\n");
    return 0;
}

static unsigned int rawltfix_deinit(void)
{
    if (rawltfix_installed)
    {
        unpatch_memory(EDMAC_SET_SIZE_PATCH_ADDR);
        rawltfix_installed = 0;
    }
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(rawltfix_init)
    MODULE_DEINIT(rawltfix_deinit)
    MODULE_LONGNAME("RAWLTFIX - EOS 200D lower-bit RAW correction")
MODULE_INFO_END()

MODULE_STRINGS_START()
    MODULE_STRING("Author", "Codex / EOS 200D lower-bit investigation")
    MODULE_STRING("License", "GPL")
    MODULE_STRING("Summary", "Guarded quiet 200D 10/12-bit RAW EDMAC correction")
MODULE_STRINGS_END()

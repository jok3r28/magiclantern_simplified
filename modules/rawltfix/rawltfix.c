#include "module.h"
#include <string.h>
#include "raw.h"
#include "patch.h"
#include "rawltfix.h"

#define EDMAC_SET_SIZE_200D 0x00035506u
#define RAW_CALLER_LR_200D  0xE0119F3Fu
#define RAW_CHANNEL_200D    23u

#define RAW_W_200D          2096u
#define RAW_H_200D          1164u
#define RAW_YB_200D         0x048Bu
#define RAW_XB_14           0x0E54u
#define RAW_XB_12           0x0C48u
#define RAW_XB_10           0x0A3Cu
#define RAW_SIZE_12         0x0037D760u
#define RAW_SIZE_10         0x002E88D0u

static int rawltfix_installed = 0;

/* Guarded, recurring correction of Canon's per-frame channel-23 descriptor.
 * This intentionally does not touch channel 24, D005 registers, RAW destination,
 * clocks or voltage. */
void rawltfix_filter_set_size(uint32_t channel,
                              struct edmac_info_D4567 *info,
                              uint32_t caller_lr)
{
    if (channel != RAW_CHANNEL_200D || caller_lr != RAW_CALLER_LR_200D || !info)
        return;

    if (raw_info.width != RAW_W_200D || raw_info.height != RAW_H_200D)
        return;

    /* Only Canon's exact simple 2096x1164 RAW descriptor is eligible. */
    if (info->xb != RAW_XB_14 || info->yb != RAW_YB_200D)
        return;
    if (info->off1a || info->off1b || info->off2a || info->off2b || info->off3 ||
        info->xa || info->ya || info->xn || info->yn)
        return;

    if (raw_info.bits_per_pixel == 12 &&
        raw_info.pitch == RAW_XB_12 &&
        raw_info.frame_size == RAW_SIZE_12)
    {
        info->xb = RAW_XB_12;
        return;
    }

    if (raw_info.bits_per_pixel == 10 &&
        raw_info.pitch == RAW_XB_10 &&
        raw_info.frame_size == RAW_SIZE_10)
    {
        info->xb = RAW_XB_10;
    }
}

static unsigned int rawltfix_init(void)
{
    if (!is_camera("200D", "1.0.1"))
        return (unsigned int)-1;

    struct function_hook_patch f_patch = {
        .patch_addr = EDMAC_SET_SIZE_200D,
        .orig_content = {0xf0, 0xb5, 0x05, 0x46, 0x55, 0x4e, 0x89, 0xb0},
        .target_function_addr = (uint32_t)rawltfix_hook_set_size_200D,
        .description = "200D guarded 10/12-bit RAW EDMAC pitch",
    };
    struct patch patch = {0};
    uint8_t hook_mem[8] = {0};

    if (convert_f_patch_to_patch(&f_patch, &patch, hook_mem))
        return (unsigned int)-1;
    if (apply_patches(&patch, 1))
        return (unsigned int)-1;

    rawltfix_installed = 1;
    return 0;
}

static unsigned int rawltfix_deinit(void)
{
    if (rawltfix_installed)
    {
        unpatch_memory(EDMAC_SET_SIZE_200D);
        rawltfix_installed = 0;
    }
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(rawltfix_init)
    MODULE_DEINIT(rawltfix_deinit)
    MODULE_LONGNAME("RAWLTFIX - EOS 200D lower-bit RAW correction")
MODULE_INFO_END()

/*
 * EOS 200D.101 RAW audio for MLV Lite.
 *
 * Uses the camera-proven DIGIC 7 Aproc PCM producer and writes WAVI/AUDF
 * blocks through MLV Lite recorder slots. Capture is pre-armed before the
 * video STARTED event so audio and video share the same MLV time origin.
 *
 * This is intentionally 200D firmware 1.0.1 specific. It does not modify
 * RAW/EDMAC/PACK32 geometry, SD clocks, or voltage state.
 */

#include <module.h>
#include <mem.h>

extern uint32_t task_create(const char *name, uint32_t priority, uint32_t stack_size, void *entry, void *arg);
extern void msleep(int amount);
extern uint32_t get_us_clock(void);
extern int is_camera(const char *model, const char *firmware);
extern int snprintf(char *str, unsigned int size, const char *format, ...);
extern void NotifyBox(int timeout, const char *fmt, ...);

typedef struct raw_aud_file FILE;
extern FILE *FIO_CreateFile(const char *name);
extern int FIO_WriteFile(FILE *stream, const void *ptr, uint32_t count);
extern void FIO_CloseFile(FILE *stream);
extern int FIO_RemoveFile(const char *filename);

#pragma pack(push,1)
typedef struct {
    uint8_t blockType[4];
    uint32_t blockSize;
    uint64_t timestamp;
} mlv_hdr_t;

typedef struct {
    uint8_t fileMagic[4];
    uint32_t blockSize;
    uint8_t versionString[8];
    uint64_t fileGuid;
    uint16_t fileNum;
    uint16_t fileCount;
    uint32_t fileFlags;
    uint16_t videoClass;
    uint16_t audioClass;
    uint32_t videoFrameCount;
    uint32_t audioFrameCount;
    uint32_t sourceFpsNom;
    uint32_t sourceFpsDenom;
} mlv_file_hdr_t;

typedef struct {
    uint8_t blockType[4];
    uint32_t blockSize;
    uint64_t timestamp;
    uint32_t frameNumber;
    uint32_t frameSpace;
} mlv_audf_hdr_t;

typedef struct {
    uint8_t blockType[4];
    uint32_t blockSize;
    uint64_t timestamp;
    uint16_t format;
    uint16_t channels;
    uint32_t samplingRate;
    uint32_t bytesPerSecond;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
} mlv_wavi_hdr_t;
#pragma pack(pop)

typedef void (*event_cbr_t)(uint32_t event, void *ctx, mlv_hdr_t *hdr);
#define MLV_REC_EVENT_STARTING  (1U<<0)
#define MLV_REC_EVENT_STARTED   (1U<<1)
#define MLV_REC_EVENT_STOPPING  (1U<<2)
#define MLV_REC_EVENT_STOPPED   (1U<<3)
#define MLV_REC_EVENT_BLOCK     (1U<<5)
#define MLV_REC_EVENT_PREPARING (1U<<7)

#define PCM_RING_BASE       0x4DEC0000u
#define PCM_BLOCK_BYTES     12000u
#define PCM_RING_BLOCKS     64u
#define PCM_BLOCK_US        62500ULL
#define PCM_SAFETY_US       125000ULL
#define SAMPLE_RATE         48000u
#define CHANNELS            2u
#define BITS                16u
#define BYTES_PER_SECOND    192000u
#define APROC_START_ADDR    0xE055107Bu
#define AUDF_HDR_AREA       0x100u
#define AUDF_BLOCK_BYTES    (AUDF_HDR_AREA + PCM_BLOCK_BYTES)
#define MAX_BLOCKS_PER_SLOT 256u
#define LOG_PATH            "ML/LOGS/RAW_AUD.LOG"

extern uint32_t mlv_rec_register_cbr(uint32_t event, event_cbr_t cbr, void *ctx);
extern uint32_t mlv_rec_unregister_cbr(event_cbr_t cbr);
extern void mlv_rec_set_rel_timestamp(mlv_hdr_t *hdr, uint64_t timestamp);
extern uint32_t mlv_rec_queue_block(mlv_hdr_t *hdr);
extern int32_t mlv_rec_get_free_slot(void);
extern void mlv_rec_get_slot_info(int32_t slot, uint32_t *size, void **address);
extern void mlv_rec_release_slot(int32_t slot, uint32_t write);
static const char deps[] __attribute__((section(".module_deps"), used)) = "mlv_lite\0";

static volatile uint32_t worker_exit;
static volatile uint32_t session_prepared;
static volatile uint32_t session_disabled;
static volatile uint32_t prearm_requested;
static volatile uint32_t capture_arm_requested;
static volatile uint32_t stop_requested;
static volatile uint32_t stopped_seen;
static volatile uint32_t capture_active;
static volatile uint32_t capture_done;
static volatile uint32_t target_valid;
static volatile uint32_t log_requested;
static volatile uint32_t session_error;
static volatile uint32_t stop_wait_timeout;

static uint64_t video_started_abs;
static uint64_t mlv_start_abs;
static uint64_t stop_abs;
static uint64_t aproc_t0;
static int32_t aproc_rc;
static uint32_t aproc_called;
static uint32_t aproc_latched; /* one direct Aproc start per power cycle */
static uint32_t next_block;
static uint32_t target_exclusive;
static uint32_t copied_blocks;
static uint32_t queued_blocks;
static uint32_t overrun_count;
static uint32_t no_slot_count;
static uint32_t slot_count;
static uint32_t slot_size_first;
static uint32_t final_null_bytes;
static uint32_t block_callback_count;
static uint32_t final_header_audio_count;

static int32_t current_slot = -1;
static uint8_t *slot_addr;
static uint32_t slot_size;
static uint32_t slot_used;
static uint32_t blocks_in_slot;
static char logbuf[2048];

static __attribute__((noinline)) void zero_bytes(void *ptr, uint32_t n)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (n--) *p++ = 0;
}

static void set_type(mlv_hdr_t *h, char a, char b, char c, char d)
{
    h->blockType[0] = (uint8_t)a; h->blockType[1] = (uint8_t)b;
    h->blockType[2] = (uint8_t)c; h->blockType[3] = (uint8_t)d;
}

static int is_type(const mlv_hdr_t *h, char a, char b, char c, char d)
{
    return h && h->blockType[0] == (uint8_t)a && h->blockType[1] == (uint8_t)b &&
           h->blockType[2] == (uint8_t)c && h->blockType[3] == (uint8_t)d;
}

static void clear_session(void)
{
    session_prepared = session_disabled = prearm_requested = capture_arm_requested = stop_requested = 0;
    stopped_seen = capture_active = capture_done = target_valid = 0;
    log_requested = session_error = stop_wait_timeout = 0;
    video_started_abs = mlv_start_abs = stop_abs = aproc_t0 = 0;
    aproc_rc = 0x7fffffff;
    aproc_called = 0;
    next_block = target_exclusive = 0;
    copied_blocks = queued_blocks = overrun_count = no_slot_count = 0;
    slot_count = slot_size_first = final_null_bytes = 0;
    block_callback_count = final_header_audio_count = 0;
    current_slot = -1; slot_addr = 0; slot_size = slot_used = blocks_in_slot = 0;
}

static int reserve_slot(void)
{
    int32_t s = mlv_rec_get_free_slot();
    if (s < 0)
    {
        no_slot_count++;
        return 0;
    }
    void *addr = 0;
    uint32_t size = 0;
    mlv_rec_get_slot_info(s, &size, &addr);
    if (!addr || size < AUDF_BLOCK_BYTES + sizeof(mlv_hdr_t))
    {
        mlv_rec_release_slot(s, 0);
        no_slot_count++;
        return 0;
    }
    current_slot = s;
    slot_addr = (uint8_t *)addr;
    slot_size = size;
    slot_used = 0;
    blocks_in_slot = 0;
    slot_count++;
    if (!slot_size_first) slot_size_first = size;
    return 1;
}

static void finalize_slot(uint32_t write)
{
    if (current_slot < 0) return;
    if (write && slot_used > 0)
    {
        mlv_hdr_t *nul = (mlv_hdr_t *)(slot_addr + slot_used);
        uint32_t remain = slot_size - slot_used;
        if (remain >= sizeof(mlv_hdr_t))
        {
            set_type(nul, 'N','U','L','L');
            nul->blockSize = remain;
            nul->timestamp = 0;
            final_null_bytes = remain;
        }
        else
        {
            session_error = 1;
            write = 0;
        }
    }
    mlv_rec_release_slot(current_slot, write ? 1u : 0u);
    if (write) queued_blocks = copied_blocks;
    current_slot = -1; slot_addr = 0; slot_size = slot_used = blocks_in_slot = 0;
}

static uint32_t safe_target_at_stop(uint64_t t)
{
    if (!aproc_called || t <= aproc_t0 + PCM_SAFETY_US) return 0;
    uint64_t d = t - aproc_t0 - PCM_SAFETY_US;
    return (uint32_t)(d / PCM_BLOCK_US) + 1u;
}

static uint32_t first_block_at_or_after_video(void)
{
    if (!aproc_called || !video_started_abs || video_started_abs <= aproc_t0) return 0;
    uint64_t d = video_started_abs - aproc_t0;
    return (uint32_t)((d + PCM_BLOCK_US - 1u) / PCM_BLOCK_US);
}

static void arm_capture_if_ready(void)
{
    if (capture_done || capture_active || session_disabled) return;
    if (!aproc_called || !video_started_abs) return;
    next_block = first_block_at_or_after_video();
    capture_active = 1;
    capture_arm_requested = 0;
}

static int capture_block_to_slot(uint32_t index)
{
    uint64_t now = get_us_clock();
    uint64_t block_start = aproc_t0 + (uint64_t)index * PCM_BLOCK_US;
    if (now < block_start + PCM_SAFETY_US) return 0;

    uint32_t producer = (uint32_t)((now - aproc_t0) / PCM_BLOCK_US);
    if (producer >= index + PCM_RING_BLOCKS)
    {
        overrun_count++;
        session_error = 1;
        return -2;
    }

    if (current_slot < 0 && !reserve_slot())
    {
        return 0;
    }

    if (slot_used + AUDF_BLOCK_BYTES + sizeof(mlv_hdr_t) > slot_size || blocks_in_slot >= MAX_BLOCKS_PER_SLOT)
    {
        finalize_slot(1);
        if (!reserve_slot()) return 0;
    }

    mlv_audf_hdr_t *h = (mlv_audf_hdr_t *)(slot_addr + slot_used);
    zero_bytes(h, AUDF_HDR_AREA);
    set_type((mlv_hdr_t *)h, 'A','U','D','F');
    h->blockSize = AUDF_BLOCK_BYTES;
    h->frameNumber = copied_blocks;
    h->frameSpace = AUDF_HDR_AREA - sizeof(mlv_audf_hdr_t);
    mlv_rec_set_rel_timestamp((mlv_hdr_t *)h, block_start);

    uint32_t *dst = (uint32_t *)(slot_addr + slot_used + AUDF_HDR_AREA);
    volatile const uint32_t *src = (volatile const uint32_t *)(PCM_RING_BASE +
        (index & (PCM_RING_BLOCKS - 1u)) * PCM_BLOCK_BYTES);
    for (uint32_t i = 0; i < PCM_BLOCK_BYTES / 4u; i++) dst[i] = src[i];

    slot_used += AUDF_BLOCK_BYTES;
    blocks_in_slot++;
    copied_blocks++;
    return 1;
}

static void queue_wavi(void)
{
    mlv_wavi_hdr_t *w = (mlv_wavi_hdr_t *)malloc(sizeof(mlv_wavi_hdr_t));
    if (!w)
    {
        session_error = 1;
        return;
    }
    zero_bytes(w, sizeof(*w));
    set_type((mlv_hdr_t *)w, 'W','A','V','I');
    w->blockSize = sizeof(*w);
    w->timestamp = 0;
    w->format = 1;
    w->channels = CHANNELS;
    w->samplingRate = SAMPLE_RATE;
    w->bytesPerSecond = BYTES_PER_SECOND;
    w->blockAlign = (BITS / 8u) * CHANNELS;
    w->bitsPerSample = BITS;
    mlv_rec_queue_block((mlv_hdr_t *)w);
}

static void write_log(void)
{
    FIO_RemoveFile(LOG_PATH);
    FILE *f = FIO_CreateFile(LOG_PATH);
    if (!f) return;
    int n = snprintf(logbuf, sizeof(logbuf),
        "RAW_AUD EOS 200D CURRENTDEV PREARM LIVE MLV AUDIO\n"
        "aproc_rc=%d called=%d latched=%d disabled=%d error=%d\n"
        "copied=%d queued=%d target=%d overrun=%d no_slot=%d slot_count=%d first_slot_size=%d\n"
        "stop_wait_timeout=%d final_null=%d block_cbr=%d final_header_audio=%d\n"
        "video_started_lo=%08x aproc_t0_lo=%08x stop_lo=%08x mlv_start_lo=%08x\n"
        "video_minus_aproc_us=%d first_block=%d safety_us=125000 audf_block=12256\n"
        "Aproc prearmed at PREPARING; WAVI at STARTING; AUDF in MLV Lite metadata slot(s).\n",
        aproc_rc, (int)aproc_called, (int)aproc_latched, (int)session_disabled, (int)session_error,
        (int)copied_blocks, (int)queued_blocks, (int)target_exclusive, (int)overrun_count,
        (int)no_slot_count, (int)slot_count, (int)slot_size_first,
        (int)stop_wait_timeout, (int)final_null_bytes, (int)block_callback_count,
        (int)final_header_audio_count, (uint32_t)video_started_abs, (uint32_t)aproc_t0,
        (uint32_t)stop_abs, (uint32_t)mlv_start_abs,
        (int)((int64_t)video_started_abs - (int64_t)aproc_t0), (int)first_block_at_or_after_video());
    if (n > 0) FIO_WriteFile(f, logbuf, (uint32_t)n);
    FIO_CloseFile(f);
}

static void worker(void *unused)
{
    (void)unused;
    while (!worker_exit)
    {
        if (prearm_requested && !session_disabled && !aproc_called && !capture_done)
        {
            prearm_requested = 0;
            if (!stop_requested && !stopped_seen)
            {
                int (*start)(void) = (int (*)(void))APROC_START_ADDR;
                aproc_rc = start();
                if (aproc_rc == 0)
                {
                    aproc_t0 = get_us_clock();
                    aproc_called = 1;
                    aproc_latched = 1;
                    arm_capture_if_ready();
                }
                else
                {
                    session_error = 1;
                    capture_done = 1;
                }
            }
        }

        if (capture_arm_requested)
            arm_capture_if_ready();

        if (capture_active && !session_error)
        {
            if (target_valid && next_block >= target_exclusive)
            {
                finalize_slot(copied_blocks > 0);
                queued_blocks = copied_blocks;
                capture_active = 0;
                capture_done = 1;
            }
            else
            {
                int r = capture_block_to_slot(next_block);
                if (r > 0)
                {
                    next_block++;
                    continue;
                }
                if (r < 0)
                {
                    finalize_slot(0);
                    capture_active = 0;
                    capture_done = 1;
                }
            }
        }

        if (stop_requested && !capture_active && !capture_done)
        {
            finalize_slot(0);
            capture_done = 1;
        }

        if (log_requested)
        {
            log_requested = 0;
            write_log();
        }

        msleep(capture_active ? 2 : 10);
    }
}

static void rec_cbr(uint32_t event, void *ctx, mlv_hdr_t *hdr)
{
    (void)ctx;

    if ((event & MLV_REC_EVENT_BLOCK) && is_type(hdr, 'M','L','V','I'))
    {
        mlv_file_hdr_t *fh = (mlv_file_hdr_t *)hdr;
        block_callback_count++;
        if (session_prepared && !session_disabled)
        {
            fh->audioClass = 1;
            fh->audioFrameCount = queued_blocks;
            fh->fileFlags |= 1u; /* audio metadata slot may be physically later than timestamps */
            final_header_audio_count = queued_blocks;
        }
    }

    if (event & MLV_REC_EVENT_PREPARING)
    {
        clear_session();
        if (aproc_latched)
        {
            session_disabled = 1;
            NotifyBox(2500, "RAW audio: power-cycle before next clip");
            return;
        }
        prearm_requested = 1;
    }

    if (event & MLV_REC_EVENT_STARTING)
    {
        if (session_disabled) return;
        if (session_error || capture_done)
        {
            session_disabled = 1;
            NotifyBox(2500, "RAW audio: prearm failed");
            return;
        }
        if (!reserve_slot())
        {
            session_disabled = 1;
            NotifyBox(2500, "RAW audio: no metadata slot");
            return;
        }
        session_prepared = 1;
        queue_wavi();
    }

    if ((event & MLV_REC_EVENT_STARTED) && session_prepared && !session_disabled)
    {
        mlv_hdr_t probe;
        zero_bytes(&probe, sizeof(probe));
        uint64_t now = get_us_clock();
        mlv_rec_set_rel_timestamp(&probe, now);
        mlv_start_abs = now - probe.timestamp;
        video_started_abs = now;
        capture_arm_requested = 1;
        arm_capture_if_ready();
    }

    if ((event & MLV_REC_EVENT_STOPPING) && session_prepared && !session_disabled)
    {
        stop_abs = get_us_clock();
        target_exclusive = safe_target_at_stop(stop_abs);
        target_valid = 1;
        stop_requested = 1;

        /* Same architecture used by generic mlv_snd: bounded wait here so the
         * metadata slot is released BEFORE MLV Lite drains its writing queue. */
        uint32_t loops = 30; /* <=300 ms */
        while (capture_active && loops--)
            msleep(10);
        if (capture_active)
            stop_wait_timeout = 1;
    }

    if ((event & MLV_REC_EVENT_STOPPED) && session_prepared && !session_disabled)
    {
        stopped_seen = 1;
        /* Abort-safe cleanup: if STOPPING did not finish the audio slot before
         * MLV Lite closed the file, never queue stale metadata after file close. */
        if (!capture_done)
        {
            if (current_slot >= 0) finalize_slot(0);
            capture_active = 0;
            capture_done = 1;
            session_error = 1;
        }
        log_requested = 1;
        if (!session_error && copied_blocks > 0 && !stop_wait_timeout)
            NotifyBox(2500, "Audio complete");
        else
            NotifyBox(3500, "RAW audio issue: e%d t%d", (int)session_error, (int)stop_wait_timeout);
    }
}

static unsigned int init(void)
{
    if (!is_camera("200D", "1.0.1")) return (unsigned int)-1;
    worker_exit = 0;
    aproc_latched = 0;
    clear_session();
    uint32_t events = MLV_REC_EVENT_PREPARING | MLV_REC_EVENT_STARTING |
                      MLV_REC_EVENT_STARTED | MLV_REC_EVENT_STOPPING |
                      MLV_REC_EVENT_STOPPED | MLV_REC_EVENT_BLOCK;
    if (!mlv_rec_register_cbr(events, rec_cbr, 0)) return (unsigned int)-1;
    if (!task_create("raw_aud", 0x1f, 0x4000, worker, 0))
    {
        mlv_rec_unregister_cbr(rec_cbr);
        return (unsigned int)-1;
    }
    return 0;
}

static unsigned int deinit(void)
{
    worker_exit = 1;
    mlv_rec_unregister_cbr(rec_cbr);
    msleep(20);
    if (current_slot >= 0) finalize_slot(0);
    /* Direct Aproc stop remains intentionally unproven and is not called. */
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(init)
    MODULE_DEINIT(deinit)
    MODULE_LONGNAME("EOS 200D RAW Audio")
MODULE_INFO_END()

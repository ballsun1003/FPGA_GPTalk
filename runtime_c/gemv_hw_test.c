#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

enum {
    LANES = 16,
    Q8_BLOCK_SIZE = 32,
    IN_FEATURES = 32,
    OUT_FEATURES = 3,
    PADDED_OUT_FEATURES = ((OUT_FEATURES + LANES - 1) / LANES) * LANES,
    ROW_PADDING = PADDED_OUT_FEATURES - OUT_FEATURES,
    BLOCKS_PER_ROW = IN_FEATURES / Q8_BLOCK_SIZE,
    INPUT_BYTES = IN_FEATURES * 2,
    SCALE_BYTES = LANES * BLOCKS_PER_ROW * 4,
    WEIGHT_BYTES = LANES * BLOCKS_PER_ROW * Q8_BLOCK_SIZE,
    PACKET_BYTES = SCALE_BYTES + WEIGHT_BYTES,
    TOTAL_WORDS = PACKET_BYTES / 4,
    SCALE_WORDS = SCALE_BYTES / 4,
    WEIGHT_WORDS = WEIGHT_BYTES / 4,
    FIRST_WEIGHT_WORD = SCALE_WORDS,
    EXPECTED_TLAST_WORD = TOTAL_WORDS - 1,
    EXPECTED_TLAST_WEIGHT_COL = Q8_BLOCK_SIZE - 1,
    EXPECTED_TLAST_LANE_BASE = LANES - 4,
    RESULT_WORDS = OUT_FEATURES * BLOCKS_PER_ROW,
    RESULT_BYTES = RESULT_WORDS * 4,
    SCALE_SHIFT = 20
};

#define STATIC_ASSERT(name, cond) typedef char static_assert_##name[(cond) ? 1 : -1]
STATIC_ASSERT(packet_bytes_is_576, PACKET_BYTES == 576);
STATIC_ASSERT(total_words_is_144, TOTAL_WORDS == 144);
STATIC_ASSERT(scale_words_is_16, SCALE_WORDS == 16);
STATIC_ASSERT(weight_words_is_128, WEIGHT_WORDS == 128);
STATIC_ASSERT(first_weight_word_is_16, FIRST_WEIGHT_WORD == 16);
STATIC_ASSERT(expected_tlast_word_is_143, EXPECTED_TLAST_WORD == 143);
STATIC_ASSERT(expected_tlast_weight_col_is_31, EXPECTED_TLAST_WEIGHT_COL == 31);
STATIC_ASSERT(expected_tlast_lane_base_is_12, EXPECTED_TLAST_LANE_BASE == 12);

enum {
    GEMV_VERSION = 0x00,
    GEMV_CONTROL = 0x04,
    GEMV_STATUS = 0x08,
    GEMV_ERROR_CODE = 0x0c,
    GEMV_MODE = 0x10,
    GEMV_SCALE_SHIFT = 0x14,
    GEMV_IN_FEATURES = 0x18,
    GEMV_OUT_FEATURES = 0x1c,
    GEMV_INPUT_BASE = 0x20,
    GEMV_WEIGHT_LENGTH = 0x24,
    GEMV_RESULT_LENGTH = 0x28,
    GEMV_START = 0x2c,
    GEMV_DONE = 0x30,
    GEMV_DEBUG_ROW = 0x34,
    GEMV_DEBUG_BLOCK = 0x38,
    GEMV_DEBUG_LANE = 0x3c,
    GEMV_DEBUG_OUT0 = 0x40,
    GEMV_DEBUG_OUT1 = 0x44,
    GEMV_DEBUG_OUT2 = 0x48,
    GEMV_DEBUG_IN_COUNT = 0x4c,
    GEMV_DEBUG_TLAST_COUNT = 0x50,
    GEMV_DEBUG_TLAST_TDATA = 0x54,
    GEMV_DEBUG_TLAST_TKEEP = 0x58,
    GEMV_DEBUG_SCALE0 = 0x5c,
    GEMV_DEBUG_SCALE1 = 0x60,
    GEMV_DEBUG_SCALE2 = 0x64,
    GEMV_DEBUG_BLOCK0 = 0x68,
    GEMV_DEBUG_BLOCK1 = 0x6c,
    GEMV_DEBUG_BLOCK2 = 0x70,
    GEMV_DEBUG_PRODUCT0_LO = 0x74,
    GEMV_DEBUG_PRODUCT0_HI = 0x78,
    GEMV_DEBUG_PRODUCT1_LO = 0x7c,
    GEMV_DEBUG_PRODUCT1_HI = 0x80,
    GEMV_DEBUG_PRODUCT2_LO = 0x84,
    GEMV_DEBUG_PRODUCT2_HI = 0x88,
    GEMV_DEBUG_SCALED0 = 0x8c,
    GEMV_DEBUG_SCALED1 = 0x90,
    GEMV_DEBUG_SCALED2 = 0x94,
    GEMV_DEBUG_ROW_ACC0 = 0x98,
    GEMV_DEBUG_ROW_ACC1 = 0x9c,
    GEMV_DEBUG_ROW_ACC2 = 0xa0,
    GEMV_BUILD_CONFIG = 0xa4
};

enum {
    DMA_MM2S_CR = 0x00,
    DMA_MM2S_SR = 0x04,
    DMA_MM2S_SA = 0x18,
    DMA_MM2S_SA_MSB = 0x1c,
    DMA_MM2S_LENGTH = 0x28,
    DMA_S2MM_CR = 0x30,
    DMA_S2MM_SR = 0x34,
    DMA_S2MM_DA = 0x48,
    DMA_S2MM_DA_MSB = 0x4c,
    DMA_S2MM_LENGTH = 0x58
};

#define DMA_CR_RUNSTOP 0x00000001u
#define DMA_CR_RESET   0x00000004u
#define DMA_SR_HALTED  0x00000001u
#define DMA_SR_IDLE    0x00000002u
#define DMA_SR_IOC_IRQ 0x00001000u
#define DMA_SR_ERR_IRQ 0x00004000u
#define DMA_SR_IRQ_CLR 0x00007000u
#define DMA_SR_ERR_MASK 0x00004770u

#define EXPECTED_GEMV_VERSION 0x000a0001u
#define EXPECTED_DMA_ADDR 0x40400000ull
#define EXPECTED_DMA_SIZE 0x00010000ull
#define EXPECTED_BRAM_ADDR 0x42000000ull
#define EXPECTED_BRAM_SIZE 0x00010000ull
#define EXPECTED_GEMV_ADDR 0x43ca0000ull
#define EXPECTED_GEMV_SIZE 0x00001000ull

#define DEFAULT_GOLDEN_DIR "/tmp/s05_fake_gemv"
#define DEFAULT_PHYS_BASE 0x3c000000ull
#define DEFAULT_PHYS_SIZE 0x04000000ull
#define DMA_MAP_BYTES 0x00002000ull
#define MM2S_BUF_OFF 0x00000000ull
#define S2MM_BUF_OFF 0x00001000ull

/* BEGIN EMBEDDED_FAKE_GEMV */

static const uint8_t EMBEDDED_INPUT_I16[] = {
    0xff, 0xff, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0xfc, 0xff, 0x04, 0x00,
    0xfd, 0xff, 0xfc, 0xff, 0xfe, 0xff, 0xfd, 0xff, 0x01, 0x00, 0xfc, 0xff,
    0x03, 0x00, 0x03, 0x00, 0x04, 0x00, 0xfc, 0xff, 0x02, 0x00, 0xfc, 0xff,
    0xfc, 0xff, 0xfe, 0xff, 0x00, 0x00, 0xfe, 0xff, 0x04, 0x00, 0xfd, 0xff,
    0x01, 0x00, 0xfd, 0xff, 0x00, 0x00, 0xfc, 0xff, 0x00, 0x00, 0x01, 0x00,
    0x02, 0x00, 0x03, 0x00,
};

static const uint8_t EMBEDDED_SCALE_Q_I32[] = {
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

static const uint8_t EMBEDDED_WEIGHT_Q8_FPGA_LAYOUT[] = {
    0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0xfe, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x02, 0xff, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0xfe, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x05, 0x02, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0xfe, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x02, 0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0xfe, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0a, 0xfe, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x02, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0xfe, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0d, 0x02, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0xfe, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0xfd, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0xfe, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x11, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0xfe, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x13, 0x02, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x14, 0xfe, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x02, 0xfe, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x16, 0xfe, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x17, 0x02, 0xfd, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xfe, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x19, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1a, 0xfe, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x02, 0xff, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1c, 0xfe, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1d, 0x02, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0xfe, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1f, 0x02, 0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x20, 0xfe, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t EMBEDDED_OUTPUT_SCALED_REF_I32[] = {
    0xd0, 0xff, 0xff, 0xff, 0x13, 0x00, 0x00, 0x00, 0xfa, 0xff, 0xff, 0xff,
};

static const uint8_t EMBEDDED_OUTPUT_BLOCK_ACC_REF_I32[] = {
    0x3f, 0xff, 0xff, 0xff, 0x26, 0x00, 0x00, 0x00, 0xce, 0xff, 0xff, 0xff,
};

/* END EMBEDDED_FAKE_GEMV */

struct uio_dev {
    char uio[256];
    char name[128];
    char dev_path[300];
    uint64_t addr;
    uint64_t size;
};

struct file_blob {
    uint8_t *data;
    size_t size;
};

static uint32_t g_packet_bytes = PACKET_BYTES;

struct perf_timing {
    uint64_t input_bram_write_ns;
    uint64_t mm2s_fill_ns;
    uint64_t s2mm_clear_ns;
    uint64_t dma_reset_ns;
    uint64_t gemv_control_clear_ns;
    uint64_t gemv_done_clear_ns;
    uint64_t gemv_config_regs_ns;
    uint64_t gemv_status_read_ns;
    uint64_t gemv_setup_ns;
    uint64_t s2mm_cr_run_ns;
    uint64_t s2mm_wait_running_ns;
    uint64_t s2mm_addr_write_ns;
    uint64_t s2mm_len_write_ns;
    uint64_t dma_s2mm_program_ns;
    uint64_t gemv_start_write_ns;
    uint64_t start_gap_sleep_ns;
    uint64_t mm2s_cr_run_ns;
    uint64_t mm2s_wait_running_ns;
    uint64_t mm2s_addr_write_ns;
    uint64_t mm2s_len_write_ns;
    uint64_t dma_mm2s_program_ns;
    uint64_t dma_setup_ns;
    uint64_t wait_poll_ns;
    uint64_t total_ns;
};

struct wait_timeline {
    uint64_t poll_start_ns;
    uint64_t first_gemv_done_ns;
    uint64_t first_gemv_error_ns;
    uint64_t first_mm2s_ioc_ns;
    uint64_t first_s2mm_ioc_ns;
    uint64_t first_mm2s_idle_ns;
    uint64_t first_s2mm_idle_ns;
    uint64_t done_ns;
    uint64_t polling_sleep_ns;
    uint32_t polling_loop_count;
    uint32_t final_mm2s_sr;
    uint32_t final_s2mm_sr;
    uint32_t final_gemv_status;
    uint32_t final_gemv_done;
    uint32_t final_gemv_error;
    uint32_t final_debug_row;
    uint32_t final_debug_block;
    uint32_t final_debug_lane;
};

struct bench_stats {
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t sum_ns;
    uint64_t samples[256];
    int count;
    int fail_count;
    int first_fail_iter;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void print_perf_timing(const char *mode_name, const struct perf_timing *t)
{
    printf("%s PERF host_ns input_bram_write=%" PRIu64
           " mm2s_fill=%" PRIu64
           " s2mm_clear=%" PRIu64
           " dma_reset=%" PRIu64
           " gemv_control_clear=%" PRIu64
           " gemv_done_clear=%" PRIu64
           " gemv_config_regs=%" PRIu64
           " gemv_status_read=%" PRIu64
           " gemv_setup=%" PRIu64
           " s2mm_cr_run=%" PRIu64
           " s2mm_wait_running=%" PRIu64
           " s2mm_addr_write=%" PRIu64
           " s2mm_len_write=%" PRIu64
           " dma_s2mm_program=%" PRIu64
           " gemv_start_write=%" PRIu64
           " start_gap_sleep=%" PRIu64
           " mm2s_cr_run=%" PRIu64
           " mm2s_wait_running=%" PRIu64
           " mm2s_addr_write=%" PRIu64
           " mm2s_len_write=%" PRIu64
           " dma_mm2s_program=%" PRIu64
           " dma_setup=%" PRIu64
           " wait_poll=%" PRIu64
           " total=%" PRIu64 "\n",
           mode_name,
           t->input_bram_write_ns,
           t->mm2s_fill_ns,
           t->s2mm_clear_ns,
           t->dma_reset_ns,
           t->gemv_control_clear_ns,
           t->gemv_done_clear_ns,
           t->gemv_config_regs_ns,
           t->gemv_status_read_ns,
           t->gemv_setup_ns,
           t->s2mm_cr_run_ns,
           t->s2mm_wait_running_ns,
           t->s2mm_addr_write_ns,
           t->s2mm_len_write_ns,
           t->dma_s2mm_program_ns,
           t->gemv_start_write_ns,
           t->start_gap_sleep_ns,
           t->mm2s_cr_run_ns,
           t->mm2s_wait_running_ns,
           t->mm2s_addr_write_ns,
           t->mm2s_len_write_ns,
           t->dma_mm2s_program_ns,
           t->dma_setup_ns,
           t->wait_poll_ns,
           t->total_ns);
}

static uint32_t g_poll_sleep_us = 1000;
static int g_repeat = 1;
static int g_no_dma_reset_after_first = 0;
static int g_quiet_pass = 0;
static int g_full_debug_status = 0;
static uint32_t g_expect_axis_width = 0;

static const char *poll_strategy_name(void)
{
    if (g_poll_sleep_us == 0) {
        return "busy";
    }
    if (g_poll_sleep_us == 1) {
        return "usleep1";
    }
    if (g_poll_sleep_us == 1000) {
        return "sleep1000";
    }
    return "custom";
}

static uint64_t delta_or_zero(uint64_t base, uint64_t value)
{
    return value ? value - base : 0;
}

static void print_wait_timeline(const char *mode_name, const struct wait_timeline *tl)
{
    printf("%s TIMELINE poll_strategy=%s poll_sleep_us=%u loops=%u sleep_ns=%" PRIu64
           " first_gemv_done_ns=%" PRIu64
           " first_gemv_error_ns=%" PRIu64
           " first_mm2s_ioc_ns=%" PRIu64
           " first_s2mm_ioc_ns=%" PRIu64
           " first_mm2s_idle_ns=%" PRIu64
           " first_s2mm_idle_ns=%" PRIu64
           " done_ns=%" PRIu64
           " final_mm2s_sr=0x%08" PRIx32
           " final_s2mm_sr=0x%08" PRIx32
           " final_gemv_status=0x%08" PRIx32
           " final_gemv_done=0x%08" PRIx32
           " final_gemv_error=0x%08" PRIx32
           " debug_row=%u debug_block=%u debug_lane=%u\n",
           mode_name, poll_strategy_name(), g_poll_sleep_us, tl->polling_loop_count,
           tl->polling_sleep_ns,
           delta_or_zero(tl->poll_start_ns, tl->first_gemv_done_ns),
           delta_or_zero(tl->poll_start_ns, tl->first_gemv_error_ns),
           delta_or_zero(tl->poll_start_ns, tl->first_mm2s_ioc_ns),
           delta_or_zero(tl->poll_start_ns, tl->first_s2mm_ioc_ns),
           delta_or_zero(tl->poll_start_ns, tl->first_mm2s_idle_ns),
           delta_or_zero(tl->poll_start_ns, tl->first_s2mm_idle_ns),
           delta_or_zero(tl->poll_start_ns, tl->done_ns),
           tl->final_mm2s_sr, tl->final_s2mm_sr, tl->final_gemv_status,
           tl->final_gemv_done, tl->final_gemv_error,
           tl->final_debug_row, tl->final_debug_block, tl->final_debug_lane);
}

static void sleep_us(long usec)
{
    struct timespec ts;
    ts.tv_sec = usec / 1000000L;
    ts.tv_nsec = (usec % 1000000L) * 1000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

static uint32_t rd32(volatile uint32_t *base, uint32_t off)
{
    return base[off / 4u];
}

static void wr32(volatile uint32_t *base, uint32_t off, uint32_t v)
{
    base[off / 4u] = v;
    __sync_synchronize();
}

static uint32_t le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t le32s(const uint8_t *p)
{
    return (int32_t)le32(p);
}

static int read_text(const char *path, char *buf, size_t len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = read(fd, buf, len - 1);
    close(fd);
    if (n < 0) {
        return -1;
    }
    buf[n] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) {
        *nl = '\0';
    }
    return 0;
}

static int read_u64_hex(const char *path, uint64_t *out)
{
    char buf[64];
    if (read_text(path, buf, sizeof(buf)) != 0) {
        return -1;
    }
    errno = 0;
    unsigned long long v = strtoull(buf, NULL, 0);
    if (errno) {
        return -1;
    }
    *out = (uint64_t)v;
    return 0;
}

static int load_uio(const char *uio, struct uio_dev *dev)
{
    char path[512];
    memset(dev, 0, sizeof(*dev));
    snprintf(dev->uio, sizeof(dev->uio), "%s", uio);
    snprintf(dev->dev_path, sizeof(dev->dev_path), "/dev/%s", uio);

    snprintf(path, sizeof(path), "/sys/class/uio/%s/name", uio);
    if (read_text(path, dev->name, sizeof(dev->name)) != 0) {
        return -1;
    }
    snprintf(path, sizeof(path), "/sys/class/uio/%s/maps/map0/addr", uio);
    if (read_u64_hex(path, &dev->addr) != 0) {
        return -1;
    }
    snprintf(path, sizeof(path), "/sys/class/uio/%s/maps/map0/size", uio);
    if (read_u64_hex(path, &dev->size) != 0) {
        return -1;
    }
    return 0;
}

static int find_uio_by_name(const char *name, struct uio_dev *out)
{
    DIR *dir = opendir("/sys/class/uio");
    if (!dir) {
        return -1;
    }
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "uio", 3) != 0) {
            continue;
        }
        struct uio_dev dev;
        if (load_uio(de->d_name, &dev) != 0) {
            continue;
        }
        if (strcmp(dev.name, name) == 0) {
            *out = dev;
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -1;
}

static void *map_uio(const struct uio_dev *dev, int *fd_out)
{
    int fd = open(dev->dev_path, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev->dev_path, strerror(errno));
        return MAP_FAILED;
    }
    void *p = mmap(NULL, (size_t)dev->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "mmap %s failed: %s\n", dev->dev_path, strerror(errno));
        close(fd);
        return MAP_FAILED;
    }
    *fd_out = fd;
    return p;
}

static void unmap_region(void *p, uint64_t size, int fd)
{
    if (p && p != MAP_FAILED) {
        munmap(p, (size_t)size);
    }
    if (fd >= 0) {
        close(fd);
    }
}

static int check_uio_addr(const struct uio_dev *dev, uint64_t addr, uint64_t size)
{
    printf("UIO name=%s dev=%s addr=0x%08" PRIx64 " size=0x%08" PRIx64 "\n",
           dev->name, dev->dev_path, dev->addr, dev->size);
    if (dev->addr != addr || dev->size != size) {
        fprintf(stderr,
                "address map mismatch for %s: got 0x%08" PRIx64 "/0x%08" PRIx64
                " expected 0x%08" PRIx64 "/0x%08" PRIx64 "\n",
                dev->name, dev->addr, dev->size, addr, size);
        return 1;
    }
    return 0;
}

static int load_file_exact(const char *dir, const char *name, size_t expected, struct file_blob *out)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
        return 1;
    }
    uint8_t *buf = (uint8_t *)calloc(1, expected);
    if (!buf) {
        fprintf(stderr, "calloc %zu failed\n", expected);
        close(fd);
        return 1;
    }
    size_t off = 0;
    while (off < expected) {
        ssize_t n = read(fd, buf + off, expected - off);
        if (n < 0) {
            fprintf(stderr, "read %s failed: %s\n", path, strerror(errno));
            free(buf);
            close(fd);
            return 1;
        }
        if (n == 0) {
            break;
        }
        off += (size_t)n;
    }
    close(fd);
    if (off != expected) {
        fprintf(stderr, "short read %s got=%zu expected=%zu\n", path, off, expected);
        free(buf);
        return 1;
    }
    out->data = buf;
    out->size = expected;
    printf("LOAD %s bytes=%zu\n", path, expected);
    return 0;
}

static void free_blob(struct file_blob *b)
{
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

static int copy_embedded_blob(const uint8_t *src, size_t size, struct file_blob *out)
{
    uint8_t *buf = (uint8_t *)calloc(1, size);
    if (!buf) {
        fprintf(stderr, "calloc embedded blob %zu failed\n", size);
        return 1;
    }
    memcpy(buf, src, size);
    out->data = buf;
    out->size = size;
    return 0;
}

static int load_embedded_golden(struct file_blob *input,
                                struct file_blob *scale,
                                struct file_blob *weight,
                                struct file_blob *scaled_ref,
                                struct file_blob *block_ref)
{
    int fail = 0;
    fail |= copy_embedded_blob(EMBEDDED_INPUT_I16, sizeof(EMBEDDED_INPUT_I16), input);
    fail |= copy_embedded_blob(EMBEDDED_SCALE_Q_I32, sizeof(EMBEDDED_SCALE_Q_I32), scale);
    fail |= copy_embedded_blob(EMBEDDED_WEIGHT_Q8_FPGA_LAYOUT, sizeof(EMBEDDED_WEIGHT_Q8_FPGA_LAYOUT), weight);
    fail |= copy_embedded_blob(EMBEDDED_OUTPUT_SCALED_REF_I32, sizeof(EMBEDDED_OUTPUT_SCALED_REF_I32), scaled_ref);
    fail |= copy_embedded_blob(EMBEDDED_OUTPUT_BLOCK_ACC_REF_I32, sizeof(EMBEDDED_OUTPUT_BLOCK_ACC_REF_I32), block_ref);
    if (!fail) {
        printf("LOAD builtin fake_gemv bytes input=%zu scale=%zu weight=%zu scaled_ref=%zu block_ref=%zu\n",
               sizeof(EMBEDDED_INPUT_I16), sizeof(EMBEDDED_SCALE_Q_I32),
               sizeof(EMBEDDED_WEIGHT_Q8_FPGA_LAYOUT), sizeof(EMBEDDED_OUTPUT_SCALED_REF_I32),
               sizeof(EMBEDDED_OUTPUT_BLOCK_ACC_REF_I32));
    }
    return fail;
}

static int build_packet(uint8_t *packet, const struct file_blob *scale, const struct file_blob *weight)
{
    if (scale->size != SCALE_BYTES || weight->size != WEIGHT_BYTES) {
        return 1;
    }
    memcpy(packet, scale->data, SCALE_BYTES);
    memcpy(packet + SCALE_BYTES, weight->data, WEIGHT_BYTES);
    return 0;
}

static int validate_packet_contract(const uint8_t *packet,
                                    const struct file_blob *scale,
                                    const struct file_blob *weight)
{
    int fail = 0;
    if (PACKET_BYTES != 576 || g_packet_bytes != PACKET_BYTES ||
        TOTAL_WORDS != 144 || SCALE_WORDS != 16 || WEIGHT_WORDS != 128 ||
        EXPECTED_TLAST_WORD != 143 ||
        EXPECTED_TLAST_WEIGHT_COL != 31 ||
        EXPECTED_TLAST_LANE_BASE != 12) {
        fprintf(stderr,
                "S05 packet contract FAIL: packet_bytes=%u dma_mm2s_len=%u total_words=%u "
                "scale_words=%u weight_words=%u expected_tlast_word=%u "
                "expected_tlast_weight_col=%u expected_tlast_lane_base=%u\n",
                (unsigned)PACKET_BYTES, (unsigned)g_packet_bytes,
                (unsigned)TOTAL_WORDS, (unsigned)SCALE_WORDS,
                (unsigned)WEIGHT_WORDS, (unsigned)EXPECTED_TLAST_WORD,
                (unsigned)EXPECTED_TLAST_WEIGHT_COL,
                (unsigned)EXPECTED_TLAST_LANE_BASE);
        fail = 1;
    }
    if (scale->size != SCALE_BYTES || weight->size != WEIGHT_BYTES) {
        fprintf(stderr, "S05 packet contract FAIL: scale/weight size mismatch\n");
        fail = 1;
    }

    for (int lane = OUT_FEATURES; lane < PADDED_OUT_FEATURES; ++lane) {
        if (le32(scale->data + lane * 4) != 0) {
            fprintf(stderr, "S05 packet contract FAIL: padded scale lane=%d nonzero tdata=0x%08x\n",
                    lane, le32(scale->data + lane * 4));
            fail = 1;
        }
    }
    for (int col = 0; col < Q8_BLOCK_SIZE; ++col) {
        for (int lane = OUT_FEATURES; lane < PADDED_OUT_FEATURES; ++lane) {
            size_t off = (size_t)col * LANES + (size_t)lane;
            if (weight->data[off] != 0) {
                fprintf(stderr,
                        "S05 packet contract FAIL: padded weight col=%d lane=%d nonzero byte=0x%02x\n",
                        col, lane, weight->data[off]);
                fail = 1;
            }
        }
    }

    if (packet != NULL) {
        uint32_t first_weight = le32(packet + FIRST_WEIGHT_WORD * 4);
        uint32_t last_word = le32(packet + EXPECTED_TLAST_WORD * 4);
        printf("S05 packet contract PASS: total_words=%u scale_words=%u weight_words=%u "
               "first_weight_word=%u expected_tlast_word=%u expected_tlast_weight_col=%u "
               "expected_tlast_lane_base=%u first_weight_tdata=0x%08x last_tdata=0x%08x\n",
               (unsigned)TOTAL_WORDS, (unsigned)SCALE_WORDS,
               (unsigned)WEIGHT_WORDS, (unsigned)FIRST_WEIGHT_WORD,
               (unsigned)EXPECTED_TLAST_WORD,
               (unsigned)EXPECTED_TLAST_WEIGHT_COL,
               (unsigned)EXPECTED_TLAST_LANE_BASE,
               first_weight, last_word);
    }
    return fail;
}

static void write_input_bram(volatile uint32_t *bram, const uint8_t *input)
{
    for (int i = 0; i < IN_FEATURES / 2; ++i) {
        wr32(bram, (uint32_t)(i * 4), le32(input + i * 4));
    }
}

static int verify_input_bram(volatile uint32_t *bram, const uint8_t *input)
{
    int fail = 0;
    for (int i = 0; i < IN_FEATURES / 2; ++i) {
        uint32_t expected = le32(input + i * 4);
        uint32_t got = rd32(bram, (uint32_t)(i * 4));
        if (got != expected) {
            fprintf(stderr,
                    "input BRAM readback mismatch word=%d got=0x%08" PRIx32
                    " expected=0x%08" PRIx32 "\n",
                    i, got, expected);
            fail = 1;
        }
    }
    if (!fail && !g_quiet_pass) {
        printf("input BRAM readback PASS words=%u\n", (unsigned)(IN_FEATURES / 2));
    }
    return fail;
}

static int wait_dma_reset_clear(volatile uint32_t *dma, uint32_t cr_off)
{
    for (int i = 0; i < 1000; ++i) {
        if ((rd32(dma, cr_off) & DMA_CR_RESET) == 0) {
            return 0;
        }
        sleep_us(1000);
    }
    return 1;
}

static void print_dma_status(const char *tag, uint32_t sr)
{
    printf("%s SR=0x%08x halted=%u idle=%u ioc=%u err_irq=%u err_detail=0x%03x\n",
           tag, sr, !!(sr & DMA_SR_HALTED), !!(sr & DMA_SR_IDLE),
           !!(sr & DMA_SR_IOC_IRQ), !!(sr & DMA_SR_ERR_IRQ), sr & 0x770u);
}

static int dma_reset(volatile uint32_t *dma)
{
    wr32(dma, DMA_MM2S_CR, DMA_CR_RESET);
    wr32(dma, DMA_S2MM_CR, DMA_CR_RESET);
    if (wait_dma_reset_clear(dma, DMA_MM2S_CR) != 0 ||
        wait_dma_reset_clear(dma, DMA_S2MM_CR) != 0) {
        fprintf(stderr, "AXI DMA reset timeout\n");
        return 1;
    }
    wr32(dma, DMA_MM2S_SR, DMA_SR_IRQ_CLR);
    wr32(dma, DMA_S2MM_SR, DMA_SR_IRQ_CLR);
    uint32_t mm2s = rd32(dma, DMA_MM2S_SR);
    uint32_t s2mm = rd32(dma, DMA_S2MM_SR);
    if (!g_quiet_pass) {
        print_dma_status("AXI_DMA reset MM2S", mm2s);
        print_dma_status("AXI_DMA reset S2MM", s2mm);
    }
    if ((mm2s & DMA_SR_ERR_MASK) || (s2mm & DMA_SR_ERR_MASK)) {
        fprintf(stderr, "AXI DMA has error status after reset\n");
        return 1;
    }
    return 0;
}

static int dma_reuse_prepare(volatile uint32_t *dma)
{
    wr32(dma, DMA_MM2S_SR, DMA_SR_IRQ_CLR);
    wr32(dma, DMA_S2MM_SR, DMA_SR_IRQ_CLR);
    uint32_t mm2s = rd32(dma, DMA_MM2S_SR);
    uint32_t s2mm = rd32(dma, DMA_S2MM_SR);
    if (!g_quiet_pass) {
        print_dma_status("AXI_DMA reuse MM2S", mm2s);
        print_dma_status("AXI_DMA reuse S2MM", s2mm);
    }
    if ((mm2s & DMA_SR_ERR_MASK) || (s2mm & DMA_SR_ERR_MASK)) {
        fprintf(stderr, "AXI DMA has error status before reuse\n");
        print_dma_status("AXI_DMA reuse error MM2S", mm2s);
        print_dma_status("AXI_DMA reuse error S2MM", s2mm);
        return 1;
    }
    return 0;
}

static int dma_status_error(uint32_t sr)
{
    return (sr & DMA_SR_ERR_MASK) != 0;
}

static int dma_status_done(uint32_t sr)
{
    return (sr & DMA_SR_IOC_IRQ) || ((sr & DMA_SR_IDLE) && !(sr & DMA_SR_HALTED));
}

static int64_t i64_from_u32_words(uint32_t lo, uint32_t hi)
{
    return (int64_t)(((uint64_t)hi << 32) | lo);
}

static int wait_dma_running(volatile uint32_t *dma, uint32_t sr_off, const char *tag)
{
    for (int i = 0; i < 1000; ++i) {
        uint32_t sr = rd32(dma, sr_off);
        if ((sr & DMA_SR_HALTED) == 0) {
            if (!g_quiet_pass) {
                print_dma_status(tag, sr);
            }
            return 0;
        }
        sleep_us(1000);
    }
    fprintf(stderr, "%s did not leave halted state\n", tag);
    print_dma_status(tag, rd32(dma, sr_off));
    return 1;
}

static void print_gemv_status(volatile uint32_t *gemv, const char *tag)
{
    uint32_t st = rd32(gemv, GEMV_STATUS);
    uint32_t done = rd32(gemv, GEMV_DONE);
    uint32_t err = rd32(gemv, GEMV_ERROR_CODE);
    uint32_t p0_lo = rd32(gemv, GEMV_DEBUG_PRODUCT0_LO);
    uint32_t p0_hi = rd32(gemv, GEMV_DEBUG_PRODUCT0_HI);
    uint32_t p1_lo = rd32(gemv, GEMV_DEBUG_PRODUCT1_LO);
    uint32_t p1_hi = rd32(gemv, GEMV_DEBUG_PRODUCT1_HI);
    uint32_t p2_lo = rd32(gemv, GEMV_DEBUG_PRODUCT2_LO);
    uint32_t p2_hi = rd32(gemv, GEMV_DEBUG_PRODUCT2_HI);
    printf("%s STATUS=0x%08x busy=%u done_sticky=%u error=%u stream_ready=%u result_valid=%u backpressure=%u start_busy=%u mode=%u DONE=0x%08x ERROR_CODE=0x%08x dbg(row=%u block=%u lane=%u out0=%" PRId32 "/0x%08" PRIx32 " out1=%" PRId32 "/0x%08" PRIx32 " out2=%" PRId32 "/0x%08" PRIx32 " in_count=%u tlast_count=%u tlast_tdata=0x%08" PRIx32 " tlast_tkeep=0x%08" PRIx32 ")\n",
           tag, st, st & 1u, (st >> 1) & 1u, (st >> 2) & 1u,
           (st >> 3) & 1u, (st >> 4) & 1u, (st >> 5) & 1u,
           (st >> 6) & 1u, (st >> 7) & 1u, done, err,
           rd32(gemv, GEMV_DEBUG_ROW), rd32(gemv, GEMV_DEBUG_BLOCK),
           rd32(gemv, GEMV_DEBUG_LANE),
           (int32_t)rd32(gemv, GEMV_DEBUG_OUT0), rd32(gemv, GEMV_DEBUG_OUT0),
           (int32_t)rd32(gemv, GEMV_DEBUG_OUT1), rd32(gemv, GEMV_DEBUG_OUT1),
           (int32_t)rd32(gemv, GEMV_DEBUG_OUT2), rd32(gemv, GEMV_DEBUG_OUT2),
           rd32(gemv, GEMV_DEBUG_IN_COUNT), rd32(gemv, GEMV_DEBUG_TLAST_COUNT),
           rd32(gemv, GEMV_DEBUG_TLAST_TDATA), rd32(gemv, GEMV_DEBUG_TLAST_TKEEP));
    printf("%s SCALE_DBG scale=[%" PRId32 ",%" PRId32 ",%" PRId32 "] block=[%" PRId32 ",%" PRId32 ",%" PRId32 "] product=[%" PRId64 ",%" PRId64 ",%" PRId64 "] scaled=[%" PRId32 ",%" PRId32 ",%" PRId32 "] row_acc=[%" PRId32 ",%" PRId32 ",%" PRId32 "]\n",
           tag,
           (int32_t)rd32(gemv, GEMV_DEBUG_SCALE0),
           (int32_t)rd32(gemv, GEMV_DEBUG_SCALE1),
           (int32_t)rd32(gemv, GEMV_DEBUG_SCALE2),
           (int32_t)rd32(gemv, GEMV_DEBUG_BLOCK0),
           (int32_t)rd32(gemv, GEMV_DEBUG_BLOCK1),
           (int32_t)rd32(gemv, GEMV_DEBUG_BLOCK2),
           i64_from_u32_words(p0_lo, p0_hi),
           i64_from_u32_words(p1_lo, p1_hi),
           i64_from_u32_words(p2_lo, p2_hi),
           (int32_t)rd32(gemv, GEMV_DEBUG_SCALED0),
           (int32_t)rd32(gemv, GEMV_DEBUG_SCALED1),
           (int32_t)rd32(gemv, GEMV_DEBUG_SCALED2),
           (int32_t)rd32(gemv, GEMV_DEBUG_ROW_ACC0),
           (int32_t)rd32(gemv, GEMV_DEBUG_ROW_ACC1),
           (int32_t)rd32(gemv, GEMV_DEBUG_ROW_ACC2));
}

static void print_gemv_status_light(volatile uint32_t *gemv, const char *tag)
{
    uint32_t st = rd32(gemv, GEMV_STATUS);
    uint32_t done = rd32(gemv, GEMV_DONE);
    uint32_t err = rd32(gemv, GEMV_ERROR_CODE);
    printf("%s STATUS=0x%08x busy=%u done_sticky=%u error=%u stream_ready=%u "
           "result_valid=%u backpressure=%u start_busy=%u mode=%u DONE=0x%08x ERROR_CODE=0x%08x\n",
           tag, st, st & 1u, (st >> 1) & 1u, (st >> 2) & 1u,
           (st >> 3) & 1u, (st >> 4) & 1u, (st >> 5) & 1u,
           (st >> 6) & 1u, (st >> 7) & 1u, done, err);
}

static void print_gemv_status_sampled(const char *tag, uint32_t st, uint32_t done, uint32_t err)
{
    printf("%s STATUS=0x%08x busy=%u done_sticky=%u error=%u stream_ready=%u "
           "result_valid=%u backpressure=%u start_busy=%u mode=%u DONE=0x%08x ERROR_CODE=0x%08x\n",
           tag, st, st & 1u, (st >> 1) & 1u, (st >> 2) & 1u,
           (st >> 3) & 1u, (st >> 4) & 1u, (st >> 5) & 1u,
           (st >> 6) & 1u, (st >> 7) & 1u, done, err);
}

static void fill_timeline_final(volatile uint32_t *dma,
                                volatile uint32_t *gemv,
                                struct wait_timeline *tl,
                                uint32_t mm2s,
                                uint32_t s2mm,
                                uint32_t st,
                                uint32_t done,
                                uint32_t err,
                                int capture_debug)
{
    tl->final_mm2s_sr = mm2s;
    tl->final_s2mm_sr = s2mm;
    tl->final_gemv_status = st;
    tl->final_gemv_done = done;
    tl->final_gemv_error = err;
    if (capture_debug) {
        tl->final_debug_row = rd32(gemv, GEMV_DEBUG_ROW);
        tl->final_debug_block = rd32(gemv, GEMV_DEBUG_BLOCK);
        tl->final_debug_lane = rd32(gemv, GEMV_DEBUG_LANE);
    } else {
        tl->final_debug_row = UINT32_MAX;
        tl->final_debug_block = UINT32_MAX;
        tl->final_debug_lane = UINT32_MAX;
    }
    (void)dma;
}

static int wait_transfer_done(volatile uint32_t *dma,
                              volatile uint32_t *gemv,
                              const char *mode_name,
                              struct wait_timeline *tl)
{
    uint32_t mm2s = 0;
    uint32_t s2mm = 0;
    uint32_t st = 0;
    uint32_t done = 0;
    uint32_t err = 0;

    memset(tl, 0, sizeof(*tl));
    tl->poll_start_ns = now_ns();
    const uint64_t timeout_ns = 5000000000ull;
    const uint32_t max_loops = g_poll_sleep_us == 0 ? 20000000u : 5000000u;

    for (uint32_t i = 0; i < max_loops; ++i) {
        mm2s = rd32(dma, DMA_MM2S_SR);
        s2mm = rd32(dma, DMA_S2MM_SR);
        st = rd32(gemv, GEMV_STATUS);
        done = rd32(gemv, GEMV_DONE);
        err = rd32(gemv, GEMV_ERROR_CODE);
        uint64_t now = now_ns();
        tl->polling_loop_count = i + 1u;

        if (!tl->first_mm2s_ioc_ns && (mm2s & DMA_SR_IOC_IRQ)) {
            tl->first_mm2s_ioc_ns = now;
        }
        if (!tl->first_s2mm_ioc_ns && (s2mm & DMA_SR_IOC_IRQ)) {
            tl->first_s2mm_ioc_ns = now;
        }
        if (!tl->first_mm2s_idle_ns && ((mm2s & DMA_SR_IDLE) && !(mm2s & DMA_SR_HALTED))) {
            tl->first_mm2s_idle_ns = now;
        }
        if (!tl->first_s2mm_idle_ns && ((s2mm & DMA_SR_IDLE) && !(s2mm & DMA_SR_HALTED))) {
            tl->first_s2mm_idle_ns = now;
        }
        if (!tl->first_gemv_done_ns && ((st & (1u << 1)) || (done & 1u))) {
            tl->first_gemv_done_ns = now;
        }
        if (!tl->first_gemv_error_ns && ((st & (1u << 2)) || err)) {
            tl->first_gemv_error_ns = now;
        }

        if (dma_status_error(mm2s) || dma_status_error(s2mm) || (st & (1u << 2)) || err) {
            tl->done_ns = now;
            fill_timeline_final(dma, gemv, tl, mm2s, s2mm, st, done, err, 1);
            fprintf(stderr, "%s transfer error while polling\n", mode_name);
            print_dma_status("AXI_DMA error MM2S", mm2s);
            print_dma_status("AXI_DMA error S2MM", s2mm);
            print_gemv_status(gemv, "GEMV error");
            print_wait_timeline(mode_name, tl);
            return 1;
        }

        if (dma_status_done(mm2s) && dma_status_done(s2mm) &&
            ((st & (1u << 1)) || (done & 1u))) {
            tl->done_ns = now;
            fill_timeline_final(dma, gemv, tl, mm2s, s2mm, st, done, err, g_full_debug_status);
            if (!g_quiet_pass) {
                print_dma_status("AXI_DMA done MM2S", mm2s);
                print_dma_status("AXI_DMA done S2MM", s2mm);
                if (g_full_debug_status) {
                    print_gemv_status(gemv, "GEMV done");
                } else {
                    print_gemv_status_sampled("GEMV done", st, done, err);
                }
                print_wait_timeline(mode_name, tl);
            }
            return 0;
        }
        if (now - tl->poll_start_ns >= timeout_ns) {
            break;
        }
        if (g_poll_sleep_us != 0) {
            sleep_us((long)g_poll_sleep_us);
            tl->polling_sleep_ns += (uint64_t)g_poll_sleep_us * 1000ull;
        }
    }

    tl->done_ns = now_ns();
    fill_timeline_final(dma, gemv, tl, mm2s, s2mm, st, done, err, 1);
    fprintf(stderr, "%s timeout waiting for DMA/GEMV completion\n", mode_name);
    print_dma_status("AXI_DMA timeout MM2S", mm2s);
    print_dma_status("AXI_DMA timeout S2MM", s2mm);
    print_gemv_status(gemv, "GEMV timeout");
    print_wait_timeline(mode_name, tl);
    return 1;
}

static int compare_output(const char *mode_name, volatile uint32_t *out, const struct file_blob *ref)
{
    int fail = 0;
    for (int i = 0; i < RESULT_WORDS; ++i) {
        uint32_t got_u = out[i];
        int32_t expected = le32s(ref->data + i * 4);
        uint32_t expected_u = (uint32_t)expected;
        int32_t got = (int32_t)got_u;
        if (!g_quiet_pass || got != expected) {
            printf("%s result[%d] got=%" PRId32 " got_hex=0x%08" PRIx32
                   " expected=%" PRId32 " expected_hex=0x%08" PRIx32 "\n",
                   mode_name, i, got, got_u, expected, expected_u);
        }
        if (got != expected) {
            fail = 1;
        }
    }
    return fail;
}

static int run_mode(int mode,
                    const char *mode_name,
                    volatile uint32_t *dma,
                    volatile uint32_t *gemv,
                    volatile uint32_t *bram,
                    volatile uint32_t *packet_buf,
                    volatile uint32_t *result_buf,
                    uint64_t packet_phys,
                    uint64_t result_phys,
                    const uint8_t *packet,
                    const struct file_blob *input,
                    const struct file_blob *ref,
                    int do_dma_reset,
                    struct perf_timing *timing_out,
                    struct wait_timeline *timeline_out)
{
    struct perf_timing timing;
    struct wait_timeline timeline;
    memset(&timing, 0, sizeof(timing));
    memset(&timeline, 0, sizeof(timeline));
    uint64_t t_total0 = now_ns();
    uint64_t t0;
    uint64_t t1;

    if (!g_quiet_pass) {
        printf("\n[RUN] %s\n", mode_name);
    }

    t0 = now_ns();
    write_input_bram(bram, input->data);
    if (verify_input_bram(bram, input->data) != 0) {
        return 1;
    }
    t1 = now_ns();
    timing.input_bram_write_ns = t1 - t0;

    t0 = now_ns();
    for (int i = 0; i < PACKET_BYTES / 4; ++i) {
        wr32(packet_buf, (uint32_t)(i * 4), le32(packet + i * 4));
    }
    t1 = now_ns();
    timing.mm2s_fill_ns = t1 - t0;

    t0 = now_ns();
    for (int i = 0; i < RESULT_BYTES / 4; ++i) {
        wr32(result_buf, (uint32_t)(i * 4), 0xcdcdcdcdu);
    }
    __sync_synchronize();
    t1 = now_ns();
    timing.s2mm_clear_ns = t1 - t0;

    t0 = now_ns();
    int dma_prep_fail = do_dma_reset ? dma_reset(dma) : dma_reuse_prepare(dma);
    if (dma_prep_fail != 0) {
        return 1;
    }
    t1 = now_ns();
    timing.dma_reset_ns = t1 - t0;

    uint64_t t_gemv_setup0 = now_ns();
    t0 = now_ns();
    wr32(gemv, GEMV_CONTROL, 0x2u);
    t1 = now_ns();
    timing.gemv_control_clear_ns = t1 - t0;

    t0 = now_ns();
    wr32(gemv, GEMV_DONE, 0x1u);
    t1 = now_ns();
    timing.gemv_done_clear_ns = t1 - t0;

    t0 = now_ns();
    wr32(gemv, GEMV_MODE, (uint32_t)mode);
    wr32(gemv, GEMV_SCALE_SHIFT, SCALE_SHIFT);
    wr32(gemv, GEMV_IN_FEATURES, IN_FEATURES);
    wr32(gemv, GEMV_OUT_FEATURES, OUT_FEATURES);
    wr32(gemv, GEMV_INPUT_BASE, EXPECTED_BRAM_ADDR);
    wr32(gemv, GEMV_WEIGHT_LENGTH, g_packet_bytes);
    wr32(gemv, GEMV_RESULT_LENGTH, RESULT_BYTES);
    t1 = now_ns();
    timing.gemv_config_regs_ns = t1 - t0;

    t0 = now_ns();
    if (!g_quiet_pass) {
        print_gemv_status_light(gemv, "GEMV configured");
    }
    t1 = now_ns();
    timing.gemv_status_read_ns = t1 - t0;
    timing.gemv_setup_ns = t1 - t_gemv_setup0;

    uint64_t t_dma_setup0 = now_ns();
    t0 = now_ns();
    wr32(dma, DMA_S2MM_CR, DMA_CR_RUNSTOP);
    t1 = now_ns();
    timing.s2mm_cr_run_ns = t1 - t0;

    t0 = now_ns();
    if (wait_dma_running(dma, DMA_S2MM_SR, "AXI_DMA armed S2MM") != 0) {
        return 1;
    }
    t1 = now_ns();
    timing.s2mm_wait_running_ns = t1 - t0;

    t0 = now_ns();
    wr32(dma, DMA_S2MM_DA, (uint32_t)result_phys);
    wr32(dma, DMA_S2MM_DA_MSB, 0u);
    t1 = now_ns();
    timing.s2mm_addr_write_ns = t1 - t0;

    t0 = now_ns();
    wr32(dma, DMA_S2MM_LENGTH, RESULT_BYTES);
    t1 = now_ns();
    timing.s2mm_len_write_ns = t1 - t0;
    timing.dma_s2mm_program_ns = t1 - t0;
    timing.dma_s2mm_program_ns = timing.s2mm_cr_run_ns +
                                  timing.s2mm_wait_running_ns +
                                  timing.s2mm_addr_write_ns +
                                  timing.s2mm_len_write_ns;

    t0 = now_ns();
    wr32(gemv, GEMV_START, 0x1u);
    t1 = now_ns();
    timing.gemv_start_write_ns = t1 - t0;

    t0 = now_ns();
    sleep_us(100);
    t1 = now_ns();
    timing.start_gap_sleep_ns = t1 - t0;

    t0 = now_ns();
    wr32(dma, DMA_MM2S_CR, DMA_CR_RUNSTOP);
    t1 = now_ns();
    timing.mm2s_cr_run_ns = t1 - t0;

    t0 = now_ns();
    if (wait_dma_running(dma, DMA_MM2S_SR, "AXI_DMA armed MM2S") != 0) {
        return 1;
    }
    t1 = now_ns();
    timing.mm2s_wait_running_ns = t1 - t0;

    t0 = now_ns();
    wr32(dma, DMA_MM2S_SA, (uint32_t)packet_phys);
    wr32(dma, DMA_MM2S_SA_MSB, 0u);
    t1 = now_ns();
    timing.mm2s_addr_write_ns = t1 - t0;

    t0 = now_ns();
    wr32(dma, DMA_MM2S_LENGTH, g_packet_bytes);
    t1 = now_ns();
    timing.mm2s_len_write_ns = t1 - t0;
    timing.dma_mm2s_program_ns = t1 - t0;
    timing.dma_mm2s_program_ns = timing.mm2s_cr_run_ns +
                                  timing.mm2s_wait_running_ns +
                                  timing.mm2s_addr_write_ns +
                                  timing.mm2s_len_write_ns;
    if (!g_quiet_pass) {
        printf("DMA programmed mm2s_len=%u s2mm_len=%u mm2s_sa=0x%08" PRIx64 " s2mm_da=0x%08" PRIx64 "\n",
               (unsigned)g_packet_bytes, (unsigned)RESULT_BYTES, packet_phys, result_phys);
    }
    timing.dma_setup_ns = t1 - t_dma_setup0;

    t0 = now_ns();
    if (wait_transfer_done(dma, gemv, mode_name, &timeline) != 0) {
        return 1;
    }
    t1 = now_ns();
    timing.wait_poll_ns = delta_or_zero(timeline.poll_start_ns, timeline.done_ns);
    timing.total_ns = t1 - t_total0;
    if (!g_quiet_pass) {
        print_perf_timing(mode_name, &timing);
    }
    if (timing_out) {
        *timing_out = timing;
    }
    if (timeline_out) {
        *timeline_out = timeline;
    }

    __sync_synchronize();
    if (compare_output(mode_name, result_buf, ref) != 0) {
        printf("%s: FAIL\n", mode_name);
        return 1;
    }

    if (!g_quiet_pass) {
        printf("%s: PASS\n", mode_name);
    }
    return 0;
}

static int parse_u64_arg(const char *name, const char *value, uint64_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 0);
    if (errno || end == value || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, value);
        return 1;
    }
    *out = (uint64_t)parsed;
    return 0;
}

static int parse_u32_arg(const char *name, const char *value, uint32_t *out)
{
    uint64_t parsed = 0;
    if (parse_u64_arg(name, value, &parsed) != 0) {
        return 1;
    }
    if (parsed > UINT32_MAX) {
        fprintf(stderr, "%s is too large: %s\n", name, value);
        return 1;
    }
    *out = (uint32_t)parsed;
    return 0;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

static void bench_stats_init(struct bench_stats *s)
{
    memset(s, 0, sizeof(*s));
    s->min_ns = UINT64_MAX;
    s->first_fail_iter = -1;
}

static void bench_stats_add(struct bench_stats *s, uint64_t total_ns, int fail, int iter)
{
    if (s->count < (int)(sizeof(s->samples) / sizeof(s->samples[0]))) {
        s->samples[s->count] = total_ns;
    }
    if (total_ns < s->min_ns) {
        s->min_ns = total_ns;
    }
    if (total_ns > s->max_ns) {
        s->max_ns = total_ns;
    }
    s->sum_ns += total_ns;
    s->count++;
    if (fail) {
        s->fail_count++;
        if (s->first_fail_iter < 0) {
            s->first_fail_iter = iter;
        }
    }
}

static uint64_t bench_percentile_ns(const struct bench_stats *s, int pct)
{
    int n = s->count;
    int max_samples = (int)(sizeof(s->samples) / sizeof(s->samples[0]));
    if (n > max_samples) {
        n = max_samples;
    }
    if (n <= 0) {
        return 0;
    }
    uint64_t tmp[256];
    memcpy(tmp, s->samples, (size_t)n * sizeof(tmp[0]));
    qsort(tmp, (size_t)n, sizeof(tmp[0]), cmp_u64);
    int idx = (int)(((int64_t)(n - 1) * pct + 99) / 100);
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= n) {
        idx = n - 1;
    }
    return tmp[idx];
}

static void print_bench_stats(const char *mode_name, const struct bench_stats *s)
{
    uint64_t avg = s->count ? s->sum_ns / (uint64_t)s->count : 0;
    uint64_t min = s->count ? s->min_ns : 0;
    printf("BENCH %s repeat=%d poll_strategy=%s poll_sleep_us=%u reset_strategy=%s "
           "fail_count=%d first_fail_iter=%d min_us=%" PRIu64 " avg_us=%" PRIu64
           " max_us=%" PRIu64 " p50_us=%" PRIu64 " p95_us=%" PRIu64 "\n",
           mode_name, s->count, poll_strategy_name(), g_poll_sleep_us,
           g_no_dma_reset_after_first ? "reset_once_reuse" : "reset_every_run",
           s->fail_count, s->first_fail_iter,
           (uint64_t)(min / 1000ull), (uint64_t)(avg / 1000ull),
           (uint64_t)(s->max_ns / 1000ull),
           (uint64_t)(bench_percentile_ns(s, 50) / 1000ull),
           (uint64_t)(bench_percentile_ns(s, 95) / 1000ull));
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--golden-dir DIR] [--phys-base ADDR] [--phys-size BYTES] [--packet-bytes BYTES]\n"
            "          [--poll-sleep-us USEC] [--repeat N] [--no-dma-reset-after-first]\n"
            "          [--quiet-pass] [--full-debug-status] [--expect-axis-width BITS]\n",
            argv0);
}

int main(int argc, char **argv)
{
    const char *golden_dir = DEFAULT_GOLDEN_DIR;
    uint64_t phys_base = DEFAULT_PHYS_BASE;
    uint64_t phys_size = DEFAULT_PHYS_SIZE;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--golden-dir") == 0 && i + 1 < argc) {
            golden_dir = argv[++i];
        } else if (strcmp(argv[i], "--phys-base") == 0 && i + 1 < argc) {
            if (parse_u64_arg("--phys-base", argv[++i], &phys_base) != 0) {
                return 2;
            }
        } else if (strcmp(argv[i], "--phys-size") == 0 && i + 1 < argc) {
            if (parse_u64_arg("--phys-size", argv[++i], &phys_size) != 0) {
                return 2;
            }
        } else if (strcmp(argv[i], "--packet-bytes") == 0 && i + 1 < argc) {
            uint64_t packet_override = 0;
            if (parse_u64_arg("--packet-bytes", argv[++i], &packet_override) != 0) {
                return 2;
            }
            if (packet_override == 0 || packet_override > PACKET_BYTES || (packet_override & 0x3u) != 0) {
                fprintf(stderr, "--packet-bytes must be a nonzero 32-bit-word-aligned value <= %u\n", (unsigned)PACKET_BYTES);
                return 2;
            }
            g_packet_bytes = (uint32_t)packet_override;
        } else if (strcmp(argv[i], "--poll-sleep-us") == 0 && i + 1 < argc) {
            if (parse_u32_arg("--poll-sleep-us", argv[++i], &g_poll_sleep_us) != 0) {
                return 2;
            }
        } else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
            uint32_t repeat = 0;
            if (parse_u32_arg("--repeat", argv[++i], &repeat) != 0) {
                return 2;
            }
            if (repeat == 0 || repeat > 256) {
                fprintf(stderr, "--repeat must be in the range 1..256\n");
                return 2;
            }
            g_repeat = (int)repeat;
        } else if (strcmp(argv[i], "--no-dma-reset-after-first") == 0) {
            g_no_dma_reset_after_first = 1;
        } else if (strcmp(argv[i], "--quiet-pass") == 0) {
            g_quiet_pass = 1;
        } else if (strcmp(argv[i], "--full-debug-status") == 0) {
            g_full_debug_status = 1;
        } else if (strcmp(argv[i], "--expect-axis-width") == 0 && i + 1 < argc) {
            if (parse_u32_arg("--expect-axis-width", argv[++i], &g_expect_axis_width) != 0) {
                return 2;
            }
            if (g_expect_axis_width != 32u && g_expect_axis_width != 128u) {
                fprintf(stderr, "--expect-axis-width must be 32 or 128\n");
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    printf("[FPGA GEMV HW TEST]\n");
    printf("case: fake_gemv\n");
    printf("golden_dir=%s\n", golden_dir);
    printf("carveout phys_base=0x%08" PRIx64 " phys_size=0x%08" PRIx64 "\n",
           phys_base, phys_size);
    printf("poll_strategy=%s poll_sleep_us=%u repeat=%d reset_strategy=%s quiet_pass=%d full_debug_status=%d\n",
           poll_strategy_name(), g_poll_sleep_us, g_repeat,
           g_no_dma_reset_after_first ? "reset_once_reuse" : "reset_every_run",
           g_quiet_pass, g_full_debug_status);

    if (phys_size < DMA_MAP_BYTES ||
        S2MM_BUF_OFF + RESULT_BYTES > phys_size ||
        MM2S_BUF_OFF + PACKET_BYTES > phys_size) {
        fprintf(stderr, "carveout is too small for S05 DMA buffers\n");
        return 2;
    }
    if ((phys_base & 0x3f) != 0) {
        fprintf(stderr, "phys-base must be at least 64-byte aligned\n");
        return 2;
    }

    struct file_blob input = {0}, scale = {0}, weight = {0}, scaled_ref = {0}, block_ref = {0};
    uint8_t packet[PACKET_BYTES];
    int fail = 0;

    if (strcmp(golden_dir, "builtin") == 0) {
        fail |= load_embedded_golden(&input, &scale, &weight, &scaled_ref, &block_ref);
    } else {
        fail |= load_file_exact(golden_dir, "input_i16.bin", INPUT_BYTES, &input);
        fail |= load_file_exact(golden_dir, "scale_q_i32.bin", SCALE_BYTES, &scale);
        fail |= load_file_exact(golden_dir, "weight_q8_fpga_layout.bin", WEIGHT_BYTES, &weight);
        fail |= load_file_exact(golden_dir, "output_scaled_ref_i32.bin", RESULT_BYTES, &scaled_ref);
        fail |= load_file_exact(golden_dir, "output_block_acc_ref_i32.bin", RESULT_BYTES, &block_ref);
    }
    if (fail || build_packet(packet, &scale, &weight) != 0) {
        fprintf(stderr, "failed to load fake_gemv golden inputs\n");
        free_blob(&input);
        free_blob(&scale);
        free_blob(&weight);
        free_blob(&scaled_ref);
        free_blob(&block_ref);
        return 1;
    }
    if (validate_packet_contract(packet, &scale, &weight) != 0) {
        fprintf(stderr, "S05 packet contract validation failed; DMA was not started\n");
        free_blob(&input);
        free_blob(&scale);
        free_blob(&weight);
        free_blob(&scaled_ref);
        free_blob(&block_ref);
        return 1;
    }
    printf("PACKET bytes=%u scale_bytes=%u weight_bytes=%u result_bytes=%u dma_packet_bytes=%u\n",
           (unsigned)PACKET_BYTES, (unsigned)SCALE_BYTES,
           (unsigned)WEIGHT_BYTES, (unsigned)RESULT_BYTES, (unsigned)g_packet_bytes);
    printf("GEOMETRY lanes=%u in_features=%u out_features=%u padded_out_features=%u row_padding=%u blocks_per_row=%u expected_tlast_lane_base=%u\n",
           (unsigned)LANES, (unsigned)IN_FEATURES, (unsigned)OUT_FEATURES,
           (unsigned)PADDED_OUT_FEATURES, (unsigned)ROW_PADDING,
           (unsigned)BLOCKS_PER_ROW, (unsigned)(LANES - 4));

    struct uio_dev dma_dev, bram_dev, gemv_dev;
    if (find_uio_by_name("axi_dma", &dma_dev) != 0 ||
        find_uio_by_name("input_bram", &bram_dev) != 0 ||
        find_uio_by_name("gemv_ctrl", &gemv_dev) != 0) {
        fprintf(stderr, "required UIO devices not found\n");
        return 1;
    }

    fail |= check_uio_addr(&dma_dev, EXPECTED_DMA_ADDR, EXPECTED_DMA_SIZE);
    fail |= check_uio_addr(&bram_dev, EXPECTED_BRAM_ADDR, EXPECTED_BRAM_SIZE);
    fail |= check_uio_addr(&gemv_dev, EXPECTED_GEMV_ADDR, EXPECTED_GEMV_SIZE);
    if (fail) {
        return 1;
    }

    int dma_fd = -1, bram_fd = -1, gemv_fd = -1, mem_fd = -1;
    volatile uint32_t *dma = (volatile uint32_t *)map_uio(&dma_dev, &dma_fd);
    volatile uint32_t *bram = (volatile uint32_t *)map_uio(&bram_dev, &bram_fd);
    volatile uint32_t *gemv = (volatile uint32_t *)map_uio(&gemv_dev, &gemv_fd);
    if (dma == MAP_FAILED || bram == MAP_FAILED || gemv == MAP_FAILED) {
        return 1;
    }

    uint32_t version = rd32(gemv, GEMV_VERSION);
    uint32_t build_config = rd32(gemv, GEMV_BUILD_CONFIG);
    uint32_t build_axis_width = build_config >> 16;
    uint32_t build_lanes = build_config & 0xffffu;
    printf("GEMV VERSION=0x%08x\n", version);
    if (version != EXPECTED_GEMV_VERSION) {
        fprintf(stderr, "unexpected GEMV version\n");
        return 1;
    }
    printf("GEMV BUILD_CONFIG=0x%08x axis_width=%u lanes=%u\n",
           build_config, build_axis_width, build_lanes);
    if (build_lanes != LANES) {
        fprintf(stderr, "unexpected GEMV lane count: got=%u expected=%u\n",
                build_lanes, (unsigned)LANES);
        return 1;
    }
    if (g_expect_axis_width != 0 && build_axis_width != g_expect_axis_width) {
        fprintf(stderr, "unexpected GEMV AXIS width: got=%u expected=%u\n",
                build_axis_width, g_expect_axis_width);
        return 1;
    }

    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        fprintf(stderr, "open /dev/mem failed: %s\n", strerror(errno));
        return 1;
    }
    void *mem = mmap(NULL, (size_t)DMA_MAP_BYTES, PROT_READ | PROT_WRITE,
                     MAP_SHARED, mem_fd, (off_t)phys_base);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "mmap carveout failed: %s\n", strerror(errno));
        return 1;
    }
    printf("DMA carveout mmap PASS virt=%p map_bytes=0x%08x\n", mem, (unsigned)DMA_MAP_BYTES);

    volatile uint32_t *packet_buf = (volatile uint32_t *)((volatile uint8_t *)mem + MM2S_BUF_OFF);
    volatile uint32_t *result_buf = (volatile uint32_t *)((volatile uint8_t *)mem + S2MM_BUF_OFF);
    uint64_t packet_phys = phys_base + MM2S_BUF_OFF;
    uint64_t result_phys = phys_base + S2MM_BUF_OFF;
    printf("DMA buffers mm2s_phys=0x%08" PRIx64 " s2mm_phys=0x%08" PRIx64 "\n",
           packet_phys, result_phys);

    struct bench_stats mode0_stats, mode1_stats;
    bench_stats_init(&mode0_stats);
    bench_stats_init(&mode1_stats);
    int mode0_fail = 0;
    int mode1_fail = 0;

    for (int iter = 0; iter < g_repeat; ++iter) {
        if (g_repeat > 1 && !g_quiet_pass) {
            printf("\n[ITER] %d/%d\n", iter + 1, g_repeat);
        }

        struct perf_timing mode0_timing;
        struct perf_timing mode1_timing;
        struct wait_timeline mode0_timeline;
        struct wait_timeline mode1_timeline;
        memset(&mode0_timing, 0, sizeof(mode0_timing));
        memset(&mode1_timing, 0, sizeof(mode1_timing));
        memset(&mode0_timeline, 0, sizeof(mode0_timeline));
        memset(&mode1_timeline, 0, sizeof(mode1_timeline));
        int mode0_reset = !g_no_dma_reset_after_first || iter == 0;
        int mode1_reset = !g_no_dma_reset_after_first;

        int mode0_iter_fail = run_mode(0, "mode=0 scaled", dma, gemv, bram,
                                       packet_buf, result_buf, packet_phys, result_phys,
                                       packet, &input, &scaled_ref,
                                       mode0_reset, &mode0_timing, &mode0_timeline);
        bench_stats_add(&mode0_stats, mode0_timing.total_ns, mode0_iter_fail, iter + 1);
        mode0_fail |= mode0_iter_fail;

        int mode1_iter_fail = run_mode(1, "mode=1 block_acc", dma, gemv, bram,
                                       packet_buf, result_buf, packet_phys, result_phys,
                                       packet, &input, &block_ref,
                                       mode1_reset, &mode1_timing, &mode1_timeline);
        bench_stats_add(&mode1_stats, mode1_timing.total_ns, mode1_iter_fail, iter + 1);
        mode1_fail |= mode1_iter_fail;

        if (g_repeat > 1 && !g_quiet_pass) {
            printf("[ITER] %d/%d mode0=%s mode1=%s\n",
                   iter + 1, g_repeat,
                   mode0_iter_fail ? "FAIL" : "PASS",
                   mode1_iter_fail ? "FAIL" : "PASS");
        }
    }

    if (g_repeat > 1) {
        print_bench_stats("mode=0 scaled", &mode0_stats);
        print_bench_stats("mode=1 block_acc", &mode1_stats);
    }

    printf("\n[FPGA GEMV HW TEST]\n");
    printf("case: fake_gemv\n");
    printf("mode=0 scaled: %s\n", mode0_fail ? "FAIL" : "PASS");
    printf("mode=1 block_acc: %s\n", mode1_fail ? "FAIL" : "PASS");
    printf("AXI-Lite bulk data path used: no\n");
    printf("AXI DMA MM2S/S2MM used: yes\n");
    printf("input BRAM used: yes\n");
    printf("physical buffer: /dev/mem O_SYNC carveout 0x%08" PRIx64 "-0x%08" PRIx64 "\n",
           phys_base, phys_base + phys_size - 1u);
    printf("OVERALL %s\n", (mode0_fail || mode1_fail) ? "FAIL" : "PASS");

    unmap_region((void *)mem, DMA_MAP_BYTES, mem_fd);
    unmap_region((void *)dma, dma_dev.size, dma_fd);
    unmap_region((void *)bram, bram_dev.size, bram_fd);
    unmap_region((void *)gemv, gemv_dev.size, gemv_fd);
    free_blob(&input);
    free_blob(&scale);
    free_blob(&weight);
    free_blob(&scaled_ref);
    free_blob(&block_ref);
    return (mode0_fail || mode1_fail) ? 1 : 0;
}

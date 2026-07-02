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
    SCALE_SHIFT = 20,
    AXIS_WIDTH_EXPECTED = 128,
    DMA_LENGTH_WIDTH = 14,
    DMA_MAX_SIMPLE_BYTES = (1u << DMA_LENGTH_WIDTH) - 1u,
    MAX_IN_FEATURES = 576,
    MAX_BLOCKS = MAX_IN_FEATURES / Q8_BLOCK_SIZE,
    MAX_PACKET_BYTES = 12000,
    RESULT_BYTES = LANES * 4,

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
    GEMV_DEBUG_SCALED0 = 0x8c,
    GEMV_DEBUG_SCALED1 = 0x90,
    GEMV_DEBUG_SCALED2 = 0x94,
    GEMV_DEBUG_ROW_ACC0 = 0x98,
    GEMV_DEBUG_ROW_ACC1 = 0x9c,
    GEMV_DEBUG_ROW_ACC2 = 0xa0,
    GEMV_BUILD_CONFIG = 0xa4,

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
#define DMA_SR_ERR_MASK 0x00004770u
#define DMA_SR_IRQ_CLR 0x00007000u

#define EXPECTED_GEMV_VERSION 0x000a0001u
#define EXPECTED_DMA_ADDR 0x40400000ull
#define EXPECTED_DMA_SIZE 0x00010000ull
#define EXPECTED_BRAM_ADDR 0x42000000ull
#define EXPECTED_BRAM_SIZE 0x00010000ull
#define EXPECTED_GEMV_ADDR 0x43ca0000ull
#define EXPECTED_GEMV_SIZE 0x00001000ull
#define DEFAULT_PHYS_BASE 0x3c000000ull
#define DEFAULT_PHYS_SIZE 0x04000000ull
#define PACKET_OFF 0x0000ull

struct uio_dev {
    char uio[256];
    char name[128];
    char dev_path[300];
    uint64_t addr;
    uint64_t size;
};

struct hw {
    struct uio_dev dma_dev;
    struct uio_dev bram_dev;
    struct uio_dev gemv_dev;
    int dma_fd;
    int bram_fd;
    int gemv_fd;
    int mem_fd;
    volatile uint32_t *dma;
    volatile uint32_t *bram;
    volatile uint32_t *gemv;
    uint8_t *mem;
    uint64_t phys_base;
    uint64_t packet_phys;
    uint64_t result_phys;
    uint64_t result_off;
    uint64_t map_bytes;
};

struct test_case {
    const char *name;
    const char *shape;
    const char *pattern;
    uint32_t in_features;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void sleep_us(long usec)
{
    struct timespec ts;
    ts.tv_sec = usec / 1000000L;
    ts.tv_nsec = (usec % 1000000L) * 1000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

static uint64_t align_up_u64(uint64_t v, uint64_t a)
{
    return (v + a - 1u) & ~(a - 1u);
}

static uint32_t rd32(volatile uint32_t *base, uint32_t off)
{
    return base[off / 4u];
}

static void wr32(volatile uint32_t *base, uint32_t off, uint32_t value)
{
    base[off / 4u] = value;
    __sync_synchronize();
}

static void gemv_clear_core(volatile uint32_t *gemv)
{
    wr32(gemv, GEMV_CONTROL, 0x2u);
    wr32(gemv, GEMV_DONE, 0x1u);
    (void)rd32(gemv, GEMV_STATUS);
    (void)rd32(gemv, GEMV_DONE);
    __sync_synchronize();
}

static void put_le32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)(u & 0xffu);
    p[1] = (uint8_t)((u >> 8) & 0xffu);
    p[2] = (uint8_t)((u >> 16) & 0xffu);
    p[3] = (uint8_t)((u >> 24) & 0xffu);
}

static int read_text(const char *path, char *buf, size_t len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len - 1u);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    return 0;
}

static int read_u64_hex(const char *path, uint64_t *out)
{
    char buf[64];
    char *end = NULL;
    if (read_text(path, buf, sizeof(buf)) != 0) return -1;
    errno = 0;
    unsigned long long v = strtoull(buf, &end, 0);
    if (errno || end == buf) return -1;
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
    if (read_text(path, dev->name, sizeof(dev->name)) != 0) return -1;
    snprintf(path, sizeof(path), "/sys/class/uio/%s/maps/map0/addr", uio);
    if (read_u64_hex(path, &dev->addr) != 0) return -1;
    snprintf(path, sizeof(path), "/sys/class/uio/%s/maps/map0/size", uio);
    if (read_u64_hex(path, &dev->size) != 0) return -1;
    return 0;
}

static int find_uio_by_name(const char *name, struct uio_dev *dev)
{
    DIR *dir = opendir("/sys/class/uio");
    if (!dir) return -1;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "uio", 3) != 0) continue;
        struct uio_dev tmp;
        if (load_uio(ent->d_name, &tmp) == 0 && strcmp(tmp.name, name) == 0) {
            *dev = tmp;
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -1;
}

static void *map_uio(const struct uio_dev *dev, int *fd)
{
    *fd = open(dev->dev_path, O_RDWR | O_SYNC);
    if (*fd < 0) return MAP_FAILED;
    return mmap(NULL, dev->size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
}

static int hw_open(struct hw *hw, uint64_t phys_base, uint64_t phys_size)
{
    memset(hw, 0, sizeof(*hw));
    hw->dma_fd = hw->bram_fd = hw->gemv_fd = hw->mem_fd = -1;
    hw->phys_base = phys_base;
    if (find_uio_by_name("axi_dma", &hw->dma_dev) ||
        find_uio_by_name("input_bram", &hw->bram_dev) ||
        find_uio_by_name("gemv_ctrl", &hw->gemv_dev)) {
        fprintf(stderr, "UIO lookup failed\n");
        return 1;
    }
    if (hw->dma_dev.addr != EXPECTED_DMA_ADDR || hw->bram_dev.addr != EXPECTED_BRAM_ADDR ||
        hw->gemv_dev.addr != EXPECTED_GEMV_ADDR) {
        fprintf(stderr, "UIO address mismatch dma=0x%08" PRIx64 " bram=0x%08" PRIx64
                " gemv=0x%08" PRIx64 "\n",
                hw->dma_dev.addr, hw->bram_dev.addr, hw->gemv_dev.addr);
        return 1;
    }
    hw->dma = (volatile uint32_t *)map_uio(&hw->dma_dev, &hw->dma_fd);
    hw->bram = (volatile uint32_t *)map_uio(&hw->bram_dev, &hw->bram_fd);
    hw->gemv = (volatile uint32_t *)map_uio(&hw->gemv_dev, &hw->gemv_fd);
    if (hw->dma == MAP_FAILED || hw->bram == MAP_FAILED || hw->gemv == MAP_FAILED) {
        fprintf(stderr, "UIO mmap failed\n");
        return 1;
    }
    hw->result_off = align_up_u64(MAX_PACKET_BYTES, 4096u);
    hw->packet_phys = phys_base + PACKET_OFF;
    hw->result_phys = phys_base + hw->result_off;
    hw->map_bytes = align_up_u64(hw->result_off + RESULT_BYTES, 4096u);
    if (hw->map_bytes > phys_size) {
        fprintf(stderr, "carveout too small\n");
        return 1;
    }
    hw->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (hw->mem_fd < 0) {
        perror("open /dev/mem");
        return 1;
    }
    hw->mem = mmap(NULL, hw->map_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, hw->mem_fd, phys_base);
    if (hw->mem == MAP_FAILED) {
        perror("mmap /dev/mem");
        return 1;
    }
    uint32_t version = rd32(hw->gemv, GEMV_VERSION);
    uint32_t build = rd32(hw->gemv, GEMV_BUILD_CONFIG);
    uint32_t axis_width = (build >> 16) & 0xffu;
    uint32_t lanes = build & 0xffffu;
    printf("GEMV VERSION=0x%08x BUILD_CONFIG=0x%08x axis_width=%u lanes=%u\n",
           version, build, axis_width, lanes);
    if (version != EXPECTED_GEMV_VERSION || axis_width != AXIS_WIDTH_EXPECTED || lanes != LANES) {
        fprintf(stderr, "unexpected GEMV build\n");
        return 1;
    }
    return 0;
}

static void hw_close(struct hw *hw)
{
    if (hw->mem && hw->mem != MAP_FAILED) munmap(hw->mem, hw->map_bytes);
    if (hw->dma && hw->dma != MAP_FAILED) munmap((void *)hw->dma, hw->dma_dev.size);
    if (hw->bram && hw->bram != MAP_FAILED) munmap((void *)hw->bram, hw->bram_dev.size);
    if (hw->gemv && hw->gemv != MAP_FAILED) munmap((void *)hw->gemv, hw->gemv_dev.size);
    if (hw->mem_fd >= 0) close(hw->mem_fd);
    if (hw->dma_fd >= 0) close(hw->dma_fd);
    if (hw->bram_fd >= 0) close(hw->bram_fd);
    if (hw->gemv_fd >= 0) close(hw->gemv_fd);
}

static int dma_done(uint32_t sr)
{
    return (sr & DMA_SR_IOC_IRQ) || ((sr & DMA_SR_IDLE) && !(sr & DMA_SR_HALTED));
}

static int wait_dma_running(volatile uint32_t *dma, uint32_t sr_off)
{
    for (int i = 0; i < 1000; ++i) {
        if ((rd32(dma, sr_off) & DMA_SR_HALTED) == 0) return 0;
        sleep_us(100);
    }
    return 1;
}

static int reset_dma(struct hw *hw)
{
    wr32(hw->dma, DMA_MM2S_CR, DMA_CR_RESET);
    wr32(hw->dma, DMA_S2MM_CR, DMA_CR_RESET);
    for (int i = 0; i < 1000; ++i) {
        if (!(rd32(hw->dma, DMA_MM2S_CR) & DMA_CR_RESET) &&
            !(rd32(hw->dma, DMA_S2MM_CR) & DMA_CR_RESET)) {
            wr32(hw->dma, DMA_MM2S_SR, DMA_SR_IRQ_CLR);
            wr32(hw->dma, DMA_S2MM_SR, DMA_SR_IRQ_CLR);
            return 0;
        }
        sleep_us(100);
    }
    return 1;
}

static int wait_done(struct hw *hw)
{
    uint64_t start = now_ns();
    for (;;) {
        uint32_t mm2s = rd32(hw->dma, DMA_MM2S_SR);
        uint32_t s2mm = rd32(hw->dma, DMA_S2MM_SR);
        uint32_t status = rd32(hw->gemv, GEMV_STATUS);
        uint32_t done = rd32(hw->gemv, GEMV_DONE);
        uint32_t err = rd32(hw->gemv, GEMV_ERROR_CODE);
        if ((mm2s & DMA_SR_ERR_MASK) || (s2mm & DMA_SR_ERR_MASK) ||
            (status & (1u << 2)) || err) {
            fprintf(stderr, "run error mm2s=0x%08x s2mm=0x%08x status=0x%08x done=0x%08x err=0x%08x\n",
                    mm2s, s2mm, status, done, err);
            return 1;
        }
        if (dma_done(mm2s) && dma_done(s2mm) && ((status & (1u << 1)) || (done & 1u))) {
            return 0;
        }
        if (now_ns() - start > 10000000000ull) {
            fprintf(stderr, "timeout waiting for completion\n");
            return 1;
        }
    }
}

static int16_t input_value(const char *pattern, uint32_t col)
{
    if (strcmp(pattern, "P6") == 0) {
        return (int16_t)((int32_t)((col * 5u + 11u) % 23u) - 11);
    }
    uint32_t block = col / Q8_BLOCK_SIZE;
    uint32_t local = col % Q8_BLOCK_SIZE;
    if (strcmp(pattern, "P4") == 0) {
        return (int16_t)((int32_t)(block * 2u) + (int32_t)(local % 5u) - 2);
    }
    return (int16_t)((int32_t)((col * 5u + 11u) % 11u) - 5);
}

static int32_t scale_value(const char *pattern, uint32_t row, uint32_t block, uint32_t blocks_per_row)
{
    (void)row;
    if (strcmp(pattern, "P1") == 0 && block != 0) return 0;
    if (strcmp(pattern, "P2") == 0 && block != blocks_per_row - 1u) return 0;
    if (strcmp(pattern, "P7") == 0 && block != blocks_per_row - 1u) return 0;
    if (strcmp(pattern, "P8") == 0 && block != blocks_per_row - 2u) return 0;
    if (strcmp(pattern, "P9") == 0 && block < blocks_per_row - 2u) return 0;
    if (strcmp(pattern, "P3") == 0) return (1 << SCALE_SHIFT) * (1 << (block % 3u));
    return 1 << SCALE_SHIFT;
}

static int8_t weight_value(const char *pattern, uint32_t row, uint32_t col, uint32_t blocks_per_row)
{
    uint32_t block = col / Q8_BLOCK_SIZE;
    uint32_t local = col % Q8_BLOCK_SIZE;
    if (strcmp(pattern, "P6") == 0) {
        return (int8_t)((int32_t)((row * 13u + col * 7u + 3u) % 17u) - 8);
    }
    if (strcmp(pattern, "P7") == 0) {
        if (block != blocks_per_row - 1u) return 0;
        return (int8_t)((int32_t)((row * 13u + col * 7u + 3u) % 17u) - 8);
    }
    if (strcmp(pattern, "P8") == 0) {
        if (block != blocks_per_row - 2u) return 0;
        return (int8_t)((int32_t)((row * 13u + col * 7u + 3u) % 17u) - 8);
    }
    if (strcmp(pattern, "P9") == 0) {
        if (block < blocks_per_row - 2u) return 0;
        return (int8_t)((int32_t)((row * 13u + col * 7u + 3u) % 17u) - 8);
    }
    if (strcmp(pattern, "P1") == 0 && block != 0) return 0;
    if (strcmp(pattern, "P2") == 0 && block != blocks_per_row - 1u) return 0;
    if (strcmp(pattern, "P5") == 0) {
        return (int8_t)((int32_t)((row * 3u + local * 2u + block) % 7u) - 3);
    }
    return (int8_t)((int32_t)((row * 3u + local * 2u + block * 5u + 1u) % 7u) - 3);
}

static int64_t round_shift_i64(int64_t value, int shift)
{
    if (shift == 0) return value;
    int64_t rounding = (int64_t)1 << (shift - 1);
    if (value >= 0) return (value + rounding) >> shift;
    return -(((-value) + rounding) >> shift);
}

static int32_t sat_i32(int64_t v)
{
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

static uint32_t make_packet(const struct test_case *tc, int16_t *input, uint8_t *packet,
                            int32_t *expected, int32_t *expected_blocks)
{
    uint32_t blocks = tc->in_features / Q8_BLOCK_SIZE;
    uint8_t *p = packet;
    for (uint32_t row = 0; row < LANES; ++row) expected[row] = 0;
    for (uint32_t col = 0; col < tc->in_features; ++col) {
        input[col] = input_value(tc->pattern, col);
    }
    for (uint32_t block = 0; block < blocks; ++block) {
        int32_t scales[LANES];
        int32_t block_acc[LANES];
        for (uint32_t lane = 0; lane < LANES; ++lane) {
            scales[lane] = scale_value(tc->pattern, lane, block, blocks);
            block_acc[lane] = 0;
            put_le32(p, scales[lane]);
            p += 4;
        }
        for (uint32_t local_col = 0; local_col < Q8_BLOCK_SIZE; ++local_col) {
            uint32_t col = block * Q8_BLOCK_SIZE + local_col;
            for (uint32_t lane = 0; lane < LANES; ++lane) {
                int8_t w = weight_value(tc->pattern, lane, col, blocks);
                *p++ = (uint8_t)w;
                block_acc[lane] += (int32_t)input[col] * (int32_t)w;
            }
        }
        for (uint32_t lane = 0; lane < LANES; ++lane) {
            int64_t scaled = round_shift_i64((int64_t)block_acc[lane] * (int64_t)scales[lane], SCALE_SHIFT);
            if (expected_blocks) {
                expected_blocks[block * LANES + lane] = block_acc[lane];
            }
            expected[lane] = sat_i32((int64_t)expected[lane] + scaled);
        }
    }
    return (uint32_t)(p - packet);
}

static void load_input_bram(struct hw *hw, const int16_t *input, uint32_t in_features)
{
    const uint8_t *bytes = (const uint8_t *)input;
    uint32_t words = (in_features * 2u + 3u) / 4u;
    for (uint32_t i = 0; i < words; ++i) {
        uint32_t v = 0;
        memcpy(&v, bytes + i * 4u, 4u);
        wr32(hw->bram, i * 4u, v);
    }
}

static const char *classify_mismatch(uint32_t idx, int32_t got, int32_t expected,
                                     uint32_t blocks, const char *pattern)
{
    (void)idx;
    if (got == INT32_MAX || got == INT32_MIN) return "SATURATION_FAIL";
    if (strcmp(pattern, "P1") == 0) return "ROW_ACC_CLEAR_FAIL";
    if (strcmp(pattern, "P2") == 0) return "INPUT_ADDR_PROGRESSION_FAIL";
    if (strcmp(pattern, "P3") == 0) return "SCALE_BLOCK_PROGRESSION_FAIL";
    if (strcmp(pattern, "P4") == 0) return "INPUT_ADDR_PROGRESSION_FAIL";
    if (strcmp(pattern, "P5") == 0) return "WEIGHT_BLOCK_PROGRESSION_FAIL";
    if (strcmp(pattern, "P6") == 0) return "PROXY_PATTERN_FAIL";
    if (strcmp(pattern, "P7") == 0) return "PROXY_LAST_BLOCK_FAIL";
    if (strcmp(pattern, "P8") == 0) return "PROXY_PREV_BLOCK_FAIL";
    if (strcmp(pattern, "P9") == 0) return "PROXY_LAST_TWO_BLOCK_ACCUM_FAIL";
    if (blocks > 1 && got != expected) return "ROW_ACC_ACCUM_FAIL";
    return "UNKNOWN_NEEDS_MORE_INSTRUMENTATION";
}

static int run_case(struct hw *hw, const struct test_case *tc)
{
    static int16_t input[MAX_IN_FEATURES];
    static uint8_t packet[MAX_PACKET_BYTES];
    static int32_t expected[LANES];
    uint32_t blocks = tc->in_features / Q8_BLOCK_SIZE;
    uint32_t packet_bytes = make_packet(tc, input, packet, expected, NULL);
    uint64_t t0 = now_ns();
    int failed = 0;
    uint32_t first_idx = 0xffffffffu;
    int32_t first_got = 0;
    int32_t first_expected = 0;
    const char *classification = "PASS";

    if (packet_bytes > DMA_MAX_SIMPLE_BYTES) {
        printf("BOARDCSV,%s,%s,%s,%u,%u,SKIP,DMA_LENGTH_LIMIT_FAIL,0,0,0,0,0,0,0,0,0,0,0,0\n",
               tc->name, tc->shape, tc->pattern, blocks, packet_bytes);
        return 0;
    }
    if (reset_dma(hw)) {
        fprintf(stderr, "%s DMA reset failed\n", tc->name);
        return 1;
    }
    load_input_bram(hw, input, tc->in_features);
    memcpy(hw->mem + PACKET_OFF, packet, packet_bytes);
    memset(hw->mem + hw->result_off, 0xcd, RESULT_BYTES);
    __sync_synchronize();

    gemv_clear_core(hw->gemv);
    wr32(hw->gemv, GEMV_MODE, 0u);
    wr32(hw->gemv, GEMV_SCALE_SHIFT, SCALE_SHIFT);
    wr32(hw->gemv, GEMV_IN_FEATURES, tc->in_features);
    wr32(hw->gemv, GEMV_OUT_FEATURES, LANES);
    wr32(hw->gemv, GEMV_INPUT_BASE, EXPECTED_BRAM_ADDR);
    wr32(hw->gemv, GEMV_WEIGHT_LENGTH, packet_bytes);
    wr32(hw->gemv, GEMV_RESULT_LENGTH, RESULT_BYTES);

    wr32(hw->dma, DMA_S2MM_CR, DMA_CR_RUNSTOP);
    if (wait_dma_running(hw->dma, DMA_S2MM_SR)) return 1;
    wr32(hw->dma, DMA_S2MM_DA, (uint32_t)hw->result_phys);
    wr32(hw->dma, DMA_S2MM_DA_MSB, 0u);
    wr32(hw->dma, DMA_S2MM_LENGTH, RESULT_BYTES);
    wr32(hw->gemv, GEMV_START, 1u);
    wr32(hw->dma, DMA_MM2S_CR, DMA_CR_RUNSTOP);
    if (wait_dma_running(hw->dma, DMA_MM2S_SR)) return 1;
    wr32(hw->dma, DMA_MM2S_SA, (uint32_t)hw->packet_phys);
    wr32(hw->dma, DMA_MM2S_SA_MSB, 0u);
    wr32(hw->dma, DMA_MM2S_LENGTH, packet_bytes);
    if (wait_done(hw)) {
        classification = rd32(hw->gemv, GEMV_ERROR_CODE) == 2u ? "TLAST_BLOCK_COUNT_FAIL" : "UNKNOWN_NEEDS_MORE_INSTRUMENTATION";
        failed = 1;
    }

    volatile int32_t *out = (volatile int32_t *)(void *)(hw->mem + hw->result_off);
    if (!failed) {
        for (uint32_t i = 0; i < LANES; ++i) {
            int32_t got = out[i];
            if (got != expected[i]) {
                failed = 1;
                first_idx = i;
                first_got = got;
                first_expected = expected[i];
                classification = classify_mismatch(i, got, expected[i], blocks, tc->pattern);
                break;
            }
        }
    }
    uint64_t elapsed_us = (now_ns() - t0) / 1000u;
    printf("BOARDCSV,%s,%s,%s,%u,%u,%s,%s,%u,%" PRId32 ",%" PRId32 ",%" PRIu64
           ",0x%08x,0x%08x,0x%08x,%u,%u,%u,%u,%u\n",
           tc->name, tc->shape, tc->pattern, blocks, packet_bytes,
           failed ? "FAIL" : "PASS", classification,
           first_idx == 0xffffffffu ? 0u : first_idx, first_got, first_expected, elapsed_us,
           rd32(hw->dma, DMA_MM2S_SR), rd32(hw->dma, DMA_S2MM_SR), rd32(hw->gemv, GEMV_STATUS),
           rd32(hw->gemv, GEMV_DEBUG_ROW), rd32(hw->gemv, GEMV_DEBUG_BLOCK),
           rd32(hw->gemv, GEMV_DEBUG_LANE), rd32(hw->gemv, GEMV_DEBUG_IN_COUNT),
           rd32(hw->gemv, GEMV_DEBUG_TLAST_COUNT));
    if (failed) {
        printf("DEBUG,%s,out0=%" PRId32 ",out1=%" PRId32 ",out2=%" PRId32
               ",dbg_out0=%" PRId32 ",dbg_out1=%" PRId32 ",dbg_out2=%" PRId32
               ",scale0=%" PRId32 ",scale1=%" PRId32 ",scale2=%" PRId32
               ",block0=%" PRId32 ",block1=%" PRId32 ",block2=%" PRId32
               ",scaled0=%" PRId32 ",scaled1=%" PRId32 ",scaled2=%" PRId32
               ",row_acc0=%" PRId32 ",row_acc1=%" PRId32 ",row_acc2=%" PRId32 "\n",
               tc->name, out[0], out[1], out[2],
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_OUT0), (int32_t)rd32(hw->gemv, GEMV_DEBUG_OUT1),
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_OUT2),
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_SCALE0), (int32_t)rd32(hw->gemv, GEMV_DEBUG_SCALE1),
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_SCALE2),
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_BLOCK0), (int32_t)rd32(hw->gemv, GEMV_DEBUG_BLOCK1),
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_BLOCK2),
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_SCALED0), (int32_t)rd32(hw->gemv, GEMV_DEBUG_SCALED1),
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_SCALED2),
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_ROW_ACC0), (int32_t)rd32(hw->gemv, GEMV_DEBUG_ROW_ACC1),
               (int32_t)rd32(hw->gemv, GEMV_DEBUG_ROW_ACC2));
        printf("OUTVEC,%s", tc->name);
        for (uint32_t i = 0; i < LANES; ++i) {
            printf(",%" PRId32, out[i]);
        }
        printf("\n");
        printf("EXPVEC,%s", tc->name);
        for (uint32_t i = 0; i < LANES; ++i) {
            printf(",%" PRId32, expected[i]);
        }
        printf("\n");
    }
    return failed;
}

static int run_case_mode1_blocks(struct hw *hw, const struct test_case *tc)
{
    static int16_t input[MAX_IN_FEATURES];
    static uint8_t packet[MAX_PACKET_BYTES];
    static int32_t ignored_mode0[LANES];
    static int32_t expected_blocks[MAX_BLOCKS * LANES];
    uint32_t blocks = tc->in_features / Q8_BLOCK_SIZE;
    uint32_t packet_bytes = make_packet(tc, input, packet, ignored_mode0, expected_blocks);
    uint32_t result_bytes = blocks * RESULT_BYTES;
    uint64_t t0 = now_ns();
    int failed = 0;
    uint32_t first_block = 0xffffffffu;
    uint32_t first_lane = 0xffffffffu;
    int32_t first_got = 0;
    int32_t first_expected = 0;

    if (packet_bytes > DMA_MAX_SIMPLE_BYTES || result_bytes > DMA_MAX_SIMPLE_BYTES) {
        printf("MODE1CSV,%s,%s,%s,%u,%u,%u,SKIP,DMA_LENGTH_LIMIT_FAIL,0,0,0,0\n",
               tc->name, tc->shape, tc->pattern, blocks, packet_bytes, result_bytes);
        return 0;
    }
    if (reset_dma(hw)) {
        fprintf(stderr, "%s mode1 DMA reset failed\n", tc->name);
        return 1;
    }
    load_input_bram(hw, input, tc->in_features);
    memcpy(hw->mem + PACKET_OFF, packet, packet_bytes);
    memset(hw->mem + hw->result_off, 0xcd, result_bytes);
    __sync_synchronize();

    gemv_clear_core(hw->gemv);
    wr32(hw->gemv, GEMV_MODE, 1u);
    wr32(hw->gemv, GEMV_SCALE_SHIFT, SCALE_SHIFT);
    wr32(hw->gemv, GEMV_IN_FEATURES, tc->in_features);
    wr32(hw->gemv, GEMV_OUT_FEATURES, LANES);
    wr32(hw->gemv, GEMV_INPUT_BASE, EXPECTED_BRAM_ADDR);
    wr32(hw->gemv, GEMV_WEIGHT_LENGTH, packet_bytes);
    wr32(hw->gemv, GEMV_RESULT_LENGTH, result_bytes);

    wr32(hw->dma, DMA_S2MM_CR, DMA_CR_RUNSTOP);
    if (wait_dma_running(hw->dma, DMA_S2MM_SR)) return 1;
    wr32(hw->dma, DMA_S2MM_DA, (uint32_t)hw->result_phys);
    wr32(hw->dma, DMA_S2MM_DA_MSB, 0u);
    wr32(hw->dma, DMA_S2MM_LENGTH, result_bytes);
    wr32(hw->gemv, GEMV_START, 1u);
    wr32(hw->dma, DMA_MM2S_CR, DMA_CR_RUNSTOP);
    if (wait_dma_running(hw->dma, DMA_MM2S_SR)) return 1;
    wr32(hw->dma, DMA_MM2S_SA, (uint32_t)hw->packet_phys);
    wr32(hw->dma, DMA_MM2S_SA_MSB, 0u);
    wr32(hw->dma, DMA_MM2S_LENGTH, packet_bytes);
    if (wait_done(hw)) {
        failed = 1;
    }

    volatile int32_t *out = (volatile int32_t *)(void *)(hw->mem + hw->result_off);
    if (!failed) {
        for (uint32_t block = 0; block < blocks; ++block) {
            for (uint32_t lane = 0; lane < LANES; ++lane) {
                uint32_t idx = block * LANES + lane;
                int32_t got = out[idx];
                int32_t expected = expected_blocks[idx];
                if (got != expected) {
                    failed = 1;
                    first_block = block;
                    first_lane = lane;
                    first_got = got;
                    first_expected = expected;
                    block = blocks;
                    break;
                }
            }
        }
    }

    uint64_t elapsed_us = (now_ns() - t0) / 1000u;
    printf("MODE1CSV,%s,%s,%s,%u,%u,%u,%s,%s,%u,%u,%" PRId32 ",%" PRId32 ",%" PRIu64 "\n",
           tc->name, tc->shape, tc->pattern, blocks, packet_bytes, result_bytes,
           failed ? "FAIL" : "PASS", failed ? "BLOCK_ACC_FAIL" : "PASS",
           first_block == 0xffffffffu ? 0u : first_block,
           first_lane == 0xffffffffu ? 0u : first_lane,
           first_got, first_expected, elapsed_us);
    if (failed) {
        for (uint32_t block = 0; block < blocks; ++block) {
            printf("MODE1_OUT_BLOCK,%s,b%u", tc->name, block);
            for (uint32_t lane = 0; lane < LANES; ++lane) {
                printf(",%" PRId32, out[block * LANES + lane]);
            }
            printf("\n");
            printf("MODE1_EXP_BLOCK,%s,b%u", tc->name, block);
            for (uint32_t lane = 0; lane < LANES; ++lane) {
                printf(",%" PRId32, expected_blocks[block * LANES + lane]);
            }
            printf("\n");
        }
        printf("MODE1_LANE4,%s", tc->name);
        for (uint32_t block = 0; block < blocks; ++block) {
            uint32_t idx = block * LANES + 4u;
            printf(",b%u_got=%" PRId32 ",b%u_exp=%" PRId32,
                   block, out[idx], block, expected_blocks[idx]);
        }
        printf("\n");
        printf("MODE1_LANE12,%s", tc->name);
        for (uint32_t block = 0; block < blocks; ++block) {
            uint32_t idx = block * LANES + 12u;
            printf(",b%u_got=%" PRId32 ",b%u_exp=%" PRId32,
                   block, out[idx], block, expected_blocks[idx]);
        }
        printf("\n");
    }
    return failed;
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

int main(int argc, char **argv)
{
    uint64_t phys_base = DEFAULT_PHYS_BASE;
    uint64_t phys_size = DEFAULT_PHYS_SIZE;
    const char *only_case = NULL;
    int repeat = 1;
    int mode1_blocks = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--phys-base") == 0 && i + 1 < argc) {
            if (parse_u64_arg("--phys-base", argv[++i], &phys_base)) return 2;
        } else if (strcmp(argv[i], "--phys-size") == 0 && i + 1 < argc) {
            if (parse_u64_arg("--phys-size", argv[++i], &phys_size)) return 2;
        } else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only_case = argv[++i];
        } else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
            repeat = atoi(argv[++i]);
            if (repeat <= 0) repeat = 1;
        } else if (strcmp(argv[i], "--mode1-blocks") == 0) {
            mode1_blocks = 1;
        } else {
            fprintf(stderr, "usage: %s [--phys-base ADDR] [--phys-size BYTES] [--only CASE] [--repeat N] [--mode1-blocks]\n", argv[0]);
            return 2;
        }
    }

    const struct test_case tests[] = {
        {"A_32x16_P0", "32x16", "P0", 32},
        {"B_64x16_P0", "64x16", "P0", 64},
        {"B_64x16_P1", "64x16", "P1", 64},
        {"B_64x16_P2", "64x16", "P2", 64},
        {"B_64x16_P3", "64x16", "P3", 64},
        {"B_64x16_P4", "64x16", "P4", 64},
        {"B_64x16_P5", "64x16", "P5", 64},
        {"C_96x16_P0", "96x16", "P0", 96},
        {"C_96x16_P1", "96x16", "P1", 96},
        {"C_96x16_P2", "96x16", "P2", 96},
        {"C_96x16_P3", "96x16", "P3", 96},
        {"D_512x16_P0", "512x16", "P0", 512},
        {"D_512x16_P6", "512x16", "P6", 512},
        {"D2_544x16_P6", "544x16", "P6", 544},
        {"E_576x16_P0", "576x16", "P0", 576},
        {"E_576x16_P1", "576x16", "P1", 576},
        {"E_576x16_P2", "576x16", "P2", 576},
        {"E_576x16_P3", "576x16", "P3", 576},
        {"E_576x16_P6", "576x16", "P6", 576},
        {"E_576x16_P7", "576x16", "P7", 576},
        {"E_576x16_P8", "576x16", "P8", 576},
        {"E_576x16_P9", "576x16", "P9", 576},
    };

    struct hw hw;
    if (hw_open(&hw, phys_base, phys_size)) return 1;
    printf("[S05.6.1 MULTIBLOCK BOARD]\n");
    printf("BOARDCSV_HEADER,case,shape,pattern,blocks_per_row,packet_bytes,result,classification,first_mismatch,got,expected,elapsed_us,mm2s_sr,s2mm_sr,status,debug_row,debug_block,debug_lane,debug_in_count,debug_tlast_count\n");
    if (mode1_blocks) {
        printf("MODE1CSV_HEADER,case,shape,pattern,blocks_per_row,packet_bytes,result_bytes,result,classification,first_block,first_lane,got,expected,elapsed_us\n");
    }
    int fail_count = 0;
    int selected_count = 0;
    for (int r = 0; r < repeat; ++r) {
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
            if (only_case && strcmp(only_case, tests[i].name) != 0) continue;
            selected_count++;
            fail_count += mode1_blocks ? run_case_mode1_blocks(&hw, &tests[i]) : run_case(&hw, &tests[i]);
        }
    }
    if (only_case && selected_count == 0) {
        fprintf(stderr, "unknown --only case: %s\n", only_case);
        hw_close(&hw);
        return 2;
    }
    printf("F_1536x16_DEFER,DMA_LENGTH_LIMIT_FAIL,packet_bytes=27648,limit=%u\n", (uint32_t)DMA_MAX_SIMPLE_BYTES);
    if (fail_count == 0) {
        printf("S05_6_1_BOARD_PASS\n");
    } else {
        printf("S05_6_1_BOARD_FAIL fail_count=%d\n", fail_count);
    }
    hw_close(&hw);
    return fail_count ? 1 : 0;
}

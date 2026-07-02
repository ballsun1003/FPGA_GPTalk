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
#define PACKET_OFF 0x00000000ull

struct uio_dev {
    char uio[256];
    char name[128];
    char dev_path[300];
    uint64_t addr;
    uint64_t size;
};

struct gemv_hw {
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
    uint64_t phys_size;
    uint64_t map_bytes;
    uint64_t packet_phys;
    uint64_t result_phys;
    uint64_t result_off;
};

struct job {
    const char *name;
    uint32_t in_features;
    uint32_t out_features;
    uint32_t blocks_per_row;
    uint32_t row_groups;
    uint32_t packet_bytes;
    uint32_t result_words[2];
    uint32_t result_bytes[2];
    int16_t *input;
    uint8_t *packet;
    int32_t *ref[2];
};

struct policy {
    const char *name;
    int reset_every_job;
    int reset_once;
    int static_config_cache;
    int packet_preloaded;
    int input_reuse;
};

struct totals {
    uint64_t total_ns;
    uint64_t dma_reset_ns;
    uint64_t config_ns;
    uint64_t input_ns;
    uint64_t packet_ns;
    uint64_t s2mm_clear_ns;
    uint64_t dma_setup_ns;
    uint64_t wait_ns;
};

struct stats {
    uint64_t *samples;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t sum_ns;
    uint32_t count;
};

enum pattern_kind {
    PATTERN_P0 = 0,
    PATTERN_P6 = 6
};

struct chunk_check {
    uint32_t chunk_count;
    uint32_t chunk_in_features;
    uint32_t chunk_packet_bytes;
    uint32_t first_mismatch;
    int32_t got;
    int32_t expected;
    const char *classification;
    uint64_t elapsed_ns;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t align_up_u64(uint64_t v, uint64_t a)
{
    return (v + a - 1u) & ~(a - 1u);
}

static uint64_t ns_to_us(uint64_t ns)
{
    return ns / 1000u;
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

static void wr32(volatile uint32_t *base, uint32_t off, uint32_t value)
{
    base[off / 4u] = value;
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
    if (fd < 0) {
        return -1;
    }
    ssize_t n = read(fd, buf, len - 1u);
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
    char *end = NULL;
    if (read_text(path, buf, sizeof(buf)) != 0) {
        return -1;
    }
    errno = 0;
    unsigned long long v = strtoull(buf, &end, 0);
    if (errno || end == buf) {
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
        if (load_uio(de->d_name, &dev) == 0 && strcmp(dev.name, name) == 0) {
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

static int check_uio_addr(const struct uio_dev *dev, uint64_t addr, uint64_t size)
{
    printf("UIO name=%s dev=%s addr=0x%08" PRIx64 " size=0x%08" PRIx64 "\n",
           dev->name, dev->dev_path, dev->addr, dev->size);
    if (dev->addr != addr || dev->size != size) {
        fprintf(stderr, "UIO address mismatch for %s\n", dev->name);
        return 1;
    }
    return 0;
}

static void gemv_hw_close(struct gemv_hw *hw)
{
    if (hw->mem && hw->mem != MAP_FAILED) {
        munmap(hw->mem, (size_t)hw->map_bytes);
    }
    if (hw->dma && hw->dma != MAP_FAILED) {
        munmap((void *)hw->dma, (size_t)hw->dma_dev.size);
    }
    if (hw->bram && hw->bram != MAP_FAILED) {
        munmap((void *)hw->bram, (size_t)hw->bram_dev.size);
    }
    if (hw->gemv && hw->gemv != MAP_FAILED) {
        munmap((void *)hw->gemv, (size_t)hw->gemv_dev.size);
    }
    if (hw->mem_fd >= 0) close(hw->mem_fd);
    if (hw->dma_fd >= 0) close(hw->dma_fd);
    if (hw->bram_fd >= 0) close(hw->bram_fd);
    if (hw->gemv_fd >= 0) close(hw->gemv_fd);
}

static int gemv_hw_open(struct gemv_hw *hw, uint64_t phys_base, uint64_t phys_size,
                        uint64_t max_packet_bytes, uint64_t max_result_bytes)
{
    memset(hw, 0, sizeof(*hw));
    hw->dma_fd = hw->bram_fd = hw->gemv_fd = hw->mem_fd = -1;
    hw->phys_base = phys_base;
    hw->phys_size = phys_size;
    hw->result_off = align_up_u64(PACKET_OFF + max_packet_bytes, 4096u);
    hw->map_bytes = align_up_u64(hw->result_off + max_result_bytes, 4096u);
    if (hw->map_bytes > phys_size) {
        fprintf(stderr, "carveout too small: need=0x%08" PRIx64 " have=0x%08" PRIx64 "\n",
                hw->map_bytes, phys_size);
        return 1;
    }
    hw->packet_phys = phys_base + PACKET_OFF;
    hw->result_phys = phys_base + hw->result_off;

    if (find_uio_by_name("axi_dma", &hw->dma_dev) != 0 ||
        find_uio_by_name("input_bram", &hw->bram_dev) != 0 ||
        find_uio_by_name("gemv_ctrl", &hw->gemv_dev) != 0) {
        fprintf(stderr, "required UIO nodes not found by name\n");
        return 1;
    }
    if (check_uio_addr(&hw->dma_dev, EXPECTED_DMA_ADDR, EXPECTED_DMA_SIZE) ||
        check_uio_addr(&hw->bram_dev, EXPECTED_BRAM_ADDR, EXPECTED_BRAM_SIZE) ||
        check_uio_addr(&hw->gemv_dev, EXPECTED_GEMV_ADDR, EXPECTED_GEMV_SIZE)) {
        return 1;
    }

    hw->dma = (volatile uint32_t *)map_uio(&hw->dma_dev, &hw->dma_fd);
    hw->bram = (volatile uint32_t *)map_uio(&hw->bram_dev, &hw->bram_fd);
    hw->gemv = (volatile uint32_t *)map_uio(&hw->gemv_dev, &hw->gemv_fd);
    if (hw->dma == MAP_FAILED || hw->bram == MAP_FAILED || hw->gemv == MAP_FAILED) {
        return 1;
    }
    hw->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (hw->mem_fd < 0) {
        fprintf(stderr, "open /dev/mem failed: %s\n", strerror(errno));
        return 1;
    }
    hw->mem = mmap(NULL, (size_t)hw->map_bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
                   hw->mem_fd, (off_t)phys_base);
    if (hw->mem == MAP_FAILED) {
        fprintf(stderr, "mmap /dev/mem failed: %s\n", strerror(errno));
        return 1;
    }
    printf("DMA carveout mmap PASS base=0x%08" PRIx64 " map_bytes=0x%08" PRIx64
           " packet_phys=0x%08" PRIx64 " result_phys=0x%08" PRIx64 "\n",
           phys_base, hw->map_bytes, hw->packet_phys, hw->result_phys);

    uint32_t version = rd32(hw->gemv, GEMV_VERSION);
    uint32_t build = rd32(hw->gemv, GEMV_BUILD_CONFIG);
    uint32_t lanes = build & 0xffffu;
    uint32_t axis_width = (build >> 16) & 0xffffu;
    printf("GEMV VERSION=0x%08" PRIx32 " BUILD_CONFIG=0x%08" PRIx32
           " axis_width=%u lanes=%u\n", version, build, axis_width, lanes);
    if (version != EXPECTED_GEMV_VERSION || lanes != LANES || axis_width != AXIS_WIDTH_EXPECTED) {
        fprintf(stderr, "unexpected GEMV version/config\n");
        return 1;
    }
    return 0;
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

static int gemv_hw_reset_dma(struct gemv_hw *hw)
{
    wr32(hw->dma, DMA_MM2S_CR, DMA_CR_RESET);
    wr32(hw->dma, DMA_S2MM_CR, DMA_CR_RESET);
    if (wait_dma_reset_clear(hw->dma, DMA_MM2S_CR) ||
        wait_dma_reset_clear(hw->dma, DMA_S2MM_CR)) {
        fprintf(stderr, "DMA reset timeout\n");
        return 1;
    }
    wr32(hw->dma, DMA_MM2S_SR, DMA_SR_IRQ_CLR);
    wr32(hw->dma, DMA_S2MM_SR, DMA_SR_IRQ_CLR);
    return 0;
}

static int gemv_hw_prepare_reuse(struct gemv_hw *hw)
{
    wr32(hw->dma, DMA_MM2S_SR, DMA_SR_IRQ_CLR);
    wr32(hw->dma, DMA_S2MM_SR, DMA_SR_IRQ_CLR);
    uint32_t mm2s = rd32(hw->dma, DMA_MM2S_SR);
    uint32_t s2mm = rd32(hw->dma, DMA_S2MM_SR);
    if ((mm2s & DMA_SR_ERR_MASK) || (s2mm & DMA_SR_ERR_MASK)) {
        fprintf(stderr, "DMA error before reuse mm2s=0x%08" PRIx32 " s2mm=0x%08" PRIx32 "\n",
                mm2s, s2mm);
        return 1;
    }
    return 0;
}

static int dma_done(uint32_t sr)
{
    return (sr & DMA_SR_IOC_IRQ) || ((sr & DMA_SR_IDLE) && !(sr & DMA_SR_HALTED));
}

static int wait_dma_running(volatile uint32_t *dma, uint32_t sr_off)
{
    for (int i = 0; i < 1000; ++i) {
        uint32_t sr = rd32(dma, sr_off);
        if ((sr & DMA_SR_HALTED) == 0) {
            return 0;
        }
        sleep_us(100);
    }
    return 1;
}

static int wait_done(struct gemv_hw *hw)
{
    const uint64_t start = now_ns();
    const uint64_t timeout_ns = 10000000000ull;
    for (uint32_t i = 0; i < 50000000u; ++i) {
        uint32_t mm2s = rd32(hw->dma, DMA_MM2S_SR);
        uint32_t s2mm = rd32(hw->dma, DMA_S2MM_SR);
        uint32_t status = rd32(hw->gemv, GEMV_STATUS);
        uint32_t done = rd32(hw->gemv, GEMV_DONE);
        uint32_t err = rd32(hw->gemv, GEMV_ERROR_CODE);
        if ((mm2s & DMA_SR_ERR_MASK) || (s2mm & DMA_SR_ERR_MASK) ||
            (status & (1u << 2)) || err) {
            fprintf(stderr, "run error mm2s=0x%08" PRIx32 " s2mm=0x%08" PRIx32
                    " status=0x%08" PRIx32 " done=0x%08" PRIx32 " err=0x%08" PRIx32 "\n",
                    mm2s, s2mm, status, done, err);
            return 1;
        }
        if (dma_done(mm2s) && dma_done(s2mm) && ((status & (1u << 1)) || (done & 1u))) {
            return 0;
        }
        if (now_ns() - start > timeout_ns) {
            fprintf(stderr, "timeout waiting for GEMV/DMA completion\n");
            return 1;
        }
    }
    return 1;
}

static void gemv_hw_load_input(struct gemv_hw *hw, const struct job *job)
{
    const uint8_t *bytes = (const uint8_t *)job->input;
    uint32_t words = (job->in_features * 2u + 3u) / 4u;
    for (uint32_t i = 0; i < words; ++i) {
        uint32_t v = 0;
        memcpy(&v, bytes + i * 4u, 4u);
        wr32(hw->bram, i * 4u, v);
    }
}

static void gemv_hw_prepare_packet(struct gemv_hw *hw, const struct job *job)
{
    memcpy(hw->mem + PACKET_OFF, job->packet, job->packet_bytes);
    __sync_synchronize();
}

static void clear_result(struct gemv_hw *hw, uint32_t result_bytes)
{
    memset(hw->mem + hw->result_off, 0xcd, result_bytes);
    __sync_synchronize();
}

static void gemv_hw_config_static(struct gemv_hw *hw, const struct job *job, int mode)
{
    wr32(hw->gemv, GEMV_MODE, (uint32_t)mode);
    wr32(hw->gemv, GEMV_SCALE_SHIFT, SCALE_SHIFT);
    wr32(hw->gemv, GEMV_IN_FEATURES, job->in_features);
    wr32(hw->gemv, GEMV_OUT_FEATURES, job->out_features);
    wr32(hw->gemv, GEMV_INPUT_BASE, EXPECTED_BRAM_ADDR);
}

static void gemv_hw_config_job(struct gemv_hw *hw, const struct job *job, int mode)
{
    wr32(hw->gemv, GEMV_CONTROL, 0x2u);
    wr32(hw->gemv, GEMV_DONE, 0x1u);
    gemv_hw_config_static(hw, job, mode);
    wr32(hw->gemv, GEMV_WEIGHT_LENGTH, job->packet_bytes);
    wr32(hw->gemv, GEMV_RESULT_LENGTH, job->result_bytes[mode]);
}

static int gemv_hw_run_one_capture(struct gemv_hw *hw, const struct job *job, int mode,
                                   const struct policy *policy, int iter, int *static_valid,
                                   struct totals *t, int32_t *captured)
{
    uint64_t t0;
    uint64_t t1;
    uint64_t run0 = now_ns();
    uint32_t result_bytes = job->result_bytes[mode];
    uint32_t result_words = job->result_words[mode];

    t0 = now_ns();
    if (policy->reset_every_job || (policy->reset_once && iter == 0)) {
        if (gemv_hw_reset_dma(hw)) return 1;
    } else {
        if (gemv_hw_prepare_reuse(hw)) return 1;
    }
    t1 = now_ns();
    t->dma_reset_ns += t1 - t0;

    if (!(policy->input_reuse && iter > 0)) {
        t0 = now_ns();
        gemv_hw_load_input(hw, job);
        t1 = now_ns();
        t->input_ns += t1 - t0;
    }

    if (!(policy->packet_preloaded && iter > 0)) {
        t0 = now_ns();
        gemv_hw_prepare_packet(hw, job);
        t1 = now_ns();
        t->packet_ns += t1 - t0;
    }

    t0 = now_ns();
    clear_result(hw, result_bytes);
    t1 = now_ns();
    t->s2mm_clear_ns += t1 - t0;

    t0 = now_ns();
    if (!policy->static_config_cache) {
        gemv_hw_config_job(hw, job, mode);
        *static_valid = 1;
    } else {
        wr32(hw->gemv, GEMV_CONTROL, 0x2u);
        wr32(hw->gemv, GEMV_DONE, 0x1u);
        if (!*static_valid) {
            gemv_hw_config_static(hw, job, mode);
            *static_valid = 1;
        }
        wr32(hw->gemv, GEMV_WEIGHT_LENGTH, job->packet_bytes);
        wr32(hw->gemv, GEMV_RESULT_LENGTH, result_bytes);
    }
    t1 = now_ns();
    t->config_ns += t1 - t0;

    t0 = now_ns();
    wr32(hw->dma, DMA_S2MM_CR, DMA_CR_RUNSTOP);
    if (wait_dma_running(hw->dma, DMA_S2MM_SR)) return 1;
    wr32(hw->dma, DMA_S2MM_DA, (uint32_t)hw->result_phys);
    wr32(hw->dma, DMA_S2MM_DA_MSB, 0u);
    wr32(hw->dma, DMA_S2MM_LENGTH, result_bytes);
    wr32(hw->gemv, GEMV_START, 0x1u);
    wr32(hw->dma, DMA_MM2S_CR, DMA_CR_RUNSTOP);
    if (wait_dma_running(hw->dma, DMA_MM2S_SR)) return 1;
    wr32(hw->dma, DMA_MM2S_SA, (uint32_t)hw->packet_phys);
    wr32(hw->dma, DMA_MM2S_SA_MSB, 0u);
    wr32(hw->dma, DMA_MM2S_LENGTH, job->packet_bytes);
    t1 = now_ns();
    t->dma_setup_ns += t1 - t0;

    t0 = now_ns();
    if (wait_done(hw)) return 1;
    t1 = now_ns();
    t->wait_ns += t1 - t0;

    volatile int32_t *out = (volatile int32_t *)(void *)(hw->mem + hw->result_off);
    for (uint32_t i = 0; i < result_words; ++i) {
        int32_t got = out[i];
        if (captured) {
            captured[i] = got;
        }
        if (got != job->ref[mode][i]) {
            fprintf(stderr, "%s mode=%d result[%u] got=%" PRId32 " expected=%" PRId32 "\n",
                    job->name, mode, i, got, job->ref[mode][i]);
            return 1;
        }
    }
    t->total_ns += now_ns() - run0;
    return 0;
}

static int gemv_hw_run_one(struct gemv_hw *hw, const struct job *job, int mode,
                           const struct policy *policy, int iter, int *static_valid,
                           struct totals *t)
{
    return gemv_hw_run_one_capture(hw, job, mode, policy, iter, static_valid, t, NULL);
}

static int32_t sat_i64_to_i32(int64_t v)
{
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

static const char *pattern_name(enum pattern_kind pattern)
{
    return pattern == PATTERN_P0 ? "P0" : "P6";
}

static int8_t weight_value(enum pattern_kind pattern, uint32_t row, uint32_t col,
                           uint32_t full_blocks_per_row)
{
    uint32_t block = col / Q8_BLOCK_SIZE;
    uint32_t local = col % Q8_BLOCK_SIZE;
    (void)full_blocks_per_row;
    if (pattern == PATTERN_P0) {
        return (int8_t)((int32_t)((row * 3u + local * 2u + block * 5u + 1u) % 7u) - 3);
    }
    return (int8_t)((int32_t)((row * 13u + col * 7u + 3u) % 17u) - 8);
}

static int32_t scale_value(enum pattern_kind pattern, uint32_t row, uint32_t block,
                           uint32_t full_blocks_per_row)
{
    (void)row;
    (void)block;
    (void)full_blocks_per_row;
    (void)pattern;
    return 1 << SCALE_SHIFT;
}

static int16_t input_value(enum pattern_kind pattern, uint32_t col)
{
    if (pattern == PATTERN_P0) {
        return (int16_t)((int32_t)((col * 5u + 11u) % 11u) - 5);
    }
    return (int16_t)((int32_t)((col * 5u + 11u) % 23u) - 11);
}

static int make_job_pattern_at(struct job *job, const char *name,
                               uint32_t in_features, uint32_t out_features,
                               uint32_t input_col_base, uint32_t row_base,
                               uint32_t full_in_features, enum pattern_kind pattern)
{
    memset(job, 0, sizeof(*job));
    job->name = name;
    job->in_features = in_features;
    job->out_features = out_features;
    job->blocks_per_row = in_features / Q8_BLOCK_SIZE;
    job->row_groups = (out_features + LANES - 1u) / LANES;
    if (in_features == 0 || out_features == 0 ||
        (in_features % Q8_BLOCK_SIZE) != 0 ||
        full_in_features == 0 || (full_in_features % Q8_BLOCK_SIZE) != 0 ||
        input_col_base + in_features > full_in_features) {
        fprintf(stderr, "invalid job geometry %s %ux%u\n", name, in_features, out_features);
        return 1;
    }
    uint32_t full_blocks_per_row = full_in_features / Q8_BLOCK_SIZE;
    job->packet_bytes = job->row_groups * job->blocks_per_row *
        (LANES * 4u + LANES * Q8_BLOCK_SIZE);
    job->result_words[0] = out_features;
    job->result_words[1] = out_features * job->blocks_per_row;
    job->result_bytes[0] = job->result_words[0] * 4u;
    job->result_bytes[1] = job->result_words[1] * 4u;

    job->input = (int16_t *)calloc(in_features, sizeof(int16_t));
    job->packet = (uint8_t *)calloc(job->packet_bytes, 1u);
    job->ref[0] = (int32_t *)calloc(job->result_words[0], sizeof(int32_t));
    job->ref[1] = (int32_t *)calloc(job->result_words[1], sizeof(int32_t));
    if (!job->input || !job->packet || !job->ref[0] || !job->ref[1]) {
        fprintf(stderr, "allocation failed for job %s\n", name);
        return 1;
    }

    for (uint32_t c = 0; c < in_features; ++c) {
        job->input[c] = input_value(pattern, input_col_base + c);
    }

    uint8_t *p = job->packet;
    for (uint32_t group = 0; group < job->row_groups; ++group) {
        uint32_t local_row_base = group * LANES;
        for (uint32_t block = 0; block < job->blocks_per_row; ++block) {
            uint32_t global_block = (input_col_base / Q8_BLOCK_SIZE) + block;
            for (uint32_t lane = 0; lane < LANES; ++lane) {
                uint32_t local_row = local_row_base + lane;
                uint32_t global_row = row_base + local_row;
                int32_t scale = (local_row < out_features) ?
                    scale_value(pattern, global_row, global_block, full_blocks_per_row) : 0;
                put_le32(p, scale);
                p += 4;
            }
            for (uint32_t col = 0; col < Q8_BLOCK_SIZE; ++col) {
                uint32_t local_col = block * Q8_BLOCK_SIZE + col;
                uint32_t global_col = input_col_base + local_col;
                for (uint32_t lane = 0; lane < LANES; ++lane) {
                    uint32_t local_row = local_row_base + lane;
                    uint32_t global_row = row_base + local_row;
                    *p++ = (uint8_t)((local_row < out_features) ?
                        weight_value(pattern, global_row, global_col, full_blocks_per_row) : 0);
                }
            }
        }
    }

    uint32_t mode1_idx = 0;
    for (uint32_t group = 0; group < job->row_groups; ++group) {
        uint32_t local_row_base = group * LANES;
        for (uint32_t block = 0; block < job->blocks_per_row; ++block) {
            uint32_t global_block = (input_col_base / Q8_BLOCK_SIZE) + block;
            for (uint32_t lane = 0; lane < LANES; ++lane) {
                uint32_t local_row = local_row_base + lane;
                uint32_t global_row = row_base + local_row;
                if (local_row >= out_features) {
                    continue;
                }
                int32_t block_acc = 0;
                for (uint32_t col = 0; col < Q8_BLOCK_SIZE; ++col) {
                    uint32_t local_col = block * Q8_BLOCK_SIZE + col;
                    uint32_t global_col = input_col_base + local_col;
                    block_acc += (int32_t)job->input[local_col] *
                                 (int32_t)weight_value(pattern, global_row, global_col,
                                                       full_blocks_per_row);
                }
                job->ref[1][mode1_idx++] = block_acc;
                int64_t scaled = ((int64_t)block_acc *
                                  (int64_t)scale_value(pattern, global_row, global_block,
                                                       full_blocks_per_row)) >> SCALE_SHIFT;
                job->ref[0][local_row] = sat_i64_to_i32((int64_t)job->ref[0][local_row] + scaled);
            }
        }
    }
    return 0;
}

static int make_job(struct job *job, const char *name, uint32_t in_features, uint32_t out_features)
{
    return make_job_pattern_at(job, name, in_features, out_features, 0, 0,
                               in_features, PATTERN_P6);
}

static void free_job(struct job *job)
{
    free(job->input);
    free(job->packet);
    free(job->ref[0]);
    free(job->ref[1]);
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

static void stats_init(struct stats *s, uint32_t count)
{
    memset(s, 0, sizeof(*s));
    s->samples = (uint64_t *)calloc(count ? count : 1u, sizeof(uint64_t));
    s->min_ns = UINT64_MAX;
}

static void stats_add(struct stats *s, uint64_t ns)
{
    s->samples[s->count++] = ns;
    if (ns < s->min_ns) s->min_ns = ns;
    if (ns > s->max_ns) s->max_ns = ns;
    s->sum_ns += ns;
}

static uint64_t stats_pct(struct stats *s, int pct)
{
    if (s->count == 0) return 0;
    qsort(s->samples, s->count, sizeof(uint64_t), cmp_u64);
    uint32_t idx = (uint32_t)(((uint64_t)(s->count - 1u) * (uint64_t)pct + 99u) / 100u);
    if (idx >= s->count) idx = s->count - 1u;
    return s->samples[idx];
}

static void stats_free(struct stats *s)
{
    free(s->samples);
}

static int gemv_hw_run_batch(struct gemv_hw *hw, const struct job *job, int mode,
                             const struct policy *policy, uint32_t batch_size,
                             int proxy, struct totals *out_totals)
{
    struct totals totals;
    struct stats stats;
    int fail_count = 0;
    int static_valid = 0;
    memset(&totals, 0, sizeof(totals));
    stats_init(&stats, batch_size);

    uint64_t batch0 = now_ns();

    if (policy->packet_preloaded) {
        uint64_t t0 = now_ns();
        gemv_hw_prepare_packet(hw, job);
        totals.packet_ns += now_ns() - t0;
    }
    if (policy->input_reuse) {
        uint64_t t0 = now_ns();
        gemv_hw_load_input(hw, job);
        totals.input_ns += now_ns() - t0;
    }

    for (uint32_t i = 0; i < batch_size; ++i) {
        uint64_t j0 = now_ns();
        int fail = gemv_hw_run_one(hw, job, mode, policy, (int)i, &static_valid, &totals);
        uint64_t j1 = now_ns();
        stats_add(&stats, j1 - j0);
        if (fail) {
            fail_count++;
            break;
        }
    }
    uint64_t batch_ns = now_ns() - batch0;
    if (out_totals) {
        *out_totals = totals;
    }
    uint64_t avg_job_ns = batch_size ? batch_ns / batch_size : 0;
    uint64_t p50 = stats_pct(&stats, 50);
    uint64_t p95 = stats_pct(&stats, 95);
    uint64_t min = stats.count ? stats.min_ns : 0;
    uint64_t max = stats.max_ns;

    if (proxy) {
        double mb = (double)(job->packet_bytes + job->result_bytes[mode]) / (1024.0 * 1024.0);
        double seconds = (double)batch_ns / 1000000000.0;
        double mbps = seconds > 0.0 ? ((mb * (double)batch_size) / seconds) : 0.0;
        double macs = (double)job->in_features * (double)job->out_features * (double)batch_size;
        double macps = seconds > 0.0 ? (macs / seconds) : 0.0;
        printf("PROXYCSV,%s,%d,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%u,%u,%d,%.3f,%.3f,%.3f\n",
               job->name, mode, job->in_features, job->out_features, batch_size,
               ns_to_us(batch_ns), ns_to_us(avg_job_ns), job->packet_bytes,
               job->result_bytes[mode], fail_count, mbps, macps / 1000000.0,
               totals.total_ns ? (100.0 * (double)(totals.dma_reset_ns + totals.config_ns +
               totals.dma_setup_ns + totals.wait_ns) / (double)totals.total_ns) : 0.0);
    } else {
        printf("CSV,%s,%d,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
               ",%" PRIu64 ",%" PRIu64 ",%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64
               ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
               policy->name, mode, batch_size,
               ns_to_us(batch_ns), ns_to_us(avg_job_ns),
               ns_to_us(min), ns_to_us(p50), ns_to_us(p95), ns_to_us(max),
               fail_count, ns_to_us(totals.dma_reset_ns), ns_to_us(totals.config_ns),
               ns_to_us(totals.input_ns), ns_to_us(totals.packet_ns),
               ns_to_us(totals.s2mm_clear_ns), ns_to_us(totals.dma_setup_ns),
               ns_to_us(totals.wait_ns));
    }
    stats_free(&stats);
    return fail_count ? 1 : 0;
}

static uint64_t full_packet_bytes_for_shape(uint32_t in_features, uint32_t out_features)
{
    uint64_t blocks = in_features / Q8_BLOCK_SIZE;
    uint64_t row_groups = (out_features + LANES - 1u) / LANES;
    return row_groups * blocks * (LANES * 4u + LANES * Q8_BLOCK_SIZE);
}

static uint32_t rowgroup_packet_bytes_for(uint32_t in_features)
{
    return (in_features / Q8_BLOCK_SIZE) * (LANES * 4u + LANES * Q8_BLOCK_SIZE);
}

static void full_reference_rowgroup(enum pattern_kind pattern, uint32_t in_features,
                                    uint32_t out_features, uint32_t row_group,
                                    int32_t out[LANES])
{
    uint32_t blocks = in_features / Q8_BLOCK_SIZE;
    uint32_t row_base = row_group * LANES;
    for (uint32_t lane = 0; lane < LANES; ++lane) {
        uint32_t row = row_base + lane;
        int32_t row_acc = 0;
        if (row >= out_features) {
            out[lane] = 0;
            continue;
        }
        for (uint32_t block = 0; block < blocks; ++block) {
            int32_t block_acc = 0;
            for (uint32_t local = 0; local < Q8_BLOCK_SIZE; ++local) {
                uint32_t col = block * Q8_BLOCK_SIZE + local;
                block_acc += (int32_t)input_value(pattern, col) *
                             (int32_t)weight_value(pattern, row, col, blocks);
            }
            int64_t scaled = ((int64_t)block_acc *
                              (int64_t)scale_value(pattern, row, block, blocks)) >> SCALE_SHIFT;
            row_acc = sat_i64_to_i32((int64_t)row_acc + scaled);
        }
        out[lane] = row_acc;
    }
}

static int gemv_hw_run_rowgroup_chunked(struct gemv_hw *hw, const char *name,
                                        enum pattern_kind pattern,
                                        uint32_t in_features, uint32_t out_features,
                                        uint32_t row_group, uint32_t input_chunk_features,
                                        const struct policy *policy,
                                        int *static_valid, uint32_t *job_iter,
                                        struct totals *totals, struct stats *stats,
                                        struct chunk_check *check)
{
    uint64_t start_ns = now_ns();
    int64_t acc[LANES];
    int32_t expected[LANES];
    uint32_t input_chunks;
    uint32_t chunk_packet_bytes;
    int rc = 1;

    memset(acc, 0, sizeof(acc));
    full_reference_rowgroup(pattern, in_features, out_features, row_group, expected);
    if (check) {
        memset(check, 0, sizeof(*check));
        check->first_mismatch = UINT32_MAX;
        check->classification = "UNKNOWN";
    }

    if (input_chunk_features == 0 ||
        (input_chunk_features % Q8_BLOCK_SIZE) != 0 ||
        (in_features % input_chunk_features) != 0 ||
        (out_features % LANES) != 0) {
        fprintf(stderr, "invalid chunked rowgroup geometry %s\n", name);
        if (check) check->classification = "CHUNK_DRIVER_BUG";
        goto done;
    }

    input_chunks = in_features / input_chunk_features;
    chunk_packet_bytes = rowgroup_packet_bytes_for(input_chunk_features);
    if (check) {
        check->chunk_count = input_chunks;
        check->chunk_in_features = input_chunk_features;
        check->chunk_packet_bytes = chunk_packet_bytes;
    }
    if (chunk_packet_bytes > DMA_MAX_SIMPLE_BYTES) {
        fprintf(stderr, "%s chunk packet %u exceeds DMA simple max %u bytes\n",
                name, chunk_packet_bytes, (uint32_t)DMA_MAX_SIMPLE_BYTES);
        if (check) check->classification = "CHUNK_DMA_FAIL";
        goto done;
    }

    for (uint32_t input_chunk = 0; input_chunk < input_chunks; ++input_chunk) {
        struct job chunk;
        char chunk_name[160];
        int32_t chunk_out[LANES];
        uint32_t input_base = input_chunk * input_chunk_features;
        uint32_t row_base = row_group * LANES;
        snprintf(chunk_name, sizeof(chunk_name), "%s_%s_rg%u_ic%u_%ux16",
                 name, pattern_name(pattern), row_group, input_chunk, input_chunk_features);
        if (make_job_pattern_at(&chunk, chunk_name, input_chunk_features, LANES,
                                input_base, row_base, in_features, pattern)) {
            if (check) check->classification = "CHUNK_DRIVER_BUG";
            goto done;
        }

        uint64_t j0 = now_ns();
        int failed = gemv_hw_run_one_capture(hw, &chunk, 0, policy, (int)*job_iter,
                                             static_valid, totals, chunk_out);
        uint64_t j1 = now_ns();
        if (stats) {
            stats_add(stats, j1 - j0);
        }
        (*job_iter)++;
        free_job(&chunk);
        if (failed) {
            if (check) check->classification = "CHUNK_GEMV_FAIL";
            goto done;
        }
        for (uint32_t lane = 0; lane < LANES; ++lane) {
            acc[lane] += (int64_t)chunk_out[lane];
        }
    }

    for (uint32_t lane = 0; lane < LANES; ++lane) {
        int32_t got = sat_i64_to_i32(acc[lane]);
        if (got != expected[lane]) {
            if (check) {
                check->first_mismatch = lane;
                check->got = got;
                check->expected = expected[lane];
                check->classification = "CHUNK_REF_MISMATCH";
            }
            goto done;
        }
    }

    if (check) {
        check->classification = "PASS";
    }
    rc = 0;

done:
    if (check) {
        check->elapsed_ns = now_ns() - start_ns;
    }
    return rc;
}

static int gemv_hw_run_tensor_rowgroups_chunked(struct gemv_hw *hw, const char *name,
                                                enum pattern_kind pattern,
                                                uint32_t in_features, uint32_t out_features,
                                                uint32_t input_chunk_features,
                                                uint32_t tensor_repeats,
                                                const struct policy *policy)
{
    if (input_chunk_features == 0 ||
        (input_chunk_features % Q8_BLOCK_SIZE) != 0 ||
        (in_features % input_chunk_features) != 0 ||
        (out_features % LANES) != 0) {
        fprintf(stderr, "invalid chunked tensor geometry %s\n", name);
        return 1;
    }

    struct policy chunk_policy = *policy;
    chunk_policy.packet_preloaded = 0;
    chunk_policy.input_reuse = 0;

    uint32_t row_groups = out_features / LANES;
    uint32_t input_chunks = in_features / input_chunk_features;
    uint32_t chunk_jobs_per_tensor = row_groups * input_chunks;
    uint32_t total_chunk_jobs = chunk_jobs_per_tensor * tensor_repeats;
    uint32_t chunk_packet_bytes = rowgroup_packet_bytes_for(input_chunk_features);
    uint64_t full_packet_bytes = full_packet_bytes_for_shape(in_features, out_features);
    uint64_t actual_packet_bytes = (uint64_t)chunk_packet_bytes * (uint64_t)total_chunk_jobs;
    uint64_t actual_result_bytes = (uint64_t)LANES * 4u * (uint64_t)total_chunk_jobs;
    uint64_t full_result_bytes = (uint64_t)out_features * 4u;
    uint32_t job_iter = 0;
    int static_valid = 0;
    int fail_count = 0;
    struct totals totals;
    struct stats stats;
    uint64_t batch0;
    uint64_t total_ns;

    memset(&totals, 0, sizeof(totals));
    stats_init(&stats, total_chunk_jobs);

    batch0 = now_ns();
    for (uint32_t r = 0; r < tensor_repeats; ++r) {
        for (uint32_t row_group = 0; row_group < row_groups; ++row_group) {
            struct chunk_check check;
            int failed = gemv_hw_run_rowgroup_chunked(hw, name, pattern, in_features,
                                                      out_features, row_group,
                                                      input_chunk_features, &chunk_policy,
                                                      &static_valid, &job_iter, &totals,
                                                      &stats, &check);
            if (failed) {
                fprintf(stderr, "%s repeat=%u row_group=%u failed: %s\n",
                        name, r, row_group, check.classification);
                fail_count++;
                goto proxy_done;
            }
        }
    }

proxy_done:
    total_ns = now_ns() - batch0;
    uint64_t avg_tensor_ns = tensor_repeats ? total_ns / tensor_repeats : 0;
    uint64_t avg_chunk_ns = job_iter ? total_ns / job_iter : 0;
    double seconds = (double)total_ns / 1000000000.0;
    double mb = (double)(actual_packet_bytes + actual_result_bytes) / (1024.0 * 1024.0);
    double mbps = seconds > 0.0 ? mb / seconds : 0.0;
    double macs = (double)in_features * (double)out_features * (double)tensor_repeats;
    double macps = seconds > 0.0 ? macs / seconds : 0.0;
    double overhead_pct = totals.total_ns ?
        (100.0 * (double)(totals.dma_reset_ns + totals.config_ns +
         totals.dma_setup_ns + totals.wait_ns) / (double)totals.total_ns) : 0.0;

    printf("PROXYCSV,%s,0,%u,%u,%u,%u,%u,%u,%u,%" PRIu64 ",%" PRIu64
           ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%d,%.3f,%.3f,%.3f,%u,%s\n",
           name, in_features, out_features, tensor_repeats, row_groups, input_chunks,
           input_chunk_features, chunk_jobs_per_tensor, ns_to_us(total_ns),
           ns_to_us(avg_tensor_ns), ns_to_us(avg_chunk_ns), full_packet_bytes,
           actual_packet_bytes, actual_result_bytes, full_result_bytes, fail_count,
           mbps, macps / 1000000.0, overhead_pct, (uint32_t)DMA_MAX_SIMPLE_BYTES,
           input_chunks > 1u ? "true" : "false");
    if (!fail_count) {
        printf("PROXY_PASS,%s\n", name);
    }
    stats_free(&stats);
    return fail_count ? 1 : 0;
}

static int gemv_hw_run_mode1_chunk_check(struct gemv_hw *hw, const char *name,
                                         enum pattern_kind pattern,
                                         uint32_t in_features, uint32_t row_group,
                                         uint32_t input_chunk_features,
                                         uint32_t input_chunk,
                                         const struct policy *policy)
{
    struct job chunk;
    struct totals totals;
    int static_valid = 0;
    int32_t *captured = NULL;
    char chunk_name[160];
    uint32_t input_base = input_chunk * input_chunk_features;
    uint32_t row_base = row_group * LANES;
    int failed;

    memset(&totals, 0, sizeof(totals));
    snprintf(chunk_name, sizeof(chunk_name), "%s_%s_mode1_rg%u_ic%u_%ux16",
             name, pattern_name(pattern), row_group, input_chunk, input_chunk_features);
    if (make_job_pattern_at(&chunk, chunk_name, input_chunk_features, LANES,
                            input_base, row_base, in_features, pattern)) {
        return 1;
    }
    captured = (int32_t *)calloc(chunk.result_words[1], sizeof(int32_t));
    if (!captured) {
        free_job(&chunk);
        return 1;
    }
    failed = gemv_hw_run_one_capture(hw, &chunk, 1, policy, 0, &static_valid,
                                     &totals, captured);
    printf("CHUNKBOARDCSV,%s,%s,1,%u,%u,%u,1,%u,%u,%s,%s,0,0,0,%" PRIu64
           ",0x%08" PRIx32 ",0x%08" PRIx32 ",0x%08" PRIx32 ",true,false\n",
           name, pattern_name(pattern), in_features, (uint32_t)LANES, row_group,
           input_chunk_features, chunk.packet_bytes, failed ? "FAIL" : "PASS",
           failed ? "CHUNK_GEMV_FAIL" : "PASS", ns_to_us(totals.total_ns),
           rd32(hw->dma, DMA_MM2S_SR), rd32(hw->dma, DMA_S2MM_SR),
           rd32(hw->gemv, GEMV_STATUS));
    free(captured);
    free_job(&chunk);
    return failed;
}

static int run_chunked_board_only(struct gemv_hw *hw)
{
    const struct policy chunk_policy = {"S05_6_3_chunked_board", 0, 1, 1, 0, 0};
    const uint32_t in_features = 1536u;
    const uint32_t out_features = 16u;
    const uint32_t chunk_in = 512u;
    int fail = 0;

    printf("[S05.6.3 F_1536x16 CHUNKED BOARD]\n");
    printf("CHUNKBOARD_HEADER,name,pattern,mode,in_features,out_features,row_group,chunk_count,chunk_in_features,chunk_packet_bytes,result,classification,first_mismatch,got,expected,elapsed_us,mm2s_sr,s2mm_sr,status,max_btt_ok,cpu_accumulation_required\n");

    for (int p = 0; p < 2; ++p) {
        enum pattern_kind pattern = p == 0 ? PATTERN_P0 : PATTERN_P6;
        struct totals totals;
        struct stats stats;
        struct chunk_check check;
        int static_valid = 0;
        uint32_t job_iter = 0;
        memset(&totals, 0, sizeof(totals));
        stats_init(&stats, 3u);
        int failed = gemv_hw_run_rowgroup_chunked(hw, "F_1536x16", pattern,
                                                  in_features, out_features, 0u,
                                                  chunk_in, &chunk_policy,
                                                  &static_valid, &job_iter, &totals,
                                                  &stats, &check);
        printf("CHUNKBOARDCSV,F_1536x16_%s,%s,0,%u,%u,0,%u,%u,%u,%s,%s,%u,%" PRId32
               ",%" PRId32 ",%" PRIu64 ",0x%08" PRIx32 ",0x%08" PRIx32
               ",0x%08" PRIx32 ",%s,true\n",
               pattern_name(pattern), pattern_name(pattern), in_features, out_features,
               check.chunk_count, check.chunk_in_features, check.chunk_packet_bytes,
               failed ? "FAIL" : "PASS", check.classification,
               check.first_mismatch == UINT32_MAX ? 0u : check.first_mismatch,
               check.got, check.expected, ns_to_us(check.elapsed_ns),
               rd32(hw->dma, DMA_MM2S_SR), rd32(hw->dma, DMA_S2MM_SR),
               rd32(hw->gemv, GEMV_STATUS),
               check.chunk_packet_bytes <= DMA_MAX_SIMPLE_BYTES ? "true" : "false");
        stats_free(&stats);
        fail |= failed;
    }

    fail |= gemv_hw_run_mode1_chunk_check(hw, "F_1536x16_P6", PATTERN_P6,
                                          in_features, 0u, chunk_in, 1u,
                                          &chunk_policy);
    if (fail) {
        printf("S05_6_3_CHUNKED_BOARD_FAIL\n");
    } else {
        printf("S05_6_3_CHUNKED_BOARD_PASS\n");
    }
    return fail;
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

static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [--phys-base ADDR] [--phys-size BYTES] [--include-1024] [--chunked-board-only]\n", argv0);
}

int main(int argc, char **argv)
{
    uint64_t phys_base = DEFAULT_PHYS_BASE;
    uint64_t phys_size = DEFAULT_PHYS_SIZE;
    int include_1024 = 0;
    int chunked_board_only = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--phys-base") == 0 && i + 1 < argc) {
            if (parse_u64_arg("--phys-base", argv[++i], &phys_base)) return 2;
        } else if (strcmp(argv[i], "--phys-size") == 0 && i + 1 < argc) {
            if (parse_u64_arg("--phys-size", argv[++i], &phys_size)) return 2;
        } else if (strcmp(argv[i], "--include-1024") == 0) {
            include_1024 = 1;
        } else if (strcmp(argv[i], "--chunked-board-only") == 0) {
            chunked_board_only = 1;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    struct job fake;
    if (make_job(&fake, "fake_gemv", 32, 3)) {
        return 1;
    }

    uint64_t max_packet = 10368u;
    uint64_t max_result = 4096u;

    struct gemv_hw hw;
    if (gemv_hw_open(&hw, phys_base, phys_size, max_packet, max_result)) {
        free_job(&fake);
        return 1;
    }

    printf("[S05.6 GEMV BATCH BENCH]\n");
    printf("AXI-Lite bulk data path used: no\n");
    printf("AXI DMA MM2S/S2MM used: yes\n");
    printf("input BRAM used: yes\n");
    printf("physical buffer: /dev/mem O_SYNC carveout 0x%08" PRIx64 "-0x%08" PRIx64 "\n",
           phys_base, phys_base + phys_size - 1u);
    printf("DMA simple length width: %u max_bytes=%u\n",
           (uint32_t)DMA_LENGTH_WIDTH, (uint32_t)DMA_MAX_SIMPLE_BYTES);
    printf("CSV_HEADER,variant,mode,batch_size,total_us,avg_per_job_us,min_us,p50_us,p95_us,max_us,fail_count,dma_reset_us,config_us,input_bram_write_us,packet_memcpy_us,s2mm_clear_us,dma_setup_us,wait_poll_us\n");
    printf("PROXY_HEADER,name,mode,in_features,out_features,tensor_repeats,row_groups,chunk_count,chunk_in_features,chunk_jobs_per_tensor,total_us,avg_per_tensor_us,avg_per_chunk_us,full_packet_bytes,actual_packet_bytes,actual_result_bytes,full_result_bytes,fail_count,effective_MBps,effective_MMACps,overhead_pct,dma_limit_bytes,cpu_accumulation_required\n");
    printf("PROXY_NOTE,full S05.4 tensor packets exceed current AXI DMA 14-bit simple transfer length, so proxy tensors are executed as TLAST-safe row/input chunks.\n");

    if (chunked_board_only) {
        int board_fail = run_chunked_board_only(&hw);
        gemv_hw_close(&hw);
        free_job(&fake);
        return board_fail ? 1 : 0;
    }

    const struct policy policies[] = {
        {"A_reset_every_job", 1, 0, 0, 0, 0},
        {"B_reset_once", 0, 1, 0, 0, 0},
        {"C_static_config_cache", 0, 1, 1, 0, 0},
        {"D_packet_preloaded", 0, 1, 1, 1, 0},
        {"E_input_reuse", 0, 1, 1, 1, 1},
        {"F_combined_hot_path", 0, 1, 1, 1, 1},
    };
    const uint32_t batch_sizes_base[] = {1, 4, 8, 16, 64, 256, 1024};
    const size_t batch_count = include_1024 ? 7u : 6u;
    int fail = 0;
    int fake_fail = 0;

    for (size_t p = 0; p < sizeof(policies) / sizeof(policies[0]); ++p) {
        for (int mode = 0; mode <= 1; ++mode) {
            for (size_t b = 0; b < batch_count; ++b) {
                fake_fail |= gemv_hw_run_batch(&hw, &fake, mode, &policies[p], batch_sizes_base[b], 0, NULL);
            }
        }
    }
    fail |= fake_fail;

    const struct policy hot = {"F_combined_hot_path", 0, 1, 1, 1, 1};
    fail |= gemv_hw_run_tensor_rowgroups_chunked(&hw, "lane_probe_32x16",
                                                 PATTERN_P6, 32, 16, 32, 1, &hot);
    const uint32_t proxy_repeats[] = {1u, 16u, 64u};
    for (size_t i = 0; i < sizeof(proxy_repeats) / sizeof(proxy_repeats[0]); ++i) {
        uint32_t repeats = proxy_repeats[i];
        fail |= gemv_hw_run_tensor_rowgroups_chunked(&hw, "mlp_576x1536_no_chunk",
                                                     PATTERN_P6, 576, 1536, 576,
                                                     repeats, &hot);
        fail |= gemv_hw_run_tensor_rowgroups_chunked(&hw, "down_1536x576_chunked",
                                                     PATTERN_P6, 1536, 576, 512,
                                                     repeats, &hot);
        fail |= gemv_hw_run_tensor_rowgroups_chunked(&hw, "lm_head_576x256_no_chunk",
                                                     PATTERN_P6, 576, 256, 576,
                                                     repeats, &hot);
    }

    if (fail) {
        printf("OVERALL FAIL\n");
    } else {
        printf("fake_gemv mode=0 batch PASS\n");
        printf("fake_gemv mode=1 batch PASS\n");
        printf("proxy benchmark PASS\n");
        printf("OVERALL PASS\n");
    }

    gemv_hw_close(&hw);
    free_job(&fake);
    return fail ? 1 : 0;
}

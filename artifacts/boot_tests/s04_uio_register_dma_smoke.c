#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct uio_dev {
    char uio[32];
    char name[128];
    char dev_path[64];
    uint64_t addr;
    uint64_t size;
};

static int read_text(const char *path, char *buf, size_t len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    return 0;
}

static int read_u64_hex(const char *path, uint64_t *out) {
    char buf[64];
    if (read_text(path, buf, sizeof(buf)) != 0) return -1;
    errno = 0;
    unsigned long long v = strtoull(buf, NULL, 0);
    if (errno) return -1;
    *out = (uint64_t)v;
    return 0;
}

static int load_uio(const char *uio, struct uio_dev *dev) {
    char path[256];
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

static int find_uio_by_name(const char *name, struct uio_dev *out) {
    DIR *dir = opendir("/sys/class/uio");
    if (!dir) return -1;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "uio", 3) != 0) continue;
        struct uio_dev dev;
        if (load_uio(de->d_name, &dev) != 0) continue;
        if (strcmp(dev.name, name) == 0) {
            *out = dev;
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -1;
}

static void *map_uio(const struct uio_dev *dev, int *fd_out) {
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

static void unmap_uio(void *p, uint64_t size, int fd) {
    if (p && p != MAP_FAILED) munmap(p, (size_t)size);
    if (fd >= 0) close(fd);
}

static uint32_t rd32(volatile uint32_t *base, uint32_t off) {
    return base[off / 4u];
}

static void wr32(volatile uint32_t *base, uint32_t off, uint32_t v) {
    base[off / 4u] = v;
    __sync_synchronize();
}

static int check_dev(const struct uio_dev *dev, uint64_t addr, uint64_t size) {
    printf("UIO name=%s dev=%s addr=0x%08llx size=0x%08llx expected_addr=0x%08llx expected_size=0x%08llx\n",
           dev->name, dev->dev_path,
           (unsigned long long)dev->addr, (unsigned long long)dev->size,
           (unsigned long long)addr, (unsigned long long)size);
    if (dev->addr != addr || dev->size != size) {
        printf("CHECK %s address_map FAIL\n", dev->name);
        return 1;
    }
    printf("CHECK %s address_map PASS\n", dev->name);
    return 0;
}

static int gemv_check(const struct uio_dev *dev) {
    int fd = -1;
    volatile uint32_t *reg = (volatile uint32_t *)map_uio(dev, &fd);
    if (reg == MAP_FAILED) return 1;
    uint32_t version = rd32(reg, 0x00);
    uint32_t status = rd32(reg, 0x08);
    uint32_t error = rd32(reg, 0x0c);
    uint32_t done = rd32(reg, 0x30);
    printf("GEMV VERSION=0x%08x STATUS=0x%08x ERROR=0x%08x DONE=0x%08x\n",
           version, status, error, done);
    int fail = 0;
    if (version != 0x000a0001u) fail = 1;
    if ((status & (1u << 2)) != 0) fail = 1;
    if (error != 0) fail = 1;
    printf("CHECK gemv_ctrl register_read %s\n", fail ? "FAIL" : "PASS");
    unmap_uio((void *)reg, dev->size, fd);
    return fail;
}

static int bram_check(const struct uio_dev *dev) {
    int fd = -1;
    volatile uint32_t *bram = (volatile uint32_t *)map_uio(dev, &fd);
    if (bram == MAP_FAILED) return 1;
    enum { N = 16 };
    uint32_t old[N], pat[N], got[N];
    for (int i = 0; i < N; ++i) {
        old[i] = bram[i];
        pat[i] = 0xa5a50000u ^ (uint32_t)(i * 0x01010101u);
        bram[i] = pat[i];
    }
    __sync_synchronize();
    int fail = 0;
    for (int i = 0; i < N; ++i) {
        got[i] = bram[i];
        if (got[i] != pat[i]) fail = 1;
    }
    for (int i = 0; i < N; ++i) bram[i] = old[i];
    __sync_synchronize();
    printf("INPUT_BRAM tested_words=%d first_pattern=0x%08x first_readback=0x%08x\n", N, pat[0], got[0]);
    printf("CHECK input_bram write_readback %s\n", fail ? "FAIL" : "PASS");
    unmap_uio((void *)bram, dev->size, fd);
    return fail;
}

static int wait_reset_clear(volatile uint32_t *dma, uint32_t cr_off) {
    for (int i = 0; i < 1000; ++i) {
        if ((rd32(dma, cr_off) & 0x4u) == 0) return 0;
        usleep(1000);
    }
    return 1;
}

static void print_dma_status(const char *tag, uint32_t cr, uint32_t sr) {
    printf("%s CR=0x%08x SR=0x%08x halted=%u idle=%u err_mask=0x%03x\n",
           tag, cr, sr, sr & 1u, (sr >> 1) & 1u, sr & 0x770u);
}

static int dma_check(const struct uio_dev *dev) {
    int fd = -1;
    volatile uint32_t *dma = (volatile uint32_t *)map_uio(dev, &fd);
    if (dma == MAP_FAILED) return 1;

    uint32_t mm2s_cr0 = rd32(dma, 0x00);
    uint32_t mm2s_sr0 = rd32(dma, 0x04);
    uint32_t s2mm_cr0 = rd32(dma, 0x30);
    uint32_t s2mm_sr0 = rd32(dma, 0x34);
    print_dma_status("AXI_DMA before MM2S", mm2s_cr0, mm2s_sr0);
    print_dma_status("AXI_DMA before S2MM", s2mm_cr0, s2mm_sr0);

    wr32(dma, 0x00, 0x00000004u);
    wr32(dma, 0x30, 0x00000004u);
    int reset_fail = wait_reset_clear(dma, 0x00) | wait_reset_clear(dma, 0x30);

    uint32_t mm2s_cr1 = rd32(dma, 0x00);
    uint32_t mm2s_sr1 = rd32(dma, 0x04);
    uint32_t s2mm_cr1 = rd32(dma, 0x30);
    uint32_t s2mm_sr1 = rd32(dma, 0x34);
    print_dma_status("AXI_DMA after  MM2S", mm2s_cr1, mm2s_sr1);
    print_dma_status("AXI_DMA after  S2MM", s2mm_cr1, s2mm_sr1);

    int fail = 0;
    if (reset_fail) fail = 1;
    if ((mm2s_sr1 & 0x770u) != 0) fail = 1;
    if ((s2mm_sr1 & 0x770u) != 0) fail = 1;
    printf("CHECK axi_dma reset_status %s\n", fail ? "FAIL" : "PASS");
    unmap_uio((void *)dma, dev->size, fd);
    return fail;
}

int main(void) {
    struct uio_dev dma, bram, gemv;
    int fail = 0;

    if (find_uio_by_name("axi_dma", &dma) != 0) {
        printf("CHECK find axi_dma FAIL\n");
        fail = 1;
    } else {
        printf("CHECK find axi_dma PASS\n");
        fail |= check_dev(&dma, 0x40400000ull, 0x00010000ull);
    }

    if (find_uio_by_name("input_bram", &bram) != 0) {
        printf("CHECK find input_bram FAIL\n");
        fail = 1;
    } else {
        printf("CHECK find input_bram PASS\n");
        fail |= check_dev(&bram, 0x42000000ull, 0x00010000ull);
    }

    if (find_uio_by_name("gemv_ctrl", &gemv) != 0) {
        printf("CHECK find gemv_ctrl FAIL\n");
        fail = 1;
    } else {
        printf("CHECK find gemv_ctrl PASS\n");
        fail |= check_dev(&gemv, 0x43ca0000ull, 0x00001000ull);
    }

    if (!fail) {
        fail |= gemv_check(&gemv);
        fail |= bram_check(&bram);
        fail |= dma_check(&dma);
    }

    printf("DMA_BUFFER_METHOD current=NONE provider=absent decision=BLOCK_DMA_TRANSFER_UNTIL_COHERENT_BUFFER\n");
    printf("OVERALL %s\n", fail ? "FAIL" : "PASS_REGISTER_BRAM_DMA_REGS_ONLY");
    return fail ? 1 : 0;
}

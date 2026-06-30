#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static sigjmp_buf bus_jmp;
static volatile uint64_t current_phys;
static volatile uint64_t current_offset;
static volatile uint32_t current_expected;
static volatile const char *current_op = "none";

static void on_sigbus(int signo)
{
    (void)signo;
    siglongjmp(bus_jmp, 1);
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s --phys-base <addr> --size <bytes> --pattern-count <count>\n",
            argv0);
}

static int parse_u64_arg(const char *name, const char *value, uint64_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 0);
    if (errno || end == value || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, value);
        return -1;
    }
    *out = (uint64_t)parsed;
    return 0;
}

static uint32_t make_pattern(uint64_t phys, uint64_t index)
{
    uint32_t x = (uint32_t)(phys ^ (phys >> 32) ^ (index * 0x9e3779b9u));
    return x ^ 0xa5a50000u ^ (uint32_t)index;
}

int main(int argc, char **argv)
{
    uint64_t phys_base = 0;
    uint64_t size = 0;
    uint64_t pattern_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--phys-base") == 0 && i + 1 < argc) {
            if (parse_u64_arg("--phys-base", argv[++i], &phys_base) != 0)
                return 2;
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (parse_u64_arg("--size", argv[++i], &size) != 0)
                return 2;
        } else if (strcmp(argv[i], "--pattern-count") == 0 && i + 1 < argc) {
            if (parse_u64_arg("--pattern-count", argv[++i], &pattern_count) != 0)
                return 2;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (size < sizeof(uint32_t) || pattern_count == 0) {
        usage(argv[0]);
        return 2;
    }
    if ((phys_base & 0x3u) != 0 || (size & 0x3u) != 0) {
        fprintf(stderr, "phys-base and size must be 32-bit aligned\n");
        return 2;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigbus;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGBUS, &sa, NULL) != 0) {
        fprintf(stderr, "sigaction(SIGBUS) failed: errno=%d %s\n", errno, strerror(errno));
        return 1;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
        return 1;
    }

    uint64_t page_mask = (uint64_t)page_size - 1u;
    uint64_t map_base = phys_base & ~page_mask;
    uint64_t page_delta = phys_base - map_base;
    uint64_t map_size = page_delta + size;
    if (map_size > (uint64_t)((size_t)-1)) {
        fprintf(stderr, "mapping too large for this userspace: 0x%016" PRIx64 "\n", map_size);
        return 2;
    }

    printf("s04_5_dma_carveout_smoke\n");
    printf("phys_base=0x%08" PRIx64 " size=0x%08" PRIx64 " pattern_count=%" PRIu64 "\n",
           phys_base, size, pattern_count);
    printf("page_size=%ld map_base=0x%08" PRIx64 " map_size=0x%08" PRIx64 "\n",
           page_size, map_base, map_size);

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "open /dev/mem failed: errno=%d %s\n", errno, strerror(errno));
        return 1;
    }
    printf("open /dev/mem O_RDWR|O_SYNC PASS\n");

    void *mapped = mmap(NULL, (size_t)map_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        fd, (off_t)map_base);
    if (mapped == MAP_FAILED) {
        fprintf(stderr,
                "mmap failed: phys_base=0x%08" PRIx64 " size=0x%08" PRIx64
                " errno=%d %s\n",
                phys_base, size, errno, strerror(errno));
        close(fd);
        return 1;
    }
    printf("mmap PASS virt=%p\n", mapped);

    volatile uint8_t *base = (volatile uint8_t *)mapped + page_delta;
    uint64_t max_offset = size - sizeof(uint32_t);
    uint64_t step = pattern_count > 1 ? max_offset / (pattern_count - 1) : 0;
    step &= ~UINT64_C(3);

    uint64_t pass_count = 0;
    for (uint64_t i = 0; i < pattern_count; i++) {
        uint64_t offset;
        if (i + 1 == pattern_count) {
            offset = max_offset & ~UINT64_C(3);
        } else {
            offset = (i * step) & ~UINT64_C(3);
        }
        uint64_t phys = phys_base + offset;
        uint32_t expected = make_pattern(phys, i);
        current_phys = phys;
        current_offset = offset;
        current_expected = expected;

        if (sigsetjmp(bus_jmp, 1) != 0) {
            fprintf(stderr,
                    "SIGBUS during %s: phys=0x%08" PRIx64
                    " offset=0x%08" PRIx64 " expected=0x%08" PRIx32 "\n",
                    (const char *)current_op,
                    (uint64_t)current_phys,
                    (uint64_t)current_offset,
                    (uint32_t)current_expected);
            munmap(mapped, (size_t)map_size);
            close(fd);
            return 1;
        }

        volatile uint32_t *ptr = (volatile uint32_t *)(base + offset);
        current_op = "write";
        *ptr = expected;
        __sync_synchronize();

        current_op = "read";
        uint32_t actual = *ptr;
        __sync_synchronize();

        if (actual != expected) {
            fprintf(stderr,
                    "MISMATCH: phys=0x%08" PRIx64 " offset=0x%08" PRIx64
                    " expected=0x%08" PRIx32 " actual=0x%08" PRIx32
                    " errno=%d %s\n",
                    phys, offset, expected, actual, errno, strerror(errno));
            munmap(mapped, (size_t)map_size);
            close(fd);
            return 1;
        }

        printf("PASS[%03" PRIu64 "]: phys=0x%08" PRIx64
               " offset=0x%08" PRIx64 " pattern=0x%08" PRIx32 "\n",
               i, phys, offset, expected);
        pass_count++;
    }

    if (munmap(mapped, (size_t)map_size) != 0) {
        fprintf(stderr, "munmap failed: errno=%d %s\n", errno, strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);

    printf("SUMMARY PASS patterns=%" PRIu64 " phys_base=0x%08" PRIx64
           " size=0x%08" PRIx64 "\n",
           pass_count, phys_base, size);
    return 0;
}

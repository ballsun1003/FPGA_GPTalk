#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    GGML_TYPE_F32 = 0,
    GGML_TYPE_Q8_0 = 8,
    GGUF_TYPE_UINT8 = 0,
    GGUF_TYPE_INT8 = 1,
    GGUF_TYPE_UINT16 = 2,
    GGUF_TYPE_INT16 = 3,
    GGUF_TYPE_UINT32 = 4,
    GGUF_TYPE_INT32 = 5,
    GGUF_TYPE_FLOAT32 = 6,
    GGUF_TYPE_BOOL = 7,
    GGUF_TYPE_STRING = 8,
    GGUF_TYPE_ARRAY = 9,
    GGUF_TYPE_UINT64 = 10,
    GGUF_TYPE_INT64 = 11,
    GGUF_TYPE_FLOAT64 = 12,

    Q8_BLOCK = 32,
    Q8_BLOCK_BYTES = 34,
    LANES = 16,
    SCALE_SHIFT = 20,
    DMA_LENGTH_WIDTH = 14,
    DMA_MAX_SIMPLE_BYTES = (1u << DMA_LENGTH_WIDTH) - 1u,
    MAX_TENSORS = 512,
    MAX_DIMS = 4,
    MAX_LAYERS = 64,
    MAX_PROMPT_TOKENS = 1024,

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

typedef struct {
    char *key;
    int value;
} hash_entry_t;

typedef struct {
    hash_entry_t *entries;
    size_t cap;
    size_t used;
} hash_table_t;

typedef struct {
    int *data;
    int len;
    int cap;
} int_vec_t;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} byte_vec_t;

typedef struct {
    char uio[256];
    char name[128];
    char dev_path[300];
    uint64_t addr;
    uint64_t size;
} uio_dev_t;

typedef struct {
    uio_dev_t dma_dev;
    uio_dev_t bram_dev;
    uio_dev_t gemv_dev;
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
    int opened;
} gemv_hw_t;

typedef struct {
    char *name;
    uint32_t n_dims;
    uint64_t dims[MAX_DIMS];
    uint32_t type;
    uint64_t raw_offset;
    uint64_t offset;
    uint64_t nbytes;
    int rows;
    int cols;
    int blocks_per_row;
} tensor_t;

typedef struct {
    char **tokens;
    int token_count;
    char **merges;
    int merge_count;
    hash_table_t vocab;
    hash_table_t merge_rank;
    int byte_to_cp[256];
    int cp_to_byte_cap;
    int *cp_to_byte;
    int bos_id;
    int eos_id;
    int endoftext_id;
    int im_start_id;
    int im_end_id;
    int unk_id;
    int add_bos;
    char *chat_template;
} tokenizer_t;

typedef struct {
    int fd;
    uint8_t *data;
    size_t size;
    tensor_t tensors[MAX_TENSORS];
    int tensor_count;
    uint32_t alignment;

    int n_layer;
    int n_embd;
    int n_head;
    int n_head_kv;
    int n_vocab;
    int n_ff;
    int n_ctx_train;
    int rope_dim;
    float rope_base;
    float rms_eps;

    tensor_t *tok_embd;
    tensor_t *output_norm;
    tensor_t *attn_norm[MAX_LAYERS];
    tensor_t *ffn_norm[MAX_LAYERS];
    tensor_t *q_proj[MAX_LAYERS];
    tensor_t *k_proj[MAX_LAYERS];
    tensor_t *v_proj[MAX_LAYERS];
    tensor_t *o_proj[MAX_LAYERS];
    tensor_t *gate_proj[MAX_LAYERS];
    tensor_t *up_proj[MAX_LAYERS];
    tensor_t *down_proj[MAX_LAYERS];
    tokenizer_t tokenizer;
} model_t;

typedef struct {
    uint64_t calls;
    uint64_t fpga_calls;
    uint64_t cpu_fallbacks;
    uint64_t fpga_rowgroup_jobs;
    uint64_t fpga_chunk_jobs;
    uint64_t fpga_repair_jobs;
    uint64_t fpga_mode1_blockacc_calls;
    uint64_t s2mm_output_bytes;
    uint64_t input_saturations;
    uint64_t fpga_ns;
    uint64_t cpu_gemv_ns;
    uint64_t cpu_scale_accum_ops;
    uint64_t cpu_scale_accum_ns;
} role_counters_t;

enum {
    GEMV_ROLE_Q = 0,
    GEMV_ROLE_K,
    GEMV_ROLE_V,
    GEMV_ROLE_O,
    GEMV_ROLE_GATE,
    GEMV_ROLE_UP,
    GEMV_ROLE_DOWN,
    GEMV_ROLE_LM_HEAD,
    GEMV_ROLE_OTHER,
    GEMV_ROLE_COUNT
};

enum {
    FPGA_REPAIR_DUPLICATE = 0,
    FPGA_REPAIR_SPARSE = 1
};

enum {
    FPGA_OUTPUT_MODE0 = 0,
    FPGA_OUTPUT_MODE1_CPU_SCALE = 1
};

typedef struct {
    uint64_t total_gemv_calls;
    uint64_t fpga_gemv_calls;
    uint64_t cpu_gemv_fallbacks;
    uint64_t fpga_rowgroup_jobs;
    uint64_t fpga_chunk_jobs;
    uint64_t fpga_repair_jobs;
    uint64_t fpga_mode1_blockacc_calls;
    uint64_t fpga_saturated_outputs_repaired;
    uint64_t fpga_saturated_outputs_unrepaired;
    uint64_t s2mm_output_bytes;
    uint64_t input_saturations;
    uint64_t fpga_ns;
    uint64_t cpu_gemv_ns;
    uint64_t cpu_non_gemv_ns;
    uint64_t cpu_scale_accum_ops;
    uint64_t cpu_scale_accum_ns;
    uint64_t tokenization_ns;
    uint64_t sampling_ns;
    role_counters_t roles[GEMV_ROLE_COUNT];
} counters_t;

typedef enum {
    BACKEND_CPU = 0,
    BACKEND_FPGA = 1
} backend_kind_t;

typedef struct {
    backend_kind_t kind;
    int require_fpga;
    int act_shift;
    int dump_layer_stats;
    int repair_mode;
    int fpga_output_mode;
    model_t *model;
    gemv_hw_t hw;
    counters_t counters;
    FILE *audit;
} gemv_backend_t;

typedef struct {
    int n_layer;
    int n_ctx;
    int kv_dim;
    float *k;
    float *v;
} kv_cache_t;

typedef struct {
    const char *model_path;
    const char *backend_name;
    const char *prompt;
    const char *audit_path;
    const char *decode_ids_arg;
    int require_fpga;
    int max_new_tokens;
    int ctx_size;
    int act_shift;
    int use_chat_template;
    int skip_special_tokens;
    int stop_on_eos;
    int dump_top_k;
    int dump_layer_stats;
    int probe_first_gemv;
    int check_packet_equivalence;
    int compare_mode1_blockacc;
    int compare_mode1_scaled_qproj;
    int compare_identity_scale;
    int onehot_localization;
    int compare_backends;
    int tokenize_only;
    int interactive;
    double temperature;
    int top_k;
    double top_p;
    int fpga_repair_mode;
    int fpga_output_mode;
    const char *packet_equivalence_csv;
    const char *mode1_blockacc_csv;
    const char *mode1_scaled_qproj_csv;
    const char *identity_scale_csv;
    const char *onehot_csv;
    const char *dump_real_qproj_fixture_dir;
} options_t;

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

static uint32_t rd32(volatile uint32_t *base, uint32_t off)
{
    return base[off / 4u];
}

static void wr32(volatile uint32_t *base, uint32_t off, uint32_t value)
{
    base[off / 4u] = value;
    __sync_synchronize();
}

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd_le64(const uint8_t *p)
{
    uint64_t lo = rd_le32(p);
    uint64_t hi = rd_le32(p + 4);
    return lo | (hi << 32);
}

static void put_le32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)(u & 0xffu);
    p[1] = (uint8_t)((u >> 8) & 0xffu);
    p[2] = (uint8_t)((u >> 16) & 0xffu);
    p[3] = (uint8_t)((u >> 24) & 0xffu);
}

static float f32_from_bits(uint32_t bits)
{
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

static float f16_to_f32(uint16_t h)
{
    uint32_t sign = ((uint32_t)h & 0x8000u) << 16;
    uint32_t exp = ((uint32_t)h >> 10) & 0x1fu;
    uint32_t frac = (uint32_t)h & 0x03ffu;
    uint32_t bits;
    if (exp == 0) {
        if (frac == 0) {
            bits = sign;
        } else {
            int exp32 = 127 - 14;
            while ((frac & 0x0400u) == 0) {
                frac <<= 1;
                exp32--;
            }
            frac &= 0x03ffu;
            bits = sign | ((uint32_t)exp32 << 23) | (frac << 13);
        }
    } else if (exp == 0x1f) {
        bits = sign | 0x7f800000u | (frac << 13);
    } else {
        bits = sign | ((exp + 112u) << 23) | (frac << 13);
    }
    return f32_from_bits(bits);
}

static uint64_t fnv1a(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 1099511628211ull;
    }
    return h;
}

static int hash_init(hash_table_t *h, size_t cap)
{
    size_t c = 1;
    while (c < cap) c <<= 1;
    h->entries = (hash_entry_t *)calloc(c, sizeof(hash_entry_t));
    if (!h->entries) return -1;
    h->cap = c;
    h->used = 0;
    return 0;
}

static int hash_put(hash_table_t *h, char *key, int value)
{
    if (!h->entries) return -1;
    size_t mask = h->cap - 1u;
    size_t i = (size_t)fnv1a(key) & mask;
    for (;;) {
        if (!h->entries[i].key) {
            h->entries[i].key = key;
            h->entries[i].value = value;
            h->used++;
            return 0;
        }
        if (strcmp(h->entries[i].key, key) == 0) {
            h->entries[i].value = value;
            return 0;
        }
        i = (i + 1u) & mask;
    }
}

static int hash_get(const hash_table_t *h, const char *key, int *value)
{
    if (!h->entries) return 0;
    size_t mask = h->cap - 1u;
    size_t i = (size_t)fnv1a(key) & mask;
    for (;;) {
        if (!h->entries[i].key) return 0;
        if (strcmp(h->entries[i].key, key) == 0) {
            *value = h->entries[i].value;
            return 1;
        }
        i = (i + 1u) & mask;
    }
}

static int vec_push(int_vec_t *v, int x)
{
    if (v->len == v->cap) {
        int nc = v->cap ? v->cap * 2 : 64;
        int *nd = (int *)realloc(v->data, (size_t)nc * sizeof(int));
        if (!nd) return -1;
        v->data = nd;
        v->cap = nc;
    }
    v->data[v->len++] = x;
    return 0;
}

static int byte_push(byte_vec_t *v, uint8_t x)
{
    if (v->len == v->cap) {
        size_t nc = v->cap ? v->cap * 2u : 128u;
        uint8_t *nd = (uint8_t *)realloc(v->data, nc);
        if (!nd) return -1;
        v->data = nd;
        v->cap = nc;
    }
    v->data[v->len++] = x;
    return 0;
}

static int byte_append(byte_vec_t *v, const void *data, size_t n)
{
    if (n == 0) return 0;
    if (v->len + n > v->cap) {
        size_t nc = v->cap ? v->cap : 128u;
        while (nc < v->len + n) nc *= 2u;
        uint8_t *nd = (uint8_t *)realloc(v->data, nc);
        if (!nd) return -1;
        v->data = nd;
        v->cap = nc;
    }
    memcpy(v->data + v->len, data, n);
    v->len += n;
    return 0;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1u);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int utf8_encode(int cp, char out[8])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        out[1] = 0;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        out[2] = 0;
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        out[3] = 0;
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    out[4] = 0;
    return 4;
}

static int utf8_decode_one(const char *s, int *cp, int *len)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) {
        *cp = p[0];
        *len = 1;
        return 0;
    }
    if ((p[0] & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        *cp = ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        *len = 2;
        return 0;
    }
    if ((p[0] & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        *cp = ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        *len = 3;
        return 0;
    }
    if ((p[0] & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 &&
        (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
        *cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3f) << 12) |
              ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
        *len = 4;
        return 0;
    }
    return -1;
}

static int utf8_decode_one_checked(const uint8_t *p, size_t n, int *cp, int *len)
{
    if (n == 0) return -1;
    if (p[0] < 0x80) {
        *cp = p[0];
        *len = 1;
        return 0;
    }
    if (p[0] >= 0xc2 && p[0] <= 0xdf) {
        if (n < 2 || (p[1] & 0xc0) != 0x80) return -1;
        *cp = ((p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        *len = 2;
        return 0;
    }
    if (p[0] >= 0xe0 && p[0] <= 0xef) {
        if (n < 3 || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80) return -1;
        if (p[0] == 0xe0 && p[1] < 0xa0) return -1;
        if (p[0] == 0xed && p[1] >= 0xa0) return -1;
        *cp = ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        *len = 3;
        return 0;
    }
    if (p[0] >= 0xf0 && p[0] <= 0xf4) {
        if (n < 4 || (p[1] & 0xc0) != 0x80 ||
            (p[2] & 0xc0) != 0x80 || (p[3] & 0xc0) != 0x80) return -1;
        if (p[0] == 0xf0 && p[1] < 0x90) return -1;
        if (p[0] == 0xf4 && p[1] >= 0x90) return -1;
        *cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3f) << 12) |
              ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
        *len = 4;
        return 0;
    }
    return -1;
}

static void tokenizer_init_byte_maps(tokenizer_t *tok)
{
    int bs[256];
    int cs[256];
    int nbs = 0;
    for (int b = '!'; b <= '~'; ++b) bs[nbs++] = b;
    for (int b = 0xa1; b <= 0xac; ++b) bs[nbs++] = b;
    for (int b = 0xae; b <= 0xff; ++b) bs[nbs++] = b;
    for (int i = 0; i < nbs; ++i) cs[i] = bs[i];
    int n = 0;
    for (int b = 0; b < 256; ++b) {
        int found = 0;
        for (int i = 0; i < nbs; ++i) {
            if (bs[i] == b) {
                found = 1;
                break;
            }
        }
        if (!found) {
            bs[nbs] = b;
            cs[nbs] = 256 + n++;
            nbs++;
        }
    }
    tok->cp_to_byte_cap = 512;
    tok->cp_to_byte = (int *)malloc((size_t)tok->cp_to_byte_cap * sizeof(int));
    for (int i = 0; i < tok->cp_to_byte_cap; ++i) tok->cp_to_byte[i] = -1;
    for (int i = 0; i < 256; ++i) {
        tok->byte_to_cp[bs[i]] = cs[i];
        if (cs[i] >= 0 && cs[i] < tok->cp_to_byte_cap) tok->cp_to_byte[cs[i]] = bs[i];
    }
}

static int byte_to_token_string(tokenizer_t *tok, uint8_t b, char **out)
{
    char tmp[8];
    int len = utf8_encode(tok->byte_to_cp[b], tmp);
    *out = xstrndup(tmp, (size_t)len);
    return *out ? 0 : -1;
}

typedef struct {
    char **s;
    int n;
    int cap;
} sym_vec_t;

static int sym_push(sym_vec_t *v, char *s)
{
    if (v->n == v->cap) {
        int nc = v->cap ? v->cap * 2 : 32;
        char **ns = (char **)realloc(v->s, (size_t)nc * sizeof(char *));
        if (!ns) return -1;
        v->s = ns;
        v->cap = nc;
    }
    v->s[v->n++] = s;
    return 0;
}

static char *concat2(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = (char *)malloc(la + lb + 1u);
    if (!p) return NULL;
    memcpy(p, a, la);
    memcpy(p + la, b, lb + 1u);
    return p;
}

static char *pair_key(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = (char *)malloc(la + lb + 2u);
    if (!p) return NULL;
    memcpy(p, a, la);
    p[la] = ' ';
    memcpy(p + la + 1u, b, lb + 1u);
    return p;
}

static int bpe_encode_segment(tokenizer_t *tok, const uint8_t *bytes, int n, int_vec_t *ids)
{
    sym_vec_t syms = {0};
    for (int i = 0; i < n; ++i) {
        char *s = NULL;
        if (byte_to_token_string(tok, bytes[i], &s) || sym_push(&syms, s)) return -1;
    }

    while (syms.n > 1) {
        int best = -1;
        int best_rank = INT32_MAX;
        for (int i = 0; i + 1 < syms.n; ++i) {
            char *pk = pair_key(syms.s[i], syms.s[i + 1]);
            if (!pk) return -1;
            int rank = 0;
            int ok = hash_get(&tok->merge_rank, pk, &rank);
            free(pk);
            if (ok && rank < best_rank) {
                best_rank = rank;
                best = i;
            }
        }
        if (best < 0) break;
        char *merged = concat2(syms.s[best], syms.s[best + 1]);
        if (!merged) return -1;
        free(syms.s[best]);
        free(syms.s[best + 1]);
        syms.s[best] = merged;
        for (int j = best + 1; j + 1 < syms.n; ++j) syms.s[j] = syms.s[j + 1];
        syms.n--;
    }

    for (int i = 0; i < syms.n; ++i) {
        int id = tok->unk_id;
        (void)hash_get(&tok->vocab, syms.s[i], &id);
        if (vec_push(ids, id)) return -1;
        free(syms.s[i]);
    }
    free(syms.s);
    return 0;
}

static int is_word_byte(uint8_t c)
{
    return isalnum((unsigned char)c) || c == '\'';
}

static int tokenizer_encode_plain(tokenizer_t *tok, const char *text, int_vec_t *ids)
{
    const uint8_t *p = (const uint8_t *)text;
    int n = (int)strlen(text);
    int i = 0;
    while (i < n) {
        int start = i;
        if (p[i] == ' ') {
            while (i < n && p[i] == ' ') i++;
            if (i < n && is_word_byte(p[i])) {
                while (i < n && is_word_byte(p[i])) i++;
            } else if (i < n && !isspace((unsigned char)p[i])) {
                while (i < n && !isspace((unsigned char)p[i]) && !is_word_byte(p[i])) i++;
            }
        } else if (is_word_byte(p[i])) {
            while (i < n && is_word_byte(p[i])) i++;
        } else if (isspace((unsigned char)p[i])) {
            while (i < n && isspace((unsigned char)p[i]) && p[i] != ' ') i++;
        } else {
            while (i < n && !isspace((unsigned char)p[i]) && !is_word_byte(p[i])) i++;
        }
        if (i == start) i++;
        if (bpe_encode_segment(tok, p + start, i - start, ids)) return -1;
    }
    return 0;
}

static int starts_with_at(const char *s, int pos, const char *needle)
{
    size_t n = strlen(needle);
    return strncmp(s + pos, needle, n) == 0;
}

static int tokenizer_encode(tokenizer_t *tok, const char *text, int_vec_t *ids)
{
    int n = (int)strlen(text);
    int i = 0;
    while (i < n) {
        if (starts_with_at(text, i, "<|im_start|>")) {
            if (vec_push(ids, tok->im_start_id)) return -1;
            i += 12;
            continue;
        }
        if (starts_with_at(text, i, "<|im_end|>")) {
            if (vec_push(ids, tok->im_end_id)) return -1;
            i += 10;
            continue;
        }
        if (starts_with_at(text, i, "<|endoftext|>")) {
            if (vec_push(ids, tok->endoftext_id)) return -1;
            i += 13;
            continue;
        }
        int start = i;
        while (i < n && !starts_with_at(text, i, "<|im_start|>") &&
               !starts_with_at(text, i, "<|im_end|>") &&
               !starts_with_at(text, i, "<|endoftext|>")) {
            i++;
        }
        char *chunk = xstrndup(text + start, (size_t)(i - start));
        if (!chunk) return -1;
        int rc = tokenizer_encode_plain(tok, chunk, ids);
        free(chunk);
        if (rc) return rc;
    }
    return 0;
}

static int tokenizer_is_special_id(tokenizer_t *tok, int id)
{
    if (id == tok->bos_id || id == tok->eos_id || id == tok->endoftext_id ||
        id == tok->im_start_id || id == tok->im_end_id) {
        return 1;
    }
    if (id >= 0 && id < tok->token_count && strncmp(tok->tokens[id], "<|", 2) == 0) return 1;
    return 0;
}

static int tokenizer_append_token_bytes(tokenizer_t *tok, int id, int skip_special,
                                        byte_vec_t *raw, int *special_skipped)
{
    if (id < 0 || id >= tok->token_count) {
        const char *bad = "<bad_token>";
        return byte_append(raw, bad, strlen(bad));
    }
    const char *s = tok->tokens[id];
    if (tokenizer_is_special_id(tok, id)) {
        if (skip_special) {
            if (special_skipped) (*special_skipped)++;
            return 0;
        }
        return byte_append(raw, s, strlen(s));
    }
    for (size_t i = 0; s[i];) {
        int cp = 0, len = 0;
        if (utf8_decode_one(s + i, &cp, &len)) {
            if (byte_push(raw, (uint8_t)'?')) return -1;
            i++;
            continue;
        }
        i += (size_t)len;
        int b = -1;
        if (cp >= 0 && cp < tok->cp_to_byte_cap) b = tok->cp_to_byte[cp];
        if (b >= 0) {
            if (byte_push(raw, (uint8_t)b)) return -1;
        } else {
            char tmp[8];
            int l = utf8_encode(cp, tmp);
            if (byte_append(raw, tmp, (size_t)l)) return -1;
        }
    }
    return 0;
}

static char *sanitize_utf8_replace(const uint8_t *raw, size_t n, int *replacement_count)
{
    byte_vec_t out = {0};
    size_t i = 0;
    if (replacement_count) *replacement_count = 0;
    while (i < n) {
        int cp = 0, len = 0;
        if (utf8_decode_one_checked(raw + i, n - i, &cp, &len) == 0) {
            (void)cp;
            if (byte_append(&out, raw + i, (size_t)len)) {
                free(out.data);
                return NULL;
            }
            i += (size_t)len;
        } else {
            static const uint8_t repl[] = {0xef, 0xbf, 0xbd};
            if (byte_append(&out, repl, sizeof(repl))) {
                free(out.data);
                return NULL;
            }
            if (replacement_count) (*replacement_count)++;
            i++;
        }
    }
    if (byte_push(&out, 0)) {
        free(out.data);
        return NULL;
    }
    return (char *)out.data;
}

static char *hex_encode_bytes(const uint8_t *raw, size_t n)
{
    static const char hexdig[] = "0123456789abcdef";
    size_t cap = n ? n * 3u : 1u;
    char *out = (char *)malloc(cap + 1u);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; ++i) {
        if (i) out[o++] = ' ';
        out[o++] = hexdig[raw[i] >> 4];
        out[o++] = hexdig[raw[i] & 0x0f];
    }
    out[o] = 0;
    return out;
}

static char *tokenizer_decode_ids_ex(tokenizer_t *tok, const int *ids, int n,
                                     int skip_special, int *replacement_count,
                                     int *special_skipped, char **raw_hex)
{
    byte_vec_t raw = {0};
    int skipped = 0;
    for (int i = 0; i < n; ++i) {
        if (tokenizer_append_token_bytes(tok, ids[i], skip_special, &raw, &skipped)) {
            free(raw.data);
            return NULL;
        }
    }
    char *text = sanitize_utf8_replace(raw.data, raw.len, replacement_count);
    if (raw_hex) *raw_hex = hex_encode_bytes(raw.data, raw.len);
    if (special_skipped) *special_skipped = skipped;
    free(raw.data);
    return text;
}

static int read_exact(FILE *f, void *dst, size_t n)
{
    return fread(dst, 1, n, f) == n ? 0 : -1;
}

static uint32_t read_u32_file(FILE *f)
{
    uint8_t b[4];
    if (read_exact(f, b, sizeof(b))) return 0;
    return rd_le32(b);
}

static uint64_t read_u64_file(FILE *f)
{
    uint8_t b[8];
    if (read_exact(f, b, sizeof(b))) return 0;
    return rd_le64(b);
}

static uint64_t read_count_file(FILE *f, uint32_t version)
{
    return version == 1 ? read_u32_file(f) : read_u64_file(f);
}

static char *read_string_file(FILE *f, uint32_t version)
{
    uint64_t n = read_count_file(f, version);
    if (n > (1ull << 32)) return NULL;
    char *s = (char *)malloc((size_t)n + 1u);
    if (!s) return NULL;
    if (read_exact(f, s, (size_t)n)) {
        free(s);
        return NULL;
    }
    s[n] = 0;
    return s;
}

static int skip_bytes(FILE *f, uint64_t n)
{
    return fseeko(f, (off_t)n, SEEK_CUR);
}

static int skip_value(FILE *f, uint32_t version, uint32_t type)
{
    switch (type) {
    case GGUF_TYPE_UINT8:
    case GGUF_TYPE_INT8:
    case GGUF_TYPE_BOOL:
        return skip_bytes(f, 1);
    case GGUF_TYPE_UINT16:
    case GGUF_TYPE_INT16:
        return skip_bytes(f, 2);
    case GGUF_TYPE_UINT32:
    case GGUF_TYPE_INT32:
    case GGUF_TYPE_FLOAT32:
        return skip_bytes(f, 4);
    case GGUF_TYPE_UINT64:
    case GGUF_TYPE_INT64:
    case GGUF_TYPE_FLOAT64:
        return skip_bytes(f, 8);
    case GGUF_TYPE_STRING: {
        uint64_t n = read_count_file(f, version);
        return skip_bytes(f, n);
    }
    case GGUF_TYPE_ARRAY: {
        uint32_t item_type = read_u32_file(f);
        uint64_t count = read_count_file(f, version);
        for (uint64_t i = 0; i < count; ++i) {
            if (skip_value(f, version, item_type)) return -1;
        }
        return 0;
    }
    default:
        return -1;
    }
}

static int read_scalar_i64(FILE *f, uint32_t type, int64_t *out)
{
    uint8_t b[8] = {0};
    switch (type) {
    case GGUF_TYPE_UINT32:
    case GGUF_TYPE_INT32:
        if (read_exact(f, b, 4)) return -1;
        *out = (int32_t)rd_le32(b);
        return 0;
    case GGUF_TYPE_UINT64:
    case GGUF_TYPE_INT64:
        if (read_exact(f, b, 8)) return -1;
        *out = (int64_t)rd_le64(b);
        return 0;
    default:
        return -1;
    }
}

static int read_scalar_f32(FILE *f, uint32_t type, float *out)
{
    uint8_t b[8] = {0};
    if (type == GGUF_TYPE_FLOAT32) {
        if (read_exact(f, b, 4)) return -1;
        *out = f32_from_bits(rd_le32(b));
        return 0;
    }
    if (type == GGUF_TYPE_FLOAT64) {
        double d = 0.0;
        if (read_exact(f, b, 8)) return -1;
        uint64_t u = rd_le64(b);
        memcpy(&d, &u, sizeof(d));
        *out = (float)d;
        return 0;
    }
    return -1;
}

static uint64_t tensor_nbytes(uint32_t type, const uint64_t *dims, uint32_t n_dims)
{
    uint64_t elems = 1;
    for (uint32_t i = 0; i < n_dims; ++i) elems *= dims[i];
    if (type == GGML_TYPE_F32) return elems * 4u;
    if (type == GGML_TYPE_Q8_0) return ((elems + Q8_BLOCK - 1u) / Q8_BLOCK) * Q8_BLOCK_BYTES;
    return 0;
}

static int model_parse_gguf(model_t *m, const char *path)
{
    memset(m, 0, sizeof(*m));
    m->alignment = 32;
    m->rope_base = 10000.0f;
    m->rms_eps = 1e-5f;
    m->fd = open(path, O_RDONLY);
    if (m->fd < 0) {
        fprintf(stderr, "open model failed: %s: %s\n", path, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(m->fd, &st)) {
        perror("fstat model");
        return -1;
    }
    m->size = (size_t)st.st_size;
    m->data = (uint8_t *)mmap(NULL, m->size, PROT_READ, MAP_SHARED, m->fd, 0);
    if (m->data == MAP_FAILED) {
        perror("mmap model");
        return -1;
    }

    FILE *f = fdopen(dup(m->fd), "rb");
    if (!f) return -1;
    uint8_t magic[4];
    if (read_exact(f, magic, 4) || memcmp(magic, "GGUF", 4) != 0) {
        fprintf(stderr, "not a GGUF file\n");
        fclose(f);
        return -1;
    }
    uint32_t version = read_u32_file(f);
    uint64_t tensor_count = read_count_file(f, version);
    uint64_t meta_count = read_count_file(f, version);
    if (tensor_count > MAX_TENSORS) {
        fprintf(stderr, "too many tensors: %" PRIu64 "\n", tensor_count);
        fclose(f);
        return -1;
    }

    tokenizer_init_byte_maps(&m->tokenizer);
    m->tokenizer.bos_id = 1;
    m->tokenizer.eos_id = 2;
    m->tokenizer.endoftext_id = 0;
    m->tokenizer.im_start_id = 1;
    m->tokenizer.im_end_id = 2;
    m->tokenizer.unk_id = 0;

    for (uint64_t mi = 0; mi < meta_count; ++mi) {
        char *key = read_string_file(f, version);
        uint32_t type = read_u32_file(f);
        if (!key) {
            fclose(f);
            return -1;
        }
        int handled = 0;
        int64_t iv = 0;
        float fv = 0.0f;
        if (strcmp(key, "general.alignment") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->alignment = (uint32_t)iv;
            handled = 1;
        } else if (strcmp(key, "llama.block_count") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->n_layer = (int)iv;
            handled = 1;
        } else if (strcmp(key, "llama.embedding_length") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->n_embd = (int)iv;
            handled = 1;
        } else if (strcmp(key, "llama.attention.head_count") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->n_head = (int)iv;
            handled = 1;
        } else if (strcmp(key, "llama.attention.head_count_kv") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->n_head_kv = (int)iv;
            handled = 1;
        } else if (strcmp(key, "llama.vocab_size") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->n_vocab = (int)iv;
            handled = 1;
        } else if (strcmp(key, "llama.feed_forward_length") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->n_ff = (int)iv;
            handled = 1;
        } else if (strcmp(key, "llama.context_length") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->n_ctx_train = (int)iv;
            handled = 1;
        } else if (strcmp(key, "llama.rope.dimension_count") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->rope_dim = (int)iv;
            handled = 1;
        } else if (strcmp(key, "llama.rope.freq_base") == 0 && read_scalar_f32(f, type, &fv) == 0) {
            m->rope_base = fv;
            handled = 1;
        } else if (strcmp(key, "llama.attention.layer_norm_rms_epsilon") == 0 && read_scalar_f32(f, type, &fv) == 0) {
            m->rms_eps = fv;
            handled = 1;
        } else if (strcmp(key, "tokenizer.ggml.bos_token_id") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->tokenizer.bos_id = (int)iv;
            handled = 1;
        } else if (strcmp(key, "tokenizer.ggml.eos_token_id") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->tokenizer.eos_id = (int)iv;
            handled = 1;
        } else if (strcmp(key, "tokenizer.ggml.unknown_token_id") == 0 && read_scalar_i64(f, type, &iv) == 0) {
            m->tokenizer.unk_id = (int)iv;
            handled = 1;
        } else if (strcmp(key, "tokenizer.chat_template") == 0 && type == GGUF_TYPE_STRING) {
            m->tokenizer.chat_template = read_string_file(f, version);
            handled = 1;
        } else if ((strcmp(key, "tokenizer.ggml.tokens") == 0 ||
                    strcmp(key, "tokenizer.ggml.merges") == 0) &&
                   type == GGUF_TYPE_ARRAY) {
            uint32_t item_type = read_u32_file(f);
            uint64_t count = read_count_file(f, version);
            if (item_type != GGUF_TYPE_STRING || count > 1000000u) {
                free(key);
                fclose(f);
                return -1;
            }
            char **arr = (char **)calloc((size_t)count, sizeof(char *));
            if (!arr) {
                free(key);
                fclose(f);
                return -1;
            }
            for (uint64_t i = 0; i < count; ++i) {
                arr[i] = read_string_file(f, version);
                if (!arr[i]) {
                    free(key);
                    fclose(f);
                    return -1;
                }
            }
            if (strcmp(key, "tokenizer.ggml.tokens") == 0) {
                m->tokenizer.tokens = arr;
                m->tokenizer.token_count = (int)count;
            } else {
                m->tokenizer.merges = arr;
                m->tokenizer.merge_count = (int)count;
            }
            handled = 1;
        }
        if (!handled) {
            if (skip_value(f, version, type)) {
                fprintf(stderr, "failed to skip metadata key %s type %u\n", key, type);
                free(key);
                fclose(f);
                return -1;
            }
        }
        free(key);
    }

    m->tensor_count = (int)tensor_count;
    for (int ti = 0; ti < m->tensor_count; ++ti) {
        tensor_t *t = &m->tensors[ti];
        t->name = read_string_file(f, version);
        t->n_dims = read_u32_file(f);
        if (t->n_dims > MAX_DIMS) {
            fprintf(stderr, "tensor %s has too many dims\n", t->name ? t->name : "?");
            fclose(f);
            return -1;
        }
        for (uint32_t d = 0; d < t->n_dims; ++d) t->dims[d] = read_u64_file(f);
        t->type = read_u32_file(f);
        t->raw_offset = read_u64_file(f);
        t->nbytes = tensor_nbytes(t->type, t->dims, t->n_dims);
    }
    uint64_t data_start = align_up_u64((uint64_t)ftello(f), m->alignment ? m->alignment : 32u);
    fclose(f);

    for (int ti = 0; ti < m->tensor_count; ++ti) {
        tensor_t *t = &m->tensors[ti];
        t->offset = data_start + t->raw_offset;
        if (t->n_dims >= 2 && t->type == GGML_TYPE_Q8_0) {
            t->cols = (int)t->dims[0];
            int rows = 1;
            for (uint32_t d = 1; d < t->n_dims; ++d) rows *= (int)t->dims[d];
            t->rows = rows;
            t->blocks_per_row = t->cols / Q8_BLOCK;
        } else if (t->n_dims == 1) {
            t->rows = (int)t->dims[0];
            t->cols = 1;
        }
    }

    if (!m->rope_dim && m->n_embd && m->n_head) m->rope_dim = m->n_embd / m->n_head;
    return 0;
}

static tensor_t *find_tensor(model_t *m, const char *name)
{
    for (int i = 0; i < m->tensor_count; ++i) {
        if (strcmp(m->tensors[i].name, name) == 0) return &m->tensors[i];
    }
    return NULL;
}

static int model_resolve_tensors(model_t *m)
{
    char name[128];
    m->tok_embd = find_tensor(m, "token_embd.weight");
    m->output_norm = find_tensor(m, "output_norm.weight");
    if (!m->tok_embd || !m->output_norm) return -1;
    if (m->n_layer <= 0 || m->n_layer > MAX_LAYERS) return -1;
    for (int l = 0; l < m->n_layer; ++l) {
        snprintf(name, sizeof(name), "blk.%d.attn_norm.weight", l);
        m->attn_norm[l] = find_tensor(m, name);
        snprintf(name, sizeof(name), "blk.%d.ffn_norm.weight", l);
        m->ffn_norm[l] = find_tensor(m, name);
        snprintf(name, sizeof(name), "blk.%d.attn_q.weight", l);
        m->q_proj[l] = find_tensor(m, name);
        snprintf(name, sizeof(name), "blk.%d.attn_k.weight", l);
        m->k_proj[l] = find_tensor(m, name);
        snprintf(name, sizeof(name), "blk.%d.attn_v.weight", l);
        m->v_proj[l] = find_tensor(m, name);
        snprintf(name, sizeof(name), "blk.%d.attn_output.weight", l);
        m->o_proj[l] = find_tensor(m, name);
        snprintf(name, sizeof(name), "blk.%d.ffn_gate.weight", l);
        m->gate_proj[l] = find_tensor(m, name);
        snprintf(name, sizeof(name), "blk.%d.ffn_up.weight", l);
        m->up_proj[l] = find_tensor(m, name);
        snprintf(name, sizeof(name), "blk.%d.ffn_down.weight", l);
        m->down_proj[l] = find_tensor(m, name);
        if (!m->attn_norm[l] || !m->ffn_norm[l] || !m->q_proj[l] || !m->k_proj[l] ||
            !m->v_proj[l] || !m->o_proj[l] || !m->gate_proj[l] || !m->up_proj[l] ||
            !m->down_proj[l]) {
            fprintf(stderr, "missing tensor in layer %d\n", l);
            return -1;
        }
    }
    return 0;
}

static int tokenizer_build_hashes(tokenizer_t *tok)
{
    if (!tok->tokens || tok->token_count <= 0 || !tok->merges || tok->merge_count <= 0) {
        return -1;
    }
    if (hash_init(&tok->vocab, (size_t)tok->token_count * 4u)) return -1;
    for (int i = 0; i < tok->token_count; ++i) {
        if (hash_put(&tok->vocab, tok->tokens[i], i)) return -1;
    }
    if (hash_init(&tok->merge_rank, (size_t)tok->merge_count * 4u)) return -1;
    for (int i = 0; i < tok->merge_count; ++i) {
        if (hash_put(&tok->merge_rank, tok->merges[i], i)) return -1;
    }
    int id = 0;
    if (hash_get(&tok->vocab, "<|im_start|>", &id)) tok->im_start_id = id;
    if (hash_get(&tok->vocab, "<|im_end|>", &id)) tok->im_end_id = id;
    if (hash_get(&tok->vocab, "<|endoftext|>", &id)) tok->endoftext_id = id;
    return 0;
}

static const uint8_t *tensor_row_ptr(model_t *m, tensor_t *t, int row)
{
    return m->data + t->offset + (uint64_t)row * (uint64_t)t->blocks_per_row * Q8_BLOCK_BYTES;
}

static void dequantize_row_q8(model_t *m, tensor_t *t, int row, float *out)
{
    const uint8_t *p = tensor_row_ptr(m, t, row);
    for (int b = 0; b < t->blocks_per_row; ++b) {
        float scale = f16_to_f32(rd_le16(p + b * Q8_BLOCK_BYTES));
        const int8_t *qs = (const int8_t *)(const void *)(p + b * Q8_BLOCK_BYTES + 2);
        for (int i = 0; i < Q8_BLOCK; ++i) out[b * Q8_BLOCK + i] = scale * (float)qs[i];
    }
}

static void gemv_cpu_q8(model_t *m, tensor_t *t, const float *input, float *out)
{
    for (int r = 0; r < t->rows; ++r) {
        const uint8_t *p = tensor_row_ptr(m, t, r);
        double sum = 0.0;
        for (int b = 0; b < t->blocks_per_row; ++b) {
            float scale = f16_to_f32(rd_le16(p + b * Q8_BLOCK_BYTES));
            const int8_t *qs = (const int8_t *)(const void *)(p + b * Q8_BLOCK_BYTES + 2);
            int col = b * Q8_BLOCK;
            for (int i = 0; i < Q8_BLOCK; ++i) {
                sum += (double)input[col + i] * (double)scale * (double)qs[i];
            }
        }
        out[r] = (float)sum;
    }
}

static int read_text_file(const char *path, char *buf, size_t len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len - 1u);
    close(fd);
    if (n < 0) return -1;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    buf[n] = 0;
    return 0;
}

static int parse_u64_file(const char *path, uint64_t *out)
{
    char buf[128];
    if (read_text_file(path, buf, sizeof(buf))) return -1;
    *out = strtoull(buf, NULL, 0);
    return 0;
}

static int find_uio_by_name(const char *name, uio_dev_t *dev)
{
    DIR *dir = opendir("/sys/class/uio");
    if (!dir) return -1;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "uio", 3) != 0) continue;
        char path[512], nbuf[128];
        snprintf(path, sizeof(path), "/sys/class/uio/%s/name", de->d_name);
        if (read_text_file(path, nbuf, sizeof(nbuf))) continue;
        if (strcmp(nbuf, name) != 0) continue;
        memset(dev, 0, sizeof(*dev));
        snprintf(dev->uio, sizeof(dev->uio), "%s", de->d_name);
        snprintf(dev->name, sizeof(dev->name), "%s", nbuf);
        snprintf(dev->dev_path, sizeof(dev->dev_path), "/dev/%s", de->d_name);
        snprintf(path, sizeof(path), "/sys/class/uio/%s/maps/map0/addr", de->d_name);
        if (parse_u64_file(path, &dev->addr)) break;
        snprintf(path, sizeof(path), "/sys/class/uio/%s/maps/map0/size", de->d_name);
        if (parse_u64_file(path, &dev->size)) break;
        closedir(dir);
        return 0;
    }
    closedir(dir);
    return -1;
}

static void *map_uio(const uio_dev_t *dev, int *fd_out)
{
    int fd = open(dev->dev_path, O_RDWR | O_SYNC);
    if (fd < 0) return MAP_FAILED;
    void *p = mmap(NULL, (size_t)dev->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        return MAP_FAILED;
    }
    *fd_out = fd;
    return p;
}

static void gemv_hw_close(gemv_hw_t *hw)
{
    if (hw->dma && hw->dma != MAP_FAILED) munmap((void *)hw->dma, (size_t)hw->dma_dev.size);
    if (hw->bram && hw->bram != MAP_FAILED) munmap((void *)hw->bram, (size_t)hw->bram_dev.size);
    if (hw->gemv && hw->gemv != MAP_FAILED) munmap((void *)hw->gemv, (size_t)hw->gemv_dev.size);
    if (hw->mem && hw->mem != MAP_FAILED) munmap(hw->mem, (size_t)hw->map_bytes);
    if (hw->dma_fd >= 0) close(hw->dma_fd);
    if (hw->bram_fd >= 0) close(hw->bram_fd);
    if (hw->gemv_fd >= 0) close(hw->gemv_fd);
    if (hw->mem_fd >= 0) close(hw->mem_fd);
    memset(hw, 0, sizeof(*hw));
    hw->dma_fd = hw->bram_fd = hw->gemv_fd = hw->mem_fd = -1;
}

static int gemv_hw_open(gemv_hw_t *hw, uint64_t phys_base, uint64_t phys_size)
{
    memset(hw, 0, sizeof(*hw));
    hw->dma_fd = hw->bram_fd = hw->gemv_fd = hw->mem_fd = -1;
    if (find_uio_by_name("axi_dma", &hw->dma_dev) ||
        find_uio_by_name("input_bram", &hw->bram_dev) ||
        find_uio_by_name("gemv_ctrl", &hw->gemv_dev)) {
        fprintf(stderr, "failed to find required UIO devices\n");
        return -1;
    }
    if (hw->dma_dev.addr != EXPECTED_DMA_ADDR || hw->bram_dev.addr != EXPECTED_BRAM_ADDR ||
        hw->gemv_dev.addr != EXPECTED_GEMV_ADDR) {
        fprintf(stderr, "UIO address mismatch dma=0x%08" PRIx64 " bram=0x%08" PRIx64
                " gemv=0x%08" PRIx64 "\n", hw->dma_dev.addr, hw->bram_dev.addr,
                hw->gemv_dev.addr);
        return -1;
    }
    if (hw->dma_dev.size < EXPECTED_DMA_SIZE || hw->bram_dev.size < EXPECTED_BRAM_SIZE ||
        hw->gemv_dev.size < EXPECTED_GEMV_SIZE) {
        fprintf(stderr, "UIO size mismatch\n");
        return -1;
    }
    hw->dma = (volatile uint32_t *)map_uio(&hw->dma_dev, &hw->dma_fd);
    hw->bram = (volatile uint32_t *)map_uio(&hw->bram_dev, &hw->bram_fd);
    hw->gemv = (volatile uint32_t *)map_uio(&hw->gemv_dev, &hw->gemv_fd);
    if (hw->dma == MAP_FAILED || hw->bram == MAP_FAILED || hw->gemv == MAP_FAILED) {
        perror("mmap uio");
        gemv_hw_close(hw);
        return -1;
    }
    uint32_t version = rd32(hw->gemv, GEMV_VERSION);
    uint32_t build = rd32(hw->gemv, GEMV_BUILD_CONFIG);
    uint32_t lanes = build & 0xffffu;
    uint32_t axis_width = (build >> 16) & 0xffffu;
    if (version != EXPECTED_GEMV_VERSION || lanes != LANES || axis_width != 128u) {
        fprintf(stderr, "unexpected GEMV VERSION=0x%08" PRIx32 " BUILD_CONFIG=0x%08" PRIx32 "\n",
                version, build);
        gemv_hw_close(hw);
        return -1;
    }
    hw->phys_base = phys_base;
    hw->phys_size = phys_size;
    hw->result_off = align_up_u64(PACKET_OFF + 10368u, 4096u);
    hw->map_bytes = align_up_u64(hw->result_off + 4096u, 4096u);
    hw->packet_phys = phys_base + PACKET_OFF;
    hw->result_phys = phys_base + hw->result_off;
    hw->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (hw->mem_fd < 0) {
        perror("open /dev/mem");
        gemv_hw_close(hw);
        return -1;
    }
    hw->mem = (uint8_t *)mmap(NULL, (size_t)hw->map_bytes, PROT_READ | PROT_WRITE,
                              MAP_SHARED, hw->mem_fd, (off_t)phys_base);
    if (hw->mem == MAP_FAILED) {
        perror("mmap /dev/mem");
        gemv_hw_close(hw);
        return -1;
    }
    hw->opened = 1;
    return 0;
}

static int wait_dma_reset_clear(volatile uint32_t *dma, uint32_t cr_off)
{
    for (int i = 0; i < 100000; ++i) {
        if ((rd32(dma, cr_off) & DMA_CR_RESET) == 0) return 0;
    }
    return -1;
}

static int gemv_hw_reset_dma(gemv_hw_t *hw)
{
    wr32(hw->dma, DMA_MM2S_CR, DMA_CR_RESET);
    wr32(hw->dma, DMA_S2MM_CR, DMA_CR_RESET);
    if (wait_dma_reset_clear(hw->dma, DMA_MM2S_CR) ||
        wait_dma_reset_clear(hw->dma, DMA_S2MM_CR)) {
        return -1;
    }
    wr32(hw->dma, DMA_MM2S_SR, DMA_SR_IRQ_CLR);
    wr32(hw->dma, DMA_S2MM_SR, DMA_SR_IRQ_CLR);
    return 0;
}

static int gemv_hw_prepare_reuse(gemv_hw_t *hw)
{
    wr32(hw->dma, DMA_MM2S_SR, DMA_SR_IRQ_CLR);
    wr32(hw->dma, DMA_S2MM_SR, DMA_SR_IRQ_CLR);
    uint32_t mm2s = rd32(hw->dma, DMA_MM2S_SR);
    uint32_t s2mm = rd32(hw->dma, DMA_S2MM_SR);
    return ((mm2s | s2mm) & DMA_SR_ERR_MASK) ? -1 : 0;
}

static int wait_dma_running(volatile uint32_t *dma, uint32_t sr_off)
{
    for (int i = 0; i < 100000; ++i) {
        if ((rd32(dma, sr_off) & DMA_SR_HALTED) == 0) return 0;
    }
    return -1;
}

static int dma_done(uint32_t sr)
{
    return ((sr & DMA_SR_IOC_IRQ) != 0u) || ((sr & DMA_SR_HALTED) == 0u && (sr & 0x2u) != 0u);
}

static int wait_done(gemv_hw_t *hw)
{
    for (int i = 0; i < 2000000; ++i) {
        uint32_t mm2s = rd32(hw->dma, DMA_MM2S_SR);
        uint32_t s2mm = rd32(hw->dma, DMA_S2MM_SR);
        uint32_t status = rd32(hw->gemv, GEMV_STATUS);
        uint32_t done = rd32(hw->gemv, GEMV_DONE);
        uint32_t err = rd32(hw->gemv, GEMV_ERROR_CODE);
        if (((mm2s | s2mm) & DMA_SR_ERR_MASK) || (status & (1u << 2)) || err) {
            fprintf(stderr, "GEMV/DMA error mm2s=0x%08x s2mm=0x%08x status=0x%08x err=0x%08x\n",
                    mm2s, s2mm, status, err);
            return -1;
        }
        if (dma_done(mm2s) && dma_done(s2mm) && ((status & (1u << 1)) || (done & 1u))) {
            return 0;
        }
    }
    fprintf(stderr, "GEMV/DMA timeout\n");
    return -1;
}

static void gemv_hw_load_input(gemv_hw_t *hw, const int16_t *input, int in_features)
{
    const uint8_t *bytes = (const uint8_t *)(const void *)input;
    uint32_t words = ((uint32_t)in_features * 2u + 3u) / 4u;
    for (uint32_t i = 0; i < words; ++i) {
        uint32_t v = 0;
        memcpy(&v, bytes + i * 4u, 4u);
        wr32(hw->bram, i * 4u, v);
    }
}

static int gemv_hw_run_packet_mode(gemv_hw_t *hw, uint8_t *packet, uint32_t packet_bytes,
                                   uint32_t in_features, uint32_t mode,
                                   uint32_t result_words, int32_t *out)
{
    if (packet_bytes > DMA_MAX_SIMPLE_BYTES) return -1;
    uint32_t result_bytes = result_words * (uint32_t)sizeof(int32_t);
    if (result_words == 0 || result_bytes > DMA_MAX_SIMPLE_BYTES || result_bytes > 4096u) return -1;
    if (gemv_hw_prepare_reuse(hw)) return -1;
    memcpy(hw->mem + PACKET_OFF, packet, packet_bytes);
    memset(hw->mem + hw->result_off, 0xcd, result_bytes);
    __sync_synchronize();

    wr32(hw->gemv, GEMV_CONTROL, 0x2u);
    wr32(hw->gemv, GEMV_DONE, 0x1u);
    wr32(hw->gemv, GEMV_MODE, mode);
    wr32(hw->gemv, GEMV_SCALE_SHIFT, SCALE_SHIFT);
    wr32(hw->gemv, GEMV_IN_FEATURES, in_features);
    wr32(hw->gemv, GEMV_OUT_FEATURES, LANES);
    wr32(hw->gemv, GEMV_INPUT_BASE, EXPECTED_BRAM_ADDR);
    wr32(hw->gemv, GEMV_WEIGHT_LENGTH, packet_bytes);
    wr32(hw->gemv, GEMV_RESULT_LENGTH, result_bytes);

    wr32(hw->dma, DMA_S2MM_CR, DMA_CR_RUNSTOP);
    if (wait_dma_running(hw->dma, DMA_S2MM_SR)) return -1;
    wr32(hw->dma, DMA_S2MM_DA, (uint32_t)hw->result_phys);
    wr32(hw->dma, DMA_S2MM_DA_MSB, 0u);
    wr32(hw->dma, DMA_S2MM_LENGTH, result_bytes);
    wr32(hw->gemv, GEMV_START, 0x1u);
    wr32(hw->dma, DMA_MM2S_CR, DMA_CR_RUNSTOP);
    if (wait_dma_running(hw->dma, DMA_MM2S_SR)) return -1;
    wr32(hw->dma, DMA_MM2S_SA, (uint32_t)hw->packet_phys);
    wr32(hw->dma, DMA_MM2S_SA_MSB, 0u);
    wr32(hw->dma, DMA_MM2S_LENGTH, packet_bytes);
    if (wait_done(hw)) return -1;

    volatile int32_t *res = (volatile int32_t *)(void *)(hw->mem + hw->result_off);
    for (uint32_t i = 0; i < result_words; ++i) out[i] = res[i];
    return 0;
}

static int gemv_hw_run_packet(gemv_hw_t *hw, uint8_t *packet, uint32_t packet_bytes,
                              uint32_t in_features, int32_t out16[LANES])
{
    return gemv_hw_run_packet_mode(hw, packet, packet_bytes, in_features, 0u,
                                   LANES, out16);
}

static int32_t sat_i64_to_i32(int64_t v)
{
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

static int64_t round_shift_signed_i64(int64_t v, int shift)
{
    if (shift <= 0) return v;
    int64_t rounding = (int64_t)1 << (shift - 1);
    if (v >= 0) return (v + rounding) >> shift;
    int64_t av = -v;
    return -((av + rounding) >> shift);
}

static void q8_fixed_ref_i32(model_t *m, tensor_t *t, const int16_t *input_i16,
                             int row_group, int col_start, int in_features,
                             int32_t out[LANES])
{
    int blocks = in_features / Q8_BLOCK;
    int block_start = col_start / Q8_BLOCK;
    for (int lane = 0; lane < LANES; ++lane) {
        int row = row_group + lane;
        int64_t row_acc = 0;
        if (row >= t->rows) {
            out[lane] = 0;
            continue;
        }
        for (int b = 0; b < blocks; ++b) {
            int full_block = block_start + b;
            const uint8_t *rp = tensor_row_ptr(m, t, row) + full_block * Q8_BLOCK_BYTES;
            float scale = f16_to_f32(rd_le16(rp));
            int32_t scale_q = (int32_t)lrintf(scale * (float)(1 << SCALE_SHIFT));
            const int8_t *qs = (const int8_t *)(const void *)(rp + 2);
            int32_t block_acc = 0;
            for (int i = 0; i < Q8_BLOCK; ++i) {
                block_acc += (int32_t)input_i16[b * Q8_BLOCK + i] * (int32_t)qs[i];
            }
            row_acc += round_shift_signed_i64((int64_t)block_acc * (int64_t)scale_q,
                                              SCALE_SHIFT);
        }
        out[lane] = sat_i64_to_i32(row_acc);
    }
}

static int build_packet_from_tensor(model_t *m, tensor_t *t, int row_group, int col_start,
                                    int in_features, uint8_t *packet, uint32_t *packet_bytes)
{
    if ((col_start % Q8_BLOCK) || (in_features % Q8_BLOCK)) return -1;
    int block_start = col_start / Q8_BLOCK;
    int blocks = in_features / Q8_BLOCK;
    uint8_t *p = packet;
    for (int b = 0; b < blocks; ++b) {
        int full_block = block_start + b;
        for (int lane = 0; lane < LANES; ++lane) {
            int row = row_group + lane;
            int32_t scale_q = 0;
            if (row < t->rows) {
                const uint8_t *rp = tensor_row_ptr(m, t, row) + full_block * Q8_BLOCK_BYTES;
                float scale = f16_to_f32(rd_le16(rp));
                scale_q = (int32_t)lrintf(scale * (float)(1 << SCALE_SHIFT));
            }
            put_le32(p, scale_q);
            p += 4;
        }
        for (int col = 0; col < Q8_BLOCK; ++col) {
            for (int lane = 0; lane < LANES; ++lane) {
                int row = row_group + lane;
                int8_t w = 0;
                if (row < t->rows) {
                    const uint8_t *rp = tensor_row_ptr(m, t, row) + full_block * Q8_BLOCK_BYTES;
                    w = ((const int8_t *)(const void *)(rp + 2))[col];
                }
                *p++ = (uint8_t)w;
            }
        }
    }
    *packet_bytes = (uint32_t)(p - packet);
    return 0;
}

static int build_packet_from_tensor_row_map(model_t *m, tensor_t *t, const int row_map[LANES],
                                            int col_start, int in_features,
                                            uint8_t *packet, uint32_t *packet_bytes)
{
    if ((col_start % Q8_BLOCK) || (in_features % Q8_BLOCK)) return -1;
    int block_start = col_start / Q8_BLOCK;
    int blocks = in_features / Q8_BLOCK;
    uint8_t *p = packet;
    for (int b = 0; b < blocks; ++b) {
        int full_block = block_start + b;
        for (int lane = 0; lane < LANES; ++lane) {
            int row = row_map[lane];
            int32_t scale_q = 0;
            if (row >= 0 && row < t->rows) {
                const uint8_t *rp = tensor_row_ptr(m, t, row) + full_block * Q8_BLOCK_BYTES;
                float scale = f16_to_f32(rd_le16(rp));
                scale_q = (int32_t)lrintf(scale * (float)(1 << SCALE_SHIFT));
            }
            put_le32(p, scale_q);
            p += 4;
        }
        for (int col = 0; col < Q8_BLOCK; ++col) {
            for (int lane = 0; lane < LANES; ++lane) {
                int row = row_map[lane];
                int8_t w = 0;
                if (row >= 0 && row < t->rows) {
                    const uint8_t *rp = tensor_row_ptr(m, t, row) + full_block * Q8_BLOCK_BYTES;
                    w = ((const int8_t *)(const void *)(rp + 2))[col];
                }
                *p++ = (uint8_t)w;
            }
        }
    }
    *packet_bytes = (uint32_t)(p - packet);
    return 0;
}

static size_t packet_block_bytes(void)
{
    return (size_t)LANES * 4u + (size_t)Q8_BLOCK * (size_t)LANES;
}

static size_t packet_scale_offset(int block, int lane)
{
    return (size_t)block * packet_block_bytes() + (size_t)lane * 4u;
}

static size_t packet_weight_offset(int block, int col, int lane)
{
    return (size_t)block * packet_block_bytes() + (size_t)LANES * 4u +
           (size_t)col * (size_t)LANES + (size_t)lane;
}

static int32_t tensor_scale_q_at(model_t *m, tensor_t *t, int row, int block)
{
    const uint8_t *rp = tensor_row_ptr(m, t, row) + block * Q8_BLOCK_BYTES;
    float scale = f16_to_f32(rd_le16(rp));
    return (int32_t)lrintf(scale * (float)(1 << SCALE_SHIFT));
}

static int8_t tensor_weight_i8_at(model_t *m, tensor_t *t, int row, int block, int col)
{
    const uint8_t *rp = tensor_row_ptr(m, t, row) + block * Q8_BLOCK_BYTES;
    return ((const int8_t *)(const void *)(rp + 2))[col];
}

static int quantize_input_chunk(const float *input, int start, int n, int act_shift,
                                int16_t *out, uint64_t *sat)
{
    float scale = (float)(1 << act_shift);
    for (int i = 0; i < n; ++i) {
        float v = input[start + i] * scale;
        long q = lrintf(v);
        if (q > 32767) {
            q = 32767;
            (*sat)++;
        } else if (q < -32768) {
            q = -32768;
            (*sat)++;
        }
        out[i] = (int16_t)q;
    }
    return 0;
}

static int gemv_role_index(const char *role)
{
    if (strcmp(role, "q_proj") == 0) return GEMV_ROLE_Q;
    if (strcmp(role, "k_proj") == 0) return GEMV_ROLE_K;
    if (strcmp(role, "v_proj") == 0) return GEMV_ROLE_V;
    if (strcmp(role, "o_proj") == 0) return GEMV_ROLE_O;
    if (strcmp(role, "gate_proj") == 0) return GEMV_ROLE_GATE;
    if (strcmp(role, "up_proj") == 0) return GEMV_ROLE_UP;
    if (strcmp(role, "down_proj") == 0) return GEMV_ROLE_DOWN;
    if (strcmp(role, "lm_head_tied_token_embd") == 0) return GEMV_ROLE_LM_HEAD;
    return GEMV_ROLE_OTHER;
}

static const char *gemv_role_name(int idx)
{
    switch (idx) {
    case GEMV_ROLE_Q: return "q_proj";
    case GEMV_ROLE_K: return "k_proj";
    case GEMV_ROLE_V: return "v_proj";
    case GEMV_ROLE_O: return "o_proj";
    case GEMV_ROLE_GATE: return "gate_proj";
    case GEMV_ROLE_UP: return "up_proj";
    case GEMV_ROLE_DOWN: return "down_proj";
    case GEMV_ROLE_LM_HEAD: return "lm_head";
    default: return "other";
    }
}

static const char *fpga_output_mode_name(int mode)
{
    return mode == FPGA_OUTPUT_MODE1_CPU_SCALE ? "mode1_cpu_scale" : "mode0";
}

static int gemv_fpga_q8(gemv_backend_t *be, tensor_t *t, const char *role, int layer,
                        const float *input, float *out, int *chunks_out, int *jobs_out)
{
    int rc = -1;
    int in_features = t->cols;
    int out_features = t->rows;
    int chunk_in = 0;
    if (in_features <= 576) chunk_in = in_features;
    else if (in_features == 1536) chunk_in = 512;
    else {
        fprintf(stderr, "unsupported FPGA GEMV in_features=%d tensor=%s\n", in_features, t->name);
        return -1;
    }
    int chunks = in_features / chunk_in;
    int row_groups = (out_features + LANES - 1) / LANES;
    int16_t *input_i16 = (int16_t *)malloc((size_t)chunk_in * sizeof(int16_t));
    uint8_t *packet = (uint8_t *)malloc(10368u);
    int64_t *acc = (int64_t *)calloc((size_t)out_features, sizeof(int64_t));
    int blocks_per_chunk = chunk_in / Q8_BLOCK;
    uint32_t mode1_result_words = (uint32_t)(blocks_per_chunk * LANES);
    int32_t *mode1_out = NULL;
    if (be->fpga_output_mode == FPGA_OUTPUT_MODE1_CPU_SCALE) {
        mode1_out = (int32_t *)malloc((size_t)mode1_result_words * sizeof(int32_t));
    }
    if (!input_i16 || !packet || !acc ||
        (be->fpga_output_mode == FPGA_OUTPUT_MODE1_CPU_SCALE && !mode1_out)) {
        goto done;
    }

    uint64_t sat_before = be->counters.input_saturations;
    int hw_jobs = 0;
    uint64_t mode1_calls = 0;
    uint64_t cpu_scale_ops = 0;
    uint64_t cpu_scale_ns = 0;
    uint64_t s2mm_bytes = 0;
    for (int ch = 0; ch < chunks; ++ch) {
        int col_start = ch * chunk_in;
        quantize_input_chunk(input, col_start, chunk_in, be->act_shift, input_i16,
                             &be->counters.input_saturations);
        gemv_hw_load_input(&be->hw, input_i16, chunk_in);
        for (int rg = 0; rg < row_groups; ++rg) {
            uint32_t packet_bytes = 0;
            int32_t out16[LANES];
            if (build_packet_from_tensor(be->model, t, rg * LANES, col_start, chunk_in,
                                         packet, &packet_bytes)) {
                goto done;
            }

            if (be->fpga_output_mode == FPGA_OUTPUT_MODE1_CPU_SCALE) {
                if (gemv_hw_run_packet_mode(&be->hw, packet, packet_bytes,
                                            (uint32_t)chunk_in, 1u,
                                            mode1_result_words, mode1_out)) {
                    fprintf(stderr, "FPGA mode1 blockacc job failed tensor=%s role=%s layer=%d row_group=%d chunk=%d\n",
                            t->name, role, layer, rg, ch);
                    goto done;
                }
                hw_jobs++;
                mode1_calls++;
                s2mm_bytes += (uint64_t)mode1_result_words * sizeof(int32_t);
                uint64_t scale0 = now_ns();
                for (int lane = 0; lane < LANES; ++lane) {
                    int row = rg * LANES + lane;
                    if (row >= out_features) continue;
                    for (int b = 0; b < blocks_per_chunk; ++b) {
                        int full_block = col_start / Q8_BLOCK + b;
                        int32_t block_acc = mode1_out[b * LANES + lane];
                        int32_t scale_q = tensor_scale_q_at(be->model, t, row, full_block);
                        acc[row] += round_shift_signed_i64((int64_t)block_acc * (int64_t)scale_q,
                                                           SCALE_SHIFT);
                        cpu_scale_ops++;
                    }
                }
                cpu_scale_ns += now_ns() - scale0;
            } else {
                if (gemv_hw_run_packet(&be->hw, packet, packet_bytes, (uint32_t)chunk_in, out16)) {
                    fprintf(stderr, "FPGA GEMV job failed tensor=%s role=%s layer=%d row_group=%d chunk=%d\n",
                            t->name, role, layer, rg, ch);
                    goto done;
                }
                hw_jobs++;
                s2mm_bytes += (uint64_t)LANES * sizeof(int32_t);
                int saturated_lanes[LANES];
                int sat_count = 0;
                for (int lane = 0; lane < LANES; ++lane) {
                    int row = rg * LANES + lane;
                    if (row >= out_features) continue;
                    if (out16[lane] == INT32_MAX || out16[lane] == INT32_MIN) {
                        saturated_lanes[sat_count++] = lane;
                    }
                }
                if (be->repair_mode == FPGA_REPAIR_SPARSE && sat_count > 1) {
                    int row_map[LANES];
                    int32_t sparse16[LANES];
                    for (int rl = 0; rl < LANES; ++rl) row_map[rl] = -1;
                    for (int si = 0; si < sat_count; ++si) {
                        int lane = saturated_lanes[si];
                        row_map[si] = rg * LANES + lane;
                    }
                    if (build_packet_from_tensor_row_map(be->model, t, row_map, col_start,
                                                         chunk_in, packet, &packet_bytes)) {
                        fprintf(stderr, "FPGA sparse repair packet failed tensor=%s role=%s layer=%d row_group=%d chunk=%d\n",
                                t->name, role, layer, rg, ch);
                        goto done;
                    }
                    if (gemv_hw_run_packet(&be->hw, packet, packet_bytes,
                                           (uint32_t)chunk_in, sparse16)) {
                        fprintf(stderr, "FPGA sparse repair job failed tensor=%s role=%s layer=%d row_group=%d chunk=%d\n",
                                t->name, role, layer, rg, ch);
                        goto done;
                    }
                    hw_jobs++;
                    s2mm_bytes += (uint64_t)LANES * sizeof(int32_t);
                    be->counters.fpga_repair_jobs++;
                    for (int si = 0; si < sat_count; ++si) {
                        int lane = saturated_lanes[si];
                        if (sparse16[si] != INT32_MAX && sparse16[si] != INT32_MIN) {
                            out16[lane] = sparse16[si];
                            be->counters.fpga_saturated_outputs_repaired++;
                        }
                    }
                }
                for (int lane = 0; lane < LANES; ++lane) {
                    int row = rg * LANES + lane;
                    if (row >= out_features) continue;
                    if (out16[lane] == INT32_MAX || out16[lane] == INT32_MIN) {
                        int row_map[LANES];
                        int32_t repair16[LANES];
                        int repaired = 0;
                        for (int rl = 0; rl < LANES; ++rl) row_map[rl] = row;
                        if (build_packet_from_tensor_row_map(be->model, t, row_map, col_start,
                                                             chunk_in, packet, &packet_bytes)) {
                            fprintf(stderr, "FPGA repair packet failed tensor=%s role=%s layer=%d row=%d chunk=%d\n",
                                    t->name, role, layer, row, ch);
                            goto done;
                        }
                        if (gemv_hw_run_packet(&be->hw, packet, packet_bytes,
                                               (uint32_t)chunk_in, repair16)) {
                            fprintf(stderr, "FPGA repair job failed tensor=%s role=%s layer=%d row=%d chunk=%d\n",
                                    t->name, role, layer, row, ch);
                            goto done;
                        }
                        hw_jobs++;
                        s2mm_bytes += (uint64_t)LANES * sizeof(int32_t);
                        be->counters.fpga_repair_jobs++;
                        for (int rl = 0; rl < LANES; ++rl) {
                            if (repair16[rl] != INT32_MAX && repair16[rl] != INT32_MIN) {
                                out16[lane] = repair16[rl];
                                repaired = 1;
                                break;
                            }
                        }
                        if (repaired) {
                            be->counters.fpga_saturated_outputs_repaired++;
                        } else {
                            be->counters.fpga_saturated_outputs_unrepaired++;
                        }
                    }
                    acc[row] += (int64_t)out16[lane];
                }
            }
        }
    }
    float inv = 1.0f / (float)(1 << be->act_shift);
    for (int r = 0; r < out_features; ++r) {
        out[r] = (float)sat_i64_to_i32(acc[r]) * inv;
    }
    be->counters.fpga_rowgroup_jobs += (uint64_t)hw_jobs;
    be->counters.fpga_chunk_jobs += (uint64_t)row_groups * (uint64_t)chunks;
    be->counters.fpga_mode1_blockacc_calls += mode1_calls;
    be->counters.cpu_scale_accum_ops += cpu_scale_ops;
    be->counters.cpu_scale_accum_ns += cpu_scale_ns;
    be->counters.s2mm_output_bytes += s2mm_bytes;
    if (chunks_out) *chunks_out = chunks;
    if (jobs_out) *jobs_out = hw_jobs;
    if (be->audit) {
        fprintf(be->audit, "%" PRIu64 ",%d,%s,%s,%s,%d,%d,%d,%d,0,%" PRIu64 "\n",
                be->counters.total_gemv_calls, layer, role, t->name,
                fpga_output_mode_name(be->fpga_output_mode), in_features, out_features,
                chunks, hw_jobs,
                be->counters.input_saturations - sat_before);
        fflush(be->audit);
    }
    rc = 0;

done:
    free(input_i16);
    free(packet);
    free(acc);
    free(mode1_out);
    return rc;
}

static int gemv_backend_run(gemv_backend_t *be, tensor_t *t, const char *role, int layer,
                            const float *input, float *out)
{
    if (!t || t->type != GGML_TYPE_Q8_0) {
        fprintf(stderr, "unsupported non-Q8_0 GEMV tensor role=%s\n", role);
        return -1;
    }
    int role_idx = gemv_role_index(role);
    role_counters_t *rc = &be->counters.roles[role_idx];
    uint64_t rowgroup_before = be->counters.fpga_rowgroup_jobs;
    uint64_t chunk_before = be->counters.fpga_chunk_jobs;
    uint64_t repair_before = be->counters.fpga_repair_jobs;
    uint64_t mode1_before = be->counters.fpga_mode1_blockacc_calls;
    uint64_t s2mm_before = be->counters.s2mm_output_bytes;
    uint64_t scale_ops_before = be->counters.cpu_scale_accum_ops;
    uint64_t scale_ns_before = be->counters.cpu_scale_accum_ns;
    uint64_t sat_before = be->counters.input_saturations;
    be->counters.total_gemv_calls++;
    rc->calls++;
    uint64_t t0 = now_ns();
    if (be->kind == BACKEND_CPU) {
        gemv_cpu_q8(be->model, t, input, out);
        uint64_t dt = now_ns() - t0;
        be->counters.cpu_gemv_ns += dt;
        rc->cpu_gemv_ns += dt;
        if (be->audit) {
            fprintf(be->audit, "%" PRIu64 ",%d,%s,%s,cpu,%d,%d,0,0,0,0\n",
                    be->counters.total_gemv_calls, layer, role, t->name, t->cols, t->rows);
        }
        return 0;
    }
    int chunks = 0, jobs = 0;
    if (gemv_fpga_q8(be, t, role, layer, input, out, &chunks, &jobs)) {
        if (be->require_fpga) return -1;
        be->counters.cpu_gemv_fallbacks++;
        rc->cpu_fallbacks++;
        rc->input_saturations += be->counters.input_saturations - sat_before;
        gemv_cpu_q8(be->model, t, input, out);
        uint64_t dt = now_ns() - t0;
        be->counters.cpu_gemv_ns += dt;
        rc->cpu_gemv_ns += dt;
        return 0;
    }
    (void)chunks;
    (void)jobs;
    be->counters.fpga_gemv_calls++;
    uint64_t dt = now_ns() - t0;
    be->counters.fpga_ns += dt;
    rc->fpga_calls++;
    rc->fpga_ns += dt;
    rc->fpga_rowgroup_jobs += be->counters.fpga_rowgroup_jobs - rowgroup_before;
    rc->fpga_chunk_jobs += be->counters.fpga_chunk_jobs - chunk_before;
    rc->fpga_repair_jobs += be->counters.fpga_repair_jobs - repair_before;
    rc->fpga_mode1_blockacc_calls += be->counters.fpga_mode1_blockacc_calls - mode1_before;
    rc->s2mm_output_bytes += be->counters.s2mm_output_bytes - s2mm_before;
    rc->input_saturations += be->counters.input_saturations - sat_before;
    rc->cpu_scale_accum_ops += be->counters.cpu_scale_accum_ops - scale_ops_before;
    rc->cpu_scale_accum_ns += be->counters.cpu_scale_accum_ns - scale_ns_before;
    return 0;
}

static int backend_open(gemv_backend_t *be, model_t *m, backend_kind_t kind,
                        int require_fpga, int act_shift, int dump_layer_stats,
                        int repair_mode, int fpga_output_mode, const char *audit_path)
{
    memset(be, 0, sizeof(*be));
    be->kind = kind;
    be->require_fpga = require_fpga;
    be->act_shift = act_shift;
    be->dump_layer_stats = dump_layer_stats;
    be->repair_mode = repair_mode;
    be->fpga_output_mode = fpga_output_mode;
    be->model = m;
    be->hw.dma_fd = be->hw.bram_fd = be->hw.gemv_fd = be->hw.mem_fd = -1;
    if (audit_path) {
        be->audit = fopen(audit_path, "w");
        if (be->audit) {
            fprintf(be->audit, "call_id,layer,role,tensor,backend,in_features,out_features,chunks,hw_jobs,fallback,input_saturations\n");
        }
    }
    if (kind == BACKEND_FPGA) {
        if (gemv_hw_open(&be->hw, DEFAULT_PHYS_BASE, DEFAULT_PHYS_SIZE)) {
            if (require_fpga) return -1;
            be->kind = BACKEND_CPU;
            be->counters.cpu_gemv_fallbacks++;
        } else if (gemv_hw_reset_dma(&be->hw)) {
            return -1;
        }
    }
    return 0;
}

static void backend_close(gemv_backend_t *be)
{
    if (be->audit) fclose(be->audit);
    if (be->hw.opened) gemv_hw_close(&be->hw);
}

static int kv_alloc(kv_cache_t *kv, model_t *m, int ctx)
{
    memset(kv, 0, sizeof(*kv));
    kv->n_layer = m->n_layer;
    kv->n_ctx = ctx;
    kv->kv_dim = m->n_head_kv * (m->n_embd / m->n_head);
    size_t elems = (size_t)kv->n_layer * (size_t)kv->n_ctx * (size_t)kv->kv_dim;
    kv->k = (float *)calloc(elems, sizeof(float));
    kv->v = (float *)calloc(elems, sizeof(float));
    return (!kv->k || !kv->v) ? -1 : 0;
}

static void kv_free(kv_cache_t *kv)
{
    free(kv->k);
    free(kv->v);
}

static float *kv_ptr(float *base, kv_cache_t *kv, int layer, int pos)
{
    return base + ((size_t)layer * (size_t)kv->n_ctx + (size_t)pos) * (size_t)kv->kv_dim;
}

static const float *tensor_f32_ptr(model_t *m, tensor_t *t)
{
    return (const float *)(const void *)(m->data + t->offset);
}

static void rms_norm(const float *x, const float *w, int n, float eps, float *out)
{
    double ss = 0.0;
    for (int i = 0; i < n; ++i) ss += (double)x[i] * (double)x[i];
    float inv = 1.0f / sqrtf((float)(ss / (double)n) + eps);
    for (int i = 0; i < n; ++i) out[i] = x[i] * inv * w[i];
}

static void apply_rope_vec(float *x, int heads, int head_dim, int rope_dim,
                           int pos, float base)
{
    for (int h = 0; h < heads; ++h) {
        float *v = x + h * head_dim;
        for (int i = 0; i < rope_dim / 2; ++i) {
            int j = 2 * i;
            float theta = (float)((double)pos / pow((double)base, (double)j / (double)rope_dim));
            float c = cosf(theta);
            float s = sinf(theta);
            float a = v[j];
            float b = v[j + 1];
            v[j] = a * c - b * s;
            v[j + 1] = a * s + b * c;
        }
    }
}

static void attention_layer(model_t *m, kv_cache_t *kv, int layer, int pos,
                            const float *q, const float *v_cur, const float *k_cur,
                            float *out)
{
    (void)v_cur;
    (void)k_cur;
    int head_dim = m->n_embd / m->n_head;
    int group = m->n_head / m->n_head_kv;
    float scale = 1.0f / sqrtf((float)head_dim);
    float *scores = (float *)malloc((size_t)(pos + 1) * sizeof(float));
    for (int i = 0; i < m->n_embd; ++i) out[i] = 0.0f;
    for (int h = 0; h < m->n_head; ++h) {
        int kvh = h / group;
        const float *qh = q + h * head_dim;
        float maxv = -INFINITY;
        for (int t = 0; t <= pos; ++t) {
            const float *kh = kv_ptr(kv->k, kv, layer, t) + kvh * head_dim;
            double dot = 0.0;
            for (int i = 0; i < head_dim; ++i) dot += (double)qh[i] * (double)kh[i];
            scores[t] = (float)dot * scale;
            if (scores[t] > maxv) maxv = scores[t];
        }
        double sum = 0.0;
        for (int t = 0; t <= pos; ++t) {
            scores[t] = expf(scores[t] - maxv);
            sum += scores[t];
        }
        float inv = sum > 0.0 ? (float)(1.0 / sum) : 0.0f;
        float *oh = out + h * head_dim;
        for (int t = 0; t <= pos; ++t) {
            float p = scores[t] * inv;
            const float *vh = kv_ptr(kv->v, kv, layer, t) + kvh * head_dim;
            for (int i = 0; i < head_dim; ++i) oh[i] += p * vh[i];
        }
    }
    free(scores);
}

static float silu(float x)
{
    return x / (1.0f + expf(-x));
}

static void dump_layer_vector_stats(gemv_backend_t *be, int layer, const char *point,
                                    const float *x, int n);

static int forward_token(model_t *m, gemv_backend_t *be, kv_cache_t *kv, int token,
                         int pos, float *hidden_norm)
{
    int n = m->n_embd;
    int kv_dim = m->n_head_kv * (m->n_embd / m->n_head);
    float *x = (float *)malloc((size_t)n * sizeof(float));
    float *xb = (float *)malloc((size_t)m->n_ff * sizeof(float));
    float *q = (float *)malloc((size_t)n * sizeof(float));
    float *k = (float *)malloc((size_t)kv_dim * sizeof(float));
    float *v = (float *)malloc((size_t)kv_dim * sizeof(float));
    float *att = (float *)malloc((size_t)n * sizeof(float));
    float *proj = (float *)malloc((size_t)m->n_ff * sizeof(float));
    float *gate = (float *)malloc((size_t)m->n_ff * sizeof(float));
    float *up = (float *)malloc((size_t)m->n_ff * sizeof(float));
    float *ff = (float *)malloc((size_t)m->n_ff * sizeof(float));
    if (!x || !xb || !q || !k || !v || !att || !proj || !gate || !up || !ff) return -1;

    if (token < 0 || token >= m->tok_embd->rows) return -1;
    dequantize_row_q8(m, m->tok_embd, token, x);
    dump_layer_vector_stats(be, -1, "embedding_output", x, n);

    uint64_t cpu0 = now_ns();
    for (int l = 0; l < m->n_layer; ++l) {
        rms_norm(x, tensor_f32_ptr(m, m->attn_norm[l]), n, m->rms_eps, xb);
        dump_layer_vector_stats(be, l, "after_input_rmsnorm", xb, n);
        be->counters.cpu_non_gemv_ns += now_ns() - cpu0;
        if (gemv_backend_run(be, m->q_proj[l], "q_proj", l, xb, q)) return -1;
        if (gemv_backend_run(be, m->k_proj[l], "k_proj", l, xb, k)) return -1;
        if (gemv_backend_run(be, m->v_proj[l], "v_proj", l, xb, v)) return -1;
        dump_layer_vector_stats(be, l, "q_proj_output", q, n);
        dump_layer_vector_stats(be, l, "k_proj_output", k, kv_dim);
        dump_layer_vector_stats(be, l, "v_proj_output", v, kv_dim);
        cpu0 = now_ns();
        apply_rope_vec(q, m->n_head, n / m->n_head, m->rope_dim, pos, m->rope_base);
        apply_rope_vec(k, m->n_head_kv, n / m->n_head, m->rope_dim, pos, m->rope_base);
        memcpy(kv_ptr(kv->k, kv, l, pos), k, (size_t)kv_dim * sizeof(float));
        memcpy(kv_ptr(kv->v, kv, l, pos), v, (size_t)kv_dim * sizeof(float));
        attention_layer(m, kv, l, pos, q, v, k, att);
        dump_layer_vector_stats(be, l, "attention_output", att, n);
        be->counters.cpu_non_gemv_ns += now_ns() - cpu0;
        if (gemv_backend_run(be, m->o_proj[l], "o_proj", l, att, proj)) return -1;
        dump_layer_vector_stats(be, l, "o_proj_output", proj, n);
        cpu0 = now_ns();
        for (int i = 0; i < n; ++i) x[i] += proj[i];
        dump_layer_vector_stats(be, l, "residual_after_attention", x, n);
        rms_norm(x, tensor_f32_ptr(m, m->ffn_norm[l]), n, m->rms_eps, xb);
        dump_layer_vector_stats(be, l, "after_post_attention_rmsnorm", xb, n);
        be->counters.cpu_non_gemv_ns += now_ns() - cpu0;
        if (gemv_backend_run(be, m->gate_proj[l], "gate_proj", l, xb, gate)) return -1;
        if (gemv_backend_run(be, m->up_proj[l], "up_proj", l, xb, up)) return -1;
        dump_layer_vector_stats(be, l, "gate_proj_output", gate, m->n_ff);
        dump_layer_vector_stats(be, l, "up_proj_output", up, m->n_ff);
        cpu0 = now_ns();
        for (int i = 0; i < m->n_ff; ++i) ff[i] = silu(gate[i]) * up[i];
        dump_layer_vector_stats(be, l, "silu_swiglu_output", ff, m->n_ff);
        be->counters.cpu_non_gemv_ns += now_ns() - cpu0;
        if (gemv_backend_run(be, m->down_proj[l], "down_proj", l, ff, proj)) return -1;
        dump_layer_vector_stats(be, l, "down_proj_output", proj, n);
        cpu0 = now_ns();
        for (int i = 0; i < n; ++i) x[i] += proj[i];
        dump_layer_vector_stats(be, l, "residual_after_mlp", x, n);
    }
    rms_norm(x, tensor_f32_ptr(m, m->output_norm), n, m->rms_eps, hidden_norm);
    dump_layer_vector_stats(be, -1, "final_rmsnorm", hidden_norm, n);
    be->counters.cpu_non_gemv_ns += now_ns() - cpu0;
    free(x);
    free(xb);
    free(q);
    free(k);
    free(v);
    free(att);
    free(proj);
    free(gate);
    free(up);
    free(ff);
    return 0;
}

static int greedy_sample(const float *logits, int n_vocab)
{
    int best = 0;
    float bv = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        if (logits[i] > bv) {
            bv = logits[i];
            best = i;
        }
    }
    return best;
}

typedef struct {
    int id;
    float logit;
} sample_candidate_t;

static int cmp_sample_candidate_desc(const void *a, const void *b)
{
    const sample_candidate_t *ca = (const sample_candidate_t *)a;
    const sample_candidate_t *cb = (const sample_candidate_t *)b;
    if (ca->logit < cb->logit) return 1;
    if (ca->logit > cb->logit) return -1;
    return ca->id - cb->id;
}

static int sample_logits(const float *logits, int n_vocab, double temperature,
                         int top_k, double top_p)
{
    if (temperature <= 0.0) return greedy_sample(logits, n_vocab);

    sample_candidate_t *cand = (sample_candidate_t *)malloc((size_t)n_vocab * sizeof(*cand));
    double *weights = (double *)malloc((size_t)n_vocab * sizeof(*weights));
    if (!cand || !weights) {
        free(cand);
        free(weights);
        return greedy_sample(logits, n_vocab);
    }

    for (int i = 0; i < n_vocab; ++i) {
        cand[i].id = i;
        cand[i].logit = logits[i];
    }
    qsort(cand, (size_t)n_vocab, sizeof(*cand), cmp_sample_candidate_desc);

    int keep = n_vocab;
    if (top_k > 0 && top_k < keep) keep = top_k;
    if (keep <= 0) keep = 1;

    double max_scaled = (double)cand[0].logit / temperature;
    double sum = 0.0;
    for (int i = 0; i < keep; ++i) {
        double scaled = (double)cand[i].logit / temperature;
        double w = exp(scaled - max_scaled);
        if (!isfinite(w) || w < 0.0) w = 0.0;
        weights[i] = w;
        sum += w;
    }
    if (sum <= 0.0 || !isfinite(sum)) {
        int id = cand[0].id;
        free(cand);
        free(weights);
        return id;
    }

    if (top_p > 0.0 && top_p < 1.0) {
        double accum = 0.0;
        int nucleus = keep;
        for (int i = 0; i < keep; ++i) {
            accum += weights[i] / sum;
            if (accum >= top_p) {
                nucleus = i + 1;
                break;
            }
        }
        if (nucleus < 1) nucleus = 1;
        keep = nucleus;
        sum = 0.0;
        for (int i = 0; i < keep; ++i) sum += weights[i];
    }

    double r = ((double)rand() / ((double)RAND_MAX + 1.0)) * sum;
    double accum = 0.0;
    int chosen = cand[keep - 1].id;
    for (int i = 0; i < keep; ++i) {
        accum += weights[i];
        if (r <= accum) {
            chosen = cand[i].id;
            break;
        }
    }
    free(cand);
    free(weights);
    return chosen;
}

static void print_escaped_text(const char *s)
{
    if (!s) {
        printf("<decode_fail>");
        return;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '\n') printf("\\n");
        else if (*p == '\r') printf("\\r");
        else if (*p == '\t') printf("\\t");
        else if (*p < 0x20 || *p == 0x7f) printf("\\x%02x", *p);
        else putchar((int)*p);
    }
}

static void dump_top_k_logits(tokenizer_t *tok, const float *logits, int n_vocab, int k)
{
    if (k <= 0) return;
    if (k > 32) k = 32;
    int ids[32];
    float scores[32];
    for (int i = 0; i < k; ++i) {
        ids[i] = -1;
        scores[i] = -INFINITY;
    }
    for (int i = 0; i < n_vocab; ++i) {
        float v = logits[i];
        int pos = -1;
        for (int j = 0; j < k; ++j) {
            if (v > scores[j]) {
                pos = j;
                break;
            }
        }
        if (pos < 0) continue;
        for (int j = k - 1; j > pos; --j) {
            scores[j] = scores[j - 1];
            ids[j] = ids[j - 1];
        }
        scores[pos] = v;
        ids[pos] = i;
    }
    printf("first_token_top%d_ids:", k);
    for (int i = 0; i < k; ++i) printf(" %d", ids[i]);
    printf("\n");
    printf("first_token_top%d_scores:", k);
    for (int i = 0; i < k; ++i) printf(" %.6f", scores[i]);
    printf("\n");
    for (int i = 0; i < k; ++i) {
        char *txt = tokenizer_decode_ids_ex(tok, &ids[i], ids[i] >= 0 ? 1 : 0, 0,
                                            NULL, NULL, NULL);
        printf("first_token_top%d_rank_%02d: id=%d score=%.6f text=", k, i + 1,
               ids[i], scores[i]);
        print_escaped_text(txt);
        printf("\n");
        free(txt);
    }
}

static uint64_t float_bits_hash(const float *x, int n)
{
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; ++i) {
        uint32_t bits = 0;
        memcpy(&bits, &x[i], sizeof(bits));
        for (int b = 0; b < 4; ++b) {
            h ^= (uint8_t)((bits >> (8 * b)) & 0xffu);
            h *= 1099511628211ull;
        }
    }
    return h;
}

static void dump_float_stats(const char *name, const float *x, int n)
{
    if (n <= 0) {
        printf("%s_stats: len=0\n", name);
        return;
    }
    float minv = x[0], maxv = x[0], absmax = fabsf(x[0]);
    double sum = 0.0, sum_abs = 0.0;
    for (int i = 0; i < n; ++i) {
        float v = x[i];
        float av = fabsf(v);
        if (v < minv) minv = v;
        if (v > maxv) maxv = v;
        if (av > absmax) absmax = av;
        sum += (double)v;
        sum_abs += (double)av;
    }
    printf("%s_stats: len=%d min=%.9g max=%.9g mean=%.9g absmax=%.9g mean_abs=%.9g checksum=%.9g hash=%016" PRIx64 "\n",
           name, n, minv, maxv, sum / (double)n, absmax, sum_abs / (double)n,
           sum, float_bits_hash(x, n));
}

static void dump_layer_vector_stats(gemv_backend_t *be, int layer, const char *point,
                                    const float *x, int n)
{
    if (!be->dump_layer_stats || n <= 0) return;
    float minv = x[0], maxv = x[0], absmax = fabsf(x[0]);
    double sum = 0.0, sum_abs = 0.0;
    for (int i = 0; i < n; ++i) {
        float v = x[i];
        float av = fabsf(v);
        if (v < minv) minv = v;
        if (v > maxv) maxv = v;
        if (av > absmax) absmax = av;
        sum += (double)v;
        sum_abs += (double)av;
    }
    printf("layer_vector_stats: backend=%s layer=%d point=%s len=%d min=%.9g max=%.9g mean=%.9g absmax=%.9g mean_abs=%.9g checksum=%.9g hash=%016" PRIx64 "\n",
           be->kind == BACKEND_FPGA ? "fpga" : "cpu", layer, point, n,
           minv, maxv, sum / (double)n, absmax, sum_abs / (double)n,
           sum, float_bits_hash(x, n));
}

static char *build_chat_prompt(tokenizer_t *tok, const char *user, int *used_gguf_template)
{
    if (used_gguf_template) *used_gguf_template = 0;
    if (!tok->chat_template ||
        !strstr(tok->chat_template, "<|im_start|>") ||
        !strstr(tok->chat_template, "add_generation_prompt")) {
        return NULL;
    }
    if (used_gguf_template) *used_gguf_template = 1;
    const char *prefix =
        "<|im_start|>system\n"
        "You are a helpful AI assistant named SmolLM, trained by Hugging Face<|im_end|>\n"
        "<|im_start|>user\n";
    const char *mid = "<|im_end|>\n<|im_start|>assistant\n";
    size_t n = strlen(prefix) + strlen(user) + strlen(mid) + 1u;
    char *s = (char *)malloc(n);
    if (!s) return NULL;
    snprintf(s, n, "%s%s%s", prefix, user, mid);
    return s;
}

static void print_i32_line(const char *label, const int32_t *v, int n)
{
    printf("%s:", label);
    for (int i = 0; i < n; ++i) printf(" %" PRId32, v[i]);
    printf("\n");
}

static void print_f32_line(const char *label, const float *v, int n)
{
    printf("%s:", label);
    for (int i = 0; i < n; ++i) printf(" %.9g", v[i]);
    printf("\n");
}

static void print_i16_stats(const char *label, const int16_t *v, int n)
{
    int16_t minv = v[0], maxv = v[0];
    int absmax = abs((int)v[0]);
    int64_t sum = 0, sum_abs = 0;
    for (int i = 0; i < n; ++i) {
        int vi = (int)v[i];
        int av = abs(vi);
        if (v[i] < minv) minv = v[i];
        if (v[i] > maxv) maxv = v[i];
        if (av > absmax) absmax = av;
        sum += vi;
        sum_abs += av;
    }
    printf("%s_i16_stats: len=%d min=%d max=%d mean=%.9g absmax=%d mean_abs=%.9g checksum=%" PRId64 "\n",
           label, n, (int)minv, (int)maxv, (double)sum / (double)n, absmax,
           (double)sum_abs / (double)n, sum);
}

static int64_t join_i64_words(uint32_t hi, uint32_t lo)
{
    uint64_t u = ((uint64_t)hi << 32) | (uint64_t)lo;
    return (int64_t)u;
}

typedef struct {
    int row_group;
    int lane;
    int row;
    int fixed_i32;
} affected_row_t;

static const affected_row_t k_qproj_affected_rows[] = {
    {0, 9, 9, 169},
    {1, 6, 22, -4},
    {1, 13, 29, -8},
    {1, 15, 31, 151},
    {3, 0, 48, -3},
    {3, 11, 59, -45},
    {4, 1, 65, -79},
    {5, 2, 82, 154},
    {5, 13, 93, 2},
    {5, 14, 94, 123},
    {6, 3, 99, -15},
    {6, 6, 102, -115},
    {6, 7, 103, -95},
    {6, 10, 106, -255},
    {7, 7, 119, 191},
    {7, 10, 122, -136},
    {8, 3, 131, 71},
    {8, 15, 143, 36},
    {9, 0, 144, 50},
    {10, 11, 171, 177},
    {10, 15, 175, 126},
    {11, 0, 176, 50},
    {11, 4, 180, -70},
    {11, 9, 185, 42},
    {12, 3, 195, -238},
    {13, 6, 214, -294},
    {14, 6, 230, 75},
    {14, 8, 232, 92},
    {15, 15, 255, -153},
    {17, 4, 276, 243},
    {17, 10, 282, 301},
    {18, 9, 297, -22},
    {18, 12, 300, -87},
    {20, 12, 332, -73},
    {21, 1, 337, 163},
    {21, 8, 344, -97},
    {22, 4, 356, 43},
    {22, 11, 363, 261},
    {23, 13, 381, -240},
    {24, 1, 385, -66},
    {25, 2, 402, 94},
    {25, 11, 411, -348},
    {25, 13, 413, 35},
    {26, 11, 427, 578},
    {27, 15, 447, -14},
    {28, 9, 457, -279},
    {30, 0, 480, 347},
    {31, 6, 502, 37},
    {31, 14, 510, -37},
    {32, 3, 515, -205},
    {33, 1, 529, 149},
    {33, 12, 540, -226},
    {34, 8, 552, 43},
    {35, 7, 567, -149},
};

static size_t qproj_affected_row_count(void)
{
    return sizeof(k_qproj_affected_rows) / sizeof(k_qproj_affected_rows[0]);
}

static int qproj_is_affected_lane(int row_group, int lane)
{
    int row = row_group * LANES + lane;
    for (size_t i = 0; i < qproj_affected_row_count(); ++i) {
        const affected_row_t *a = &k_qproj_affected_rows[i];
        if (a->row_group == row_group && a->lane == lane && a->row == row) return 1;
    }
    return 0;
}

static int run_packet_equivalence_check(model_t *m, const options_t *opt)
{
    int rc = -1;
    int used_gguf_template = 0;
    char *prompt_text = NULL;
    int_vec_t ids = {0};
    uint8_t *mixed_packet = NULL;
    uint8_t *duplicate_packet = NULL;
    FILE *csv = NULL;

    if (!m->q_proj[0]) {
        fprintf(stderr, "packet equivalence failed: missing layer0 q_proj tensor\n");
        return -1;
    }
    tensor_t *t = m->q_proj[0];
    int n = m->n_embd;
    if ((n % Q8_BLOCK) || t->cols < n) {
        fprintf(stderr, "packet equivalence failed: unsupported q_proj cols=%d n_embd=%d\n",
                t->cols, n);
        return -1;
    }

    if (opt->use_chat_template) {
        prompt_text = build_chat_prompt(&m->tokenizer, opt->prompt, &used_gguf_template);
    } else {
        prompt_text = strdup(opt->prompt);
    }
    if (!prompt_text || tokenizer_encode(&m->tokenizer, prompt_text, &ids)) {
        fprintf(stderr, "packet equivalence tokenizer failed\n");
        goto done;
    }

    size_t max_packet_bytes = (size_t)(n / Q8_BLOCK) * packet_block_bytes();
    mixed_packet = (uint8_t *)malloc(max_packet_bytes);
    duplicate_packet = (uint8_t *)malloc(max_packet_bytes);
    if (!mixed_packet || !duplicate_packet) {
        fprintf(stderr, "packet equivalence allocation failed\n");
        goto done;
    }
    if (opt->packet_equivalence_csv) {
        csv = fopen(opt->packet_equivalence_csv, "w");
        if (!csv) {
            fprintf(stderr, "packet equivalence csv open failed: %s: %s\n",
                    opt->packet_equivalence_csv, strerror(errno));
            goto done;
        }
        fprintf(csv, "row_group,lane,row,block,col,kind,mixed_offset,duplicate_offset,mixed_value,duplicate_value,direct_value,match\n");
    }

    printf("packet_equivalence_check: layer=0 role=q_proj tensor=%s\n", t->name);
    printf("hardware_execution: no\n");
    printf("raw_prompt: %s\n", opt->prompt);
    printf("chat_template_applied: %s\n", opt->use_chat_template ? "true" : "false");
    printf("chat_template_source: %s\n",
           opt->use_chat_template ? (used_gguf_template ? "gguf_chatml" : "unsupported") : "none");
    printf("prompt_token_count: %d\n", ids.len);
    printf("prompt_token_ids:");
    for (int i = 0; i < ids.len; ++i) printf(" %d", ids.data[i]);
    printf("\n");
    printf("tensor_rows: %d\n", t->rows);
    printf("tensor_cols: %d\n", t->cols);
    printf("in_features: %d\n", n);
    printf("q8_blocks: %d\n", n / Q8_BLOCK);
    printf("packet_block_bytes: %zu\n", packet_block_bytes());
    printf("expected_packet_bytes: %zu\n", max_packet_bytes);
    printf("scale_header_order: beat0 lane0..3; beat1 lane4..7; beat2 lane8..11; beat3 lane12..15\n");
    printf("weight_byte_order: byte0=lane0 byte1=lane1 ... byte15=lane15 per column\n");
    printf("dma_btt_unit: bytes\n");
    printf("tkeep_policy: 0xffff full beat\n");
    printf("row_id_contract: row=row_group_base+lane\n");
    printf("affected_rows: %zu\n", qproj_affected_row_count());

    uint64_t scale_checks = 0;
    uint64_t weight_checks = 0;
    uint64_t mixed_duplicate_mismatches = 0;
    uint64_t direct_reference_mismatches = 0;
    uint64_t duplicate_uniform_mismatches = 0;
    int mismatch_samples = 0;
    int blocks = n / Q8_BLOCK;

    printf("packet_equivalence_row_header: row_group,lane,row,fixed_i32,scale_checks,weight_checks,mixed_duplicate_mismatches,direct_reference_mismatches,duplicate_uniform_mismatches\n");
    for (size_t ai = 0; ai < qproj_affected_row_count(); ++ai) {
        const affected_row_t *a = &k_qproj_affected_rows[ai];
        int row_group_base = a->row_group * LANES;
        if (a->row != row_group_base + a->lane || a->row >= t->rows) {
            fprintf(stderr, "packet equivalence affected row contract invalid: rg=%d lane=%d row=%d\n",
                    a->row_group, a->lane, a->row);
            goto done;
        }
        uint32_t mixed_bytes = 0;
        uint32_t duplicate_bytes = 0;
        int row_map[LANES];
        for (int lane = 0; lane < LANES; ++lane) row_map[lane] = a->row;
        if (build_packet_from_tensor(m, t, row_group_base, 0, n,
                                     mixed_packet, &mixed_bytes) ||
            build_packet_from_tensor_row_map(m, t, row_map, 0, n,
                                             duplicate_packet, &duplicate_bytes)) {
            fprintf(stderr, "packet equivalence packet build failed for row=%d\n", a->row);
            goto done;
        }
        if (mixed_bytes != duplicate_bytes || mixed_bytes != max_packet_bytes) {
            fprintf(stderr, "packet equivalence packet length mismatch row=%d mixed=%" PRIu32
                    " duplicate=%" PRIu32 " expected=%zu\n",
                    a->row, mixed_bytes, duplicate_bytes, max_packet_bytes);
            goto done;
        }

        uint64_t row_scale_checks = 0;
        uint64_t row_weight_checks = 0;
        uint64_t row_mixdup_mismatches = 0;
        uint64_t row_direct_mismatches = 0;
        uint64_t row_uniform_mismatches = 0;

        for (int block = 0; block < blocks; ++block) {
            size_t mo = packet_scale_offset(block, a->lane);
            size_t dob = packet_scale_offset(block, a->lane);
            int32_t mv = (int32_t)rd_le32(mixed_packet + mo);
            int32_t dv = (int32_t)rd_le32(duplicate_packet + dob);
            int32_t direct = tensor_scale_q_at(m, t, a->row, block);
            int match = (mv == dv && mv == direct);
            if (mv != dv) {
                mixed_duplicate_mismatches++;
                row_mixdup_mismatches++;
            }
            if (mv != direct || dv != direct) {
                direct_reference_mismatches++;
                row_direct_mismatches++;
            }
            for (int lane = 1; lane < LANES; ++lane) {
                int32_t lane_v = (int32_t)rd_le32(duplicate_packet + packet_scale_offset(block, lane));
                if (lane_v != (int32_t)rd_le32(duplicate_packet + packet_scale_offset(block, 0))) {
                    duplicate_uniform_mismatches++;
                    row_uniform_mismatches++;
                    break;
                }
            }
            scale_checks++;
            row_scale_checks++;
            if (csv) {
                fprintf(csv, "%d,%d,%d,%d,%d,scale_q,%" PRIu64 ",%" PRIu64
                        ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%d\n",
                        a->row_group, a->lane, a->row, block, -1,
                        (uint64_t)mo, (uint64_t)dob, mv, dv, direct, match);
            }
            if (!match && mismatch_samples < 16) {
                printf("packet_equivalence_mismatch: row_group=%d lane=%d row=%d block=%d kind=scale_q mixed=%" PRId32 " duplicate=%" PRId32 " direct=%" PRId32 "\n",
                       a->row_group, a->lane, a->row, block, mv, dv, direct);
                mismatch_samples++;
            }

            for (int col = 0; col < Q8_BLOCK; ++col) {
                mo = packet_weight_offset(block, col, a->lane);
                dob = packet_weight_offset(block, col, a->lane);
                int mv_w = (int)(int8_t)mixed_packet[mo];
                int dv_w = (int)(int8_t)duplicate_packet[dob];
                int direct_w = (int)tensor_weight_i8_at(m, t, a->row, block, col);
                match = (mv_w == dv_w && mv_w == direct_w);
                if (mv_w != dv_w) {
                    mixed_duplicate_mismatches++;
                    row_mixdup_mismatches++;
                }
                if (mv_w != direct_w || dv_w != direct_w) {
                    direct_reference_mismatches++;
                    row_direct_mismatches++;
                }
                for (int lane = 1; lane < LANES; ++lane) {
                    int lane_v = (int)(int8_t)duplicate_packet[packet_weight_offset(block, col, lane)];
                    int lane0_v = (int)(int8_t)duplicate_packet[packet_weight_offset(block, col, 0)];
                    if (lane_v != lane0_v) {
                        duplicate_uniform_mismatches++;
                        row_uniform_mismatches++;
                        break;
                    }
                }
                weight_checks++;
                row_weight_checks++;
                if (csv) {
                    fprintf(csv, "%d,%d,%d,%d,%d,weight_i8,%" PRIu64 ",%" PRIu64
                            ",%d,%d,%d,%d\n",
                            a->row_group, a->lane, a->row, block, col,
                            (uint64_t)mo, (uint64_t)dob, mv_w, dv_w, direct_w, match);
                }
                if (!match && mismatch_samples < 16) {
                    printf("packet_equivalence_mismatch: row_group=%d lane=%d row=%d block=%d col=%d kind=weight_i8 mixed=%d duplicate=%d direct=%d\n",
                           a->row_group, a->lane, a->row, block, col, mv_w, dv_w, direct_w);
                    mismatch_samples++;
                }
            }
        }
        printf("packet_equivalence_row: %d,%d,%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
               a->row_group, a->lane, a->row, a->fixed_i32, row_scale_checks,
               row_weight_checks, row_mixdup_mismatches, row_direct_mismatches,
               row_uniform_mismatches);
    }

    printf("packet_equivalence_scale_checks: %" PRIu64 "\n", scale_checks);
    printf("packet_equivalence_weight_checks: %" PRIu64 "\n", weight_checks);
    printf("packet_equivalence_mixed_duplicate_mismatches: %" PRIu64 "\n",
           mixed_duplicate_mismatches);
    printf("packet_equivalence_direct_reference_mismatches: %" PRIu64 "\n",
           direct_reference_mismatches);
    printf("packet_equivalence_duplicate_uniform_mismatches: %" PRIu64 "\n",
           duplicate_uniform_mismatches);
    if (opt->packet_equivalence_csv) {
        printf("packet_equivalence_csv: %s\n", opt->packet_equivalence_csv);
    }
    if (mixed_duplicate_mismatches || direct_reference_mismatches || duplicate_uniform_mismatches) {
        printf("packet_equivalence_result: FAIL\n");
        printf("classification: PACKET_LAYOUT_FAIL\n");
    } else {
        printf("packet_equivalence_result: PASS\n");
        printf("classification: PACKET_LAYOUT_PASS_NEXT_MODE1\n");
    }
    rc = 0;

done:
    if (csv) fclose(csv);
    free(prompt_text);
    free(ids.data);
    free(mixed_packet);
    free(duplicate_packet);
    return rc;
}

static int32_t q8_block_acc_ref_i32(model_t *m, tensor_t *t, const int16_t *input_i16,
                                    int row, int block)
{
    const uint8_t *rp = tensor_row_ptr(m, t, row) + block * Q8_BLOCK_BYTES;
    const int8_t *qs = (const int8_t *)(const void *)(rp + 2);
    int32_t acc = 0;
    for (int col = 0; col < Q8_BLOCK; ++col) {
        acc += (int32_t)input_i16[block * Q8_BLOCK + col] * (int32_t)qs[col];
    }
    return acc;
}

static int32_t q8_const_scale_ref_i32(model_t *m, tensor_t *t, const int16_t *input_i16,
                                      int row, int blocks, int32_t scale_q)
{
    int64_t row_acc = 0;
    for (int block = 0; block < blocks; ++block) {
        int32_t block_acc = q8_block_acc_ref_i32(m, t, input_i16, row, block);
        row_acc += round_shift_signed_i64((int64_t)block_acc * (int64_t)scale_q,
                                          SCALE_SHIFT);
    }
    return sat_i64_to_i32(row_acc);
}

static void packet_override_all_scale_q(uint8_t *packet, int blocks, int32_t scale_q)
{
    for (int block = 0; block < blocks; ++block) {
        for (int lane = 0; lane < LANES; ++lane) {
            put_le32(packet + packet_scale_offset(block, lane), scale_q);
        }
    }
}

static int run_mode1_blockacc_compare(model_t *m, const options_t *opt)
{
    int rc = -1;
    int used_gguf_template = 0;
    char *prompt_text = NULL;
    int_vec_t ids = {0};
    float *x = NULL;
    float *xb = NULL;
    int16_t *input_i16 = NULL;
    uint8_t *packet = NULL;
    int32_t *mixed_out = NULL;
    int32_t *duplicate_out = NULL;
    FILE *csv = NULL;
    gemv_hw_t hw;
    memset(&hw, 0, sizeof(hw));
    hw.dma_fd = hw.bram_fd = hw.gemv_fd = hw.mem_fd = -1;

    if (!m->q_proj[0]) {
        fprintf(stderr, "mode1 compare failed: missing layer0 q_proj tensor\n");
        return -1;
    }
    tensor_t *t = m->q_proj[0];
    int n = m->n_embd;
    if ((n % Q8_BLOCK) || t->cols < n) {
        fprintf(stderr, "mode1 compare failed: unsupported q_proj cols=%d n_embd=%d\n",
                t->cols, n);
        return -1;
    }
    int blocks = n / Q8_BLOCK;
    uint32_t result_words = (uint32_t)(blocks * LANES);

    if (opt->use_chat_template) {
        prompt_text = build_chat_prompt(&m->tokenizer, opt->prompt, &used_gguf_template);
    } else {
        prompt_text = strdup(opt->prompt);
    }
    if (!prompt_text || tokenizer_encode(&m->tokenizer, prompt_text, &ids) || ids.len <= 0) {
        fprintf(stderr, "mode1 compare tokenizer failed\n");
        goto done;
    }

    x = (float *)malloc((size_t)n * sizeof(float));
    xb = (float *)malloc((size_t)n * sizeof(float));
    input_i16 = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    packet = (uint8_t *)malloc((size_t)blocks * packet_block_bytes());
    mixed_out = (int32_t *)malloc((size_t)result_words * sizeof(int32_t));
    duplicate_out = (int32_t *)malloc((size_t)result_words * sizeof(int32_t));
    if (!x || !xb || !input_i16 || !packet || !mixed_out || !duplicate_out) {
        fprintf(stderr, "mode1 compare allocation failed\n");
        goto done;
    }
    if (opt->mode1_blockacc_csv) {
        csv = fopen(opt->mode1_blockacc_csv, "w");
        if (!csv) {
            fprintf(stderr, "mode1 compare csv open failed: %s: %s\n",
                    opt->mode1_blockacc_csv, strerror(errno));
            goto done;
        }
        fprintf(csv, "row_group,lane,row,block,cpu_block_acc_i32,mixed_lane_i32,duplicate_same_lane_i32,duplicate_lane0_i32,mixed_vs_cpu,duplicate_same_lane_vs_cpu,mixed_vs_duplicate,duplicate_good_lanes\n");
    }

    int token = ids.data[0];
    dequantize_row_q8(m, m->tok_embd, token, x);
    rms_norm(x, tensor_f32_ptr(m, m->attn_norm[0]), n, m->rms_eps, xb);
    uint64_t saturations = 0;
    quantize_input_chunk(xb, 0, n, opt->act_shift, input_i16, &saturations);

    printf("mode1_blockacc_compare: layer=0 role=q_proj tensor=%s\n", t->name);
    printf("hardware_execution: yes\n");
    printf("raw_prompt: %s\n", opt->prompt);
    printf("chat_template_applied: %s\n", opt->use_chat_template ? "true" : "false");
    printf("chat_template_source: %s\n",
           opt->use_chat_template ? (used_gguf_template ? "gguf_chatml" : "unsupported") : "none");
    printf("prompt_token_count: %d\n", ids.len);
    printf("prompt_token_ids:");
    for (int i = 0; i < ids.len; ++i) printf(" %d", ids.data[i]);
    printf("\n");
    printf("probe_token: %d\n", token);
    printf("probe_act_shift: %d\n", opt->act_shift);
    printf("probe_input_saturations: %" PRIu64 "\n", saturations);
    printf("tensor_rows: %d\n", t->rows);
    printf("tensor_cols: %d\n", t->cols);
    printf("in_features: %d\n", n);
    printf("q8_blocks: %d\n", blocks);
    printf("mode1_result_words: %" PRIu32 "\n", result_words);
    printf("mode1_result_bytes: %" PRIu32 "\n", result_words * (uint32_t)sizeof(int32_t));
    printf("affected_rows: %zu\n", qproj_affected_row_count());
    print_i16_stats("mode1_quantized_input", input_i16, n);

    if (gemv_hw_open(&hw, DEFAULT_PHYS_BASE, DEFAULT_PHYS_SIZE) ||
        gemv_hw_reset_dma(&hw)) {
        fprintf(stderr, "mode1 compare FPGA open/reset failed\n");
        goto done;
    }

    uint64_t total_blocks = 0;
    uint64_t mixed_cpu_mismatches = 0;
    uint64_t duplicate_cpu_mismatches = 0;
    uint64_t mixed_duplicate_mismatches = 0;
    uint64_t duplicate_uniform_mismatches = 0;
    int mismatch_samples = 0;

    printf("mode1_blockacc_row_header: row_group,lane,row,fixed_i32,blocks,mixed_cpu_mismatches,duplicate_cpu_mismatches,mixed_duplicate_mismatches,duplicate_uniform_mismatches\n");
    for (size_t ai = 0; ai < qproj_affected_row_count(); ++ai) {
        const affected_row_t *a = &k_qproj_affected_rows[ai];
        int row_group_base = a->row_group * LANES;
        if (a->row != row_group_base + a->lane || a->row >= t->rows) {
            fprintf(stderr, "mode1 compare affected row contract invalid: rg=%d lane=%d row=%d\n",
                    a->row_group, a->lane, a->row);
            goto done;
        }

        uint32_t packet_bytes = 0;
        if (build_packet_from_tensor(m, t, row_group_base, 0, n, packet, &packet_bytes)) {
            fprintf(stderr, "mode1 compare mixed packet build failed row=%d\n", a->row);
            goto done;
        }
        gemv_hw_load_input(&hw, input_i16, n);
        if (gemv_hw_run_packet_mode(&hw, packet, packet_bytes, (uint32_t)n, 1u,
                                    result_words, mixed_out)) {
            fprintf(stderr, "mode1 compare mixed FPGA run failed row=%d\n", a->row);
            goto done;
        }

        int row_map[LANES];
        for (int lane = 0; lane < LANES; ++lane) row_map[lane] = a->row;
        if (build_packet_from_tensor_row_map(m, t, row_map, 0, n, packet, &packet_bytes)) {
            fprintf(stderr, "mode1 compare duplicate packet build failed row=%d\n", a->row);
            goto done;
        }
        gemv_hw_load_input(&hw, input_i16, n);
        if (gemv_hw_run_packet_mode(&hw, packet, packet_bytes, (uint32_t)n, 1u,
                                    result_words, duplicate_out)) {
            fprintf(stderr, "mode1 compare duplicate FPGA run failed row=%d\n", a->row);
            goto done;
        }

        uint64_t row_mixed_cpu_mismatches = 0;
        uint64_t row_duplicate_cpu_mismatches = 0;
        uint64_t row_mixed_duplicate_mismatches = 0;
        uint64_t row_duplicate_uniform_mismatches = 0;
        for (int block = 0; block < blocks; ++block) {
            int idx = block * LANES + a->lane;
            int32_t cpu = q8_block_acc_ref_i32(m, t, input_i16, a->row, block);
            int32_t mixed = mixed_out[idx];
            int32_t duplicate = duplicate_out[idx];
            int32_t duplicate_lane0 = duplicate_out[block * LANES + 0];
            int duplicate_good_lanes = 0;
            for (int lane = 0; lane < LANES; ++lane) {
                if (duplicate_out[block * LANES + lane] == cpu) duplicate_good_lanes++;
            }
            int mixed_ok = (mixed == cpu);
            int duplicate_ok = (duplicate == cpu);
            int mixdup_ok = (mixed == duplicate);
            if (!mixed_ok) {
                mixed_cpu_mismatches++;
                row_mixed_cpu_mismatches++;
            }
            if (!duplicate_ok) {
                duplicate_cpu_mismatches++;
                row_duplicate_cpu_mismatches++;
            }
            if (!mixdup_ok) {
                mixed_duplicate_mismatches++;
                row_mixed_duplicate_mismatches++;
            }
            if (duplicate_good_lanes != LANES) {
                duplicate_uniform_mismatches++;
                row_duplicate_uniform_mismatches++;
            }
            total_blocks++;
            if (csv) {
                fprintf(csv, "%d,%d,%d,%d,%" PRId32 ",%" PRId32 ",%" PRId32
                        ",%" PRId32 ",%d,%d,%d,%d\n",
                        a->row_group, a->lane, a->row, block, cpu, mixed,
                        duplicate, duplicate_lane0, mixed_ok, duplicate_ok,
                        mixdup_ok, duplicate_good_lanes);
            }
            if ((!mixed_ok || !duplicate_ok || !mixdup_ok || duplicate_good_lanes != LANES) &&
                mismatch_samples < 32) {
                printf("mode1_blockacc_mismatch: row_group=%d lane=%d row=%d block=%d cpu=%" PRId32 " mixed=%" PRId32 " duplicate_same_lane=%" PRId32 " duplicate_lane0=%" PRId32 " duplicate_good_lanes=%d\n",
                       a->row_group, a->lane, a->row, block, cpu, mixed,
                       duplicate, duplicate_lane0, duplicate_good_lanes);
                mismatch_samples++;
            }
        }
        printf("mode1_blockacc_row: %d,%d,%d,%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
               a->row_group, a->lane, a->row, a->fixed_i32, blocks,
               row_mixed_cpu_mismatches, row_duplicate_cpu_mismatches,
               row_mixed_duplicate_mismatches, row_duplicate_uniform_mismatches);
    }

    printf("mode1_blockacc_total_blocks: %" PRIu64 "\n", total_blocks);
    printf("mode1_blockacc_mixed_cpu_mismatches: %" PRIu64 "\n", mixed_cpu_mismatches);
    printf("mode1_blockacc_duplicate_cpu_mismatches: %" PRIu64 "\n", duplicate_cpu_mismatches);
    printf("mode1_blockacc_mixed_duplicate_mismatches: %" PRIu64 "\n",
           mixed_duplicate_mismatches);
    printf("mode1_blockacc_duplicate_uniform_mismatches: %" PRIu64 "\n",
           duplicate_uniform_mismatches);
    if (opt->mode1_blockacc_csv) {
        printf("mode1_blockacc_csv: %s\n", opt->mode1_blockacc_csv);
    }
    if (mixed_cpu_mismatches == 0 && duplicate_cpu_mismatches == 0 &&
        mixed_duplicate_mismatches == 0 && duplicate_uniform_mismatches == 0) {
        printf("mode1_blockacc_result: PASS\n");
        printf("classification: MODE1_PASS_NEXT_MODE0_SCALE_PATH\n");
    } else if (mixed_cpu_mismatches != 0 && duplicate_cpu_mismatches == 0) {
        printf("mode1_blockacc_result: FAIL\n");
        printf("classification: MODE1_MIXED_ROW_FAIL\n");
    } else {
        printf("mode1_blockacc_result: FAIL\n");
        printf("classification: MODE1_COMMON_OR_DUPLICATE_FAIL\n");
    }
    rc = 0;

done:
    if (hw.opened) gemv_hw_close(&hw);
    if (csv) fclose(csv);
    free(prompt_text);
    free(ids.data);
    free(x);
    free(xb);
    free(input_i16);
    free(packet);
    free(mixed_out);
    free(duplicate_out);
    return rc;
}

static int run_mode1_scaled_qproj_compare(model_t *m, const options_t *opt)
{
    int rc = -1;
    int used_gguf_template = 0;
    char *prompt_text = NULL;
    int_vec_t ids = {0};
    float *x = NULL;
    float *xb = NULL;
    float *cpu_out = NULL;
    float *fpga_out = NULL;
    FILE *csv = NULL;
    gemv_backend_t be;
    memset(&be, 0, sizeof(be));

    if (!m->q_proj[0]) {
        fprintf(stderr, "mode1 scaled qproj compare failed: missing layer0 q_proj tensor\n");
        return -1;
    }
    tensor_t *t = m->q_proj[0];
    int n = m->n_embd;
    if (t->cols != n || t->rows != n) {
        fprintf(stderr, "mode1 scaled qproj compare failed: unsupported q_proj shape rows=%d cols=%d n_embd=%d\n",
                t->rows, t->cols, n);
        return -1;
    }

    if (opt->use_chat_template) {
        prompt_text = build_chat_prompt(&m->tokenizer, opt->prompt, &used_gguf_template);
    } else {
        prompt_text = strdup(opt->prompt);
    }
    if (!prompt_text || tokenizer_encode(&m->tokenizer, prompt_text, &ids) || ids.len <= 0) {
        fprintf(stderr, "mode1 scaled qproj compare tokenizer failed\n");
        goto done;
    }

    x = (float *)malloc((size_t)n * sizeof(float));
    xb = (float *)malloc((size_t)n * sizeof(float));
    cpu_out = (float *)malloc((size_t)t->rows * sizeof(float));
    fpga_out = (float *)malloc((size_t)t->rows * sizeof(float));
    if (!x || !xb || !cpu_out || !fpga_out) {
        fprintf(stderr, "mode1 scaled qproj compare allocation failed\n");
        goto done;
    }
    if (opt->mode1_scaled_qproj_csv) {
        csv = fopen(opt->mode1_scaled_qproj_csv, "w");
        if (!csv) {
            fprintf(stderr, "mode1 scaled qproj csv open failed: %s: %s\n",
                    opt->mode1_scaled_qproj_csv, strerror(errno));
            goto done;
        }
        fprintf(csv, "row,cpu_float,fpga_mode1_cpu_scale_float,diff,abs_diff,rel_diff,large_diff_gt_1,large_diff_gt_16,catastrophic_gt_1e6\n");
    }

    int token = ids.data[0];
    dequantize_row_q8(m, m->tok_embd, token, x);
    rms_norm(x, tensor_f32_ptr(m, m->attn_norm[0]), n, m->rms_eps, xb);
    gemv_cpu_q8(m, t, xb, cpu_out);

    if (backend_open(&be, m, BACKEND_FPGA, 1, opt->act_shift, 0,
                     opt->fpga_repair_mode, FPGA_OUTPUT_MODE1_CPU_SCALE, NULL)) {
        fprintf(stderr, "mode1 scaled qproj compare backend open failed\n");
        goto done;
    }
    if (gemv_backend_run(&be, t, "q_proj", 0, xb, fpga_out)) {
        fprintf(stderr, "mode1 scaled qproj compare FPGA q_proj failed\n");
        goto done;
    }

    double sum_abs = 0.0;
    double sum_sq = 0.0;
    double max_abs = 0.0;
    int max_idx = -1;
    uint64_t gt_1 = 0;
    uint64_t gt_16 = 0;
    uint64_t catastrophic = 0;
    for (int r = 0; r < t->rows; ++r) {
        double diff = (double)fpga_out[r] - (double)cpu_out[r];
        double ad = fabs(diff);
        double denom = fabs((double)cpu_out[r]);
        double rel = denom > 1.0e-12 ? ad / denom : ad;
        if (ad > max_abs) {
            max_abs = ad;
            max_idx = r;
        }
        if (ad > 1.0) gt_1++;
        if (ad > 16.0) gt_16++;
        if (ad > 1000000.0 || fabs((double)fpga_out[r]) >= 1000000.0) catastrophic++;
        sum_abs += ad;
        sum_sq += diff * diff;
        if (csv) {
            fprintf(csv, "%d,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d,%d\n",
                    r, cpu_out[r], fpga_out[r], diff, ad, rel,
                    ad > 1.0, ad > 16.0,
                    (ad > 1000000.0 || fabs((double)fpga_out[r]) >= 1000000.0));
        }
    }
    double mean_abs = t->rows ? sum_abs / (double)t->rows : 0.0;
    double rmse = t->rows ? sqrt(sum_sq / (double)t->rows) : 0.0;

    printf("mode1_scaled_qproj_compare: layer=0 role=q_proj tensor=%s\n", t->name);
    printf("hardware_execution: yes\n");
    printf("fpga_output_mode: mode1_cpu_scale\n");
    printf("raw_prompt: %s\n", opt->prompt);
    printf("chat_template_applied: %s\n", opt->use_chat_template ? "true" : "false");
    printf("chat_template_source: %s\n",
           opt->use_chat_template ? (used_gguf_template ? "gguf_chatml" : "unsupported") : "none");
    printf("prompt_token_count: %d\n", ids.len);
    printf("prompt_token_ids:");
    for (int i = 0; i < ids.len; ++i) printf(" %d", ids.data[i]);
    printf("\n");
    printf("probe_token: %d\n", token);
    printf("probe_act_shift: %d\n", opt->act_shift);
    printf("tensor_rows: %d\n", t->rows);
    printf("tensor_cols: %d\n", t->cols);
    dump_float_stats("mode1_scaled_qproj_cpu_float", cpu_out, t->rows);
    dump_float_stats("mode1_scaled_qproj_fpga_float", fpga_out, t->rows);
    printf("mode1_scaled_qproj_mean_abs_diff: %.9g\n", mean_abs);
    printf("mode1_scaled_qproj_rmse: %.9g\n", rmse);
    printf("mode1_scaled_qproj_max_abs_diff: %.9g\n", max_abs);
    printf("mode1_scaled_qproj_max_abs_diff_row: %d\n", max_idx);
    printf("mode1_scaled_qproj_gt_1: %" PRIu64 "\n", gt_1);
    printf("mode1_scaled_qproj_gt_16: %" PRIu64 "\n", gt_16);
    printf("mode1_scaled_qproj_catastrophic_gt_1e6: %" PRIu64 "\n", catastrophic);
    printf("total_gemv_calls: %" PRIu64 "\n", be.counters.total_gemv_calls);
    printf("fpga_gemv_calls: %" PRIu64 "\n", be.counters.fpga_gemv_calls);
    printf("cpu_gemv_fallbacks: %" PRIu64 "\n", be.counters.cpu_gemv_fallbacks);
    printf("fpga_repair_jobs: %" PRIu64 "\n", be.counters.fpga_repair_jobs);
    printf("fpga_mode1_blockacc_calls: %" PRIu64 "\n",
           be.counters.fpga_mode1_blockacc_calls);
    printf("cpu_scale_accum_ops: %" PRIu64 "\n", be.counters.cpu_scale_accum_ops);
    printf("cpu_scale_accum_time_ms: %.3f\n",
           (double)be.counters.cpu_scale_accum_ns / 1000000.0);
    printf("s2mm_output_bytes: %" PRIu64 "\n", be.counters.s2mm_output_bytes);
    if (opt->mode1_scaled_qproj_csv) {
        printf("mode1_scaled_qproj_csv: %s\n", opt->mode1_scaled_qproj_csv);
    }
    if (catastrophic == 0) {
        printf("mode1_scaled_qproj_result: PASS\n");
        printf("classification: MODE1_CPU_SCALE_QPROJ_NO_CATASTROPHIC_OUTPUT\n");
    } else {
        printf("mode1_scaled_qproj_result: FAIL\n");
        printf("classification: MODE1_CPU_SCALE_QPROJ_CATASTROPHIC_OUTPUT\n");
    }
    rc = 0;

done:
    if (be.hw.opened || be.audit) backend_close(&be);
    if (csv) fclose(csv);
    free(prompt_text);
    free(ids.data);
    free(x);
    free(xb);
    free(cpu_out);
    free(fpga_out);
    return rc;
}

static int run_identity_scale_compare(model_t *m, const options_t *opt)
{
    int rc = -1;
    int used_gguf_template = 0;
    char *prompt_text = NULL;
    int_vec_t ids = {0};
    float *x = NULL;
    float *xb = NULL;
    int16_t *input_i16 = NULL;
    uint8_t *packet = NULL;
    FILE *csv = NULL;
    gemv_hw_t hw;
    memset(&hw, 0, sizeof(hw));
    hw.dma_fd = hw.bram_fd = hw.gemv_fd = hw.mem_fd = -1;

    if (!m->q_proj[0]) {
        fprintf(stderr, "identity-scale compare failed: missing layer0 q_proj tensor\n");
        return -1;
    }
    tensor_t *t = m->q_proj[0];
    int n = m->n_embd;
    if ((n % Q8_BLOCK) || t->cols < n) {
        fprintf(stderr, "identity-scale compare failed: unsupported q_proj cols=%d n_embd=%d\n",
                t->cols, n);
        return -1;
    }
    int blocks = n / Q8_BLOCK;
    int32_t identity_scale_q = (int32_t)(1u << opt->act_shift);

    if (opt->use_chat_template) {
        prompt_text = build_chat_prompt(&m->tokenizer, opt->prompt, &used_gguf_template);
    } else {
        prompt_text = strdup(opt->prompt);
    }
    if (!prompt_text || tokenizer_encode(&m->tokenizer, prompt_text, &ids) || ids.len <= 0) {
        fprintf(stderr, "identity-scale compare tokenizer failed\n");
        goto done;
    }

    x = (float *)malloc((size_t)n * sizeof(float));
    xb = (float *)malloc((size_t)n * sizeof(float));
    input_i16 = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    packet = (uint8_t *)malloc((size_t)blocks * packet_block_bytes());
    if (!x || !xb || !input_i16 || !packet) {
        fprintf(stderr, "identity-scale compare allocation failed\n");
        goto done;
    }
    if (opt->identity_scale_csv) {
        csv = fopen(opt->identity_scale_csv, "w");
        if (!csv) {
            fprintf(stderr, "identity-scale csv open failed: %s: %s\n",
                    opt->identity_scale_csv, strerror(errno));
            goto done;
        }
        fprintf(csv, "row_group,lane,row,cpu_identity_i32,mixed_i32,duplicate_same_lane_i32,duplicate_lane0_i32,mixed_vs_cpu,duplicate_same_lane_vs_cpu,mixed_vs_duplicate,duplicate_good_lanes\n");
    }

    int token = ids.data[0];
    dequantize_row_q8(m, m->tok_embd, token, x);
    rms_norm(x, tensor_f32_ptr(m, m->attn_norm[0]), n, m->rms_eps, xb);
    uint64_t saturations = 0;
    quantize_input_chunk(xb, 0, n, opt->act_shift, input_i16, &saturations);

    printf("identity_scale_compare: layer=0 role=q_proj tensor=%s\n", t->name);
    printf("hardware_execution: yes\n");
    printf("raw_prompt: %s\n", opt->prompt);
    printf("chat_template_applied: %s\n", opt->use_chat_template ? "true" : "false");
    printf("chat_template_source: %s\n",
           opt->use_chat_template ? (used_gguf_template ? "gguf_chatml" : "unsupported") : "none");
    printf("prompt_token_count: %d\n", ids.len);
    printf("prompt_token_ids:");
    for (int i = 0; i < ids.len; ++i) printf(" %d", ids.data[i]);
    printf("\n");
    printf("probe_token: %d\n", token);
    printf("probe_act_shift: %d\n", opt->act_shift);
    printf("identity_scale_q: %" PRId32 "\n", identity_scale_q);
    printf("probe_input_saturations: %" PRIu64 "\n", saturations);
    printf("tensor_rows: %d\n", t->rows);
    printf("tensor_cols: %d\n", t->cols);
    printf("in_features: %d\n", n);
    printf("q8_blocks: %d\n", blocks);
    printf("affected_rows: %zu\n", qproj_affected_row_count());
    print_i16_stats("identity_scale_quantized_input", input_i16, n);

    if (gemv_hw_open(&hw, DEFAULT_PHYS_BASE, DEFAULT_PHYS_SIZE) ||
        gemv_hw_reset_dma(&hw)) {
        fprintf(stderr, "identity-scale FPGA open/reset failed\n");
        goto done;
    }

    uint64_t mixed_cpu_mismatches = 0;
    uint64_t duplicate_cpu_mismatches = 0;
    uint64_t mixed_duplicate_mismatches = 0;
    uint64_t duplicate_uniform_mismatches = 0;
    int mismatch_samples = 0;

    printf("identity_scale_row_header: row_group,lane,row,cpu_identity_i32,mixed_i32,duplicate_same_lane_i32,duplicate_lane0_i32,mixed_vs_cpu,duplicate_same_lane_vs_cpu,mixed_vs_duplicate,duplicate_good_lanes\n");
    for (size_t ai = 0; ai < qproj_affected_row_count(); ++ai) {
        const affected_row_t *a = &k_qproj_affected_rows[ai];
        int row_group_base = a->row_group * LANES;
        if (a->row != row_group_base + a->lane || a->row >= t->rows) {
            fprintf(stderr, "identity-scale affected row contract invalid: rg=%d lane=%d row=%d\n",
                    a->row_group, a->lane, a->row);
            goto done;
        }

        uint32_t packet_bytes = 0;
        int32_t mixed_out[LANES];
        int32_t duplicate_out[LANES];
        if (build_packet_from_tensor(m, t, row_group_base, 0, n, packet, &packet_bytes)) {
            fprintf(stderr, "identity-scale mixed packet build failed row=%d\n", a->row);
            goto done;
        }
        packet_override_all_scale_q(packet, blocks, identity_scale_q);
        gemv_hw_load_input(&hw, input_i16, n);
        if (gemv_hw_run_packet(&hw, packet, packet_bytes, (uint32_t)n, mixed_out)) {
            fprintf(stderr, "identity-scale mixed FPGA run failed row=%d\n", a->row);
            goto done;
        }

        int row_map[LANES];
        for (int lane = 0; lane < LANES; ++lane) row_map[lane] = a->row;
        if (build_packet_from_tensor_row_map(m, t, row_map, 0, n, packet, &packet_bytes)) {
            fprintf(stderr, "identity-scale duplicate packet build failed row=%d\n", a->row);
            goto done;
        }
        packet_override_all_scale_q(packet, blocks, identity_scale_q);
        gemv_hw_load_input(&hw, input_i16, n);
        if (gemv_hw_run_packet(&hw, packet, packet_bytes, (uint32_t)n, duplicate_out)) {
            fprintf(stderr, "identity-scale duplicate FPGA run failed row=%d\n", a->row);
            goto done;
        }

        int32_t cpu = q8_const_scale_ref_i32(m, t, input_i16, a->row, blocks,
                                             identity_scale_q);
        int32_t mixed = mixed_out[a->lane];
        int32_t duplicate = duplicate_out[a->lane];
        int32_t duplicate_lane0 = duplicate_out[0];
        int duplicate_good_lanes = 0;
        for (int lane = 0; lane < LANES; ++lane) {
            if (duplicate_out[lane] == cpu) duplicate_good_lanes++;
        }
        int mixed_ok = (mixed == cpu);
        int duplicate_ok = (duplicate == cpu);
        int mixdup_ok = (mixed == duplicate);
        if (!mixed_ok) mixed_cpu_mismatches++;
        if (!duplicate_ok) duplicate_cpu_mismatches++;
        if (!mixdup_ok) mixed_duplicate_mismatches++;
        if (duplicate_good_lanes != LANES) duplicate_uniform_mismatches++;

        if (csv) {
            fprintf(csv, "%d,%d,%d,%" PRId32 ",%" PRId32 ",%" PRId32
                    ",%" PRId32 ",%d,%d,%d,%d\n",
                    a->row_group, a->lane, a->row, cpu, mixed, duplicate,
                    duplicate_lane0, mixed_ok, duplicate_ok, mixdup_ok,
                    duplicate_good_lanes);
        }
        printf("identity_scale_row: %d,%d,%d,%" PRId32 ",%" PRId32 ",%" PRId32
               ",%" PRId32 ",%d,%d,%d,%d\n",
               a->row_group, a->lane, a->row, cpu, mixed, duplicate,
               duplicate_lane0, mixed_ok, duplicate_ok, mixdup_ok,
               duplicate_good_lanes);
        if ((!mixed_ok || !duplicate_ok || !mixdup_ok || duplicate_good_lanes != LANES) &&
            mismatch_samples < 32) {
            printf("identity_scale_mismatch: row_group=%d lane=%d row=%d cpu=%" PRId32 " mixed=%" PRId32 " duplicate_same_lane=%" PRId32 " duplicate_lane0=%" PRId32 " duplicate_good_lanes=%d\n",
                   a->row_group, a->lane, a->row, cpu, mixed, duplicate,
                   duplicate_lane0, duplicate_good_lanes);
            mismatch_samples++;
        }
    }

    printf("identity_scale_rows_checked: %zu\n", qproj_affected_row_count());
    printf("identity_scale_mixed_cpu_mismatches: %" PRIu64 "\n", mixed_cpu_mismatches);
    printf("identity_scale_duplicate_cpu_mismatches: %" PRIu64 "\n",
           duplicate_cpu_mismatches);
    printf("identity_scale_mixed_duplicate_mismatches: %" PRIu64 "\n",
           mixed_duplicate_mismatches);
    printf("identity_scale_duplicate_uniform_mismatches: %" PRIu64 "\n",
           duplicate_uniform_mismatches);
    if (opt->identity_scale_csv) {
        printf("identity_scale_csv: %s\n", opt->identity_scale_csv);
    }
    if (mixed_cpu_mismatches == 0 && duplicate_cpu_mismatches == 0 &&
        mixed_duplicate_mismatches == 0 && duplicate_uniform_mismatches == 0) {
        printf("identity_scale_result: PASS\n");
        printf("classification: IDENTITY_SCALE_PASS_REAL_SCALE_PATH_SUSPECT\n");
    } else if (mixed_cpu_mismatches != 0 && duplicate_cpu_mismatches == 0) {
        printf("identity_scale_result: FAIL\n");
        printf("classification: IDENTITY_SCALE_MIXED_ROW_FAIL\n");
    } else {
        printf("identity_scale_result: FAIL\n");
        printf("classification: IDENTITY_SCALE_COMMON_OR_DUPLICATE_FAIL\n");
    }
    rc = 0;

done:
    if (hw.opened) gemv_hw_close(&hw);
    if (csv) fclose(csv);
    free(prompt_text);
    free(ids.data);
    free(x);
    free(xb);
    free(input_i16);
    free(packet);
    return rc;
}

static int mkdir_p_local(const char *path)
{
    char tmp[4096];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(tmp, path, n + 1u);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0775) && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0775) && errno != EEXIST) return -1;
    return 0;
}

static int join_path(char *out, size_t out_n, const char *a, const char *b)
{
    int n = snprintf(out, out_n, "%s/%s", a, b);
    return (n < 0 || (size_t)n >= out_n) ? -1 : 0;
}

static int write_bytes_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int rc = (fwrite(data, 1, n, f) == n) ? 0 : -1;
    if (fclose(f) && rc == 0) rc = -1;
    return rc;
}

static int write_i16_le_file(const char *path, const int16_t *v, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    for (int i = 0; i < n; ++i) {
        uint16_t u = (uint16_t)v[i];
        if (fputc((int)(u & 0xffu), f) == EOF ||
            fputc((int)((u >> 8) & 0xffu), f) == EOF) {
            fclose(f);
            return -1;
        }
    }
    return fclose(f) ? -1 : 0;
}

static int write_i32_le_file(const char *path, const int32_t *v, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    for (int i = 0; i < n; ++i) {
        uint32_t u = (uint32_t)v[i];
        for (int b = 0; b < 4; ++b) {
            if (fputc((int)((u >> (8 * b)) & 0xffu), f) == EOF) {
                fclose(f);
                return -1;
            }
        }
    }
    return fclose(f) ? -1 : 0;
}

static int write_u8_hex_file(const char *path, const uint8_t *v, size_t n)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (size_t i = 0; i < n; ++i) {
        if (fprintf(f, "%02x\n", (unsigned)v[i]) < 0) {
            fclose(f);
            return -1;
        }
    }
    return fclose(f) ? -1 : 0;
}

static int write_i16_hex_file(const char *path, const int16_t *v, int n)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < n; ++i) {
        if (fprintf(f, "%04x\n", (unsigned)((uint16_t)v[i])) < 0) {
            fclose(f);
            return -1;
        }
    }
    return fclose(f) ? -1 : 0;
}

static int write_i32_hex_file(const char *path, const int32_t *v, int n)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < n; ++i) {
        if (fprintf(f, "%08x\n", (unsigned)((uint32_t)v[i])) < 0) {
            fclose(f);
            return -1;
        }
    }
    return fclose(f) ? -1 : 0;
}

static int dump_case_file_set(const char *case_dir, const int16_t *input_i16, int in_features,
                              const uint8_t *packet, uint32_t packet_bytes,
                              const int32_t *mode0, const int32_t *block_acc,
                              const int32_t *scaled_block, int block_words,
                              const int32_t *scale_q, int scale_words)
{
    char path[4096];
    if (join_path(path, sizeof(path), case_dir, "input_i16.bin") ||
        write_i16_le_file(path, input_i16, in_features)) return -1;
    if (join_path(path, sizeof(path), case_dir, "input_i16.hex") ||
        write_i16_hex_file(path, input_i16, in_features)) return -1;
    if (join_path(path, sizeof(path), case_dir, "packet_axis128.bin") ||
        write_bytes_file(path, packet, packet_bytes)) return -1;
    if (join_path(path, sizeof(path), case_dir, "packet_axis128.hex") ||
        write_u8_hex_file(path, packet, packet_bytes)) return -1;
    if (join_path(path, sizeof(path), case_dir, "output_mode0_i32.bin") ||
        write_i32_le_file(path, mode0, LANES)) return -1;
    if (join_path(path, sizeof(path), case_dir, "output_mode0_i32.hex") ||
        write_i32_hex_file(path, mode0, LANES)) return -1;
    if (join_path(path, sizeof(path), case_dir, "block_acc_i32.bin") ||
        write_i32_le_file(path, block_acc, block_words)) return -1;
    if (join_path(path, sizeof(path), case_dir, "block_acc_i32.hex") ||
        write_i32_hex_file(path, block_acc, block_words)) return -1;
    if (join_path(path, sizeof(path), case_dir, "scaled_block_i32.bin") ||
        write_i32_le_file(path, scaled_block, block_words)) return -1;
    if (join_path(path, sizeof(path), case_dir, "scaled_block_i32.hex") ||
        write_i32_hex_file(path, scaled_block, block_words)) return -1;
    if (join_path(path, sizeof(path), case_dir, "scale_q_i32.bin") ||
        write_i32_le_file(path, scale_q, scale_words)) return -1;
    return 0;
}

static int run_real_qproj_fixture_dump(model_t *m, const options_t *opt)
{
    int rc = -1;
    int used_gguf_template = 0;
    char *prompt_text = NULL;
    int_vec_t ids = {0};
    float *x = NULL;
    float *xb = NULL;
    int16_t *input_i16 = NULL;
    uint8_t *packet = NULL;
    int32_t *block_acc = NULL;
    int32_t *scaled_block = NULL;
    int32_t *scale_q = NULL;
    FILE *summary = NULL;
    FILE *affected_csv = NULL;

    if (!m->q_proj[0]) {
        fprintf(stderr, "real q_proj fixture dump failed: missing layer0 q_proj tensor\n");
        return -1;
    }
    tensor_t *t = m->q_proj[0];
    int n = m->n_embd;
    if ((n % Q8_BLOCK) || t->rows < LANES || t->cols < n) {
        fprintf(stderr, "real q_proj fixture dump failed: unsupported shape rows=%d cols=%d n=%d\n",
                t->rows, t->cols, n);
        return -1;
    }
    int blocks = n / Q8_BLOCK;
    int block_words = blocks * LANES;
    int32_t identity_scale_q = (int32_t)(1u << opt->act_shift);

    if (mkdir_p_local(opt->dump_real_qproj_fixture_dir)) {
        fprintf(stderr, "fixture dir create failed: %s: %s\n",
                opt->dump_real_qproj_fixture_dir, strerror(errno));
        return -1;
    }

    if (opt->use_chat_template) {
        prompt_text = build_chat_prompt(&m->tokenizer, opt->prompt, &used_gguf_template);
    } else {
        prompt_text = strdup(opt->prompt);
    }
    if (!prompt_text || tokenizer_encode(&m->tokenizer, prompt_text, &ids) || ids.len <= 0) {
        fprintf(stderr, "real q_proj fixture dump tokenizer failed\n");
        goto done;
    }

    x = (float *)malloc((size_t)n * sizeof(float));
    xb = (float *)malloc((size_t)n * sizeof(float));
    input_i16 = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    packet = (uint8_t *)malloc((size_t)blocks * packet_block_bytes());
    block_acc = (int32_t *)malloc((size_t)block_words * sizeof(int32_t));
    scaled_block = (int32_t *)malloc((size_t)block_words * sizeof(int32_t));
    scale_q = (int32_t *)malloc((size_t)block_words * sizeof(int32_t));
    if (!x || !xb || !input_i16 || !packet || !block_acc || !scaled_block || !scale_q) {
        fprintf(stderr, "real q_proj fixture dump allocation failed\n");
        goto done;
    }

    int token = ids.data[0];
    dequantize_row_q8(m, m->tok_embd, token, x);
    rms_norm(x, tensor_f32_ptr(m, m->attn_norm[0]), n, m->rms_eps, xb);
    uint64_t saturations = 0;
    quantize_input_chunk(xb, 0, n, opt->act_shift, input_i16, &saturations);

    char path[4096];
    if (join_path(path, sizeof(path), opt->dump_real_qproj_fixture_dir, "case_summary.csv")) {
        fprintf(stderr, "fixture summary path too long\n");
        goto done;
    }
    summary = fopen(path, "w");
    if (!summary) {
        fprintf(stderr, "fixture summary open failed: %s: %s\n", path, strerror(errno));
        goto done;
    }
    fprintf(summary, "case,row_group,affected_lanes,affected_rows,in_features,out_features,blocks,packet_bytes,axis128_beats,expected_tlast_beat,identity_scale_q,fixture_dir\n");

    if (join_path(path, sizeof(path), opt->dump_real_qproj_fixture_dir, "affected_rows.csv")) {
        fprintf(stderr, "fixture affected path too long\n");
        goto done;
    }
    affected_csv = fopen(path, "w");
    if (!affected_csv) {
        fprintf(stderr, "fixture affected open failed: %s: %s\n", path, strerror(errno));
        goto done;
    }
    fprintf(affected_csv, "row_group,lane,row,fixed_i32,identity_expected_i32,fixture_case\n");

    int group_has_affected[128] = {0};
    if (t->rows / LANES >= (int)(sizeof(group_has_affected) / sizeof(group_has_affected[0]))) {
        fprintf(stderr, "real q_proj fixture dump group table too small\n");
        goto done;
    }
    for (size_t ai = 0; ai < qproj_affected_row_count(); ++ai) {
        const affected_row_t *a = &k_qproj_affected_rows[ai];
        if (a->row_group >= 0 && a->row_group * LANES + a->lane == a->row &&
            a->row < t->rows) {
            group_has_affected[a->row_group] = 1;
        }
    }

    int case_count = 0;
    uint32_t packet_bytes = 0;
    for (int row_group = 0; row_group * LANES < t->rows; ++row_group) {
        if (!group_has_affected[row_group]) continue;
        int row_base = row_group * LANES;
        char case_name[64];
        char case_dir[4096];
        snprintf(case_name, sizeof(case_name), "qproj_rg%02d", row_group);
        if (join_path(case_dir, sizeof(case_dir), opt->dump_real_qproj_fixture_dir, case_name) ||
            mkdir_p_local(case_dir)) {
            fprintf(stderr, "fixture case dir create failed: %s\n", case_name);
            goto done;
        }

        if (build_packet_from_tensor(m, t, row_base, 0, n, packet, &packet_bytes)) {
            fprintf(stderr, "fixture packet build failed row_group=%d\n", row_group);
            goto done;
        }
        packet_override_all_scale_q(packet, blocks, identity_scale_q);

        int32_t mode0[LANES];
        for (int block = 0; block < blocks; ++block) {
            for (int lane = 0; lane < LANES; ++lane) {
                int row = row_base + lane;
                int idx = block * LANES + lane;
                int32_t acc = q8_block_acc_ref_i32(m, t, input_i16, row, block);
                block_acc[idx] = acc;
                scaled_block[idx] = sat_i64_to_i32(round_shift_signed_i64(
                    (int64_t)acc * (int64_t)identity_scale_q, SCALE_SHIFT));
                scale_q[idx] = identity_scale_q;
            }
        }
        for (int lane = 0; lane < LANES; ++lane) {
            mode0[lane] = q8_const_scale_ref_i32(m, t, input_i16, row_base + lane,
                                                 blocks, identity_scale_q);
        }

        if (dump_case_file_set(case_dir, input_i16, n, packet, packet_bytes, mode0,
                               block_acc, scaled_block, block_words, scale_q, block_words)) {
            fprintf(stderr, "fixture file write failed: %s\n", case_name);
            goto done;
        }

        int affected_lanes[LANES];
        int affected_count = 0;
        for (size_t ai = 0; ai < qproj_affected_row_count(); ++ai) {
            const affected_row_t *a = &k_qproj_affected_rows[ai];
            if (a->row_group == row_group) {
                affected_lanes[affected_count++] = a->lane;
                fprintf(affected_csv, "%d,%d,%d,%d,%d,%s\n",
                        a->row_group, a->lane, a->row, a->fixed_i32,
                        mode0[a->lane], case_name);
            }
        }

        char lanes_buf[128] = "";
        char rows_buf[256] = "";
        for (int i = 0; i < affected_count; ++i) {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%s%d", i ? "|" : "", affected_lanes[i]);
            strncat(lanes_buf, tmp, sizeof(lanes_buf) - strlen(lanes_buf) - 1u);
            snprintf(tmp, sizeof(tmp), "%s%d", i ? "|" : "", row_base + affected_lanes[i]);
            strncat(rows_buf, tmp, sizeof(rows_buf) - strlen(rows_buf) - 1u);
        }

        char manifest_path[4096];
        if (join_path(manifest_path, sizeof(manifest_path), case_dir, "MANIFEST.txt")) {
            fprintf(stderr, "fixture manifest path too long\n");
            goto done;
        }
        FILE *mf = fopen(manifest_path, "w");
        if (!mf) {
            fprintf(stderr, "fixture manifest open failed: %s: %s\n",
                    manifest_path, strerror(errno));
            goto done;
        }
        fprintf(mf,
                "name: %s\n"
                "source: S06.5.2 real layer0 q_proj raw Hi identity-scale fixture\n"
                "prompt_mode: %s\n"
                "raw_prompt: %s\n"
                "prompt_token_count: %d\n"
                "probe_token: %d\n"
                "act_shift: %d\n"
                "identity_scale_q: %d\n"
                "input_saturations: %" PRIu64 "\n"
                "row_group: %d\n"
                "row_base: %d\n"
                "affected_lanes: %s\n"
                "affected_rows: %s\n"
                "in_features: %d\n"
                "out_features: %d\n"
                "blocks_per_row: %d\n"
                "packet_bytes: %u\n"
                "axis_width_bits: 128\n"
                "expected_tlast_beat: %u\n"
                "mode0_output_order: lane\n"
                "mode1_output_order: block,lane\n",
                case_name, opt->use_chat_template ? "chat" : "raw", opt->prompt,
                ids.len, token, opt->act_shift, identity_scale_q, saturations,
                row_group, row_base, lanes_buf, rows_buf, n, LANES, blocks,
                packet_bytes, packet_bytes / 16u - 1u);
        if (fclose(mf)) {
            fprintf(stderr, "fixture manifest close failed: %s\n", manifest_path);
            goto done;
        }

        fprintf(summary, "%s,%d,%s,%s,%d,%d,%d,%u,%u,%u,%d,%s\n",
                case_name, row_group, lanes_buf, rows_buf, n, LANES, blocks,
                packet_bytes, packet_bytes / 16u, packet_bytes / 16u - 1u,
                identity_scale_q, case_dir);
        case_count++;
    }

    printf("real_qproj_fixture_dump: layer=0 role=q_proj tensor=%s\n", t->name);
    printf("raw_prompt: %s\n", opt->prompt);
    printf("chat_template_applied: %s\n", opt->use_chat_template ? "true" : "false");
    printf("prompt_token_count: %d\n", ids.len);
    printf("probe_token: %d\n", token);
    printf("probe_act_shift: %d\n", opt->act_shift);
    printf("identity_scale_q: %" PRId32 "\n", identity_scale_q);
    printf("probe_input_saturations: %" PRIu64 "\n", saturations);
    printf("tensor_rows: %d\n", t->rows);
    printf("tensor_cols: %d\n", t->cols);
    printf("in_features: %d\n", n);
    printf("q8_blocks: %d\n", blocks);
    printf("affected_rows: %zu\n", qproj_affected_row_count());
    printf("fixture_affected_row_groups: %d\n", case_count);
    printf("fixture_dir: %s\n", opt->dump_real_qproj_fixture_dir);
    printf("fixture_status: PREPARED_NOT_SIMULATED\n");
    print_i16_stats("fixture_quantized_input", input_i16, n);
    rc = 0;

done:
    if (summary) fclose(summary);
    if (affected_csv) fclose(affected_csv);
    free(prompt_text);
    free(ids.data);
    free(x);
    free(xb);
    free(input_i16);
    free(packet);
    free(block_acc);
    free(scaled_block);
    free(scale_q);
    return rc;
}

static int run_onehot_localization(model_t *m, const options_t *opt)
{
    static const int test_cols[] = {0, 31, 32, 575};
    int rc = -1;
    int16_t *input_i16 = NULL;
    uint8_t *packet = NULL;
    FILE *csv = NULL;
    gemv_hw_t hw;
    memset(&hw, 0, sizeof(hw));
    hw.dma_fd = hw.bram_fd = hw.gemv_fd = hw.mem_fd = -1;

    if (!m->q_proj[0]) {
        fprintf(stderr, "onehot localization failed: missing layer0 q_proj tensor\n");
        return -1;
    }
    tensor_t *t = m->q_proj[0];
    int n = m->n_embd;
    if ((n % Q8_BLOCK) || t->cols < n) {
        fprintf(stderr, "onehot localization failed: unsupported q_proj cols=%d n_embd=%d\n",
                t->cols, n);
        return -1;
    }
    int blocks = n / Q8_BLOCK;
    int row_groups = (t->rows + LANES - 1) / LANES;
    int16_t onehot_value = (int16_t)(1u << opt->act_shift);

    input_i16 = (int16_t *)calloc((size_t)n, sizeof(int16_t));
    packet = (uint8_t *)malloc((size_t)blocks * packet_block_bytes());
    if (!input_i16 || !packet) {
        fprintf(stderr, "onehot localization allocation failed\n");
        goto done;
    }
    if (opt->onehot_csv) {
        csv = fopen(opt->onehot_csv, "w");
        if (!csv) {
            fprintf(stderr, "onehot localization csv open failed: %s: %s\n",
                    opt->onehot_csv, strerror(errno));
            goto done;
        }
        fprintf(csv, "test_col,block,col_in_block,row_group,lane,row,input_i16,weight_i8,scale_q,block_acc_i32,cpu_i32,fpga_i32,match,is_affected_lane\n");
    }

    printf("onehot_input_localization: layer=0 role=q_proj tensor=%s\n", t->name);
    printf("hardware_execution: yes\n");
    printf("scale_policy: real_tensor_scale_q\n");
    printf("probe_act_shift: %d\n", opt->act_shift);
    printf("onehot_input_value_i16: %" PRId16 "\n", onehot_value);
    printf("tensor_rows: %d\n", t->rows);
    printf("tensor_cols: %d\n", t->cols);
    printf("in_features: %d\n", n);
    printf("q8_blocks: %d\n", blocks);
    printf("row_groups: %d\n", row_groups);
    printf("test_columns:");
    for (size_t i = 0; i < sizeof(test_cols) / sizeof(test_cols[0]); ++i) {
        printf(" %d", test_cols[i]);
    }
    printf("\n");

    if (gemv_hw_open(&hw, DEFAULT_PHYS_BASE, DEFAULT_PHYS_SIZE) ||
        gemv_hw_reset_dma(&hw)) {
        fprintf(stderr, "onehot localization FPGA open/reset failed\n");
        goto done;
    }

    uint64_t total_checks = 0;
    uint64_t total_mismatches = 0;
    uint64_t affected_checks = 0;
    uint64_t affected_mismatches = 0;
    uint64_t lane_mismatches[LANES] = {0};
    uint64_t lane_checks[LANES] = {0};
    int mismatch_samples = 0;

    printf("onehot_col_summary_header: test_col,block,col_in_block,total_checks,total_mismatches,affected_checks,affected_mismatches,lane_mismatches\n");
    for (size_t ci = 0; ci < sizeof(test_cols) / sizeof(test_cols[0]); ++ci) {
        int test_col = test_cols[ci];
        if (test_col < 0 || test_col >= n) {
            fprintf(stderr, "onehot localization invalid test col=%d n=%d\n", test_col, n);
            goto done;
        }
        int test_block = test_col / Q8_BLOCK;
        int col_in_block = test_col % Q8_BLOCK;
        memset(input_i16, 0, (size_t)n * sizeof(int16_t));
        input_i16[test_col] = onehot_value;

        uint64_t col_checks = 0;
        uint64_t col_mismatches = 0;
        uint64_t col_affected_checks = 0;
        uint64_t col_affected_mismatches = 0;
        uint64_t col_lane_mismatches[LANES] = {0};

        for (int rg = 0; rg < row_groups; ++rg) {
            uint32_t packet_bytes = 0;
            int32_t fpga_out[LANES];
            int row_group_base = rg * LANES;
            if (build_packet_from_tensor(m, t, row_group_base, 0, n, packet, &packet_bytes)) {
                fprintf(stderr, "onehot packet build failed rg=%d\n", rg);
                goto done;
            }
            gemv_hw_load_input(&hw, input_i16, n);
            if (gemv_hw_run_packet(&hw, packet, packet_bytes, (uint32_t)n, fpga_out)) {
                fprintf(stderr, "onehot FPGA run failed col=%d rg=%d\n", test_col, rg);
                goto done;
            }
            for (int lane = 0; lane < LANES; ++lane) {
                int row = row_group_base + lane;
                if (row >= t->rows) continue;
                const uint8_t *rp = tensor_row_ptr(m, t, row) + test_block * Q8_BLOCK_BYTES;
                int32_t scale_q = tensor_scale_q_at(m, t, row, test_block);
                int8_t w = ((const int8_t *)(const void *)(rp + 2))[col_in_block];
                int32_t block_acc = (int32_t)onehot_value * (int32_t)w;
                int32_t cpu = sat_i64_to_i32(round_shift_signed_i64(
                    (int64_t)block_acc * (int64_t)scale_q, SCALE_SHIFT));
                int32_t got = fpga_out[lane];
                int match = (got == cpu);
                int affected = qproj_is_affected_lane(rg, lane);
                total_checks++;
                col_checks++;
                lane_checks[lane]++;
                if (affected) {
                    affected_checks++;
                    col_affected_checks++;
                }
                if (!match) {
                    total_mismatches++;
                    col_mismatches++;
                    lane_mismatches[lane]++;
                    col_lane_mismatches[lane]++;
                    if (affected) {
                        affected_mismatches++;
                        col_affected_mismatches++;
                    }
                    if (mismatch_samples < 64) {
                        printf("onehot_mismatch: test_col=%d block=%d col_in_block=%d row_group=%d lane=%d row=%d input=%" PRId16 " weight=%d scale_q=%" PRId32 " block_acc=%" PRId32 " cpu=%" PRId32 " fpga=%" PRId32 " affected=%d\n",
                               test_col, test_block, col_in_block, rg, lane, row,
                               onehot_value, (int)w, scale_q, block_acc, cpu,
                               got, affected);
                        mismatch_samples++;
                    }
                }
                if (csv) {
                    fprintf(csv, "%d,%d,%d,%d,%d,%d,%" PRId16 ",%d,%" PRId32
                            ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%d,%d\n",
                            test_col, test_block, col_in_block, rg, lane, row,
                            onehot_value, (int)w, scale_q, block_acc, cpu, got,
                            match, affected);
                }
            }
        }
        printf("onehot_col_summary: %d,%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",",
               test_col, test_block, col_in_block, col_checks, col_mismatches,
               col_affected_checks, col_affected_mismatches);
        for (int lane = 0; lane < LANES; ++lane) {
            printf("%s%" PRIu64, lane ? " " : "", col_lane_mismatches[lane]);
        }
        printf("\n");
    }

    printf("onehot_total_checks: %" PRIu64 "\n", total_checks);
    printf("onehot_total_mismatches: %" PRIu64 "\n", total_mismatches);
    printf("onehot_affected_checks: %" PRIu64 "\n", affected_checks);
    printf("onehot_affected_mismatches: %" PRIu64 "\n", affected_mismatches);
    printf("onehot_lane_checks:");
    for (int lane = 0; lane < LANES; ++lane) printf(" lane%d=%" PRIu64, lane, lane_checks[lane]);
    printf("\n");
    printf("onehot_lane_mismatches:");
    for (int lane = 0; lane < LANES; ++lane) printf(" lane%d=%" PRIu64, lane, lane_mismatches[lane]);
    printf("\n");
    if (opt->onehot_csv) {
        printf("onehot_csv: %s\n", opt->onehot_csv);
    }
    if (total_mismatches == 0) {
        printf("onehot_result: PASS\n");
        printf("classification: ONEHOT_MODE0_PASS\n");
    } else {
        printf("onehot_result: FAIL\n");
        printf("classification: MODE0_OUTPUT_PATH_FAIL\n");
    }
    rc = 0;

done:
    if (hw.opened) gemv_hw_close(&hw);
    if (csv) fclose(csv);
    free(input_i16);
    free(packet);
    return rc;
}

static int run_first_gemv_probe(model_t *m, const options_t *opt)
{
    int rc = -1;
    int used_gguf_template = 0;
    char *prompt_text = NULL;
    int_vec_t ids = {0};
    float *x = NULL, *xb = NULL, *cpu_out = NULL, *fixed_float = NULL, *fpga_float = NULL;
    int16_t *input_i16 = NULL;
    uint8_t *packet = NULL;
    gemv_hw_t hw;
    memset(&hw, 0, sizeof(hw));
    hw.dma_fd = hw.bram_fd = hw.gemv_fd = hw.mem_fd = -1;

    if (opt->use_chat_template) {
        prompt_text = build_chat_prompt(&m->tokenizer, opt->prompt, &used_gguf_template);
    } else {
        prompt_text = strdup(opt->prompt);
    }
    if (!prompt_text || tokenizer_encode(&m->tokenizer, prompt_text, &ids) || ids.len <= 0) {
        fprintf(stderr, "probe tokenizer failed\n");
        goto done;
    }

    int n = m->n_embd;
    x = (float *)malloc((size_t)n * sizeof(float));
    xb = (float *)malloc((size_t)n * sizeof(float));
    cpu_out = (float *)malloc((size_t)m->q_proj[0]->rows * sizeof(float));
    fixed_float = (float *)malloc((size_t)LANES * sizeof(float));
    fpga_float = (float *)malloc((size_t)LANES * sizeof(float));
    input_i16 = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    packet = (uint8_t *)malloc(10368u);
    if (!x || !xb || !cpu_out || !fixed_float || !fpga_float || !input_i16 || !packet) {
        fprintf(stderr, "probe allocation failed\n");
        goto done;
    }

    int token = ids.data[0];
    dequantize_row_q8(m, m->tok_embd, token, x);
    rms_norm(x, tensor_f32_ptr(m, m->attn_norm[0]), n, m->rms_eps, xb);
    uint64_t saturations = 0;
    quantize_input_chunk(xb, 0, n, opt->act_shift, input_i16, &saturations);

    int32_t fixed_ref[LANES];
    int32_t fpga_i32[LANES];
    uint32_t packet_bytes = 0;
    q8_fixed_ref_i32(m, m->q_proj[0], input_i16, 0, 0, n, fixed_ref);
    gemv_cpu_q8(m, m->q_proj[0], xb, cpu_out);
    if (build_packet_from_tensor(m, m->q_proj[0], 0, 0, n, packet, &packet_bytes)) {
        fprintf(stderr, "probe packet build failed\n");
        goto done;
    }

    int32_t scale_min = INT32_MAX, scale_max = INT32_MIN;
    int64_t scale_sum = 0;
    int scale_count = 0;
    for (int b = 0; b < n / Q8_BLOCK; ++b) {
        for (int lane = 0; lane < LANES; ++lane) {
            const uint8_t *rp = tensor_row_ptr(m, m->q_proj[0], lane) + b * Q8_BLOCK_BYTES;
            int32_t sq = (int32_t)lrintf(f16_to_f32(rd_le16(rp)) * (float)(1 << SCALE_SHIFT));
            if (sq < scale_min) scale_min = sq;
            if (sq > scale_max) scale_max = sq;
            scale_sum += sq;
            scale_count++;
        }
    }

    printf("probe_first_gemv: layer=0 role=q_proj tensor=%s row_group=0 token=%d\n",
           m->q_proj[0]->name, token);
    printf("raw_prompt: %s\n", opt->prompt);
    printf("chat_template_applied: %s\n", opt->use_chat_template ? "true" : "false");
    printf("chat_template_source: %s\n",
           opt->use_chat_template ? (used_gguf_template ? "gguf_chatml" : "unsupported") : "none");
    printf("prompt_token_count: %d\n", ids.len);
    printf("prompt_token_ids:");
    for (int i = 0; i < ids.len; ++i) printf(" %d", ids.data[i]);
    printf("\n");
    printf("probe_act_shift: %d\n", opt->act_shift);
    printf("probe_in_features: %d\n", n);
    printf("probe_out_lanes: %d\n", LANES);
    printf("probe_packet_bytes: %" PRIu32 "\n", packet_bytes);
    printf("probe_input_saturations: %" PRIu64 "\n", saturations);
    dump_float_stats("probe_embedding_output", x, n);
    dump_float_stats("probe_after_input_rmsnorm", xb, n);
    print_i16_stats("probe_quantized_input", input_i16, n);
    printf("probe_scale_q_stats: count=%d min=%" PRId32 " max=%" PRId32
           " mean=%.9g checksum=%" PRId64 "\n",
           scale_count, scale_min, scale_max, (double)scale_sum / (double)scale_count,
           scale_sum);
    printf("probe_packet_first64_hex:");
    for (int i = 0; i < 64 && i < (int)packet_bytes; ++i) printf(" %02x", packet[i]);
    printf("\n");

    if (gemv_hw_open(&hw, DEFAULT_PHYS_BASE, DEFAULT_PHYS_SIZE) ||
        gemv_hw_reset_dma(&hw)) {
        fprintf(stderr, "probe FPGA open/reset failed\n");
        goto done;
    }
    gemv_hw_load_input(&hw, input_i16, n);
    if (gemv_hw_run_packet(&hw, packet, packet_bytes, (uint32_t)n, fpga_i32)) {
        fprintf(stderr, "probe FPGA run failed\n");
        goto done;
    }

    float inv = 1.0f / (float)(1 << opt->act_shift);
    for (int i = 0; i < LANES; ++i) {
        fixed_float[i] = (float)fixed_ref[i] * inv;
        fpga_float[i] = (float)fpga_i32[i] * inv;
    }
    print_f32_line("probe_cpu_float_first16", cpu_out, LANES);
    print_i32_line("probe_fixed_ref_i32_first16", fixed_ref, LANES);
    print_f32_line("probe_fixed_ref_float_first16", fixed_float, LANES);
    print_i32_line("probe_fpga_i32_first16", fpga_i32, LANES);
    print_f32_line("probe_fpga_float_first16", fpga_float, LANES);

    int mismatches = 0;
    int first_mismatch = -1;
    int64_t max_abs_diff = 0;
    for (int i = 0; i < LANES; ++i) {
        int64_t d = (int64_t)fpga_i32[i] - (int64_t)fixed_ref[i];
        int64_t ad = d < 0 ? -d : d;
        if (ad > max_abs_diff) max_abs_diff = ad;
        if (fpga_i32[i] != fixed_ref[i]) {
            if (first_mismatch < 0) first_mismatch = i;
            mismatches++;
        }
    }
    printf("probe_fixed_vs_fpga_mismatches: %d\n", mismatches);
    printf("probe_fixed_vs_fpga_first_mismatch: %d\n", first_mismatch);
    printf("probe_fixed_vs_fpga_max_abs_diff_i32: %" PRId64 "\n", max_abs_diff);
    printf("probe_debug_status: status=0x%08" PRIx32 " done=0x%08" PRIx32
           " error=0x%08" PRIx32 "\n",
           rd32(hw.gemv, GEMV_STATUS), rd32(hw.gemv, GEMV_DONE),
           rd32(hw.gemv, GEMV_ERROR_CODE));
    printf("probe_debug_pos: row=%" PRIu32 " block=%" PRIu32 " lane=%" PRIu32
           " in_count=%" PRIu32 " tlast_count=%" PRIu32
           " tlast_tdata=0x%08" PRIx32 " tlast_tkeep=0x%08" PRIx32 "\n",
           rd32(hw.gemv, GEMV_DEBUG_ROW), rd32(hw.gemv, GEMV_DEBUG_BLOCK),
           rd32(hw.gemv, GEMV_DEBUG_LANE), rd32(hw.gemv, GEMV_DEBUG_IN_COUNT),
           rd32(hw.gemv, GEMV_DEBUG_TLAST_COUNT),
           rd32(hw.gemv, GEMV_DEBUG_TLAST_TDATA),
           rd32(hw.gemv, GEMV_DEBUG_TLAST_TKEEP));
    printf("probe_debug_scale012: %" PRId32 " %" PRId32 " %" PRId32 "\n",
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_SCALE0),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_SCALE1),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_SCALE2));
    printf("probe_debug_block012: %" PRId32 " %" PRId32 " %" PRId32 "\n",
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_BLOCK0),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_BLOCK1),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_BLOCK2));
    printf("probe_debug_product012: %" PRId64 " %" PRId64 " %" PRId64 "\n",
           join_i64_words(rd32(hw.gemv, GEMV_DEBUG_PRODUCT0_HI),
                          rd32(hw.gemv, GEMV_DEBUG_PRODUCT0_LO)),
           join_i64_words(rd32(hw.gemv, GEMV_DEBUG_PRODUCT1_HI),
                          rd32(hw.gemv, GEMV_DEBUG_PRODUCT1_LO)),
           join_i64_words(rd32(hw.gemv, GEMV_DEBUG_PRODUCT2_HI),
                          rd32(hw.gemv, GEMV_DEBUG_PRODUCT2_LO)));
    printf("probe_debug_scaled012: %" PRId32 " %" PRId32 " %" PRId32 "\n",
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_SCALED0),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_SCALED1),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_SCALED2));
    printf("probe_debug_row_acc012: %" PRId32 " %" PRId32 " %" PRId32 "\n",
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_ROW_ACC0),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_ROW_ACC1),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_ROW_ACC2));
    printf("probe_debug_out012: %" PRId32 " %" PRId32 " %" PRId32 "\n",
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_OUT0),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_OUT1),
           (int32_t)rd32(hw.gemv, GEMV_DEBUG_OUT2));

    int lane_mismatches[LANES] = {0};
    int lane_saturations[LANES] = {0};
    int total_scan_mismatches = 0;
    int total_scan_saturations = 0;
    int repair_rows[8];
    int repair_count = 0;
    int row_groups = (m->q_proj[0]->rows + LANES - 1) / LANES;
    printf("probe_q_proj_scan_header: row_group,lane,row,fixed_i32,fpga_i32,diff_i32\n");
    for (int rg = 0; rg < row_groups; ++rg) {
        if (build_packet_from_tensor(m, m->q_proj[0], rg * LANES, 0, n,
                                     packet, &packet_bytes)) {
            fprintf(stderr, "probe scan packet build failed rg=%d\n", rg);
            goto done;
        }
        q8_fixed_ref_i32(m, m->q_proj[0], input_i16, rg * LANES, 0, n, fixed_ref);
        gemv_hw_load_input(&hw, input_i16, n);
        if (gemv_hw_run_packet(&hw, packet, packet_bytes, (uint32_t)n, fpga_i32)) {
            fprintf(stderr, "probe scan FPGA run failed rg=%d\n", rg);
            goto done;
        }
        for (int lane = 0; lane < LANES; ++lane) {
            int row = rg * LANES + lane;
            if (row >= m->q_proj[0]->rows) continue;
            if (fpga_i32[lane] == INT32_MAX || fpga_i32[lane] == INT32_MIN) {
                lane_saturations[lane]++;
                total_scan_saturations++;
            }
            if (fpga_i32[lane] != fixed_ref[lane]) {
                lane_mismatches[lane]++;
                total_scan_mismatches++;
                int have_repair_row = 0;
                for (int ri = 0; ri < repair_count; ++ri) {
                    if (repair_rows[ri] == row) have_repair_row = 1;
                }
                if (!have_repair_row && repair_count < (int)(sizeof(repair_rows) / sizeof(repair_rows[0]))) {
                    repair_rows[repair_count++] = row;
                }
                printf("probe_q_proj_scan_mismatch: %d,%d,%d,%" PRId32 ",%" PRId32 ",%" PRId64 "\n",
                       rg, lane, row, fixed_ref[lane], fpga_i32[lane],
                       (int64_t)fpga_i32[lane] - (int64_t)fixed_ref[lane]);
            }
        }
    }
    printf("probe_q_proj_scan_total_mismatches: %d\n", total_scan_mismatches);
    printf("probe_q_proj_scan_total_saturated_outputs: %d\n", total_scan_saturations);
    printf("probe_q_proj_scan_lane_mismatches:");
    for (int lane = 0; lane < LANES; ++lane) printf(" lane%d=%d", lane, lane_mismatches[lane]);
    printf("\n");
    printf("probe_q_proj_scan_lane_saturated_outputs:");
    for (int lane = 0; lane < LANES; ++lane) printf(" lane%d=%d", lane, lane_saturations[lane]);
    printf("\n");

    printf("probe_repair_all_lanes_header: row,fixed_i32,good_lanes,outputs\n");
    for (int ri = 0; ri < repair_count; ++ri) {
        int row = repair_rows[ri];
        int row_map[LANES];
        for (int lane = 0; lane < LANES; ++lane) row_map[lane] = row;
        if (build_packet_from_tensor_row_map(m, m->q_proj[0], row_map, 0, n,
                                             packet, &packet_bytes)) {
            fprintf(stderr, "probe repair packet build failed row=%d\n", row);
            goto done;
        }
        q8_fixed_ref_i32(m, m->q_proj[0], input_i16, row, 0, n, fixed_ref);
        int32_t row_fixed = fixed_ref[0];
        gemv_hw_load_input(&hw, input_i16, n);
        if (gemv_hw_run_packet(&hw, packet, packet_bytes, (uint32_t)n, fpga_i32)) {
            fprintf(stderr, "probe repair FPGA run failed row=%d\n", row);
            goto done;
        }
        int good = 0;
        for (int lane = 0; lane < LANES; ++lane) {
            if (fpga_i32[lane] == row_fixed) good++;
        }
        printf("probe_repair_all_lanes: %d,%" PRId32 ",%d,", row, row_fixed, good);
        for (int lane = 0; lane < LANES; ++lane) {
            printf("%s%" PRId32, lane ? " " : "", fpga_i32[lane]);
        }
        printf("\n");
    }

    printf("probe_sparse_single_header: row,lane,fixed_i32,orig_lane_i32,lane0_i32,orig_lane_ok,lane0_ok\n");
    for (int ri = 0; ri < repair_count; ++ri) {
        int row = repair_rows[ri];
        int lane = row % LANES;
        int row_map[LANES];
        int32_t orig_out[LANES];
        int32_t lane0_out[LANES];
        for (int i = 0; i < LANES; ++i) row_map[i] = -1;
        row_map[lane] = row;
        if (build_packet_from_tensor_row_map(m, m->q_proj[0], row_map, 0, n,
                                             packet, &packet_bytes)) {
            fprintf(stderr, "probe sparse original-lane packet build failed row=%d\n", row);
            goto done;
        }
        q8_fixed_ref_i32(m, m->q_proj[0], input_i16, row, 0, n, fixed_ref);
        int32_t row_fixed = fixed_ref[0];
        gemv_hw_load_input(&hw, input_i16, n);
        if (gemv_hw_run_packet(&hw, packet, packet_bytes, (uint32_t)n, orig_out)) {
            fprintf(stderr, "probe sparse original-lane FPGA run failed row=%d\n", row);
            goto done;
        }
        for (int i = 0; i < LANES; ++i) row_map[i] = -1;
        row_map[0] = row;
        if (build_packet_from_tensor_row_map(m, m->q_proj[0], row_map, 0, n,
                                             packet, &packet_bytes)) {
            fprintf(stderr, "probe sparse lane0 packet build failed row=%d\n", row);
            goto done;
        }
        gemv_hw_load_input(&hw, input_i16, n);
        if (gemv_hw_run_packet(&hw, packet, packet_bytes, (uint32_t)n, lane0_out)) {
            fprintf(stderr, "probe sparse lane0 FPGA run failed row=%d\n", row);
            goto done;
        }
        printf("probe_sparse_single: %d,%d,%" PRId32 ",%" PRId32 ",%" PRId32 ",%d,%d\n",
               row, lane, row_fixed, orig_out[lane], lane0_out[0],
               orig_out[lane] == row_fixed, lane0_out[0] == row_fixed);
    }

    printf("probe_sparse_group_header: mode,count,good,total,outputs\n");
    for (int mode = 0; mode < 2; ++mode) {
        for (int count = 2; count <= repair_count; count *= 2) {
            int row_map[LANES];
            int expected[LANES];
            int expected_count = 0;
            int32_t group_out[LANES];
            for (int i = 0; i < LANES; ++i) row_map[i] = -1;
            if (mode == 0) {
                for (int i = 0; i < count; ++i) {
                    int row = repair_rows[i];
                    int lane = row % LANES;
                    row_map[lane] = row;
                    expected[expected_count++] = lane;
                }
            } else {
                for (int i = 0; i < count; ++i) {
                    row_map[i] = repair_rows[i];
                    expected[expected_count++] = i;
                }
            }
            if (build_packet_from_tensor_row_map(m, m->q_proj[0], row_map, 0, n,
                                                 packet, &packet_bytes)) {
                fprintf(stderr, "probe sparse group packet build failed mode=%d count=%d\n",
                        mode, count);
                goto done;
            }
            gemv_hw_load_input(&hw, input_i16, n);
            if (gemv_hw_run_packet(&hw, packet, packet_bytes, (uint32_t)n, group_out)) {
                fprintf(stderr, "probe sparse group FPGA run failed mode=%d count=%d\n",
                        mode, count);
                goto done;
            }
            int good = 0;
            for (int ei = 0; ei < expected_count; ++ei) {
                int lane = expected[ei];
                int row = row_map[lane];
                q8_fixed_ref_i32(m, m->q_proj[0], input_i16, row, 0, n, fixed_ref);
                if (group_out[lane] == fixed_ref[0]) good++;
            }
            printf("probe_sparse_group: %s,%d,%d,%d,",
                   mode == 0 ? "original_lanes" : "compact_lanes",
                   count, good, expected_count);
            for (int lane = 0; lane < LANES; ++lane) {
                printf("%s%" PRId32, lane ? " " : "", group_out[lane]);
            }
            printf("\n");
        }
    }
    rc = 0;

done:
    if (hw.opened) gemv_hw_close(&hw);
    free(prompt_text);
    free(ids.data);
    free(x);
    free(xb);
    free(cpu_out);
    free(fixed_float);
    free(fpga_float);
    free(input_i16);
    free(packet);
    return rc;
}

static void print_gemv_role_breakdown(const counters_t *c)
{
    printf("gemv_role_breakdown_header: role,calls,fpga_calls,cpu_fallbacks,fpga_rowgroup_jobs,fpga_chunk_jobs,fpga_repair_jobs,fpga_mode1_blockacc_calls,s2mm_output_bytes,input_saturations,fpga_time_ms,cpu_gemv_time_ms,cpu_scale_accum_ops,cpu_scale_accum_time_ms\n");
    for (int i = 0; i < GEMV_ROLE_COUNT; ++i) {
        const role_counters_t *r = &c->roles[i];
        if (r->calls == 0 && r->fpga_calls == 0 && r->cpu_fallbacks == 0) continue;
        printf("gemv_role_breakdown: %s,%" PRIu64 ",%" PRIu64 ",%" PRIu64
               ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
               ",%" PRIu64 ",%.3f,%.3f,%" PRIu64 ",%.3f\n",
               gemv_role_name(i), r->calls, r->fpga_calls, r->cpu_fallbacks,
               r->fpga_rowgroup_jobs, r->fpga_chunk_jobs, r->fpga_repair_jobs,
               r->fpga_mode1_blockacc_calls, r->s2mm_output_bytes,
               r->input_saturations, (double)r->fpga_ns / 1000000.0,
               (double)r->cpu_gemv_ns / 1000000.0,
               r->cpu_scale_accum_ops,
               (double)r->cpu_scale_accum_ns / 1000000.0);
    }
}

static int run_generation(model_t *m, const options_t *opt, backend_kind_t kind,
                          const char *audit_path, counters_t *out_counters,
                          int *first_token_out, int *max_new_done,
                          double *per_token_ms_out)
{
    gemv_backend_t be;
    if (backend_open(&be, m, kind, opt->require_fpga, opt->act_shift,
                     opt->dump_layer_stats, opt->fpga_repair_mode,
                     opt->fpga_output_mode, audit_path)) {
        fprintf(stderr, "backend open failed\n");
        return -1;
    }

    uint64_t tok0 = now_ns();
    int used_gguf_template = 0;
    char *prompt_text = NULL;
    if (opt->use_chat_template) {
        prompt_text = build_chat_prompt(&m->tokenizer, opt->prompt, &used_gguf_template);
        if (!prompt_text) {
            fprintf(stderr, "chat template requested but GGUF template is unsupported/missing\n");
            backend_close(&be);
            return -1;
        }
    } else {
        prompt_text = strdup(opt->prompt);
    }
    if (!prompt_text) {
        backend_close(&be);
        return -1;
    }
    int_vec_t ids = {0};
    if (tokenizer_encode(&m->tokenizer, prompt_text, &ids) || ids.len <= 0) {
        fprintf(stderr, "tokenizer failed\n");
        free(prompt_text);
        backend_close(&be);
        return -1;
    }
    be.counters.tokenization_ns += now_ns() - tok0;
    printf("raw_prompt: %s\n", opt->prompt);
    printf("chat_template_applied: %s\n", opt->use_chat_template ? "true" : "false");
    printf("chat_template_source: %s\n",
           opt->use_chat_template ? (used_gguf_template ? "gguf_chatml" : "unsupported") : "none");
    printf("prompt_text: %s\n", prompt_text);
    printf("prompt_token_count: %d\n", ids.len);
    printf("prompt_token_ids:");
    for (int i = 0; i < ids.len; ++i) printf(" %d", ids.data[i]);
    printf("\n");
    free(prompt_text);
    if (ids.len + opt->max_new_tokens > opt->ctx_size) {
        fprintf(stderr, "ctx too small for prompt+generation\n");
        backend_close(&be);
        return -1;
    }

    kv_cache_t kv;
    if (kv_alloc(&kv, m, opt->ctx_size)) {
        fprintf(stderr, "KV allocation failed\n");
        backend_close(&be);
        return -1;
    }
    size_t kv_bytes = (size_t)m->n_layer * 2u * (size_t)opt->ctx_size *
                      (size_t)m->n_head_kv * (size_t)(m->n_embd / m->n_head) * sizeof(float);
    printf("backend: %s\n", kind == BACKEND_FPGA ? "fpga" : "cpu");
    printf("act_shift: %d\n", opt->act_shift);
    printf("fpga_output_mode: %s\n", fpga_output_mode_name(opt->fpga_output_mode));
    printf("fpga_repair_mode: %s\n",
           opt->fpga_repair_mode == FPGA_REPAIR_SPARSE ? "sparse" : "duplicate");
    printf("sampling_temperature: %.6g\n", opt->temperature);
    printf("sampling_top_k: %d\n", opt->top_k);
    printf("sampling_top_p: %.6g\n", opt->top_p);
    printf("sampling_mode: %s\n", opt->temperature <= 0.0 ? "greedy" : "temperature");
    printf("ctx-size: %d\n", opt->ctx_size);
    printf("KV cache bytes: %zu\n", kv_bytes);

    float *hidden = (float *)malloc((size_t)m->n_embd * sizeof(float));
    float *logits = (float *)malloc((size_t)m->n_vocab * sizeof(float));
    if (!hidden || !logits) return -1;

    uint64_t gen0 = now_ns();
    for (int i = 0; i < ids.len; ++i) {
        if (forward_token(m, &be, &kv, ids.data[i], i, hidden)) {
            fprintf(stderr, "forward failed at prompt token %d\n", i);
            return -1;
        }
    }
    int first = -1;
    int done = 0;
    const char *stop_reason = "max_new_tokens";
    int *generated = (int *)calloc((size_t)opt->max_new_tokens, sizeof(int));
    if (!generated) return -1;
    for (int step = 0; step < opt->max_new_tokens; ++step) {
        if (step == 0 && opt->dump_top_k > 0) {
            dump_float_stats("first_token_lm_head_input_hidden", hidden, m->n_embd);
        }
        if (gemv_backend_run(&be, m->tok_embd, "lm_head_tied_token_embd", -1, hidden, logits)) {
            fprintf(stderr, "lm_head GEMV failed\n");
            return -1;
        }
        if (step == 0 && opt->dump_top_k > 0) {
            dump_float_stats("first_token_logits", logits, m->n_vocab);
            dump_top_k_logits(&m->tokenizer, logits, m->n_vocab, opt->dump_top_k);
        }
        uint64_t s0 = now_ns();
        int next = sample_logits(logits, m->n_vocab, opt->temperature, opt->top_k, opt->top_p);
        be.counters.sampling_ns += now_ns() - s0;
        if (first < 0) first = next;
        generated[done] = next;
        done++;
        if (opt->stop_on_eos &&
            (next == m->tokenizer.eos_id || next == m->tokenizer.im_end_id ||
             next == m->tokenizer.endoftext_id)) {
            stop_reason = "eos";
            break;
        }
        if (step + 1 < opt->max_new_tokens) {
            int pos = ids.len + step;
            if (forward_token(m, &be, &kv, next, pos, hidden)) {
                fprintf(stderr, "forward failed at generated token %d\n", step);
                return -1;
            }
        }
    }
    printf("generated_token_ids:");
    for (int i = 0; i < done; ++i) printf(" %d", generated[i]);
    printf("\n");
    printf("tokens_generated: %d\n", done);
    printf("ctx_used: %d\n", ids.len + done);
    printf("ctx_max: %d\n", opt->ctx_size);
    printf("ctx used / ctx max: %d / %d\n", ids.len + done, opt->ctx_size);
    int repl = 0, skipped = 0;
    char *raw_hex = NULL;
    char *generated_text = tokenizer_decode_ids_ex(&m->tokenizer, generated, done,
                                                   opt->skip_special_tokens,
                                                   &repl, &skipped, &raw_hex);
    printf("generated_text: %s\n", generated_text ? generated_text : "<decode_fail>");
    printf("generated_text_utf8_replacements: %d\n", repl);
    printf("generated_special_tokens_skipped: %d\n", skipped);
    if (raw_hex && done <= 64) printf("generated_byte_hex: %s\n", raw_hex);
    printf("stop_reason: %s\n", stop_reason);
    char *first_text = tokenizer_decode_ids_ex(&m->tokenizer, &first, first >= 0 ? 1 : 0,
                                               opt->skip_special_tokens,
                                               NULL, NULL, NULL);
    printf("first_generated_token_text: %s\n", first_text ? first_text : "<decode_fail>");
    free(raw_hex);
    free(generated_text);
    free(first_text);
    uint64_t gen_ns = now_ns() - gen0;
    double per_tok_ms = done ? (double)gen_ns / 1000000.0 / (double)done : 0.0;

    if (out_counters) *out_counters = be.counters;
    if (first_token_out) *first_token_out = first;
    if (max_new_done) *max_new_done = done;
    if (per_token_ms_out) *per_token_ms_out = per_tok_ms;

    printf("total_gemv_calls: %" PRIu64 "\n", be.counters.total_gemv_calls);
    printf("fpga_gemv_calls: %" PRIu64 "\n", be.counters.fpga_gemv_calls);
    printf("cpu_gemv_fallbacks: %" PRIu64 "\n", be.counters.cpu_gemv_fallbacks);
    printf("AXI DMA path used: %s\n", kind == BACKEND_FPGA ? "yes" : "no");
    printf("input BRAM used: %s\n", kind == BACKEND_FPGA ? "yes" : "no");
    printf("AXI-Lite bulk path used: no\n");
    printf("fpga_repair_jobs: %" PRIu64 "\n", be.counters.fpga_repair_jobs);
    printf("fpga_mode1_blockacc_calls: %" PRIu64 "\n",
           be.counters.fpga_mode1_blockacc_calls);
    printf("s2mm_output_bytes: %" PRIu64 "\n", be.counters.s2mm_output_bytes);
    printf("fpga_saturated_outputs_repaired: %" PRIu64 "\n",
           be.counters.fpga_saturated_outputs_repaired);
    printf("fpga_saturated_outputs_unrepaired: %" PRIu64 "\n",
           be.counters.fpga_saturated_outputs_unrepaired);
    printf("input_saturations: %" PRIu64 "\n", be.counters.input_saturations);
    printf("fpga_time_ms: %.3f\n", (double)be.counters.fpga_ns / 1000000.0);
    printf("cpu_gemv_time_ms: %.3f\n", (double)be.counters.cpu_gemv_ns / 1000000.0);
    printf("cpu_non_gemv_time_ms: %.3f\n", (double)be.counters.cpu_non_gemv_ns / 1000000.0);
    printf("cpu_scale_accum_ops: %" PRIu64 "\n", be.counters.cpu_scale_accum_ops);
    printf("cpu_scale_accum_time_ms: %.3f\n",
           (double)be.counters.cpu_scale_accum_ns / 1000000.0);
    printf("tokenization_time_ms: %.3f\n", (double)be.counters.tokenization_ns / 1000000.0);
    printf("sampling_time_ms: %.3f\n", (double)be.counters.sampling_ns / 1000000.0);
    printf("per_token_latency_ms: %.3f\n", per_tok_ms);
    print_gemv_role_breakdown(&be.counters);

    free(ids.data);
    free(generated);
    free(hidden);
    free(logits);
    kv_free(&kv);
    backend_close(&be);
    return 0;
}

static void usage(const char *argv0)
{
    printf("usage: %s [--model PATH] [--backend cpu|fpga] [--fpga-output-mode mode0|mode1_cpu_scale] [--require-fpga] [--ctx-size N] [--max-new-tokens N] [--prompt TEXT] [--prompt-raw TEXT] [--prompt-chat TEXT] [--chat-template] [--temperature T] [--top-k K] [--top-p P] [--fpga-repair-mode duplicate|sparse] [--interactive] [--decode-ids IDS] [--ignore-eos] [--dump-top-k N] [--dump-layer-stats] [--probe-first-gemv] [--check-packet-equivalence] [--packet-equivalence-csv PATH] [--compare-mode1-blockacc] [--mode1-blockacc-csv PATH] [--compare-mode1-scaled-qproj] [--mode1-scaled-qproj-csv PATH] [--compare-identity-scale] [--identity-scale-csv PATH] [--dump-real-qproj-wrapper-fixture DIR] [--onehot-localization] [--onehot-csv PATH] [--tokenize-only] [--compare-backends]\n", argv0);
}

static const char *default_model_path(void)
{
    if (access("/opt/smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf", R_OK) == 0) {
        return "/opt/smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf";
    }
    if (access("quantized_model/original_gguf/SmolLM2-135M-Instruct-Q8_0.gguf", R_OK) == 0) {
        return "quantized_model/original_gguf/SmolLM2-135M-Instruct-Q8_0.gguf";
    }
    return "SmolLM2-135M-Instruct-Q8_0.gguf";
}

static int parse_options(int argc, char **argv, options_t *opt)
{
    memset(opt, 0, sizeof(*opt));
    opt->model_path = default_model_path();
    opt->backend_name = "cpu";
    opt->prompt = "Hi";
    opt->max_new_tokens = 1;
    opt->ctx_size = 128;
    opt->act_shift = 8;
    opt->skip_special_tokens = 1;
    opt->stop_on_eos = 1;
    opt->temperature = 0.0;
    opt->top_k = 0;
    opt->top_p = 1.0;
    opt->fpga_repair_mode = FPGA_REPAIR_DUPLICATE;
    opt->fpga_output_mode = FPGA_OUTPUT_MODE0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) opt->model_path = argv[++i];
        else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) opt->backend_name = argv[++i];
        else if (strcmp(argv[i], "--fpga-output-mode") == 0 && i + 1 < argc) {
            const char *mode = argv[++i];
            if (strcmp(mode, "mode0") == 0) opt->fpga_output_mode = FPGA_OUTPUT_MODE0;
            else if (strcmp(mode, "mode1_cpu_scale") == 0) {
                opt->fpga_output_mode = FPGA_OUTPUT_MODE1_CPU_SCALE;
            } else {
                fprintf(stderr, "unknown fpga output mode: %s\n", mode);
                return -1;
            }
        }
        else if (strcmp(argv[i], "--require-fpga") == 0) opt->require_fpga = 1;
        else if (strcmp(argv[i], "--ctx-size") == 0 && i + 1 < argc) opt->ctx_size = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-new-tokens") == 0 && i + 1 < argc) opt->max_new_tokens = atoi(argv[++i]);
        else if (strcmp(argv[i], "--prompt") == 0 && i + 1 < argc) {
            opt->prompt = argv[++i];
            opt->use_chat_template = 0;
        }
        else if (strcmp(argv[i], "--prompt-raw") == 0 && i + 1 < argc) {
            opt->prompt = argv[++i];
            opt->use_chat_template = 0;
        }
        else if (strcmp(argv[i], "--prompt-chat") == 0 && i + 1 < argc) {
            opt->prompt = argv[++i];
            opt->use_chat_template = 1;
        }
        else if (strcmp(argv[i], "--chat-template") == 0) opt->use_chat_template = 1;
        else if (strcmp(argv[i], "--decode-ids") == 0 && i + 1 < argc) opt->decode_ids_arg = argv[++i];
        else if (strcmp(argv[i], "--no-skip-special") == 0) opt->skip_special_tokens = 0;
        else if (strcmp(argv[i], "--ignore-eos") == 0) opt->stop_on_eos = 0;
        else if (strcmp(argv[i], "--dump-top-k") == 0 && i + 1 < argc) opt->dump_top_k = atoi(argv[++i]);
        else if (strcmp(argv[i], "--dump-layer-stats") == 0) opt->dump_layer_stats = 1;
        else if (strcmp(argv[i], "--probe-first-gemv") == 0) opt->probe_first_gemv = 1;
        else if (strcmp(argv[i], "--check-packet-equivalence") == 0) opt->check_packet_equivalence = 1;
        else if (strcmp(argv[i], "--packet-equivalence-csv") == 0 && i + 1 < argc) opt->packet_equivalence_csv = argv[++i];
        else if (strcmp(argv[i], "--compare-mode1-blockacc") == 0) opt->compare_mode1_blockacc = 1;
        else if (strcmp(argv[i], "--mode1-blockacc-csv") == 0 && i + 1 < argc) opt->mode1_blockacc_csv = argv[++i];
        else if (strcmp(argv[i], "--compare-mode1-scaled-qproj") == 0) opt->compare_mode1_scaled_qproj = 1;
        else if (strcmp(argv[i], "--mode1-scaled-qproj-csv") == 0 && i + 1 < argc) opt->mode1_scaled_qproj_csv = argv[++i];
        else if (strcmp(argv[i], "--compare-identity-scale") == 0) opt->compare_identity_scale = 1;
        else if (strcmp(argv[i], "--identity-scale-csv") == 0 && i + 1 < argc) opt->identity_scale_csv = argv[++i];
        else if (strcmp(argv[i], "--dump-real-qproj-wrapper-fixture") == 0 && i + 1 < argc) opt->dump_real_qproj_fixture_dir = argv[++i];
        else if (strcmp(argv[i], "--onehot-localization") == 0) opt->onehot_localization = 1;
        else if (strcmp(argv[i], "--onehot-csv") == 0 && i + 1 < argc) opt->onehot_csv = argv[++i];
        else if (strcmp(argv[i], "--tokenize-only") == 0) opt->tokenize_only = 1;
        else if (strcmp(argv[i], "--compare-backends") == 0) opt->compare_backends = 1;
        else if (strcmp(argv[i], "--interactive") == 0) opt->interactive = 1;
        else if (strcmp(argv[i], "--temperature") == 0 && i + 1 < argc) opt->temperature = atof(argv[++i]);
        else if (strcmp(argv[i], "--top-k") == 0 && i + 1 < argc) opt->top_k = atoi(argv[++i]);
        else if (strcmp(argv[i], "--top-p") == 0 && i + 1 < argc) opt->top_p = atof(argv[++i]);
        else if (strcmp(argv[i], "--fpga-repair-mode") == 0 && i + 1 < argc) {
            const char *mode = argv[++i];
            if (strcmp(mode, "duplicate") == 0) opt->fpga_repair_mode = FPGA_REPAIR_DUPLICATE;
            else if (strcmp(mode, "sparse") == 0) opt->fpga_repair_mode = FPGA_REPAIR_SPARSE;
            else {
                fprintf(stderr, "unknown fpga repair mode: %s\n", mode);
                return -1;
            }
        }
        else if (strcmp(argv[i], "--act-shift") == 0 && i + 1 < argc) opt->act_shift = atoi(argv[++i]);
        else if (strcmp(argv[i], "--audit-csv") == 0 && i + 1 < argc) opt->audit_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "unknown/incomplete arg: %s\n", argv[i]);
            return -1;
        }
    }
    if (opt->max_new_tokens <= 0) opt->max_new_tokens = 1;
    if (opt->ctx_size <= 0) opt->ctx_size = 128;
    if (opt->temperature < 0.0) opt->temperature = 0.0;
    if (opt->top_k < 0) opt->top_k = 0;
    if (opt->top_p <= 0.0 || opt->top_p > 1.0) opt->top_p = 1.0;
    return 0;
}

static int parse_id_list(const char *s, int_vec_t *ids)
{
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';') p++;
        if (!*p) break;
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p || v < INT32_MIN || v > INT32_MAX) return -1;
        if (vec_push(ids, (int)v)) return -1;
        p = end;
    }
    return ids->len > 0 ? 0 : -1;
}

static int run_decode_ids(tokenizer_t *tok, const char *arg, int default_skip_special)
{
    int_vec_t ids = {0};
    if (parse_id_list(arg, &ids)) {
        fprintf(stderr, "decode-ids parse failed: %s\n", arg);
        free(ids.data);
        return -1;
    }
    printf("decode_ids:");
    for (int i = 0; i < ids.len; ++i) printf(" %d", ids.data[i]);
    printf("\n");
    printf("decode_default_skip_special: %s\n", default_skip_special ? "true" : "false");

    int repl = 0, skipped = 0;
    char *raw_hex = NULL;
    char *text = tokenizer_decode_ids_ex(tok, ids.data, ids.len, default_skip_special,
                                         &repl, &skipped, &raw_hex);
    printf("decoded_text: %s\n", text ? text : "<decode_fail>");
    printf("decoded_utf8_replacements: %d\n", repl);
    printf("decoded_special_tokens_skipped: %d\n", skipped);
    printf("decoded_byte_hex: %s\n", raw_hex ? raw_hex : "<hex_fail>");
    free(text);
    free(raw_hex);

    repl = 0;
    skipped = 0;
    raw_hex = NULL;
    text = tokenizer_decode_ids_ex(tok, ids.data, ids.len, 0, &repl, &skipped, &raw_hex);
    printf("decoded_text_with_specials: %s\n", text ? text : "<decode_fail>");
    printf("decoded_with_specials_utf8_replacements: %d\n", repl);
    printf("decoded_with_specials_byte_hex: %s\n", raw_hex ? raw_hex : "<hex_fail>");
    free(text);
    free(raw_hex);
    free(ids.data);
    return 0;
}

static void counters_accumulate(counters_t *dst, const counters_t *src)
{
    dst->total_gemv_calls += src->total_gemv_calls;
    dst->fpga_gemv_calls += src->fpga_gemv_calls;
    dst->cpu_gemv_fallbacks += src->cpu_gemv_fallbacks;
    dst->fpga_rowgroup_jobs += src->fpga_rowgroup_jobs;
    dst->fpga_chunk_jobs += src->fpga_chunk_jobs;
    dst->fpga_repair_jobs += src->fpga_repair_jobs;
    dst->fpga_mode1_blockacc_calls += src->fpga_mode1_blockacc_calls;
    dst->fpga_saturated_outputs_repaired += src->fpga_saturated_outputs_repaired;
    dst->fpga_saturated_outputs_unrepaired += src->fpga_saturated_outputs_unrepaired;
    dst->s2mm_output_bytes += src->s2mm_output_bytes;
    dst->input_saturations += src->input_saturations;
    dst->fpga_ns += src->fpga_ns;
    dst->cpu_gemv_ns += src->cpu_gemv_ns;
    dst->cpu_non_gemv_ns += src->cpu_non_gemv_ns;
    dst->cpu_scale_accum_ops += src->cpu_scale_accum_ops;
    dst->cpu_scale_accum_ns += src->cpu_scale_accum_ns;
    dst->tokenization_ns += src->tokenization_ns;
    dst->sampling_ns += src->sampling_ns;
    for (int i = 0; i < GEMV_ROLE_COUNT; ++i) {
        dst->roles[i].calls += src->roles[i].calls;
        dst->roles[i].fpga_calls += src->roles[i].fpga_calls;
        dst->roles[i].cpu_fallbacks += src->roles[i].cpu_fallbacks;
        dst->roles[i].fpga_rowgroup_jobs += src->roles[i].fpga_rowgroup_jobs;
        dst->roles[i].fpga_chunk_jobs += src->roles[i].fpga_chunk_jobs;
        dst->roles[i].fpga_repair_jobs += src->roles[i].fpga_repair_jobs;
        dst->roles[i].fpga_mode1_blockacc_calls += src->roles[i].fpga_mode1_blockacc_calls;
        dst->roles[i].s2mm_output_bytes += src->roles[i].s2mm_output_bytes;
        dst->roles[i].input_saturations += src->roles[i].input_saturations;
        dst->roles[i].fpga_ns += src->roles[i].fpga_ns;
        dst->roles[i].cpu_gemv_ns += src->roles[i].cpu_gemv_ns;
        dst->roles[i].cpu_scale_accum_ops += src->roles[i].cpu_scale_accum_ops;
        dst->roles[i].cpu_scale_accum_ns += src->roles[i].cpu_scale_accum_ns;
    }
}

static void trim_line_end(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static void print_session_stats(const counters_t *c, int turns)
{
    printf("interactive_turns: %d\n", turns);
    printf("session_total_gemv_calls: %" PRIu64 "\n", c->total_gemv_calls);
    printf("session_fpga_gemv_calls: %" PRIu64 "\n", c->fpga_gemv_calls);
    printf("session_cpu_gemv_fallbacks: %" PRIu64 "\n", c->cpu_gemv_fallbacks);
    printf("session_fpga_repair_jobs: %" PRIu64 "\n", c->fpga_repair_jobs);
    printf("session_fpga_mode1_blockacc_calls: %" PRIu64 "\n",
           c->fpga_mode1_blockacc_calls);
    printf("session_s2mm_output_bytes: %" PRIu64 "\n", c->s2mm_output_bytes);
    printf("session_input_saturations: %" PRIu64 "\n", c->input_saturations);
    printf("session_fpga_time_ms: %.3f\n", (double)c->fpga_ns / 1000000.0);
    printf("session_cpu_gemv_time_ms: %.3f\n", (double)c->cpu_gemv_ns / 1000000.0);
    printf("session_cpu_non_gemv_time_ms: %.3f\n", (double)c->cpu_non_gemv_ns / 1000000.0);
    printf("session_cpu_scale_accum_ops: %" PRIu64 "\n", c->cpu_scale_accum_ops);
    printf("session_cpu_scale_accum_time_ms: %.3f\n",
           (double)c->cpu_scale_accum_ns / 1000000.0);
    printf("session_tokenization_time_ms: %.3f\n", (double)c->tokenization_ns / 1000000.0);
    printf("session_sampling_time_ms: %.3f\n", (double)c->sampling_ns / 1000000.0);
    print_gemv_role_breakdown(c);
}

static int run_interactive(model_t *model, const options_t *base_opt)
{
    backend_kind_t kind = strcmp(base_opt->backend_name, "fpga") == 0 ? BACKEND_FPGA : BACKEND_CPU;
    counters_t session = {0};
    int turns = 0;
    char line[4096];

    printf("interactive_mode: true\n");
    printf("interactive_backend: %s\n", kind == BACKEND_FPGA ? "fpga" : "cpu");
    printf("interactive_prompt_mode: %s\n", base_opt->use_chat_template ? "chat" : "raw");
    printf("interactive_commands: /reset /stats /quit\n");
    printf("interactive_ctx_size: %d\n", base_opt->ctx_size);
    printf("interactive_max_new_tokens: %d\n", base_opt->max_new_tokens);
    printf("interactive_act_shift: %d\n", base_opt->act_shift);
    printf("interactive_fpga_output_mode: %s\n",
           fpga_output_mode_name(base_opt->fpga_output_mode));
    printf("interactive_fpga_repair_mode: %s\n",
           base_opt->fpga_repair_mode == FPGA_REPAIR_SPARSE ? "sparse" : "duplicate");

    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("interactive_eof: true\n");
            break;
        }
        trim_line_end(line);
        if (line[0] == '\0') continue;
        if (strcmp(line, "/quit") == 0) {
            printf("interactive_quit: ok\n");
            break;
        }
        if (strcmp(line, "/reset") == 0) {
            memset(&session, 0, sizeof(session));
            turns = 0;
            printf("interactive_reset: ok\n");
            continue;
        }
        if (strcmp(line, "/stats") == 0) {
            print_session_stats(&session, turns);
            continue;
        }

        options_t turn_opt = *base_opt;
        turn_opt.prompt = line;
        counters_t turn = {0};
        int first = -1;
        int done = 0;
        double per_ms = 0.0;
        printf("interactive_turn: %d\n", turns + 1);
        if (run_generation(model, &turn_opt, kind, turn_opt.audit_path,
                           &turn, &first, &done, &per_ms)) {
            fprintf(stderr, "interactive generation failed\n");
            return -1;
        }
        counters_accumulate(&session, &turn);
        turns++;
        printf("interactive_first_token: %d\n", first);
        printf("interactive_tokens_generated: %d\n", done);
        printf("interactive_per_token_latency_ms: %.3f\n", per_ms);
        printf("interactive_turn_complete: ok\n");
    }

    print_session_stats(&session, turns);
    printf("S07 gate: not ready (S06.5 semantic gate pending)\n");
    return 0;
}

int main(int argc, char **argv)
{
    options_t opt;
    if (parse_options(argc, argv, &opt)) {
        usage(argv[0]);
        return 2;
    }
    srand(1);
    model_t model;
    if (model_parse_gguf(&model, opt.model_path)) return 3;
    if (tokenizer_build_hashes(&model.tokenizer)) {
        fprintf(stderr, "tokenizer metadata/hash build failed\n");
        return 3;
    }
    if (model_resolve_tensors(&model)) {
        fprintf(stderr, "model tensor resolve failed\n");
        return 3;
    }
    printf("model path: %s\n", opt.model_path);
    printf("tokenizer status: gguf gpt2-bpe tokens=%d merges=%d bos=%d eos=%d endoftext=%d im_start=%d im_end=%d chat_template=%s\n",
           model.tokenizer.token_count, model.tokenizer.merge_count, model.tokenizer.bos_id,
           model.tokenizer.eos_id, model.tokenizer.endoftext_id,
           model.tokenizer.im_start_id, model.tokenizer.im_end_id,
           model.tokenizer.chat_template ? "yes" : "no");
    printf("metadata: n_layer=%d n_embd=%d n_head=%d n_head_kv=%d vocab_size=%d n_ff=%d rope_dim=%d rope_base=%.1f\n",
           model.n_layer, model.n_embd, model.n_head, model.n_head_kv, model.n_vocab,
           model.n_ff, model.rope_dim, model.rope_base);
    printf("S05.6.3 chunk policy: in<=576 no chunk; in==1536 split 512+512+512\n");
    printf("hardware build changed: no\n");

    if (opt.decode_ids_arg) {
        return run_decode_ids(&model.tokenizer, opt.decode_ids_arg, opt.skip_special_tokens) ? 4 : 0;
    }

    if (opt.tokenize_only) {
        int used_gguf_template = 0;
        char *prompt_text = NULL;
        if (opt.use_chat_template) {
            prompt_text = build_chat_prompt(&model.tokenizer, opt.prompt, &used_gguf_template);
        } else {
            prompt_text = strdup(opt.prompt);
        }
        int_vec_t ids = {0};
        if (!prompt_text || tokenizer_encode(&model.tokenizer, prompt_text, &ids)) {
            fprintf(stderr, "tokenize-only failed\n");
            free(prompt_text);
            return 4;
        }
        printf("tokenizer mode: %s\n", opt.use_chat_template ? "chat_template" : "raw");
        printf("raw_prompt: %s\n", opt.prompt);
        printf("chat_template_applied: %s\n", opt.use_chat_template ? "true" : "false");
        printf("chat_template_source: %s\n",
               opt.use_chat_template ? (used_gguf_template ? "gguf_chatml" : "unsupported") : "none");
        printf("prompt_text: %s\n", prompt_text);
        printf("prompt_token_count: %d\n", ids.len);
        printf("prompt_token_ids:");
        for (int i = 0; i < ids.len; ++i) printf(" %d", ids.data[i]);
        printf("\n");
        printf("BOS policy: add_bos_token=false; no implicit BOS inserted\n");
        printf("EOS policy: chat turns use <|im_end|> id %d; eos_token_id metadata is %d\n",
               model.tokenizer.im_end_id, model.tokenizer.eos_id);
        free(prompt_text);
        free(ids.data);
        return 0;
    }

    if (opt.check_packet_equivalence) {
        return run_packet_equivalence_check(&model, &opt) ? 4 : 0;
    }

    if (opt.compare_mode1_blockacc) {
        return run_mode1_blockacc_compare(&model, &opt) ? 4 : 0;
    }

    if (opt.compare_mode1_scaled_qproj) {
        return run_mode1_scaled_qproj_compare(&model, &opt) ? 4 : 0;
    }

    if (opt.compare_identity_scale) {
        return run_identity_scale_compare(&model, &opt) ? 4 : 0;
    }

    if (opt.dump_real_qproj_fixture_dir) {
        return run_real_qproj_fixture_dump(&model, &opt) ? 4 : 0;
    }

    if (opt.onehot_localization) {
        return run_onehot_localization(&model, &opt) ? 4 : 0;
    }

    if (opt.probe_first_gemv) {
        return run_first_gemv_probe(&model, &opt) ? 4 : 0;
    }

    if (opt.interactive) {
        return run_interactive(&model, &opt) ? 4 : 0;
    }

    if (opt.compare_backends) {
        counters_t cpu = {0}, fpga = {0};
        int tok_cpu = -1, tok_fpga = -1, done_cpu = 0, done_fpga = 0;
        double ms_cpu = 0.0, ms_fpga = 0.0;
        printf("[compare] backend=cpu\n");
        if (run_generation(&model, &opt, BACKEND_CPU, NULL, &cpu, &tok_cpu, &done_cpu, &ms_cpu)) return 4;
        printf("[compare] backend=fpga\n");
        if (run_generation(&model, &opt, BACKEND_FPGA, opt.audit_path, &fpga, &tok_fpga, &done_fpga, &ms_fpga)) return 5;
        printf("compare_cpu_first_token: %d\n", tok_cpu);
        printf("compare_fpga_first_token: %d\n", tok_fpga);
        printf("compare_cpu_per_token_ms: %.3f\n", ms_cpu);
        printf("compare_fpga_per_token_ms: %.3f\n", ms_fpga);
        printf("CPU vs HW comparison available: yes\n");
        return 0;
    }

    backend_kind_t kind = strcmp(opt.backend_name, "fpga") == 0 ? BACKEND_FPGA : BACKEND_CPU;
    counters_t counters;
    int first = -1, done = 0;
    double per_ms = 0.0;
    int rc = run_generation(&model, &opt, kind, opt.audit_path, &counters, &first, &done, &per_ms);
    if (rc) return 4;
    printf("first generated token/text: %d\n", first);
    printf("max-new-tokens achieved: %d\n", done);
    printf("CPU vs HW comparison available: %s\n", opt.compare_backends ? "yes" : "no");
    if (kind == BACKEND_FPGA && opt.fpga_output_mode == FPGA_OUTPUT_MODE1_CPU_SCALE &&
        counters.cpu_gemv_fallbacks == 0 && counters.fpga_repair_jobs == 0 && done > 0) {
        printf("S07 gate: ready (mode1_cpu_scale functional demo gate passed)\n");
    } else {
        printf("S07 gate: not ready (S06.5 semantic gate pending)\n");
    }
    return 0;
}

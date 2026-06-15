#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define BASE_WORD ((uint64_t)0x100000000ULL)
#define MAX_BYTES (8ULL * 1024ULL * 1024ULL * 1024ULL)
#define MAX_BITS (MAX_BYTES * 8ULL)
#define MAX_WORDS (MAX_BYTES / sizeof(uint32_t))

typedef struct {
    uint32_t *data;
    size_t len;
    size_t cap;
} BigInt;

static size_t bi_bit_count(const BigInt *a);

static void bi_init(BigInt *a) {
    a->data = NULL;
    a->len = 0;
    a->cap = 0;
}

static void bi_clear(BigInt *a) {
    free(a->data);
    a->data = NULL;
    a->len = 0;
    a->cap = 0;
}

static bool bi_grow(BigInt *a, size_t mincap) {
    if (mincap <= a->cap) return true;
    if (mincap > MAX_WORDS) return false;
    size_t newcap = a->cap ? a->cap * 2 : 4;
    if (newcap < mincap) newcap = mincap;
    if (newcap > MAX_WORDS) newcap = MAX_WORDS;
    uint32_t *data = realloc(a->data, newcap * sizeof(uint32_t));
    if (!data) return false;
    memset(data + a->cap, 0, (newcap - a->cap) * sizeof(uint32_t));
    a->data = data;
    a->cap = newcap;
    return true;
}

static void bi_normalize(BigInt *a) {
    while (a->len > 0 && a->data[a->len - 1] == 0) {
        a->len--;
    }
}

static bool memory_exceeded(const BigInt *a) {
    size_t bits = bi_bit_count(a);
    size_t bytes = (bits + 7) / 8;
    return bytes > MAX_BYTES;
}

static void bi_set_ui(BigInt *a, uint32_t v) {
    if (!bi_grow(a, 1)) return;
    a->data[0] = v;
    a->len = (v == 0) ? 0 : 1;
}

static bool bi_copy(BigInt *dest, const BigInt *src) {
    if (!bi_grow(dest, src->len)) return false;
    memcpy(dest->data, src->data, src->len * sizeof(uint32_t));
    dest->len = src->len;
    return true;
}

static bool bi_is_zero(const BigInt *a) {
    return a->len == 0;
}

static bool bi_is_one(const BigInt *a) {
    return a->len == 1 && a->data[0] == 1;
}

static int bi_cmp_ui(const BigInt *a, uint32_t v) {
    if (a->len == 0) return v == 0 ? 0 : -1;
    if (a->len > 1) return 1;
    if (a->data[0] < v) return -1;
    if (a->data[0] > v) return 1;
    return 0;
}

static int bi_cmp(const BigInt *a, const BigInt *b) {
    if (a->len < b->len) return -1;
    if (a->len > b->len) return 1;
    for (size_t i = a->len; i > 0; i--) {
        uint32_t av = a->data[i - 1];
        uint32_t bv = b->data[i - 1];
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    return 0;
}

static bool bi_add_ui(BigInt *res, const BigInt *a, uint32_t v) {
    if (bi_cmp_ui(a, UINT32_MAX - v) > 0 && a->len > MAX_WORDS - 1) {
        return false;
    }
    if (!bi_copy(res, a)) return false;

    uint64_t carry = v;
    size_t i = 0;
    while (carry != 0) {
        if (i >= res->len) {
            if (!bi_grow(res, res->len + 1)) return false;
            res->data[res->len++] = 0;
        }
        uint64_t sum = (uint64_t)res->data[i] + carry;
        res->data[i] = (uint32_t)sum;
        carry = sum >> 32;
        i++;
    }
    return true;
}

static bool bi_mul_word(BigInt *res, const BigInt *a, uint32_t w) {
    if (w == 0 || bi_is_zero(a)) {
        bi_set_ui(res, 0);
        return true;
    }
    if (a->len > MAX_WORDS - 1) return false;

    if (!bi_grow(res, a->len + 1)) return false;
    uint64_t carry = 0;
    for (size_t i = 0; i < a->len; i++) {
        uint64_t prod = (uint64_t)a->data[i] * w + carry;
        res->data[i] = (uint32_t)prod;
        carry = prod >> 32;
    }
    if (carry) {
        res->data[a->len] = (uint32_t)carry;
        res->len = a->len + 1;
    } else {
        res->len = a->len;
    }
    return true;
}

static bool bi_mul(BigInt *res, const BigInt *a, const BigInt *b) {
    if (bi_is_zero(a) || bi_is_zero(b)) {
        bi_set_ui(res, 0);
        return true;
    }
    if (a->len + b->len > MAX_WORDS) return false;
    if (!bi_grow(res, a->len + b->len)) return false;

    size_t n = a->len + b->len;
    for (size_t i = 0; i < n; i++) {
        res->data[i] = 0;
    }

    for (size_t i = 0; i < a->len; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b->len; j++) {
            uint64_t sum = (uint64_t)a->data[i] * b->data[j];
            sum += res->data[i + j];
            sum += carry;
            res->data[i + j] = (uint32_t)sum;
            carry = sum >> 32;
        }
        res->data[i + b->len] = (uint32_t)carry;
    }

    res->len = n;
    bi_normalize(res);
    return true;
}

static bool bi_add(BigInt *res, const BigInt *a, const BigInt *b) {
    const BigInt *longer = (a->len >= b->len) ? a : b;
    const BigInt *shorter = (a->len < b->len) ? a : b;
    if (!bi_grow(res, longer->len + 1)) return false;

    uint64_t carry = 0;
    size_t i = 0;
    for (; i < shorter->len; i++) {
        uint64_t sum = carry + longer->data[i] + shorter->data[i];
        res->data[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    for (; i < longer->len; i++) {
        uint64_t sum = carry + longer->data[i];
        res->data[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    if (carry) {
        res->data[i++] = (uint32_t)carry;
    }
    res->len = i;
    bi_normalize(res);
    return true;
}

static bool bi_sub_ui(BigInt *res, const BigInt *a, uint32_t v) {
    if (bi_is_zero(a) && v == 0) {
        bi_set_ui(res, 0);
        return true;
    }
    if (bi_cmp_ui(a, v) < 0) return false;
    if (!bi_copy(res, a)) return false;

    uint64_t borrow = v;
    size_t i = 0;
    while (borrow != 0) {
        uint64_t cur = (uint64_t)res->data[i];
        if (cur >= borrow) {
            res->data[i] = (uint32_t)(cur - borrow);
            borrow = 0;
        } else {
            res->data[i] = (uint32_t)(BASE_WORD + cur - borrow);
            borrow = 1;
        }
        i++;
    }
    bi_normalize(res);
    return true;
}

static bool bi_div2(BigInt *res, const BigInt *a) {
    if (!bi_copy(res, a)) return false;
    uint64_t carry = 0;
    for (size_t i = res->len; i > 0; i--) {
        size_t idx = i - 1;
        uint64_t cur = ((uint64_t)carry << 32) | res->data[idx];
        res->data[idx] = (uint32_t)(cur >> 1);
        carry = cur & 1;
    }
    bi_normalize(res);
    return true;
}

static bool bi_is_odd(const BigInt *a) {
    return a->len > 0 && (a->data[0] & 1u) != 0;
}

static size_t bi_bit_count(const BigInt *a) {
    if (a->len == 0) return 0;
    uint32_t top = a->data[a->len - 1];
    return 32 * (a->len - 1) + (32 - __builtin_clz(top));
}

static bool bi_div_mod_word(BigInt *q, const BigInt *a, uint32_t w,
                            uint32_t *rem) {
    if (w == 0) return false;
    if (!bi_copy(q, a)) return false;
    uint64_t carry = 0;
    for (size_t i = q->len; i > 0; i--) {
        size_t idx = i - 1;
        uint64_t cur = (carry << 32) | q->data[idx];
        q->data[idx] = (uint32_t)(cur / w);
        carry = cur % w;
    }
    bi_normalize(q);
    *rem = (uint32_t)carry;
    return true;
}

static bool bi_set_str(BigInt *a, const char *str) {
    bi_set_ui(a, 0);
    for (const char *p = str; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        uint32_t digit = (uint32_t)(*p - '0');
        BigInt tmp;
        bi_init(&tmp);
        if (!bi_mul_word(&tmp, a, 10)) {
            bi_clear(&tmp);
            return false;
        }
        if (!bi_add_ui(a, &tmp, digit)) {
            bi_clear(&tmp);
            return false;
        }
        bi_clear(&tmp);
    }
    return true;
}

static bool bi_to_string(const BigInt *a, char **out) {
    if (bi_is_zero(a)) {
        *out = strdup("0");
        return *out != NULL;
    }

    BigInt temp;
    BigInt q;
    bi_init(&temp);
    bi_init(&q);
    if (!bi_copy(&temp, a)) {
        bi_clear(&temp);
        bi_clear(&q);
        return false;
    }

    const uint32_t base10 = 1000000000u;
    size_t parts_cap = 16;
    size_t parts_len = 0;
    uint32_t *parts = malloc(parts_cap * sizeof(uint32_t));
    if (!parts) {
        bi_clear(&temp);
        bi_clear(&q);
        return false;
    }

    while (!bi_is_zero(&temp)) {
        uint32_t rem;
        if (!bi_div_mod_word(&q, &temp, base10, &rem)) {
            free(parts);
            bi_clear(&temp);
            bi_clear(&q);
            return false;
        }
        if (parts_len >= parts_cap) {
            size_t newcap = parts_cap * 2;
            uint32_t *more = realloc(parts, newcap * sizeof(uint32_t));
            if (!more) {
                free(parts);
                bi_clear(&temp);
                bi_clear(&q);
                return false;
            }
            parts = more;
            parts_cap = newcap;
        }
        parts[parts_len++] = rem;
        BigInt swap = temp;
        temp = q;
        q = swap;
    }

    size_t bufsize = 32 + parts_len * 9;
    char *buf = malloc(bufsize);
    if (!buf) {
        free(parts);
        bi_clear(&temp);
        bi_clear(&q);
        return false;
    }

    char *ptr = buf;
    int written = snprintf(ptr, bufsize, "%u", parts[parts_len - 1]);
    ptr += written;
    for (size_t i = parts_len - 1; i > 0; i--) {
        written = snprintf(ptr, bufsize - (ptr - buf), "%09u", parts[i - 1]);
        ptr += written;
    }
    *out = buf;
    free(parts);
    bi_clear(&temp);
    bi_clear(&q);
    return true;
}

static bool pow_would_exceed_bytes(const BigInt *base, const BigInt *exp) {
    if (bi_is_zero(exp)) return false;
    if (bi_is_zero(base)) return false;
    if (bi_is_one(base)) return false;

    size_t base_bits = bi_bit_count(base);
    if (base_bits <= 1) return false;
    if (bi_bit_count(exp) > 36) return true;

    uint64_t exp_val = 0;
    for (size_t i = exp->len; i > 0; i--) {
        exp_val = (exp_val << 32) | exp->data[i - 1];
    }
    if (exp_val == 0) return false;
    __uint128_t bits = (__uint128_t)exp_val * (base_bits - 1);
    return bits > (__uint128_t)MAX_BITS;
}

static bool big_pow(BigInt *result, const BigInt *base, const BigInt *exp,
                    bool *overflow) {
    if (*overflow) {
        bi_set_ui(result, 0);
        return true;
    }
    if (bi_is_zero(exp)) {
        bi_set_ui(result, 1);
        return true;
    }
    if (bi_is_zero(base)) {
        bi_set_ui(result, 0);
        return true;
    }
    if (bi_is_one(base)) {
        bi_set_ui(result, 1);
        return true;
    }
    if (pow_would_exceed_bytes(base, exp)) {
        *overflow = true;
        bi_set_ui(result, 0);
        return true;
    }

    BigInt base_copy;
    BigInt exp_copy;
    BigInt temp;
    bi_init(&base_copy);
    bi_init(&exp_copy);
    bi_init(&temp);
    if (!bi_copy(&base_copy, base) || !bi_copy(&exp_copy, exp)) {
        bi_clear(&base_copy);
        bi_clear(&exp_copy);
        bi_clear(&temp);
        return false;
    }
    bi_set_ui(result, 1);

    while (!bi_is_zero(&exp_copy)) {
        if (bi_is_odd(&exp_copy)) {
            if (!bi_mul(&temp, result, &base_copy)) {
                *overflow = true;
                break;
            }
            BigInt swap = *result;
            *result = temp;
            temp = swap;
            if (memory_exceeded(result)) {
                *overflow = true;
                break;
            }
        }

        if (!bi_is_zero(&exp_copy) && bi_cmp_ui(&exp_copy, 1) > 0) {
            if (!bi_mul(&temp, &base_copy, &base_copy)) {
                *overflow = true;
                break;
            }
            BigInt swap = base_copy;
            base_copy = temp;
            temp = swap;
            if (memory_exceeded(&base_copy)) {
                *overflow = true;
                break;
            }
        }

        if (!bi_div2(&exp_copy, &exp_copy)) {
            *overflow = true;
            break;
        }
    }

    bi_clear(&base_copy);
    bi_clear(&exp_copy);
    bi_clear(&temp);
    return true;
}

static bool knuth_arrow(BigInt *result, const BigInt *base,
                        unsigned int arrows, const BigInt *b,
                        bool *overflow) {
    if (*overflow) {
        bi_set_ui(result, 0);
        return true;
    }
    if (arrows == 1) {
        return big_pow(result, base, b, overflow);
    }
    if (bi_is_zero(b)) {
        bi_set_ui(result, 1);
        return true;
    }

    BigInt b_minus_one;
    BigInt inner_result;
    bi_init(&b_minus_one);
    bi_init(&inner_result);
    if (!bi_copy(&b_minus_one, b)) {
        bi_clear(&b_minus_one);
        bi_clear(&inner_result);
        return false;
    }
    if (!bi_sub_ui(&b_minus_one, &b_minus_one, 1)) {
        *overflow = true;
        bi_clear(&b_minus_one);
        bi_clear(&inner_result);
        return false;
    }

    if (!knuth_arrow(&inner_result, base, arrows, &b_minus_one, overflow)) {
        bi_clear(&b_minus_one);
        bi_clear(&inner_result);
        return false;
    }
    if (*overflow) {
        bi_set_ui(result, 0);
        bi_clear(&b_minus_one);
        bi_clear(&inner_result);
        return true;
    }

    bool ok = knuth_arrow(result, base, arrows - 1, &inner_result, overflow);
    bi_clear(&b_minus_one);
    bi_clear(&inner_result);
    return ok;
}

int main(void) {
    BigInt base;
    BigInt b;
    BigInt result;
    bi_init(&base);
    bi_init(&b);
    bi_init(&result);

    bool overflow;
    unsigned long arrows;
    char base_str[1024];
    char b_str[1024];

    printf("--- Calculadora de Setas de Knuth (até 8GB) ---\n");
    printf("Use base arrows b, por exemplo: 2 2 5\n");
    printf("Digite 0 para base sair.\n\n");

    while (true) {
        printf("Digite base, arrows e b: ");
        if (scanf("%1023s %lu %1023s", base_str, &arrows, b_str) != 3) {
            break;
        }

        if (!bi_set_str(&base, base_str)) {
            fprintf(stderr, "Valor de base inválido.\n");
            continue;
        }
        if (bi_is_zero(&base)) {
            break;
        }
        if (!bi_set_str(&b, b_str)) {
            fprintf(stderr, "Valor de b inválido.\n");
            continue;
        }
        if (arrows == 0) {
            fprintf(stderr, "Arrows deve ser >= 1.\n");
            continue;
        }

        overflow = false;
        if (!knuth_arrow(&result, &base, arrows, &b, &overflow)) {
            fprintf(stderr, "Erro interno ao calcular.\n");
            break;
        }

        if (overflow) {
            printf("[!] Limite ultrapassado: o cálculo exige mais que 8GB de memória.\n");
        } else {
            char *text;
            if (bi_to_string(&result, &text)) {
                printf("[+] Resultado: %s\n", text);
                free(text);
            } else {
                fprintf(stderr, "Erro ao gerar saída.\n");
            }
        }
    }

    bi_clear(&base);
    bi_clear(&b);
    bi_clear(&result);
    return 0;
}

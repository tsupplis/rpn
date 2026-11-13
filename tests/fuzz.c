/*
 * fuzz.c - robustness run for the library, meant for the sanitizer build.
 *
 * Every script given on the command line is used as a seed.  Each seed is
 * mutated many times (flipped, inserted and deleted bytes, cut and
 * duplicated pieces, braces and continuation characters thrown in) and
 * run on a fresh calculator.  A fixed set of purely random inputs is run
 * too.  The generator is deterministic, so a failure can be repeated.
 *
 * Checked on every run:
 *   - no crash and no sanitizer report (the build does that);
 *   - the stack never exceeds its maximum;
 *   - every byte the library allocated is released again.
 *
 * Runs are bounded: the step and call limits are small, and the text
 * "limit" is broken in every input so that a mutated script cannot raise
 * them.
 *
 * usage: fuzz [-n rounds] file...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rpn.h"

#define MAXTEXT 8192
#define STACK_MAX 40

static unsigned long seed_state = 12345UL;
static long allocs, frees;
static long failures;
static long runs;

static unsigned long rnd(unsigned long n)
{
    seed_state = seed_state * 1103515245UL + 12345UL;
    return ((seed_state >> 8) & 0xFFFFFFUL) % n;
}

static void *f_alloc(void *u, size_t n)
{
    (void)u;
    allocs++;
    return malloc(n);
}

static void f_release(void *u, void *p)
{
    (void)u;
    frees++;
    free(p);
}

static void f_out(void *u, const char *t)
{
    (void)u;
    (void)t;
}

static void f_error(void *u, int code, const char *m)
{
    (void)u;
    (void)code;
    (void)m;
}

/* Breaks every "limit" so that steplimit and calllimit cannot be used. */
static void defuse(char *s, size_t n)
{
    size_t i;

    for (i = 0; i + 5 <= n; i++) {
        if (memcmp(s + i, "limit", 5) == 0) {
            s[i + 4] = 'x';
        }
    }
}

static void run_one(const char *text, size_t n)
{
    rpn_env_t env;
    rpn_ctx_t *c;
    char buf[MAXTEXT + 1];
    long a0 = allocs, f0 = frees;

    memcpy(buf, text, n);
    buf[n] = '\0';
    defuse(buf, n);
    env.user = NULL;
    env.alloc = f_alloc;
    env.release = f_release;
    env.out = f_out;
    env.error = f_error;
    c = rpn_new(&env);
    if (c == NULL) {
        return;
    }
    rpn_set_stack_max(c, STACK_MAX);
    rpn_exec(c, "2000 steplimit 20 calllimit");
    rpn_exec(c, buf);
    if (rpn_depth(c) > STACK_MAX) {
        printf("FAIL: stack deeper than its maximum\n");
        failures++;
    }
    rpn_free(c);
    if (allocs - a0 != frees - f0) {
        printf("FAIL: %ld allocations, %ld releases\n", allocs - a0,
               frees - f0);
        failures++;
    }
    runs++;
}

static const char interesting[] = "{}_# \n\t(),-+.e0123456789abcxyz?<>=!*/";

/* Copies seed into out and applies a few random mutations. */
static size_t mutate(const char *seed, size_t n, char *out)
{
    size_t len = n;
    int rounds = 1 + (int)rnd(4);
    size_t i, j, k;

    memcpy(out, seed, n);
    while (rounds-- > 0 && len > 0) {
        i = rnd(len);
        switch (rnd(7)) {
        case 0:                                   /* flip a byte          */
            out[i] = (char)(1 + rnd(255));
            break;
        case 1:                                   /* a significant byte   */
            out[i] = interesting[rnd(sizeof interesting - 1)];
            break;
        case 2:                                   /* delete a byte        */
            memmove(out + i, out + i + 1, len - i - 1);
            len--;
            break;
        case 3:                                   /* insert a byte        */
            if (len < MAXTEXT - 1) {
                memmove(out + i + 1, out + i, len - i);
                out[i] = interesting[rnd(sizeof interesting - 1)];
                len++;
            }
            break;
        case 4:                                   /* cut the tail         */
            len = i + 1;
            break;
        case 5:                                   /* delete a piece       */
            j = i + rnd(40);
            if (j > len) {
                j = len;
            }
            memmove(out + i, out + j, len - j);
            len -= j - i;
            break;
        default:                                  /* duplicate a piece    */
            k = 1 + rnd(60);
            if (i + k > len) {
                k = len - i;
            }
            if (len + k < MAXTEXT - 1) {
                memmove(out + i + k, out + i, len - i);
                memcpy(out + i, out + i + k, k);
                len += k;
            }
            break;
        }
    }
    return len;
}

static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *p;

    if (f == NULL) {
        return NULL;
    }
    p = (char *)malloc(MAXTEXT);
    if (p == NULL) {
        fclose(f);
        return NULL;
    }
    *n = fread(p, 1, MAXTEXT - 1, f);
    fclose(f);
    return p;
}

int main(int argc, char **argv)
{
    char out[MAXTEXT + 1];
    int rounds = 150;
    int i, r;
    size_t n, len;
    char *seed;
    long seeds = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            rounds = atoi(argv[++i]);
            continue;
        }
        seed = slurp(argv[i], &n);
        if (seed == NULL) {
            continue;
        }
        seeds++;
        run_one(seed, n);                         /* the seed itself      */
        for (r = 0; r < rounds; r++) {
            len = mutate(seed, n, out);
            run_one(out, len);
        }
        free(seed);
    }
    /* purely random text, in two alphabets */
    for (r = 0; r < rounds * 20; r++) {
        len = 1 + rnd(300);
        for (n = 0; n < len; n++) {
            out[n] = (r & 1) ? (char)(1 + rnd(255))
                             : interesting[rnd(sizeof interesting - 1)];
        }
        run_one(out, len);
    }
    printf("fuzz: %ld seeds, %ld runs, %ld failures\n", seeds, runs, failures);
    return failures != 0;
}

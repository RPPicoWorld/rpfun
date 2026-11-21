/**
 * @file whetstone_impl.h
 * @brief Whetstone kernel, included once per floating-point type.
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Before including, define:
 *   WHET_REAL   the floating-point type (float or double)
 *   WHET_NAME   the name of the generated function
 *   WHET_SIN, WHET_COS, WHET_ATAN, WHET_SQRT, WHET_EXP, WHET_LOG  the matching math functions
 *
 * The modules and loop weights follow the classic Curnow & Wichmann Whetstone as
 * found in the netlib C version. One pass with loop = 1 is 100 000 Whetstone
 * instructions. Everything is kept in local state so both cores can run it at the
 * same time without disturbing each other.
 */

#ifndef WHET_CAT
#define WHET_CAT2(a, b) a##b
#define WHET_CAT(a, b) WHET_CAT2(a, b)
#endif

static void WHET_CAT(WHET_NAME, _pa)(WHET_REAL *e, WHET_REAL t, WHET_REAL t2) {
    for (int j = 0; j < 6; ++j) {
        e[1] = (e[1] + e[2] + e[3] - e[4]) * t;
        e[2] = (e[1] + e[2] - e[3] + e[4]) * t;
        e[3] = (e[1] - e[2] + e[3] + e[4]) * t;
        e[4] = (-e[1] + e[2] + e[3] + e[4]) / t2;
    }
}

static void __attribute__((noinline)) WHET_CAT(WHET_NAME, _p3)(WHET_REAL x, WHET_REAL y, WHET_REAL *z, WHET_REAL t, WHET_REAL t2) {
    WHET_REAL x1 = x;
    WHET_REAL y1 = y;
    x1 = t * (x1 + y1);
    y1 = t * (x1 + y1);
    *z = (x1 + y1) / t2;
}

static void __attribute__((noinline)) WHET_CAT(WHET_NAME, _p0)(WHET_REAL *e1, int j, int k, int l) {
    e1[j] = e1[k];
    e1[k] = e1[l];
    e1[l] = e1[j];
}

/**
 * @brief Run one Whetstone pass.
 * @param loop Scale factor; one pass is 100 000 * loop Whetstone instructions.
 * @return A checksum of the results so the compiler cannot discard the work.
 */
static WHET_REAL __attribute__((noinline)) WHET_NAME(int loop) {
    const WHET_REAL t = (WHET_REAL)0.499975;
    const WHET_REAL t1 = (WHET_REAL)0.50025;
    const WHET_REAL t2 = (WHET_REAL)2.0;

    const int n2 = 12 * loop;
    const int n3 = 14 * loop;
    const int n4 = 345 * loop;
    const int n6 = 210 * loop;
    const int n7 = 32 * loop;
    const int n8 = 899 * loop;
    const int n9 = 616 * loop;
    const int n11 = 93 * loop;

    WHET_REAL e1[5];
    WHET_REAL x, y, z;
    int j, k, l;

    // Module 2: array elements
    e1[1] = (WHET_REAL)1.0;
    e1[2] = e1[3] = e1[4] = (WHET_REAL)-1.0;
    for (int i = 0; i < n2; ++i) {
        e1[1] = (e1[1] + e1[2] + e1[3] - e1[4]) * t;
        e1[2] = (e1[1] + e1[2] - e1[3] + e1[4]) * t;
        e1[3] = (e1[1] - e1[2] + e1[3] + e1[4]) * t;
        e1[4] = (-e1[1] + e1[2] + e1[3] + e1[4]) * t;
    }

    // Module 3: array as parameter
    for (int i = 0; i < n3; ++i) {
        WHET_CAT(WHET_NAME, _pa)(e1, t, t2);
    }

    // Module 4: conditional jumps
    j = 1;
    for (int i = 0; i < n4; ++i) {
        j = (j == 1) ? 2 : 3;
        j = (j > 2) ? 0 : 1;
        j = (j < 1) ? 1 : 0;
    }

    // Module 6: integer arithmetic
    j = 1;
    k = 2;
    l = 3;
    for (int i = 0; i < n6; ++i) {
        j = j * (k - j) * (l - k);
        k = l * k - (l - j) * k;
        l = (l - k) * (k + j);
        e1[l - 1] = (WHET_REAL)(j + k + l);
        e1[k - 1] = (WHET_REAL)(j * k * l);
    }

    // Module 7: trigonometric functions
    x = y = (WHET_REAL)0.5;
    for (int i = 0; i < n7; ++i) {
        x = t * WHET_ATAN(t2 * WHET_SIN(x) * WHET_COS(x) / (WHET_COS(x + y) + WHET_COS(x - y) - (WHET_REAL)1.0));
        y = t * WHET_ATAN(t2 * WHET_SIN(y) * WHET_COS(y) / (WHET_COS(x + y) + WHET_COS(x - y) - (WHET_REAL)1.0));
    }

    // Module 8: procedure calls
    x = y = z = (WHET_REAL)1.0;
    for (int i = 0; i < n8; ++i) {
        WHET_CAT(WHET_NAME, _p3)(x, y, &z, t, t2);
    }

    // Module 9: array references
    j = 1;
    k = 2;
    l = 3;
    e1[1] = (WHET_REAL)1.0;
    e1[2] = (WHET_REAL)2.0;
    e1[3] = (WHET_REAL)3.0;
    for (int i = 0; i < n9; ++i) {
        WHET_CAT(WHET_NAME, _p0)(e1, j, k, l);
    }

    // Module 11: standard functions
    x = (WHET_REAL)0.75;
    for (int i = 0; i < n11; ++i) {
        x = WHET_SQRT(WHET_EXP(WHET_LOG(x) / t1));
    }

    return e1[1] + e1[2] + e1[3] + e1[4] + x + y + z + (WHET_REAL)(j + k + l);
}

#pragma once

#include <stdint.h>

// number of bits before the decimal place
#define P_MAGIC 17
// number of bits after the decimal place
#define Q_MAGIC 14
// fixed point magic number, defined as f = 2**q
#define F_MAGIC (int)(1 << Q_MAGIC)

// typedef for a fixed point real number
typedef int fp_real;

// convert an integer n to fixed point real number
inline fp_real fp_int_to_real(int n) { return n * F_MAGIC; }
// convert fp real number to an integer (rounding to zero)
inline int fp_real_to_int(fp_real x) { return x / F_MAGIC; }
// convert fp real number to an integer (rounding to nearest)
inline fp_real fp_real_to_int_nearest(fp_real x)
{
    if (x >= 0)
        return (x + F_MAGIC / 2) / F_MAGIC;
    else
        return (x - F_MAGIC / 2) / F_MAGIC;
}
// add a real number x, and integer n
inline fp_real fp_add(fp_real x, int n)
{
    return x + n * F_MAGIC;
}
// subtract an integer n from a real number x
inline fp_real fp_sub(fp_real x, int n)
{
    return x - n * F_MAGIC;
}
// multiply two real numbers
inline fp_real fp_mult(fp_real x, fp_real y)
{
    return ((int64_t)x) * y / F_MAGIC;
}
// divide two real numbers
inline fp_real fp_div(fp_real x, fp_real y)
{
    return ((int64_t)x) * F_MAGIC / y;
}
/*
 * compat.h - the single place for compiler and library quirks.
 *
 * The code base targets ANSI C89 (-ansi -pedantic) and legacy compilers
 * such as Aztec C.  Anything that differs between compilers is isolated
 * here, so the rest of the sources stay clean.
 */
#ifndef COMPAT_H
#define COMPAT_H

#include <float.h>
#include <limits.h>
#include <math.h>

/*
 * C89 has no isnan/isinf/isfinite.  The argument is evaluated several
 * times.  Some libraries (Aztec C) have no real infinity: an overflowing
 * function returns exactly 1.79e308, which is below DBL_MAX.  So every
 * magnitude from RPN_BIG up counts as infinite, on all platforms, to keep
 * the behaviour identical (the lost range, 1.79e308 to 1.797e308, is
 * irrelevant in practice).
 */
#define RPN_BIG 1.79e308
#define RPN_ISNAN(x) ((x) != (x))
#define RPN_ISINF(x) (!RPN_ISNAN(x) && ((x) >= RPN_BIG || (x) <= -RPN_BIG))
#define RPN_FINITE(x) (!RPN_ISNAN(x) && !RPN_ISINF(x))

#endif /* COMPAT_H */

/**
 * libc_compat.c — Compatibility wrappers for IDO compiler built-ins and
 * BSD libc functions used by the decomp.
 *
 * The original N64 code was compiled with the SGI IDO 7.1 compiler, which
 * provides __sinf/__cosf as compiler built-ins and links against a BSD-
 * flavored libc.  On modern toolchains (MSVC, GCC, Clang) these symbols
 * don't exist, so we provide thin wrappers to the standard equivalents.
 */

#include <ssb_types.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <PR/xstdio.h>

/* ========================================================================= */
/*  IDO math built-ins                                                       */
/* ========================================================================= */

/*
 * IDO's __sinf and __cosf are single-precision math built-ins.
 * Declared in include/common.h as: f32 __sinf(f32); f32 __cosf(f32);
 */

f32 __cosf(f32 angle)
{
	return cosf(angle);
}

f32 __sinf(f32 angle)
{
	return sinf(angle);
}

/* ========================================================================= */
/*  BSD libc                                                                 */
/* ========================================================================= */

/*
 * bzero is the BSD equivalent of memset(ptr, 0, len).
 * Used throughout the decomp for clearing memory regions.
 */

void bzero(void *ptr, unsigned int len)
{
	memset(ptr, 0, len);
}

/* ========================================================================= */
/*  IDO printf backend                                                       */
/* ========================================================================= */

/*
 * _Printf is IDO's internal formatted-output engine, used by the decomp's
 * debug printf system (src/sys/debug.c).  The full IDO implementation lives
 * in src/libultra/libc/xprintf.c with dependencies on xlitob, xldtob, etc.
 *
 * Rather than pulling in the full IDO printf chain (which has its own
 * integer/float formatting with non-standard quirks), we provide a minimal
 * wrapper that delegates to vsnprintf.  The outfun callback is called once
 * with the formatted result.
 *
 * Signature from include/PR/xstdio.h:
 *   typedef char* outfun(char*, const char*, size_t);
 *   int _Printf(outfun prout, char* arg, const char* fmt, va_list args);
 */

int _Printf(outfun prout, char *arg, const char *fmt, va_list args)
{
	char buf[512];
	int n = vsnprintf(buf, sizeof(buf), fmt, args);

	if (n > 0)
	{
		if ((size_t)n >= sizeof(buf))
		{
			n = sizeof(buf) - 1;
		}
		prout(arg, buf, (size_t)n);
	}

	return n;
}

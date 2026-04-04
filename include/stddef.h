#ifndef __STDDEF_H__
#define __STDDEF_H__

#include <PR/ultratypes.h>

#ifdef _MSC_VER
/* MSVC: offsetof is a compiler intrinsic */
#ifndef offsetof
#define offsetof(type, member) ((size_t)&(((type*)0)->member))
#endif
#elif !defined(__sgi)
/* GCC/Clang: use built-in offsetof macro */
#define offsetof(type, member) __builtin_offsetof(type, member)
#else
/* IDO: use macro from Indy headers */
#define offsetof(s, m) (size_t)(&(((s*)0)->m))
#endif

#endif /* __STDDEF_H__ */

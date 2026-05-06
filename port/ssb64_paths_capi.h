#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copies UTF-8 app bundle directory plus trailing NUL into out. Returns nonzero on success. */
int ssb64_RealAppBundlePathUtf8(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

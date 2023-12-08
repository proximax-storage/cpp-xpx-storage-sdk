
#ifndef sodium_utils_H
#define sodium_utils_H

#include <cstddef>

#include "plugins.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SODIUM_C99
# if defined(__cplusplus) || !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#  define SODIUM_C99(X)
# else
#  define SODIUM_C99(X) X
# endif
#endif

PLUGIN_API
void sodium_memzero(void * const pnt, const size_t len) __attribute__ ((nonnull));

#ifdef __cplusplus
}
#endif

#endif

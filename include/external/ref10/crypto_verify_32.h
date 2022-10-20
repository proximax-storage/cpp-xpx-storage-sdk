#ifndef crypto_verify_32_H
#define crypto_verify_32_H

#include "plugins.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_verify_32_ref_BYTES 32
PLUGIN_API extern int crypto_verify_32(const unsigned char *,const unsigned char *);

#ifdef __cplusplus
}
#endif

#endif

/*
Implementation by the Keccak Team, namely, Guido Bertoni, Joan Daemen,
Michaël Peeters, Gilles Van Assche and Ronny Van Keer,
hereby denoted as "the implementer".

For more information, feedback or questions, please refer to our website:
https://keccak.team/

To the extent possible under law, the implementer has waived all copyright
and related or neighboring rights to the source code in this file.
http://creativecommons.org/publicdomain/zero/1.0/

---

Please refer to SnP-documentation.h for more details.
*/

#ifndef _KeccakP_1600_SnP_h_
#define _KeccakP_1600_SnP_h_

#include "plugins.h"
#include "brg_endian.h"
#include "KeccakP-1600-opt64-config.h"

#define KeccakP1600_implementation      "generic 64-bit optimized implementation (" KeccakP1600_implementation_config ")"
#define KeccakP1600_stateSizeInBytes    200
#define KeccakP1600_stateAlignment      8
#define KeccakF1600_FastLoop_supported
#define KeccakP1600_12rounds_FastLoop_supported

#include <cstddef>

#define KeccakP1600_StaticInitialize()
PLUGIN_API void KeccakP1600_Initialize(void *state);
#if (PLATFORM_BYTE_ORDER == IS_LITTLE_ENDIAN)
#define KeccakP1600_AddByte(state, byte, offset) \
    ((unsigned char*)(state))[(offset)] ^= (byte)
#else
PLUGIN_API void KeccakP1600_AddByte(void *state, unsigned char data, unsigned int offset);
#endif
PLUGIN_API void KeccakP1600_AddBytes(void *state, const unsigned char *data, unsigned int offset, unsigned int length);
PLUGIN_API void KeccakP1600_OverwriteBytes(void *state, const unsigned char *data, unsigned int offset, unsigned int length);
PLUGIN_API void KeccakP1600_OverwriteWithZeroes(void *state, unsigned int byteCount);
PLUGIN_API void KeccakP1600_Permute_Nrounds(void *state, unsigned int nrounds);
PLUGIN_API void KeccakP1600_Permute_12rounds(void *state);
PLUGIN_API void KeccakP1600_Permute_24rounds(void *state);
PLUGIN_API void KeccakP1600_ExtractBytes(const void *state, unsigned char *data, unsigned int offset, unsigned int length);
PLUGIN_API void KeccakP1600_ExtractAndAddBytes(const void *state, const unsigned char *input, unsigned char *output, unsigned int offset, unsigned int length);
PLUGIN_API size_t KeccakF1600_FastLoop_Absorb(void *state, unsigned int laneCount, const unsigned char *data, size_t dataByteLen);
PLUGIN_API size_t KeccakP1600_12rounds_FastLoop_Absorb(void *state, unsigned int laneCount, const unsigned char *data, size_t dataByteLen);

#endif

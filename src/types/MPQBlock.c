#include "MPQBlock.h"
#include "../util/Crypto.h"
#include <stdlib.h>
#include <string.h>

void MPQBlock_CalculateEncryptionSeed(struct MPQBlock *block, const char *file_name) {
    // The key uses the file name without its folder; names like "(listfile)" have no folder at all.
    const char    *slash   = strrchr(file_name, '\\');
    const char    *name    = slash != NULL ? slash + 1 : file_name;
    const uint32_t seed    = crypto_hash_string(name, 3);
    block->encryption_seed = (seed + block->file_position) ^ block->size_uncompressed;
}

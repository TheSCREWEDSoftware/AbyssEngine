#include "MPQHeader.h"
#include "../common/Logging.h"
#include <stdlib.h>
#include <string.h>

static const char mpq_magic[4] = {'M', 'P', 'Q', 0x1A};

void MPQHeader_Read(FILE *file, const char *mpq_path, MPQHeader *header) {
    fseek(file, 0, SEEK_SET);
    if (fread(header, sizeof(MPQHeader), 1, file) != 1) {
        memset(header, 0, sizeof(MPQHeader)); // too short: fails the magic check below
    }

    for (int i = 0; i < 4; i++) {
        if (header->magic[i] != mpq_magic[i]) {
            LOG_FATAL("Failed to load '%s' due to invalid header.", mpq_path);
        }
    }
}

// The data readers on bytes made here (no game files needed): the tables, DC6 pictures, and then broken and random
// input for every reader and for the network's item messages, which must never crash or read out of bounds (run it
// with -fsanitize=address to be sure).
#include "../src/data/DataTable.h"
#include "../src/net/NetMessage.h"
#include "../src/net/Protocol.h"
#include "../src/types/DC6.h"
#include "../src/types/DCC.h"
#include "../src/types/DS1.h"
#include "../src/types/DT1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition);                                    \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static uint32_t seed = 12345u;
static uint32_t Random(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static void Put32(uint8_t *at, const uint32_t value) {
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

static void TestDataTable(void) {
    const char text[] = "Name\tLevel\tEmpty\r\nzombie\t5\t\nSkeleton\t\t3\n\nlast\t7";
    DataTable *t      = DataTable_FromText(text, (int)strlen(text));
    CHECK(DataTable_GetRowCount(t) == 3);             // (the blank line isn't a row)
    CHECK(DataTable_GetColumnIndex(t, "level") == 1); // (any case)
    CHECK(DataTable_GetColumnIndex(t, "Nope") == -1);
    CHECK(strcmp(DataTable_GetString(t, 0, 0), "zombie") == 0);
    CHECK(DataTable_GetInt(t, 0, 1) == 5);
    CHECK(!DataTable_HasValue(t, 1, 1));
    CHECK(DataTable_GetInt(t, 1, 2) == 3);
    CHECK(strcmp(DataTable_GetString(t, 2, 0), "last") == 0);
    CHECK(strcmp(DataTable_GetString(t, 9, 0), "") == 0); // out of range: empty, not a crash
    CHECK(strcmp(DataTable_GetString(t, 0, 9), "") == 0);
    DataTable_Destroy(&t);
    CHECK(t == NULL);
    t = DataTable_FromText("", 0);
    CHECK(DataTable_GetRowCount(t) == 0);
    DataTable_Destroy(&t);
}

// A DC6 of one 3 x 2 frame: the bottom row 1 2 3, the top row (skip 1) 7 8
static int MakeDc6(uint8_t *out) {
    static const uint8_t pixels[] = {3, 1, 2, 3, 0x80, 0x81, 2, 7, 8, 0x80};
    memset(out, 0, 128);
    Put32(out + 0, 6);           // version
    Put32(out + 4, 1);           // flags
    Put32(out + 8, 0);           // encoding
    Put32(out + 12, 0xEEEEEEEE); // termination
    Put32(out + 16, 1);          // directions
    Put32(out + 20, 1);          // frames per direction
    Put32(out + 24, 28);         // the frame's pointer
    uint8_t *frame = out + 28;
    Put32(frame + 4, 3); // width
    Put32(frame + 8, 2); // height
    Put32(frame + 28, sizeof(pixels));
    memcpy(frame + 32, pixels, sizeof(pixels));
    return 28 + 32 + (int)sizeof(pixels) + 3;
}

static void TestDc6(void) {
    uint8_t   bytes[128];
    const int size = MakeDc6(bytes);
    DC6      *dc6  = DC6_LoadMemory(bytes, (uint32_t)size);
    CHECK(DC6_GetTotalFrameCount(dc6) == 1);
    uint32_t w = 0;
    uint32_t h = 0;
    DC6_GetFrameSize(dc6, 0, &w, &h);
    CHECK(w == 3 && h == 2);
    const uint8_t *p = DC6_GetFramePixelData(dc6, 0);
    CHECK(p[3] == 1 && p[4] == 2 && p[5] == 3); // bottom row
    CHECK(p[0] == 0 && p[1] == 7 && p[2] == 8); // top row
    DC6_Destroy(&dc6);

    // Broken ones: every length cut short, and random bytes changed
    for (int cut = 0; cut < size; cut++) {
        dc6 = DC6_LoadMemory(bytes, (uint32_t)cut);
        DC6_Destroy(&dc6);
    }
    for (int round = 0; round < 3000; round++) {
        uint8_t copy[128];
        memcpy(copy, bytes, sizeof(copy));
        for (int n = 1 + (int)(Random() % 6); n > 0; n--) {
            copy[Random() % (uint32_t)size] = (uint8_t)Random();
        }
        dc6 = DC6_LoadMemory(copy, (uint32_t)size);
        for (uint32_t f = 0; f < DC6_GetTotalFrameCount(dc6); f++) {
            DC6_GetFrameSize(dc6, f, &w, &h);
            (void)DC6_GetFramePixelData(dc6, f);
        }
        DC6_Destroy(&dc6);
    }
}

// Random bytes behind a believable start: the readers must say no or read what they can, never crash
static void FuzzReaders(void) {
    uint8_t *bytes = malloc(4096);
    for (int round = 0; round < 4000; round++) {
        const uint32_t size = Random() % 4096;
        for (uint32_t b = 0; b < size; b++) {
            bytes[b] = (uint8_t)Random();
        }
        switch (round % 4) {
        case 0: // DT1: version 7, 6
            if (size >= 8) {
                Put32(bytes, 7);
                Put32(bytes + 4, 6);
            }
            if (size >= 276) {
                Put32(bytes + 268, Random() % 8);
                Put32(bytes + 272, 276);
            }
            {
                DT1 *dt1 = DT1_LoadMemory(bytes, size);
                DT1_Destroy(&dt1);
            }
            break;
        case 1: // DS1: a version, a small size
            if (size >= 12) {
                Put32(bytes, 3 + (Random() % 16));
                Put32(bytes + 4, Random() % 8);
                Put32(bytes + 8, Random() % 8);
            }
            {
                DS1 *ds1 = DS1_LoadMemory(bytes, size);
                DS1_Destroy(&ds1);
            }
            break;
        case 2: // DCC: its signature, a direction or two
            if (size >= 7) {
                bytes[0] = 0x74;
                bytes[2] = (uint8_t)(1 + (Random() % 2));
                Put32(bytes + 3, 1 + (Random() % 4));
            }
            {
                DCC *dcc = DCC_LoadMemory(bytes, size);
                DCC_Destroy(&dcc);
            }
            break;
        default: { // a table of random text
            DataTable *t = DataTable_FromText((const char *)bytes, (int)size);
            for (int r = 0; r < DataTable_GetRowCount(t) && r < 5; r++) {
                (void)DataTable_GetInt(t, r, 0);
            }
            DataTable_Destroy(&t);
            break;
        }
        }
    }
    free(bytes);
}

// The network's item: written and read back the same; random messages read without trouble
static void FuzzNetItems(void) {
    Item item;
    memset(&item, 0, sizeof(item));
    snprintf(item.code, sizeof(item.code), "hax");
    item.quantity      = 1;
    item.quality       = 2;
    item.mod_count     = 1;
    item.mods[0].type  = 3;
    item.mods[0].value = 12;
    item.sockets       = 2;
    item.grade         = 9;
    item.ethereal      = true;
    snprintf(item.owner, sizeof(item.owner), "Tester");
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetItem_Write(&writer, &item);
    NetReader reader;
    NetReader_Begin(&reader, writer.data, writer.size);
    Item back;
    NetItem_Read(&reader, &back);
    CHECK(!reader.failed);
    CHECK(strcmp(back.code, "hax") == 0 && back.quality == 2 && back.mod_count == 1 && back.mods[0].value == 12);
    CHECK(back.grade == 9 && back.ethereal && strcmp(back.owner, "Tester") == 0);

    uint8_t bytes[600];
    for (int round = 0; round < 20000; round++) {
        const int size = (int)(Random() % sizeof(bytes));
        for (int b = 0; b < size; b++) {
            bytes[b] = (uint8_t)Random();
        }
        if (round % 2 == 0 && size > (int)writer.size) {
            memcpy(bytes, writer.data, writer.size); // a real one, then changed here and there
            bytes[Random() % writer.size] = (uint8_t)Random();
        }
        NetReader_Begin(&reader, bytes, size);
        NetItem_Read(&reader, &back);
        CHECK(back.mod_count <= ITEM_MODS_MAX && back.sockets <= ITEM_SOCKETS_MAX &&
              back.socketed_count <= back.sockets && back.quality < ITEM_QUALITY_COUNT);
        CHECK(memchr(back.code, '\0', sizeof(back.code)) != NULL &&
              memchr(back.owner, '\0', sizeof(back.owner)) != NULL);
    }
}

int main(void) {
    const char *given = getenv("ABYSS_FUZZ_SEED"); // (another seed: other random input)
    seed              = given != NULL && strtoul(given, NULL, 10) != 0 ? (uint32_t)strtoul(given, NULL, 10) : seed;
    TestDataTable();
    TestDc6();
    FuzzReaders();
    FuzzNetItems();
    if (failures > 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    printf("readers: all good\n");
    return 0;
}

#include "DCC.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include "DC6.h"
#include <stdlib.h>
#include <string.h>

// DCC decoding follows the format notes by Paul Siramy (dcc2pcx) and the OpenDiablo2 decoder:
// per direction a set of frame headers, then five bit streams that rebuild the image 4x4 cell by cell, re-using
// cells from the previous frame wherever they did not change.

#define DCC_SIGNATURE     0x74
#define DCC_MAX_CELL_SIZE 8

static const int dcc_bit_widths[16] = {0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 26, 28, 30, 32};

struct DCC {
    int       direction_count;
    int       frames_per_direction;
    DCCFrame *frames; // direction-major
};

// --- Bit stream (LSB first, bounds checked) ----------------------------------------------------------------------

typedef struct DCCBits {
    const uint8_t *data;
    size_t         position; // in bits
    size_t         end;      // in bits
    bool           overflow;
} DCCBits;

static DCCBits DCC__Bits(const uint8_t *data, const size_t start_bit, const size_t end_bit) {
    const DCCBits bits = {.data = data, .position = start_bit, .end = end_bit, .overflow = false};
    return bits;
}

static uint32_t DCC__ReadBits(DCCBits *bits, const int count) {
    if (count == 0) {
        return 0;
    }
    if (bits->position + (size_t)count > bits->end) {
        bits->overflow = true;
        bits->position = bits->end;
        return 0;
    }

    uint32_t result = 0;
    for (int i = 0; i < count; i++) {
        const size_t bit  = bits->position++;
        result           |= (uint32_t)((bits->data[bit >> 3] >> (bit & 7)) & 1) << i;
    }
    return result;
}

static int32_t DCC__ReadSigned(DCCBits *bits, const int count) {
    const uint32_t value = DCC__ReadBits(bits, count);
    if (count == 0 || count == 32) {
        return (int32_t)value;
    }
    const uint32_t sign = 1u << (count - 1);
    return (int32_t)((value ^ sign) - sign);
}

static uint32_t DCC__ReadU32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

// --- Direction decoding ---------------------------------------------------------------------------------------

typedef struct DCCCell {
    int x; // relative to the direction box
    int y;
    int w;
    int h;
} DCCCell;

typedef struct DCCFrameHeader {
    int      xmin;
    int      ymin;
    int      xmax;
    int      ymax;
    int      width;
    int      height;
    int      cells_w;
    int      cells_h;
    int      first_cell_x; // direction cell column/row of the frame's first cell
    int      first_cell_y;
    DCCCell *cells;
} DCCFrameHeader;

typedef struct DCCPixelEntry {
    uint8_t value[4];
    int     frame;
    int     frame_cell;
} DCCPixelEntry;

static int DCC__CountBits(const uint32_t mask) {
    int count = 0;
    for (uint32_t m = mask; m != 0; m >>= 1) {
        count += (int)(m & 1);
    }
    return count;
}

// Splits one axis of a frame into cells aligned to the direction's 4 pixel grid.
static int DCC__SplitAxis(const int offset_in_direction, const int length, int *first_size) {
    const int first = 4 - (offset_in_direction % 4);
    *first_size     = first;

    if (length - first <= 1) {
        *first_size = length;
        return 1;
    }

    const int rest  = length - first - 1;
    int       count = 2 + (rest / 4);
    if (rest % 4 == 0) {
        count--;
    }
    return count;
}

static void DCC__BuildFrameCells(DCCFrameHeader *frame, const int dir_xmin, const int dir_ymin) {
    const int off_x = frame->xmin - dir_xmin;
    const int off_y = frame->ymin - dir_ymin;
    int       first_w;
    int       first_h;

    frame->cells_w      = DCC__SplitAxis(off_x, frame->width, &first_w);
    frame->cells_h      = DCC__SplitAxis(off_y, frame->height, &first_h);
    frame->first_cell_x = off_x / 4;
    frame->first_cell_y = off_y / 4;
    frame->cells        = calloc((size_t)frame->cells_w * frame->cells_h, sizeof(DCCCell));
    FAIL_IF_NULL(frame->cells);

    int y = off_y;
    for (int cy = 0; cy < frame->cells_h; cy++) {
        int h = (cy == 0) ? first_h : 4;
        if (cy == frame->cells_h - 1 && cy > 0) {
            h = frame->height - first_h - (4 * (frame->cells_h - 2));
        }

        int x = off_x;
        for (int cx = 0; cx < frame->cells_w; cx++) {
            int w = (cx == 0) ? first_w : 4;
            if (cx == frame->cells_w - 1 && cx > 0) {
                w = frame->width - first_w - (4 * (frame->cells_w - 2));
            }

            DCCCell *cell  = &frame->cells[(cy * frame->cells_w) + cx];
            cell->x        = x;
            cell->y        = y;
            cell->w        = w;
            cell->h        = h;
            x             += w;
        }
        y += h;
    }
}

static bool DCC__DecodeDirection(DCC *dcc, const int direction, const uint8_t *data, const size_t start,
                                 const size_t end) {
    const int frame_count = dcc->frames_per_direction;
    DCCBits   bits        = DCC__Bits(data, start * 8, end * 8);

    DCC__ReadBits(&bits, 32); // outsize_coded
    const uint32_t compression    = DCC__ReadBits(&bits, 2);
    const int      variable0_bits = dcc_bit_widths[DCC__ReadBits(&bits, 4)];
    const int      width_bits     = dcc_bit_widths[DCC__ReadBits(&bits, 4)];
    const int      height_bits    = dcc_bit_widths[DCC__ReadBits(&bits, 4)];
    const int      xoffset_bits   = dcc_bit_widths[DCC__ReadBits(&bits, 4)];
    const int      yoffset_bits   = dcc_bit_widths[DCC__ReadBits(&bits, 4)];
    const int      optional_bits  = dcc_bit_widths[DCC__ReadBits(&bits, 4)];
    const int      coded_bits     = dcc_bit_widths[DCC__ReadBits(&bits, 4)];

    DCCFrameHeader *frames         = calloc((size_t)frame_count, sizeof(DCCFrameHeader));
    size_t          optional_total = 0;
    FAIL_IF_NULL(frames);

    int dir_xmin = 0x7FFFFFFF, dir_ymin = 0x7FFFFFFF, dir_xmax = -0x7FFFFFFF, dir_ymax = -0x7FFFFFFF;

    for (int f = 0; f < frame_count; f++) {
        DCC__ReadBits(&bits, variable0_bits);
        DCCFrameHeader *frame  = &frames[f];
        frame->width           = (int)DCC__ReadBits(&bits, width_bits);
        frame->height          = (int)DCC__ReadBits(&bits, height_bits);
        const int xoffset      = DCC__ReadSigned(&bits, xoffset_bits);
        const int yoffset      = DCC__ReadSigned(&bits, yoffset_bits);
        optional_total        += DCC__ReadBits(&bits, optional_bits);
        DCC__ReadBits(&bits, coded_bits);
        const bool bottom_up = DCC__ReadBits(&bits, 1) != 0;

        frame->xmin = xoffset;
        frame->xmax = xoffset + frame->width - 1;
        if (bottom_up) {
            frame->ymin = yoffset;
            frame->ymax = yoffset + frame->height - 1;
        } else {
            frame->ymax = yoffset;
            frame->ymin = yoffset - frame->height + 1;
        }

        if (frame->width > 0 && frame->height > 0) {
            dir_xmin = frame->xmin < dir_xmin ? frame->xmin : dir_xmin;
            dir_ymin = frame->ymin < dir_ymin ? frame->ymin : dir_ymin;
            dir_xmax = frame->xmax > dir_xmax ? frame->xmax : dir_xmax;
            dir_ymax = frame->ymax > dir_ymax ? frame->ymax : dir_ymax;
        }
    }

    bool ok = !bits.overflow && dir_xmax >= dir_xmin && dir_ymax >= dir_ymin;
    if (!ok) {
        free(frames);
        return dir_xmax < dir_xmin; // a direction with only empty frames is fine
    }

    if (optional_total > 0) {
        bits.position  = (bits.position + 7) & ~(size_t)7;
        bits.position += optional_total * 8;
    }

    size_t equal_size = 0, mask_size = 0, encoding_size = 0, raw_size = 0;
    if (compression & 2) {
        equal_size = DCC__ReadBits(&bits, 20);
    }
    mask_size = DCC__ReadBits(&bits, 20);
    if (compression & 1) {
        encoding_size = DCC__ReadBits(&bits, 20);
        raw_size      = DCC__ReadBits(&bits, 20);
    }

    uint8_t pixel_values[256];
    int     pixel_value_count = 0;
    for (int i = 0; i < 256; i++) {
        if (DCC__ReadBits(&bits, 1)) {
            pixel_values[pixel_value_count++] = (uint8_t)i;
        }
    }

    size_t  stream_start   = bits.position;
    DCCBits equal_bits     = DCC__Bits(data, stream_start, stream_start + equal_size);
    stream_start          += equal_size;
    DCCBits mask_bits      = DCC__Bits(data, stream_start, stream_start + mask_size);
    stream_start          += mask_size;
    DCCBits encoding_bits  = DCC__Bits(data, stream_start, stream_start + encoding_size);
    stream_start          += encoding_size;
    DCCBits raw_bits       = DCC__Bits(data, stream_start, stream_start + raw_size);
    stream_start          += raw_size;
    DCCBits pixel_bits     = DCC__Bits(data, stream_start, end * 8);

    if (bits.overflow || stream_start > end * 8) {
        free(frames);
        return false;
    }

    const int dir_w   = dir_xmax - dir_xmin + 1;
    const int dir_h   = dir_ymax - dir_ymin + 1;
    const int cells_w = 1 + ((dir_w - 1) / 4);
    const int cells_h = 1 + ((dir_h - 1) / 4);

    int total_frame_cells = 0;
    for (int f = 0; f < frame_count; f++) {
        if (frames[f].width > 0 && frames[f].height > 0) {
            DCC__BuildFrameCells(&frames[f], dir_xmin, dir_ymin);
            total_frame_cells += frames[f].cells_w * frames[f].cells_h;
        }
    }

    // Stage 1: work out the (up to 4) colours of every cell that changes, frame by frame.
    DCCPixelEntry *entries     = calloc((size_t)total_frame_cells + 1, sizeof(DCCPixelEntry));
    int           *cell_latest = malloc(sizeof(int) * (size_t)cells_w * cells_h);
    int            entry_count = 0;
    FAIL_IF_NULL(entries);
    FAIL_IF_NULL(cell_latest);
    for (int i = 0; i < cells_w * cells_h; i++) {
        cell_latest[i] = -1;
    }

    for (int f = 0; f < frame_count; f++) {
        const DCCFrameHeader *frame = &frames[f];
        if (frame->cells == NULL) {
            continue;
        }

        for (int cy = 0; cy < frame->cells_h; cy++) {
            for (int cx = 0; cx < frame->cells_w; cx++) {
                const int dir_cell = ((frame->first_cell_y + cy) * cells_w) + frame->first_cell_x + cx;
                if (dir_cell < 0 || dir_cell >= cells_w * cells_h) {
                    continue;
                }

                uint32_t pixel_mask = 0x0F;
                if (cell_latest[dir_cell] >= 0) {
                    const uint32_t same = equal_size > 0 ? DCC__ReadBits(&equal_bits, 1) : 0;
                    if (same) {
                        continue; // unchanged since the previous frame
                    }
                    pixel_mask = DCC__ReadBits(&mask_bits, 4);
                }

                uint32_t  read_pixel[4] = {0, 0, 0, 0};
                uint32_t  last_pixel    = 0;
                int       decoded       = 0;
                const int pixel_count   = DCC__CountBits(pixel_mask);
                const int raw_encoding =
                    (pixel_count > 0 && encoding_size > 0) ? (int)DCC__ReadBits(&encoding_bits, 1) : 0;

                for (int i = 0; i < pixel_count; i++) {
                    if (raw_encoding) {
                        read_pixel[i] = DCC__ReadBits(&raw_bits, 8);
                    } else {
                        read_pixel[i] = last_pixel;
                        uint32_t step;
                        do {
                            step           = DCC__ReadBits(&pixel_bits, 4);
                            read_pixel[i] += step;
                        } while (step == 15 && !pixel_bits.overflow);
                    }

                    if (read_pixel[i] == last_pixel) {
                        read_pixel[i] = 0; // no more colours in this cell
                        break;
                    }
                    last_pixel = read_pixel[i];
                    decoded++;
                }

                DCCPixelEntry       *entry    = &entries[entry_count];
                const DCCPixelEntry *previous = cell_latest[dir_cell] >= 0 ? &entries[cell_latest[dir_cell]] : NULL;
                int                  next     = decoded - 1; // colours are stored newest first
                for (int i = 0; i < 4; i++) {
                    if (pixel_mask & (1u << i)) {
                        entry->value[i] = next >= 0 ? (uint8_t)read_pixel[next--] : 0;
                    } else {
                        entry->value[i] = previous != NULL ? previous->value[i] : 0;
                    }
                }
                entry->frame          = f;
                entry->frame_cell     = (cy * frame->cells_w) + cx;
                cell_latest[dir_cell] = entry_count++;
            }
        }
    }

    // Colour codes are indices into this direction's list of used palette entries.
    for (int e = 0; e < entry_count; e++) {
        for (int i = 0; i < 4; i++) {
            const uint8_t code  = entries[e].value[i];
            entries[e].value[i] = code < pixel_value_count ? pixel_values[code] : 0;
        }
    }

    // Stage 2: paint the cells into a direction-sized bitmap and cut each frame out of it.
    uint8_t *bitmap = calloc((size_t)dir_w * dir_h, 1);
    DCCCell *latest = calloc((size_t)cells_w * cells_h, sizeof(DCCCell));
    FAIL_IF_NULL(bitmap);
    FAIL_IF_NULL(latest);

    int entry_index = 0;
    for (int f = 0; f < frame_count; f++) {
        const DCCFrameHeader *frame = &frames[f];
        DCCFrame             *out   = &dcc->frames[(direction * frame_count) + f];

        if (frame->cells == NULL) {
            continue;
        }

        for (int cy = 0; cy < frame->cells_h; cy++) {
            for (int cx = 0; cx < frame->cells_w; cx++) {
                const int      frame_cell = (cy * frame->cells_w) + cx;
                const DCCCell *cell       = &frame->cells[frame_cell];
                const int      dir_cell   = ((frame->first_cell_y + cy) * cells_w) + frame->first_cell_x + cx;
                if (dir_cell < 0 || dir_cell >= cells_w * cells_h || cell->x + cell->w > dir_w ||
                    cell->y + cell->h > dir_h) {
                    continue;
                }
                DCCCell *previous = &latest[dir_cell];

                if (entry_index < entry_count && entries[entry_index].frame == f &&
                    entries[entry_index].frame_cell == frame_cell) {
                    const DCCPixelEntry *entry = &entries[entry_index++];

                    if (entry->value[0] == entry->value[1]) {
                        for (int y = 0; y < cell->h; y++) {
                            memset(&bitmap[((cell->y + y) * dir_w) + cell->x], entry->value[0], (size_t)cell->w);
                        }
                    } else {
                        const int code_bits = entry->value[1] == entry->value[2] ? 1 : 2;
                        for (int y = 0; y < cell->h; y++) {
                            for (int x = 0; x < cell->w; x++) {
                                const uint32_t code                           = DCC__ReadBits(&pixel_bits, code_bits);
                                bitmap[((cell->y + y) * dir_w) + cell->x + x] = entry->value[code & 3];
                            }
                        }
                    }
                } else if (previous->w == cell->w && previous->h == cell->h) {
                    // Unchanged cell: move last frame's pixels to where this frame's cell sits.
                    if (previous->x != cell->x || previous->y != cell->y) {
                        uint8_t copy[DCC_MAX_CELL_SIZE * DCC_MAX_CELL_SIZE];
                        for (int y = 0; y < cell->h && y < DCC_MAX_CELL_SIZE; y++) {
                            for (int x = 0; x < cell->w && x < DCC_MAX_CELL_SIZE; x++) {
                                copy[(y * DCC_MAX_CELL_SIZE) + x] =
                                    bitmap[((previous->y + y) * dir_w) + previous->x + x];
                            }
                        }
                        for (int y = 0; y < cell->h && y < DCC_MAX_CELL_SIZE; y++) {
                            for (int x = 0; x < cell->w && x < DCC_MAX_CELL_SIZE; x++) {
                                bitmap[((cell->y + y) * dir_w) + cell->x + x] = copy[(y * DCC_MAX_CELL_SIZE) + x];
                            }
                        }
                    }
                } else {
                    for (int y = 0; y < cell->h; y++) {
                        memset(&bitmap[((cell->y + y) * dir_w) + cell->x], 0, (size_t)cell->w);
                    }
                }

                *previous = *cell;
            }
        }

        // Cut the frame out of the direction bitmap
        const int fx = frame->xmin - dir_xmin;
        const int fy = frame->ymin - dir_ymin;
        out->width   = frame->width;
        out->height  = frame->height;
        out->x       = frame->xmin;
        out->y       = frame->ymin;
        out->pixels  = malloc((size_t)frame->width * frame->height);
        FAIL_IF_NULL(out->pixels);
        for (int y = 0; y < frame->height; y++) {
            memcpy(&out->pixels[y * frame->width], &bitmap[((fy + y) * dir_w) + fx], (size_t)frame->width);
        }
    }

    for (int f = 0; f < frame_count; f++) {
        free(frames[f].cells);
    }
    free(frames);
    free(entries);
    free(cell_latest);
    free(bitmap);
    free(latest);
    return true;
}

// --- Public API -------------------------------------------------------------------------------------------------

DCC *DCC_Load(const char *path) {
    if (!FileManager_FileExists(path)) {
        return NULL;
    }

    MpqStream     *stream = FileManager_OpenFile(path);
    const uint32_t size   = MpqStream_GetSize(stream);
    uint8_t       *data   = malloc(size + 4);
    FAIL_IF_NULL(data);
    memset(data, 0, size + 4);
    MpqStream_Read(stream, data, 0, size);
    MpqStream_Destroy(&stream);

    // Header: signature, version, directions, frames per direction (u32), tag (u32), size (u32), dir offsets
    if (size < 15 || data[0] != DCC_SIGNATURE) {
        LOG_WARN("'%s' is not a DCC file.", path);
        free(data);
        return NULL;
    }

    const int directions = data[2];
    const int frames     = (int)DCC__ReadU32(&data[3]);
    if (directions <= 0 || frames <= 0 || frames > 1024 || 15u + (uint32_t)directions * 4 > size) {
        LOG_WARN("'%s' has an invalid DCC header.", path);
        free(data);
        return NULL;
    }

    DCC *dcc = malloc(sizeof(DCC));
    FAIL_IF_NULL(dcc);
    dcc->direction_count      = directions;
    dcc->frames_per_direction = frames;
    dcc->frames               = calloc((size_t)directions * frames, sizeof(DCCFrame));
    FAIL_IF_NULL(dcc->frames);

    bool ok = true;
    for (int d = 0; d < directions && ok; d++) {
        const uint32_t start = DCC__ReadU32(&data[15 + (d * 4)]);
        const uint32_t end   = (d + 1 < directions) ? DCC__ReadU32(&data[15 + ((d + 1) * 4)]) : size;
        if (start >= end || end > size) {
            ok = false;
            break;
        }
        ok = DCC__DecodeDirection(dcc, d, data, start, end);
    }

    free(data);
    if (!ok) {
        LOG_WARN("Failed to decode DCC '%s'.", path);
        DCC_Destroy(&dcc);
        return NULL;
    }

    return dcc;
}

DCC *DCC_LoadFromDC6(const char *path) {
    if (!FileManager_FileExists(path)) {
        return NULL;
    }

    struct DC6    *dc6        = DC6_Load(path);
    const uint32_t total      = DC6_GetTotalFrameCount(dc6);
    const uint32_t directions = DC6_GetDirectionCount(dc6);
    if (total == 0 || directions == 0 || total % directions != 0) {
        DC6_Destroy(&dc6);
        return NULL;
    }

    DCC *dcc = malloc(sizeof(DCC));
    FAIL_IF_NULL(dcc);
    dcc->direction_count      = (int)directions;
    dcc->frames_per_direction = (int)(total / directions);
    dcc->frames               = calloc(total, sizeof(DCCFrame));
    FAIL_IF_NULL(dcc->frames);

    for (uint32_t i = 0; i < total; i++) {
        uint32_t w;
        uint32_t h;
        int32_t  ox;
        int32_t  oy;
        DC6_GetFrameSize(dc6, i, &w, &h);
        DC6_GetFrameOffset(dc6, i, &ox, &oy);
        if (w == 0 || h == 0) {
            continue;
        }

        // DC6 offsets give the bottom-left corner relative to the anchor
        DCCFrame *frame = &dcc->frames[i];
        frame->width    = (int)w;
        frame->height   = (int)h;
        frame->x        = ox;
        frame->y        = oy - (int)h;
        frame->pixels   = malloc((size_t)w * h);
        FAIL_IF_NULL(frame->pixels);
        memcpy(frame->pixels, DC6_GetFramePixelData(dc6, i), (size_t)w * h);
    }

    DC6_Destroy(&dc6);
    return dcc;
}

void DCC_Destroy(DCC **dcc) {
    if (dcc == NULL || *dcc == NULL) {
        return;
    }

    for (int i = 0; i < (*dcc)->direction_count * (*dcc)->frames_per_direction; i++) {
        free((*dcc)->frames[i].pixels);
    }
    free((*dcc)->frames);
    free(*dcc);
    *dcc = NULL;
}

int DCC_GetDirectionCount(const DCC *dcc) { return dcc->direction_count; }

int DCC_GetFramesPerDirection(const DCC *dcc) { return dcc->frames_per_direction; }

const DCCFrame *DCC_GetFrame(const DCC *dcc, const int direction, const int frame) {
    if (direction < 0 || direction >= dcc->direction_count || frame < 0 || frame >= dcc->frames_per_direction) {
        return NULL;
    }
    return &dcc->frames[(direction * dcc->frames_per_direction) + frame];
}

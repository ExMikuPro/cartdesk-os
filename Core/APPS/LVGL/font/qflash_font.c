#include "qflash_font.h"

#include <string.h>

#define QFNT_MAGIC              0x544E4651u
#define QFNT_VERSION            1u
#define QFNT_HEADER_SIZE        64u
#define QFNT_MAX_FONT_COUNT     8u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t font_count;
    uint32_t font_table_offset;
    uint32_t payload_crc32;
    uint32_t flags;
    uint32_t reserved0;
    uint8_t reserved[32];
} qfnt_header_t;

typedef struct __attribute__((packed)) {
    uint16_t pixel_size;
    uint16_t reserved0;
    int16_t line_height;
    int16_t baseline;
    uint32_t glyph_count;
    uint32_t glyph_table_offset;
    uint32_t bitmap_offset;
    uint32_t bitmap_size;
    uint32_t reserved1;
    uint32_t reserved2;
} qfnt_font_record_t;

typedef struct __attribute__((packed)) {
    uint32_t codepoint;
    uint32_t bitmap_offset;
    uint16_t advance;
    uint16_t box_width;
    uint16_t box_height;
    int16_t offset_x;
    int16_t offset_y;
    uint16_t reserved;
} qfnt_glyph_record_t;

typedef struct {
    const uint8_t *base;
    uint32_t total_size;
    const qfnt_font_record_t *record;
} qflash_font_context_t;

_Static_assert(sizeof(qfnt_header_t) == QFNT_HEADER_SIZE, "QFNT header layout mismatch");
_Static_assert(sizeof(qfnt_font_record_t) == 32u, "QFNT font record layout mismatch");
_Static_assert(sizeof(qfnt_glyph_record_t) == 20u, "QFNT glyph record layout mismatch");

static bool qflash_get_glyph_dsc(const lv_font_t *font,
                                 lv_font_glyph_dsc_t *dsc,
                                 uint32_t letter,
                                 uint32_t letter_next);
static const void *qflash_get_glyph_bitmap(lv_font_glyph_dsc_t *dsc,
                                           lv_draw_buf_t *draw_buf);

static qflash_font_context_t s_context_16;
static qflash_font_context_t s_context_20;
static qflash_font_context_t s_context_24;
static bool s_mounted;
static const char *s_last_error = "QFNT 尚未挂载";

lv_font_t qflash_font_16 = {
    .get_glyph_dsc = qflash_get_glyph_dsc,
    .get_glyph_bitmap = qflash_get_glyph_bitmap,
    .line_height = 18,
    .base_line = 3,
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = LV_FONT_KERNING_NONE,
    .static_bitmap = 1,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = &s_context_16,
    .fallback = &lv_font_montserrat_16,
};

lv_font_t qflash_font_20 = {
    .get_glyph_dsc = qflash_get_glyph_dsc,
    .get_glyph_bitmap = qflash_get_glyph_bitmap,
    .line_height = 22,
    .base_line = 4,
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = LV_FONT_KERNING_NONE,
    .static_bitmap = 1,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = &s_context_20,
    .fallback = &lv_font_montserrat_20,
};

lv_font_t qflash_font_24 = {
    .get_glyph_dsc = qflash_get_glyph_dsc,
    .get_glyph_bitmap = qflash_get_glyph_bitmap,
    .line_height = 26,
    .base_line = 5,
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = LV_FONT_KERNING_NONE,
    .static_bitmap = 1,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = &s_context_24,
    .fallback = &lv_font_montserrat_20,
};

static bool range_valid(uint32_t offset, uint32_t size, uint32_t total_size)
{
    return offset <= total_size && size <= total_size - offset;
}

static void clear_contexts(void)
{
    memset(&s_context_16, 0, sizeof(s_context_16));
    memset(&s_context_20, 0, sizeof(s_context_20));
    memset(&s_context_24, 0, sizeof(s_context_24));
}

static bool bind_record(const uint8_t *base,
                        uint32_t total_size,
                        const qfnt_font_record_t *record,
                        qflash_font_context_t *context,
                        lv_font_t *font)
{
    uint32_t glyph_bytes;
    const qfnt_glyph_record_t *glyphs;
    if(record->glyph_count > UINT32_MAX / sizeof(qfnt_glyph_record_t)) {
        return false;
    }
    glyph_bytes = record->glyph_count * sizeof(qfnt_glyph_record_t);
    if(!range_valid(record->glyph_table_offset, glyph_bytes, total_size)
       || !range_valid(record->bitmap_offset, record->bitmap_size, total_size)
       || record->line_height <= 0
       || record->baseline < 0) {
        return false;
    }

    glyphs = (const qfnt_glyph_record_t *)(base + record->glyph_table_offset);
    for(uint32_t index = 0; index < record->glyph_count; index++) {
        const qfnt_glyph_record_t *glyph = &glyphs[index];
        uint32_t bitmap_size = (uint32_t)glyph->box_width * glyph->box_height;
        if((index > 0u && glyphs[index - 1u].codepoint >= glyph->codepoint)
           || (bitmap_size > 0u
               && (glyph->bitmap_offset < record->bitmap_offset
                   || !range_valid(glyph->bitmap_offset - record->bitmap_offset,
                                   bitmap_size,
                                   record->bitmap_size)))) {
            return false;
        }
    }

    context->base = base;
    context->total_size = total_size;
    context->record = record;
    font->line_height = record->line_height;
    font->base_line = record->baseline;
    return true;
}

bool QFlashFont_Mount(const void *mapped_base, size_t region_size)
{
    const uint8_t *base = mapped_base;
    const qfnt_header_t *header;
    bool have_16 = false;
    bool have_20 = false;
    bool have_24 = false;

    s_mounted = false;
    clear_contexts();

    if(base == NULL || region_size < sizeof(qfnt_header_t)) {
        s_last_error = "QFNT 映射区域无效";
        return false;
    }

    header = (const qfnt_header_t *)base;
    if(header->magic != QFNT_MAGIC
       || header->version != QFNT_VERSION
       || header->header_size != QFNT_HEADER_SIZE
       || (header->flags & 1u) == 0u) {
        s_last_error = "QFNT 文件头或版本不匹配";
        return false;
    }
    if(header->total_size > region_size
       || header->total_size < sizeof(qfnt_header_t)
       || header->font_count == 0
       || header->font_count > QFNT_MAX_FONT_COUNT
       || !range_valid(header->font_table_offset,
                       header->font_count * sizeof(qfnt_font_record_t),
                       header->total_size)) {
        s_last_error = "QFNT 文件范围非法";
        return false;
    }

    const qfnt_font_record_t *records =
        (const qfnt_font_record_t *)(base + header->font_table_offset);
    for(uint32_t index = 0; index < header->font_count; index++) {
        const qfnt_font_record_t *record = &records[index];
        bool valid = false;
        if(record->pixel_size == 16u) {
            valid = bind_record(base, header->total_size, record, &s_context_16, &qflash_font_16);
            have_16 = valid;
        }
        else if(record->pixel_size == 20u) {
            valid = bind_record(base, header->total_size, record, &s_context_20, &qflash_font_20);
            have_20 = valid;
        }
        else if(record->pixel_size == 24u) {
            valid = bind_record(base, header->total_size, record, &s_context_24, &qflash_font_24);
            have_24 = valid;
        }

        if((record->pixel_size == 16u || record->pixel_size == 20u || record->pixel_size == 24u)
           && !valid) {
            clear_contexts();
            s_last_error = "QFNT 字号记录非法";
            return false;
        }
    }

    if(!have_16 || !have_20 || !have_24) {
        clear_contexts();
        s_last_error = "QFNT 缺少 16/20/24 px 字号";
        return false;
    }

    s_mounted = true;
    s_last_error = "OK";
    return true;
}

const lv_font_t *QFlashFont_Get(uint16_t pixel_size)
{
    if(!s_mounted) {
        return NULL;
    }
    if(pixel_size == 16u) return &qflash_font_16;
    if(pixel_size == 20u) return &qflash_font_20;
    if(pixel_size == 24u) return &qflash_font_24;
    return NULL;
}

bool QFlashFont_IsMounted(void)
{
    return s_mounted;
}

const char *QFlashFont_LastError(void)
{
    return s_last_error;
}

static const qfnt_glyph_record_t *find_glyph(const qflash_font_context_t *context,
                                             uint32_t codepoint,
                                             uint32_t *index_out)
{
    const qfnt_glyph_record_t *glyphs;
    uint32_t low = 0;
    uint32_t high;

    if(context == NULL || context->record == NULL) {
        return NULL;
    }

    glyphs = (const qfnt_glyph_record_t *)(context->base + context->record->glyph_table_offset);
    high = context->record->glyph_count;
    while(low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if(glyphs[middle].codepoint < codepoint) {
            low = middle + 1u;
        }
        else {
            high = middle;
        }
    }

    if(low >= context->record->glyph_count || glyphs[low].codepoint != codepoint) {
        return NULL;
    }
    if(index_out != NULL) {
        *index_out = low;
    }
    return &glyphs[low];
}

static bool qflash_get_glyph_dsc(const lv_font_t *font,
                                 lv_font_glyph_dsc_t *dsc,
                                 uint32_t letter,
                                 uint32_t letter_next)
{
    const qflash_font_context_t *context = font->dsc;
    uint32_t glyph_index;
    bool is_tab = letter == '\t';
    const qfnt_glyph_record_t *glyph;
    LV_UNUSED(letter_next);

    if(is_tab) {
        letter = ' ';
    }
    glyph = find_glyph(context, letter, &glyph_index);
    if(glyph == NULL) {
        return false;
    }

    dsc->adv_w = is_tab ? (uint16_t)(glyph->advance * 2u) : glyph->advance;
    dsc->box_w = glyph->box_width;
    dsc->box_h = glyph->box_height;
    dsc->ofs_x = glyph->offset_x;
    dsc->ofs_y = glyph->offset_y;
    dsc->stride = glyph->box_width;
    dsc->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc->is_placeholder = false;
    dsc->gid.index = glyph_index + 1u;
    return true;
}

static const void *qflash_get_glyph_bitmap(lv_font_glyph_dsc_t *dsc,
                                           lv_draw_buf_t *draw_buf)
{
    const lv_font_t *font = dsc->resolved_font;
    const qflash_font_context_t *context;
    const qfnt_glyph_record_t *glyphs;
    const qfnt_glyph_record_t *glyph;
    uint32_t glyph_index;
    uint32_t bitmap_size;
    LV_UNUSED(draw_buf);

    if(font == NULL || dsc->gid.index == 0u) {
        return NULL;
    }
    context = font->dsc;
    if(context == NULL || context->record == NULL) {
        return NULL;
    }

    glyph_index = dsc->gid.index - 1u;
    if(glyph_index >= context->record->glyph_count) {
        return NULL;
    }
    glyphs = (const qfnt_glyph_record_t *)(context->base + context->record->glyph_table_offset);
    glyph = &glyphs[glyph_index];
    bitmap_size = (uint32_t)glyph->box_width * glyph->box_height;
    if(bitmap_size == 0u || !range_valid(glyph->bitmap_offset, bitmap_size, context->total_size)) {
        return NULL;
    }
    return context->base + glyph->bitmap_offset;
}

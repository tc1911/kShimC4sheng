#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * The menu's font: an LZ4-compressed Noto Sans CJK SC subset (ASCII, Latin,
 * punctuation, kana, GB2312 level 1) embedded in the image and inflated at
 * boot into RAM outside the runtime slot; LVGL's TinyTTF draws it at any
 * size. See src/ui/fonts/provenance.json and tools/gen_ui_font.py.
 */

/* Bytes the inflated font needs. */
size_t kshim_ui_font_size(void);

/* Inflate the font into Buffer and check it. Returns 0 and sets *Font,
 * or a negative error (bad blob, buffer too small, corrupt data). */
int kshim_ui_font_inflate(void *Buffer, size_t Capacity, const void **Font);

#include <ui_font.h>

#include <string.h>

#include "src/libs/lz4/lz4.h"

/* ui_font_blob.S: "KSF1", raw size, compressed size, CRC-32, LZ4 block. */
extern const unsigned char kshim_ui_font_blob[];
extern const unsigned char kshim_ui_font_blob_end[];

#define KSHIM_UI_FONT_HEADER 16U

static uint32_t ReadLe32(const unsigned char *Bytes)
{
  return (uint32_t)Bytes[0] | ((uint32_t)Bytes[1] << 8) |
         ((uint32_t)Bytes[2] << 16) | ((uint32_t)Bytes[3] << 24);
}

static uint32_t Crc32(const unsigned char *Bytes, size_t Length)
{
  uint32_t Crc = UINT32_MAX;

  while (Length-- != 0U) {
    Crc ^= *Bytes++;
    for (unsigned Bit = 0; Bit < 8U; Bit++)
      Crc = (Crc >> 1) ^ (0xedb88320U & (0U - (Crc & 1U)));
  }
  return ~Crc;
}

static int Header(uint32_t *Raw, uint32_t *Packed)
{
  size_t Bytes = (size_t)(kshim_ui_font_blob_end - kshim_ui_font_blob);

  if (Bytes < KSHIM_UI_FONT_HEADER ||
      memcmp(kshim_ui_font_blob, "KSF1", 4) != 0)
    return -1;
  *Raw = ReadLe32(kshim_ui_font_blob + 4);
  *Packed = ReadLe32(kshim_ui_font_blob + 8);
  if (*Raw == 0U || *Raw > INT32_MAX || *Packed == 0U ||
      *Packed > Bytes - KSHIM_UI_FONT_HEADER)
    return -1;
  return 0;
}

size_t kshim_ui_font_size(void)
{
  uint32_t Raw, Packed;

  return Header(&Raw, &Packed) == 0 ? Raw : 0U;
}

int kshim_ui_font_inflate(void *Buffer, size_t Capacity, const void **Font)
{
  uint32_t Raw, Packed;

  if (Font == NULL)
    return -1;
  *Font = NULL;
  if (Header(&Raw, &Packed) != 0)
    return -1;
  if (Buffer == NULL || Capacity < Raw)
    return -2;
  if (LZ4_decompress_safe((const char *)kshim_ui_font_blob +
                              KSHIM_UI_FONT_HEADER,
                          Buffer, (int)Packed, (int)Raw) != (int)Raw ||
      Crc32(Buffer, Raw) != ReadLe32(kshim_ui_font_blob + 12))
    return -3;
  *Font = Buffer;
  return 0;
}

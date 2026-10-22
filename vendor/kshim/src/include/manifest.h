#pragma once

#include "bitops.h"

#include <stdint.h>

#define MANIFEST_MAGIC 0x464D504BUL // 'K', 'P', 'M', 'F'
#define MANIFEST_VERSION_MAJOR 0U
#define MANIFEST_VERSION_MINOR 1U
#define MANIFEST_MAX_ENTRIES 50U
#define MANIFEST_ALIGNMENT_SHIFT_MIN 2U
#define MANIFEST_ALIGNMENT_SHIFT_MAX 21U
#define MANIFEST_ENTRY_ALIGNMENT 4U

typedef enum {
    MF_ENTRY_TYPE_LINUX = 1,
    MF_ENTRY_TYPE_FREE_EXEC,
    MF_ENTRY_TYPE_SHIM,
    MF_ENTRY_TYPE_BLOB,
    MF_ENTRY_TYPE_DTB,
    MF_ENTRY_TYPE_MANIFEST,
} MANIFEST_ENTRY_TYPE;

typedef enum {
    MF_ENTRY_FLAG_BASE_IMAGE = BIT(0),
    MF_ENTRY_FLAG_COPY = BIT(1),
    MF_ENTRY_FLAG_EXECUTABLE = BIT(2),
    MF_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB = BIT(3),
    MF_ENTRY_FLAG_HAS_QC_ABOOT_HEADER = BIT(4),
} MANIFEST_ENTRY_FLAG;

typedef struct {
    uint16_t major;
    uint16_t minor;
} MANIFEST_VERSION;

_Static_assert(sizeof(MANIFEST_VERSION) == sizeof(uint32_t),
               "Invalid manifest version size");

#pragma pack(1)

typedef struct {
    uint64_t instructions;
    uint64_t image_size;
    uint64_t manifest_offset;
    uint64_t manifest_size;
    uint64_t shim_offset;
} MANIFEST_BASE_IMAGE;

_Static_assert(sizeof(MANIFEST_BASE_IMAGE) == 5 * sizeof(uint64_t),
               "Invalid manifest base image size");

typedef struct {
    uint32_t magic;
    MANIFEST_VERSION version;
    uint32_t entry_count;
    uint32_t default_entry;
    uint32_t timeout_ms;
    MANIFEST_BASE_IMAGE base_image;
} MANIFEST_HEADER;

_Static_assert(sizeof(MANIFEST_HEADER) == 60,
               "Invalid manifest header size");

typedef struct {
    uint64_t offset;
    uint64_t size;
    uint32_t type;
    uint32_t flags;
    uint64_t load_address;
    uint32_t entry_offset;
    uint32_t device_tree_blob_entry;
    uint32_t alignment;
    uint64_t copy_size_max;
    uint32_t name_len;
    char name[];
} MANIFEST_ENTRY;

_Static_assert(sizeof(MANIFEST_ENTRY) == 56,
               "Invalid manifest entry size");

typedef struct {
    MANIFEST_HEADER header;
    uint8_t entries[];
} MANIFEST;

#pragma pack()
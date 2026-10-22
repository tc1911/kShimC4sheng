/* SPDX-License-Identifier: MIT */
/*
 * TB323FU M2 persistent-console (ramoops) offline interface.
 *
 * This header is the frozen interface between the offline console utility and
 * the three data-free stages: capture, bind and read_console.  Implementations
 * live in tb323fu_ramoops.c.
 *
 * Profile (fixed TB323FU M2):
 *   region  = 0xc2000000 + 0xc0000, reserved child of /reserved-memory
 *   record  = 0x10000 (dmesg), console = 0x40000, pmsg = 0x40000
 *   ecc     = 16 bytes, block = 128, mem-type = 2, ftrace = 0, max-reason = 4
 *
 * Kernel layout reference: references/fs/pstore/ram.c (probe order dmesg,
 * console, pmsg, ftrace) and references/fs/pstore/ram_core.c
 * (persistent_ram_buffer header, ECC accounting, persistent_ram_save_old).
 */
#ifndef TB323FU_RAMOOPS_H
#define TB323FU_RAMOOPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Fixed TB323FU M2 ramoops profile ---------------------------------- */
#define TB323FU_RAMOOPS_REGION_BASE   0xc2000000ull
#define TB323FU_RAMOOPS_REGION_SIZE   0xc0000u
#define TB323FU_RAMOOPS_CONSOLE_OFF   0x40000u   /* dmesg zone precedes console */
#define TB323FU_RAMOOPS_CONSOLE_SIZE  0x40000u
#define TB323FU_RAMOOPS_RECORD_SIZE   0x10000u
#define TB323FU_RAMOOPS_PMSG_SIZE     0x40000u
#define TB323FU_RAMOOPS_FTRACE_SIZE   0u
#define TB323FU_RAMOOPS_ECC_SIZE      16u
#define TB323FU_RAMOOPS_ECC_BLOCK     128u
#define TB323FU_RAMOOPS_MEM_TYPE      2u
#define TB323FU_RAMOOPS_MAX_REASON    4u
#define TB323FU_RAMOOPS_MAX_DUMP_CNT  4u       /* dump_mem_sz / record_size */

/* persistent_ram_buffer layout (ram_core.c). */
#define TB323FU_PRZ_SIG               0x43474244u /* PERSISTENT_RAM_SIG (DBGC) */
#define TB323FU_PRZ_HEADER_SIZE       12u         /* sig, start, size (u32 LE) */

/*
 * Usable console data bytes after the 12-byte header and the trailing ECC
 * area, computed exactly like persistent_ram_init_ecc():
 *   blocks = DIV_ROUND_UP(console_size - 12 - ecc, block + ecc)
 *   total  = (blocks + 1) * ecc
 *   usable = console_size - 12 - total
 * With console_size=0x40000, ecc=16, block=128: blocks=1821, total=29152,
 * usable=232980.
 */
#define TB323FU_RAMOOPS_CONSOLE_USABLE 232980u

/* Exported tail window and stage summary sizing. */
#define TB323FU_RAMOOPS_TAIL_MAX      252u  /* raw tail bytes, may be binary */
#define TB323FU_RAMOOPS_STAGE_MAX     32u   /* max stage characters kept */
#define TB323FU_RAMOOPS_STAGE_BUF     48u   /* stage field backing storage */

/* ---- Status codes ------------------------------------------------------- */
enum tb323fu_ramoops_status {
	TB323FU_RAMOOPS_OK            = 0,
	TB323FU_RAMOOPS_ERR_INVALID   = -1,   /* malformed argument / FDT */
	TB323FU_RAMOOPS_ERR_NOT_FOUND = -2,   /* required node/field absent */
	TB323FU_RAMOOPS_ERR_AMBIGUOUS = -3,   /* more than one candidate */
	TB323FU_RAMOOPS_ERR_LAYOUT    = -4,   /* layout/value mismatch */
	TB323FU_RAMOOPS_ERR_OVERFLOW  = -5,   /* value/length out of range */
	TB323FU_RAMOOPS_ERR_CAPACITY  = -6,   /* output buffer too small */
	TB323FU_RAMOOPS_ERR_CORRUPT   = -7,   /* inconsistent RAM header */
	/* Returned by the not-yet-implemented stubs; never by a real impl. */
	TB323FU_RAMOOPS_ERR_NOTIMPL   = -100,
};

/* ---- Layout ------------------------------------------------------------- */
/*
 * Result of capture().  On any capture failure the caller must treat the
 * struct as invalid: capture() zeroes it and leaves valid == false, so a
 * failed call never leaves a consumable layout behind.
 */
typedef struct tb323fu_ramoops_layout {
	uint64_t base;           /* region physical base (0xc2000000) */
	uint64_t size;           /* region size (0xc0000) */
	uint32_t record_size;    /* 0x10000 */
	uint32_t console_size;   /* 0x40000 */
	uint32_t pmsg_size;      /* 0x40000 */
	uint32_t ftrace_size;    /* 0 (absent or explicit zero) */
	uint32_t ecc_size;       /* 16 */
	uint32_t mem_type;       /* 2 */
	uint32_t region_phandle; /* phandle of the referenced reservation */
	bool     valid;          /* set true only on success */
} tb323fu_ramoops_layout;

/*
 * capture(): read-only inspection of the runtime FDT.
 *
 * Requirements (all violations are rejected):
 *   - runtime is a valid FDT of at most runtime_size bytes.
 *   - exactly one *available* node compatible = "qcom,ramoops" (zero =>
 *     ERR_NOT_FOUND, more than one => ERR_AMBIGUOUS).  Nodes with
 *     status != okay/ok (e.g. "disabled") are not candidates.
 *   - its "memory-region" holds exactly one valid phandle (4 bytes); the
 *     phandle resolves to a node that is a DIRECT, available child of
 *     /reserved-memory (ERR_INVALID / ERR_LAYOUT otherwise).
 *   - that reserved child's parent has #address-cells == 2, #size-cells == 2
 *     and an empty "ranges" (length 0).
 *   - the reserved child's "reg" is exactly 16 bytes decoding to
 *     0xc2000000 / 0xc0000, has "no-map" and no "reusable".
 *   - on qcom,ramoops: record-size=0x10000, console-size=0x40000,
 *     pmsg-size=0x40000, ecc-size=16, mem-type=2, each exactly 4 bytes;
 *     ftrace-size absent or present, exactly 4 bytes and 0.
 *
 * The source FDT is never modified.  On failure the source is unchanged and
 * *out is zeroed.  Returns TB323FU_RAMOOPS_OK on success.
 */
int tb323fu_ramoops_capture(const void *runtime, size_t runtime_size,
			    tb323fu_ramoops_layout *out);

/*
 * bind(): annotate the handoff output tree with a ramoops node.
 *
 * The runtime-memory handoff has already copied the reservation into
 * output's /reserved-memory.  bind() locates the unique child reservation
 * whose "reg" is exactly base/size, carries "no-map" and is active (no status
 * or status okay/ok), renames it to "ramoops@c2000000", and adds only:
 *   compatible = "ramoops", record-size, console-size, pmsg-size,
 *   ftrace-size (=layout->ftrace_size), ecc-size, mem-type, max-reason = 4.
 * The node's reg and existing phandle are preserved and it becomes the
 * platform device directly.
 *
 * Rejected / explicit failure (never a silent new or overlapping node):
 *   - no exact base/size reservation (ERR_NOT_FOUND);
 *   - more than one exact reservation (ERR_AMBIGUOUS);
 *   - a reservation that only partially covers the range (ERR_LAYOUT);
 *   - the exact reservation lacking no-map or being disabled (ERR_LAYOUT);
 *   - another node anywhere already compatible = "ramoops" (ERR_LAYOUT);
 *   - another node anywhere already named "ramoops@c2000000" (ERR_LAYOUT);
 *   - not enough room in output for the edits (ERR_CAPACITY).
 *
 * Only the target node is modified; no sibling reservation is touched.
 * On failure the output contents are unspecified and must not be consumed
 * (bind() is not transactional).  The runtime source is never passed here and
 * is never modified.  Returns TB323FU_RAMOOPS_OK on success.
 */
int tb323fu_ramoops_bind(void *output, size_t capacity,
			 const tb323fu_ramoops_layout *layout);

/* ---- Console read ------------------------------------------------------- */
typedef enum tb323fu_console_state {
	TB323FU_CONSOLE_NO_SIGNATURE = 0, /* header sig != 0x43474244 */
	TB323FU_CONSOLE_BAD_HEADER,       /* sig ok, size>capacity or start>size */
	TB323FU_CONSOLE_EMPTY,            /* sig ok, size==0 (and start==0) */
	TB323FU_CONSOLE_RAW_LOG,          /* valid raw circular log present */
} tb323fu_console_state;

/*
 * Decoded, read-only view of the fixed console zone.  No ECC correction is
 * performed: ecc_corrected is always false and the log is raw/unverified.
 */
typedef struct tb323fu_console_summary {
	tb323fu_console_state state;
	uint32_t start;                 /* header start, echoed */
	uint32_t size;                  /* header size, echoed */
	uint32_t capacity;              /* TB323FU_RAMOOPS_CONSOLE_USABLE */
	uint8_t  tail[TB323FU_RAMOOPS_TAIL_MAX]; /* last raw bytes, may be binary */
	size_t   tail_len;              /* 0..TAIL_MAX, independent of NULs */
	char     stage[TB323FU_RAMOOPS_STAGE_BUF]; /* NUL-terminated stage token */
	bool     have_stage;
	bool     ecc_corrected;         /* always false: raw, unverified */
} tb323fu_console_summary;

/*
 * read_console(): decode the console zone of region[] in place (read-only).
 *
 * layout must be a valid captured layout.  len must cover the whole region
 * (len >= 0xc0000) or ERR_INVALID is returned; region is never read past len.
 * The console header lives at region + 0x40000 and holds sig/start/size as
 * u32 little-endian.  Validation matches the kernel persistent_ram_post_init:
 *   sig != 0x43474244                       -> NO_SIGNATURE
 *   size > capacity || start > size         -> BAD_HEADER
 *   size == 0                               -> EMPTY
 *   otherwise                               -> RAW_LOG
 * The logical log is reconstructed exactly like persistent_ram_save_old:
 * logical[i] = data[(start + i) % size] for i in [0,size), where data is
 * region + 0x40000 + 12.  Bytes in the trailing ECC area never enter the log.
 * tail[] receives the last min(size, 252) raw logical bytes (tail_len is the
 * exact count and may contain any byte value, including NUL).
 *
 * stage[] receives the value X of the LAST complete, newline-terminated line
 * that contains the marker "KSHIM_LINUX_STAGE: ".  Real kernel console lines
 * are prefixed (e.g. "[    0.000000] "), so the marker is matched anywhere
 * within the line, not only at the start: X is the remainder of that line (a
 * single trailing '\r' before the '\n' is stripped).  X must be 1..32 bytes,
 * all in [0x21,0x7e]; a line whose X is empty, too long or contains any other
 * byte leaves the previously found stage (or have_stage=false) untouched.  A
 * partial final line without '\n' never becomes a stage.  Scanning is a
 * streaming, byte-indexed walk of the reconstructed circular order (never
 * strstr over the raw blob), so binary bytes and ring wrap are handled.
 *
 * region and *out must be non-NULL (ERR_INVALID).  The RAM is never written.
 * Returns TB323FU_RAMOOPS_OK for every content state above; a negative value
 * means the arguments are unusable.
 */
int tb323fu_ramoops_read_console(const volatile uint8_t *region, size_t len,
				 const tb323fu_ramoops_layout *layout,
				 tb323fu_console_summary *out);

#ifdef __cplusplus
}
#endif

#endif /* TB323FU_RAMOOPS_H */

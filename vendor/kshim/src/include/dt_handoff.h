#ifndef DT_HANDOFF_H
#define DT_HANDOFF_H

#include <stddef.h>

/*
 * Merge a runtime (source) device tree's RAM/memory description into a
 * payload (target) device tree, producing a merged output FDT.
 *
 * runtime / payload inputs are immutable: the call MUST NOT modify them.
 * output buffer contents are invalid on failure (do not use on non-zero rc).
 *
 * Destination requires root "#address-cells = 2" and "#size-cells = 2".
 * The initial builtin-initramfs profile removes destination
 * linux,initrd-start / linux,initrd-end properties.
 * Any caller opt-in policy is applied externally, not by this function.
 *
 * Returns 0 on success, a negative FDT error code on failure.
 */
int dt_handoff_memory(const void *runtime, size_t runtime_size,
                      const void *payload, size_t payload_size,
                      void *output, size_t capacity);


/* DT memory snapshot/overlay policy.
 *
 * The snapshot image is immutable, private and packed; it is produced only
 * after a successful dt_memory_capture() and used solely as input to
 * dt_memory_apply(). apply() preserves the chosen image. output and overlay
 * are independent buffers aligned to 8 bytes. If an overlay buffer is
 * consumed, the output image is left invalid on failure.
 */
#define DT_MEMORY_SNAPSHOT_BYTES (32u * 1024u)
#define DT_MEMORY_OVERLAY_BYTES  (64u * 1024u)

int dt_memory_capture(const void *runtime, size_t runtime_size,
                      void *saved, size_t capacity);
int dt_memory_apply(const void *saved, size_t saved_size,
                    const void *payload, size_t payload_size,
                    void *output, size_t capacity,
                    void *overlay, size_t overlay_capacity);

/* Diagnostic: detail of the last legacy normalization call.
 * Not functional state and not a return code. */
const char *dt_handoff_failure_detail(void);

#endif /* DT_HANDOFF_H */

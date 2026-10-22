#ifndef KSHIM_DT_SIMPLEFB_H
#define KSHIM_DT_SIMPLEFB_H

#include <stddef.h>
#include <framebuffer.h>

/*
 * Promote the disabled /chosen simple-framebuffer template into a live node
 * describing fb and reserve its scanout span in the FDT memory reservation
 * block. No-ops (returns 0, blob untouched) when /chosen or the template is
 * absent. Every validation check runs before any mutation, so a rejected call
 * leaves every byte of the supplied capacity unchanged.
 */
int dt_simplefb_apply(void *blob, size_t capacity,
                      const kshim_framebuffer_config_t *fb);

#endif /* KSHIM_DT_SIMPLEFB_H */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* LVGL includes this file before declaring its own OS API types. */
typedef struct {
    void (*callback)(void *);
    void *user_data;
    uint64_t ticket;
    uint32_t worker_index;
    uint32_t active;
    uint32_t finished;
    uint32_t inline_mode;
} lv_thread_t;

typedef struct {
    uint64_t owner;
    uint32_t locked;
    uint32_t depth;
    uint32_t initialized;
    uint32_t reserved;
} lv_mutex_t;

typedef struct {
    uint32_t signaled;
    uint32_t initialized;
} lv_thread_sync_t;

/* Hooks used by the generated LVGL software-renderer adapter. */
void kshim_lvgl_os_thread_set_inline(lv_thread_t *thread);
int kshim_lvgl_os_thread_is_inline(const lv_thread_t *thread);

/* Runtime diagnostics used by the QEMU concurrency gate. */
size_t kshim_lvgl_os_active_threads(void);
size_t kshim_lvgl_os_peak_threads(void);
uint32_t kshim_lvgl_os_worker_mask(void);
int kshim_lvgl_os_reset_stats(void);
int kshim_lvgl_os_failed(void);
int kshim_lvgl_os_prepare_shutdown(void);

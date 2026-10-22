/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KSHIM_TOUCH_MAX_CONTACTS 10U
#define KSHIM_TOUCH_NO_PRIMARY UINT8_MAX

typedef enum kshim_touch_rotation {
    KSHIM_TOUCH_ROTATION_0 = 0,
    KSHIM_TOUCH_ROTATION_90 = 90,
    KSHIM_TOUCH_ROTATION_180 = 180,
    KSHIM_TOUCH_ROTATION_270 = 270,
} kshim_touch_rotation_t;

typedef enum kshim_touch_contact_action {
    KSHIM_TOUCH_CONTACT_ENTER = 0,
    KSHIM_TOUCH_CONTACT_MOVE,
    KSHIM_TOUCH_CONTACT_LEAVE,
} kshim_touch_contact_action_t;

typedef enum kshim_touch_event_type {
    KSHIM_TOUCH_EVENT_PRESS = 0,
    KSHIM_TOUCH_EVENT_MOVE,
    KSHIM_TOUCH_EVENT_RELEASE,
    /* Discard a contact after an error/shutdown without completing a click. */
    KSHIM_TOUCH_EVENT_CANCEL,
} kshim_touch_event_type_t;

typedef struct kshim_touch_event {
    kshim_touch_event_type_t type;
    uint8_t contact_id;
    uint32_t x;
    uint32_t y;
} kshim_touch_event_t;

typedef void (*kshim_touch_event_fn)(void *context,
                                     const kshim_touch_event_t *event);

typedef struct kshim_touch_config {
    uint32_t source_width;
    uint32_t source_height;
    uint32_t output_width;
    uint32_t output_height;
    kshim_touch_rotation_t rotation;
    bool flip_x, flip_y;
    kshim_touch_event_fn emit;
    void *emit_context;
} kshim_touch_config_t;

typedef struct kshim_touch_contact {
    uint32_t x;
    uint32_t y;
    uint64_t activation_order;
    bool active;
} kshim_touch_contact_t;

typedef struct kshim_touch {
    kshim_touch_config_t config;
    kshim_touch_contact_t contacts[KSHIM_TOUCH_MAX_CONTACTS];
    uint64_t next_activation_order;
    uint32_t reported_x;
    uint32_t reported_y;
    uint8_t primary_id;
    bool initialized;
    bool primary_active;
} kshim_touch_t;

enum {
    KSHIM_TOUCH_OK = 0,
    KSHIM_TOUCH_ERR_INVALID = -22,
};

int kshim_touch_init(kshim_touch_t *touch,
                     const kshim_touch_config_t *config);

/* MOVE for an inactive contact and LEAVE for a released contact are ignored. */
int kshim_touch_update(kshim_touch_t *touch, uint8_t contact_id,
                       kshim_touch_contact_action_t action,
                       uint32_t x, uint32_t y);

/* Cancel the reported primary once and discard every tracked contact. */
void kshim_touch_cancel(kshim_touch_t *touch);

bool kshim_touch_get_primary(const kshim_touch_t *touch,
                             kshim_touch_event_t *primary);

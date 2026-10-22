/* SPDX-License-Identifier: MIT */
#include <touch.h>

#include <stddef.h>
#include <string.h>

static bool kshim_touch_rotation_valid(kshim_touch_rotation_t rotation)
{
    return rotation == KSHIM_TOUCH_ROTATION_0 ||
           rotation == KSHIM_TOUCH_ROTATION_90 ||
           rotation == KSHIM_TOUCH_ROTATION_180 ||
           rotation == KSHIM_TOUCH_ROTATION_270;
}

static uint32_t kshim_touch_clamp(uint32_t value, uint32_t extent)
{
    if (value >= extent)
        return extent - 1U;
    return value;
}

static uint32_t kshim_touch_scale(uint32_t value, uint32_t source_extent,
                                  uint32_t output_extent)
{
    uint64_t numerator;

    if (source_extent <= 1U || output_extent <= 1U)
        return 0;

    numerator = (uint64_t)value * (output_extent - 1U);
    numerator += (source_extent - 1U) / 2U;
    return (uint32_t)(numerator / (source_extent - 1U));
}

static void kshim_touch_transform(const kshim_touch_t *touch, uint32_t x,
                                  uint32_t y, uint32_t *output_x,
                                  uint32_t *output_y)
{
    uint32_t rotated_x;
    uint32_t rotated_y;
    uint32_t rotated_width;
    uint32_t rotated_height;

    x = kshim_touch_clamp(x, touch->config.source_width);
    y = kshim_touch_clamp(y, touch->config.source_height);

    if (touch->config.flip_x) x = touch->config.source_width - 1U - x;
    if (touch->config.flip_y) y = touch->config.source_height - 1U - y;

    switch (touch->config.rotation) {
    case KSHIM_TOUCH_ROTATION_90:
        rotated_x = touch->config.source_height - 1U - y;
        rotated_y = x;
        rotated_width = touch->config.source_height;
        rotated_height = touch->config.source_width;
        break;
    case KSHIM_TOUCH_ROTATION_180:
        rotated_x = touch->config.source_width - 1U - x;
        rotated_y = touch->config.source_height - 1U - y;
        rotated_width = touch->config.source_width;
        rotated_height = touch->config.source_height;
        break;
    case KSHIM_TOUCH_ROTATION_270:
        rotated_x = y;
        rotated_y = touch->config.source_width - 1U - x;
        rotated_width = touch->config.source_height;
        rotated_height = touch->config.source_width;
        break;
    case KSHIM_TOUCH_ROTATION_0:
    default:
        rotated_x = x;
        rotated_y = y;
        rotated_width = touch->config.source_width;
        rotated_height = touch->config.source_height;
        break;
    }

    *output_x = kshim_touch_scale(rotated_x, rotated_width,
                                  touch->config.output_width);
    *output_y = kshim_touch_scale(rotated_y, rotated_height,
                                  touch->config.output_height);
}

static void kshim_touch_emit(kshim_touch_t *touch,
                             kshim_touch_event_type_t type,
                             uint8_t contact_id, uint32_t x, uint32_t y)
{
    kshim_touch_event_t event = {
        .type = type,
        .contact_id = contact_id,
        .x = x,
        .y = y,
    };

    if (touch->config.emit != NULL)
        touch->config.emit(touch->config.emit_context, &event);
}

static void kshim_touch_report_primary(kshim_touch_t *touch,
                                       kshim_touch_event_type_t type)
{
    const kshim_touch_contact_t *contact =
        &touch->contacts[touch->primary_id];

    kshim_touch_transform(touch, contact->x, contact->y,
                          &touch->reported_x, &touch->reported_y);
    kshim_touch_emit(touch, type, touch->primary_id,
                     touch->reported_x, touch->reported_y);
}

static uint8_t kshim_touch_find_oldest(const kshim_touch_t *touch)
{
    uint64_t oldest_order = UINT64_MAX;
    uint8_t oldest_id = KSHIM_TOUCH_NO_PRIMARY;

    for (uint8_t id = 0; id < KSHIM_TOUCH_MAX_CONTACTS; ++id) {
        const kshim_touch_contact_t *contact = &touch->contacts[id];

        if (contact->active && contact->activation_order < oldest_order) {
            oldest_order = contact->activation_order;
            oldest_id = id;
        }
    }
    return oldest_id;
}

static void kshim_touch_select_next_primary(kshim_touch_t *touch)
{
    uint8_t next_id = kshim_touch_find_oldest(touch);

    if (next_id == KSHIM_TOUCH_NO_PRIMARY) {
        touch->primary_id = KSHIM_TOUCH_NO_PRIMARY;
        touch->primary_active = false;
        return;
    }

    touch->primary_id = next_id;
    touch->primary_active = true;
    kshim_touch_report_primary(touch, KSHIM_TOUCH_EVENT_PRESS);
}

int kshim_touch_init(kshim_touch_t *touch,
                     const kshim_touch_config_t *config)
{
    if (touch == NULL || config == NULL || config->source_width == 0U ||
        config->source_height == 0U || config->output_width == 0U ||
        config->output_height == 0U ||
        !kshim_touch_rotation_valid(config->rotation))
        return KSHIM_TOUCH_ERR_INVALID;

    memset(touch, 0, sizeof(*touch));
    touch->config = *config;
    touch->primary_id = KSHIM_TOUCH_NO_PRIMARY;
    touch->next_activation_order = 1U;
    touch->initialized = true;
    return KSHIM_TOUCH_OK;
}

int kshim_touch_update(kshim_touch_t *touch, uint8_t contact_id,
                       kshim_touch_contact_action_t action,
                       uint32_t x, uint32_t y)
{
    kshim_touch_contact_t *contact;
    uint32_t transformed_x;
    uint32_t transformed_y;

    if (touch == NULL || !touch->initialized ||
        contact_id >= KSHIM_TOUCH_MAX_CONTACTS ||
        action < KSHIM_TOUCH_CONTACT_ENTER ||
        action > KSHIM_TOUCH_CONTACT_LEAVE)
        return KSHIM_TOUCH_ERR_INVALID;

    contact = &touch->contacts[contact_id];

    if (action == KSHIM_TOUCH_CONTACT_LEAVE) {
        if (!contact->active)
            return KSHIM_TOUCH_OK;

        contact->active = false;
        if (!touch->primary_active || touch->primary_id != contact_id)
            return KSHIM_TOUCH_OK;

        kshim_touch_emit(touch, KSHIM_TOUCH_EVENT_RELEASE, contact_id,
                         touch->reported_x, touch->reported_y);
        touch->primary_active = false;
        touch->primary_id = KSHIM_TOUCH_NO_PRIMARY;
        kshim_touch_select_next_primary(touch);
        return KSHIM_TOUCH_OK;
    }

    if (action == KSHIM_TOUCH_CONTACT_MOVE && !contact->active)
        return KSHIM_TOUCH_OK;

    x = kshim_touch_clamp(x, touch->config.source_width);
    y = kshim_touch_clamp(y, touch->config.source_height);

    if (!contact->active) {
        contact->active = true;
        contact->activation_order = touch->next_activation_order++;
        if (touch->next_activation_order == 0U)
            touch->next_activation_order = 1U;
    }
    contact->x = x;
    contact->y = y;

    if (!touch->primary_active) {
        touch->primary_id = contact_id;
        touch->primary_active = true;
        kshim_touch_report_primary(touch, KSHIM_TOUCH_EVENT_PRESS);
        return KSHIM_TOUCH_OK;
    }

    if (touch->primary_id != contact_id)
        return KSHIM_TOUCH_OK;

    kshim_touch_transform(touch, x, y, &transformed_x, &transformed_y);
    if (transformed_x == touch->reported_x &&
        transformed_y == touch->reported_y)
        return KSHIM_TOUCH_OK;

    touch->reported_x = transformed_x;
    touch->reported_y = transformed_y;
    kshim_touch_emit(touch, KSHIM_TOUCH_EVENT_MOVE, contact_id,
                     transformed_x, transformed_y);
    return KSHIM_TOUCH_OK;
}

void kshim_touch_cancel(kshim_touch_t *touch)
{
    if (touch == NULL || !touch->initialized)
        return;

    if (touch->primary_active) {
        kshim_touch_emit(touch, KSHIM_TOUCH_EVENT_CANCEL,
                         touch->primary_id, touch->reported_x,
                         touch->reported_y);
    }

    memset(touch->contacts, 0, sizeof(touch->contacts));
    touch->primary_id = KSHIM_TOUCH_NO_PRIMARY;
    touch->primary_active = false;
}

bool kshim_touch_get_primary(const kshim_touch_t *touch,
                             kshim_touch_event_t *primary)
{
    if (touch == NULL || !touch->initialized || !touch->primary_active)
        return false;

    if (primary != NULL) {
        primary->type = KSHIM_TOUCH_EVENT_MOVE;
        primary->contact_id = touch->primary_id;
        primary->x = touch->reported_x;
        primary->y = touch->reported_y;
    }
    return true;
}

#!/usr/bin/env python3
"""Generate the kShim SMP-aware LVGL software renderer from pinned source."""

import argparse
from pathlib import Path


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(f"expected one {label} block, found {count}")
    return source.replace(old, new, 1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    source = args.source.read_text()
    include_replacements = {
        '#include "lv_draw_sw_private.h"':
            '#include <src/draw/sw/lv_draw_sw_private.h>',
        '#include "../lv_draw_private.h"':
            '#include <src/draw/lv_draw_private.h>',
        '#include "../../core/lv_refr.h"':
            '#include <src/core/lv_refr.h>',
        '#include "../../display/lv_display_private.h"':
            '#include <src/display/lv_display_private.h>',
        '#include "../../stdlib/lv_string.h"':
            '#include <src/stdlib/lv_string.h>',
        '#include "../../core/lv_global.h"':
            '#include <src/core/lv_global.h>',
    }
    for old, new in include_replacements.items():
        source = replace_once(source, old, new, old)

    old_init = """    uint32_t i;
    for(i = 0; i < LV_DRAW_SW_DRAW_UNIT_CNT; i++) {
        lv_draw_sw_unit_t * draw_sw_unit = lv_draw_create_unit(sizeof(lv_draw_sw_unit_t));
        draw_sw_unit->base_unit.dispatch_cb = dispatch;
        draw_sw_unit->base_unit.evaluate_cb = evaluate;
        draw_sw_unit->idx = i;
        draw_sw_unit->base_unit.delete_cb = LV_USE_OS ? lv_draw_sw_delete : NULL;

#if LV_USE_OS
        lv_thread_init(&draw_sw_unit->thread, LV_THREAD_PRIO_HIGH, render_thread_cb, LV_DRAW_THREAD_STACK_SIZE, draw_sw_unit);
#endif
    }
"""
    new_init = """    uint32_t i;
#if LV_USE_OS == LV_OS_CUSTOM
    uint32_t created = 0;
#endif
    for(i = 0; i < LV_DRAW_SW_DRAW_UNIT_CNT; i++) {
        lv_draw_sw_unit_t * draw_sw_unit = lv_draw_create_unit(sizeof(lv_draw_sw_unit_t));
        draw_sw_unit->base_unit.dispatch_cb = dispatch;
        draw_sw_unit->base_unit.evaluate_cb = evaluate;
        draw_sw_unit->idx = i;
        draw_sw_unit->base_unit.delete_cb = LV_USE_OS ? lv_draw_sw_delete : NULL;

#if LV_USE_OS == LV_OS_CUSTOM
        lv_result_t sync_status = lv_thread_sync_init(&draw_sw_unit->sync);
        lv_result_t thread_status = LV_RESULT_INVALID;
        if(sync_status == LV_RESULT_OK) {
            draw_sw_unit->inited = true;
            thread_status = lv_thread_init(&draw_sw_unit->thread, LV_THREAD_PRIO_HIGH,
                                           render_thread_cb, LV_DRAW_THREAD_STACK_SIZE, draw_sw_unit);
        }
        if(thread_status != LV_RESULT_OK) {
            if(sync_status == LV_RESULT_OK) {
                draw_sw_unit->inited = false;
                lv_thread_sync_delete(&draw_sw_unit->sync);
            }
            _draw_info.unit_head = draw_sw_unit->base_unit.next;
            _draw_info.unit_cnt--;
            lv_free(draw_sw_unit);

            /* A render unit still has to exist when all secondary CPUs are
             * occupied. Keep the unit on Core0 so LVGL dispatches all
             * LV_DRAW_SW_DRAW_UNIT_CNT units (7 workers plus Core0 inline). */
            draw_sw_unit = lv_draw_create_unit(sizeof(lv_draw_sw_unit_t));
            draw_sw_unit->base_unit.dispatch_cb = dispatch;
            draw_sw_unit->base_unit.evaluate_cb = evaluate;
            draw_sw_unit->idx = i;
            draw_sw_unit->base_unit.delete_cb = lv_draw_sw_delete;
            kshim_lvgl_os_thread_set_inline(&draw_sw_unit->thread);
            created++;
            continue;
        }
        created++;
#elif LV_USE_OS
        lv_thread_init(&draw_sw_unit->thread, LV_THREAD_PRIO_HIGH, render_thread_cb,
                       LV_DRAW_THREAD_STACK_SIZE, draw_sw_unit);
#endif
    }
"""
    source = replace_once(source, old_init, new_init, "draw-unit init")

    old_dispatch = """#if LV_USE_OS
    /*Let the render thread work*/
    if(draw_sw_unit->inited) lv_thread_sync_signal(&draw_sw_unit->sync);
#else
    execute_drawing_unit(draw_sw_unit);
#endif
"""
    new_dispatch = """#if LV_USE_OS == LV_OS_CUSTOM
    if(kshim_lvgl_os_thread_is_inline(&draw_sw_unit->thread)) {
        execute_drawing_unit(draw_sw_unit);
    }
    else if(draw_sw_unit->inited) {
        lv_thread_sync_signal(&draw_sw_unit->sync);
    }
#elif LV_USE_OS
    /*Let the render thread work*/
    if(draw_sw_unit->inited) lv_thread_sync_signal(&draw_sw_unit->sync);
#else
    execute_drawing_unit(draw_sw_unit);
#endif
"""
    source = replace_once(source, old_dispatch, new_dispatch, "dispatch")

    old_thread_start = """    lv_draw_sw_unit_t * u = ptr;

    lv_thread_sync_init(&u->sync);
    u->inited = true;

    while(1) {
"""
    new_thread_start = """    lv_draw_sw_unit_t * u = ptr;

#if LV_USE_OS != LV_OS_CUSTOM
    lv_thread_sync_init(&u->sync);
    u->inited = true;
#endif

    while(1) {
"""
    source = replace_once(
        source, old_thread_start, new_thread_start, "render-thread init")

    banner = "/* Generated from pinned LVGL by ports/lvgl/generate_draw_sw.py. */\n"
    generated = banner + source
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != generated:
        args.output.write_text(generated)


if __name__ == "__main__":
    main()

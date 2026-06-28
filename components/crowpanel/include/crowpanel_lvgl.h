// Optional LVGL integration for the CrowPanel Advance 7".
//
// Bridges the board's RGB panel and GT911 touch (already brought up by
// crowpanel_init()) to LVGL via Espressif's esp_lvgl_port. After calling
// crowpanel_lvgl_init() you get an lv_display_t and can use the normal LVGL
// API. LVGL runs in its own task; guard any LVGL calls made from other tasks
// with crowpanel_lvgl_lock()/crowpanel_lvgl_unlock().
//
// Usage:
//   crowpanel_init();
//   lv_display_t *disp = crowpanel_lvgl_init();
//   if (crowpanel_lvgl_lock(0)) {
//       lv_obj_t *label = lv_label_create(lv_screen_active());
//       lv_label_set_text(label, "Hello");
//       crowpanel_lvgl_unlock();
//   }

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize LVGL on top of the already-initialized board (call crowpanel_init
// first). Returns the LVGL display, or NULL on failure. Safe to call once.
lv_display_t *crowpanel_lvgl_init(void);

// Take the LVGL mutex before calling LVGL APIs from a task other than the
// LVGL task. timeout_ms = 0 waits forever. Returns true if acquired.
bool crowpanel_lvgl_lock(uint32_t timeout_ms);

// Release the LVGL mutex taken with crowpanel_lvgl_lock().
void crowpanel_lvgl_unlock(void);

#ifdef __cplusplus
}
#endif

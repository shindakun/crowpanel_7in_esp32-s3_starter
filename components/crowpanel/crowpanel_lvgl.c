// LVGL integration for the CrowPanel Advance 7.0, using esp_lvgl_port.
// See crowpanel_lvgl.h for the API.

#include "crowpanel_lvgl.h"
#include "crowpanel.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "crowpanel_lvgl";

static lv_display_t *s_disp = NULL;

lv_display_t *crowpanel_lvgl_init(void)
{
    if (s_disp) {
        return s_disp;
    }

    esp_lcd_panel_handle_t panel = crowpanel_panel_handle();
    esp_lcd_touch_handle_t touch = crowpanel_touch_handle();
    if (!panel) {
        ESP_LOGE(TAG, "crowpanel_init() must be called first");
        return NULL;
    }

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&port_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init failed");
        return NULL;
    }

    // For an RGB panel the LVGL draw buffers map onto the panel's own PSRAM
    // framebuffers. full_refresh + bb_mode bounce gives a clean, tear-light
    // result on this 800x480 panel.
    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = CROWPANEL_LCD_H_RES * CROWPANEL_LCD_V_RES,
        .double_buffer = true,
        .hres = CROWPANEL_LCD_H_RES,
        .vres = CROWPANEL_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_spiram = true,
                .full_refresh = true,
            },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags =
            {
                .bb_mode = true,
                .avoid_tearing = true,
            },
    };

    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed");
        return NULL;
    }

    if (touch) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = s_disp,
            .handle = touch,
        };
        if (!lvgl_port_add_touch(&touch_cfg)) {
            ESP_LOGW(TAG, "lvgl_port_add_touch failed; display only");
        }
    }

    ESP_LOGI(TAG, "LVGL ready");
    return s_disp;
}

bool crowpanel_lvgl_lock(uint32_t timeout_ms)
{
    // esp_lvgl_port takes -1 to mean "wait forever".
    return lvgl_port_lock(timeout_ms == 0 ? -1 : (int)timeout_ms);
}

void crowpanel_lvgl_unlock(void)
{
    lvgl_port_unlock();
}

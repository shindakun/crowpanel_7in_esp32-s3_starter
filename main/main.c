// CrowPanel Advance 7.0-HMI (ESP32-S3): board self-test / starter template.
//
// Proves the board on first flash using the full base stack:
//   - crowpanel_init():      RGB panel + GT911 touch + backlight (STC8H1K28)
//   - crowpanel_lvgl_init(): LVGL 9 via esp_lvgl_port
//   - an LVGL UI: a counter label and a button that increments on touch
//   - crowpanel_set_brightness(): a slider that dims the backlight live
//   - sdcard_mount(): mounts the microSD and writes a test file, showing the
//     result on screen (skipped gracefully if no card is inserted)
//
// Wi-Fi/NVS helpers (net.h) are part of the base too; see net_wifi_connect().
// To start a real project, replace build_ui() and app_main below.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "crowpanel.h"
#include "crowpanel_lvgl.h"
#include "sdcard.h"
// #include "net.h"   // net_wifi_connect("ssid", "pass", 15000);

static const char *TAG = "demo";

static lv_obj_t *s_counter_label;
static lv_obj_t *s_sd_label;
static int s_count = 0;

static void btn_event_cb(lv_event_t *e)
{
    (void)e;
    s_count++;
    lv_label_set_text_fmt(s_counter_label, "Touches: %d", s_count);
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    crowpanel_set_brightness((uint8_t)val);
}

// Build a small LVGL UI. Caller must hold the LVGL lock.
static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x103040), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CrowPanel Advance 7\"");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    s_counter_label = lv_label_create(scr);
    lv_label_set_text(s_counter_label, "Touches: 0");
    lv_obj_set_style_text_color(s_counter_label, lv_color_white(), 0);
    lv_obj_align(s_counter_label, LV_ALIGN_CENTER, 0, -80);

    s_sd_label = lv_label_create(scr);
    lv_label_set_text(s_sd_label, "SD: checking...");
    lv_obj_set_style_text_color(s_sd_label, lv_color_white(), 0);
    lv_obj_align(s_sd_label, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 220, 80);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Tap me");
    lv_obj_center(btn_label);

    lv_obj_t *slider_label = lv_label_create(scr);
    lv_label_set_text(slider_label, "Brightness");
    lv_obj_set_style_text_color(slider_label, lv_color_white(), 0);
    lv_obj_align(slider_label, LV_ALIGN_BOTTOM_MID, 0, -90);

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_width(slider, 400);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, 100, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

// Mount the SD card, write+read back a test file, and report on the UI.
static void sd_selftest(void)
{
    char status[64];
    if (sdcard_mount() != ESP_OK) {
        snprintf(status, sizeof(status), "SD: no card / mount failed");
    } else {
        FILE *f = fopen(SDCARD_MOUNT_POINT "/crowpanel_test.txt", "w");
        if (f) {
            fprintf(f, "hello from crowpanel\n");
            fclose(f);
        }
        uint64_t total = 0, used = 0;
        sdcard_usage(&total, &used);
        snprintf(status, sizeof(status), "SD: OK  %lluMB used / %lluMB", used / (1024 * 1024), total / (1024 * 1024));
        ESP_LOGI(TAG, "%s", status);
    }
    if (crowpanel_lvgl_lock(0)) {
        lv_label_set_text(s_sd_label, status);
        crowpanel_lvgl_unlock();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CrowPanel Advance 7\" self-test (LVGL)");

    ESP_ERROR_CHECK(crowpanel_init());

    if (!crowpanel_lvgl_init()) {
        ESP_LOGE(TAG, "LVGL init failed");
        return;
    }

    // LVGL runs in its own task; hold the lock while building the UI.
    if (crowpanel_lvgl_lock(0)) {
        build_ui();
        crowpanel_lvgl_unlock();
    }

    sd_selftest();

    // Nothing else to do here: esp_lvgl_port drives rendering and input.
    // Your application logic can run in this task or its own.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

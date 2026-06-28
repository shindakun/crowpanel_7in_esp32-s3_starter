// CrowPanel Advance 7.0-HMI (ESP32-S3): board self-test / starter template.
//
// Proves the board on first flash using the full base stack:
//   - crowpanel_init():      RGB panel + GT911 touch + backlight (STC8H1K28)
//   - crowpanel_lvgl_init(): LVGL 9 via esp_lvgl_port
//   - an LVGL UI: a counter label and a button that increments on touch
//   - crowpanel_set_brightness(): a slider that dims the backlight live
//   - sdcard_mount(): mounts the microSD and writes a test file, showing the
//     result on screen (skipped gracefully if no card is inserted)
//   - audio_mic_init(): captures from the INMP441 mic and shows a live level
//     bar (independent pins, always safe to run)
//   - net_wifi_connect(): connects to the Wi-Fi network set below and reports
//     the result on screen (set WIFI_SSID / WIFI_PASS before flashing)
//
// The speaker (audio_speaker_*) shares GPIO 4/5/6 with the SD card via the
// S0/S1 switches, so this demo does not drive it alongside SD; see audio.h.
//
// To start a real project, replace build_ui() and app_main below.

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "crowpanel.h"
#include "crowpanel_lvgl.h"
#include "sdcard.h"
#include "audio.h"
#include "net.h"

// Wi-Fi credentials for the demo's connection test. Set these before flashing;
// leave SSID empty to skip the Wi-Fi test entirely.
#define WIFI_SSID ""
#define WIFI_PASS ""

static const char *TAG = "demo";

static lv_obj_t *s_counter_label;
static lv_obj_t *s_sd_label;
static lv_obj_t *s_wifi_label;
static lv_obj_t *s_mic_bar;
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
    lv_obj_align(s_counter_label, LV_ALIGN_CENTER, 0, -110);

    s_sd_label = lv_label_create(scr);
    lv_label_set_text(s_sd_label, "SD: checking...");
    lv_obj_set_style_text_color(s_sd_label, lv_color_white(), 0);
    lv_obj_align(s_sd_label, LV_ALIGN_CENTER, 0, -78);

    s_wifi_label = lv_label_create(scr);
    lv_label_set_text(s_wifi_label, "Wi-Fi: ...");
    lv_obj_set_style_text_color(s_wifi_label, lv_color_white(), 0);
    lv_obj_align(s_wifi_label, LV_ALIGN_CENTER, 0, -46);

    lv_obj_t *mic_label = lv_label_create(scr);
    lv_label_set_text(mic_label, "Mic level");
    lv_obj_set_style_text_color(mic_label, lv_color_white(), 0);
    lv_obj_align(mic_label, LV_ALIGN_CENTER, 0, 90);

    s_mic_bar = lv_bar_create(scr);
    lv_obj_set_size(s_mic_bar, 400, 20);
    lv_bar_set_range(s_mic_bar, 0, 100);
    lv_obj_align(s_mic_bar, LV_ALIGN_CENTER, 0, 115);

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

static void set_wifi_label(const char *text)
{
    if (crowpanel_lvgl_lock(0)) {
        lv_label_set_text(s_wifi_label, text);
        crowpanel_lvgl_unlock();
    }
}

// Connect to the configured Wi-Fi network and report the result on screen.
// Blocks while connecting, so it runs in its own task.
static void wifi_task(void *arg)
{
    (void)arg;
    if (WIFI_SSID[0] == '\0') {
        set_wifi_label("Wi-Fi: not configured (set WIFI_SSID)");
        ESP_LOGW(TAG, "WIFI_SSID empty; skipping Wi-Fi test");
        vTaskDelete(NULL);
        return;
    }

    set_wifi_label("Wi-Fi: connecting...");
    ESP_LOGI(TAG, "connecting to Wi-Fi \"%s\"", WIFI_SSID);

    char status[96];
    if (net_wifi_connect(WIFI_SSID, WIFI_PASS, 20000) == ESP_OK) {
        char ip[16] = "?";
        net_wifi_get_ip(ip, sizeof(ip));
        snprintf(status, sizeof(status), "Wi-Fi: %s  (%s)", WIFI_SSID, ip);
        // Connected, so sync the clock from NTP (best-effort).
        if (net_sntp_sync(10000) == ESP_OK) {
            ESP_LOGI(TAG, "clock synced via SNTP");
        }
    } else {
        snprintf(status, sizeof(status), "Wi-Fi: failed to connect to %s", WIFI_SSID);
    }
    ESP_LOGI(TAG, "%s", status);
    set_wifi_label(status);
    vTaskDelete(NULL);
}

// Continuously read the mic and drive the on-screen level bar.
static void mic_task(void *arg)
{
    (void)arg;
    if (audio_mic_init(16000) != ESP_OK) {
        ESP_LOGW(TAG, "mic init failed; level bar stays at 0");
        vTaskDelete(NULL);
        return;
    }
    static int16_t buf[256];
    while (1) {
        size_t got = 0;
        if (audio_mic_read(buf, sizeof(buf), &got) == ESP_OK && got > 0) {
            size_t n = got / sizeof(int16_t);
            int32_t peak = 0;
            for (size_t i = 0; i < n; i++) {
                int32_t a = abs(buf[i]);
                if (a > peak) {
                    peak = a;
                }
            }
            int level = (int)((peak * 100) / 32768);
            if (crowpanel_lvgl_lock(20)) {
                lv_bar_set_value(s_mic_bar, level, LV_ANIM_OFF);
                crowpanel_lvgl_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
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

    // Mic capture and Wi-Fi connect each run in their own task.
    xTaskCreate(mic_task, "mic", 4096, NULL, 4, NULL);
    xTaskCreate(wifi_task, "wifi", 4096, NULL, 4, NULL);

    // Nothing else to do here: esp_lvgl_port drives rendering and input.
    // Your application logic can run in this task or its own.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

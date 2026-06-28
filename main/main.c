// CrowPanel Advance 7.0-HMI (ESP32-S3): board self-test / starter template.
//
// This demo exercises every part of the board so a freshly cloned project
// proves its hardware on the first flash:
//   - cycles the screen through solid colors (display + backlight)
//   - logs touch coordinates from the GT911 (touch)
//   - ramps the backlight brightness up and down (STC8H1K28 control MCU)
//
// All board details live in the reusable crowpanel component. To start a real
// project, replace the body of app_main below; crowpanel_init() and the API in
// crowpanel.h are all you need.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "crowpanel.h"

static const char *TAG = "demo";

void app_main(void)
{
    ESP_LOGI(TAG, "CrowPanel Advance 7.0 self-test");

    ESP_ERROR_CHECK(crowpanel_init());

    const uint16_t colors[] = {
        CROWPANEL_RGB565(255, 0, 0),    // red
        CROWPANEL_RGB565(0, 255, 0),    // green
        CROWPANEL_RGB565(0, 0, 255),    // blue
        CROWPANEL_RGB565(255, 255, 255),// white
        CROWPANEL_RGB565(0, 0, 0),      // black
    };
    const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
    const int num_colors = sizeof(colors) / sizeof(colors[0]);

    int ci = 0;
    int tick = 0;
    int brightness = 100;
    int bright_dir = -5;   // ramp brightness down then back up
    bool was_touched = false;

    while (1) {
        // Cycle the background color every ~2 seconds.
        if (tick % 40 == 0) {
            ESP_LOGI(TAG, "Fill: %s", names[ci]);
            crowpanel_fill(colors[ci]);
            ci = (ci + 1) % num_colors;
        }

        // Every ~0.5s, ramp the backlight to show set_brightness working.
        if (tick % 10 == 0) {
            brightness += bright_dir;
            if (brightness <= 10) { brightness = 10; bright_dir = 5; }
            if (brightness >= 100) { brightness = 100; bright_dir = -5; }
            crowpanel_set_brightness(brightness);
        }

        // Poll touch ~20Hz and log any points. While touched, drop to a low
        // brightness so you can feel touch affecting the backlight too.
        crowpanel_touch_point_t pts[5];
        uint8_t cnt = crowpanel_get_touches(pts, 5);
        if (cnt > 0) {
            for (int i = 0; i < cnt; i++) {
                ESP_LOGI(TAG, "Touch[%d] x=%u y=%u strength=%u", i, pts[i].x, pts[i].y, pts[i].strength);
            }
            if (!was_touched) {
                crowpanel_set_brightness(20);
                was_touched = true;
            }
        } else if (was_touched) {
            crowpanel_set_brightness(brightness);
            was_touched = false;
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

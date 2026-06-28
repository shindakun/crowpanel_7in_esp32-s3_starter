// Board support for the Elecrow CrowPanel Advance 7.0-HMI (ESP32-S3).
// Also sold rebranded (e.g. IoTeikXgo, Amazon B0GVDBN9RT).
//
//   ESP32-S3-WROOM-1 N16R8 (16MB flash / 8MB octal PSRAM)
//   7" IPS 800x480, SC7277 RGB driver, 16-bit RGB565 parallel bus
//   GT911 capacitive touch on I2C (SDA=15, SCL=16, addr 0x5D)
//   STC8H1K28 control MCU on I2C (addr 0x30): backlight + touch enable
//
// This is the V1.3+ board revision (STC8H1K28). Older V1.0/1.2 boards drive
// the backlight through a CH422G expander at 0x24/0x38 instead; see the README.
//
// Typical use:
//   crowpanel_init();                       // panel + touch + backlight on
//   crowpanel_fill(CROWPANEL_RGB565(0,0,0));
//   crowpanel_set_brightness(50);           // 0..100 %
//   crowpanel_touch_point_t p;
//   if (crowpanel_get_touch(&p)) { ... }

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CROWPANEL_LCD_H_RES 800
#define CROWPANEL_LCD_V_RES 480

// Pack 8-bit r,g,b into an RGB565 value.
#define CROWPANEL_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// A single touch point in panel pixel coordinates.
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t strength;
} crowpanel_touch_point_t;

// Initialize the RGB panel, the I2C bus, the STC8H control MCU (backlight on +
// touch enabled), and the GT911 touch controller. Call once at startup.
esp_err_t crowpanel_init(void);

// Fill the entire screen with one RGB565 color.
void crowpanel_fill(uint16_t color);

// Draw an RGB565 bitmap into the rectangle [x_start,x_end) x [y_start,y_end).
// Thin wrapper over esp_lcd_panel_draw_bitmap for custom rendering.
void crowpanel_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *pixels);

// Set backlight brightness as a percentage, 0 (off) to 100 (full).
// The underlying STC8H1K28 uses an inverted 0..245 scale; this maps for you.
void crowpanel_set_brightness(uint8_t percent);

// Read the first touch point. Returns true and fills *out if the screen is
// being touched, false otherwise. Pass NULL to just poll/refresh state.
bool crowpanel_get_touch(crowpanel_touch_point_t *out);

// Read up to max_points touch points. Returns the number reported (0..5).
uint8_t crowpanel_get_touches(crowpanel_touch_point_t *out, uint8_t max_points);

// Low-level handles, for advanced use (e.g. wiring up LVGL directly).
esp_lcd_panel_handle_t crowpanel_panel_handle(void);
esp_lcd_touch_handle_t crowpanel_touch_handle(void);

#ifdef __cplusplus
}
#endif

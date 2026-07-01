// Board support implementation for the CrowPanel Advance 7.0-HMI (ESP32-S3).
// See crowpanel.h for the public API and board details.
//
// Pin map and RGB timings are taken verbatim from Elecrow's official
// LovyanGFX_Driver.h; the STC8H1K28 backlight/touch protocol is from their
// V1.3+ ESP-IDF example (main.c writes single command bytes to I2C 0x30).

#include "crowpanel.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"

static const char *TAG = "crowpanel";

#define LCD_H_RES CROWPANEL_LCD_H_RES
#define LCD_V_RES CROWPANEL_LCD_V_RES

// ---- RGB565 data pins (from Elecrow LovyanGFX_Driver.h) ----
// LovyanGFX d0..d4 = Blue, d5..d10 = Green, d11..d15 = Red.
// esp_lcd RGB panel expects data_gpio_nums[0..15] = B0..B4,G0..G5,R0..R4.
#define PIN_B0 21
#define PIN_B1 47
#define PIN_B2 48
#define PIN_B3 45
#define PIN_B4 38
#define PIN_G0 9
#define PIN_G1 10
#define PIN_G2 11
#define PIN_G3 12
#define PIN_G4 13
#define PIN_G5 14
#define PIN_R0 7
#define PIN_R1 17
#define PIN_R2 18
#define PIN_R3 3
#define PIN_R4 46

#define PIN_DE 42
#define PIN_VSYNC 41
#define PIN_HSYNC 40
#define PIN_PCLK 39
#define PIN_DISP -1 // not wired to a GPIO on this board

#define PCLK_HZ (16 * 1000 * 1000) // conservative; Elecrow drives ~21MHz

// ---- GT911 touch I2C ----
#define PIN_TOUCH_SDA 15
#define PIN_TOUCH_SCL 16
#define PIN_TOUCH_RST -1 // reset handled by the STC8H control MCU, not a GPIO
#define PIN_TOUCH_INT -1
#define GT911_I2C_ADDR ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS // 0x5D

// ---- STC8H1K28 control MCU (board V1.3+) ----
// Single command byte written to I2C 0x30:
//   250 = enable touch (release GT911 reset)
//   0   = backlight full brightness; the scale is inverted, 245 = off.
//   246 = buzzer on, 247 = buzzer off (per the Elecrow CrowPanel GitHub).
#define STC8H_ADDR 0x30
#define STC8H_TOUCH_EN 250
#define STC8H_BL_MIN_VAL 245 // value that turns the backlight fully off
#define STC8H_BL_MAX_VAL 0   // value for full brightness
#define STC8H_BUZZER_ON 246  // passive buzzer on (fixed tone)
#define STC8H_BUZZER_OFF 247 // passive buzzer off

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;

// Send a single command byte to the STC8H1K28 control MCU at 0x30.
// Never fatal: a failed write must not crash the app (would boot-loop).
static void stc8h_cmd(uint8_t value)
{
    i2c_master_dev_handle_t dev = NULL;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = STC8H_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev) != ESP_OK) {
        ESP_LOGW(TAG, "STC8H add_device failed");
        return;
    }
    esp_err_t err = i2c_master_transmit(dev, &value, 1, 100);
    i2c_master_bus_rm_device(dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "STC8H write %u failed: %s", value, esp_err_to_name(err));
    }
}

void crowpanel_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    // Map 0..100 % onto the inverted 0..245 scale (0 = brightest, 245 = off).
    uint8_t value = STC8H_BL_MIN_VAL - (uint8_t)((STC8H_BL_MIN_VAL * (uint32_t)percent) / 100);
    stc8h_cmd(value);
}

void crowpanel_buzzer_set(bool on)
{
    stc8h_cmd(on ? STC8H_BUZZER_ON : STC8H_BUZZER_OFF);
}

void crowpanel_buzzer_beep(uint32_t duration_ms)
{
    stc8h_cmd(STC8H_BUZZER_ON);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    stc8h_cmd(STC8H_BUZZER_OFF);
}

void crowpanel_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *pixels)
{
    esp_lcd_panel_draw_bitmap(s_panel, x_start, y_start, x_end, y_end, pixels);
}

void crowpanel_fill(uint16_t color)
{
    // Draw row by row from a single-row buffer to keep RAM use small.
    static uint16_t *row = NULL;
    if (!row) {
        row = heap_caps_malloc(LCD_H_RES * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!row) {
            ESP_LOGE(TAG, "row buffer alloc failed");
            return;
        }
    }
    for (int x = 0; x < LCD_H_RES; x++) {
        row[x] = color;
    }
    for (int y = 0; y < LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + 1, row);
    }
}

uint8_t crowpanel_get_touches(crowpanel_touch_point_t *out, uint8_t max_points)
{
    if (!s_touch) {
        return 0;
    }
    esp_lcd_touch_read_data(s_touch);
    esp_lcd_touch_point_data_t data[5];
    uint8_t cnt = 0;
    if (esp_lcd_touch_get_data(s_touch, data, &cnt, 5) != ESP_OK) {
        return 0;
    }
    if (out) {
        uint8_t n = (cnt < max_points) ? cnt : max_points;
        for (uint8_t i = 0; i < n; i++) {
            out[i].x = data[i].x;
            out[i].y = data[i].y;
            out[i].strength = data[i].strength;
        }
    }
    return cnt;
}

bool crowpanel_get_touch(crowpanel_touch_point_t *out)
{
    crowpanel_touch_point_t p;
    uint8_t cnt = crowpanel_get_touches(&p, 1);
    if (cnt > 0 && out) {
        *out = p;
    }
    return cnt > 0;
}

esp_lcd_panel_handle_t crowpanel_panel_handle(void)
{
    return s_panel;
}
esp_lcd_touch_handle_t crowpanel_touch_handle(void)
{
    return s_touch;
}

static esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "Initializing RGB panel %dx%d", LCD_H_RES, LCD_V_RES);

    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .bits_per_pixel = 16,
        .psram_trans_align = 64,
        // Three PSRAM framebuffers for triple-buffered, tear-free LVGL output
        // (see crowpanel_lvgl.c). Two buffers flicker on redraw under DMA load;
        // the third removes the draw/scan-out overlap. No bounce buffer: it
        // conflicts with the multi-framebuffer avoid_tearing path.
        .num_fbs = 3,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = PIN_DISP,
        .pclk_gpio_num = PIN_PCLK,
        .vsync_gpio_num = PIN_VSYNC,
        .hsync_gpio_num = PIN_HSYNC,
        .de_gpio_num = PIN_DE,
        .data_gpio_nums =
            {
                PIN_B0,
                PIN_B1,
                PIN_B2,
                PIN_B3,
                PIN_B4,
                PIN_G0,
                PIN_G1,
                PIN_G2,
                PIN_G3,
                PIN_G4,
                PIN_G5,
                PIN_R0,
                PIN_R1,
                PIN_R2,
                PIN_R3,
                PIN_R4,
            },
        .timings =
            {
                .pclk_hz = PCLK_HZ,
                .h_res = LCD_H_RES,
                .v_res = LCD_V_RES,
                // Timings from Elecrow driver (SC7277).
                .hsync_back_porch = 8,
                .hsync_front_porch = 8,
                .hsync_pulse_width = 4,
                .vsync_back_porch = 8,
                .vsync_front_porch = 8,
                .vsync_pulse_width = 4,
                .flags =
                    {
                        .pclk_active_neg = true, // Elecrow uses pclk_idle_high; latch on falling edge
                    },
            },
        .flags =
            {
                .fb_in_psram = true, // 800*480*2 = 768KB framebuffer lives in PSRAM
            },
    };

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &s_panel);
    if (err != ESP_OK)
        return err;
    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK)
        return err;
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK)
        return err;
    ESP_LOGI(TAG, "RGB panel ready");
    return ESP_OK;
}

static esp_err_t init_i2c_and_touch(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus (SDA=%d SCL=%d)", PIN_TOUCH_SDA, PIN_TOUCH_SCL);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_TOUCH_SDA,
        .scl_io_num = PIN_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK)
        return err;

    // Enable touch and turn the backlight on via the STC8H1K28 at 0x30.
    // Must happen before GT911 init so the touch chip is out of reset.
    ESP_LOGI(TAG, "Enabling touch + backlight via STC8H1K28 (0x30)");
    stc8h_cmd(STC8H_TOUCH_EN);
    crowpanel_set_brightness(100);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.dev_addr = GT911_I2C_ADDR;
    err = esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io);
    if (err != ESP_OK)
        return err;

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = PIN_TOUCH_INT,
        .flags =
            {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
    };
    err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch);
    if (err != ESP_OK)
        return err;
    ESP_LOGI(TAG, "GT911 ready");
    return ESP_OK;
}

// Release anything crowpanel_init allocated. Safe to call on partial init.
static void crowpanel_teardown(void)
{
    if (s_touch) {
        esp_lcd_touch_del(s_touch);
        s_touch = NULL;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
}

esp_err_t crowpanel_init(void)
{
    esp_err_t err = init_display();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
        crowpanel_teardown();
        return err;
    }
    err = init_i2c_and_touch();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c/touch init failed: %s", esp_err_to_name(err));
        crowpanel_teardown();
        return err;
    }
    return ESP_OK;
}

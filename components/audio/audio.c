// Audio implementation for the CrowPanel Advance 7". See audio.h.
//
// Speaker output and mic input use the ESP-IDF v5 I2S standard-mode driver.
// The speaker amplifier is enabled via the STC8H1K28 control MCU on the shared
// I2C bus (0x30, command 248), the same MCU that drives backlight/touch.

#include "audio.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"

static const char *TAG = "audio";

// ---- Speaker pins (shared with SD via S0/S1; MIC & SPK position) ----
#define SPK_BCLK 5
#define SPK_LRCK 6
#define SPK_DOUT 4

// ---- Microphone pins (INMP441-style I2S, independent) ----
#define MIC_BCLK 19
#define MIC_WS 2
#define MIC_DIN 20

// ---- STC8H1K28 control MCU on the shared I2C bus ----
#define STC8H_ADDR 0x30
#define STC8H_SPK_ON 248 // enable the speaker amplifier
#define I2C_SDA 15
#define I2C_SCL 16

static i2s_chan_handle_t s_tx = NULL; // speaker
static i2s_chan_handle_t s_rx = NULL; // mic

// Send one command byte to the STC8H at 0x30 on a short-lived I2C bus handle.
// Non-fatal: a failed amp-enable should not crash audio init.
static void stc8h_cmd(uint8_t value)
{
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    // The bus may already exist (created by crowpanel_init); reuse it if so.
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        if (i2c_master_get_bus_handle(I2C_NUM_0, &bus) != ESP_OK || bus == NULL) {
            ESP_LOGW(TAG, "no I2C bus for STC8H amp control");
            return;
        }
    }
    i2c_master_dev_handle_t dev = NULL;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = STC8H_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) == ESP_OK) {
        esp_err_t err = i2c_master_transmit(dev, &value, 1, 100);
        i2c_master_bus_rm_device(dev);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "STC8H cmd %u failed: %s", value, esp_err_to_name(err));
        }
    }
}

esp_err_t audio_speaker_init(uint32_t sample_rate_hz)
{
    if (s_tx) {
        return ESP_OK;
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = SPK_BCLK,
                .ws = SPK_LRCK,
                .dout = SPK_DOUT,
                .din = I2S_GPIO_UNUSED,
            },
    };
    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }

    // Enable the on-board amplifier.
    stc8h_cmd(STC8H_SPK_ON);
    ESP_LOGI(TAG, "speaker ready @ %lu Hz (BCLK=%d WS=%d DOUT=%d)", (unsigned long)sample_rate_hz, SPK_BCLK, SPK_LRCK,
             SPK_DOUT);
    return ESP_OK;
}

esp_err_t audio_speaker_write(const void *data, size_t len, size_t *bytes_written)
{
    if (!s_tx) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_write(s_tx, data, len, bytes_written, portMAX_DELAY);
}

esp_err_t audio_speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!s_tx) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t rate = 16000;
    const int amp = 8000;
    const size_t frames = 256;
    int16_t buf[frames * 2]; // stereo interleaved
    uint32_t total_frames = (rate * duration_ms) / 1000;
    double phase = 0.0;
    double step = 2.0 * M_PI * (double)freq_hz / (double)rate;

    while (total_frames > 0) {
        size_t n = (total_frames < frames) ? total_frames : frames;
        for (size_t i = 0; i < n; i++) {
            int16_t s = (int16_t)(amp * sin(phase));
            phase += step;
            if (phase > 2.0 * M_PI) {
                phase -= 2.0 * M_PI;
            }
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
        }
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, buf, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        if (err != ESP_OK) {
            return err;
        }
        total_frames -= n;
    }
    return ESP_OK;
}

void audio_speaker_set_muted(bool muted)
{
    // The STC8H enables the amp with 248. We approximate mute by leaving the
    // amp on and writing silence is the caller's job; re-enabling on unmute
    // covers the common case. A dedicated mute command is not documented.
    if (!muted) {
        stc8h_cmd(STC8H_SPK_ON);
    }
}

esp_err_t audio_speaker_deinit(void)
{
    if (!s_tx) {
        return ESP_OK;
    }
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx = NULL;
    return ESP_OK;
}

esp_err_t audio_mic_init(uint32_t sample_rate_hz)
{
    if (s_rx) {
        return ESP_OK;
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx);
    if (err != ESP_OK) {
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = MIC_BCLK,
                .ws = MIC_WS,
                .dout = I2S_GPIO_UNUSED,
                .din = MIC_DIN,
            },
    };
    err = i2s_channel_init_std_mode(s_rx, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return err;
    }
    err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return err;
    }
    ESP_LOGI(TAG, "mic ready @ %lu Hz (BCLK=%d WS=%d DIN=%d)", (unsigned long)sample_rate_hz, MIC_BCLK, MIC_WS,
             MIC_DIN);
    return ESP_OK;
}

esp_err_t audio_mic_read(void *data, size_t len, size_t *bytes_read)
{
    if (!s_rx) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_read(s_rx, data, len, bytes_read, portMAX_DELAY);
}

esp_err_t audio_mic_deinit(void)
{
    if (!s_rx) {
        return ESP_OK;
    }
    i2s_channel_disable(s_rx);
    i2s_del_channel(s_rx);
    s_rx = NULL;
    return ESP_OK;
}

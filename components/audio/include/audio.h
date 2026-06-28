// Optional audio support for the CrowPanel Advance 7" (ESP32-S3).
//
// Speaker (I2S output via the on-board amplifier):
//   BCLK=IO5, LRCLK(WS)=IO6, DOUT=IO4. These pins are SHARED with the microSD
//   card via the S0/S1 DIP switches: the speaker only works in the MIC & SPK
//   position (S1=0, S0=0), and SD and speaker cannot be used at the same time.
//   The amplifier is enabled through the STC8H1K28 control MCU (I2C 0x30,
//   command 248).
//
// Microphone (PDM, independent pins, works alongside the speaker):
//   CLK=IO19, DATA=IO20 (a PDM mic, no WS line). Captured via PDM RX with the
//   hardware PDM-to-PCM filter, so audio_mic_read() returns 16-bit PCM.
//
// Usage:
//   audio_speaker_init(16000);                 // sample rate
//   audio_speaker_write(pcm, n_bytes);         // signed 16-bit mono/stereo
//   ...
//   audio_mic_init(16000);
//   int16_t buf[256]; size_t got;
//   audio_mic_read(buf, sizeof(buf), &got);

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Speaker (I2S output + amplifier) ----

// Initialize the speaker I2S output and enable the amplifier via the STC8H1K28.
// Requires the S0/S1 switches in the MIC & SPK position (conflicts with SD).
esp_err_t audio_speaker_init(uint32_t sample_rate_hz);

// Write interleaved signed 16-bit PCM to the speaker. Blocks until queued.
// Returns ESP_OK and sets *bytes_written (may be less than len on timeout).
esp_err_t audio_speaker_write(const void *data, size_t len, size_t *bytes_written);

// Play a simple sine tone for the given duration (blocking). Handy self-test.
esp_err_t audio_speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms);

// Unmute re-enables the amplifier (no dedicated STC8H mute command exists; to
// silence output, write silence or stop writing). Kept for API symmetry.
void audio_speaker_set_muted(bool muted);

// Tear down the speaker I2S and disable the amplifier.
esp_err_t audio_speaker_deinit(void);

// ---- Microphone (PDM input) ----

// Initialize the PDM microphone input.
esp_err_t audio_mic_init(uint32_t sample_rate_hz);

// Read signed 16-bit PCM samples from the mic. Blocks until data is available.
// Sets *bytes_read with the number of bytes actually read.
esp_err_t audio_mic_read(void *data, size_t len, size_t *bytes_read);

// Tear down the microphone I2S.
esp_err_t audio_mic_deinit(void);

#ifdef __cplusplus
}
#endif

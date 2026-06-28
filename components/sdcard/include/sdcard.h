// Optional SD card (microSD slot) support for the CrowPanel Advance 7".
//
// The slot is wired to a dedicated SPI bus (SPI2/HSPI): SCK=5, MISO=4, MOSI=6,
// CS=GPIO0. Note GPIO0 is also the strapping/BOOT pin; a card seated at boot is
// fine in practice, but avoid driving it during the reset window.
//
// This mounts a FAT filesystem under a VFS path (default "/sdcard") so you can
// use standard C file I/O (fopen/fread/fwrite/fclose) on the card.
//
// Usage:
//   if (sdcard_mount() == ESP_OK) {
//       FILE *f = fopen("/sdcard/hello.txt", "w");
//       fprintf(f, "hi");
//       fclose(f);
//   }

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDCARD_MOUNT_POINT "/sdcard"

// Mount the SD card as a FAT filesystem at SDCARD_MOUNT_POINT.
// Initializes the SPI bus on first call. Returns ESP_OK on success, or an
// error (e.g. ESP_FAIL if no card / mount failed). Safe to call once.
esp_err_t sdcard_mount(void);

// Unmount the card and free the SPI bus.
esp_err_t sdcard_unmount(void);

// Total and used capacity in bytes (0 if not mounted). Either pointer may be
// NULL. Returns ESP_OK if the values were filled.
esp_err_t sdcard_usage(uint64_t *total_bytes, uint64_t *used_bytes);

#ifdef __cplusplus
}
#endif

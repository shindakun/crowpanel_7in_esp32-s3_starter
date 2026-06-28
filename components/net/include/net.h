// Minimal networking helpers shared across projects on this board.
//
// net_nvs_init() brings up NVS (needed by Wi-Fi and by anything storing
// settings). net_wifi_connect() initializes the Wi-Fi stack in station mode,
// connects to the given AP, and blocks until an IP is obtained or it times out.

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize NVS flash, erasing and retrying if the partition is new or in a
// version it cannot read. Safe to call multiple times.
esp_err_t net_nvs_init(void);

// Connect to a Wi-Fi access point in station mode and wait for an IP.
// Calls net_nvs_init() and esp_netif/event-loop setup internally if needed.
// timeout_ms = 0 waits indefinitely. Returns ESP_OK once an IP is assigned.
esp_err_t net_wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

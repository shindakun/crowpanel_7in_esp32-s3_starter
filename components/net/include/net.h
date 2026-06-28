// Minimal networking helpers shared across projects on this board.
//
// net_nvs_init() brings up NVS (needed by Wi-Fi and by anything storing
// settings). net_wifi_connect() initializes the Wi-Fi stack in station mode,
// connects to the given AP, and blocks until an IP is obtained or it times out.
// After a successful connect the component auto-reconnects on drops until
// net_wifi_disconnect() is called.

#pragma once

#include <stdbool.h>
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
// timeout_ms = 0 waits indefinitely. Returns ESP_OK once an IP is assigned,
// ESP_ERR_TIMEOUT if it timed out, or ESP_FAIL after exhausting retries.
// Once connected, drops are auto-reconnected until net_wifi_disconnect().
esp_err_t net_wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms);

// Disconnect from Wi-Fi and stop auto-reconnect. Safe if not connected.
esp_err_t net_wifi_disconnect(void);

// True if currently associated and holding an IP.
bool net_wifi_is_connected(void);

// Copy the current IPv4 address as a string (e.g. "10.0.0.110") into out.
// Returns ESP_OK if connected and the buffer was filled, else an error.
// out_size should be at least 16 bytes.
esp_err_t net_wifi_get_ip(char *out, size_t out_size);

// Sync the system clock from NTP (SNTP). Must be connected first. Blocks up to
// timeout_ms for the first time update (0 = a default ~10s wait). Returns
// ESP_OK once the clock is set, ESP_ERR_TIMEOUT otherwise.
esp_err_t net_sntp_sync(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

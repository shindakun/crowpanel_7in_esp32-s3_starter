// Networking helpers. See net.h.

#include "net.h"

#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

static const char *TAG = "net";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_events = NULL;
static esp_netif_t *s_sta_netif = NULL;
static bool s_stack_inited = false;
static bool s_connected = false;      // currently holds an IP
static bool s_auto_reconnect = false; // keep reconnecting on drops
static int s_retry = 0;
#define MAX_RETRY 5

esp_err_t net_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_auto_reconnect) {
            // Already connected once: reconnect indefinitely on drops.
            esp_wifi_connect();
        } else if (s_retry < MAX_RETRY) {
            esp_wifi_connect();
            s_retry++;
            ESP_LOGI(TAG, "retry connect (%d/%d)", s_retry, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry = 0;
        s_connected = true;
        s_auto_reconnect = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t net_wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms)
{
    esp_err_t err = net_nvs_init();
    if (err != ESP_OK)
        return err;

    if (!s_stack_inited) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_sta_netif = esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        ESP_ERROR_CHECK(
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
        s_wifi_events = xEventGroupCreate();
        s_stack_inited = true;
    }

    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_retry = 0;
    s_auto_reconnect = false; // bounded retry during the initial connect

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));
    // Accept any auth mode at or above open (open/WPA/WPA2/WPA3), so this works
    // with open and WPA3-only networks, not just WPA2-PSK.
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    TickType_t wait = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, wait);
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    // On failure or timeout, stop so the stack is idle and a later call to
    // net_wifi_connect() starts cleanly (it re-arms s_retry and the event bits).
    ESP_LOGE(TAG, "failed to connect to \"%s\"", ssid);
    s_auto_reconnect = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    return (bits & WIFI_FAIL_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

esp_err_t net_wifi_disconnect(void)
{
    s_auto_reconnect = false;
    s_connected = false;
    if (!s_stack_inited) {
        return ESP_OK;
    }
    esp_wifi_disconnect();
    return esp_wifi_stop();
}

bool net_wifi_is_connected(void)
{
    return s_connected;
}

esp_err_t net_wifi_get_ip(char *out, size_t out_size)
{
    if (!out || out_size < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_connected || !s_sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_netif_ip_info_t ip_info;
    esp_err_t err = esp_netif_get_ip_info(s_sta_netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }
    snprintf(out, out_size, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

esp_err_t net_sntp_sync(uint32_t timeout_ms)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means it was already initialized; that's fine.
        return err;
    }
    uint32_t wait = (timeout_ms == 0) ? 10000 : timeout_ms;
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(wait));
    if (err == ESP_OK) {
        time_t now = time(NULL);
        ESP_LOGI(TAG, "time synced: %s", ctime(&now));
    } else {
        ESP_LOGW(TAG, "SNTP sync timed out");
    }
    return err;
}

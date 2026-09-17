#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#include "nvs_flash.h"

#define WIFI_SSID     "Proximus-Home-FC88"
#define WIFI_PASSWORD "wwfc2ycszazhh"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define PING_COUNT     10
#define PING_INTERVAL  1000
#define PING_TIMEOUT   1000

static const char *TAG = "wifi";

static EventGroupHandle_t wifi_event_group;
static int retry_count = 0;

#define MAX_RETRY 10


static void ping_gateway(esp_netif_t *netif)
{
    esp_netif_ip_info_t ip_info;

    ESP_ERROR_CHECK(
        esp_netif_get_ip_info(netif, &ip_info)
    );

    ESP_LOGI(TAG, "IP address : " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Netmask    : " IPSTR, IP2STR(&ip_info.netmask));
    ESP_LOGI(TAG, "Gateway    : " IPSTR, IP2STR(&ip_info.gw));

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();

    ping_config.target_addr.u_addr.ip4.addr = ip_info.gw.addr;
    ping_config.target_addr.type = ESP_IPADDR_TYPE_V4;

    // ping_config.count = ESP_PING_COUNT_INFINITE;
    ping_config.count = PING_COUNT;  // only 10 pings
    ping_config.interval_ms = PING_INTERVAL;
    ping_config.timeout_ms = PING_TIMEOUT;

    esp_ping_callbacks_t callbacks = {
        .on_ping_success = NULL,
        .on_ping_timeout = NULL,
        .on_ping_end = NULL,
    };

    esp_ping_handle_t ping;

    ESP_ERROR_CHECK(
        esp_ping_new_session(
            &ping_config,
            &callbacks,
            &ping
        )
    );

    ESP_LOGI(TAG, "Starting %d ping to gateway...", PING_COUNT);

    ESP_ERROR_CHECK(
        esp_ping_start(ping)
    );

    ESP_LOGI(TAG, "Ping done.");
}


static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to AP...");
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {

        if (retry_count < MAX_RETRY) {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGW(TAG, "Retrying connection (%d/%d)...", retry_count, MAX_RETRY);

        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }

    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Connected!");
        ESP_LOGI(TAG, "IP address: " IPSTR, IP2STR(&event->ip_info.ip));

        retry_count = 0;

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

        // perform ping if connected
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            ping_gateway(netif);
        }
    }
}

static void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,

            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    ESP_LOGI(TAG, "Wi-Fi initialization completed.");
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connection established.");

    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi.");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
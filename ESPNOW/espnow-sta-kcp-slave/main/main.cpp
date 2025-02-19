#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include <freertos/task.h>
#include <nvs_flash.h>
#include <esp_mac.h>

#include "esp_log.h"
#include "wireless.h"
#include "PairInfo.h"
#include "led.h"
#include "magic_enum.hpp"
#include "ikcp.h"
#include "kcp.h"

static const char *const TAG = "main";

extern "C" void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    configure_led();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI("main", "mac: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Wireless::GetInstance();
    pairInfo.status = PairInfo::pair_status_t::BROADCASTING;
    uint8_t b_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t msg[] = R"({"Hello": "World"})";
    esp_now_send(b_addr, msg, sizeof(msg));
    vTaskDelay(pdMS_TO_TICKS(5000));
    if (pairInfo.GetConv()) {
        pairInfo.status = PairInfo::pair_status_t::PAIRED;
    } else {
        pairInfo.status = PairInfo::pair_status_t::NOT_PAIRED;
        ESP_LOGI(TAG, "broadcast done, pair failed");
        return;
    }
    ESP_LOGI(TAG, "broadcast done, status: %s", magic_enum::enum_name(pairInfo.status).data());

    xTaskCreate(KCP_Task, "KCP_Task", 4096, NULL, 5, NULL);
}


#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include <freertos/task.h>
#include <nvs_flash.h>
#include <esp_mac.h>

#include "esp_log.h"
#include "wireless.h"
#include "PairInfo.h"
#include "led.h"

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
    pairInfo.status = PairInfo::PAIRING;
    // we use master mac[3], mac[4], mac[5] and 1byte state as kcp conv
    while (1) {
        ESP_LOGI("main", "Hello World! This is Master.");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

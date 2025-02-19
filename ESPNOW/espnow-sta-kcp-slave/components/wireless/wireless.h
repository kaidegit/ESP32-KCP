//
// Created by yekai on 2025/2/13.
//

#ifndef ESPNOW_STA_KCP_MASTER_WIRELESS_H
#define ESPNOW_STA_KCP_MASTER_WIRELESS_H

#include <array>
#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class Wireless {
public:

    enum class wl_protocol_t : uint8_t {
        KCP = 0,
    };

    using espnow_event_id_t = enum {
        ESPNOW_SEND_CB,
        ESPNOW_RECV_CB,
    };

    using espnow_event_send_cb_t = struct {
        uint8_t src_addr[6];
        esp_now_send_status_t status;
    };

    using espnow_event_recv_cb_t = struct {
        uint8_t src_addr[6];
        uint8_t *data;
        int data_len;
    };

    using espnow_event_info_t = union {
        espnow_event_send_cb_t send_cb;
        espnow_event_recv_cb_t recv_cb;
    };

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
    using espnow_event_t = struct {
        espnow_event_id_t id;
        espnow_event_info_t info;
    };

    static Wireless *GetInstance() {
        if (instance == nullptr) {
            instance = new Wireless();
            instance->Init();
        }
        return instance;
    }

    virtual esp_err_t Init() {
        if (ESP_OK != InitWiFi(WIFI_MODE_STA, 6)) {
            return ESP_FAIL;
        }
        if (ESP_OK != InitEspNow()) {
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    virtual esp_err_t InitWiFi(wifi_mode_t wifi_mode, uint8_t wifi_channel);

    virtual esp_err_t InitEspNow();

    esp_err_t espnow_pair_helper(std::array<uint8_t, 6> mac);

    static void espnow_event_task(void *parameter);

    static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);

    static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);

    static constexpr const std::array<uint8_t, ESP_NOW_ETH_ALEN> broadcast_mac =
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
private:
    Wireless() = default;

    static Wireless *instance;

    wifi_interface_t ifidx;
    uint8_t channel;

    QueueHandle_t espnow_event_queue = NULL;

    static constexpr const char *const TAG = "Wireless";
    static constexpr const char *const pmk = "yekai01234567890";    // 16字符
};

#endif //ESPNOW_STA_KCP_MASTER_WIRELESS_H

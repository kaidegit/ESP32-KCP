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

    enum {
        EXAMPLE_ESPNOW_DATA_BROADCAST,
        EXAMPLE_ESPNOW_DATA_UNICAST,
        EXAMPLE_ESPNOW_DATA_MAX,
    };

    typedef struct {
        uint8_t type;                         //Broadcast or unicast ESPNOW data.
        uint8_t state;                        //Indicate that if has received broadcast ESPNOW data or not.
        uint16_t seq_num;                     //Sequence number of ESPNOW data.
        uint16_t crc;                         //CRC16 value of ESPNOW data.
        uint32_t magic;                       //Magic number which is used to determine which device to send unicast ESPNOW data.
        uint8_t payload[0];                   //Real payload of ESPNOW data.
    } __attribute__((packed)) example_espnow_data_t;

/* Parameters of sending ESPNOW data. */
    typedef struct {
        bool unicast;                         //Send unicast ESPNOW data.
        bool broadcast;                       //Send broadcast ESPNOW data.
        uint8_t state;                        //Indicate that if has received broadcast ESPNOW data or not.
        uint32_t magic;                       //Magic number which is used to determine which device to send unicast ESPNOW data.
        uint16_t count;                       //Total count of unicast ESPNOW data to be sent.
        uint16_t delay;                       //Delay between sending two ESPNOW data, unit: ms.
        int len;                              //Length of ESPNOW data to be sent, unit: byte.
        uint8_t *buffer;                      //Buffer pointing to ESPNOW data.
        uint8_t dest_mac[ESP_NOW_ETH_ALEN];   //MAC address of destination device.
    } espnow_send_param_t;

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

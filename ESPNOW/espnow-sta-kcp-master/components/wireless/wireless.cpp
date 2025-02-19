#include <esp_netif.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <cstring>
#include <esp_random.h>
#include "esp_mac.h"
#include <memory>
#include <ikcp.h>
#include "wireless.h"
#include "esp_log.h"
#include "PairInfo.h"
#include "cJSON.h"
#include "span"
#include "magic_enum.hpp"
#include "led.h"

Wireless *Wireless::instance = nullptr;
extern SemaphoreHandle_t kcp_mutex;
extern ikcpcb *kcp;

esp_err_t Wireless::InitWiFi(wifi_mode_t wifi_mode, uint8_t wifi_channel) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(wifi_mode));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE));

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK( esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
#endif

    ifidx = wifi_mode == WIFI_MODE_STA ? WIFI_IF_STA : WIFI_IF_AP;
    channel = wifi_channel;

    return ESP_OK;
}

esp_err_t Wireless::InitEspNow() {
    espnow_event_queue = xQueueCreate(10, sizeof(espnow_event_t));
    if (espnow_event_queue == NULL) {
        ESP_LOGE(TAG, "Create espnow_event_queue fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
//    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(Wireless::espnow_recv_cb));
#if CONFIG_ESPNOW_ENABLE_POWER_SAVE
    ESP_ERROR_CHECK( esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW) );
    ESP_ERROR_CHECK( esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL) );
#endif
    /* Set primary master key. */
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *) pmk));

    auto paired_mac = pairInfo.GetPairedMac();

    if (paired_mac != broadcast_mac) {
        ESP_LOGI(TAG, "Get paired peer " MACSTR "", MAC2STR(paired_mac.data()));
        if (ESP_OK != espnow_pair_helper(paired_mac)) {
            goto __failed;
        }
        ESP_LOGI(TAG, "Add paired peer success");
    } else {
        ESP_LOGI(TAG, "No saved peer");
    }

    if (ESP_OK != espnow_pair_helper(broadcast_mac)) {
        goto __failed;
    }
    ESP_LOGI(TAG, "Add broadcast peer success");

    xTaskCreate(
        espnow_event_task,
        "example_espnow_task",
        4096,
        nullptr,
        4,
        NULL
    );

    return ESP_OK;

    __failed:

    vQueueDelete(espnow_event_queue);
    esp_now_deinit();
    return ESP_FAIL;
}

void Wireless::espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    espnow_event_t evt;
    espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = ESPNOW_SEND_CB;
    memcpy(send_cb->src_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(Wireless::GetInstance()->espnow_event_queue, &evt, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

void Wireless::espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    espnow_event_t evt;
    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

    if (recv_info->src_addr == NULL || recv_info->des_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    if (memcmp(recv_info->des_addr, broadcast_mac.data(), ESP_NOW_ETH_ALEN) == 0) {
        /* If added a peer with encryption before, the receive packets may be
         * encrypted as peer-to-peer message or unencrypted over the broadcast channel.
         * Users can check the destination address to distinguish it.
         */
        ESP_LOGI(TAG, "Receive broadcast ESPNOW data");
    } else {
        ESP_LOGI(TAG, "Receive unicast ESPNOW data");
    }

    evt.id = ESPNOW_RECV_CB;
    memcpy(recv_cb->src_addr, recv_info->src_addr, ESP_NOW_ETH_ALEN);
    memcpy(recv_cb->des_addr, recv_info->des_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = (uint8_t *) malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(Wireless::GetInstance()->espnow_event_queue, &evt, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

void Wireless::espnow_event_task(void *parameter) {
    auto instance = GetInstance();
    espnow_event_t event;
    while (1) {
        if (xQueueReceive(instance->espnow_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            switch (event.id) {
            case ESPNOW_SEND_CB: {  // 发送后收到发送完成/失败消息
            }
                break;
            case ESPNOW_RECV_CB: {  // 收到数据
                ESP_LOGI(TAG, "Recv Data when PairStatus: %s", magic_enum::enum_name(pairInfo.status).data());
                switch (pairInfo.status) {
                    using
                    enum PairInfo::pair_status_t;
                case PAIRING: {
                    if ((memcmp(event.info.recv_cb.des_addr, broadcast_mac.data(), 6) == 0) and
                        (event.info.recv_cb.data_len > 0)) {
                        auto msg = std::string_view(
                            reinterpret_cast<char *>(event.info.recv_cb.data),
                            event.info.recv_cb.data_len
                        );
                        ESP_LOGI(TAG, "Receive broadcast ESPNOW data from " MACSTR,
                                 MAC2STR(event.info.recv_cb.src_addr));
                        ESP_LOGI(TAG, "Recv %.*s", msg.size(), msg.data());
                        auto json = cJSON_Parse(msg.data());
                        if (!json) {
                            ESP_LOGW(TAG, "Parse json fail");
                            cJSON_Delete(json);
                            break;
                        }
                        auto hello_field = cJSON_GetObjectItem(json, "Hello");
                        if (!hello_field) {
                            ESP_LOGW(TAG, "No Hello field");
                            cJSON_Delete(json);
                            break;
                        }
//                        if ((hello_field->type != cJSON_String) or
//                            (strcmp(hello_field->valuestring, "World") != 0)) {
//                            ESP_LOGW(TAG, "Hello field is not World");
//                            cJSON_Delete(json);
//                            break;
//                        }
                        if ((hello_field->type == cJSON_String) and
                            (strcmp(hello_field->valuestring, "OK") == 0)) {
                            ESP_LOGI(TAG, "Recv Hello OK, pairing done");
                            cJSON_Delete(json);
                            pairInfo.status = PAIRED;
                            break;
                        }
                        if (memcmp(event.info.recv_cb.src_addr, pairInfo.GetPairedMac().data(), 6) == 0) {
                            ESP_LOGI(TAG, "peer already paired");
                        } else {
                            auto ret = pairInfo.SavePairedMac(std::span<uint8_t, 6>(event.info.recv_cb.src_addr, 6));
                            if (ret != ESP_OK) {
                                ESP_LOGW(TAG, "Save paired mac fail");
                            } else {
                                ESP_LOGI(TAG, "Paired with " MACSTR, MAC2STR(pairInfo.GetPairedMac().data()));
                            }
                            instance->espnow_pair_helper(pairInfo.GetPairedMac());
                        }
                        auto conv = PairInfo::CreateConv(pairInfo.GetPairedMac());
                        pairInfo.SetConv(conv);
                        // send Hello World back to let the other device know we are paired
                        auto rsp_json = cJSON_CreateObject();
                        cJSON_AddStringToObject(rsp_json, "protocol", "kcp");
                        cJSON_AddNumberToObject(rsp_json, "conv", conv);
                        auto _msg = cJSON_PrintUnformatted(rsp_json);
                        esp_now_send(pairInfo.GetPairedMac().data(), (const uint8_t *) _msg, strlen(_msg));
                        cJSON_Delete(rsp_json);
                        cJSON_free(_msg);
                        blink_led();
                    }
                }
                    break;
                case PAIRED: {
                    auto msg = std::string_view(
                        reinterpret_cast<char *>(event.info.recv_cb.data),
                        event.info.recv_cb.data_len
                    );
                    ESP_LOGI(TAG, "Receive ESPNOW data from " MACSTR,
                             MAC2STR(event.info.recv_cb.src_addr));
                    if (!kcp_mutex or !kcp) {
                        ESP_LOGW(TAG, "KCP not initialized");
                        break;
                    }
                    xSemaphoreTake(kcp_mutex, portMAX_DELAY);
                    ikcp_input(kcp, (const char *) msg.data(), msg.size());
                    xSemaphoreGive(kcp_mutex);
                }
                    break;
                default:
                    break;
                }
                free(event.info.recv_cb.data);
            }
                break;
            }
        }
    }
}

esp_err_t Wireless::espnow_pair_helper(std::array<uint8_t, 6> mac) {
    auto peer = std::make_unique<esp_now_peer_info_t>();

    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        return ESP_ERR_ESPNOW_NO_MEM;
    }
    memset(peer.get(), 0, sizeof(esp_now_peer_info_t));
    peer->channel = channel;
    peer->ifidx = ifidx;
    peer->encrypt = false;
    memcpy(peer->peer_addr, mac.data(), ESP_NOW_ETH_ALEN);

    return esp_now_add_peer(peer.get());
}
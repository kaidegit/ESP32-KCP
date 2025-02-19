#include <esp_netif.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <cstring>
#include <esp_random.h>
#include <memory>
#include <ikcp.h>
#include "wireless.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "PairInfo.h"
#include "cJSON.h"
#include "span"
#include "led.h"
#include "magic_enum.hpp"

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
    ESP_ERROR_CHECK(esp_now_register_send_cb(Wireless::espnow_send_cb));
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
                espnow_event_send_cb_t *send_cb = &event.info.send_cb;
                ESP_LOGI(TAG, "Send data success");
                switch (pairInfo.status) {
                    using
                    enum PairInfo::pair_status_t;
                case NOT_PAIRED:
                    break;
                case PAIRING:
                    break;
                case PAIRED:
                    break;
                case BROADCASTING: {
                    // send broadcast again after 500ms
                    auto tmr_h = xTimerCreate(
                        "broadcast_timer",
                        500,
                        pdFALSE,
                        NULL,
                        [](TimerHandle_t xTimer) {
                            if (pairInfo.status == BROADCASTING) {
                                ESP_LOGI(TAG, "Broadcast again");
                                static uint8_t msg[] = R"({"Hello": "World"})";
                                esp_now_send(broadcast_mac.data(), msg, sizeof(msg));
                                blink_led();
                            }
                        }
                    );
                    xTimerStart(tmr_h, portMAX_DELAY);
                }
                    break;
                }
            }
                break;
            case ESPNOW_RECV_CB: {  // 收到数据
                ESP_LOGI(TAG, "Recv Data when PairStatus: %s", magic_enum::enum_name(pairInfo.status).data());
                switch (pairInfo.status) {
                    using
                    enum PairInfo::pair_status_t;
                case BROADCASTING: {
                    auto msg = std::string_view(
                        reinterpret_cast<char *>(event.info.recv_cb.data),
                        event.info.recv_cb.data_len
                    );
                    ESP_LOGI(TAG, "Receive ESPNOW data from " MACSTR,
                             MAC2STR(event.info.recv_cb.src_addr));
                    ESP_LOGI(TAG, "Recv %.*s", msg.size(), msg.data());

                    auto json = cJSON_Parse(msg.data());
                    if (!json) {
                        ESP_LOGW(TAG, "Parse json fail");
                        cJSON_Delete(json);
                        break;
                    }
                    auto protocol_field = cJSON_GetObjectItem(json, "protocol");
                    if (!protocol_field) {
                        ESP_LOGW(TAG, "No protocol field");
                        cJSON_Delete(json);
                        break;
                    }
                    if (protocol_field->type != cJSON_String) {
                        ESP_LOGW(TAG, "protocol type error");
                        cJSON_Delete(json);
                        break;
                    }
                    auto protocol_p = protocol_field->valuestring;
                    auto protocol_str = std::string(protocol_p);
                    // to upper
                    for (auto &c: protocol_str) {
                        c = (char) toupper(c);
                    }
                    auto pro = magic_enum::enum_cast<wl_protocol_t>(protocol_str);
                    if (!pro.has_value()) {
                        ESP_LOGW(TAG, "Invalid protocol");
                        cJSON_Delete(json);
                        break;
                    }
                    auto protocol = pro.value();
                    switch (protocol) {
                    case wl_protocol_t::KCP: {
                        auto conv_field = cJSON_GetObjectItem(json, "Conv");
                        if (!conv_field) {
                            ESP_LOGW(TAG, "No conv field");
                            cJSON_Delete(json);
                            break;
                        }
                        if (conv_field->type != cJSON_Number) {
                            ESP_LOGW(TAG, "Conv type error");
                            cJSON_Delete(json);
                            break;
                        }
                        auto conv = conv_field->valueint;
                        pairInfo.SetConv(conv);
                        ESP_LOGI(TAG, "Conv set to 0x%x", conv);
                    }
                        break;
                    default:
                        break;
                    }
                    cJSON_free(json);

                    // send `{"Hello": "OK"}` back to notify the sender
                    static uint8_t rsp_msg[] = R"({"Hello": "OK"})";
                    esp_now_send(broadcast_mac.data(), rsp_msg, sizeof(rsp_msg));
                }
                    break;
                case PAIRING: {
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

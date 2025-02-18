//
// Created by yekai on 2025/2/14.
//

#include <string>
#include "PairInfo.h"
#include "esp_err.h"
#include <nvs_handle.hpp>
#include <cstring>
#include <esp_mac.h>
#include <esp_random.h>
#include "wireless.h"
#include "esp_log.h"

PairInfo pairInfo;

static bool is_valid_mac(std::string_view mac) {
    if (mac.size() != 17) return false;  // 检查长度是否为 17 (包括分隔符)

    for (size_t i = 0; i < mac.size(); ++i) {
        if (i % 3 == 2) {
            if (mac[i] != ':') return false;  // 每隔两个字符应该是冒号分隔
        } else {
            if (!std::isxdigit(mac[i])) return false;  // 检查每个字符是否是十六进制数字
        }
    }
    return true;
}

std::array<uint8_t, 6> PairInfo::LoadPairedMac() {
    esp_err_t ret = ESP_OK;
    std::array<uint8_t, 6> mac = Wireless::broadcast_mac;
    auto handle = nvs::open_nvs_handle("storage", NVS_READONLY, &ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "open_nvs_handle failed %s", esp_err_to_name(ret));
        return mac;
    }
    size_t saved_mac_len = 0;
    ret = handle->get_item_size(nvs::ItemType::SZ, paired_mac_key, saved_mac_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "saved_mac not found %s", esp_err_to_name(ret));
        return mac;
    }
    if (saved_mac_len == 0) {
        ESP_LOGW(TAG, "saved_mac_len is 0");
        return mac;
    }
    auto saved_mac = std::make_unique<char[]>(saved_mac_len);
    memset(saved_mac.get(), 0, saved_mac_len);
    ret = handle->get_string(paired_mac_key, saved_mac.get(), saved_mac_len);
    // "XX:XX:XX:XX:XX:XX" -> array
    auto saved_mac_str = std::string_view{saved_mac.get(), saved_mac_len};
    if (!is_valid_mac(saved_mac.get())) {
        ESP_LOGW(TAG, "saved_mac is not valid");
        return mac;
    }
    size_t idx = 0;
    for (size_t i = 0; i < 6; ++i) {
        unsigned int byte = 0;
        for (size_t j = 0; j < 2; ++j) {
            char c = saved_mac[idx++];
            if (std::isdigit(c)) {
                byte = byte * 16 + (c - '0');  // 如果是数字，转换为对应的十六进制值
            } else if (std::isalpha(c)) {
                byte = byte * 16 + (std::tolower(c) - 'a' + 10);  // 如果是字母，转换为对应的十六进制值
            }
        }
        mac[i] = static_cast<uint8_t>(byte);
        idx++;  // 跳过冒号
    }
    return mac;
}

esp_err_t PairInfo::SavePairedMac(std::array<uint8_t, 6> mac) {
    esp_err_t ret = ESP_OK;
    std::string mac_str;
    for (auto byte: mac) {
        mac_str += std::to_string(byte);
        mac_str += ":";
    }
    mac_str.pop_back();

    ret = SavePairedMac(mac_str);
    if (ret != ESP_OK) {
        return ret;
    }

    paired_mac = mac;
    return ESP_OK;
}

esp_err_t PairInfo::SavePairedMac(std::span<uint8_t, 6> mac) {
    esp_err_t ret = ESP_OK;
    std::string mac_str;
    for (auto byte: mac) {
        mac_str += std::to_string(byte);
        mac_str += ":";
    }
    mac_str.pop_back();

    ret = SavePairedMac(mac_str);
    if (ret != ESP_OK) {
        return ret;
    }

    std::copy(mac.begin(), mac.end(), paired_mac.data());
    return ESP_OK;
}

//esp_err_t PairInfo::SavePairedMac(uint8_t *mac) {
//    esp_err_t ret = ESP_OK;
//    std::string mac_str;
//    for (int i = 0; i < 6; i++) {
//        mac_str += std::to_string(mac[i]);
//        mac_str += ":";
//    }
//    mac_str.pop_back();
//
//    ret = SavePairedMac(mac_str);
//    if (ret != ESP_OK) {
//        return ret;
//    }
//
//    for (int i = 0; i < 6; i++) {
//        paired_mac[i] = mac[i];
//    }
//    return ESP_OK;
//}

esp_err_t PairInfo::SavePairedMac(std::string_view mac) {
    esp_err_t ret = ESP_OK;

    auto handle = nvs::open_nvs_handle("storage", NVS_READWRITE, &ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "open_nvs_handle failed %s", esp_err_to_name(ret));
        return ret;
    }
    ret = handle->set_string(paired_mac_key, mac.data());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set_string failed %s", esp_err_to_name(ret));
        return ret;
    }
    ret = handle->commit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "commit failed %s", esp_err_to_name(ret));
        return ret;
    }
    paired_mac = {0, 0, 0, 0, 0, 0};
    return ESP_OK;
}

std::array<uint8_t, 6> PairInfo::GetPairedMac() {
    if (paired_mac == std::array<uint8_t, 6>{0, 0, 0, 0, 0, 0}) {
        paired_mac = LoadPairedMac();
    }
    return paired_mac;
}

uint32_t PairInfo::GetConv() {
    return conv;
}

uint32_t PairInfo::CreateConv() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    // 最后一字节保留，可以根据最后一字节来判断是否是kcp的包
    return CreateConv(std::array<uint8_t, 6>{mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]});
}

void PairInfo::SetConv(uint32_t conv) {
    this->conv = conv;
}

uint32_t PairInfo::CreateConv(std::array<uint8_t, 6> mac) {
    conv = mac[4] << 24 | mac[5] << 16 | (esp_random() & 0xFF) << 8;
    return conv;
}


//
// Created by yekai on 2025/2/14.
//

#ifndef ESPNOW_STA_KCP_MASTER_PAIRINFO_H
#define ESPNOW_STA_KCP_MASTER_PAIRINFO_H

#include <cstdint>
#include <array>
#include "span"
#include "string_view"
#include "esp_err.h"

struct PairInfo {
    using pair_status_t = enum {
        NOT_PAIRED,
        PAIRING,
        PAIRED,
        BROADCASTING,
    };

    pair_status_t status = NOT_PAIRED;

    std::array<uint8_t, 6> GetPairedMac();

    uint32_t GetConv();

    void SetConv(uint32_t conv);

    uint32_t CreateConv(std::array<uint8_t, 6> mac);

    uint32_t CreateConv();

    std::array<uint8_t, 6> LoadPairedMac();

    esp_err_t SavePairedMac(std::array<uint8_t, 6> mac);

    esp_err_t SavePairedMac(std::span<uint8_t, 6> mac);

//    esp_err_t SavePairedMac(uint8_t *mac);

    static constexpr const char *const TAG = "PairStatus";
    static constexpr const char *const paired_mac_key = "peer_mac";

private:
    esp_err_t SavePairedMac(std::string_view mac);

    std::array<uint8_t, 6> paired_mac{0, 0, 0, 0, 0, 0};
    uint32_t conv = 0;
};

extern PairInfo pairInfo;

#endif //ESPNOW_STA_KCP_MASTER_PAIRINFO_H

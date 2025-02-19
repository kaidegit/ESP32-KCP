#include <cstdint>
#include <string>
#include <esp_now.h>
#include "kcp.h"
#include "ikcp.h"
#include "PairInfo.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG "KCP_TASK"

ikcpcb *kcp{nullptr};

SemaphoreHandle_t kcp_mutex{nullptr};

static int _KCP_Out(const char *buf, int len, ikcpcb *kcp, void *user) {
    esp_now_send(pairInfo.GetPairedMac().data(), (const uint8_t *) buf, len);
    return 0;
}

static void KCP_Init(ikcpcb *&kcp, uint32_t conv) {
    kcp = ikcp_create(conv, (void *) 0);

    kcp->output = _KCP_Out;

    ikcp_setmtu(kcp, 128);
    ikcp_nodelay(kcp, 1, 50, 2, 1);
}

static void KCP_Deinit(ikcpcb *&kcp) {
    if (kcp) {
        ikcp_release(kcp);
        kcp = nullptr;
    }
}

void KCP_Task(void *para) {
    static char buf[1024];

    kcp_mutex = xSemaphoreCreateBinary();
    xSemaphoreGive(kcp_mutex);

    KCP_Init(kcp, pairInfo.GetConv());

    static int cnt = 0;
    while (1) {

        xSemaphoreTake(kcp_mutex, portMAX_DELAY);

        ikcp_update(kcp, pdTICKS_TO_MS(xTaskGetTickCount()));

        auto len = ikcp_recv(kcp, buf, 1024);
        if (len > 0) {
            ESP_LOGI(TAG, "kcp recv %d: %.*s", len, len, buf);
        }

        cnt++;
        if (cnt % 100 == 0) {
            static std::string_view kcp_msg = {R"({"Hello": "KCP Master"})"};
            ikcp_send(kcp, kcp_msg.data(), kcp_msg.size());
        }

        xSemaphoreGive(kcp_mutex);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
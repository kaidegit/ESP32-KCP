#include "AsyncUDP.h"
#include "WiFi.h"
#include "cJSON.h"
#include "ikcp.h"
#include <Arduino.h>

static const char *SSID = "ESP32-Master";
static const char *PASSWORD = "12345678";

AsyncUDP udp;
IPAddress remoteIP(192, 168, 4, 1);
uint16_t udp_port, kcp_conv;

ikcpcb *kcp;

TaskHandle_t kcp_task_handle;

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    udp.writeTo((const uint8_t *)buf, len, remoteIP, udp_port);
    return 0;
}

void kcp_init() {
    uint32_t conv = kcp_conv;
    kcp = ikcp_create(conv, (void *)0);

    kcp->output = udp_output;

    ikcp_wndsize(kcp, 128, 128);
    ikcp_nodelay(kcp, 1, 10, 2, 1);
}

void kcp_deinit() {
    ikcp_flush(kcp);
    ikcp_release(kcp);
    kcp = nullptr;
}

void KCP_Thread(void *para) {
    static char buf[1024];

    if (!udp.connect(IPAddress(192, 168, 4, 1), udp_port)) {
        Serial.println("UDP connect failed");
        vTaskDelete(NULL);
    }
    Serial.println("UDP connected at port " + String(udp_port));
    udp.onPacket([](AsyncUDPPacket packet) {
        ikcp_input(kcp, (const char *)packet.data(), packet.length());
    });
    
    uint32_t cnt = 0;
    kcp_init();

    while (1) {
        ikcp_update(kcp, millis());
        auto len = ikcp_recv(kcp, buf, 1024);
        if (len > 0) {
            Serial.println("kcp recv");
            buf[len] = 0;
            Serial.println(buf);
        }

        cnt++;
        if (cnt % 100 == 0) {
            char msg[1024];
            sprintf(msg, "hello master %d", cnt);
            Serial.println("kcp send");
            ikcp_send(kcp, msg, strlen(msg));
        }
        delay(10);
    }
}

void TCP_Thread(void *para) {
    static auto client = WiFiClient();
    static auto cnt = 0;
    while (1) {
        if (client && client.connected()) { // connected to server
            if (client.available()) {
                String data = client.readStringUntil('\n');
                Serial.println("TCP recv: " + data);
                if (data.equals("pong\r")) {
                    // ping back
                    cnt = 0;
                } else {
                    // try resolve json
                    cJSON *root = cJSON_Parse(data.c_str());
                    if (root) {
                        cJSON *ip_json = cJSON_GetObjectItem(root, "ip");
                        if (ip_json) {
                            // TODO not use it yet
                            // remoteIP.fromString(ip_json->valuestring);
                            // Serial.println("ip: " + remoteIP.toString());
                        }
                        cJSON *port_json = cJSON_GetObjectItem(root, "udp_port");
                        if (port_json) {
                            udp_port = port_json->valueint;
                            Serial.println("port: " + String(udp_port));
                        }
                        cJSON *conv_json = cJSON_GetObjectItem(root, "kcp_conv");
                        if (conv_json) {
                            kcp_conv = conv_json->valueint;
                            Serial.println("conv: " + String(kcp_conv));
                        }
                    }
                    cJSON_Delete(root);
                    // start kcp thread
                    xTaskCreate(
                        KCP_Thread,
                        "KCP_Thread",
                        4096,
                        NULL,
                        5,
                        &kcp_task_handle);
                }
            } else {
                cnt++;
                if (cnt >= 1000) {
                    client.println("ping");
                    cnt = 500; // resend ping in 0.5s if no pong
                }
            }
        } else { // disconnected, try to connect it
            Serial.println("client disconnected");
            if (kcp_task_handle) {
                vTaskDelete(kcp_task_handle);
                kcp_task_handle = NULL;
                Serial.println("delete kcp task");
            }

            client.connect(IPAddress(192, 168, 4, 1), 12345);
            delay(100);
            client.println("req");
            Serial.println("client connected");
        }
        delay(1);
    }
}

void setup() {
    Serial.begin(115200);
    // connect to wifi
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("WiFi Failed");
        while (1) {
            delay(1000);
        }
    }
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    xTaskCreate(TCP_Thread, "TCP_Thread", 4096, NULL, 5, NULL);
}

void loop() {
    delay(10);
}
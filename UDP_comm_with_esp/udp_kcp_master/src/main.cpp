#include "AsyncUDP.h"
#include "WiFi.h"
#include "cJSON.h"
#include "ikcp.h"
#include <Arduino.h>

static const char *SSID = "ESP32-Master";
static const char *PASSWORD = "12345678";

WiFiServer server(12345, 1);
IPAddress myIP;
IPAddress remoteIP;
uint16_t remote_port;

uint16_t udp_port, kcp_conv;

AsyncUDP udp;

ikcpcb *kcp;

TaskHandle_t kcp_task_handle;

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    udp.writeTo((const uint8_t *)buf, len, remoteIP, remote_port);
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

    if (!udp.listen(udp_port)) {
        Serial.println("UDP listen failed");
        vTaskDelete(NULL);
    }
    Serial.println("UDP listening at port " + String(udp_port));
    udp.onPacket([&](AsyncUDPPacket packet) {
        ikcp_input(kcp, (const char *)packet.data(), packet.length());
        remote_port = packet.remotePort();
    });

    kcp_init();

    uint32_t cnt = 0;
    while (1) {

        ikcp_update(kcp, millis());
        auto len = ikcp_recv(kcp, buf, 1024);
        if (len > 0) {
            Serial.println("kcp recv");
            buf[len] = 0;
            Serial.println(buf);
        }

        cnt++;
        if (cnt % 150 == 0) {
            char msg[1024];
            sprintf(msg, "hello slave %d", cnt);
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
        if (client && client.connected()) { // client is connected
            if (client.available()) {       // can read some bytes from client
                String data = client.readStringUntil('\n');
                Serial.println("TCP recv: " + data);
                // check if it is a req or a ping
                if (data.equals("req\r")) {
                    // generate and send the kcp info in json
                    cJSON *root = cJSON_CreateObject();
                    cJSON_AddStringToObject(root, "ip", myIP.toString().c_str());
                    cJSON_AddNumberToObject(root, "udp_port", udp_port);
                    cJSON_AddNumberToObject(root, "kcp_conv", kcp_conv);
                    char *json = cJSON_PrintUnformatted(root);
                    Serial.printf("send kcp info: %s\r\n", json);
                    client.println(json);
                    cJSON_free(json);
                    cJSON_Delete(root);
                    cnt = 0;
                    // start kcp thread
                    xTaskCreate(
                        KCP_Thread,
                        "KCP_Thread",
                        4096,
                        NULL,
                        5,
                        &kcp_task_handle);
                } else {
                    client.println("pong");
                    cnt = 0;
                }
            } else {
                cnt++;
                // do not ping in 2s
                if (cnt > 2000) {
                    client.~WiFiClient();
                    if (kcp_task_handle) {
                        Serial.println("delete kcp task");
                        vTaskDelete(kcp_task_handle);
                        kcp_task_handle = NULL;
                    }
                }
            }
        } else { // client is not connected
            auto hasClient = server.hasClient();
            if (hasClient) {
                Serial.println("new client");
                client = server.available();
                remoteIP = client.remoteIP();
                Serial.println("remote ip: " + remoteIP.toString());
                cnt = 0;
            }
        }
        delay(1);
    }
}

void setup() {
    udp_port = random(10000, 40000);
    kcp_conv = random(10000, 40000);

    Serial.begin(115200);

    delay(500);

    Serial.printf("udp port:%d kcp conv:%d\r\n", udp_port, kcp_conv);
    // set up a ap
    if (!WiFi.softAP(SSID, PASSWORD)) {
        log_e("Soft AP creation failed.");
        while (1) {
        }
    }
    myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);

    server.begin();
    Serial.println("Server started");

    xTaskCreate(
        TCP_Thread,
        "TCP_Thread",
        4096,
        NULL,
        5,
        NULL);
}

void loop() {
    delay(10);
}
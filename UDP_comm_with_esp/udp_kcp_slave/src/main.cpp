#include "AsyncUDP.h"
#include "WiFi.h"
#include "cJSON.h"
#include "ikcp.h"
#include <Arduino.h>

static const char *SSID = "ESP32-Master";
static const char *PASSWORD = "12345678";

AsyncUDP udp;

ikcpcb *kcp;

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    union {
        int id;
        void *ptr;
    } parameter;
    parameter.ptr = user;
    // vnet->send(parameter.id, buf, len);
    udp.write((const uint8_t *)buf, (size_t)len);
    return 0;
}

void kcp_init() {
    uint32_t conv = 0x11223344;
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
                            auto ip = ip_json->valuestring;
                            Serial.println("ip: " + String(ip));
                        }
                        cJSON *port_json = cJSON_GetObjectItem(root, "udp_port");
                        if (port_json) {
                            auto port = port_json->valueint;
                            Serial.println("port: " + String(port));
                        }
                        cJSON *conv_json = cJSON_GetObjectItem(root, "kcp_conv");
                        if (conv_json) {
                            auto conv = conv_json->valueint;
                            Serial.println("conv: " + String(conv));
                        }
                    }
                    cJSON_Delete(root);
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

    // udp.

    // if (udp.connect(IPAddress(192, 168, 123, 23), 23456)) {
    //     Serial.println("UDP connected");
    //     udp.onPacket([&](AsyncUDPPacket packet) {
    //         printf("udp recv\r\n");
    //         ikcp_input(kcp, (const char *)packet.data(), packet.length());
    //         // Serial.print("UDP Packet Type: ");
    //         // Serial.print(packet.isBroadcast() ? "Broadcast" : packet.isMulticast() ? "Multicast"
    //         //                                                                        : "Unicast");
    //         // Serial.print(", From: ");
    //         // Serial.print(packet.remoteIP());
    //         // Serial.print(":");
    //         // Serial.print(packet.remotePort());
    //         // Serial.print(", To: ");
    //         // Serial.print(packet.localIP());
    //         // Serial.print(":");
    //         // Serial.print(packet.localPort());
    //         // Serial.print(", Length: ");
    //         // Serial.print(packet.length());
    //         // Serial.print(", Data: ");
    //         // Serial.write(packet.data(), packet.length());
    //         // Serial.println();
    //         // // reply to the client
    //         // packet.printf("Got %u bytes of data", packet.length());
    //     });
    //     // Send unicast
    //     udp.print("Hello Server!");
    // }

    // kcp_init();
}

void loop() {
    // Serial.printf("Hello World\r\n");
    // delay(1000);
    // udp.broadcastTo("Anyone here?", 23456);

    // static int cnt = 0;
    // static int retrans_time = 0;
    // static char buf[1024];

    // ikcp_update(kcp, millis());
    // cnt++;
    // if (cnt % 100 == 0) {
    //     printf("kcp send\r\n");
    //     const char* hello = "hello from esp32";
    //     ikcp_send(kcp, hello, strlen(hello) + 1);
    // }
    // auto len = ikcp_recv(kcp, buf, 1024);
    // if (len > 0) {
    //     retrans_time = 0;
    //     printf("kcp recv");
    //     buf[len] = 0;
    //     printf(buf);
    // } else {
    //     retrans_time ++;
    //     if (retrans_time > 1000) {
    //         retrans_time = 0;
    //         Serial.println("restart kcp");
    //         kcp_deinit();
    //         kcp_init();
    //     }
    // }
    delay(10);
}
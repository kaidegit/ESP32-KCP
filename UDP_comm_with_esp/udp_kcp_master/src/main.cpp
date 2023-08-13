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
    union {
        int id;
        void *ptr;
    } parameter;
    parameter.ptr = user;
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

        // Serial.print("UDP Packet Type: ");
        // Serial.print(packet.isBroadcast() ? "Broadcast" : packet.isMulticast() ? "Multicast"
        //                                                                        : "Unicast");
        // Serial.print(", From: ");
        // Serial.print(packet.remoteIP());
        // Serial.print(":");
        // Serial.print(packet.remotePort());
        // Serial.print(", To: ");
        // Serial.print(packet.localIP());
        // Serial.print(":");
        // Serial.print(packet.localPort());
        // Serial.print(", Length: ");
        // Serial.print(packet.length());
        // Serial.print(", Data: ");
        // Serial.write(packet.data(), packet.length());
        // Serial.println();

        remote_port = packet.remotePort();
        // // reply to the client

        // packet.printf("Got %u bytes of data", packet.length());
    });

    // udp.broadcast("Anyone here?");

    kcp_init();

    uint32_t cnt = 0;
    while (1) {
        // delay(1000);
        // char msg[1024];
        // sprintf(msg, "hello client %d", cnt++);
        // udp.writeTo((const uint8_t *)msg, strlen(msg), remoteIP, remote_port);
        // Send broadcast
        //  udp.broadcast("Anyone here?");

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

    // while (1) {
    //     auto hasClient = server.hasClient();
    //     if (hasClient) {
    //         Serial.println("new client");
    //         break;
    //     }
    //     delay(10);
    // }

    // client = server.available();

    xTaskCreate(
        TCP_Thread,
        "TCP_Thread",
        4096,
        NULL,
        5,
        NULL);

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
    // vTaskDelete(NULL);
    // static WiFiClient client = server.available();

    // if (client && client.connected()) {
    //     // client.println("Hello Client1");
    //     if (client.available()) { // can read some bytes from client
    //         String data = client.readStringUntil('\n');
    //         Serial.println(data);
    //         client.println("Hello Client");
    //     }
    // }

    // USBSerial.printf("Hello World\r\n");
    // delay(1000);
    // udp.broadcastTo("Anyone here?", 23456);

    // static int cnt = 0;
    // static int retrans_time = 0;
    // static char buf[1024];

    // ikcp_update(kcp, millis());
    // cnt++;
    // if (cnt % 100 == 0) {
    //     printf("kcp send\r\n");
    //     const char *hello = "hello from esp32";
    //     ikcp_send(kcp, hello, strlen(hello) + 1);
    // }
    // auto len = ikcp_recv(kcp, buf, 1024);
    // if (len > 0) {
    //     retrans_time = 0;
    //     printf("kcp recv");
    //     buf[len] = 0;
    //     printf(buf);
    // } else {
    //     retrans_time++;
    //     if (retrans_time > 1000) {
    //         retrans_time = 0;
    //         Serial.println("restart kcp");
    //         kcp_deinit();
    //         kcp_init();
    //     }
    // }
    delay(10);
}
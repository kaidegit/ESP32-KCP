#include "AsyncUDP.h"
#include "WiFi.h"
#include "WiFiInfo.inc" // WiFi SSID and password
#include "ikcp.h"
#include <Arduino.h>

AsyncUDP udp;

ikcpcb *kcp;

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
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
    
    if (udp.connect(IPAddress(192, 168, 123, 23), 23456)) {
        Serial.println("UDP connected");
        udp.onPacket([&](AsyncUDPPacket packet) {
            ikcp_input(kcp, (const char *)packet.data(), packet.length());
        });
        // Send unicast
        udp.print("Hello Server!");
    }

    kcp_init();
}

void loop() {
    static int cnt = 0;
    static int retrans_time = 0;
    static char buf[1024];

    ikcp_update(kcp, millis());
    cnt++;
    if (cnt % 100 == 0) {
        printf("kcp send\r\n");
        const char* hello = "hello from esp32";
        ikcp_send(kcp, hello, strlen(hello) + 1);
    }
    auto len = ikcp_recv(kcp, buf, 1024);
    if (len > 0) {
        retrans_time = 0;
        printf("kcp recv");
        buf[len] = 0;
        printf(buf);
    } else {
        retrans_time ++;
        if (retrans_time > 1000) {
            retrans_time = 0;
            Serial.println("restart kcp");
            kcp_deinit();
            kcp_init();
        }
    }
    delay(10);
}
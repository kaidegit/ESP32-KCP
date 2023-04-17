一个esp32c3使用基于udp的kcp的例子，esp32作为kcp客户端，nodejs为服务端



（有空再来更说明）



存在的问题：

服务端重启会导致接收的问题

![重连问题](C:\Users\yekai\Documents\GitHub\ESP32-KCPoverUDP\known_issue\重连问题.png)

see [this issue]([关于发送端开始发送后，若接收端重启后会无法接收数据 · Issue #94 · skywind3000/kcp (github.com)](https://github.com/skywind3000/kcp/issues/94))


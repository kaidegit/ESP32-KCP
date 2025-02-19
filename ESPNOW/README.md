配对流程

* master监听，slave发广播`{"Hello": "xxx"}`
* master收到广播后向slave发送`{"protocol":"kcp","conv":1234}`
* slave回复`{"Hello": "OK"}`
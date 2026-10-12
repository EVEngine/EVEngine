# 平台事件：帧循环拥有输入泵

**入口：** `eve.PlatformEvent()`，启动槽位 `platform_event`。它负责平台消息泵和
消费式队列。普通游戏使用 keyboard/mouse/touch 的状态接口，由 load.nut 统一 pump/poll。
不要在 eve_update 再 drain 平台队列，否则会抢走退出、窗口或输入消息。

## 任务一：处理玩家输入

在 config.modules 声明 keyboard/mouse，update 使用 keyboard.isDown 或
key_just_pressed；渲染读取更新后的状态。完整示例见
[游戏开发教程](../GAME_DEVELOPMENT.md)。按下状态和只触发一次的边沿是不同问题。

## 任务二：实现独立工具宿主

只有自己拥有帧循环的宿主才调用 pump，然后 poll 直到返回空字符串，紧接着用
getLastData 读取该条数据；下一次 poll 会更新 lastData。pollData 的复合返回形式以
绑定契约为准，不凭 C++ Message 指针猜脚本形状。pushData(name,data) 将文本消息放进
队列，接收者明确约定数据格式；通用游戏事件日志用 GameEvent，持续订阅用 Event/Rx。

队列是一次性消费，不会自动广播给所有系统；多个消费者需要宿主明确分发。不要在
工作线程调用窗口/VM 方法。源码与普通循环证据：
[PlatformEvent.cpp](../../../src/modules/platform_event/PlatformEvent.cpp)、
[load.nut](../../../src/scripts/load.nut)。

## 补充绑定参考

参数与接收者见 [源码生成参考](platform_event/api-reference.md)，使用前先确认本章的前置条件与生命周期。

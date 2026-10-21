# Physics Bake Particle Capture

L6 录制卫星将 particles 的完整 owning 事件记录转换成 physics_bake 的不可变轨迹。
每个 Sim 录制会话必须提供独立 PersistentId；birthSerial 不能跨会话充当全局身份。
时间是实际离散模拟步边界，不声称连续碰撞时刻。每个出生必须有终止死亡事件；
同时间快照按最终状态覆盖，零持续时间出生不可见，验证后省略；float 无法区分的事件时间拒绝。

## module-interface

- 原生面：buildParticleTrack 借用完整事件、配方和显式时长，返回 checked owning ParticleTrack。
- Capability：无 provider 或查询。
- ECS/Link：无跨调用实体指针；输入稳定期间同步转换，源销毁不影响结果。
- 事件：消费数值日志，无回调或订阅。
- 脚本：暂无。
- 工具协议：无。

本模块无反向播放依赖；完整裁剪级别尚未验证，不从 include 图推导运行可裁剪。

当前 CPU sprite 录制支持 billboard、axis、stretched 和稳定出生顺序（sortMode=none）。
GPU simulation、ribbon 和距离/年龄排序尚未拥有相应的完整记录合同，准备录制时显式拒绝；
录制中切换到这些模式使 journal 失效，不能把它们静默重放为普通 sprite。
这些限制是尚未完成的通道能力，不代表所有粒子表现均已支持。

多发射器调用使用 `ParticleRecordingSource` 借用日志数组，统一时长和完整配方转换。
所有会话身份必须唯一（包括空日志），总事件不超过 2 百万，总可见出生不超过 65536；
相同 birthSerial 在不同 emitterSession 中合法，不会相互插值。单发射器入口委托同一实现。
该组合仅处理记录数据；每种材质/纹理的重放绑定仍由表现层显式提供。

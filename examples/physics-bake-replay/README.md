# Physics Bake Replay

四类真实物理录制与双实例重放：刚体弹跳、布料、绳索和 CPU 粒子爆发。
上排蓝色以 1 倍速播放，下排橙色以 0.65 倍速、错开起始时间播放；
底部两组粒子共享同一冻结资产。UI 可暂停、重启、切换正放/倒放。
面板实时显示两实例的播放时间与有符号速率；暂停时重启会保持暂停并归零，
切换方向只改变速率符号，恢复后从当前时间继续。

```sh
make run/linux-debug GAME=examples/physics-bake-replay
# Windows: make run/win32-debug GAME=examples/physics-bake-replay
```

首次启动用固定 60 Hz 录制三秒：刚体/布料/绳索每两步保存一帧，粒子记录真实出生、
采样和死亡事件。资产保存在引擎用户存档目录的 `physics-bake-replay/clips/` 下，日志打印完整路径。
每个通道保存后重新从文件加载；下一次启动直接加载缓存，不创建这些通道的模拟源。
仅 NotFound 触发录制，损坏、身份不匹配和其他 IO 错误会明确停止，不覆盖现有资产。
将 config.nut 中 `bake.playbackOnly` 改为 true 可强制只加载；缺少任何资产即失败。
这个开关不参与生产配方，所以可以用同一套资产验证第二进程仅播放。

配方包括实际引擎生产源码/构建指纹、后端版本、脚本内容、固定步长、采样间隔和命名随机种子。
通用 BakeRecipe 与旧破坏动画配方使用同一份构建身份生成机制；粒子生产源码也参与指纹。
修改生产代码、脚本、采样设置或种子会产生新的缓存键。显式重建可先移动旧资产到别处，
示例不会自动删除缓存。

录制完成后刚体 world 显式销毁，布料/绳索由最后 VM 引用释放并 GC。
粒子发射器属于 ECS，示例 stop/reset 后丢弃脚本引用，退役所有活粒子；这不宣称删除 ECS 实体。
重放阶段只 advance 播放时钟并提交姿态/网格/精灵，**没有物理或粒子求解 update 调用**。
粒子 GPU 驻留、ribbon、视角相关排序不在这个 CPU sprite 通道的支持范围。

网格和材质沿用 Graphics/ECS 资源生命周期。示例关闭 hot reload，防止反复加载叠加场景实体。
首次 Debug 烘焙可能需要二十余秒；日志逐通道报告录制和发布。缓存加载则直接进入重放。
源码检查：`make check/examples`。宿主已构建后烟测：

```sh
MIN_RUN_SECONDS=2 RUN_SECONDS=30 bash scripts/smoke_examples.sh physics-bake-replay
```

Linux 无显示设备时按根 AGENTS.md 设置软件 Vulkan、null audio，并在 Xvfb 下运行。
烟测证明启动及持续运行；视觉证据须使用引擎自身截图/像素读回，不能使用 Xvfb 截图。

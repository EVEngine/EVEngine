# VRM Avatar

从仓库根目录直接运行：

```
make run/win32-debug GAME=examples/vrm-avatar
```

示例自带合成 VRM 合约模型（两个三角形网格、表情和弹簧链），无需下载私有资产即可验证导入。
将 VRoid Studio 导出的 VRM 1.0 文件复制为本目录的 `model.vrm` 后，示例优先加载该文件，
展示眨眼、视线跟踪、头部转动与头发弹簧骨骼。启动日志中的 `VRM_SOURCE` 显示实际使用的模型。
空格循环模型提供的表情，P 暂停/恢复自动头部动作。

自带模型由 `test/assets/vrm/generate.py` 统一生成，与测试夹具内容一致。
用户模型不随示例分发；指定模型损坏会显示加载诊断并停止更新，不会悄悄改用自带模型。

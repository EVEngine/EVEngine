# Linux 无图形物理机制检查

该夹具链接真实 Box2D/Box3D、引擎 World/Body/Joint 和真实 Squirrel VM，运行同一批
`test/unit/physics/` 与 `test/integration/physics-simulation/` 中的物理用例。
无 Vulkan/SDL 宿主；唯一替身是缺席的 PlatformEvent 入口，
意外调用时抛错，不能把这个夹具当成完整事件宿主或 GUI 验证。

```sh
cmake -S test/profile/physics_mechanics -B build/physics-mechanics -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/physics-mechanics --target force_integration --parallel 4
ctest --test-dir build/physics-mechanics -LE bundle --output-on-failure --parallel 4
```

依赖使用仓库固定的 `external/zeroerr` 与 `third-party` 源码，需先初始化子模块及
官方构建配置拉取 third-party。每个测试独立进程运行；同时检查 ZeroErr 断言警告，
避免仅看进程返回码遗漏失败。普通测试目标从同一测试目录和分类目录发现这些用例。

内存检查使用相同真实实现，不关闭 ASan 全局检查：

```sh
cmake -S test/profile/physics_mechanics -B build/physics-mechanics-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address'
cmake --build build/physics-mechanics-asan --target force_integration --parallel 4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build/physics-mechanics-asan \
  -R 'physics.joint2d.snapshot|physics.joint2d.script|physics.forces.script' --output-on-failure
```

该组覆盖本次新增的恢复、失败回滚及 VM 引用生命周期；不代表完整引擎的 sanitizer 结果。
夹具也链接实际 Physics world 工厂与 sensing 坐标实现，避免只在未启用 ASan 的链接器
剔除路径下碰巧通过。World 工厂从绑定文件纯移动到 PhysicsWorldFactory.cpp，行为不变。

## 可选弹射物组合

夹具还编译真实 `WorldHitProbe`、`CombatCarrierRuntime` 和 World3D 显式冲量绑定。
`carrierComposition.*` 覆盖 Box3D/Jolt 的无反馈与主动反馈、陈旧身份拒绝、真实 VM 参数验证，
`carrierWorldSweep.*` 覆盖世界恢复后会话及接触失效。

```sh
ctest --test-dir build/physics-mechanics -R 'carrierComposition\.|carrierWorldSweep\.' --output-on-failure
```

`carrier_probe_absent` 单独链接弹射物实现与 common，不链接任何 physics 源码／库；
`carrierComposition.physicsImplementationAbsent` 验证缺席查询会话明确失败、关闭碰撞不访问会话、
项目映射可用普通查询实现注入且失败不提交帧。这是该适配器 L1/L2/L4 的聚焦证据，
不等价于完整引擎所有模块的裁剪矩阵。GUI 示例仍需正式 `eve` 的 smoke 验证。

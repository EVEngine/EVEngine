# Biome Region Tour / 群落区域漫游

四个相邻的真实 3D 区域，共用连续高度场与贯穿地图的小径：晴朗草甸、雨雾湿地、金色旱地、雪林高地。
摄像机使用原生 `CameraController` 的八个命名视角，自动穿越各区，分别提供全景和近景。
一圈约 80 秒；右下角小地图显示真实区域边界和相机位置，左上角显示群落绑定及发布版本。

## 运行

```sh
make run/win32-debug GAME=examples/biome-region-tour
# 或在包含本次 BiomeRegionBinding 接口的构建/SDK 中：
eve run examples/biome-region-tour
```

- `Space`：暂停/继续相机，生态展示动画继续。
- `1`–`4`：跳至指定区域，随后继续巡游；暂停时保持该观察点。
- `R`：从草甸重新巡游。
- `V`：通过现有群落编辑事务，将当前群落的天气方案改成风暴。
- `Z`：撤销当前群落上一次编辑，区域自动恢复其继承的天气方案。

## 复用与所有权

`schemes.nut` 是本示例的项目资源内容，包含六类方案，不注册新的引擎群落系统。
每区持有原生 `BiomeRulesEditor` 文档，并用 `bindRegion` 保存区域身份、空间 URI 和固定 seed。
文档仅关联六个 `project://tour/...` URI；生成阶段从 `binding.configuration()` 取得方案。
植被分布复用 `BiomeRules`、`SpatialData` 和路径排除，树网格/图集复用原生 Procgen 生成器。
地表使用原生高度场网格；动物和守卫是本示例的程序化几何展示模型，带轻量游走/呼吸动画，未实现战斗 AI。

进入区域由相机的真实 X 坐标判定，边界位于 -32、0、32；区域范围 X=-64..64，Z=-22..22。
天气调用已有 Weather 模块，雾使用场景深度与 Volumetric；颜色和密度在跨区时平滑过渡。
当前只支持单观察相机的区域天气投影，不宣称各区域同时拥有独立的全球天气系统。
每 0.25 秒读取绑定配置并按 revision 更新环境；静态植物、地表和生物在初始化时生成，
修改这些方案后需要重启示例重新生成。`V`/`Z` 演示的天气关联无需重启。
这是项目侧方案消费示例，没有修改引擎 API 或注册第二套可变群落配置。

所有对象及生成资源由 world 持有到脚本退出，相机、文档和区域绑定由主脚本持有。
使用固定生成种子和注入的帧 dt；不依赖外部模型、下载或本机绝对路径。热重载关闭，避免重复生成场景。

## 验证

```sh
python3 scripts/check_examples.py
MIN_RUN_SECONDS=2 RUN_SECONDS=90 bash scripts/smoke_examples.sh biome-region-tour
```

启动输出 `BIOME_TOUR_READY`，每次跨区输出 `BIOME_TOUR_ENTER`。
各区停留 7 秒后通过引擎 `saveFramePng` 生成 `capture-meadow.png` 等四张图；这些验证产物已忽略。
`game.agent.json` 提供正式 observe/actions，可通过 `eve_play` 观察区域、暂停、跳转及执行风暴/撤销验证。
启动 smoke 只验证存活与错误日志；完整巡游另需检查四个区域到达日志及四张画面。

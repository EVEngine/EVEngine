# Procedural Road Lab

纯数学生成道路（无预制道路/桥梁模型）。简单场景验证几何后，打开复杂立交：

| 键 | scene | 内容 |
|----|-------|------|
| 1 | `straight` | 平坦直线段（无路口） |
| 2 | `curve` | 水平弯道（无路口） |
| 3 | `bridge` | 等高高架直线 + 桥墩 |
| 4 | `cross` | 地面十字路口（四臂外推 + 圆心朝外圆弧转角） |
| 5 | `t-junction` | 不等宽丁字路口 |
| 6 | `y-junction` | 不等宽、不同车道数的 Y 型汇合 |
| 7 | `sloped-t` | 三条不同高程道路汇入同一路口 |
| 8 | `curve-uphill` | 曲线、坡度和汇合路口组合 |
| 9 | `tight-turn` | 短边近折返连接，可视化验证局部导航 turn ribbon |
| R | `roundabout` | 四入口单向环岛 + 双向接入道路 |
| 0 | `interchange` | 地面十字 + 对角高架 + 四条外围爬坡匝道 |
| T | `tee` | 等宽 T 字路口与宽角路肩弦 |
| Y | `y` | 120° 三臂路口 |
| F | `fork` | 60° 锐角分叉 |
| K | `skew` | 135° 斜角三臂 |
| G | `roadType` | 在 `highway` / `railway` 之间切换（铁道：道砟截面 + 钢轨设施 + 轨枕） |

```sh
make run GAME=examples/procedural-road
# headless：用 scene.txt 指定单个场景；写入 gallery 会依次保存全部场景
# 写入 railway / highway 可切换道路类型（与场景名互斥时优先作为类型）
echo railway > examples/procedural-road/scene.txt
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a scripts/smoke_examples.sh procedural-road
```

配方参数：`scene=straight|curve|bridge|cross|tee|y|fork|skew|t-junction|y-junction|sloped-t|curve-uphill|tight-turn|roundabout|interchange`，`roadType=highway|railway`，另有 `mesh.roadNetwork` / `tex.roadMarkings`。`roadType=railway` 时 `lanes` 表示并行轨道数（1–2）。
沥青公路使用通用 `pbr.asphalt` 配方生成 albedo/normal/height，并复用 Mesh3D 的纹理去重复与轻量视差；铁道用材质 tint 区分道砟/钢轨/轨枕。道路拓扑与渲染材质保持解耦。
设计说明见 `docs/dev/程序化道路系统设计.md`。

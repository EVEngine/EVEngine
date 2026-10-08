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

```sh
make run GAME=examples/procedural-road
# headless：用 scene.txt 指定单个场景；写入 gallery 会依次保存全部场景
echo interchange > examples/procedural-road/scene.txt
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a scripts/smoke_examples.sh procedural-road
```

配方参数：`scene=straight|curve|bridge|cross|t-junction|y-junction|sloped-t|curve-uphill|tight-turn|roundabout|interchange`，另有 `mesh.roadNetwork` / `tex.roadMarkings`。
沥青使用通用 `pbr.asphalt` 配方生成 albedo/normal/height，并复用 Mesh3D 的纹理去重复与轻量视差；道路拓扑与渲染材质保持解耦。
设计说明见 `docs/dev/程序化道路系统设计.md`。

# 旋涡星系 / Spiral Galaxy

用 EveScript 与现有 Vulkan 2D 纹理批绘制渲染四条蓝紫旋臂、暖色银核、
尘埃带、7,600 颗三维星点与 650 颗背景星。无需下载资产或编译自定义 Shader。

## 运行

仓库根目录执行（Linux/macOS 替换平台名）：

```sh
make run/win32-debug GAME=examples/galaxy
```

或使用对应版本 SDK：`eve run examples/galaxy`。

## 操作

| 操作 | 效果 |
|---|---|
| 鼠标左键拖动 | 旋转与改变观测倾角 |
| 滚轮 | 缩放 |
| Space | 暂停 / 恢复旋转 |
| 1 / 2 / 3 | 俯视 / 斜视 / 接近侧视 |
| R | 恢复默认视角、缩放、时间和旋转状态 |
| F5 | 用引擎自有 `saveFramePng` 保存上一张呈现帧到 `galaxy.png` |

## 实现与范围

- `main.nut`：固定种子 73129 的独立 Park–Miller 随机流，对数旋臂与厚银核点云。
  初始化时生成；逐帧仅旋转、透视投影并提交精灵，窗口尺寸参与投影。
- `config.nut`：1280×800，显式声明 gfx、win、keyboard、mouse。
- `assets/star.png`、`assets/glow.png`：原创透明径向精灵，
  可用 `python examples/galaxy/generate_assets.py` 重新生成，仅依赖 Python 标准库。

本示例采用艺术化刚体旋转，不模拟 N 体引力或真实恒星轨道。
时间只来自 `eve_update(dt)`；同种子生成同一初始点云，浮点投影允许平台舍入差异。
背景、星云、银核、尘埃、星点按固定图层合成，未做逐星深度排序或物理体积吸收。
CPU 每帧投影约九千个精灵；低性能设备可减少 `buildGalaxy()` 中的循环次数。
资源在初始化时读取，渲染回调不读磁盘；`persist` 保持热重载时的视角与点云。

## 验证

```sh
python scripts/check_examples.py
MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh galaxy
```

视觉验收需运行真实引擎并用 F5 或引擎 MCP 截图；离线效果图不能作为运行证据。

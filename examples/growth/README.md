# Growth — 可配置生命体生长模拟

演示 `eve.Growth()`：本例用地球农场内容（小麦 + 牛），但物种是 **JSON 数据驱动** 的——
`kind` / `needs` / `reproduction.mode` / 季节布局均可换成外星晶体裂变、黏菌出芽等体系
（见 `docs/usr/modules/growth.md` 与单元测试 `growth.xeno.*`）。独立 ECS 实体，不绑 hex。

画面左侧是农场场景（背景、谷仓、田垄、小麦阶段精灵、牛与产出上浮图标），右侧 HUD
显示日历 / 状态 / 事件。素材清单见 `assets/ART-README.md`。

## 运行

```bash
make run/<platform>-debug GAME=examples/growth
```

无操作键：示例每帧自动浇水/喂养并按 `secondsPerSimDay=2` 推进仿真日。
需要 `font` 模块与 `assets/fonts/DejaVuSans-Bold.ttf`（授权见 `assets/fonts/NOTICE.txt`）。

## 相关

- 设计：`docs/dev/生长模拟系统设计.md`
- 用户文档：`docs/usr/modules/growth.md`

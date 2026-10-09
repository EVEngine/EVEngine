# Mesh Contact Fusion（接触带融合）

并排演示 **硬拼接** vs **间隙 soft-snap 焊颈** vs **实时 A←B 熔贴**：

| 位置（相机朝 −Z） | API | 行为 |
|---|---|---|
| 左 | `mergeStaticMeshes` blend OFF | **同一间隙**的双球，中间留空（对照：未融合） |
| 中 | blend ON + **softSnap** + post weld | 同一间隙，对面顶点跨缝拉近，轮廓焊成 **花生/颈**；soft-snap 后按几何重算法线，再 weld/轻 smooth 让接触带连续 |
| 右 | `MeshAdhereLive` | 细分球体 soft-snap 到地面（底部摊成熔贴饼）；灰色 ghost = 熔贴前 |

要点：

- **方块对缝只会“拍死”成平面**——看不出融合。本示例用**球体**，soft-snap 才会拉出可见的焊颈轮廓。
- **深重叠 + soft-snap 会把缝“凹”进去**——那是最近点互相拉入体积。中路用的是**间隙焊合**。
- 静态默认仍可关融合；这里中路显式打开 soft-snap 是为了让几何变化一眼可见。
- 动态路径默认 soft-snap；源抬升必须落在 `edgeRadius` 内，否则查询打不中。

设计说明见
[`docs/dev/superpowers/specs/2026-10-08-mesh-merge-adhere-plan.md`](../../docs/dev/superpowers/specs/2026-10-08-mesh-merge-adhere-plan.md)。

## 运行

```bash
cd examples/mesh-contact-fusion
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/mesh-contact-fusion
```

## 操作

- **Edge / Material radius**、**Strength**、**Source lift**：脏标记右侧 live 并重算。
- **Soft snap**：只作用于右侧 live；关掉后球体回到未贴合姿态（仍保留权重逻辑）。
- **Bake live**：冻结派生后再 activate。

成功标记：

- 控制台打印 `MESH_CONTACT_FUSION_PASS hard+gapWeld+liveMelt ...`
- 当前目录写出 `mesh-contact-fusion.png`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 双球硬拼接 / 焊颈 / live 熔贴 + UI |
| `config.nut` | 1280×720、`hotReload = true` |

## 契约

可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh`。

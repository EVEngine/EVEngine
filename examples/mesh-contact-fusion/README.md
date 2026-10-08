# Mesh Contact Fusion（接触带融合）

并排演示 **硬拼接** vs **间隙 soft-snap 焊合** vs **实时 A←B 熔贴**：

| 位置（相机朝 −Z） | API | 行为 |
|---|---|---|
| 左 | `mergeStaticMeshes` blend OFF | 两块立方体硬相交（对照） |
| 中 | blend ON + **softSnap**，源之间留小间隙 | 对面顶点互相拉近，缝被焊住；接触权重烘成顶点色 |
| 右 | `MeshAdhereLive` | 细分圆柱 soft-snap 到地面；灰色 ghost = 熔贴前，橙色 = 熔贴后 |

要点：

- **深重叠 + soft-snap 会把缝“凹”进去**——那是最近点互相拉入体积，不是焊合。本示例中路用的是**间隙焊合**。
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
- **Soft snap**：只作用于右侧 live；关掉后圆柱回到未贴合姿态（仍保留权重逻辑）。
- **Bake live**：冻结派生后再 activate。

成功标记：

- 控制台打印 `MESH_CONTACT_FUSION_PASS hard+gapWeld+liveMelt ...`
- 当前目录写出 `mesh-contact-fusion.png`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 硬拼接 / 间隙焊合 / live 熔贴 + UI |
| `config.nut` | 1280×720、`hotReload = true` |

## 契约

可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh`。

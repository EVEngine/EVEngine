# Mesh Contact Fusion（接触带融合）

并排演示 **提交式静态合并** 与 **实时动态粘合**：

| 位置（相机朝 −Z） | API | 行为 |
|---|---|---|
| 左 | `eve.MeshMergePlan` + `eve.mergeStaticMeshes` | 默认 **关闭** 接触带融合（两块立方体硬相交） |
| 中 | 同上，`setEnableContactBlend(true)` | **可选开启** 法线/材质融合；静态路径 **关闭 soft-snap**（多源互相最近点会把缝“凹”进去） |
| 右 | `eve.MeshAdhereLive` | 立方体 A soft-snap 到地面 B；滑条改半径/强度/抬升后立即 `evaluate` + `derivedMeshResult` 上传 |

共享 CPU 原语是 `MeshContactBlend`；图节点路径 `deform.meshAdhere` 与 live 会话走同一套 A←B
最近点融合。设计说明见
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

- **Edge / Material radius**、**Strength**：脏标记 live 会话并重算派生网格（源 A 保持权威）。
- **Source lift**：抬高 A 相对地面，再 `setSource` + evaluate，观察接触带跟随。
- **Soft snap**：只作用于右侧 live 会话；关闭后 A 仍写接触权重但不贴位置。
- **Bake live**：`bakeToMesh` 冻结当前派生后重新 activate，便于对照「提交 vs 持续」。

成功标记：

- 控制台打印 `MESH_CONTACT_FUSION_PASS static=blend-off+on live=MeshAdhereLive ...`
- 当前目录写出 `mesh-contact-fusion.png`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 静态双路径 + live 会话、UI、截图 |
| `config.nut` | 1280×720、`hotReload = true` |

## 契约

可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh`：至少存活 2 秒且无错误标记。

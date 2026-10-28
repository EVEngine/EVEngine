# Swappable Tilesets 示例

这个示例把用户提供的 `Swappable Tilesets Starter 4.0` RPG Maker 风格图集接入 EVEngine，演示：

- A2 图集的 24×24 四分之一瓦片拼接；
- 同一逻辑地图在夏季、秋季图集之间切换；
- B 图集中的普通 48×48 与跨格精灵；
- 运行时画地形并重新计算外角、内角、孔洞和孤立格。

运行：

```sh
make run/linux-debug GAME=examples/swappable-tilesets
```

操作：鼠标左键绘制，右键擦除；`1` 选择沙地，`2` 选择石地，`T` 切换季节，`R` 恢复初始地图。

素材来自用户提供的本地目录 `Swappable Tilesets Starter 4.0`。用户已确认这套素材免费且可自由使用。仓库内只复制了本示例使用的三张原始 PNG。

这里的四分之一瓦片选择器用于可视化演示。它没有替代 `map` 模块的 RPG Maker MV/MZ 导入器，也不表示编辑器已经具备完整的 A1–A5/B–E 资产面板。

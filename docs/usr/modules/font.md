# 字体模块

**脚本入口：** `eve.Font()`

从文件或内存创建 FontData，并读取字形度量和位图。

## 基本用法

```squirrel
local font = eve.Font();
local data = font.newFontDataFromFile("fonts/game.ttf", 24);
```

## 对象关系与调用时机

`Font` 创建 `FontData`；FontData 保存 face 与字号，提供度量并按需栅格化 Glyph 到 ImageData。布局使用 advance/kerning，渲染资源应缓存。

## 目标导向指南

### 载入字体并计算布局

用 `newFontDataFromFile(path, size)` 创建字体数据；`getWidth(text)` 计算文本宽度，`getLineHeight()` 决定行距，居中前应先测量而不是估算字符数。

### 生成单个字形贴图

先用 `hasGlyph(codepoint)` 检查，再调用 `newGlyphImageData(codepoint)`。使用 `getGlyphBearingX/Y()` 和 `getGlyphAdvance()` 排版，不能只按位图宽度推进光标。

## 常见问题

- 按位图宽度排版：忽略 bearing、advance 和 kerning 会错位。
- 字体文件不存在仍继续：加载阶段应提供 fallback。
- 每帧创建 FontData：`newFontDataFromFile` 已走统一资源缓存（同一路径 + 字号共享一份 face，文件变化时原地刷新），但仍应避免每帧调用；`newFontData(Data, size)` 的内存路径不缓存。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getAscent()`、`getBaseline()`、`getDescent()`、`getFamilyName()`、`getFormat()`、`getGlyphAdvance()`、`getGlyphBearingX()`、`getGlyphBearingY()`
- `getGlyphCount()`、`getGlyphHeight()`、`getGlyphWidth()`、`getHeight()`、`getKerning()`、`getLineHeight()`、`getName()`、`getSize()`
- `getStyleName()`、`getWidth()`、`hasGlyph()`、`hasGlyphs()`、`newFontData()`、`newFontDataFromFile()`、`newGlyphImageData()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/font/`](../../../src/modules/font/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `font`。

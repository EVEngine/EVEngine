# PNG 文件加载 → GPU Texture 设计

> 状态：已讨论确认；实现计划见 `docs/dev/superpowers/plans/2026-08-04-png-texture-from-file.md`。
> 范围：**本轮只做 A**（文件 PNG → Texture）。Quad UV（B）、Camera2D（C）不在本 spec。
> 关联：[docs/dev/2D渲染API设计.md](../../2D渲染API设计.md)

## 1. 目标

打通并验收真实 PNG 解码路径：

`像素/ImageData` →（可选）编码为 PNG 文件 → `Filesystem::read` → `Image::newImageData` → `Graphics::newTexture` → `Sprite` 绘制。

对外提供便捷 API，避免调用方每次手写三步组合。

## 2. 非目标

- 不向仓库提交测试 PNG 资源
- 不实现 Quad / 图集 UV
- 不实现 Camera2D
- 不新增 Squirrel 绑定（本轮仍以 C++ 测试验收）
- 不做通用 Asset/Resource 模块
- 不支持压缩纹理直传 GPU（仍走解码后的 RGBA8 `ImageData`）

## 3. API

在 `eve::graphics::Graphics` 增加：

```cpp
virtual Texture *newTextureFromFile(const std::string &filename) = 0;
```

Vulkan 实现约定：

1. `Filesystem::create()`（或已有实例）`read(filename)` 得到 `FileData*`
2. `Image::create()->newImageData(fileData)` 解码
3. 若格式不是 `RGBA8`，抛 `Exception`（与现有 `newTexture(ImageData*)` 一致）
4. 调用已有 `newTexture(ImageData*)` 上传
5. 释放步骤中临时的 `FileData` / `ImageData`（Texture 仍由 Graphics `ownedTextures` 持有）

失败语义：文件缺失、解码失败、格式不支持 → 抛 `eve::Exception`，不返回 null。

## 4. 依赖与所有权

| 对象 | 生命周期 |
|------|----------|
| `FileData` / `ImageData`（加载过程） | 函数内创建，上传后释放 |
| `Texture*` | Graphics 拥有，与 `newTexture` 相同 |
| PhysFS 挂载 / identity | 由调用方在读文件前配置好（测试负责 `setIdentity` / `setupWriteDirectory` / `setSource` 等） |

Graphics 实现允许依赖 `EVFileSystem` + `EVImage`（`unit_test` / `eve` 已链接或需补齐）。

## 5. 冒烟测试（临时 PNG，不进仓库）

在 `GraphicsSmoke`（或并列用例）中：

1. 初始化 Window + Graphics（现有路径）
2. 初始化 Filesystem：可写目录 + 必要的 source/mount，使随后的相对路径可读
3. 用 CPU 像素构造 `ImageData`（如棋盘）
4. `ImageData::encode(PNG, filename, writefile=true)` 写出临时 PNG（依赖 `Image` 模块已创建，编码器可用）
5. `gfx->newTextureFromFile(filename)` 读回并上传
6. 挂到 `Renderable2D::Sprite.texture`，与纯色矩形同屏 `RenderSystem::render` 若干帧
7. 可选：测试结束删除临时文件；失败则保留便于排查

不提交 `test/assets/*.png`。

## 6. 验收标准

- [x] `newTextureFromFile` 声明于公共 `Graphics.h`，Vulkan 实现完整
- [x] 冒烟：临时 PNG 写盘 → 读回 → 窗口可见贴图精灵
- [x] 错误路径：不存在的文件名抛异常（可用 `EXPECT_THROW`）
- [x] 现有 `Batcher` / 多纹理 / 纯 `ImageData` 路径回归仍通过

## 7. 后续（不在本轮）

- **B**：Sprite Quad UV / 图集切帧
- **C**：Camera2D 组件化视口

## 8. 决策记录

| 问题 | 选择 |
|------|------|
| 本轮范围 | D：A→B→C 顺序，本轮只落地 A |
| 测试 PNG 来源 | 运行时 encode 临时文件，不进仓库 |
| 便捷 API | `Graphics::newTextureFromFile` |

# 澄蓝 · 可编辑分层母稿

`front.ora` 与 `greeting.ora` 是现在的绘画原稿。默认 demo 直接使用它们导出的
素材，不再使用旧版按裁片缩放拼装的 PNG。

每份母稿使用 768 × 1536 透明画布，包含 28 个变体分组、41 个绘制层。身体是
完整的、穿基础背心短裤的底稿，躯干和手臂没有为适应衣服而删除。身体与前景
手部分区重组已与保留的身体参考图逐像素比较。

支持两姿势、每姿势六表情、三发型、银星饰品、两衬衣、两外套、两裙子。
表情共用脸部轮廓，仅修改内部五官与脸颊。前发按两姿势分别制作，双眼可见。
领口前后、衣袖、手部、腰头分别处理遮挡；长袖海军蓝外套遮住衬衣袖，移除外套
或换成短袖披肩后，完整衬衣袖重新显示。

## 编辑与导出

1. 使用支持 OpenRaster 的绘画软件打开 `.ora`，例如 Krita 或 GIMP。
2. `_guides` 是隐藏的坐标及关节参考层；检查时显示，导出前保持隐藏。
3. 分组名为 `slot.variant`，例如 `shirt.ivory`、`shirt-back.ivory`、
   `shirt-sleeves.ivory`。可以编辑其中的绘制层，保留分组名与画布尺寸。
4. 同一件衣服分布在多个遮挡位置，切换它时同步切换上述同名变体分组。
   Avatar 中的联动由导出的槽位资料自动完成。
5. 保存 ORA，然后在本目录运行 `python export.py`（需要 Pillow、numpy）。
6. 重启 demo 查看新导出。热重载默认关闭，避免原生纹理与导出版本不同步。

导出器读取 **ORA 内的绘画层**，不会重新生成插画。支持正常 alpha 混合、绘制层
透明度和位移；不支持的混合模式、缺少的变体、未知分组会在发布前报错。
兼容性 XML 属性允许由 OpenRaster 应用保留；运行用的命名槽位采用固定版本契约。
生成成功后才原子更新 `manifest.nut`，旧版本完整素材在此之前仍有效。

首次重建使用的生成源图、对齐数据和播种工具不随仓库发布。日常编辑与导出
完全基于两份 ORA，无需这些中间文件。

身体参考 PNG 是完整性回归基准，不是绘画来源。正常改衣服不需要更新它；有意
修改锁定的身体模板时，需要同时审核并更新这一基准。

## 唯一来源与一致性边界

完整设计图 `exports/<revision>/<pose>/complete-*.png` 从母稿直接合成。
同版本分层 PNG 重组与这些完整图逐像素一致；demo 的完成图切换使用相同位置、
尺寸和纹理颜色。缩放时，逐层采样与整图采样在透明边缘存在少量差异，实际
引擎截图的误差记录在 `../../verification-master/report.json`。

这次完成图是**重建母稿的完成图**。相对最初概念图，人物比例和纹样有变化，
不宣称与最初概念图像素相同。生成提示词及来源说明见 `PROVENANCE.md`。

## 本地生成的证据（不提交）

- `verification.json`：完整身体、母稿与导出 PNG 重组的文件级检查。
- `front-wardrobe.jpg` / `greeting-wardrobe.jpg`：每姿势八种服装混搭。
- `front-expressions.jpg` / `greeting-expressions.jpg`：六表情近景。
- `../../verification-master/`：真实 Avatar 截图、完整图截图与放大差分。
- `test_master_io.py`：ORA 读回、完整身体、表情轮廓、编辑传播、非法模式拒绝、
  失败不覆盖已发布清单的回归检查。

格式依据：[OpenRaster 层级规范](https://www.openraster.org/baseline/layer-stack-spec.html)。

## 架构契约

绘画权威为 ORA；PNG 与 Squirrel 清单为
可重建导出。遮挡规则由 master_io.py 的 OCCLUSIONS 定义，并生成到运行清单。
资源由 Graphics 持有，Avatar 只借用纹理，脚本保留实例及资源引用。没有修改
引擎 C++ 公共 API、模块边界或 ECS 模型。新增元数据有 schema/version；不支持
旧版 PNG 配方自动迁移，新 demo 只消费母稿导出。

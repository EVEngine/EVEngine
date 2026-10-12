# 资源管线：从源文件到目标平台运行

asset 是原生资源包层，**没有通用 `eve.Asset()` 脚本加载器**。工具链的
`eve asset` 与运行时的 Image/Model3D/Font/Audio 绑定是不同入口。普通小型游戏可直接
用游戏目录内资源；生产管线需要稳定引用、验证和目标平台 Cook。

## 任务一：接入外部资源包

先按[Unity 导入指南](asset-unity-import.md)执行 scan，查看源索引、缺失引用和有限转换
报告；再 import 生成 `.eva`，validate 检查包结构，cook 指定平台输出 `.evpack`。
保留源包 ID 与原始 GUID，重新导入时复用逻辑身份。文件移动不能让所有对象引用失效。
扫描成功只证明识别了输入，不证明 Animator、脚本、shader 或运行时行为完成转换。

## 任务二：把资源加入游戏并发布

明确区分源包（可编辑内容）、Cook 产物（目标平台消费）和运行时资源对象（CPU/GPU/音源）。
根据实际格式选择模块：图片 → ImageData → Texture；模型 → ModelData → 渲染实体；
字体 → FontData → Graphics 字体；音频 → SoundData → Audio Source。
完整接线见[游戏开发教程](../GAME_DEVELOPMENT.md)与对应模块章节。

构建产物不能引用开发机绝对路径。验证 manifest/schema/version、缺失依赖、目标平台标签，
再在目录、归档及目标平台上启动；不能把 Windows 的纹理/着色器缓存当作所有平台通用。
恢复/导入先验证候选再替换可见资源，旧消费者持有的快照或句柄按各模块的生命周期处理。

命令行真实入口见 [AssetCommand.cpp](../../../src/modules/asset/import/AssetCommand.cpp)，
资源层实现见 [asset](../../../src/modules/asset/)，
验证用例见 [test](../../../test/) 下 asset_package、asset_cook 与 asset_unity_import 系列。

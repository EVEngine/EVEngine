# 用户文档验证记录

本记录区分源码核对、脚本执行与实际画面；绑定名称覆盖并不证明所有参数、生命周期、
平台或 GPU 输出正确。审核基准为当前仓库的绑定实现和 module_manifest，不以设计草案
或不同版本的已安装 SDK 代替。旧的“全部 API 已逐章验证”结论不再适用。

## 本轮补齐范围

1. [存档](SAVE.md)：RPGSaveSession 的整数成功投影、Tactics restoreJson 的 Result、
   fs 原子写盘、保存目录身份与有效备份；明确单文件原子性不自动跨业务域原子。
2. [导航](MODULES.md)：补上既有玩法/组合模块和新增基础/编辑卫星章节；修复 Procgen
   子目录到源码、示例、测试的相对路径，以及 GPGPU 已不存在的 shader 文件链接。
3. [ECS](modules/entity.md)：独立组件、类型查询、系统顺序、延迟结构变化、销毁和
   热重载重建，CPU/GPU 状态权威边界。
4. 玩法基础：Economy、Attributes、Decision、Definitions、Tags、GameEvent、PolicyRegistry、
   Transaction、RTS；同时补 Profiler、平台事件、资源管线与 Fluids 的实际工作流。
5. [完整游戏教程](GAME_DEVELOPMENT.md)：配置/import、规则、输入、纹理、HUD、版本化
   存档/备份、重开、热重载、调试和交付，配套 [game-tutorial](../../examples/game-tutorial/README.md)。
6. 高级模块：Graphics/Animation/Building/Editor/Procgen/Scene 的集成顺序，以及
   Rope、Scene–Physics、Procgen–Physics/Animation、RayTracing、StylizeAction、Tensor provider、
   PixelWorld 的组合与清理；编辑器/可编辑目标卫星拥有各自章节及真实示例/测试链接。

## 可以自动重现的检查

```sh
python3 scripts/generate_binding_contracts.py --json-output eve-api.json
python3 scripts/generate_user_api_reference.py --catalogue eve-api.json --check
python3 scripts/check_bindings.py --strict --quiet
python3 scripts/check_binding_gap_metadata.py
python3 scripts/check_examples.py
make check
```

源码契约目录本轮生成 11168 项、0 unresolved、0 placeholder 参数。用户章节的名称扫描
覆盖 9949 个绑定、209 个模块目录，没有缺失章节或待补名称；历史 gap 条目已移除，
metadata 的只读基准保持不变，没有新增白名单。扫描不是全平台能力验收。make check 已通过，包括其 246 项脚本测试。

补充参考由 [generate_user_api_reference.py](../../scripts/generate_user_api_reference.py)
生成；主章保留人工维护的任务、前置对象、调用阶段和失败处理。dynamic 返回类型与没有
明确接收者的记录必须继续查绑定源码，不能从生成器推断默认参数或 Result/借用规则。
修改主章后重新生成，再执行 --check；不要手工添加一个未绑定的脚本函数到参考页。

用户文档的相对链接检查排除 fenced code、外部 URL 和锚点后，所有文件/目录目标存在。
这只验证目标存在，不把源码测试链接当作本轮已经运行了那个测试。

## 直接执行文档代码

[script_user_doc_examples.cpp](../../test/integration/script-api/script_api_graphics_scene_tests.cpp) 读取 Markdown 的
squirrel 代码块，使用当前源码 Runtime 执行，覆盖经济余额/满仓、属性修饰、AI 决策与
恢复、定义版本/失效 handle、标签、事件多消费者、策略、事务及整帧性能捕获。
另执行 Rope 示例、ECS 创建/移动/延迟销毁，并加载教程实际 rules.nut
检查存档往返、坏版本/数量/额外字段、边界移动和一次命中计分。

```sh
make unit-test/win32-debug DOMAIN=scripts
ctest --test-dir build/win32-debug -R userDocs --output-on-failure
```

测试中的 ECS 图形 stub 只隔离生命周期断言，不作为实际渲染证据。完整游戏仍须通过
正式 eve run 和引擎拥有的截图路径验证。Windows 本轮 13 个 userDocs 定向用例全部通过；教程的完整 main.nut 另通过
图形/UI/文件系统 fixture 验证初始化、坏档保留、备份失败不覆盖主档和纹理原位更新。
这些 fixture 仍不作为实际输入/渲染证据。Windows 本轮仅运行这些定向用例，未运行
所有平台的完整引擎套件。

## 实际游戏验证

```sh
MIN_RUN_SECONDS=2 RUN_SECONDS=15 bash scripts/smoke_examples.sh game-tutorial
```

本轮已用当前源码的 OBJECT 引擎运行教程：15 秒烟测通过，无已知错误标记；
引擎 eve_screenshot 的 960×540 帧确认角色棋盘纹理、黄色目标及右上 HUD 可见。
正式 eve_play 的 save/restart/load 路径执行成功，实际存档落盘为 tutorial.collect|1|0。
另用显式外部测试档 score=7 验证加载、保存、重开和坏主档的有效备份恢复；
这属于存档 fixture，不代表通过输入取得分数。修改实际 main.nut 后，热重载保留状态、
纹理原位更新并重建 HUD。eve zip 生成归档，归档冷启动成功呈现首帧。
归档运行的 eve_play 当前找不到 game.agent.json 契约；正式动作验证采用目录工程。
本轮没有验证真实键盘/鼠标操作、目标平台 SDK package 或全部高级示例。

Windows 使用 Git Bash；本工作树脚本为 CRLF，运行内容相同的 LF 临时副本，
并以独立命名的当前引擎副本避免 taskkill 影响其他会话。烟测只证明程序进循环并在最小窗口内存活、没有已知错误标记。交互验收按教程逐项检查
移动、重开、主档/备份、坏档保留旧状态与热重载；截图由 eve_screenshot 捕获，不能用
桌面/Xvfb 图像替代引擎帧。现存的高级示例本轮以源码/fixture 交叉核对，没有全部重新
执行，其资源、设备和平台条件仍需相应用户手册和 CI 验证。

## 本地构建条件

本轮 Windows 构建复用预编译依赖。默认严格构建遇到 Poco 外部头 C4244，
OBJECT 配置还要求缓存中不存在的 SDL2-staticd 库名。为继续验证，仅修改 git-ignored
的生成构建规则：将缓存头作为外部头处理，并链接缓存实际提供的 SDL2d。
运行验证链接关闭 PDB 以减少内存占用。引擎源码仍保留 /W4 /WX；这不能作为默认严格构建或静态 SDK 发布通过的证据。
SHARED 游戏启动还出现必需模块注册失败，定向脚本测试通过不代表该启动路径通过。

## 当前边界与发现

- Attributes 的 min/max 历史别名分别代表 clamp_min/clamp_max，不能按数学函数理解。
- 模块内导出函数直接按名字调用另一个导出函数会发生 root lookup；教程使用模块私有
  local 闭包复用实现，并以实际 import 文件验证。
- Profiler.begin 直接绑定 const char*，普通字符串的脚本调用类型转换失败。文档采用
  可运行的整帧捕获；原生 zone 与已有 profiler.nut 的待修绑定分别说明，不宣称 zone 已通过。
- eve test 当前命令为空实现，教程不把返回 0 当作游戏测试证据。
- rayTracing 只公开能力查询；没有脚本 BLAS/TLAS/dispatch 入口。PixelWorld Thread 与
  部分 provider 通过原生组合使用，不能虚构脚本构造和推理函数。
- Texture 属于 Graphics；普通脚本重载原位更新，不每次创建新纹理。没有脚本
  releaseTexture，清脚本引用不代表 GPU 资源释放，设备重建需重新建立缓存。

## 架构规范适用性

本轮没有修改公共引擎 API、ECS 模型、持久格式或模块边界。文档按既有 Result/状态投影、
唯一 owner、Link/handle 失效、注入时间及候选验证说明调用；教程的教学存档显式携带
schema/version 并拒绝未知字段。新测试独立成文件，示例拥有 config/main/README 和导航行。
没有新增兼容 facade、架构基线或豁免。编译、源门禁与实际运行证据分别记录，不互相代替。

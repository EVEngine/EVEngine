# 物理动画烘焙与重放

目标：将 CollapseAnimation 升级为可持久化、可复用、播放不运行物理解算器的系统；
破坏碎块完成后继续覆盖刚体、布料、绳索、粒子。以下项目全部验收前，完整目标保持进行中。

## 实施与验收

- [ ] M1 文件可靠性：有界解析、内容校验、旧格式迁移、未知格式拒绝、原子且不覆盖的发布、并发和失败测试。
- [ ] M2 缓存身份：源资产及依赖内容、算法与后端版本、参数、单位、固定步长、采样策略、命名随机流种子共同参与身份；读取时验证预期身份。
- [ ] M3 播放核心：不可变共享片段、独立时间状态、暂停/变速/循环/倒放/跳转/实例空间变换；明确端点、零长度和离散状态语义。
- [ ] M4 碎块闭环：真实仿真录制、保存、跨进程重载、源世界销毁、多实例独立播放；迁移现有脚本和渲染消费者。
- [ ] M5 刚体：2D/3D 姿态录制与真实表现消费；稳定对象顺序和身份。
- [ ] M6 布料与绳索：固定拓扑顶点通道、拓扑身份与拒绝策略、真实录制与变形表现消费。
- [ ] M7 粒子：稳定粒子身份、生灭时间、位置/大小/旋转/颜色，边界不得对无关粒子插值；真实生产与消费路径。
- [ ] M8 验证：源质量门禁、领域测试、独立播放链接与运行、裁剪组合、CPU/内存/文件体积/误差数据、引擎自有截图。

## 当前验收摘要

Linux 已完成四类通道（碎块、刚体、固定拓扑变形、粒子）的录制/持久化/独立实例播放实现，
布料和绳索共用变形通道。下方进度证据按实施顺序保留，早期“待实现”只代表当时状态。

| 验收面 | 已有证据 | 尚未覆盖 |
|---|---|---|
| 纯播放裁剪 | 原生与 VM 完整清单 OBJECT/SHARED probe | 其他平台执行 |
| 图形组合 | Linux OBJECT/SHARED 完整 58 模块链接，两者各 5 个独立图形/脚本用例通过 | 跨平台；其他资源所有权组合 |
| 精度 | 真实刚体/布料/绳索基线；粒子 13 字段与生灭对照；真实拆散碎块 240 Hz 落地及降采样；短弧/整圈混叠回归 | 房屋 8 秒源姿态突变根因；其他后端与更广场景 |
| 生命周期 | 源世界/VM 释放、独立快照、mesh 退役恢复、Graphics owner 替换、材质替换与真实纹理释放重绑，OBJECT/SHARED 均通过 | 带脚本别名的粒子源删除仍禁止；自定义 shader/camera/canvas 等组合 |
| 存储 | Linux 匿名暂存、fsync、8 进程竞争发布（20 轮）、故障/SIGKILL 注入与格式准入 | 非 Linux 当前只有原子可见性；耐久性及平台验收 |
| 成本 | Release 四通道 1/16/256 实例 CPU 采样/文件体积/进程 RSS 报告 | 不代表独显性能或所有场景的实时仿真加速比 |

M1–M8 在覆盖全部适用平台与边界前保持未勾选。接下来按仍缺失的合同/证据推进，
不重复把已有场景测试当作新增功能。最终还需汇总完整使用文档、源码门禁和相关平台回归。

## 边界与不变量

录制 builder 是录制状态唯一 owner，仿真世界仍是实时物理唯一 owner。
发布后的片段不可变；播放器拥有时间与实例变换，渲染只消费采样结果。
片段不保留世界、body、ECS、VM、GPU 或临时输入指针。
源先销毁、片段先销毁均不得影响另一方；播放器持有真正共享的只读片段。
替换资产时已有播放器继续使用旧片段，新实例使用新片段；显式替换需先验证后发布。

烘焙数据承担表现，不隐式执行碰撞、伤害或其他玩法事件。实时接管需要独立的状态转换合同。
采样只依赖显式时间；固定步长与 RNG 从录制上下文注入，文件名随机数不参与模拟和缓存身份。
同一片段的浮点插值采用数值容差合同，不承诺不同物理后端重新烘焙结果逐位相等。
世界单位、2D 平面、坐标空间与实例变换写入通道合同，禁止隐含的像素/米转换。

## 现有路径与改造方式

生产者是 GeometryCollectionInstance；CollapseAnimation::capture 复制完整帧。
消费者是 GeometryCollectionRenderer 和 CollapseDust；脚本通过 CollapseAnimationBindings 调用同一 C++ API。
同一场景还使用 CollapseDust 的 EVEDUST1 粒子缓存，它仍有独立的旧式文件发布实现；
后续通道整合必须收敛其文件与时间机制，保留其领域数据和雾表现策略。
当前 main.nut 将 config.nut/house.json/main.nut 字节拼接生成键，未覆盖物理后端和算法身份；
任何 load 失败都进入重新烘焙，但目标若仍存在，save 会拒绝。M2/M4 必须区分不存在、
损坏、身份不匹配与不可读，使用明确的新版本发布/重建策略，不能吞掉诊断或覆盖旧资产。
将 capture 的活世界依赖移到独立 TU，纯文件与采样 TU 只依赖 owning presentation 值。
先通过不链接 solver/graphics 的真实可执行文件证明 L2/L3 播放边界，再推进独立可裁剪运行时模块。
这项局部证据不能替代整套 manifest 模块裁剪验收。

EVECOLL1 v1/v2 作为历史输入迁移，保留已发布的兼容能力；历史文件没有校验，不能声称已验证其原始完整性。
新 v3 文件使用固定 32 字节非密码学 contentKey 校验尾部，覆盖前面的全部字节；仅检测意外损坏，不用于安全认证。
旧文件重新保存成为 v3。未知版本、额外数据、非有限数、越界和不一致拓扑拒绝。
文件发布先生成完整候选，在目标同目录的独占临时目录写完关闭，再以不覆盖的硬链接原子发布。
不支持硬链接的文件系统返回明确失败；原子可见性不等于断电持久性，fsync/平台耐久写入仍需单独实现和验证。
失败清理仅删除本次拥有的临时文件和目录，不删除目标。并发发布只有一个成功者。

## 架构适用项

R-BOUND-1/3：使用真实链接和运行证据，不从 include 图推导裁剪。
R-FACE-1/5：数据与脚本面留在 physics 宿主，不向 common 增加领域状态；不恢复已删除的 JSON catalogue。
R-MECH-1：录制是已知单源逐帧复制，播放是已知片段逐实例采样，直接调用；文件发布需要全成或全不成。
当前不新增 ECS System、跨域 Link、provider 注册表或回调。未来 ECS 接入前另列 View/read/write/phase。
R-MECH-2/3/5：采样热路径登记在源码检查器，不做字符串模块查找。
R-COST-1/2/4/5：录制与 IO 明示线性成本和准备阶段；时间与状态查询不做 IO，不添加无意义 builder。
所有可失败操作使用 checked Result；准备失败不改变已发布对象。没有批准的架构例外。

## 进度证据

2026-10-21 第一批实现：

- 新保存输出 v3，校验覆盖头、几何与帧；v1/v2 仍可迁移。加载只使用一个有上限的序列化输入缓冲。
- 独占临时目录与不覆盖硬链接发布已实现；普通失败不删除目标，两个并发保存者只能有一个成功。
- BonePresentation 提取为无世界依赖值类型，录制移到 CollapseAnimationCapture.cpp。
- `test/profile/physics_bake` 使用真实生产 codec/sampler/几何校验，链接行没有 solver、graphics、script 或 section GC；GCC Debug 编译成功。
- 5 个独立 CTest 用例通过：旧格式升级与采样、数值损坏、嵌入几何升级/生命周期、并发发布、失败保留目标。
- capture TU 使用 GCC C++20 `-Wall -Wextra -Werror -fsyntax-only` 检查通过；这不替代实际世界录制的链接与运行。
- 架构改动行检查和 `git diff --check` 通过。完整 `make check` 首次在 4 个道路测试导入 NumPy 时失败；工具环境补齐依赖后重跑通过（base=origin/dev，460 个脚本测试）。
  本地工具环境为 `/tmp/evengine-bake-tools`；构建目录 `/tmp/evengine-1782-physics-bake`，源质量日志 `/tmp/evengine-1782-bake-check.log`。

本批不代表 M1 全部完成：断电耐久写入、进程中断遗留暂存清理、写入阶段故障注入、
不支持硬链接的文件系统与 Windows 宿主仍需验证。M2–M8 保持未完成。

2026-10-21 播放核心推进：

- 新增 CollapsePlayback：prepare 按值冻结录制，复制播放器共享只读样本但独立持有时间、速率、暂停和循环状态。
- 显式注入非负 elapsedSeconds；负速率倒放，seek 独立于暂停/速率且夹取端点，循环前进使用半开区间，非有限输入与溢出失败保留时间。
- 独立无 solver 播放 profile 编译通过，6/6 分进程 CTest 通过；新增用例验证源片段替换、共享身份、时钟独立、暂停、跳转、反向循环和失败事务。
- 架构改动行检查与 diff 空白检查通过。此批完整 make check 已启动，结果以 /tmp/evengine-1782-bake-player-check.log 为准，尚未据此声明通过。
- M3 仍未完成：实例空间变换、脚本/渲染消费者、更多端点及生命周期测试仍待接入；M2 缓存身份仍待实现。

2026-10-21 播放器与消费者接入：

- CollapsePlayback 增加刚体实例变换（归一化 XYZW 四元数，绕烘焙原点旋转后平移，米制）。非法变换不修改原状态，采样变换溢出返回失败。
- 渲染卫星新增 preparePlayback/submitPlayback，复用既有网格、正向/阴影/深度路径，提交失败禁用前一次提交。播放采样只复制值，不保留 player 指针。
- Squirrel 新增 createPlayback、clone 和时钟/变换接口；房屋示例与灰尘表现从同一个原生播放器读取时间，移除脚本独立时间与暂停状态。
- 生产编译参数下 CollapseAnimationBindings.cpp、DestructionFx.cpp、FractureRenderer.cpp 语法编译通过。生成的绑定合同包含新接口；例子布局及架构改动行检查通过。
- 无 solver profile 的 7/7 测试通过，新增旋转/平移、实例隔离、非法四元数、零时长循环、显式末帧、溢出后恢复覆盖。
- 新增真实 VM 生命周期测试，深度渲染测试改为源实例和世界销毁后使用播放器；目前仅写入，未运行，不能当作通过证据。
- 上一轮完整 make check 已结束通过（460 个脚本测试）；本轮更新后的门禁在 /tmp/evengine-1782-bake-player-check2.log，仍需检查终态。
- 完整 make debug JOBS=4 首次遇到缺少 Vulkan 开发包；系统 sudo 需要交互认证，改用 apt-get download 将发行版开发包解包到 /tmp/evengine-1782-sdk，VULKAN_SDK 指向其 usr 目录后配置通过。
  此后构建在缺少 vkbuilder.hpp 停止，正在初始化固定版本 external/VKBuilder 与 external/ik.hpp，再继续原宿主构建。宿主运行、画面和 Windows 均未验证。
- 根构建自动应用了仓库管理的 ECS.hpp/zeroerr 补丁，产生子模块工作区脏状态；不提交这些子模块修改。

本轮终态更新：

- `/tmp/evengine-1782-bake-player-check2.log` 完整源质量门禁 exit 0，460 个脚本测试通过（68.065 秒）。最后修改后架构检查与 diff 空白检查再次通过。
- VKBuilder 和 ik.hpp 已初始化到仓库固定提交，重新配置 SHARED 宿主构建后继续编译；当前运行 handle 41197，日志 `/tmp/evengine-1782-bake-host-build.log`。下一轮先检查该 handle，不重复启动并发构建。
- native player、脚本绑定、渲染消费者和示例修改已经存在；真实 VM 测试、深度渲染测试和示例烟测仍待宿主构建完成后运行。M2/M4–M8 尚未完成，M3 的运行组合验收也未完成。

2026-10-21 配方身份与 v4 文件：

- 新增 CollapseBakeRecipe（有界 owning builder）：算法/构建修订、后端修订、参数摘要、最多 64 个命名依赖摘要、固定 float 步长、采样 tick 间隔、最多 64 个命名 uint64 随机流种子。
- EVECREC1 采用长度前缀、固定字段顺序、名称排序，单位固定米/右手/Y-up；未知版本、尾部、重复/非规范顺序、越界和无效 token 拒绝。配方上限 64 KiB。
- beginBake 只能在首帧前冻结配方，不能给已有录制重新贴身份。带配方保存为 EVECOLL1 v4（网格数量之后长度+配方，最后 checksum 覆盖全部字节），无配方仍保存 v3。
- loadForRecipe 验证完整 canonical 配方，缺失文件 NotFound、身份不符 Conflict、历史无身份文件 PreconditionViolation；普通 load 保留显式历史加载能力。失败不替换现有脚本 clip。
- capture/load 校验配方的采样时间网格；容差为 4 个 float epsilon 相对时间、上限四分之一个采样间隔，首帧严格为零。
- Squirrel 配方与 beginBake/loadForRecipe 已接入；配方 key 使用 common Value 统一 Result 投影。脚本 seed 当前接受非负 int32，native 为 uint64。
- 示例仅在 not_found 时自动烘焙，显式 playbackAsset 不存在、损坏和不可读直接失败；灰尘 loader 同样区分缺失/不可读并拒绝 NUL 路径。
- 独立无 solver profile 8/8 通过，含 v4 独立夹具和逐字节往返、版本/依赖/参数/种子/步长改变身份、加入顺序不影响身份、64 项预算、完整 uint64 种子、离网格时间即使 checksum 正确也拒绝。
- 真实世界录制测试升级为 beginBake + loadForRecipe；VM 测试扩展配方 builder 与失败保留。这些 host 用例尚未运行。
- 完整 source gate `/tmp/evengine-1782-bake-recipe-check.log` exit 0：460 个脚本测试、70.529 秒。后续灰尘小改动的架构、示例布局、diff 检查通过。
- 已按宿主实际编译参数 syntax-check CollapseAnimationBindings、CollapseBakeRecipeBindings、CollapseAnimationCapture；修复 key Result 必须显式投影为 Value::string。灰尘 TU 检查 handle 84232。

下一步与构建状态：

- 宿主 make handle 41197 已在 unit_test_physics 链接出现本轮新 recipe 方法的 undefined reference；这是运行期间新增源文件没有进入最初配置/库对象的结果。其余编译任务还在收尾，先检查终态，再重新 CMake 配置并重建（必须纳入 CollapseBakeRecipe.cpp 与 CollapseBakeRecipeBindings.cpp，并重编译 CollapseAnimation 布局的消费者），不要运行当前中间态二进制作验收。
- 当前根构建环境：VULKAN_SDK=/tmp/evengine-1782-sdk/usr，PATH 加该目录 bin，LD_LIBRARY_PATH=/tmp/evengine-1782-sdk/usr/lib/x86_64-linux-gnu；make debug JOBS=4 CMAKE_EXTRA_ARGS=-DEVENGINE_MODULE_LINKAGE=SHARED。
- M2 尚未闭合：house-collapse 默认缓存仍用旧 bakePath，尚需从实际 producer/backend 构建信息生成配方并接入 loadForRecipe/beginBake，不能仅填长期不变的显示名称冒充版本。应处理未提交 C++ 改动导致实现变化的身份失效（构建源码指纹），并保留显式 playbackAsset 的跨平台资产播放语义。
- M1 耐久写入/中断恢复、M3/M4 host 运行及跨进程多实例、M5–M8 原目标均继续保留。

2026-10-21 默认缓存闭环与真实进程证据：

- BakeBuildIdentity.cmake 随 physics_destruction 配置生成 producer/Box3D 依赖标识，覆盖引擎/脚本/物理/资产/schema 源码内容、构建规则、依赖补丁及编译器/ABI/选项；目录依赖让未提交源码修改触发重新配置。无墙钟和绝对 checkout 路径。
- 新的真实 CMake 测试验证重复配置与路径迁移稳定、源文件内容修改和新增文件在普通 build 时更新标识、后端构建选项改变标识；该测试通过。
- house-collapse 默认使用原生构建指纹 + Box3D 修订 + config/模型/main/dust 脚本摘要 + 固定步长 + fracture seed，执行 beginBake/loadForRecipe。显式 playbackAsset 保持便携播放；生产世界显式选择 box3d。
- 完整 SHARED 宿主 make debug JOBS=4 已通过（/tmp/evengine-1782-bake-host-rebuild.log 和 bake-host-finalbuild.log）；随后按真实 VM 测试发现的问题再次定向编译 physics，尚待末尾记录该轮结果。
- 真实 VM 首轮发现 clone 是 Squirrel 关键字，接口改名 copy；随后发现严格指针转换在 null 进入 lambda 前抛异常。新增类型标签校验 helper，配方/播放器参数先读 ssq::Object，再返回 checked ResultRef，覆盖 null 与错误类输入；修复后的 VM 复测待完成。
- 真实世界录制/v4往返/销毁世界后加载等 native 测试 11 项通过；当时唯一失败项为上述 VM 入参问题，不将整组记为通过。
- 深度渲染测试通过：源实例和 World3D 已销毁，播放器变换后的网格仍正确改变深度，renderer 销毁后深度注册过期。使用 Lavapipe 软件 Vulkan，不是独显性能证据。
- 窗口初始化最初失败由沙箱 X11 socket 限制导致：xdpyinfo 无法连接，同一 SDL 最小探针在允许本地通信后 init=0；正式窗口测试/烟测用沙箱外 Xvfb。
- 120 秒 house-collapse 官方烟测通过，产生真实 v4 文件 cottage-faacc0039a21c49593ac0b817c5651d6.evecollapse（25,631,534 字节）及灰尘文件。缓存均 git-ignored，不提交。
- 第二个独立引擎进程 /tmp/evengine-1782-house-reload.log 明确打印 loaded saved animation without cook or physics world、loaded saved smoke, no fluid simulation；eve_init 约 3.27 秒，first present 约 6.66 秒，20 秒结束为 timeout 124（预期停止）。
- 新增 physics_bake_inspect 独立程序，链接仅自身 main、生产 codec/player/recipe/geometry 静态库及 zeroerr，无 solver/renderer/VM/capture 或 section GC。读取上述真实资产成功：507 碎块、961 帧、16 秒；两实例正反向和不同 placement 共采样 121680 姿态。Debug CPU 采样 59.032 ms，不含加载/上传/渲染，不能当 Release 性能结论。日志 /tmp/evengine-1782-bake-cross-process.log。
- 无 solver profile 重构为共享生产静态库供 test 与 inspector 使用，8/8 专项用例仍通过。probe 已列入 test_levels。
- 构建指纹版本 make check 已通过 461 个脚本测试（70.430 秒）；新增参数校验/probe 后的最新门禁 handle 19472，日志 /tmp/evengine-1782-bake-current-check.log。

继续事项：先检查 physics 定向重编译 handle 24832（/tmp/evengine-1782-bake-vm-rebuild.log），再复测 VM 与渲染；不要启动重叠构建。旧的 41197/47020/31731 均终态。当前有效窗口测试须允许本地 X11 通信。
M1 耐久写入/中断恢复、M3 更多失败生命周期、M4 引擎自有截图及同时多实例表现、M5–M8 仍需继续。默认配方闭环已有实测，但不把它替代完整目标。

本轮最终运行更新：

- physics 定向重编译 handle 24832 已 exit 0；修复后的 native/VM 组合 12/12 全部通过（/tmp/evengine-1782-bake-native-tests.log）。
- 最终深度 + 灰尘生命周期组合 2/2 通过，2.85 秒（/tmp/evengine-1782-bake-composition-tests.log）；两个输入检查修复未破坏现有渲染/缓存消费。
- 120 秒烟测与随后独立进程复用发生在本轮最后一次 null/type 入参加固之前；该加固已由上述最终 VM 与渲染测试验证，但没有将此前烟测冒充为再次执行。
- 最新完整源质量门禁 handle 19472 已 exit 0，461 个脚本测试通过（68.118 秒），日志 /tmp/evengine-1782-bake-current-check.log。当前本轮所有构建/测试/烟测 handle 均已终态，没有需要继续等待的构建。

2026-10-21 Linux 耐久屏障：

- CollapseAnimation 保存已在 Linux 上对完整暂存文件执行 fsync，再执行原有不覆盖硬链接发布，最后 fsync 目标父目录；EINTR 重试，其他错误返回 checked Result。
- 发布前刷盘失败不创建目标；发布后目录屏障失败不撤回已可见目标，诊断提供 publication=published / not_published 与 durability=unconfirmed，调用者可以明确区分状态。已有文件从不删除或覆盖。
- Linux 独立 profile 新增链接期 --wrap=fsync 故障注入，生产代码没有环境变量/全局测试开关。验证真实文件/目录描述符的顺序、EINTR、两阶段 EIO、发布状态、失败后可读性、重试冲突及临时目录清理。
- GCC 严格编译与无 solver profile 9/9 用例通过。这是操作系统调用及注入证据，不是实际断电实验；硬件/文件系统仍必须兑现 fsync 语义。
- 此增量未改变格式、采样、ECS 或模块边界。适用 Result、持久化失败语义、R-MECH-1 直接调用及 R-COST-1 准备阶段 IO；没有架构例外。
- Windows/macOS/Web 当前仍只有原有原子可见性，未验证其耐久屏障；进程中断遗留清理、灰尘文件发布整合和完整通道扩展仍待完成，M1 与总目标保持进行中。
- 本轮完整 make check 日志：/tmp/evengine-1782-bake-durability-check.log；终态另记。

耐久屏障本轮交接状态：格式化后独立 profile 再次编译并通过 9/9（/tmp/evengine-bake-durability-tests.log）。完整 make check handle=1142 仍存活，停留在绑定文档覆盖检查；宿主进程已确认 python3 正在执行。后续先轮询该 handle，不重复启动门禁。本轮未重建完整引擎或重跑图形烟测。

2026-10-21 共享资产发布：

- 上一轮完整 source gate handle 1142 已终态 exit 0：461 脚本测试通过（68.022 秒）。
- 提取 BakedAssetFile 为 physics/destruction 内部宿主的直接调用边界，CollapseAnimation 和 CollapseDust 两个实际生产消费者使用同一发布实现。保留 EVECOLL/EVEDUST 格式各自所有权，不增加 common 状态或新模块。
- 灰尘保存不再使用 exists 后直接 ofstream 截断目标的竞态路径；完整序列化后调用不覆盖发布器，拒绝 NUL 路径，冲突返回结构化 Conflict。Linux 共用耐久屏障和失败诊断。
- R-MECH-1：已知两个消费者的低频同步发布使用直接函数；R-COST-1：IO 明示线性成本、仅准备阶段；导出 EVENGINE_API_DOMAINS，API 标注所有权与线程语义。无新增 ECS/Link，无架构例外。
- 共享实现独立 profile 9/9 通过。灰尘宿主用例已加入两个并发保存者只有一个成功、获胜资产完整可读，以及 NUL/Conflict 检查；其运行验收仍待构建完成。
- 宿主物理构建 handle 45649，日志 /tmp/evengine-1782-bake-shared-publisher-host.log；完整源质量检查日志 /tmp/evengine-1782-bake-shared-publisher-check.log。后续先收取现有任务终态，不重复启动。

共享发布器后续状态：宿主 handle 45649 终态 exit 2，失败仅为新增测试 REQUIRE 的逻辑或触发 zeroerr 运算符重载歧义；生产实现已编译/链接。已将完整判定先求值为 const bool，再交给 REQUIRE。重建 handle 89396 正在运行（日志 /tmp/evengine-1782-bake-shared-publisher-host-retry.log），源质量 handle 33600 也仍在运行。后续收取这两个句柄，并运行 physics_collapse_dust.savedGasOutlivesSolverAndRejectsPartialLoad 及 physics.bake.*；不能将已写入的灰尘并发断言视为运行通过。

2026-10-21 共享发布器终态与引擎截图：

- 宿主重建 handle 89396 exit 0，unit_test_physics 编译链接成功。完整 source gate handle 33600 exit 0，461 脚本测试通过（69.752 秒）。
- 实际运行 13/13 通过：physics_collapse_animation.*、physics.bake.* 和 physics_collapse_dust.savedGasOutlivesSolverAndRejectsPartialLoad；包括灰尘两个并发发布者唯一成功、Conflict 和完整重载。日志 /tmp/evengine-1782-bake-publisher-host-tests.log。
- 使用 /tmp/evengine-bake-visual 中的示例副本，将 playbackAsset 显式指向先前已烘焙的 507 碎块资产；运行日志 /tmp/evengine-1782-bake-visual.log 明确确认动画与烟雾均从文件加载、没有 cook/物理世界/流体仿真。
- 引擎 MCP eve_screenshot 返回成功，取得 1280x860 的 /tmp/evengine-bake-playback.png；通过 eve_input_batch 触发示例既有数字 5 快捷键并等到 eve_input_poll ready，再取得 /tmp/evengine-bake-final.png。已实际查看两张图：完整房屋与 15.9 秒倒塌碎块/烟雾都呈现，地面与阴影可见。截图来自引擎回读，不是 Xvfb 帧缓冲。
- 此运行显式选择 Mesa Lavapipe ICD，仅证明软件 Vulkan 上的表现；不声称独立 GPU 性能、像素精确正确性或多实例画面验收。示例副本只改变资产路径，没有使用 eve_eval 充当玩法。
- M4/M8 新增单实例视觉证据；同时多实例画面、崩溃恢复、跨平台耐久写入和刚体/布料/绳索/粒子通道仍未完成。总目标保持进行中。

2026-10-21 多实例碎块表现：

- house-collapse 新增 M 对照模式：copy() 共享冻结 CPU 样本，独立时钟/变换，独立 GPU 渲染器；左侧平移 -8m，右侧 +8m。右侧 P 暂停、B 反向、9/0 首末帧，循环倒放；左侧原有控制不变。
- 退出模式释放第二 renderer/player，主实例保留时间并恢复原点。昂贵 renderer 准备仅发生在用户进入模式时，不在每帧采样中进行。比较模式明确不画尚不支持实例变换的灰尘；没有声称粒子通道已完成。
- 真实引擎加载既有资产，无新仿真。引擎 MCP 输入与截图验证并查看：/tmp/evengine-bake-multi.png 左 0.0s / 右 15.9s；/tmp/evengine-bake-multi-swapped.png 左 15.9s / 右 0.0s；/tmp/evengine-bake-multi-running.png 左仍 15.9s / 右独立倒放到 4.0s；/tmp/evengine-bake-multi-released.png 释放第二实例后主实例在原点保持 15.9s。两实例均有材质/阴影，截图由引擎回读产生，Lavapipe 软件 Vulkan。
- 同一批多个不同按键输入仅观察到第一个事件生效，因此验收改为单独输入、等待 input_poll ready，再截图；未使用 eve_eval 或把工具注入成功当作游戏响应证据。
- EveScript 技能与实际绑定合同已查阅，examples layout 与 git diff --check 通过。本轮不改变公共 API、持久格式、ECS 或模块边界；沿用 Result 检查与显式 dt，无架构例外。
- 完整门禁 handle 49234（/tmp/evengine-1782-bake-multi-check.log），标准 120 秒示例烟测 /tmp/evengine-1782-bake-multi-smoke.log 尚待收取终态；视觉运行 handle 58588 有 180 秒 timeout。

多实例本轮交接：视觉运行 58588 已按 180 秒 timeout 结束（exit 124，预期），绑定合同生成 37424 exit 0。源码门禁 49234 仍在脚本测试阶段，标准 smoke handle 45496 仍在 120 秒存活窗口。后续先收取这两个现有句柄；本轮尚不声明它们通过。

2026-10-21 灰尘完整性：

- 双实例上一轮 source gate 49234 exit 0：461 脚本测试通过（108.810 秒）；标准 120 秒 house-collapse smoke 日志已记录 PASS、1 passed/0 failed。
- 灰尘新保存输出 EVEDUST1 v2，尾部增加与碎块相同的 32 字节非密码学校验，覆盖头和全部样本。读取在文件总预算（16 + 12*MaxFrames + 28*MaxSamples + 32）内一次加载；验证完整性后再解析至候选帧，成功才替换原片段。
- v1 保留为无原始完整性保证的历史输入，重新保存迁移 v2；未知版本、尾部及超预算拒绝。HashMismatch 使用结构化诊断，不以文案分支。没有第二份状态 owner，不改变灰尘表现采样语义。
- 宿主灰尘用例增加有限粒子坐标位损坏拒绝、失败保留、v1→v2 字节一致迁移；原结构非法输入重新计算合法 checksum，确保仍然检验结构而不是仅因 checksum 失败而通过测试。
- 适用持久格式版本/迁移/未知字段政策与 Result、原子恢复规则；无架构例外。本轮尚未以新增断言写入代替运行证据。
- 宿主构建 handle 22719，日志 /tmp/evengine-1782-bake-dust-integrity-build.log；完整 source gate handle 10523，日志 /tmp/evengine-1782-bake-dust-integrity-check.log。先检查终态，再运行灰尘持久化用例。

2026-10-21 Linux 中断回收：

- 灰尘完整性 source gate 10523 exit 0，461 脚本测试通过（67.751 秒）。宿主构建 22719 exit 2，失败为测试 lambda 未捕获 zeroerr 上下文；已改为捕获引用，不能将这一轮算作宿主测试通过。
- Linux 发布实现改为打开目标父目录、O_TMPFILE 匿名暂存、循环处理 EINTR/短写、fsync 数据、通过 /proc/self/fd + linkat 不覆盖发布、fsync 父目录。发布前进程中断由内核回收匿名 inode，不创建新暂存目录；发布后保留完整目标。
- 已核对 Linux man-pages open(2)/linkat(2) 合同：https://man7.org/linux/man-pages/man2/open.2.html 与 https://man7.org/linux/man-pages/man2/linkat.2.html。使用 procfs 链接避免要求 CAP_DAC_READ_SEARCH；不支持 O_TMPFILE 明确 Unsupported，不退回有名字的暂存。当前 /tmp 已实测支持。
- 独立 profile 9/9 通过，新增实际子进程 SIGKILL 注入：数据 fsync 前无目标/无暂存残留，目录 fsync 前已有完整可加载目标/无暂存残留；原校验、并发、生命周期测试保持通过。另注入短写+EINTR、ENOSPC、零写、O_TMPFILE 不支持，以及两道 fsync EIO。日志 /tmp/evengine-bake-anonymous-tests.log。
- 这是进程中断证据，不是断电实验。其他平台仍使用原有具名暂存实现；历史暂存不会自动扫描删除。跨平台耐久验收仍未完成。
- API 已明确所需文件系统能力、所有权、成本、线程与已发布失败语义；R-MECH-1 直接同步调用，Result/持久化事务规则适用，无架构例外。
- 宿主重建 handle 11871（/tmp/evengine-1782-bake-anonymous-host.log）与本轮源质量 handle 2494（/tmp/evengine-1782-bake-anonymous-check.log）仍在运行。后续先收取终态，运行灰尘 v2 持久化及 physics.bake.* 用例，不重新启动活任务。

2026-10-21 宿主组合验收与刚体边界审查：

- 匿名暂存宿主重建 11871 exit 0。重新运行灰尘持久化 + physics_collapse_animation.* + physics.bake.*，13/13 通过；日志 /tmp/evengine-1782-bake-integrity-host-tests.log。灰尘校验/迁移断言这次已实际执行，不再只有源码证据。
- 刚体生产 API 证据：Body.h getX/getY 使用像素、getAngle 使用弧度；World.h getMeter 提供像素/米转换。Body3D.h 使用米和 XYZW 四元数。PhysicsHandles.h 明确 runtime handle 不能持久化；World/World3D::findBody(handle) 拒绝 stale/foreign handle。
- 下一阶段实现约束：资产轨道使用调用方提供的稳定持久身份；录制绑定持有 generation-qualified handle，只在捕获调用期间解析借用，不跨帧保留 Body 指针。冻结后的轨道顺序固定，来源重排按身份归位，缺失/重复/新增身份在固定拓扑录制中事务性拒绝。
- 2D/3D 坐标空间必须显式区分。不能直接把像素塞进目前固定 metres-rh-y-up 的 CollapseBakeRecipe；2D 像素/弧度与 3D 米/四元数采用明确通道合同，转换参数进入身份。不能仅从“2D”推断 Y 轴朝向并隐式翻转。
- 当前 CollapseAnimation 含破坏专用 FractureGeometry，不能将其直接挪到更低层通用 bake 模块造成反向依赖。刚体扩展需保留领域几何在破坏宿主，逐步复用纯值轨道/时钟/发布机制；新增模块时更新真实 manifest、消费路径及裁剪组合证据，不建立第二份手写目录。
- 以上为源码审查所得实现约束，尚不是刚体通道实现或验收。总目标及 M5–M8 保持进行中。

2026-10-21 刚体纯值轨道与模块边界：

- 上一轮匿名暂存完整门禁 2494 exit 0：461 脚本测试通过（68.001 秒）。
- 新增 physics/bake 宿主卫星（physics_bake，L3，EVPhysicsBake），仅在既有 physics manifest 片段声明。BakedAssetFile 下移该模块，导出改为 EVENGINE_API_BACKENDS；碎块与灰尘两个真实消费者均更新包含路径和依赖。破坏专用 FractureGeometry 留在原领域，没有反向 include。
- 新增 RigidPoseTrack：显式 PlanarPixels/Metres3D、PersistentId roster、逐帧重排归位、非零唯一稳定身份、有限时间/姿态、归一化 XYZW 四元数、不可变共享 owning 数据、按身份采样及端点夹取。2D 不隐式转换单位或翻转轴；3D 不混入像素。采样热路径已登记，不做 IO/solver/module lookup。
- 独立 profile 使用真实生产实现通过 11/11；新增两例验证稳定身份与顺序、复制后源替换、未知身份、非法枚举、时间/样本/四元数拒绝和 2D/3D 空间区分。日志 /tmp/evengine-bake-rigid-tests.log。该 profile 是 L2/L3 生产对象的局部证据，不替代完整 manifest 裁剪。
- 模块接口六面与当前能力边界记录在 docs/usr/modules/physics_bake.md。Result、拥有值数据、线程/生命周期、成本分层及 R-BOUND-1/3、R-MECH-1/3 适用；无 ECS/System/Link/provider/callback 新增，无架构例外。
- 本步未实现刚体 live-world 捕获、持久化、播放器/渲染适配，不将纯值测试当作 M5 完成。下一步必须接入实际 body 生产者与表现消费者，并复用公共时钟/存储机制。
- 新模块完整门禁 handle 41982（/tmp/evengine-1782-bake-rigid-check.log）、宿主编译 handle 1829（/tmp/evengine-1782-bake-rigid-host.log）运行中；后续先收取这两个句柄终态。

2026-10-21 刚体活源采集：

- 上轮新模块门禁 41982 终态 exit 2：physics_bake 声明缺少源检查器要求的 L3 标题；已补 L3/L5 标题，profile_matrix --check 通过。宿主 1829 exit 2：BindingContracts 重新生成发现原有 beginBake/loadForRecipe 的 argument 参数被视为占位符，已改为 recipeObject，没有绕过门禁。
- 新增 physics_bake_capture 录制卫星（L5，依赖 physics 与 physics_bake），纯值父模块不依赖录制卫星。两个 captureRigidFrame 重载真实读取 World/Body 和 World3D/Body3D，2D 保留像素 XY 并将弧度角转为 Z 四元数，3D 保留米与 XYZW。
- RigidBakeBinding 明确持有独立持久 ID、world runtime handle、body runtime handle。拒绝 nil/重复 ID、重复 body、跨世界与过期 handle，采集只产生拥有值的完整帧，无 world step、无时间/RNG 生成、无回调或帧间裸指针。世界恢复需重建绑定；六面与双向生命周期见 docs/usr/modules/physics_bake_capture.md。
- 新增真实 2D/3D 物理推进、两帧采集、源销毁后插值、跨世界/过期绑定拒绝测试，归属 integration/physics-bake。尚待运行，不能以源码代替通过。
- 现有完整宿主构建 handle 20770（/tmp/evengine-1782-bake-capture-host.log）、源码门禁 handle 13951（/tmp/evengine-1782-bake-capture-check.log）进行中。后续先收取终态，运行 physics.bake.capture* 及已有轨道/发布回归。
- 公共 API 使用 Result、owner-thread 借用和明确 @cost；数据面 typed handle 是显式合同，不建立 ECS/System 或 provider 注册。无架构例外。持久化、渲染消费及其余通道仍未完成。

2026-10-21 刚体资产文件：

- 采集阶段宿主构建 20770 exit 0，完整门禁 13951 exit 0（461 脚本测试，68.624 秒）。实际运行两个 capture 用例 2/2 通过：2D/3D 世界推进、过期拒绝、源销毁后插值成立。日志 /tmp/evengine-1782-bake-capture-tests.log。
- 新增 EVERIGD1 v1 显式刚体资产：小端 version/space/trackCount/frameCount，规范排序的 16 字节 PersistentId，时间与七 float 姿态，32 字节非密码学校验尾部。加载有界读取、先校验、再精确长度/身份顺序/时间/姿态准入；未知版本、尾部和损坏拒绝，成功才返回只读轨道。首版无历史迁移。
- 摘要实现下移 physics_bake 的 bakedAssetContentKey；CollapseAnimation::contentKey 保留兼容 facade，原算法/文件校验不变。碎块、灰尘、刚体使用同一发布实现；没有向纯值模块引入 solver/graphics。
- 文件目前不携带烘焙配方，只允许显式资产加载，不能据此自动缓存命中。M5 的完整身份准入、播放器与真实表现消费者仍需完成；不能把此阶段缩小为最终成功。
- 独立无 solver profile 13/13 通过，含刚体双坐标空间往返、Conflict 保留、有限数值损坏、正确校验下的非法头/ID/尾部、截断；日志 /tmp/evengine-bake-rigid-file-tests.log。
- 真实 capture 用例已进一步接入 save/load 后采样，但修改后的宿主断言尚待运行。宿主构建 handle 33161（/tmp/evengine-1782-bake-rigid-file-host.log），完整门禁 /tmp/evengine-1782-bake-rigid-file-check.log 运行中；先收取终态。
- 适用版本化持久格式、Result、原子恢复、显式坐标、R-BOUND-1/3 与 R-COST-1/2；无架构例外。

2026-10-21 共用时间组件：

- 刚体文件阶段宿主 33161 exit 0，完整门禁 63087 exit 0（461 脚本测试，68.315 秒）。实际 capture + rigidAsset 用例 4/4 通过，已包含真实世界采集、销毁、保存/重载后插值；日志 /tmp/evengine-1782-bake-rigid-roundtrip-tests.log。
- BakePlaybackClock 提取暂停/速率/循环/跳转与数值失败语义；CollapsePlayback 改为组合该组件，RigidPlayback 成为第二个实际消费者。片段保有唯一 duration，时钟只保有时间/速率/暂停/循环值，不持有世界或读取系统时钟。
- RigidPlayback 直接以常数成本构造，共享不可变轨道，复制时独立时间状态，按 PersistentId 采样；没有给廉价操作增加准备 ceremony。新热路径已登记。刚体实例变换与表现消费尚待接入。
- 独立 profile 13/13 通过，旧碎块时钟边界回归继续通过，刚体用例增加双播放器独立 seek/倒放/循环/暂停/非法 dt 保留断言；日志 /tmp/evengine-bake-clock-tests.log。
- 已 touch 所有直接包含 CollapsePlayback.h 的 src/test 消费者以强制重编译布局。宿主构建 handle 4629（/tmp/evengine-1782-bake-clock-host.log），源质量 /tmp/evengine-1782-bake-clock-check.log 进行中，后续先收取终态。
- 适用可组合单一状态 owner、显式时间、Result 失败事务、R-COST 低成本直接调用、R-MECH 直接组件调用；无新增 ECS/Link/provider/callback，无架构例外。

2026-10-21 共用实例变换：

- 上轮时钟宿主构建 4629 exit 0；源质量 65477 因模块测试证据过期失败，已用 test_module_coverage.py --write 更新，未跳过检查。
- BakeRigidTransform 提取归一化四元数旋转、平移及溢出诊断；CollapsePlayback 与 RigidPlayback 组合同一值组件，复制后的实例变换独立。2D 像素轨道拒绝离开 XY 平面的平移/旋转，3D 保留米单位，无隐式轴转换或缩放。
- 独立无 solver profile 13/13 通过（/tmp/evengine-bake-placement-tests.log），覆盖独立实例、平面约束、空间旋转与原碎块回归。初次提取编译错误已修复，以修复后运行结果为准。
- 已 touch 直接包含两个播放器头的消费者。宿主构建 27524（/tmp/evengine-bake-placement-host-build.log）和完整源码门禁 6746（/tmp/evengine-bake-placement-check.log）进行中，待收取终态。
- 适用单一状态 owner、Result 失败保留、显式坐标与生命周期、热路径登记、跨模块导出；无新增 ECS/Link/provider/callback，无架构例外。刚体表现消费、来源身份准入及后续通道仍未完成。

2026-10-21 实例变换宿主回归：

- 宿主构建 27524 exit 0；重新运行全部 physics.bake.* 14/14 通过，含真实 2D/3D 捕获、文件往返、播放器变换与原碎块存储回归。日志 /tmp/evengine-bake-placement-host-tests.log。这不是渲染或跨平台验收。
- 完整源质量句柄 6746 再次轮询确认仍在运行，日志 /tmp/evengine-bake-placement-check.log；尚未声明通过，不重启同一任务。
- 表现接入源码审查：Renderable3D 的直接旋转 setter 使用 yaw/pitch/roll，但 Transform3D 已有值拥有的 linkedWorld 矩阵通道，StaticPrefabRenderInstance 正在消费该通道。后续刚体表现适配应复用真实矩阵消费路径，并定义 ECS generation handle 的 stale 与销毁顺序合同，不能只把姿态采样当作完整渲染接入。

2026-10-21 刚体表现消费接入：

- 上轮完整门禁 6746 exit 0，461 脚本测试通过（68.196 秒）。
- 新增 L4 physics_bake_graphics 卫星，只依赖 graphics 与 physics_bake。submitRigidPlayback 通过真实 Renderable3D linkedWorld/modelFromTransform 路径提交米制姿态，保留网格缩放；拒绝像素轨道、未知身份与非有限矩阵，失败不改目标。
- 接口同步借用对象，不跨帧存指针、不创建 Link/实体、不推进时间、不调用 solver；调用方在 ECS/render owner thread 提供有效对象并排除其他 transform writer。实际单对象直接调用无额外 builder，热路径和 module-interface 已登记，无架构例外。
- 新增组合用例检查插值旋转/平移/缩放真实矩阵消费、错误保留、播放器先销毁及目标先销毁、直接 TRS 接管。尚待宿主运行，不能当作 GPU 画面验收。
- 本轮宿主日志 /tmp/evengine-bake-presentation-host.log，门禁日志 /tmp/evengine-bake-presentation-check.log；句柄以工具结果为准，继续先收取终态。2D 表现、刚体配方缓存和后续布料/绳索/粒子仍未完成。

2026-10-21 平面表现与编译修正：

- 表现宿主构建 21208 exit 2：transform() 返回 ComponentRef，不可用 auto* 推导。已改为值持有组件引用，借用仍限制在调用内；未宣称此前测试通过。
- 增加 Renderable2D 重载，只接受 PlanarPixels，以 2*atan2(qz,qw) 将旋转转为渲染器的角度制，不翻转 XY，不改 sprite/尺寸/缩放。新增实际 Renderable2D 组合用例覆盖插值、角度转换、空间拒绝、失败保留及销毁顺序。
- module manifest 的表现卫星加入 2d group，仍不依赖 solver。文档、测试分类与模块证据同步更新。
- 修正后宿主构建日志 /tmp/evengine-bake-presentation-host2.log，原源质量 93684 仍在运行；先收取终态再决定后续验证，不重启活任务。两种表现适配均仍待宿主实际运行与图像验收。

2026-10-21 表现构建等待证据：

- 再次轮询确认宿主 43311 与门禁 93684 均仍在运行；新 EVPhysicsBakeGraphics 已编译成功，宿主继续编译图形 Vulkan 文件。未重启活任务，未将中间模块成功当作最终链接或测试成功。
- git diff --check 通过。后续收取上述两个句柄终态，再运行 physics.bake.* 中新增 2D/3D 表现组合用例。
- 刚体配方接入前审查：现有 CollapseBakeRecipe 的 EVECREC1 固定 metres-rh-y-up，且实现依赖 destruction；不能直接由 L3 bake 引用。通用来源元数据需下移纯值机制并保持已有碎块编码兼容，同时为 PlanarPixels 明确坐标合同，之后才允许刚体自动缓存准入。

2026-10-21 捕获到表现组合与领域构建修正：

- 门禁 93684 exit 0，461 测试通过（71.530 秒）。真实 capture 用例进一步连接源世界销毁→保存→重载→播放器→Renderable2D/3D；不是仅用合成轨道测试表现。
- 宿主 43311 exit 2：测试错误调用实体 destroy()；实际 ECS API 是 ecs::DestroyEntity，已修正两份新组合测试。生产模块已成功编译/链接，但不能将此算作测试通过。
- CMake 生成的领域清单显示 rigid_presentation.cpp 归 graphics，rigid_capture.cpp 归 physics。因此随后构建两个真实领域目标 unit_test_physics 与 unit_test_graphics（已有配置，不重复 configure），日志 /tmp/evengine-bake-presentation-domains.log。
- 最新源质量 13552（/tmp/evengine-bake-presentation-roundtrip-check.log）仍在运行。后续先收取运行终态，再实际执行两个领域发现的 physics.bake.*。

2026-10-21 真实捕获到表现运行证据：

- 构建 89848 的 physics 目标已完成。实际执行 capture2D/capture3D 两项测试 2/2 通过（/tmp/evengine-bake-presentation-capture-tests.log），覆盖真实 World 推进与捕获、world/body 销毁、文件保存重载、播放器按持久身份采样并提交真实 Renderable2D/3D。没有活物理世界参与播放提交。
- 此证据覆盖原生数据流与对象生命周期，不是 GPU 绘制或脚本示例验收。独立矩阵/失败组合用例归 graphics，89848 仍在编译该领域其余测试，不能提前声称那些用例已运行。
- 源质量 13552 仍在运行；继续收取相同句柄，不重启。更新使用文档以区分已实现原生表现与尚未完成脚本/GPU/配方准入。
- 随后构建 89848 exit 0，physics 与 graphics 两个领域均完成。重跑全部 physics.bake.* 16/16 通过（/tmp/evengine-bake-presentation-all-tests.log），这次包含两个表现组合用例、真实捕获链路及已有存储/播放回归。仍不代表 GPU 画面验收。

2026-10-21 共用来源配方机制：

- 将原配方值实现迁移到 L3 physics_bake/BakeRecipe，使用公共 bakedAssetContentKey 和本模块诊断，不再反向依赖 destruction。原 CollapseBakeRecipe 为文档明确的兼容别名，原脚本名称和 EVECREC1 编码保持不变。
- 当前规范仍固定米制右手 Y-up；没有把像素刚体或未携配方的 EVERIGD1 宣称为自动缓存可准入。接下来需要显式坐标合同和刚体资产嵌入校验。
- 独立生产 profile 重建 5872 exit 0，13/13 通过，日志 /tmp/evengine-bake-recipe-common-tests.log；已有配方/文件/发布/播放回归使用迁移后实现。宿主重建 66405（/tmp/evengine-bake-recipe-common-host.log）进行中，旧完整门禁 13552 仍在运行。
- 已 touch 直接使用相关头的消费者，避免旧对象布局/符号证据。导出组改为 BACKENDS，Result/单一数据 owner/持久格式兼容/模块边界规则适用，无架构例外。

2026-10-21 配方坐标合同：

- BakeRecipe 增加 checked BakeCoordinates：原米制右手 Y-up 继续编码 EVECREC1；PlanarPixels 明确编码 EVEBREC1 + planar-pixels-xy，保留生产者 XY 不隐式翻转。相同参数/来源在不同空间产生不同规范字节和键，未知枚举失败保留。
- 碎块 beginBake 与文件恢复均显式拒绝非米制配方，避免公共别名扩展后混入像素数据；原米制编码保持兼容。新增回归检查空间往返、键不同、无效枚举保留、碎块拒绝及切回米制字节一致。
- 第一次测试编译因缺失枚举命名空间失败，已限定 eve::physics，未将失败当作通过。迁移宿主 66405 在该测试终止 exit 2；随后 touch 相关消费者并重建当前配置，日志 /tmp/evengine-bake-recipe-space-host.log。源门禁 59596 仍在运行，后续先检查终态。
- 此步尚未把配方嵌入刚体文件；EVERIGD1 显式加载限制仍然有效。适用版本化规范编码、单一坐标 owner、失败事务与模块边界，无例外。

2026-10-21 刚体内嵌来源准入：

- 上轮宿主 94712 exit 2 是 CMake 自动重配置时漏传本地 VULKAN_SDK；源门禁 59596 exit 2 是新引用后模块证据过期。已更新证据，后续宿主命令恢复 SDK 环境，没有降低门禁。
- RigidPoseTrack 增加带配方 prepare 重载，检查完整配方、坐标与固定采样网格，冻结 shared immutable 副本；无配方路径仍用于明确选择的旧资产。持久 v2 在原头后内嵌有界规范配方，并纳入整体校验。load 恢复再次检查语义，loadForRecipe 精确比较完整规范字节，旧 v1/不匹配失败。
- 独立 profile 构建与 13/13 通过（/tmp/evengine-bake-rigid-provenance-tests.log）；双空间覆盖 v2 往返、v1 拒绝自动准入、后端变化、空间/时间拒绝、损坏与合法校验下非法配方长度。此证据不替代宿主绑定/链接或图像验收。
- 原子恢复、版本/未知字段、Result、不可变单一 owner 与成本合同适用；无例外。宿主与最终门禁需在最新状态重新运行。

2026-10-21 来源语义恢复测试：

- 新增合法整体 checksum 下的两种 v2 非法输入：资产坐标与内嵌配方不一致、帧时间偏离配方固定网格。两种都必须以 InvalidArgument 语义拒绝，不能依赖 HashMismatch 偶然挡住。
- 独立构建 81488 exit 0，13/13 通过（/tmp/evengine-bake-provenance-semantic-tests.log）。同步整理模块文档，移除早期“尚未内嵌来源”的过时描述，保留脚本、GPU 和裁剪验证缺口。
- 当前宿主 78706（/tmp/evengine-bake-rigid-provenance-host.log）与源门禁 69750（/tmp/evengine-bake-rigid-provenance-check.log）经轮询仍运行；先收取终态后执行宿主领域回归，勿重复构建。

2026-10-21 来源宿主回归与变形通道审查：

- 宿主 78706 exit 0；最新 physics.bake.* 16/16 通过（/tmp/evengine-bake-provenance-host-tests.log），包含 v2 来源/语义测试、源世界销毁后的表现提交和既有碎块回归。源门禁 69750 仍在运行。
- 变形生产 API 源码审查：Cloth3D 提供网格行列、粒子位置与当前三角形数，但没有公开三角形索引快照；撕裂会改变 triangles_。Rope3D 提供粒子位置、元素 active 状态及 topologyRevision，cut/repair/resize/自动撕裂会更新 revision。
- 固定拓扑轨道不能只比较点数：布料撕裂可能点数不变，绳索断开也不改变粒子数。后续应捕获明确连通性并校验拓扑身份；发生变化时拒绝同一固定拓扑片段，不能按原网格补回已撕裂面或跨断点插值。此为下一阶段设计约束，不是变形通道完成证据。

2026-10-21 变形拓扑值层：

- 上轮源门禁 69750 exit 0，461 脚本测试通过（69.216 秒）。
- 新增 DeformTopology：有界三角形/线段索引、顶点数与可选有限 UV，规范内容身份覆盖连通性与绕序，UV signed zero 规范化。拥有不可变共享数据，借用 spans 明确寿命，无 solver/graphics/provider。
- 独立构建 96083 exit 0，14/14 通过；覆盖同点数撕裂/断绳、绕序、源输入修改、共享寿命、索引与 UV 错误。模块证据器要求合并两份共 139 行的小单元文件，已收敛为 physics_bake_values.cpp 并保持用例，未豁免规则。
- 整理后重跑句柄 54518，日志 /tmp/evengine-bake-topology-tests2.log。后续宿主与源门禁需要覆盖新拓扑，不能将这一步视为布料/绳索通道完成。适用不可变 owner、成本、借用寿命、Result 规则，无例外。

2026-10-21 固定拓扑变形帧：

- 上轮宿主 76472 exit 0；源门禁 11505 exit 2，具体日志 /tmp/evengine-bake-topology-check.log，不能声明通过。
- 新增 DeformTrack，冻结实际拓扑/位置帧和完整配方，显式固定采样网格与坐标检查；连通性和 UV 使用精确比较，不仅依赖非密码学摘要。相同拓扑的帧共享一份规范拓扑，限制总顶点样本 2000000。
- 采样接受精确长度 caller buffer，端点夹取、双精度中间线性插值、无分配/solver/IO；非法时间/输出大小在写入前拒绝。热路径已登记。
- 独立构建 75779 exit 0，15/15 通过（/tmp/evengine-bake-deform-track-tests.log）；覆盖拓扑/UV/类型/样本数漂移、坐标/时间拒绝、源销毁、端点与失败输出保留。模块证据更新成功。
- 仅完成变形帧值层，尚未真实 cloth/rope 采集、持久化或表现。适用不可变 owner、Result、借用寿命、固定拓扑与成本规则，无架构例外。

2026-10-21 真实布料与绳索采集接入：

- 变形帧宿主 42781 exit 0；源门禁 17309 仍运行，尚未声明其通过。
- Rope3D::captureSegmentIndices 导出 active 元素实际端点，Cloth3D::captureTriangleIndices 导出 surviving triangles；只返回 owning 值，不保留源引用。API 标注 owner thread、无回调与线性捕获成本。
- 新 L6 physics_bake_deform_capture 卫星通过两个 captureDeformFrame 重载采集米制位置与真实连通性；仅同步借用，不推进世界，不重建初始网格，全部连通性消失明确拒绝。纯值播放模块无反向依赖。module-interface 已记录。
- 新增实际 rope/cloth 推进、cut/tear、同点数拓扑拒绝与源销毁后采样组合测试，尚待运行，不能按源码宣称通过。
- 当前宿主日志 /tmp/evengine-bake-deform-capture-host.log，后续先收取终态。适用值所有权、Result、模块分层、成本、明确坐标与拓扑合同，无架构例外；持久化和真实变形表现仍待接入。

2026-10-21 变形采集验证推进：

- 上轮完整门禁 17309 exit 0，461 脚本测试通过（68.775 秒）。对新增 Cloth3D/Rope3D API 的改动行执行 git clang-format，未产生额外格式改动。
- 采集顺序调整为先验证时间/粒子预算，再分配连通性快照，避免无效调用先复制全部拓扑；静态重载选择源实现，没有未知回调或跨帧指针。
- 当前宿主 8996（/tmp/evengine-bake-deform-capture-host.log）仍编译中；最新完整门禁 54702（/tmp/evengine-bake-deform-capture-check.log）运行中。源在构建期间有修改，终态后核对 DeformBakeCapture 对象新鲜度并增量重编译必要目标，再运行组合测试。

2026-10-21 采集对象新鲜度与拓扑恢复边界：

- 宿主 8996 exit 0，但 mtime 核对显示 DeformBakeCapture.cpp 晚于其对象文件；因此不以旧对象运行验收。SDK 环境完整的增量重建 75350（/tmp/evengine-bake-deform-capture-fresh.log）已启动。
- 绳索组合测试增加全部 cut 后空连通性拒绝，以及 repair 恢复相同连通性后固定拓扑片段可准入。该策略明确按采样时实际连通性判断，不将拓扑 revision 当作持久身份。
- 源门禁 54702 仍运行。后续先收取两句柄终态，再运行真实 capture 测试；当前不宣称组合验收通过。

2026-10-21 变形采集验收与资产持久化：

- 新鲜重编译 75350 exit 0；真实 ropeCapture/clothCapture 2/2 通过（/tmp/evengine-bake-deform-capture-tests.log），涵盖真实推进、撕裂/断开、全部断开拒绝、repair 恢复、源销毁后采样。源门禁 54702 exit 0，461 脚本测试通过（71.577 秒）。
- 新增 DeformTrackFile EVEDEFM1 v1，有界读取、整体校验、精确长度、拓扑/UV/帧/配方再验证，公共原子不覆盖发布，loadForRecipe 精确规范身份比较。当前首版无历史格式迁移；未知字段/版本拒绝，失败不发布片段。
- 独立构建 94802 exit 0，16/16 通过（/tmp/evengine-bake-deform-file-tests.log），新增三角形/线段文件往返、配方不匹配、Conflict 保留、损坏与合法校验下异常头、截断、已加载片段独立性。资产测试统一重命名为 track_storage.cpp，未拆出重复临时文件机制。
- 适用持久 schema/version、事务恢复、成本与 Result 规则，无架构例外；新编解码宿主与最新源质量尚需运行，实际捕获→文件→表现的后半程仍待接入。

2026-10-21 变形独立播放器：

- 变形文件宿主 99349 exit 0；完整门禁 59470 exit 2，详见 /tmp/evengine-bake-deform-file-check.log，未声称通过。
- 新增 DeformPlayback，显式 prepare 一次分配私有顶点缓冲，move-only 实例共享只读 DeformTrack；组合公共 BakePlaybackClock/BakeRigidTransform，支持暂停/速率/循环/跳转/实例变换。平面轨道拒绝离面变换。
- 采样先在私有 scratch 验证并变换完整帧，成功才复制至 caller output；失败不污染任何输出顶点。不新增每帧 allocation、IO、solver、字符串查找或回调，热路径已登记。
- 独立构建 60527 exit 0，17/17 通过（/tmp/evengine-bake-deform-player-tests.log），含独立实例共享身份、倒放循环、旋转/平移、暂停、晚顶点溢出整帧保留、失败后恢复与平面拒绝。模块测试证据已更新。
- 适用成本分层、单一状态 owner、Result/事务输出、线程和生命周期规则，无架构例外；变形 renderer/GPU 及粒子通道仍未实现。

2026-10-21 真实变形来源持久重放组合：

- 两个真实 cloth/rope 采集用例已从直接 track.sample 扩展为：源销毁→save→loadForRecipe→DeformPlayback::prepare/seek/sample。新断言尚待当前宿主完成后运行，不能沿用上一轮 2/2 声称本轮通过。
- 渲染接口源码审查：Graphics::newMeshFromArrays/updateMeshVertices/releaseMesh 提供真实动态网格路径；Mesh 由 Graphics 拥有，成功 releaseMesh 后 CPU facade 转移调用方删除。后续表现适配必须明确该寿命，避免跨帧保存未受控裸指针。三角形和线段需分别准备表面/线段几何，不能用初始网格掩盖连通性变化。
- 当前宿主 1607（/tmp/evengine-bake-deform-player-host.log）和源门禁 28246（/tmp/evengine-bake-deform-player-check.log）仍运行，继续等待同一任务终态。
- 随后宿主 1607 exit 0，mtime 确认 deform_capture 测试对象晚于最新源码。实际 physics.bake.* 22/22 通过（/tmp/evengine-bake-deform-player-host-tests.log），包含本轮真实源销毁后的保存/配方重载/独立播放器链路；这是原生数据流证据，尚非 GPU 绘制验收。

2026-10-21 变形图形消费实现：

- 新增 DeformMeshPresentation，准备期预算/分配 CPU buffers 与独立 DeformPlayback，双面三角网格或带端盖八边形管段；采样后重算法线，double 中间计算并在写 float 前检查可表示性。
- createMesh/upload 使用实际 Graphics 动态网格接口，所有图形指针仅调用期借用；不跨帧保存 Mesh/Graphics，不创建 ECS/System/Link。调用方管理 GPU 生命周期与失败后的停止绘制，避免隐含第二 owner。原生数据面/成本/线程/双向销毁合同已文档化，热路径登记。
- 新增真实 window/render 测试，用引擎 readback 检查两种网格出现→更新移走→恢复，以及 CPU 适配器先销毁后网格仍可绘制。尚待运行，不能以源码当作 GPU 验收。
- 测试分类初次用了不存在的 gpu-render，证据脚本拒绝；已改为仓库实际 render-window 并重生成证据，没有添加新分类或绕过检查。
- 适用 Result、成本、借用寿命、公共跨组导出与机制直接调用规则；无架构例外。后续先构建 graphics 领域，再在明确的软件 Vulkan 环境跑测试。

2026-10-21 变形图形构建等待与粒子来源审查：

- 句柄 61159 和 98851 轮询确认仍运行；宿主已编译 DeformMeshPresentation.cpp，无该文件编译错误，尚未最终链接。完整门禁处于绑定/脚本检查阶段；不重启活任务、不宣称渲染测试已通过。
- 粒子来源源码审查：CPU Particle 只有状态/寿命等字段，没有稳定出生身份；Sim 的 particles/alive 会按活粒子槽位消费。GpuSim 的 nextBirthSerial 是 GPU spawn 计数，residentActive 后位置留在 GPU，不可把 CPU particles 当作实时 GPU 数据。
- M7 录制必须先明确 CPU 出生身份与重置/槽位复用语义，并对 GPU 驻留来源提供明确可观测支持边界；不能按容器下标在不同生命之间插值。最终颜色由 ParticleSystem::sampleColor 从配置曲线/梯度求值，采集应复用真实表现计算而非遗漏颜色。
- 上述是生产接口审查约束，不是粒子通道实现或验收。当前仍优先完成变形网格渲染验证。

2026-10-21 变形渲染测试目标核对：

- 图形宿主构建 61159 exit 0。生成的 build.make 明确新 deform_presentation.cpp 归 unit_test_physics（不同于已有 rigid_presentation.cpp）；因此补建实际物理领域目标，日志 /tmp/evengine-bake-deform-mesh-physics.log。不会把图形目标成功算作新用例已编译。
- 完整源码门禁 98851 仍在运行，已进入脚本测试。后续先收取物理构建终态，再使用 lvp_icd.json、Xvfb 和引擎 getPixel 运行唯一渲染用例；这是软件 Vulkan 功能验证，不做独显性能声明。
- 物理目标 54082 exit 0；真实渲染执行 45614 exit 0，用例 1/1 通过（2.57 秒，/tmp/evengine-bake-deform-mesh-render-tests.log）。窗口由 Xvfb 提供，ICD 显式指定 Lavapipe，像素来自引擎 getPixel。两种拓扑都验证出现→网格更新移出→恢复，以及适配器销毁后 GPU 网格仍可绘制；未使用 Xvfb 截图，不宣称独显性能。

2026-10-21 粒子出生身份推进：

- CPU Particle 增加 uint64 birthSerial；Sim 是递增序列唯一 owner。编号随死亡压缩移动，start/stop/reset 不重置编号；新的 Sim 是新的身份作用域，后续 recorder 必须结合 emitter/session 身份。
- 编号 0 表示未出生或计数耗尽；最后一个合法编号为 UINT64_MAX，之后拒绝出生并统计 droppedSpawns，不复用编号。蒙皮采样之前也检查耗尽，避免拒绝出生仍消耗 RNG。
- 新增 particle_identity 集成测试，覆盖压缩、停止重启、reset、最大编号与耗尽。已登记 module-composition 和模块覆盖索引；尚未执行，不计作通过。
- 宿主重编译正在执行 session 79824，日志 /tmp/evengine-bake-particle-identity-build.log；因 Particle 布局变化，已 touch 所有直接包含 ParticleEmitter.h 的实现/测试文件。当前目标 unit_test_physics；配置确认新测试实际归属 unit_test_particles，必须等当前构建终态后再构建该目标，不并发使用同一构建目录。
- 这只是 CPU 粒子录制的身份基础。GPU resident 粒子、完整轨迹格式、生灭插值、持久化与表现消费仍未完成。不存在本轮完整 make check 通过声明。

粒子身份验证更新：

- session 79824（physics）及 56169（particles）构建均 exit 0。新增 physics.bake.particleBirthIdentitySurvivesCompactionAndRestart 实际通过。
- 扩展回归首次 4/10 通过：5 个旧粒子用例在 unit_test_graphics 内段错误，该目标未重建，尚不能排除旧 Particle/Sim 布局；1 个窗口用例因未配置 Xvfb 失败。不得将该组记作通过。
- 正在重建 unit_test_graphics，session 26431，日志 /tmp/evengine-bake-particle-graphics-build.log；结束后复测 no-render 粒子 playback/pool 用例，窗口用例需 Xvfb/软件 Vulkan。
- 完整 make check session 11384 仍在执行，日志 /tmp/evengine-bake-particle-identity-check.log。请继续检查原 handle，不能因等待超时重复启动。

粒子回归终态更新：

- unit_test_graphics 重建 session 26431 exit 0；同一组 no-render 回归 9/9 通过（0.55 秒），包括先前段错误的 5 个用例，符合旧目标结构布局未重编译的诊断。
- 窗口用例 particles.playback.renderDistanceTrailAndLoopingBurst 在 Xvfb + Lavapipe 下 1/1 通过，1.68 秒，session 45753 exit 0。这是软件 Vulkan 功能验证，不是独显性能证据。
- 下一步粒子录制必须复用实时外观求值：ParticleSystem.cpp 的 sampleColor/sampleScale/flipbookUV/appendParticleItem 目前是局部函数，包含渐变、尺寸曲线、速度拉伸、轴向/旋转曲线与 UV。不能只复制 x/y/size/rot 而忽略这些配置。资产只能保存 owning 数值及资源身份，不能保存 Draw 的纹理、shader、camera 指针。

- 本轮完整 make check session 11384 最终 exit 0：461 个脚本测试通过，71.718 秒；source-quality gate passed (base=origin/dev)。当前没有仍在执行的构建或门禁 handle。出生身份基础完成验证，粒子完整轨迹及其余原目标验收仍继续。

粒子外观单一实现推进：

- 新增 ParticleAppearance owning 数值和 evaluateParticleAppearance；从实时 ParticleSystem 提取原有颜色/大小曲线、旋转模式和 atlas UV 计算，实时 appendParticleItem 已改为消费该值。保留 stretched 模式原有锚点，避免重构改变现有效果。
- 不创建 ECS System/Link，不保留 Config、Particle、纹理、相机或 GPU 引用。函数只读取调用期间稳定的输入，不执行回调或模拟；返回数值的有限性仍由未来资产 admission 检查。导出 DOMAINS，并登记热路径。
- 新增 physics.bake.particleAppearanceMatchesLiveSpriteModes，覆盖尺寸与颜色、axis、stretched、负 flipbook 索引及 owning 值。尚未运行。
- 构建 unit_test_particles session 90744，日志 /tmp/evengine-bake-particle-appearance-build.log；旧出生身份版本的 gate 已通过，不代替本轮改动后的 gate。

- 本轮架构改动行检查通过。完整 make check 已启动 session 79834，日志 /tmp/evengine-bake-particle-appearance-check.log；构建 90744 同时仍在执行。后续先检查两个原 handle，再运行粒子 appearance/identity 用例和实时窗口回归，不重复启动构建。

外观数值可靠性更新：

- 检查提取代码时发现原 flipbook 的 int 网格乘法和 float→int 帧索引在极值下有溢出/未定义行为。新实现采用 int64 网格乘积与 double fmod 后有界转换；负帧保持原循环语义。
- evaluateParticleAppearance 改为 checked Result<ParticleAppearance>，在返回前拒绝任一非有限外观字段；实时渲染显式检查失败并跳过该项。录制入口将传播诊断。该更新取代前文“输出有限性仅由未来 admission 检查”的中间设计。
- 测试增加最大 int 网格、最大有限 float 帧号、无限帧号及 NaN 位置。unit_test_particles 初次构建 90744 已 exit 0，但不是最新 Result 版本的验证；当前重编译 session 96160，日志 /tmp/evengine-bake-particle-appearance-result-build.log。
- 首次外观版本 make check session 79834 仍需检查终态，完成后必须为最新 Result 修改重新运行 gate。还未运行最新外观用例，不提前声明通过。

共享外观运行验证：

- Result 版本构建 96160 在测试断言编译失败：zeroerr 的 REQUIRE(Result) 尝试复制 move-only Result。已改为显式 bool 检查；重新构建 session 33910 exit 0。
- 最新 physics.bake.particle* 两个集成用例 2/2 通过（0.20 秒），包括身份与外观极值。
- 实时 particles.playback.renderDistanceTrailAndLoopingBurst 在同一最新共享库上 1/1 通过（Xvfb/Lavapipe，1.68 秒），session 88942 exit 0。
- 初次外观 gate 79834 exit 0；最新 Result 版本完整 gate session 37081 仍在执行，日志 /tmp/evengine-bake-particle-appearance-result-check.log。除该门禁外没有运行中的构建。断言 bool 修复发生于本次 gate 开始后；最终交付仍需最后一次稳定源码门禁。
- 后续继续实现粒子 owning 轨迹、稳定身份与生灭区间、采样/持久化和资源表现消费，不能把已验证的外观基础替代完整粒子通道。

粒子轨迹核心实现：

- 新增纯 physics_bake ParticleTrack：发射器会话 PersistentId + uint64 birthSerial，最多 65536 出生、2 百万关键帧；同一身份重复、nil 身份、非有限数、非法时序或超出 clip duration 拒绝。准备完成后冻结并共享只读数值。
- 每条轨迹首尾 key 明示包含出生/排除死亡。事件可处于采样 tick 之间，生产者必须提供真实边界，不将稀疏快照推断当作精确事件。空轨迹和静默尾段合法。
- 显式时间采样；姿态/颜色线性插值，unwrapped 角度保留转数，atlas UV 阶梯保持。caller 预分配 capacity 大小输出，返回可见数量，失败不修改输出，无 solver/图形/脚本引用。
- 新增粒子身份边界/复制隔离/错误输入单测，纳入独立无 solver profile。profile 构建 session 33237 exit 0，GCC -Wall -Wextra -Werror；本轮架构和空白检查通过。CTest 正在执行，以下续记结果。
- 上一外观 Result gate session 37081 已 exit 0，但不覆盖新 ParticleTrack。新轨迹持久化、生产录制和表现消费仍未实现，不将其视为粒子通道完成。

- 新轨迹首轮用例因夹具缺少 source 摘要被 recipe.encode 正确拒绝，补齐源摘要后重新构建成功；独立 profile 按 -LE bundle 跑分进程用例 18/18 通过（0.05 秒），session 94111 exit 0。
- 最新完整 make check session 13050 执行中，日志 /tmp/evengine-bake-particle-track-check.log。没有宿主构建正在运行；新轨迹尚无宿主链接/运行证据，独立 profile 证据仅证明无 solver 纯数值路径。

粒子持久化推进：

- ParticleTrack 新增 save/load/loadForRecipe，EVEPART1 v1：24 字节固定头、完整配方、每轨迹 28 字节身份与数量、每 key 56 字节时间与外观、32 字节校验。总文件上限由 65536 出生/2 百万 key/64KiB 配方推导。
- 加载先验证文件界限与校验，预扫描全部记录长度/累计 key 预算及精确尾部，再分配轨迹并调用同一 prepare 语义验证；失败无部分发布。共用 publishBakedAsset，不覆盖已有资产。
- 新增往返逐字节一致、UINT64_MAX 身份、源 clip 销毁后重载、配方不匹配、损坏、未知版本、尾部、有效校验下的非法生灭时间测试。
- 独立无 solver profile 重新配置/编译 session 76262 exit 0，19/19 分进程测试通过；日志 /tmp/evengine-particle-file-build.log 和 /tmp/evengine-particle-file-tests.log。架构改动行检查通过。
- 上一轨迹核心 make check session 13050 仍在运行，日志 /tmp/evengine-bake-particle-track-check.log；该门禁启动早于本轮持久化，不能替代本轮最终门禁。后续检查原 handle 后启动最新 gate。
- 尚待真实粒子事件录制、播放时钟/实例变换和渲染消费者；不能以文件往返替代完整粒子闭环。

粒子播放器推进：

- 新增 BakedParticlePlayback，复用 BakePlaybackClock/BakeRigidTransform；准备一次 scratch，独立实例共享只读轨迹，move-only 防止隐式大缓冲复制。
- 明确平面 placement：围绕烘焙原点变换 sprite 中心并累加未折返 degree 旋转，double 中间计算，最后检查 float 范围。先在私有 scratch 完成全部可见粒子变换再提交，失败不修改调用者输出。
- 扩展粒子用例覆盖多实例独立时间、暂停、反向循环、中心旋转、出平面拒绝、溢出时输出保留。独立 profile configure/build session 7749 exit 0，19/19 分进程用例通过；日志 /tmp/evengine-particle-player-build.log 和 /tmp/evengine-particle-player-tests.log。架构检查通过。
- 之前轨迹核心 gate 13050 已 exit 0，不覆盖后续文件/player 修改。当前没有构建运行；最新完整门禁见后续 handle。真实事件采集、渲染资源消费、脚本和完整系统验收仍待推进。

- 最新完整 make check session 83401 执行中，日志 /tmp/evengine-bake-particle-player-check.log。下一轮检查原 handle，不重复启动。

粒子表现适配器推进：

- 新增 ParticleBakePresentation（physics_bake_graphics）：Pimpl 自有 BakedParticlePlayback、采样和 DrawItem2D 缓冲。draw 借用材质模板与 Graphics，填充 recorded sprite 后调用现有 RenderSystem::drawItems；scope 清理确保不跨调用保留借用资源引用。
- 明确图形资源需存活到 frame completion，adapter 不拥有其生命周期。没有 ECS System/Link 或源 emitter 引用；采样失败不提交，成功仅代表提交而非 GPU 完成。
- 新增真实窗口测试 physics.bake.particleSpritesRenderIndependentBirthIntervals：两个实例出生/死亡、平移、销毁一个实例后另一个继续播放；使用引擎 getPixel 而非 Xvfb 截图。
- 架构/空白检查通过，模块覆盖和 test_levels 已更新。宿主构建 session 49286 正在运行，日志 /tmp/evengine-particle-presentation-build.log，当前目标 unit_test_physics。待配置完成检查新 particle_presentation.cpp 实际所属域再构建/运行，不能假定属于 physics。
- 上一播放器完整 gate session 83401 仍在运行；新渲染适配器还没有编译/运行通过证据。后续先检查原 handles；没有启动第二个宿主构建。

粒子表现宿主验证更新：

- 初次宿主构建 session 49286 exit 0，新窗口用例实际归属 unit_test_physics。
- 首轮窗口 session 3767 exit 8：空批次像素为默认清屏红分量 0.101961，而夹具期望黑色。adapter 使用叠加绘制模式并不拥有全帧清屏，已将测试改为每帧 Graphics::clear 黑色，同时把有粒子阈值提高为 >0.5，避免背景也被误认为粒子。
- 修正后的宿主重编译 session 2038 执行中，日志 /tmp/evengine-particle-presentation-rebuild.log；源码在编译开始后补入 clear，必须确认对应对象比源码新，再运行窗口用例。不能把首轮失败记成通过。
- 播放器版本 gate 83401 exit 0。渲染适配器完整 gate session 78984 运行中，日志 /tmp/evengine-bake-particle-presentation-check.log；测试清屏调整晚于该 gate 启动，最终稳定源码门禁仍需复验。
- 真实粒子事件记录还需覆盖自然死亡、collision kill、寿命损失、reset 和出生后继承速度的最终外观；仅对比稀疏 alive 快照无法宣称完整事件录制。

粒子画面回读验证完成：

- 重编译 2038 因清屏调用的 braced-list 无法隐式转 optional<Color> 失败；夹具改为显式 eve::Color 后，session 9669 exit 0。
- 最新窗口复测 session 35095 exit 0，physics.bake.particleSpritesRenderIndependentBirthIntervals 1/1 通过（1.52 秒）；日志 /tmp/evengine-particle-presentation-retests.log。显式黑色清屏，红色粒子阈值 >0.5，死亡/移出中心阈值 <0.01；证据为引擎 getPixel，软件 Vulkan。
- 当前没有运行中的宿主构建/窗口测试；源质量门禁 session 78984 仍运行，日志 /tmp/evengine-bake-particle-presentation-check.log。该门禁启动于最终夹具修正前；最终稳定源码 gate 待结束后重跑。
- 粒子核心、文件、时钟、实例变换与合成轨迹的真实 renderer 已有验证；真实模拟事件 recorder 尚未实现，不能宣称完整粒子生产闭环。

粒子事件记录基础：

- 新增 ParticleEventJournal：自有 time/birthSerial/event kind/ParticleAppearance 数值；prepare 一次预留最多 2 百万事件，append 无扩容、无回调，要求时间非递减、有效身份及有限外观。
- Exhausted/InvalidInput 为 sticky 状态，后续 append 失败；disable 不清除失败，prepare 只有完整成功才替换旧事件。避免部分记录被误当成功资产。
- ParticleAppearance owning 值拆入独立同模块头，避免事件层依赖完整 emitter 配置。外观求值与实时渲染行为未改。
- 新增 journal 预算耗尽、无效准备保留旧数据、时间倒退、错误不可被停止清除、输入复制隔离测试，尚未运行。
- 宿主 unit_test_particles 构建 session 82325 执行中，日志 /tmp/evengine-particle-journal-build.log。上一渲染适配器 gate 78984 已 exit 0，但不覆盖本轮 journal。
- 当前仅事件缓冲基础，尚未将其接入 spawn/death/reset 或转换为 ParticleTrack。下一步记录必须在实际删除前求值、继承速度完成后更新出生外观，并显式处理跨 emitter 时钟；不能从帧末 alive 列表反推完整事件。

事件交付合同加固：

- 检查实际源码发现先前 disable 的替换未生效（格式化空格使文本替换未匹配），已修复为仅 Recording→Disabled，失败状态保留。新增 finish 仅对有效 Recording 转移 owning vector，错误/disabled 拒绝且保留数据；后续轨迹转换仍负责检查生灭完整性。
- 测试增加失败记录不能 finish、成功交付后旧数据不受重新 prepare 影响、空记录交付。
- 构建 82325 在 finish 链接失败（库对象在本轮新增方法前已编译，测试已使用新方法）。已等该构建终态后启动重新编译，日志 /tmp/evengine-particle-journal-finalbuild.log，原失败不能记作通过。
- 架构检查通过；完整 make check session 81819 执行中，日志 /tmp/evengine-bake-particle-journal-check.log。模拟内部事件接入尚未完成。
- 当前最新宿主构建 handle 为 8325；下一轮先检查 8325 和 81819，不重复启动。

真实粒子模拟事件接入：

- journal 最终宿主构建 8325 exit 0，身份/外观/journal 三项测试 3/3 通过（0.23 秒）。
- Sim 增加 disabled-by-default journal 与 recordingSeconds。prepareParticleRecording 要求空 CPU emitter 且无正在进行的录制，准备成功才清时间；finish 记录当前存活出生的结束事件并交付 owning 数据，不改变实时模拟存活状态。
- spawn 记录初始外观，帧末在速度继承完成后记录最终外观；自然死亡/碰撞 kill/寿命损失和 reset 在删除前记录。事件时间为实际执行的离散模拟步边界，绝不声称连续碰撞时刻。停止/循环不重置 recordingSeconds。
- 子发射器录制时间必须与来源录制时间一致，否则目标 journal sticky invalid；继承速度后再追加同时间 Snapshot。尚未提供多 emitter 统一录制调度器，不能声明任意 emitter graph 自动可录制。
- 新增实际 emitter 自然死亡/reset/销毁后事件生存测试，尚未运行。Sim 布局改变，已 touch 所有直接包含 ParticleEmitter.h 的 cpp，需重建消费者避免旧布局。宿主 unit_test_particles session 23772 正在执行，日志 /tmp/evengine-particle-recording-build.log。
- 架构/空白检查通过。旧 journal gate session 81819 仍运行，不覆盖新事件接入。GPU resident 录制切换、事件→轨迹转换、碰撞专测和原目标其他验收仍待推进。

真实事件与轨迹转换推进：

- 事件接入构建 23772 exit 0；出生身份/外观/journal/真实自然死亡与 reset 四项 4/4 通过（0.24 秒），源 emitter 销毁后 owning events 保留。
- GPU 选择策略补强：prepare 显式拒绝 gpuSimulation 配置，ParticleSystem 进入 resident 尝试前使 journal invalid。即使 GPU fallback，CPU-only 录制也不隐瞒 backend 选择变化；这些最新保护尚未重编译验证。
- 新增 L6 physics_bake_particle_capture（DEPS particles/physics_bake，2d/3d）；公开 buildParticleTrack，借用完整事件、会话身份、显式时长与配方，结果为 owning track。无 ECS/Link/callback，纯播放无反向依赖；module-interface 文档已添加。
- 转换验证事件顺序、身份、有限值、总事件/出生预算、终止死亡；同时间 snapshot 更新最终状态，零持续出生验证后省略。不同 double 时间压成同一 float 明确拒绝，避免静默丢事件。缺失死亡或死亡后复用身份拒绝。
- 新增真实 emitter 录制→销毁→轨迹采样测试，并覆盖缺失死亡、重复死亡及输入释放。尚未运行；当前宿主重编译 session 80204，日志 /tmp/evengine-particle-capture-build.log。等待配置后确认 particle_capture.cpp 所属目标，不重复启动同一目录构建。
- 架构检查通过。旧 journal gate 81819 exit 0，不覆盖本轮转换/模块与 GPU 保护。仍待最新门禁、碰撞/GPU失败测试、真实事件→文件→渲染完整组合和脚本入口。

真实录制文件到像素的组合测试升级：

- 原粒子窗口测试改为真实 CPU emitter（固定 seed、静止红色 sprite、1 秒寿命、4 个 0.25 秒步），完成自然死亡后 finish，销毁源 emitter。
- 完整 events 转换为 ParticleTrack，保存到独占测试临时目录；source track 离开作用域、events 清空后 loadForRecipe，再准备两个独立 renderer/player。沿用生灭、平移、独立时间、销毁一方后另一方继续的 getPixel 断言。
- 尚未运行这一升级版用例。配置已确认 particle_capture.cpp 与 particle_presentation.cpp 属 unit_test_physics；当前 80204 正在构建 unit_test_particles，待它终态后构建 physics（不要并发同目录构建）。日志 /tmp/evengine-particle-capture-build.log。
- 架构/空白检查通过；最新完整 gate session 17409 正在执行，日志 /tmp/evengine-particle-capture-check.log。
- 新测试仍只覆盖 CPU 单 emitter 的确定性示例；碰撞、跨 emitter 时间编排、GPU失败、脚本入口和完整多通道验收仍未完成。

真实粒子持久化重放组合通过：

- unit_test_particles 构建 80204 exit 0；随后 unit_test_physics 构建 19397 exit 0，已编译转换与真实窗口组合用例。
- 最新 physics.bake.particle* 非窗口 7/7 通过（0.31 秒），覆盖真实事件→源销毁→轨迹、文件往返/损坏、播放控制、身份/外观/journal/reset。
- 真实录制→源 emitter 销毁→转换保存→source track/事件释放→loadForRecipe→双实例渲染的窗口用例 1/1 通过（1.52 秒），session 40288 exit 0；日志 /tmp/evengine-particle-capture-render-tests.log。引擎 getPixel 回读，Xvfb 仅提供显示，设备为 Lavapipe，不是独显性能证据。
- 当前没有运行中的构建或窗口测试；完整 gate session 17409 仍运行，日志 /tmp/evengine-particle-capture-check.log，继续检查原 handle。
- 尚待碰撞/寿命损失、GPU切换与跨 emitter 失败/组合专测，以及通用脚本入口、性能与裁剪验收；这组通过证明 CPU 单 emitter 原生生产链路，不能替代全部系统目标。

碰撞记录与表现策略边界：

- 新增真实 bounds collision kill 与 bounce+100% lifetime loss 用例，检查删除前的碰撞后外观、显式 0.125 秒死亡事件和完整交付；同时检查 GPU 配置、ribbon 和录制中排序策略变化失败。
- 查明 sprite 轨迹尚不能表达 ribbon 连通性或相机相关/年龄排序，prepare 与每次 event hook 现在显式拒绝不支持策略，不静默改变重放表现。支持 billboard/axis/stretched + sortMode=none；其余能力仍是待完成项。
- 当前宿主 unit_test_particles 构建 session 25562，日志 /tmp/evengine-particle-collision-build.log，新增用例尚未运行。架构与空白检查通过。
- 之前 capture 全门禁 session 17409 仍需检查终态，本轮修改晚于其启动；不把之前的通过证据扩展到新限制/测试。
- capture 门禁 17409 本轮确认 exit 0；当前仅碰撞测试版本宿主构建 25562 仍运行，没有新的完整门禁进程。后续先完成该构建并运行测试，再跑最新 gate。

碰撞与失败路径验证结果：

- unit_test_particles 构建 25562 exit 0；最新粒子非窗口组合 8/8 通过（0.32 秒），日志 /tmp/evengine-particle-collision-tests.log。
- 新增用例实际覆盖 bounds collision kill、bounce 后寿命损失死亡、GPU 配置拒绝、ribbon 拒绝、录制中排序变化导致 journal 失败；碰撞后数值与 0.125 秒步边界事件已验证。
- 完整源质量门禁 session 62428 仍运行，日志 /tmp/evengine-particle-collision-check.log；当前没有构建/领域测试进程需要等待。
- 这些验证没有覆盖跨 emitter 的统一调度、GPU 轨迹读回、ribbon/相机相关排序重放；也未完成通用脚本入口和 M8 性能/裁剪/多平台验收。原完整目标保持进行中。

子发射器事件边界推进：

- fireSubEmitter 录制出生时临时使用来源 executed-step recordingSeconds，并用局部析构恢复目标自身时间，避免来源先更新/目标尚未更新导致无理由失效。没有推进目标模拟，也不读取墙钟。
- 来源未录制或目标时钟已领先于来源仍 sticky invalid；目标随后的事件必须满足 journal 非递减合同。速度继承完成后仍补写同时间 snapshot。
- 新增两种调度顺序测试：source-first / target-first，确认目标自己时钟不被子出生推进、出生时间 .25、继承 vx=7、结束时间 .5。尚未运行。
- 宿主 unit_test_particles 构建 session 41744 执行中，日志 /tmp/evengine-subevent-build.log；架构与空白检查通过。
- 碰撞版本完整 gate 62428 本轮仍需检查终态；它不覆盖本轮子发射器修改。尚未实现多 emitter 合并资产/资源模板关联或覆盖任意异步调度，不能将两种同步更新顺序测试扩展为所有 graph 支持。

子发射器顺序验证结果：

- 新增目标已领先 (.5) 而来源事件较早 (.25) 的拒绝测试，确认 InvalidInput、目标时钟仍为 .5 且 finish 不交付残缺记录。
- 宿主构建 41744 exit 0，最新粒子+子发射器非窗口组合 9/9 通过（0.34 秒），日志 /tmp/evengine-subevent-tests.log。包括两种正常更新顺序、速度继承、反向时间拒绝及既有文件/碰撞/生命周期回归。
- 碰撞版本完整门禁 62428 exit 0。子发射器最新完整门禁 session 78459 执行中，日志 /tmp/evengine-subevent-check.log；当前无构建或领域测试在运行。
- 尚未有多 emitter 合并资产与不同材质组合验收；脚本入口和完整系统其他阶段仍需推进。

多发射器轨迹组合：

- buildParticleTrack 新增借用 ParticleRecordingSource 数组入口，单发射器委托同一实现；先验证唯一/非 nil 会话与总事件预算，再转换，最终共用 ParticleTrack::prepare。
- 不同会话相同 birthSerial 合法；重复会话（包括空日志）拒绝。总 2 百万事件/65536 session/65536 可见出生，任何失败不发布部分资产。调用者提供覆盖全部源和 RNG 的同一 recipe。
- 转换测试增加两个会话同出生编号输出独立身份、重复会话拒绝；尚未运行。
- 宿主 unit_test_physics 构建 session 23025，日志 /tmp/evengine-particle-multi-build.log。上一子发射器完整 gate 78459 仍需检查终态；最新多源修改尚无完整门禁证据。
- 多材质渲染绑定/组合像素、通用脚本和完整裁剪性能仍未验收；不把数据组合等同于完整多发射器效果重放。

多材质消费推进：

- ParticleBakePresentation 增加 identity-sorted ParticleBakeMaterial span，模板按调用借用，不持久存储；可见源缺失绑定、重复/无序/非 nil 违规在任何批次提交前失败。统一 drawImpl 复用既有单模板路径，CPU buffers 清理保留失败后的可恢复性。
- 新窗口测试将两会话组合存盘重载，红/绿同位置 sprite 通过独立 material.layer 改变前后关系；缺失绑定时像素保持清屏、重复绑定拒绝、重新提供完整绑定恢复。尚未运行最新用例。
- 前一多源构建 23025 exit 0；子发射器 gate 78459 exit 0。它们不覆盖随后多材质修改。
- 最新宿主构建 /tmp/evengine-particle-material-build.log 与完整门禁 /tmp/evengine-particle-material-check.log 执行中；下条记录 handles。架构和空白检查通过。
- 当前宿主构建 handle 28205、完整门禁 handle 3686；下一轮继续原 handles，未完成前不启动同目录重叠构建。

多源多材质组合验证通过：

- 宿主 unit_test_physics 构建 28205 exit 0；扩展后的多会话转换用例 1/1 通过（0.18 秒），覆盖独立身份和重复会话拒绝。
- 多材质窗口组合 62928 exit 0，1/1 通过（1.53 秒），日志 /tmp/evengine-particle-material-tests.log。验证合并轨迹保存重载、红绿重叠 sprite 的 per-emitter layer 改变结果、缺失绑定整批不提交、重复绑定拒绝以及恢复后正常绘制。
- 证据为软件 Vulkan + 引擎 getPixel；没有 GPU 性能或跨平台结论。材质差异本次验证的是 layer，不声明已逐一验证纹理/shader/canvas 的所有组合。
- 当前没有运行中的构建/窗口测试；最新完整 gate session 3686 仍需检查终态，日志 /tmp/evengine-particle-material-check.log。
- 已核对现有 CollapseAnimationBindings/CollapseBakeRecipeBindings 的 checked Result 与 Squirrel owned-instance 模式；通用通道脚本入口仍待实现，不能以现有 destruction 绑定替代。

多材质源质量检查修正：

- 门禁 3686 已结束 exit 2：quality metadata 将 drawImpl 注释中的 fallback 计为新增回退债务（73 > 72）。代码实际是显式单模板绘制入口，不在缺失 per-emitter 绑定时回退；该路径仍返回 NotFound 且不提交。
- 将私有参数和借用注释统一命名为 uniformMaterial，准确表达单模板入口；没有修改债务基线、检查器或运行语义。quality metadata 恢复 72 并通过，git diff --check 通过。
- 现有无 solver 播放 profile 重跑 19/19 通过，日志 /tmp/evengine-bake-core-recheck.log；此 profile 不覆盖 graphics 适配器。
- 最新完整 make check session 50838 仍在运行，日志 /tmp/evengine-particle-material-check2.log；不得把局部检查当作完整门禁通过。
- 通用脚本接入需先解决类型注册所有权：CollapseBakeRecipe 是 BakeRecipe 的 C++ 别名，现有注册函数在 destruction 内；通用入口应避免对同一个原生类型建立相互覆盖的脚本注册，也不能让纯播放核心依赖 VM 或 destruction。

通用脚本卫星首次接入：

- 多材质修正完整门禁 50838 exit 0，461 个脚本测试通过；日志 /tmp/evengine-particle-material-check2.log。该结果早于本轮新脚本模块。
- 新增 physics_bake_script（L4，依赖纯 physics_bake），唯一 manifest 注册 PhysicsBake/physicsBake；纯播放模块没有 VM 反向依赖。WORLD 导出宏随 L4 链接组。
- eve.BakeRecipe 使用独立 VM 包装类型持有 canonical BakeRecipe，不重复注册 CollapseBakeRecipe 的原生别名；所有校验与编码仍委托原生值。RNG 种子使用十进制字符串保留完整 uint64，拒绝溢出/负号/尾随字符。
- loadRigid 必须通过完整预期 recipe 校验才产生 VM-owned 原生 RigidPlayback；复制共享不可变片段且拥有独立时钟与实例变换，暴露稳定 ID 和 checked 姿态采样。无隐式重新录制、回调、世界指针或新持久格式。
- 新 VM 用例覆盖两种新旧类型注册顺序、最大种子和失败不变、空/错误类型、身份不匹配、独立时间/变换和 GC 后其他播放器继续采样；尚未运行。
- 新模块六面合同和成本/生命周期记录在 docs/usr/modules/physics_bake_script.md；适用 Result、单一状态所有权、时间注入、模块边界、导出宏和脚本生命周期规范，无架构例外。
- 架构改动行、layering、quality metadata、diff 空白检查通过。宿主构建 session 14423（/tmp/evengine-bake-script-build.log）仍在运行；不得重启同目录并行构建。最新完整 gate 日志 /tmp/evengine-bake-script-check.log，handle 在后续记录。
- 录制与布料/绳索/粒子脚本入口、完整裁剪/性能及跨平台验证仍未完成。

- 本轮完整 gate handle 12570；新增 EVPhysicsBakeScript 已编译成功，宿主链接仍在进行。生成绑定合同 11554 条、0 unresolved、0 placeholder；包含 BakedRigidPlayback 的真实方法。该信息不替代 VM 用例运行。

通用刚体脚本首次运行验证：

- 宿主 unit_test_physics 构建 14423 exit 0；新测试实际编译并注册到 CTest。
- physics.bake.scriptRigidPlaybackOwnsLoadedTrackAndCoexistsWithLegacyRecipe 1/1 通过（0.03 秒，CTest 总 0.19 秒），日志 /tmp/evengine-bake-script-tests.log。包含第二种注册顺序下实际删除资产文件，再 GC 其他包装对象后继续通过 surviving player 采样。
- 上述证据覆盖 VM 真实投影和生命周期，不是仅语法检查；仍不代表录制/布料/绳索/粒子脚本闭环。当前完整 gate 12570 待终态，无运行中的宿主构建。

通用变形脚本入口推进：

- 前一刚体脚本完整 gate 12570 exit 0，461 脚本测试通过；该结果不覆盖随后变形入口的全部新增代码。
- physics_bake_script 增加 loadDeform，使用原生 DeformTrack::loadForRecipe 和 DeformPlayback。prepareInstance 显式准备独立 scratch 和默认时间状态，避免隐藏昂贵复制；控制接口仍使用同一原生时钟与变换。
- captureSample 返回 VM-owned BakedDeformSample，拥有独立位置缓冲和共享不可变拓扑。一次线性采样/分配后，point/index/uv 通过索引做常数成本 checked 读取，不生成全量嵌套脚本表，不保留 player 指针。文档明确其成本及原生批量表现接口的用途。
- 新 VM 用例覆盖 3D 三角形/UV 和 2D 线段/无 UV、错误资产格式、null 配方、越界、实例默认状态、平面 placement 拒绝、历史帧不变、文件删除和播放器 GC 后帧存活；尚未运行。
- 新代码 EVPhysicsBakeScript 已编译成功，无运行用例结论；当前宿主构建 session 57589，日志 /tmp/evengine-bake-deform-script-build.log。当前完整 gate session 38030，日志 /tmp/evengine-bake-deform-script-check.log。下一轮继续这些 handles，禁止同 build 目录重叠构建。
- 架构改动行与 diff 空白检查通过；测试覆盖登记已更新（202 模块）。无新增 ECS System、Link、持久格式或架构例外。
- 录制、粒子脚本、脚本到批量表现、整套裁剪/性能及跨平台验收仍待完成。

变形脚本运行验证：

- 宿主构建 57589 exit 0；刚体和变形 VM 回归 2/2 通过（分别 0.03/0.02 秒，总 0.21 秒），日志 /tmp/evengine-bake-deform-script-tests.log。
- 真实 VM 通过上述两种坐标/拓扑的快照独立性、文件删除、播放器回收后位置与拓扑访问测试；未运行窗口表现，不将数值采样等同于脚本可视化闭环。
- 完整 gate 38030 仍需终态检查；当前无运行中的构建或测试。

粒子脚本重放推进：

- 前一变形完整 gate 38030 exit 0，461 个脚本测试通过（69.705 秒）；不能作为随后粒子入口全部修改的完成证据。
- PhysicsBake 增加 loadParticles，直接调用 ParticleTrack::loadForRecipe 和 BakedParticlePlayback::prepare；VM-owned 玩家提供同一时间/空间控制和显式 prepareInstance，纯播放仍不依赖 particles 模拟模块。
- captureSample 返回 owning BakedParticleSample 帧句柄，只暴露可见粒子；particle(index) 的稳定身份为 emitter UUID + 十进制 uint64 birthSerial，不截断为脚本浮点。所有 sprite 数值字段和保持式 UV 均投影原生结果。帧销毁不影响播放器，播放器/文件销毁不影响已完成帧。
- 新 VM 用例覆盖出生包含/死亡排除、同时间死亡/另一次出生、完整最大序号、跨 360 度插值、保持 UV、默认独立实例、平面 placement、删除文件和 GC 后快照；尚未运行。
- 架构和 diff 空白检查通过，新 EVPhysicsBakeScript 已编译。宿主构建 session 82953，日志 /tmp/evengine-bake-particle-script-build.log；完整 gate session 88819，日志 /tmp/evengine-bake-particle-script-check.log。继续原 handles，不启动同目录重叠构建。
- 下一录制接入需定义 owning 会话：每次捕获借用源，保留稳定身份和值帧，不保留跨调用源裸指针；配方和播放时间继续保持单一原生 owner。脚本数值采样尚不能替代批量表现的可运行示例和完整裁剪/性能验收。

粒子脚本运行验证：

- 宿主 unit_test_physics 构建 82953 exit 0；刚体/变形/粒子真实 VM 回归 3/3 通过（总 0.24 秒），日志 /tmp/evengine-bake-particle-script-tests.log。
- 粒子边界帧保留完整序号且未混合不同身份；UV、角度和时间控制通过上述精确断言。资产删除后独立玩家继续采样，玩家 GC 后快照存活。
- 无运行中的宿主构建或测试；完整 gate 88819 仍待终态。该验证无窗口，不声明脚本表现闭环或 GPU 性能完成。

有界变形录制会话推进：

- 前一粒子脚本完整 gate 88819 exit 0，461 个脚本测试通过（69.541 秒）。
- 新增纯 physics_bake 的 DeformRecording：checked prepare 冻结配方和显式 1..2000000 总顶点样本预算；append 只保留 owning 值帧，共享原生逐帧校验，拒绝时已录数据和计数保持不变；freeze 线性复制为不可变轨迹，会话可继续追加，旧轨迹不变。
- DeformTrack 的逐帧验证抽成同模块内部唯一实现，录制与加载一致检查时间网格、严格递增、精确拓扑/UV、有限坐标、平面限制及顶点数。没有新增世界/VM 指针、ECS System、持久格式或隐式物理更新。
- 首次测试登记误用了不存在的 pure-value 分类，CMake 按合同拒绝；已改为 local-logic。模块证据检查要求小型单元文件合并，故新用例归入 physics_bake 现有模块测试文件，没有改变检查器或基线。
- 独立无 solver/graphics/script profile 最新构建 79603 exit 0，20/20 测试通过，日志 /tmp/evengine-deform-recording-core-build2.log 和 /tmp/evengine-deform-recording-core-tests.log。覆盖失败不追加、预算边界、配方冻结、输入独立、继续追加不改变旧轨迹和移动会话。
- 真实布料/绳索组合测试已改为 DeformRecording 路径，在撕裂/切断帧拒绝后追加合法帧，源对象销毁后 freeze/save/load/playback；此修改尚未运行。
- 当前宿主构建 session 13932，日志 /tmp/evengine-deform-recording-host-build.log；完整 gate session 25411，日志 /tmp/evengine-deform-recording-check.log。继续原 handles，不启动同 build 目录重叠构建。
- WORLD/VM 脚本录制卫星尚未接入该会话；刚体录制会话、粒子脚本录制、批量表现和完整裁剪/性能/跨平台仍未完成。本批适用 checked Result、明确成本、单一所有权、固定时钟和共享验证合同，无架构例外。

变形录制原生组合与脚本接入：

- 上一宿主构建 13932 exit 0；布料/绳索实际捕获会话和有界会话用例 3/3 通过（0.29 秒），日志 /tmp/evengine-deform-recording-host-tests.log。
- 上一 gate 25411 exit 2：新增本轮模块期间测试证据表变旧，未声明完整通过。现已刷新为 203 模块，并启动最新门禁。
- 新增 L6 physics_bake_recording_script，依赖 playback VM 与 cloth/rope capture；PhysicsBakeRecording.beginDeform 返回 VM-owned DeformRecording，captureCloth/captureRope 对真实源做类型守卫并只借用当前调用，save 冻结后复用原有持久化。源先销毁不影响已录帧。
- 播放 VM 导出 copyScriptBakeRecipe，公开边界只前置声明域值，返回拥有元数据的 checked 副本；不泄漏私有包装器，也不建立第二套配方规则。跨组 WORLD/ORCHESTRATION 导出已声明。
- 新真实 VM 组合用例创建并 step Cloth3D/Rope3D、录两帧、拒绝错误类型/时间/拓扑，源释放后保存；校验配方修改不改变会话、重复保存拒绝、加载后录制会话释放仍能采样。Rope.cut 按现有脚本 bool 返回合同断言，不假设原生 Result 自动投影。该新用例尚未运行。
- 架构和 layering 检查通过。当前宿主构建 session 46678，日志 /tmp/evengine-deform-recording-script-build.log；完整 gate session 69053，日志 /tmp/evengine-deform-recording-script-check.log。继续原 handles，不重叠构建。
- 文档 docs/usr/modules/physics_bake_recording_script.md 记录六面、借用生命周期、显式时间和费用。刚体/粒子录制脚本、批量表现与全量裁剪/性能/平台验收仍未完成；无批准的架构例外。

变形录制脚本首次运行结果与修正：

- 宿主构建 46678 exit 0；首次 VM 回归 3/4 通过，新增用例在最终 frame.point(0).y < 3 断言失败，日志 /tmp/evengine-deform-recording-script-tests.log。录制/保存/重载前面的断言已走通，但不能记为整个用例通过。
- 核对 ClothModel::grid：顶行 inverseMass=0，默认固定，故原测试的自由下落假设错误。测试在初始捕获前显式 unpin 顶行 0/1/2，保留产品默认固定行为。最新重构建 session 73321，日志 /tmp/evengine-deform-recording-script-build2.log；待重跑全部四个 VM 用例。
- 新模块完整 gate 69053 exit 0（461 个脚本测试，69.977 秒），日志 /tmp/evengine-deform-recording-script-check.log；该门禁运行期间追加了上述测试场景修正，不能将此结果视为修正后的宿主运行证据。
- 下一轮先继续 build 73321，再运行 ^physics.bake.script(Records|Rigid|Deform|Particle)；当前没有其他运行中的构建。

变形脚本修正验证及刚体会话推进：

- 修正后的宿主构建 73321 exit 0；四个真实 VM 用例 4/4 通过（总 0.31 秒），日志 /tmp/evengine-deform-recording-script-tests2.log。实际 Cloth3D/Rope3D 录制、源释放、存储、身份校验重载和会话释放后采样已走通；该测试不含窗口表现。
- 新增 RigidRecording，以 recipe 确定坐标并冻结 provenance，有界追加 owning 帧；首帧固定身份 roster，候选排序/归一化后才提交，失败不修改历史帧或计数。freeze 形成独立轨迹，继续录制不影响旧片段。
- RigidPoseTrack 的逐帧验证抽成同模块内部唯一实现，录制与资产加载共用；未知坐标、时间、身份、平面、有限数、零四元数仍按既有规则拒绝。没有新增持久格式或运行时源指针。
- 无 solver profile 构建 5246 exit 0，21/21 测试通过，日志 /tmp/evengine-rigid-recording-core-build.log 和 /tmp/evengine-rigid-recording-core-tests.log。新测试覆盖两个坐标空间、完整失败序列后恢复、预算、归一化、配方冻结、移动和旧轨迹不变。
- 真实 2D/3D capture 测试改用 RigidRecording，并将重载升级为 loadForRecipe；源 body 销毁后计数保持，世界销毁后 freeze/save/reload/表现提交仍需最新宿主运行确认。
- 当前宿主构建 session 15198，日志 /tmp/evengine-rigid-recording-host-build.log；当前无其他宿主构建。后续继续该 handle，不启动重叠构建。
- 刚体脚本录制、粒子脚本录制、批量表现、全量裁剪/性能与跨平台验收仍未完成；适用 Result、单一所有权、注入时间、共享验证和成本规则，无架构例外。

- 最新完整 gate session 32515（/tmp/evengine-rigid-recording-check.log）仍在运行。架构改动行检查和模块测试证据刷新通过。

刚体脚本录制接入：

- 上一宿主构建 15198 exit 0；2D/3D 实际捕获经有界会话与 loadForRecipe 的组合及会话测试 3/3 通过，日志 /tmp/evengine-rigid-recording-host-tests.log。上一完整 gate 32515 exit 0。
- recording_script 增加 beginRigid/RigidBakeRecording，bindBody2D/3D 检查真实源类型、同一 live world、稳定非 nil UUID 与重复绑定，只保留 generation-qualified world/body handles 和 PersistentId。首帧成功后 roster 锁定，capture2D/3D 复用 captureRigidFrame 和 RigidRecording；无 source 裸指针跨调用。
- RigidRecording.space() 暴露冻结配方的坐标权威，脚本入口没有复制第二份可变坐标状态。save 仍冻结并公共发布，源先销毁不影响已接受帧，会话先销毁不修改世界。恢复世界必须新建会话/绑定，不持久化 runtime handles。
- 新真实 VM 用例覆盖 2D/3D 创建/step、错误类型/坐标/世界、重复绑定、首帧后拒绝重绑定、源 body 销毁再建时旧句柄拒绝、世界销毁后保存重载及片段采样。像素 meter 在创建 body 前显式设置，形状包装器保留至 body 销毁。新用例尚未运行。
- 架构、layering 和 diff 空白检查通过；宿主构建 session 26714，日志 /tmp/evengine-rigid-recording-script-build.log；完整 gate session 3374，日志 /tmp/evengine-rigid-recording-script-check.log。继续原 handles。
- 粒子脚本录制、脚本批量表现、完整裁剪/性能和跨平台验收仍未完成，无架构例外。

刚体脚本录制运行验证：

- 最新宿主构建 26714 exit 0；五个真实 VM 用例 5/5 通过（总 0.32 秒），日志 /tmp/evengine-rigid-recording-script-tests.log。
- 新用例证实 2D/3D 录制、旧 generation handle 在 body 销毁重建后拒绝、世界销毁后的保存/配方重载及独立采样。没有窗口证据，不将这等同于脚本批量渲染闭环。
- 当前无运行中的构建或测试；完整 gate 3374 尚需检查终态。

独立粒子脚本录制与多源归档：

- 上一刚体脚本完整 gate 3374 exit 0（461 脚本测试，69.568 秒）。
- 新增 L6 physics_bake_particle_script，独立于刚体/布料/绳索录制。begin/finish 复用 emitter-owned 原生日志；finish 预建拥有存储和 VM Result 后才转移日志，输出不可变共享 RecordedParticleJournal。
- 日志时长使用覆盖累计 double 时刻的向上 float 舍入，避免 1/60 累积后最后事件落在错误的较短 float 时长之外；原有事件键精度与不可区分时间拒绝合同保持。
- ParticleBakeArchive 冻结 pixels2d 完整配方，addJournal 共享拥有日志，拒绝 nil/重复 UUID、重复日志及合计 2 百万事件预算，最多 65536 源。save 复用多源 buildParticleTrack 与公共发布，转换/IO/已有目标失败不消耗源日志，允许修正路径重试。
- 新真实 VM 测试配置两个 CPU 发射器、以 1/60 步长推进 30 次、关闭日志并合并；覆盖 GPU begin 拒绝、重复开始/结束、重复归档源、向上时长、日志包装器释放、源 stop/reset、IO 重试、配方冻结、完整身份与末尾空帧。追加预算耗尽拒绝发布和显式重新 prepare 后空日志恢复。尚未运行。
- 六面、生命周期、时间协调、CPU 策略限制及成本写入 docs/usr/modules/physics_bake_particle_script.md。源码架构/layering 检查通过；EVPhysicsBakeParticleScript 已编译。
- 当前宿主构建 session 48127，日志 /tmp/evengine-particle-recording-script-build.log；完整 gate session 61594，日志 /tmp/evengine-particle-recording-script-check.log。下一轮继续原 handles，不重叠构建。
- 脚本批量表现、完整裁剪/性能和跨平台验收仍未完成；GPU/ribbon/排序粒子仍明确不支持，不把 sprite 通道等同于所有粒子效果。

粒子录制脚本首次 VM 运行与参数修正：

- 宿主构建 48127 exit 0。首次六个 VM 用例 5/6 通过，新用例在 buffer:20 的 setRenderMode 调用因 wrong number of parameters 停止；日志 /tmp/evengine-particle-recording-script-tests.log。此前五个用例均通过，新粒子录制不计通过。
- 核对 ParticleEmitter::setRenderMode(mode, stretchFactor)：C++ 默认值不会自动补齐此直接 Squirrel 绑定。测试两处补充显式 1.0；其余 setLifetime/setSpeed/begin/emit/stop/reset 参数数已对照实际声明。
- 修正后重构建 session 74307，日志 /tmp/evengine-particle-recording-script-build2.log。完成后重跑 ^physics.bake.script(Records|Rigid|Deform|Particle)。当前完整 gate 61594 仍待终态。

粒子脚本录制修正验证与刚体表现桥：

- 修正后宿主构建 74307 exit 0；六个 VM 回归 6/6 通过（总 0.35 秒），日志 /tmp/evengine-particle-recording-script-tests2.log。新增粒子用例证实两个真实 CPU 发射器日志合并、1/60 累积时长、耗尽日志拒绝及显式重新录制、停止/清空源后归档重试和身份重放。
- 完整 gate 61594 exit 0（461 脚本测试，72.610 秒），日志 /tmp/evengine-particle-recording-script-check.log。该证据不覆盖后续新表现桥。
- 新增可选 L4 physics_bake_graphics_script，依赖 native adapter 与 playback VM；submitRigid2D/3D 类型守卫后直接调用 submitRigidPlayback，不复制姿态到脚本表，不创建实体/资源或推进时间，不保留玩家/目标裸指针。纯播放与 native graphics 不反向依赖 VM 桥。
- 新组合用例注册真实 Graphics/PhysicsBake VM 类，提交到真实 Renderable2D/3D 并检查 TRS/世界矩阵；覆盖空值、错误类/坐标、无效/未知 UUID 和失败后目标不变。无窗口，不当作视觉验收；尚未运行。
- 六面、生命周期、费用与后续 TRS 写入语义在 docs/usr/modules/physics_bake_graphics_script.md。架构改动行检查通过。
- 当前宿主构建 session 77587，日志 /tmp/evengine-bake-graphics-script-build.log；完整 gate session 25435，日志 /tmp/evengine-bake-graphics-script-check.log。继续原 handles，禁止同目录重叠构建。
- 布料/绳索/粒子的脚本批量表现、完整裁剪/性能与跨平台验收仍未完成；无架构例外。

刚体脚本表现桥验收范围校正：

- /tmp/evengine-bake-graphics-script-build.log 证实 unit_test_physics 构建完成；/tmp/evengine-bake-graphics-script-check.log 证实完整 make check 通过（461 个脚本测试，70.367 秒）。
- 重新运行既有脚本回归，6/6 通过，日志 /tmp/evengine-bake-graphics-script-tests.log。这六项不包含新表现桥：生成的 test/CMakeFiles/unit_test_graphics.dir/build.make 将 script_presentation.cpp 分配到 unit_test_graphics，因此 physics 构建通过不能证明该用例编译或运行。
- 已启动真正所属 graphics 测试域构建，session 66058，日志 /tmp/evengine-bake-graphics-domain-build.log；终态前不重复启动同目录构建。下一步运行精确用例 physics.bake.scriptRigidPresentationBorrowsTargetsAndPreservesFailures。
- 变形脚本资源接入核查：Graphics::newMesh* 返回 Graphics 所有的借用句柄，releaseMesh 成功才转移 CPU facade 删除责任。新接口不能将该借用指针交给 makeOwnedSquirrelInstance；应保留既有 graphics 资源所有权并验证源/表现/图形宿主销毁次序。

刚体脚本表现桥实际编译与运行结果：

- graphics 域首次构建 66058 exit 2：新用例 REQUIRE 中组合两个 nullptr 比较触发 zeroerr 表达式分解 operator&& 二义性；不是通过。已拆为独立断言，保留两个目标的检查。
- 修正后 graphics 域构建 56875 exit 0，日志 /tmp/evengine-bake-graphics-domain-build2.log。
- 精确用例 physics.bake.scriptRigidPresentationBorrowsTargetsAndPreservesFailures 1/1 通过（0.05 秒），日志 /tmp/evengine-bake-rigid-script-presentation-test.log。证实真实 VM 绑定向 2D/3D Renderable 提交、实例变换和独立时钟、错误输入保持目标；不包含窗口或像素验收。
- git diff --check 通过。完整 make check 的通过证据来自本次断言拆分之前；该修正没有变更模块/API/格式。所有进程已终止，无待轮询构建。
- 本次适用规则：checked Result、同步借用生命周期、独立时间、可选脚本边界、真实组合测试；无架构例外。下一阶段继续变形与粒子脚本批量表现及完整目标验收，不将这七项 VM 用例当作全系统完成。

布料/绳索脚本批量表现接入：

- PhysicsBakeGraphics.prepareDeform 从原生 DeformPlayback 的共享不可变片段准备 VM 所有的 DeformMeshPresentation；表现对象内部播放器是其时间/placement 唯一 owner，默认状态独立于输入播放器。控制方法直接委托，不外借内部 player。
- createMesh 返回 Graphics 所有的借用 Mesh，并通过标准 Result 标记 hasValue/ownership；遵循既有 Graphics.newMesh* 指针投影（simplesquirrel pushByPtr 不安装删除钩子）。upload 同步借用 Graphics/Mesh，类型守卫后委托原生批量采样与 GPU 上传，不逐顶点生成脚本 Value。
- 六面、准备/上传成本、恢复方式、源/表现/网格所有权及错误后跳过绘制的语义已补入用户文档。脚本表现 TU 已加入 HOT_PATH_GLOBS；独立 architecture contracts 检查通过，无新增上行依赖或架构例外。
- 新窗口用例 scriptDeformPresentationRendersAfterSourceAndPresenterRelease 覆盖三角形和线段：释放输入播放器后使用两份独立表现、暂停/倒放/placement、错误类型和 foreign/released 网格、表现释放后最后几何仍可画，并用引擎 getPixel 校验中心红/黑。当前尚未运行，不计视觉通过。
- graphics 域构建 29521 exit 0，既有刚体 VM 表现用例再次 1/1 通过（/tmp/evengine-bake-deform-rigid-regression.log）。新窗口用例的生成 build.make 实际归属 unit_test_physics；已顺序启动 physics 构建 10899，日志 /tmp/evengine-bake-deform-script-physics-build.log。完整 gate 63197 仍运行，日志 /tmp/evengine-bake-deform-script-check.log；继续这两个 handles，勿重叠宿主构建。

布料/绳索脚本表现窗口验收结果：

- physics 域构建 10899 exit 0，新增窗口用例实际编译并进入 CTest；日志 /tmp/evengine-bake-deform-script-physics-build.log。
- 窗口用例 79205 exit 0，精确 case 1/1 通过（2.51 秒），日志 /tmp/evengine-bake-deform-script-window-test.log。三角形双面表面和八边形绳索管均经过真实 Squirrel 工厂/控制/createMesh/upload，使用引擎 getPixel 验证移动出视野、倒放和两份表现 placement 独立；无 Xvfb framebuffer 截图。此证据使用 Lavapipe 软件 Vulkan，不代表独显性能。
- 生命周期实测覆盖输入播放器与局部源片段释放后继续播放、表现对象先释放仍可画网格，以及 releaseMesh 后活着的 detached facade 拒绝再次上传。未故意解引用宿主销毁后的裸网格；调用方仍必须遵守既有 Graphics 借用句柄生命周期。宿主先销毁的组合验收保留为后续工作。
- 完整 make check 63197 exit 0，461 Python 用例、72.915 秒，日志 /tmp/evengine-bake-deform-script-check.log；独立改动行架构检查和 git diff --check 均通过。当前无运行中的 build/test/gate handles。
- 布料/绳索脚本批量表现已接入并取得上述范围的窗口证据；可运行最终示例、粒子脚本材质/批量表现、完整模块裁剪、性能/误差量化及跨平台验收仍未完成。M1–M8 总目标保持进行中，无架构规则例外。

粒子脚本批量表现实现：

- PhysicsBakeGraphics.prepareParticles 从原生 BakedParticlePlayback 准备独立 ParticleBakePresentation；复制调用者提供的有序、唯一、非 nil emitter UUID 数组（上限 65536），一次预留材质与引用绑定缓冲。ID 字符串仅在准备阶段解析；热路径只使用强身份值。
- BakedParticlePresentation.draw(graphics, materialSprites) 同步读取 Renderable2D 的纹理/法线/着色器/canvas/camera/layer/blend/light 策略，委托现有原生粒子批处理。源 sprite 几何/颜色/UV/visible 不覆盖录制资产。所有材质引用由 scope guard 在成功或失败返回前清除；只持有独立原生播放器和拥有型身份/缓冲。
- 脚本时间控制抽为同 TU 类型模板，变形与粒子表现均直接转发各自唯一原生 player。新增接口、成本、六面与资源存活条件已写入用户文档；无新 module/Link/provider 或架构例外。
- 新窗口用例 scriptParticlePresentationBatchesMaterialsAndRejectsPartialDraw 覆盖两发射会话同 birthSerial 的双材质、图层切换、源播放器和 ID 数组释放/修改、独立实例与末尾死亡、placement、空/错类型/重复/乱序身份，以及漏绑和第二材质非法时提交前失败。当前尚未运行，不计窗口通过。
- 测试源码按生成 build.make 归属 unit_test_physics；宿主构建 session 12384，日志 /tmp/evengine-bake-particle-script-presentation-build.log。完整 gate session 61776，日志 /tmp/evengine-bake-particle-script-presentation-check.log。两个 handles 均仍活动；继续轮询，不重叠宿主构建。
- 独立改动行 architecture contracts 和 git diff --check 通过。完整最终示例、纹理/着色器/canvas/camera 各组合、性能/裁剪/平台验收仍待完成。

粒子脚本表现实际窗口验证：

- unit_test_physics 构建 12384 exit 0，日志 /tmp/evengine-bake-particle-script-presentation-build.log；新 test TU 实际编译并注册。
- 窗口测试 46386 exit 0，2/2 通过（变形 2.51 秒、粒子 1.39 秒），日志 /tmp/evengine-bake-particle-script-presentation-tests.log。覆盖新增粒子脚本工厂/材质绑定/批量 draw 的真实像素行为，及控制方法模板抽取后的布料/绳索回归。
- 粒子用例证实两会话同 serial 的独立身份、多材质图层即时切换、输入 player/ID 数组释放或修改后的播放、两实例时钟与 placement 独立、末帧死亡返回零，以及缺失第二 emitter 或错误第二材质时整批不绘制。像素来源为引擎 getPixel；环境为软件 Vulkan，非独显性能证据。
- 这项用例使用原生冻结轨迹夹具，不代替已有真实 emitter 录制/持久化测试；纹理、法线、着色器、canvas/camera 具体组合仍需验证。完整 gate 61776 尚在运行，继续该 handle。

粒子脚本表现源码门禁终态：

- 完整 make check 61776 exit 0，461 Python 用例通过（70.514 秒），日志 /tmp/evengine-bake-particle-script-presentation-check.log。宿主构建与两个窗口用例均已结束，当前没有需要继续轮询的 handle。
- 本批遵守 checked Result、唯一时间 owner、显式准备成本、同步资源借用、失败提交原子性、模块分层与 code-first 热路径登记，无批准或隐含例外。
- 当前刚体、变形、CPU sprite 粒子的脚本表现均有实际组合验证；仍不能据此勾选整套 M1–M8 完成。下一步优先建立通用可运行示例，将真实录制、持久化、源销毁、重载和多实例表现串联，并继续纹理/相机/离屏资源组合、完整裁剪与性能/误差/平台验收。

通用示例准备中发现并修复源对象脚本所有权缺口：

- 生成并读取 /tmp/evengine-bake-example-api.json（生成 session 1173 exit 0），遵循 EveScript 正常生命周期与实际绑定。尚未创建新示例，先解决源销毁证据不成立的问题。
- 源码核对发现 Cloth.newCloth3D、Rope.newRope3D 和 Rope.newRope3DFromJson 原绑定直接返回 raw pointer；simplesquirrel pushByPtr 不设置删除 hook。因此先前 script_recording 中 source=null/collectgarbage 只证明包装器释放，不能单独证明原生源析构。此前 C++ 拥有型源删除测试证据不受影响。
- 三个脚本工厂现通过 makeOwnedSquirrelInstance 包装唯一所有权；正常返回仍为原有实例，失败保持原有异常接口；候选对象在发布失败时自动释放。C++ 工厂仍向原生调用者转移所有权，未改变 2D/GPU cloth 的既有合同。对应模块手册及 Rope 原生 facade 所有权注释已校正。
- 真实 VM 录制用例在实例上检查非空释放 hook，再用计数 wrapper 调用该原 hook；不比较不同 DLL 中 inline 模板的函数地址。源释放后确认 cloth=1、rope=1 再存盘/加载/播放；JSON rope 的别名保活和最后别名释放后 rope=2 也验证通过。
- 宿主构建 41072 exit 0，日志 /tmp/evengine-bake-source-ownership-build.log；三项脚本录制回归 3/3 通过，总 0.29 秒，日志 /tmp/evengine-bake-source-ownership-tests.log。新增计数断言已实际执行，现在具备脚本源原生释放后重放的更强证据。
- 源码门禁 session 1336 仍运行，日志 /tmp/evengine-bake-source-ownership-check.log；继续该 handle。此修复属于烘焙生命周期闭环，不代表通用示例已经交付。下一步仍需示例、粒子 ECS 源显式释放路径、身份版本接入、性能/裁剪/平台验收。
- 为后续正常示例运行启动实际 eve 宿主目标构建，日志 /tmp/evengine-bake-example-host-build.log。此前 physics 构建已终止，未重叠同目录宿主构建；需要以后续 handle 终态确认实际 executable 更新。
- eve 宿主构建 handle 为 55283；完整源码门禁 handle 为 1336。当前两者仍运行，下一轮首先轮询这两个 handles，禁止重复启动宿主构建。适用规则为唯一 owner、同步借用生命周期、Result 检查与兼容脚本边界；无架构例外。

通用四通道示例与共享构建身份：

- 上轮源码门禁 1336 exit 0（461 用例，73.054 秒），eve 宿主构建 55283 exit 0。
- 新 examples/physics-bake-replay/main.nut + config.nut + README.md，已注册示例总览。固定 60 Hz、三秒的真实刚体/布料/绳索/CPU sprite 录制，三维通道 stride=2，持久化后重载，双实例各自 rate/phase/placement。后续 update/render 只有播放器 advance、姿态提交、网格上传和粒子 draw；没有 solver update。
- 刚体源 world 显式销毁，cloth/rope 由 owning VM 引用释放；粒子源 stop/reset 后退役活粒子，但 ECS 实体仍由 ECS 所有，未宣称它已删除。默认缓存只在 NotFound 时生成，其他失败明确停止；playbackOnly 配置强制拒绝缺失缓存，且不影响配方身份。
- 通用 BakeRecipe 暴露 producerBuildRevision/box3DBackendRevision，复用现有 CMake 单份身份生成；生成条件支持仅 physics_bake_script 而无 destruction 目标。生产指纹补纳 particles 源码。没有新增模块依赖或第二份身份清单。完整裁剪构建仍待验证。
- eve + unit_test_physics 构建 72964 exit 0，日志 /tmp/evengine-bake-example-build2.log；新旧配方身份在两种 VM 注册顺序下一致的回归 1/1 通过，日志 /tmp/evengine-bake-shared-identity-test.log。
- 初次运行先发现 filesystem 存档 identity 未初始化，随后发现 deform 预算应为顶点数乘帧数而非仅帧数，均已修正；之后又修正脚本类名为实际绑定的 eve.Sprite2D。失败日志 /tmp/evengine-bake-example-first.log、/tmp/evengine-bake-example-smoke-first.log、/tmp/evengine-bake-example-full-bake.log 均不计通过。
- 12 秒烟测虽然报告进程存活，但当时仅有刚体资产，布料仍在录制；没有据此认定闭环。延长运行后 /tmp/evengine-bake-example-full-bake2.log 证实四项 baked-and-reloaded 和 READY，首次呈现约 24.36 秒。最终 README 使用 30 秒烟测并说明首次 Debug 烘焙开销。
- 最终脚本经官方 30 秒烟测 12494 exit 0，日志 /tmp/evengine-bake-example-final-smoke.log。以 playbackOnly=true 的新进程 18467 命中全部四项 cache-only、READY 并呈现（约 5.81 秒），日志 /tmp/evengine-bake-example-final-reload.log；90 秒诊断超时 exit 124 后结束。配置已恢复默认 false。
- 引擎 MCP eve_screenshot 成功生成并已查看 /tmp/evengine-physics-bake-example-final.png（1280x800），可见两组刚体/布料/绳索和粒子，无调试面板遮挡。使用 Lavapipe 软件 Vulkan；不是 Xvfb 截图，也不代表独显性能。
- UI automation 成功排入 pause/restart 点击，但 capture 输入批次首次返回 readback 未就绪，后续暂停和重启截图哈希完全相同。尚不能据此确认按钮行为；需继续核对实际时间状态与输入派发/帧捕获时序，不能将排入输入当作交互验收。
- make check 15604 exit 0（461 用例，75.213 秒）覆盖主体实现；最终脚本修正后再次启动完整 gate 75162，日志 /tmp/evengine-bake-example-final-check.log，当前仍运行。下一轮先轮询此 handle；无运行中的引擎/构建进程。示例布局 176 项通过，git diff --check 通过。
- 当前依然保留完整目标：交互验收、粒子 ECS 源删除路径、更多材质/相机/离屏组合、完整裁剪、性能/误差与跨平台耐久性等未完成。适用 Result、唯一 owner、时间/RNG 注入、schema/事务、模块边界和成本合同，无架构例外。

四通道示例交互验收：

- 前轮最终 gate 75162 已终态通过：461 用例、70.665 秒，日志 /tmp/evengine-bake-example-final-check.log。前轮取得此终态证据改变了待办状态，不再等待或重启该 handle。
- 排查运行时 eve_status 确认 paused=false，eve_render_status 的 renderFlowEvents 持续更新；没有依据归因于调试暂停。新增正常 UI 播放状态文本，直接展示两个原生播放器的 time/rate 和暂停状态，未通过 eval 修改游戏。
- 新实际宿主进程端口 17845 完成四通道重新烘焙与加载，READY、首次呈现 19.37 秒，日志 /tmp/evengine-bake-controls-clock.log。脚本内容进入缓存身份，因此此次 UI 修改产生新的资产键。
- 用 eve_ui_click 经真实 pendingEvents/dispatch/consumeClick 路径操作，再用 eve_ui_get 读取 clock：暂停间隔一秒不变；暂停时 Restart 两时间均 0.00；Reverse 速率变为 -1.00/-0.65；恢复后时间从 2.42/2.62 递减到 1.92/2.29。自动断言全部通过，原始响应 /tmp/evengine-bake-controls-evidence.json。
- 引擎截图 /tmp/evengine-bake-controls-verified.png 已实际查看：暂停时间 1.79/2.21、有符号负速率、两组布料/绳索和粒子可见。截图来自引擎 readback，首次未就绪后重试成功。软件 Vulkan 证据，不作独显性能声明。此前相同截图不再作为交互失败或通过的依据。
- 示例布局检查 176 项与 git diff --check 通过。本轮仅新增示例可观察状态与说明，没有公共 API、持久格式、模块或生命周期改动；沿用单一播放器时间 owner，无架构例外。最终全门禁日志 /tmp/evengine-bake-controls-check.log，需收取终态。
- 本轮引擎 session 5262 已结束 exit 0（验收后按精确端口停止），无遗留示例进程。门禁 session 68944 仍活动；已实际核对 make PID 516820 和 check_bindings.py --strict PID 517667 存活，继续轮询该 handle，不因日志停顿重启。
- 下一项源生命周期核对：ParticleEmitter::release 已存在并最终调用 ecs::DestroyEntity(this)；脚本 Emitter 仍使用原生指针绑定。后续需先处理生成代际/别名失效语义，再接入显式退役，不能直接添加会留下悬垂脚本别名的 release 绑定。

粒子录制源生命周期继续验收：

- 前轮 gate 68944 已终态 exit 0，461 项测试通过（70.782 秒），日志 /tmp/evengine-bake-controls-check.log。
- 新增 scriptParticleAssetOutlivesProducerVmAndEcsSources：真实 VM 生成两个 CPU 发射器、录制并发布；生产 VM 完全析构后由宿主调用源 release，检查两个旧 ECS handle 均不能 resolve。新的 VM 仅注册 PhysicsBake，加载文件、验证稳定会话身份/重复 birthSerial、独立时钟与 placement；删除文件、释放第一实例后第二实例仍采样，末端为零。
- 该用例目前已写入并登记 test_levels 与模块覆盖，尚未执行通过；宿主 unit_test_physics 构建 handle 83275，日志 /tmp/evengine-bake-particle-retirement-build.log。此测试证明的范围将是源/VM 生命周期隔离，不是无粒子库链接的完整 manifest 裁剪。
- 没有把原生 release 直接暴露给脚本；原有裸指针 Emitter 包装器仍存在别名失效风险，脚本主动删除路径保持待办。用户文档明确 stop/reset/丢弃脚本引用不删除 ECS 实体，原生退休须先结束全部借用；同时修正材质批量绘制尚未接入的过时描述，指向已实现的 graphics script 接口。
- 本轮无公共 API/格式/模块/ECS 模型变更。适用唯一 owner、显式生命周期与证据范围；无架构例外。最终源码门禁日志 /tmp/evengine-bake-particle-retirement-check.log，终态另记。
- 构建 83275 exit 0，但首次 CTest 输出 No tests were found，不计任何通过。生成 build.make 确认新增 ParticleEmitter.h 后该 TU 被自动归入 unit_test_particles（此前仅 Particles.h 时归于 physics）。已启动正确目标构建 90069，日志 /tmp/evengine-bake-particle-retirement-particles-build.log；没有重叠同目录构建。完成后须用 --no-tests=error 运行两个 scriptParticle 生命周期/归档用例。
- 本轮完整源码 gate handle 62417 仍运行；日志 /tmp/evengine-bake-particle-retirement-check.log。两项待终态任务均保留原 handle。

粒子生命周期测试终态：

- 正确目标 unit_test_particles 构建 90069 exit 0；新增 TU 已实际编译，CTest 自动发现两个用例。
- 使用 --no-tests=error 的定向 CTest exit 0，2/2 通过（0.21 秒）；日志 /tmp/evengine-bake-particle-retirement-tests.log。新用例实际验证生产 VM 析构、两个源 ECS 句柄失效、仅播放绑定的新 VM 重载、多实例身份/时间/placement，以及文件删除后继续采样。
- 此证据不覆盖仍持有裸指针别名时主动删除源、不替代完整链接裁剪，也不证明粒子 GPU/ribbon 模式。没有新增公共删除 API；这些范围保持待办。
- 完整源码 gate 62417 exit 0，461 项测试通过（71.519 秒），日志 /tmp/evengine-bake-particle-retirement-check.log；最终 git diff --check 通过。本轮全部构建/测试/gate 已终态，无待轮询进程。

独立 Release 多通道采样测量：

- 新 physics_bake_measure 探针直接链接既有生产 runtime，支持 rigid/deform/particles 与 1..1024 实例。记录加载/准备/采样时间、文件字节数、采样值数量和位置校验和；采样窗口包括 seek、Result 检查和校验和，不包括分配、IO、上传或渲染。每实例一次预热，之后 2400 次查询，240 个时间点含端点重复十次，奇数实例反向。没有新增引擎 API、格式或模块。
- 独立 Release 目录 /tmp/evengine-1782-bake-release 使用 GCC，实际 link.txt 为 -O3 -DNDEBUG，仅 libphysics_bake_runtime.a 与 zeroerr；ldd 只有标准 C/C++/数学库，无 solver/graphics/Squirrel。该源码组合证明 core codec/player 的 L2/L3，不替代完整 manifest 裁剪。
- 配置 4299、构建 79432 均 exit 0；Release CTest 21/21 通过（含 Linux 发布故障注入），日志 /tmp/evengine-bake-release-tests.log。探针拒绝无参数、非法实例数、未知类型及缺失资产；60 次初测的各组采样数量和校验和稳定。
- 对真实四通道资产执行 1/16/256 实例、每组五个独立进程，原始初测 /tmp/evengine-bake-release-measurements.json。初测与源码门禁并行，暂不作为最终性能数字；计划门禁结束后复测。RSS 用 /usr/bin/time 记录整个进程峰值，不称为资产净占用或逐实例占用；load 未清 OS 页缓存，不称冷加载。
- 最终源码 gate handle 43798，日志 /tmp/evengine-bake-measure-check.log，当前已核对 make/check_bindings 进程仍在运行。无其他构建或测量任务存活。下一步收取该 gate，再单独复测保存正式结果。

Release 测量终态：

- gate 43798 exit 0，461 项测试通过（70.167 秒），日志 /tmp/evengine-bake-measure-check.log。
- 正式复测 96878 exit 0，在本任务无并行 build/gate 时执行四通道各 1/16/256 实例、每组五个进程；另运行真实 507 碎块资产的既有 inspector 五次。各组采样数量/位置校验和稳定。
- 可阅读报告 docs/dev/physics-bake-performance-2026-10-21.md 保存环境、输入 SHA-256、加载/准备/采样时间、文件体积和进程 RSS；原始五次数据 /tmp/evengine-bake-release-final-measurements.json。没有 GPU、实时模拟加速比、冷缓存或逐实例净内存的推断。
- 本轮所有构建/测试/测量/gate 已终态。适用生产路径复用、Result 检查、共享只读所有权和成本分层；没有公共引擎 API、模块/格式变更或架构例外。完整 manifest 裁剪、误差、跨平台与源别名删除仍未完成。

完整 manifest 原生播放裁剪：

- 新根 profile physics-bake-only 通过既有模块清单选择 common + physics_bake；不重新声明模块或手工列出生产源。CMake 与 profile matrix 检查精确闭包和仅 squirrel 第三方组，禁止显式启用额外模块。
- cmake/physics_bake_check 链接完整 EVE_LINK_TARGETS，新增 native manifest 探针与复用测量程序；根 eve_profile_smoke 包含两者。运行器必须找到 present/absent 能力探针和原生播放探针，缺失/不可执行/非零退出均失败，支持 Windows .exe 名称。两个新 Python 用例验证缺失文件不静默通过、.exe 播放失败向上传播。
- 根 Makefile profile/configure 25888、profile/build 85907、profile/smoke 均 exit 0（Linux Debug OBJECT）。目录 /tmp/evengine-bake-manifest/physics-bake-only，日志 /tmp/evengine-bake-manifest-{configure,build,smoke}.log。实际 CMakeCache 仅 common;physics_bake；compile_commands 中模块 TU 为 bake 根目录完整 16 个源，无录制、实时物理、粒子、图形或 VM 卫星。
- /tmp/evengine-bake-manifest-link.txt 记录完整链接命令，含 EVCommon/EVPhysicsBake 对象，无 solver/renderer 对象或 section GC。ldd 只有系统 C/C++ 库；foundation 的 squirrel 静态依赖仍存在，不把此 profile 称为完全无脚本库。
- manifest 探针通过刚体/变形/粒子的 prepare/save/loadForRecipe，删除资产后采样、刚体实例隔离和粒子死亡端点断言。此为 L1 编译、L2 完整对象链接、L3 执行和 L4 已知输出证据。
- 根 profile 的 eve_bake_measure 已加载前轮真实刚体/布料/绳索/粒子资产，两实例各 2400 次查询成功；日志 /tmp/evengine-bake-manifest-real-assets.log。这是 Debug 功能证据，不替代前轮 Release 性能测量。
- 独立失败配置 /tmp/evengine-bake-profile-rejection 验证 EVENGINE_MODULE_physics=ON 被明确拒绝；错误日志 /tmp/evengine-bake-profile-rejection.log。脚本矩阵检查 13 profiles 通过。
- 修正 physics_bake 用户文档内过期的待接入描述，并补充 profile 命令/边界。全量 git clang-format 因已有脏子模块目录 ECS.hpp 返回错误，未据此宣称格式检查通过；新 C++ probe 已单文件 clang-format，完整 make check 自带改动行格式检查已通过。
- SHARED 配置 99074 exit 0，构建 60877 仍活动，日志 /tmp/evengine-bake-manifest-shared-build.log；完整源码 gate 22852 仍活动，日志 /tmp/evengine-bake-manifest-check.log。继续原 handles，不重复启动。
- 适用单一清单、模块闭包/六面证据、Result 与唯一所有权；无公共运行时 API/格式变化，无架构例外。此配置不包括旧 physics_destruction 的独立裁剪、脚本/图形组合或其他平台，完整目标仍未完成。

完整模块 SHARED 验收：

- SHARED 构建 60877 exit 0，根 profile/smoke exit 0，日志 /tmp/evengine-bake-manifest-shared-{build,smoke}.log。manifest 源清单同样只有 bake 16 个模块 TU。
- ldd 的引擎依赖仅此 profile 构建的 EVFoundation/EVBackends 两个分组库；未加载实时物理、粒子或图形模块。四份真实资产在 SHARED eve_bake_measure 上各两实例采样成功，日志 /tmp/evengine-bake-manifest-shared-real-assets.log。
- OBJECT/SHARED 本轮构建与运行均已终态；仅完整源码 gate 22852 待收取。Linux 原生通道的完整模块裁剪已有证据，但旧破坏模块、脚本/渲染配置与跨平台仍为独立待办。
- 最终源码 gate 22852 exit 0，463 项脚本测试通过（70.408 秒，新增两项 smoke 失败传播测试），日志 /tmp/evengine-bake-manifest-check.log。所有本轮任务已终态。
- 下一项旧破坏模块裁剪需要真实模块边界调整：CollapseAnimation/CollapsePlayback/FractureGeometry 的纯实现虽已独立链接过，但仍归属 physics_destruction、导出 DOMAINS；其 capture 方法仍由实时域实现。不能仅把文件追加进另一目标或改变 profile 源清单就声称完成，应保持符号/数据唯一 owner，并一并处理公开头与 Windows 导出边界。

破坏资产独立模块拆分方案：

- 采用 physics/bake/collapse 宿主内卫星 physics_bake_collapse（L3，依赖 physics_bake），拥有 CollapseAnimation/CollapsePlayback、BonePresentation 和 FractureGeometry 的纯值实现与符号。旧 destruction 头路径仅兼容转发，唯一状态与实现均在新模块；脚本注册仍由 destruction 拥有。
- capture 消费方声明 CollapseCaptureSource 三个只读查询（boneCount/bonePresentation/fragmentGeometry），GeometryCollectionInstance 实现。四轴：每次离线采样一个已知源、每采样帧读取、提供方编译期已知、整帧全成或全不成，因此同步接口调用，无 registry/event/跨帧缓存。采样播放本身不调用接口。
- 此接口是非 ECS 对象的无状态边界协议，不是新领域实体根或能力状态；没有新增 ECS System/View/结构修改或跨域 Link。接口只借用当前调用，几何共享为冻结只读值；源先销毁不影响 clip。禁止同一 clip 的采集重入和采集期间源变更，不持锁调用接口。
- canonical 类型导出改归 BACKENDS，实时实例/脚本入口仍为 DOMAINS；现有 C++ capture 调用通过派生接口转换保持源码可用，脚本类型和 EVECOLL1 格式不变。接口变化后重建实际消费者，避免旧对象布局。
- 需同时验证接口拒绝/整帧回滚、原有真实世界采集、独立 profile OBJECT/SHARED 及真实房屋资产；验证完成前不声明破坏模块裁剪已完成，无架构例外。

破坏纯播放模块迁移验收：

- 新模块已落地，旧路径仅转发；采集首帧加入几何合法性验证，bad_alloc 转 checked failure，失败不追加半帧。两项新单测覆盖源查询失败、几何数量/网格错误、非法姿态、拓扑漂移、分配失败与源销毁后的独立实例。
- 实际重建受头布局变化影响的消费者，host eve/unit_test_physics 编译链接完成；核心最终重建 51471 exit 0，23/23 CTest 通过。host 的真实世界、失效源、内容标识、脚本冻结资产与新接口共 6/6 测试通过。
- 根 profile OBJECT 17486、SHARED 34502 的 configure/build/smoke 均 exit 0；日志 /tmp/evengine-bake-collapse-{manifest,shared}-{configure,build,smoke}.log。两者精确模块闭包 common;physics_bake;physics_bake_collapse，compile_commands 均包含 20 个模块 TU（16 个通用烘焙加 4 个破坏纯值实现），无实时物理、图形或脚本注册卫星。
- manifest probe 在两种链接方式下通过破坏/刚体/变形/粒子保存加载与预期样本检查。真实房屋资产在两个独立 inspector 进程均读到 507 碎块、961 帧、16 秒、25,631,534 字节，双实例正反向共采样 121,680 姿态，pose_sum=923788。日志 /tmp/evengine-bake-collapse-{manifest,shared}-real-asset.log；这是 Debug 功能验证，不是新性能结论。
- ldd：OBJECT 仅系统库；SHARED 引擎依赖仅该裁剪目录 EVBackends/EVFoundation。foundation 声明的 Squirrel 静态依赖仍存在。此为纯播放模块 L1/L2/L3/L4 证据，不代表脚本/渲染裁剪或 Windows 导出验收。
- 第一次源码门禁仅因 GeometryCollectionInstance.h 一行格式失败，已修正；重跑 76177 日志 /tmp/evengine-bake-collapse-check.log，终态待收取。
- 本轮适用公共 API 的 Result/成本/生命周期约定、单一状态 owner、消费方窄接口、完整模块清单与裁剪验证、六面边界、事务采集和导出分组。未新增 ECS System/Link、未改变持久格式，无架构例外。跨平台、脚本/渲染裁剪、误差基线和粒子源别名删除仍未完成。

- 门禁 76177 在绑定/分层检查通过后发现自动测试覆盖证据过期；重新执行 test_module_coverage.py --write 与 check_test_manifest.py 已通过（206 模块、318 C++ 源）。最终完整门禁为 18293，日志 /tmp/evengine-bake-collapse-check-final.log，终态待收取。
- 新三模块 OBJECT/SHARED 的 eve_bake_measure 另外复验了真实刚体、布料、绳索和粒子资产，各两实例，全部 exit 0；日志 /tmp/evengine-bake-collapse-{manifest,shared}-real-channels.log。

- 最终完整源码门禁 18293 exit 0，日志 /tmp/evengine-bake-collapse-check-final.log；本轮构建、功能测试与门禁均已终态。整体目标继续，下一步按当前验收摘要推进。

真实仿真重采样误差基线：

- 新增 resampling_error.cpp 两项 physics-backend 组合测试。120 Hz 运行 2 秒的真实 World3D 下落旋转刚体、8x8 固定边布料、16 节点固定端绳索，保存 owning 参考帧；源先销毁，再将同一次参考序列按 1/2/4/8 帧间隔准备生产 track 并重采样全部参考时刻。
- 比较位置/顶点欧氏距离与归一化四元数最短角误差，输出 max/RMS。断言源确实运动、保留帧位置 <1e-5 m、120 Hz 旋转 <1e-6 rad；固定场景位置回归预算 60/30/15 Hz 分别为 1/5/10 mm，旋转 <1e-4 rad。该预算不承诺任意特效精度。
- 首轮 host build 88478、最终增量 build+CTest 17787 均 exit 0。最终两项测试通过，日志 /tmp/evengine-bake-error-final-{build,tests}.log。30 Hz 最大位置误差：刚体 1.366 mm、布料 3.635 mm、绳索 1.262 mm。报告 docs/dev/physics-bake-resampling-error-2026-10-21.md 给出方法、完整表格和适用边界。
- 源码门禁 37062 exit 0，463 项脚本测试通过；日志 /tmp/evengine-bake-error-check.log。更新测试归属证据，无生产 API/格式/模块边界更改，无架构例外。所有本轮进程已终态。
- 此项建立刚体/固定拓扑变形的场景基线；尚不覆盖接触瞬态、碎块旋转、粒子生灭/外观与跨平台差异，不据此将 M8 标为完成。下一项继续补这些精度维度或脚本/渲染裁剪及生命周期合同。

碎块旋转和状态边界：

- 检查生产采样器确认采用 quaternion shortest nlerp 与左侧帧离散状态，无需修复插值实现。新增纯核心测试在 101 个查询时刻验证 179/-179 度短弧、相反四元数不翻转、归一化、精确状态边界，以及倒放中点。
- 独立核心重建/CTest 91059 exit 0，24/24 测试通过；日志 /tmp/evengine-bake-rotation-{build,tests}.log。这是分析姿态合同验证，不作为真实碰撞碎块误差证据。
- 更新 physics_bake_collapse 文档，明确 nlerp 非恒定角速度和端点无法恢复超过半圈/整圈运动的限制，改变录制频率须更新配方身份。未改变公共 API、资产格式或模块边界，无架构例外。
- 完整源码门禁 9072 运行中，日志 /tmp/evengine-bake-rotation-check.log，终态待收取；整体目标继续。

- 门禁 9072 exit 0，463 项脚本测试通过（70.848 秒），完整 source-quality 通过。本轮所有构建/测试/gate 已终态，整体目标保持进行中。

粒子真实表现与生命周期对照：

- 新增 particle_reference.cpp，使用 64 Hz 的真实 CPU 发射器产生 8 个重叠 birth，保存独立实时表现参考，finish 后销毁源，再生产 buildParticleTrack 重放。比较 64 个 tick 的身份集合与 13 个表现字段，覆盖变化尺寸/透明度/旋转、atlas 和数组压缩；逐个验证死亡前一个 float 可见、死亡时刻不可见。
- 主机构建 20010 exit 0，实际新测试归属 unit_test_particles。CTest 1/1 通过、1638 断言，日志 /tmp/evengine-bake-particle-reference-{build,tests}.log。测量方法补入 physics-bake-resampling-error-2026-10-21.md；无 GPU/降采样/跨平台结论。
- 测试清单及模块证据已更新，完整源码门禁 14120 运行中，日志 /tmp/evengine-bake-particle-reference-check.log，终态待收取。无生产 API、格式或模块边界改变，无架构例外。

- 源码门禁 14120 exit 0，463 项脚本测试通过（70.731 秒），完整 source-quality 通过；本轮所有构建与检查已终态。整体目标继续，脚本/渲染裁剪、资源生命周期及跨平台验收仍待完成。

变形渲染目标先释放与恢复：

- 扩展现有 deformMeshesRenderAndUpdateWithoutSimulation 组合测试，同时覆盖三角网格与绳管：releaseMesh 后保持 detached facade 活着验证 upload 拒绝且时钟不变，删除 facade，再由同一 presentation 创建新 GPU mesh；引擎像素读回确认新目标可显示并随 seek/upload 移出/移回。随后仍验证 presentation 先销毁不影响 mesh 绘制。
- 没有向接口传入悬空引用；Graphics owner-first、其他资源类型和后端错误后的恢复是不同合同，未把本项扩大为这些结论。
- host build 9021 exit 0，软件 Vulkan/Xvfb 窗口 CTest 62568 exit 0，1/1 测试、35 断言通过。日志 /tmp/evengine-bake-mesh-retire-{build,tests}.log，图像断言来自引擎读回而非 Xvfb 截图。没有独显性能结论。
- 更新用户文档里的过期“无脚本/待宿主测试”描述。未改变 API/格式/模块边界，无架构例外。完整源码门禁 2788 运行中，日志 /tmp/evengine-bake-mesh-retire-check.log，终态待收取。

- 完整源码门禁 2788 exit 0，463 项脚本测试通过（70.628 秒）。本轮所有任务已终态，整体目标继续。下一项优先增加仅含 physics_bake_script 的完整清单 VM 播放裁剪证据；当前 physics-bake-only 精确闭包没有该卫星，不能用原生 profile 代替它。

完整模块 VM 播放裁剪：

- 新 root profile physics-bake-script-only 在原生三模块闭包上加入 physics_bake_script；沿用模块清单解析、完整 EVE_LINK_TARGETS 和共享 profile checks，不维护第二份生产源清单。CMake/profile matrix 精确拒绝闭包漂移，第三方仍仅 squirrel。
- 新 script_manifest.cpp 原生准备三通道带配方资产，销毁生产值，然后真实 Squirrel VM 只 expose PhysicsBake：加载刚体/变形/粒子，检查独立实例，删除文件并 GC 原播放器后继续采样，最终检查 owning 变形/粒子快照。通过现有 Binding Contracts 目录与生产绑定核对调用，未使用 eve_eval。
- smoke runner 必须找到并执行 eve_bake_script_check；新 Python 测试证明缺失 VM probe 会失败且补齐后四个 probe 全执行。脚本矩阵 14 profiles 通过，runner 三项测试通过。
- OBJECT 完整 configure/build/smoke 4221 exit 0；SHARED 34490 exit 0，目录 /tmp/evengine-bake-manifest{,-shared}/physics-bake-script-only；日志 /tmp/evengine-bake-script-{configure,build,smoke}.log 和 /tmp/evengine-bake-script-shared-{configure,build,smoke}.log。
- 实际 enabled modules 为 common;physics_bake;physics_bake_collapse;physics_bake_script；OBJECT compile_commands 21 个模块 TU（20 原生加 PhysicsBake.cpp），没有录制/实时物理/粒子模拟/图形实现。OBJECT ldd 仅系统库；SHARED 加本裁剪目录 EVFoundation/EVBackends/EVWorld，其中 World 仅承载本次选中的脚本绑定模块，不是实时物理世界。
- 这是脚本卫星 L1/L2/L3/L4 证据，非游戏 host、旧 CollapseAnimation 脚本注册、图形提交或其他平台验收。未修改公共 API/格式/运行时 owner。适用唯一清单、完整闭包证据、独立模块/可选组合和 Result 检查；无架构例外。
- 完整源码门禁 76533 运行中，日志 /tmp/evengine-bake-script-check.log，终态待收取。整体目标继续。

- 完整源码门禁 76533 exit 0，464 项脚本测试通过（70.715 秒），source-quality 通过。本轮所有构建/VM smoke/gate 已终态。整体目标继续，图形裁剪及跨平台耐久性等仍为待办。

图形播放完整客户端裁剪配置：

- 当前 minimal 清单本身无实时物理/粒子。新增 physics-bake-render 作为 minimal 客户端选择加五个 bake 模块，不强行把 Graphics 标成 hostless，也不手工抽取渲染源。解析得到 56 模块；第三方无 box2d/box3d/jolt。
- 新 cmake/physics_bake_render_check 链接完整 EVE_LINK_TARGETS、宿主资源与平台库，复用现有原生变形、脚本变形、脚本粒子和脚本刚体表现用例；CTest 按进程隔离，必须发现测试，且要求 zeroerr 明确 1 通过/0 warning/0 failed/0 skipped。没有新生产 API 或模块边界。
- profile matrix 新增禁止实时生产者/solver 依赖的检查；smoke runner 新增 CTest 调用，单测验证 --no-tests=error 和专属选择表达式。15 profiles 和 runner 4 项单测通过。
- OBJECT configure 已完成，build handle 10909 运行中，日志 /tmp/evengine-bake-render-{configure,build}.log；完整源码门禁 2889 运行中，日志 /tmp/evengine-bake-render-check.log。尚未声明图形裁剪链接或运行成功。
- 适用单一模块清单、完整链接/组合测试证据、Result 和现有资源生命周期合同，无架构例外。实现文档明确此配置需真实窗口后端，软件 Vulkan 不作独显性能证据。

图形裁剪 OBJECT 实际链接与运行：

- 首次完整链接 10909 exit 2，真实暴露 minimal 缺少客户端直接引用的 DevTool/DebugAdapter/LanguageServer 与 Font/FontData 符号；未删除对象或开启 GC 掩盖。新配置明确加入 devtools/font，闭包改为 58 模块，仍无实时物理和粒子模拟。minimal 全局选择本身未改动。
- 修正后完整构建 77715 exit 0；软件 Vulkan/Xvfb 的 profile/smoke 56127 exit 0，四个进程隔离 CTest 全过（6.45 秒）。日志 /tmp/evengine-bake-render-rebuild.log、/tmp/evengine-bake-render-smoke.log。
- 实际 compile_commands 共 476 个模块 TU，其中 25 个 physics TU 全在 bake 子树，无 particles 源；nm 未发现 World/Body/Cloth3D/Rope3D/ParticleEmitter 的定义符号。完整链接行保存 /tmp/evengine-bake-render-link.txt，包含 EVCmdLine 与 EVPhysicsBakeGraphics 对象、无 section GC。ldd 无 solver 库。
- 这是 Linux OBJECT L1/L2/L3/L4 图形组合证据，不推导 SHARED、旧 destruction 图形卫星或其他平台。没有生产 API/格式/状态 owner 改动，无架构例外。
- 首轮源码门禁 2889 exit 0（465 tests），补齐 font/devtools 后最终门禁 92409 尚在运行，日志 /tmp/evengine-bake-render-final-check.log，需收取终态。

- 最终源码门禁 92409 exit 0，465 项脚本测试通过（70.739 秒），source-quality 通过。本轮所有构建/窗口 smoke/gate 已终态。整体目标继续，SHARED 图形配置和跨平台等仍待验证。


图形裁剪 SHARED 验收：

- configure/build 75784 exit 0，目录 /tmp/evengine-bake-manifest-shared/physics-bake-render，日志 /tmp/evengine-bake-render-shared-{configure,build}.log。
- 软件 Vulkan/Xvfb profile/smoke 24874 exit 0，四个独立 CTest 4/4 通过（6.34 秒），日志 /tmp/evengine-bake-render-shared-smoke.log。验证与 OBJECT 同一原生变形生命周期、脚本变形、脚本粒子批处理、脚本刚体目标提交，不是只有库文件存在。
- 实际 compile_commands 同为 476 模块 TU，25 个 physics TU 全属于 bake，无 particles 模拟源。ldd 指向此裁剪目录 EVFoundation/EVPlatform/EVBackends/EVWorld 与 ImGui，其他为 SDL/Vulkan 等允许依赖；日志 /tmp/evengine-bake-render-shared-ldd.log。未加载完整主机构建的领域库。
- 更新验收摘要和用户文档，区分已有 Linux 原生/VM/图形 OBJECT/SHARED 证据与未完成的跨平台耐久性/资源合同。当前未发现 Windows 交叉编译器或 Wine，不能声称 Windows 执行通过。
- 本轮只新增验证证据和文档，无源代码、API、格式或模块配置修改；沿用上一轮对同一源代码通过的完整 gate 92409（465 tests），不重复运行无变化的源码门禁。git diff --check 通过，无架构例外。所有本轮构建/测试已终态，整体目标继续。


Graphics owner-first 真实退役与替换：

- 新 graphics_owner_retirement.cpp 针对三角/线段表现，各创建真实 headless Graphics 与 GPU mesh、上传、销毁整个 owner、CPU seek/sample、创建替换 owner 和 mesh 再上传，最终再次销毁 owner 后 CPU 采样与清理。无悬空 facade 调用；测试仅承诺顺序生命周期。
- OBJECT 首次 smoke 31737 exit 2，新用例复现 Vulkan instance warmup already consumed，其他四例通过。定位为 consumeWarmedInstance 对全局预热结果只允许一次消费，而后续 Graphics 初始化直接抛错。不是 GPU/驱动推断。
- 修复保持一次性异步启动预热；未消费时移动 future，已消费时在互斥锁外调用既有 createInstanceFast，为后续 owner 创建新的独占实例。未复用旧句柄，未增加公共 API/格式/全局第二状态源，未持锁调用后端。
- 修复后 OBJECT rebuild 59950 exit 0、smoke 19996 exit 0（5/5，11.39 秒）；SHARED rebuild 97973 exit 0、smoke 59406 exit 0（5/5，11.07 秒）。日志 /tmp/evengine-bake-owner-object-{rebuild,final-smoke}.log、/tmp/evengine-bake-owner-shared-{rebuild,smoke}.log。
- 本机与 SDK 目录均无 Khronos validation layer，未声称验证层无错误；软件 Vulkan 功能通过不代表独显性能或其他平台。这里是完整裁剪模块下的实际 GPU 资源生命周期验证，不替代纹理/材质等其他借用资源合同。
- 首轮源码门禁 54145 exit 0（465 tests），但生产修复发生在其格式检查之后；最终代码重新运行 gate 35021，日志 /tmp/evengine-bake-owner-final-check.log，终态待收取。适用唯一所有权、锁外后端调用、显式生命周期和完整组合测试；无架构例外。

- 最终源码门禁 35021 exit 0，完整 make check 通过；git diff --check 通过。本轮 OBJECT/SHARED 构建、五项表现测试和最终源码门禁均已终态。整体目标继续，下一优先项为真实碎块碰撞/高速旋转误差、其余借用资源生命周期及跨平台持久化耐久性。

碎块降采样误差诊断与旋转混叠回归：

- 新独立 collapse_error 工具使用生产 sampler/capture，对给定参考时间网格测量 stride 1/2/4/8 的位置/角度最大值与 RMS、状态不一致数、峰值骨与时间；不更改生产 API、文件格式或模块边界。
- 真实 cottage 资产参考测量显示 stride 2 最大位置误差 1.49598 m（RMS 0.0036666 m），325 状态样本不同；峰值骨 427、7.98333 秒。stride 8 最大角差达到 pi。不能据平均误差批准碎块降采样，当前示例仍逐步捕获。
- 新解析每秒一圈回归验证密采样精度与端点采样丢失整圈的反例；不将它当作实时碰撞对照。纯运行时 CTest 25/25 通过，日志 /tmp/evengine-collapse-error-tests.log。
- 详细结果写入 resampling-error 报告；实际碰撞峰值成因与更高频实时参考仍待验证。无公共 API/持久格式改动，无架构例外；源码门禁终态另记。
- 诊断工具最终构建/真实资产测量 95087 exit 0；原始纯运行时测试与首轮测量 36689 exit 0。显式 NaN 频率输入被拒绝（exit 1）；两份变更 C++ 的 clang-format --dry-run --Werror 与 git diff --check 均通过。
- 完整 make check 会话 20188 当前仍存活，日志 /tmp/evengine-collapse-error-check.log，最近停留在绑定指南覆盖阶段；尚未收取终态，不声明门禁通过。工具峰值定位输出在该轮格式阶段后补充，最终单独格式检查通过；必要时收取终态后补跑受影响检查。

碎块峰值定位和真实碰撞对照推进：

- 上轮 gate 20188 已收取 exit 0，465 tests（70.693 秒），完整源码门禁通过。
- 工具新增峰值邻域输出，build/measure 91577 exit 0。实际骨 427 在 7.98333→8 秒从 y=3.01774 到 0.249548，状态都为 Detached。示例有 8 秒冲击，但尚不声称原因是碰撞/冲击；采样器把已有输入大位移摊入更早区间可由代码和数据直接解释。
- 新真实碰撞参考用例使用 GeometryCollectionInstance 两个拆散碎块、静态地面、240 Hz 外部步长、2 秒、8 rad/s 初始旋转，捕获 stride 1/2/4/8；记录独立 live pose，源销毁后比较所有时间点。验证实际下落并受地面约束；保留帧必须位置/角度精确，其他帧报告误差，不虚构通用预算。
- 首次配置暴露 test-level 缺少显式 case 分类，已补齐并生成覆盖目录。宿主重新构建 handle 78297，完整 gate 46436，均待收取。日志 /tmp/evengine-collapse-collision-{build,check}.log。未改公共 API/持久格式，无架构例外。
- 宿主构建 78297 exit 0，明确重新编译 collapse_collision_reference.cpp；真实对照 CTest 1/1 通过，日志 /tmp/evengine-collapse-collision-tests.log。240 Hz 原时刻位置误差为零；120/60/30 Hz 最大误差分别 .012456/.0185591/.0570586 m。所有保留帧与状态断言通过，世界在重放前销毁。
- 实时碰撞对照从缺证据推进为受控两碎块落地夹具已验证；房屋原始突变根因、高速极限和其他平台仍未因此完成。报告补充边界和可复现方法。
- 最终源码门禁 46436 exit 0，465 tests（70.772 秒）、source-quality 全通过；git diff --check 通过。本轮全部构建/测量/测试/gate 已终态，无遗留运行会话。整体目标仍 active，下一步按剩余生命周期与跨平台耐久性验收继续。

粒子材质替换生命周期回归：

- 审核 ParticleBakePresentation：只在 draw 调用内借用 DrawItem2D，CPU items 用作用域清理退出，底层图形资源仍必须活到帧完成；没有持久材质链接。明确不接受已释放的原始引用。
- 扩展 scriptParticlePresentationBatchesMaterialsAndRejectsPartialDraw：前一帧读回后先清空全部 VM 材质别名、GC，再 native DestroyEntity 两个旧材质并创建替代；原播放器 seek(.5)，重新传新材质，像素应从红变绿且提交数为 2。之后销毁播放器，新材质仍可 setLayer，覆盖另一销毁顺序。
- OBJECT 裁剪构建 75008 exit 0，窗口 profile/smoke 86502 exit 0，5/5（11.74 秒）。日志 /tmp/evengine-bake-material-{build,smoke}.log。这里无自定义纹理，不把 ECS 材质替换声称为底层 texture/shader/canvas 退役安全。
- SHARED 构建 37332 与完整源码门禁 8402 仍待收取，日志 /tmp/evengine-bake-material-shared-build.log、/tmp/evengine-bake-material-check.log。本轮无生产 API、格式、模块边界修改，无架构例外。
- SHARED 构建 37332 exit 0、smoke 97338 exit 0，5/5（11.22 秒）；日志 /tmp/evengine-bake-material-shared-{build,smoke}.log。最终 gate 8402 exit 0，465 tests（71.242 秒），source-quality 与 git diff --check 通过。本轮全部会话终态；材质对象替换已获双链接配置证据，纹理/着色器等底层借用资源和跨平台耐久性仍待完成。

粒子纹理显式退役与替换：

- 在既有脚本粒子表现回归中加入真实 1×1 纹理的白→黑→白替换，保持同一播放器和材质存活，以绿色→黑色→绿色像素断言检测新绑定。
- 每次清除两材质绑定、checked releaseTexture、确认 gpuHandle=null、delete 已转移 facade，再创建绑定新纹理。核对 Vulkan releaseTexture 内部 waitForSharedGpuResources 后注销和释放；没有直接删除仍由 Graphics 拥有的对象，也无 VM 纹理别名。
- 适用调用期借用、GPU 在途资源存活与两阶段释放合同，无生产 API/格式/模块修改，无架构例外。自定义 shader/camera/canvas 与其他后端仍不在此证据范围。
- OBJECT build 20520 exit 0、smoke 30728 exit 0（5/5，11.31 秒）；SHARED build 94558 exit 0、smoke 94522 exit 0（5/5，11.22 秒）。日志 /tmp/evengine-bake-texture-{build,smoke}.log 和 /tmp/evengine-bake-texture-shared-{build,smoke}.log。像素验证来自引擎读回，软件 Vulkan 不用于独显性能结论。
- 最终源码门禁 86858 exit 0，465 tests（70.965 秒），source-quality 与 git diff --check 通过。本轮全部构建、窗口测试和 gate 已终态。纹理显式释放后替换重放已有证据，整体目标仍继续，尚未宣布跨平台或所有借用资源验收完成。

多进程缓存发布竞争验证：

- 重新核对 BakedAssetFile：Linux 使用匿名 staging、数据 fsync、无覆盖 linkat、目录 fsync；非 Linux 当前仍只原子可见。未把 Linux 测试当作其他平台验收，也未在缺少平台执行证据时新增未经验证的后端实现。
- durability.cpp 新增 8 个 fork 子进程经 pipe 起跑屏障竞争同一路径；每个写不同的 64 KiB 字节串，要求一个成功、七个 Conflict，并逐字节核对最终文件等于获胜进程的数据。没有修改生产 API/格式或发布算法。
- 构建/纯运行时 CTest 92298 exit 0，25/25 通过，日志 /tmp/evengine-bake-publish-race-{build,tests}.log。额外连续 20 轮 durability 探针全部通过（每轮含 8 进程竞争、既有故障和 SIGKILL 注入），日志 /tmp/evengine-bake-publish-race-stress.log。
- 适用不可覆盖发布、跨进程完整性与失败隔离要求，无架构例外。此测试不模拟物理掉电，不声明非 Linux 耐久性；完整 gate 94923 待终态，日志 /tmp/evengine-bake-publish-race-check.log。

总览与使用文档一致性审查：

- 修正用户文档中“粒子录制脚本待完成”“刚体尚未接表现/配方”“播放 VM 完整裁剪待验证”等过时描述，引用当前实际卫星和 Linux OBJECT/SHARED 证据，未扩大至其他平台。
- physics_bake 总览集中四通道入口、六步录制/发布/重放流程、NotFound 与冲突/损坏的区别、published 失败恢复、资源退役合同、误差报告和明确不支持能力。module-interface 原生面更新为实际轨道/录制/播放家族，VM 仍归独立卫星。
- 顶部验收摘要同步真实落地对照、材质/纹理/Graphics owner 和多进程发布证据；M1–M8 原范围未缩减、未整体勾选。无代码/API/格式修改，无架构例外。
- 上轮 gate 94923 exit 2：test_module_coverage 生成证据过期，未发生生产代码/领域测试失败；已运行 scripts/test_module_coverage.py --write 更新。完整新 gate 11362 运行中，日志 /tmp/evengine-bake-audit-check.log，须收取终态。四份修改用户文档的相对文件链接全部存在，git diff --check 通过。

真实碎块输入隔离与冻结资产生命周期：

- 审核确认 CollapseCaptureSource 合同要求返回几何在共享期间不可变；捕获不防御违反该协议的外部可变别名。真实 GeometryCollectionInstance::createFractured 会复制调用者几何，符合协议。
- 扩展已有 physics_voronoi.meshBakeSurvivesWorldAndDisk：实例创建后清空原始 geometry/collection；录制完成销毁世界；准备两个播放器后清空原始 clip。两个播放器独立采样成功，从播放器保有的资产保存/加载后 positions/indices/materials 与原始快照完全一致。
- 宿主构建 92736 exit 0，日志明确重新编译 physics_voronoi_fracture.cpp；CTest 1/1 通过（无 warning/failed/skipped），日志 /tmp/evengine-bake-freeze-{build,tests}.log。
- 完整 gate 11362 exit 0，465 tests（72.602 秒），此前过期覆盖目录问题已解除。本轮测试修改发生在该 gate 格式阶段之后，单独 clang-format --dry-run --Werror、覆盖目录检查及 git diff --check 补验通过；无公共 API/持久格式/生产行为变更，无架构例外。

三平台原生纯播放 CI 接入：

- 当前工作区不存在 .local-debug 调试入口；PATH 无 MinGW/Wine，未假称可本机执行 Windows 测试。
- 主 CI 新增 physics-bake-codec，依赖 source-quality，Ubuntu/Windows/macOS 三 lane 独立配置、构建 Debug、运行纯播放 CTest；--no-tests=error 且排除 bundle，不吞掉缺失或失败用例。仅初始化 zeroerr 子模块，不要求 solver/Vulkan/窗口；上传各平台配置与测试诊断。
- 这是持续平台验收入口，不等于已获 Windows/macOS 通过证据；Linux syscall 故障注入仍只在 Linux，其他平台运行其真实 portable 发布路径。静态独立 lane 不证明 Windows DLL 导出或非 Linux 落盘耐久性。
- workflow YAML 解析及三 lane/缺失测试检查通过，git diff --check 通过。全新 Linux 构建/测试 handle 41156（/tmp/evengine-bake-ci-{configure,build,tests}.log），完整源码门禁 70962（/tmp/evengine-bake-ci-check.log）待收取。无 API/格式/生产行为改动，无架构例外。
- 全新目录 Linux configure/build/test 41156 exit 0，纯播放 CTest 25/25 通过；此结果不替代尚未运行的 Windows/macOS 托管 lane。完整源码门禁 70962 继续等待终态。

碎块播放器准备分配失败：

- 独立 allocation 探针重载本可执行文件的分配函数，仅注入下一次分配失败、随后允许诊断分配。修复前 CTest 68418 exit 8，真实输出 std::bad_alloc，确认共享资产准备绕过 checked Result。
- CollapsePlayback::prepare 捕获内部 bad_alloc 并返回 Failed 诊断；不改变公共签名、布局、格式或现存播放器。文档明确 move 输入即使失败也消费，调用前 by-value 拷贝和诊断自身分配失败仍可能抛异常，不宣称进程持续 OOM 可恢复。
- 回归要求失败返回 Result、旧播放器时间/采样不变、原始 clip 不变、后续重试成功。独立可执行文件不改变生产分配器，无测试开关或模块依赖；已注册测试来源并更新覆盖证据。
- 适用 Result 错误边界、唯一所有权和失败隔离规则，无架构例外。纯播放构建/全套结果记录于 /tmp/evengine-bake-alloc-{build,tests}.log，旧 CI gate 70962 尚待收取；生产修复后需重新完整门禁。
- 修复后构建/测试 19267 exit 0，纯播放 26/26，通过故障返回、旧实例不变及重试断言。旧 gate 70962 exit 0（465 tests，70.991 秒），但不拿它代替修复后的全量检查；新最终门禁已启动，日志 /tmp/evengine-bake-alloc-check.log。

Release 失败合同及真实资产复验：

- 全新 /tmp/evengine-bake-final-release 配置 GCC Release，configure/build/CTest 12992 exit 0，26/26 通过；包含单次分配故障、原实例不变/重试、Linux 屏障与 8 进程发布竞争。日志 /tmp/evengine-bake-final-release-{configure,build,tests}.log。
- 单独运行 Release allocation/durability 程序均 exit 0，输出 checked allocation failure 与 1 winner/7 conflicts/65536 verified bytes；不是仅依赖 Debug 断言通过。
- Release inspector 加载真实 cottage 资产（507 骨、961 帧、16 秒、25631534 bytes），双实例 121680 姿态，checksum-like pose_sum 923788，exit 0；日志 /tmp/evengine-bake-final-release-asset.log。单次 sampling_ms=15.337 只作为本次执行记录，不替代已有多次性能报告，不含 GPU/IO 加速结论。
- CI codec 矩阵扩展为三平台 × Debug/Release 共六 lane，配置/构建/CTest 参数及 artifact 名均使用 build_type，YAML 解析和轴数量检查通过。托管运行尚未发生；无生产/API/格式修改，无架构例外。最终源码 gate 22889 仍待收取，日志 /tmp/evengine-bake-alloc-check.log。
- 最终分配修复源码门禁 22889 已收取 exit 0，465 tests（70.713 秒），source-quality 通过。Debug/Release 纯播放各 26/26 结果与最终门禁均已终态，无遗留构建/测试会话。
- 当前为 detached HEAD 本地工作区，所有实现仍未提交；origin 为 EVEngine/EVEngine，GitHub 登录可用。新增三平台双配置 workflow 尚无托管执行证据，不把本地配置或登录状态当成平台通过。跨平台落盘耐久性、宿主 DLL 和剩余资源组合仍未完成。

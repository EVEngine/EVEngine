# Provider 生命周期收敛与 WebGPU 后端职责拆分

日期：2026-11-11

## 范围与唯一真源

本次覆盖依赖装配、Provider 发布机制、边界取证器及 WebGPU 内部组织。
不改变 ECS 模型、游戏时间、RNG、存档格式或渲染算法。

`common/ProviderRegistry.h` 是实例级发布机制的唯一实现：id 唯一、代际递增、
精确代际撤销、持有记录和派发快照。进程能力注册表的实例仍由 foundation DLL
中的 `CapabilityOwned.cpp` 唯一拥有，不能在各个 DLL 内形成模板静态副本。
脚本、编辑扩展和场景 payload 的注册表分别由其宿主实例拥有。

共享的是发布与持有机制；脚本优先级、编辑扩展 activate/beginUnload/deactivate、
场景 componentType 路由继续属于各自领域。固定生命周期的借用能力继续使用原有
`cap::provide/query` 路径，不把借用服务伪装成 shared_ptr 所有权。

## module-interface：本次改动的六个面

| 模块 | 能力与依赖 | 数据、事件、脚本、协议 |
| --- | --- | --- |
| common | 提供实例级 ProviderRegistry；全局 owned capability 委托同一机制 | Record 拥有 shared_ptr 与 metadata；handle 仅在原 registry 内解释，不持久化；无新增事件、脚本或工具协议 |
| audio | Audio 构造成功后发布 query、waveform、parameter、notify、preview；能力实现注入同一 AudioBinding | Audio 唯一拥有注册束；绑定无进程静态状态；保留既有脚本接口与 action 协议 |
| graphics | Graphics 构造时解析固定进程生命周期的可选 IRayTracing；渲染阶段仅借用缓存 | 缺席进入既有 SSR 路径；无新增脚本绑定、事件或存档字段；Graphics 独占后端资源 |
| editing | ExtensionProviderRegistry 委托 common 机制；领域层保留生命周期回调与 descriptor | 每 id 同时最多一个生命周期转换；同 id 重入返回 Conflict；租约保持旧实现存活 |
| scene_editing / scene_editor | payload registry 持有 shared_ptr；SceneEditorModule 共享其真实 provider 所有权 | 查询在持有快照下调用 provider；原 property/operation 返回值仍是当前同步调用的借用，撤销后不能保存使用；无持久化变更 |

线程与重入：common 机制仅在复制/移动持有记录时持锁，不在锁内调用 provider。
编辑扩展按 descriptor 指定的线程执行回调，registry 销毁要求调用方静止。
脚本 resolver 保持 VM 所在线程亲和；priority 高者优先，同优先级按注册序。
场景 payload 与 audio 仍为主线程使用。音频关闭先撤销注册、清理仍存活客户端
的 Source，再使绑定失效，最后停止 worker 并释放 OpenAL context。

## 连接机制的四轴推导

| 交换 | 基数 | 频率 | 关注者可知性 | 原子性与选择 |
| --- | --- | --- | --- | --- |
| Audio 能力到 Audio | 单一宿主 | 查询/参数/动作 | 装配时已知 | 注入宿主绑定；销毁使保留客户端显式不可用 |
| 渲染到 IRayTracing | 零或一个固定服务 | 每帧 | 构造时可知 | 构造时绑定 ProviderRef；热路径无字符串查询 |
| 脚本到 source providers | 多个，有优先级 | 编译/import | 开放扩展点 | 每次派发持有快照；回调增删影响下一次派发 |
| 场景到 payload providers | 每 componentType 一个 | inspector 查询 | 开放扩展点 | 持有快照；同类型重复发布拒绝 |
| editor 到扩展 | 每 id 一个 | 非热路径 | generation handle | 预留 id、锁外生命周期回调、发布或撤销；拒绝卸载保留原代际 |

租约只延长实现对象的内存生命，不重启已经停止的领域服务。撤销后新 acquire
失败，已取得的旧记录仍拥有旧实现，不能通过旧 handle 访问替代代际。

## WebGPU 内部边界

| 实现文件 | 职责 |
| --- | --- |
| Graphics.cpp | 设备、surface、默认资源初始化与帧呈现 |
| GraphicsPipelines.cpp | 内置 bind-group layout 与 pipeline 构造/缓存 |
| GraphicsTextures.cpp | 纹理创建、上传、更新、cubemap 与资源绑定 |
| GraphicsMeshes.cpp | mesh 创建、顶点布局使用、形变上传与释放 |
| GraphicsDraw.cpp | 帧内 arena、绘制收集与 Canvas API |
| GraphicsPasses.cpp | render target 生命周期、shadow/G-buffer/decal pass 编码与提交 |
| GraphicsShaders.cpp | 用户 shader 校验及 pipeline 准备 |
| GraphicsPipelineSupport.cpp | 各实现单元共享的内部 descriptor/layout helper |

这些文件属于原 graphics 模块，source discovery 由原 manifest 派生，未增加模块、
第二份 boot list 或第二个 GPU 状态 owner。原 Graphics 的资源容器和 frame-slot
状态保持唯一权威；拆分按完整函数移动，统一 helper 没有复制实现。

## 取证器与报告格式

module_boundary_audit 复用构建门禁的 manifest 文件清单和目录解析，覆盖宿主片段，
避免只统计顶层声明。扫描信号标 CANDIDATE，缺少运行证据标 UNVERIFIED；
不因已删除的 catalogue 产生缺项结论，也不以 include 图证明链接可裁剪性。
JSON v2 的 schema、未知字段及 v1 迁移规则见《模块边界审查清单》。

## 验收边界

相关测试覆盖：Provider 缺席、空实现、重复发布、撤销后旧代际拒绝、旧租约保持、
registry 先销毁、provider 析构重入；脚本回调中自行撤销；音频绑定实际宿主、撤销、
保留 preview 在宿主销毁后返回 NotFound；原 editor 拒绝卸载与场景 transaction/undo
用例继续作为组合契约。

L1/L2/L3/L4 分别取证，不从依赖图或源扫描宣称完整可裁剪。
全平台全套测试遵循仓库约定交由 CI。没有新增架构规则例外。

## 已验证结果

| 项目 | 结果 |
| --- | --- |
| `make check`，base `origin/dev` | 全部通过；包括架构、分层、bindings、测试归属、格式和脚本测试 |
| Windows x64 / MSVC，WebGPU / OBJECT | 严格编译并链接 `eve.exe` 与单体 `unit_test.exe`；`EVENGINE_TEST_DOMAIN_SPLIT=OFF` |
| 定向 CTest | 65/65 通过，不启用自动重试；包含真实纹理/mesh/PBR/2D 渲染回读、WGSL 生命周期、帧捕获以及本次 Provider/Audio/Script/Editor 契约 |
| Windows x64 / WebGPU / SHARED | 严格编译链接 `eve.exe`、`unit_test_ui.exe`、`unit_test_graphics.exe`；最终 140/140 项相关定向测试通过（161.52 秒，无自动重试），独立 PhotoMode 雾效失败详见下文 |
| headless / SHARED | configure、编译、Foundation DLL 链接完成；独立消费方实际启动，验证音频提供者缺席、跨 DLL 发布、旧代际拒绝及旧租约保持 |
| 边界取证 | 宿主片段修复后扫描 203 个模块，原顶层扫描仅 85 个；4 个取证器回归测试通过 |
| 搬移核对 | 原 WebGPU 文件 189 个方法完整保留，无遗漏、无重复；仅四个方法含有显式整数转换/未使用变量清理 |

验证依赖使用与引擎 pin 匹配的 third-party v0.1.2 发布包。
构建通过仓库 Makefile 执行，末轮增量使用其等价的
`cmake/with-msvc.cmd cmake --build build/win32-debug --parallel 8`。
定向测试使用 Makefile 相同的 `scripts/run_ctest.py --windows hidden -- ctest` 配方，
按上述契约名称筛选，排除 bundle，保持每例进程隔离。

完整构建同时修复了 WebGPU ImGui 字体上传 stride 的有界窄化转换，以及测试 TU
误引入 Vulkan 私有类的问题：VkPipeline 断言仍归 Vulkan 构建，通用 shader 测试和
WebGPU shader 生命周期测试保留，没有添加运行期 skip 或放宽断言。

WebGPU SHARED 的后续修复将 ImGui core/SDL 的源码列表统一，并仅在 Vulkan 后端
选择 Vulkan renderer 和 loader。引擎自有 WebGPU renderer 的声明不再使用
`eve_imgui` 的 DLL 导入宏；它的实现和调用方都由 EVUI 持有，保持既有 ImGui 后端
兼容签名。ImGui context 仍由单个共享库持有，没有引入第二份全局状态。
同时在 FileSystem 的 BuildSettings 中补上实际使用的 WebGPU 平台路径库依赖，
使 Foundation DLL 自身解析路径函数，不依赖宿主可执行文件替它解析。
补齐 WebGPU compute、InitFlow 和 PBR variant 的既有跨 DLL 消费接口导出；
没有改变其签名或调用语义。
配置回归测试覆盖 WebGPU/Vulkan 与 SHARED/OBJECT 四种组合，检查源码、loader
依赖和 FileSystem 平台库依赖。没有修改第三方实现或新增公共 API。

ImGui adapter 的真实 GPU 回读还修复了入口名字节长度、sampler 有效默认值、
multisample mask、16 位索引上传的四字节对齐、字体绑定恢复和 device 引用计数配对。
原来失败的嵌入纹理、纹理桥接、绘制顺序/裁剪三例均通过；两例 overlay/面板 smoke
也通过。完整 SHARED `eve`、`unit_test_ui`、`unit_test_graphics` 已编译链接成功。

首次扩展运行 141 例时，发现独立的 PhotoMode 雾效 shader 错误：
`ui.pcgPhotoModeApply.lightingFogAuthorityDrivesFogAndDensityVolume` 在
`graphics/shaders/VolumetricWgsl.h` 的 `acc.rgb += ...` 上被 Dawn 拒绝。
该项没有通过，本次保留失败记录，不修改雾效实现或添加代码级 skip；后续相关测试
筛选明确排除此例。最终 `make check` 的 nodiscard 五项编译失败契约均实际运行并通过，
没有依赖找不到编译器时的跳过分支。浏览器、Vulkan 完整构建及其他平台 CI 未在本次本机验证中执行。

## 适用规则取证

- R-COHESION-1/3/4：发布身份、代际失效、持有及撤销由 common 唯一拥有；脚本优先级、编辑扩展生命周期与场景路由策略分别保留在原宿主。
- R-MECH-1/2/3：按四轴选择注入或持有快照；Audio 能力实现不再查找 ModuleManager；RenderSystem3D 热路径改为读取构造时绑定的服务。
- R-BOUND-1/3/4：分层门禁通过；L2/L3/L4 的缺席证据来自真实 headless 构建、链接和消费方启动，不从 include 图推断。
- R-FACE-1/2/5：本文件列出本次模块的六个面；common 仅包含发布机制，不依赖编辑、场景或图形策略；可选 RT 提供者存在/缺席均有组合测试。
- R-COST-1/4：新快照 API 标明复制、排序及摊销成本；每帧服务读取保持常量时间，不增加 builder/token 仪式。
- Result 与所有权规范：新增发布/撤销操作返回 nodiscard Result；真实共享所有权支撑跨撤销持有，关闭与析构不在持锁期间调用领域回调。没有新增规则例外。

## PR 同步验证（2026-11-12）

分支重放到 `origin/dev` 的 `7e09d026c0249a638958af4ffe8e2042d2a6977f`，
保留其新增 PBR GBuffer、RGBA16Float emissive 和深度比较逻辑。
相对该基线再次审计仍为 189/189 个方法，无遗漏或新增；四处既有窄化/清理差异不变。
Windows x64 WebGPU SHARED 的 eve、graphics/ui/core/audio 测试程序全部重新编译链接；
150/150 项 UI/WebGPU/脚本回归通过（155.27 秒），另 6/6 项 Provider/Audio 生命周期
契约通过，均无自动重试。PhotoMode 雾效独立失败仍按上文明确排除。
最终 `make check`（包含实际 nodiscard 诊断）和 `git diff --check` 通过。

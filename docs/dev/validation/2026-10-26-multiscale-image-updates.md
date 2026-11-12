# 多尺度图像数据更新：接口与验收记录

状态：同步图像更新已接入批次、脚本和原生示例；局部构建/测试通过，整帧目标未通过。

## 接口与兼容边界

新增 graphics 所有的 ShaderImageUpdate：prepare 复制、验证一张完整图像，返回 owning token；
apply 在 graphics 线程、提交之外应用到一个显式借用的活 shader。token 不保留 provider、shader、
输入指针或回调。它不修改程序、布局、sampler、矩阵缓冲或描述符，只更新原有图像数据。
不支持的 provider 明确返回 Unsupported；Vulkan 实现等待旧帧完成后进行 staging 与图像屏障。
共享图像或仍有不可变内容身份的图像拒绝写入。布局/容量/借用状态不匹配在写入之前失败。
设备或队列提交失败需要主机恢复，不承诺已提交 GPU 写入的回滚。

SphereBatch 增加 update(SphereFrame const&)：复用初次准备的 max(1, visibleCount) 容量，
可减到零并在容量内恢复；仅成功后发布 draw count。SphereFrame 已是昂贵准备阶段的 owning 结果，
不引入另一套模型所有权或持久格式。脚本批次 update 使用同一 Result 投影与严格 native typetag 检查。
原生示例改为两个 CPU frame、一个 GPU batch，四秒切换时显式更新。

四轴机制：一个显式 shader owner、高频定向调用、调用者知道接收者、无需事件广播；
采用模块内 typed 调用，不新增全局 registry 或运行时字符串查询。
六面变化只有 C++ 资源接口与既有 multiscale 脚本面；没有新增 capability、ECS/Link、事件、工具协议。
旧 Graphics 虚接口、替换操作、默认模块开关与旧视图不改。后端私有 helper 通过 friend 获得窄访问，
不公开 GPU 容器，不改变类数据布局。R-MECH、R-COST、Result/ownership/thread 契约适用，无规则例外。

## 预定验收

- 原生更新后颜色正确、清空与恢复正确、超容量及活动 pass 拒绝后旧结果仍可绘制。
- 输入复制、moved token、形状错误、共享身份拒绝、provider-first 销毁安全。
- 脚本错误类型/null/错误接收者受控，正常更新后 input/frame 可 GC。
- 开启 Vulkan validation 的聚焦测试、严格 Debug SHARED 构建、Release OBJECT 构建。
- 示例标准 smoke 和引擎近/远截图对照。
- 相同 Release 配置的百万 update/rebuild 成本，独立报告而非整帧验收。
- make check 与 changed-line 格式检查。尚未执行的项目不标记通过。

该接口仍有每帧复制、分配 staging 和同步，尚不是异步流式上传；CPU 投影、场求解及完整场景问题仍待完成。
成本按保留容量计算，减少 draw count 不减少整张图像上传；后端另有 O(S) 的 provider shader
所有权查找（S 是当前 shader 数）。当前多尺度路径按视图批次调用，不能每个物体建立一个 shader
再逐物体更新后仍宣称总 O(N)。动态容量与高频 shader 查找优化尚不在本轮已验证范围。

## 已取得证据

严格 Debug SHARED 构建退出 0：build/multiscale-image-update-build-2.log、
multiscale-image-update-build-final.log。首次编译发现 backend helper 缺少私有访问授权，
通过窄 friend 声明修正；没有把 GPU 容器公开。头文件消费者已显式更新时间以强制重编译。
同一优化 Release OBJECT、minimal + font + multiscale 配置的 eve/render_benchmark 链接通过，
日志为 multiscale-image-update-release-build.log；Release 沿用标准警告策略，非严格 Release 宣称。

开启 EVENGINE_VULKAN_VALIDATION=1 的 graphics.multiscale 聚焦用例 9/9 通过，
multiscale-image-update-tests-final.log；LastTest.log 未发现 VUID/Validation Error。
测试包含实际颜色变化、空/非空切换、超容量与活动 pass 失败保持、输入释放、moved token、
形状错误、脚本错误类型/null/错误接收者、provider-first 销毁。
共享保护同时测试活身份拒绝，以及身份释放后两个 shader 仍共享时拒绝、退役一个后更新成功。
此处未模拟设备丢失，也未取得 WebGPU/provider-absent 构建证据。
另核对 .github/workflows/ci.yml 时发现 Linux 图形 job 未显式开启新模块却会 smoke 所有示例。
后续已通过 [CI 组合接线](2026-10-26-multiscale-ci-composition.md) 在该 job 显式启用并增加缺族失败检查，
保持引擎默认开关不变；本地显式启用的 smoke 仍不能证明远程 CI 已通过。

Release 标准 smoke 持续 12 秒通过：multiscale-image-update-smoke.log。
正常入口第 60/300 帧引擎截图 update-near.png / update-far.png 与 rounding 对应图片
RGB 差均为零，均为 1920x1080；结果在 build/multiscale-captures/update-image-comparison.json。
捕获日志为 multiscale-image-update-capture-near.log / -far.log。

## 百万移动记录：交错成本比较

AMD Radeon AI PRO R9700、1920x1080、VSync/validation 关闭、同一 Release 二进制；
每轮 4 个静态预热帧，16 个全量移动帧。顺序 update/rebuild/update/rebuild/update/rebuild，
每种模式三轮。每个输入仍有独立记录，没有减少移动对象或改成静态绘制。

- update 各轮 CPU 循环 P50：410.221、406.031、415.118 ms。
- rebuild 各轮 CPU 循环 P50：482.969、484.037、571.087 ms。
- update 各轮上传阶段 P50：51.7251、51.1862、61.9218 ms。
- rebuild 各轮上传/退役阶段 P50：129.271、128.979、108.401 ms。
- update CPU 准备 P50：350.475、347.867、348.934 ms；rebuild 为 346.3、347.967、456.997 ms。

最后一次 rebuild 的 CPU 准备明显波动，不能把循环差异全部归因于资源更新。
阶段中位数的中位数是上传 128.979 → 51.7251 ms、循环 484.037 → 410.221 ms；
这不是合并帧分布的百分位。16 样本下每轮 P95/P99 等于最大观测值，不能证明长期尾延迟。
GPU 队列 P50 为约 2.1–2.5 ms，仍是延迟采样，不含 CPU 准备。
所有结果 wholeWorldAcceptance=false：没有场求解、积分、生成、碰撞，仍远超 16.67 ms。

原始日志：build/multiscale-image-update-release-1000000.log（首轮 update），
multiscale-image-update-release-1000000-2.log / -3.log，
multiscale-image-rebuild-release-1000000-1.log / -2.log / -3.log。

## 源码门禁与交付边界

完整 make check（base=origin/dev）退出 0：build/multiscale-image-update-source-check.log，
529 项脚本测试、5 项既有平台跳过，未新增跳过。已有文件仅格式化修改行，新头文件补充容量和
shader 查找成本后通过 clang-format --dry-run --Werror，git diff --check 通过。
本轮符合显式昂贵准备、Result/nodiscard、所有权/线程约束、窄模块边界及默认行为保持规则，
没有架构规则例外。远程 CI、provider-absent 与完整百万状态整帧验收仍未完成。

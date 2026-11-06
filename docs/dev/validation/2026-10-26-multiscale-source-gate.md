# 多尺度实现：源码门禁恢复

日期：2026-10-26。工作树基线：5f2cd482ad1f88a7e11116000823a2b139c54a61。
本记录补充点引力适配记录中的失败结果，不覆盖历史失败证据。

## 修复范围

Windows 缓存脚本契约测试原先调用 PATH 上的 bash，可能选中 WSL launcher，
并把 Windows 反斜线路径直接交给 shell。测试现在显式定位 Git for Windows 的
原生 Bash，并把脚本路径、参数及相关环境变量转换为正斜线形式；POSIX 仍使用 bash。
没有放宽原有断言，也没有增加跳过项。

SDK 安装规则把第三方安装前缀嵌入 install(CODE)，原生反斜线可能被 CMake
作为转义重新解释。仅在 Windows host 上规范化分隔符，保留前缀解析来源和安装行为。
POSIX 路径不按搜索路径列表转换；测试覆盖包含空格以及冒号的依赖目录。
本次不改变运行时接口、模块生命周期、持久格式或旧游戏功能。

## 验证

Windows 使用 cmake/with-msvc.cmd 配置环境后执行 make check，工具 PATH 包含
本工作树 build/check-tools/bin 和 Git/bin，PYTHONPATH 指向 build/check-tools。
完整日志为 build/multiscale-source-gate-repaired.log。
结果：退出码 0，529 项脚本测试，5 项既有跳过，source-quality gate passed，
比较基线为 origin/dev。其中包含源码架构门禁；这不替代平台构建或运行时验收。

Windows 缓存脚本测试为 8 项通过（1 项既有跳过），SDK 架构测试为 7 项通过
（1 项既有跳过）。WSL Linux 对应测试分别为 8 项通过（1 项既有跳过）和 7 项通过。
Linux 测试前仅将三个 shell 工作树文件临时恢复成 Git 索引已有的 LF，测试后恢复
Windows 原有 CRLF，没有提交或保留 shell 内容变更。

最后将 SDK 分隔符处理收紧为 Windows host 专属后，Windows 和 WSL 再各自运行
test_source_install_uses_resolved_architecture_prefix，均退出 0。
Linux 这次明确使用包含冒号的依赖目录。

## 验收边界

适用规则为保持默认行为、源质量门禁和跨平台路径契约；无批准例外。
没有新公共运行时 API，也没有新增架构豁免、跳过项或第二份登记真源。
当前源码门禁已通过；完整 OBJECT/裁剪/平台 CI、可视化示例和百万独立状态
1080p、60 FPS（整帧 16.67 ms）仍未完成，不能由本记录推导为通过。

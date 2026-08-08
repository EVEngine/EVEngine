本开发工具提供了可视化的对象检测、修改、调试功能
可以方便连接外部编辑器、启动 language server


## 组件检测器

可以检测引擎内所有组件、用户定义的脚本等
可以查看游戏对象的属性、资源、依赖关系等等


## 脚本调试器 / 动态切片（Slicer）

对我们的脚本可以调试执行，设置断点等。

出错时可用类似 **program slicer** 的动态后向切片，定位「哪些代码导致了错误」：

1. **调用栈**：通过 Squirrel `sq_setnativedebughook` + `sq_stackinfos` 记录 Call/Return/Line
2. **数据流**：采样局部变量变更，建立 Def→Use 到达定值边；出错点再对相关变量记 Use
3. **后向切片**：从错误点（及可疑变量）沿数据依赖与控制前驱回溯，得到相关源码位置
4. **环形缓冲**：默认最多保留 10 万条事件；满员后每新增 1 条就丢掉最旧 1 条（可用 `setMaxEvents`）

### C++ API

- `eve::dev::CallGraph`：事件记录 + `sliceBackward()` / `formatErrorReport()`
- `eve::dev::DevTool`：挂到 VM（`attach`），出错时 `notifyError()` 输出报告

### 使用方式

```bash
eve run --debug .
```

脚本异常时 stderr 会打印：

- Error / Site
- Call stack
- Data flow（def → use）
- Relevant code（slice 中的源码位置）

桌面 Debug 构建链接 `EVDevTools`；Android / iOS 精简运行时不包含本模块。


## language server

可以创建自动提示功能等，方便代码编写

# Azure / Avatar Atelier

默认示例已接入可编辑分层母稿，使用真实 `eve.Avatar` 图层渲染。

运行：`make run/win32-debug GAME=examples/avatar-document-editor`。

- 左侧选择两种姿势、六种表情、三种发型，也可查看完整身体底稿。
- 右侧独立切换衬衣、上衣、裙子和银星饰品；支持不穿外套。
- 选择“学院”或“轻礼装”后，可切换“显示母稿完成图”检查重组效果。
- 目前的完成图来自重建母稿，不是最初概念图的逐像素复制。

母稿位于 `assets/azure-master/front.ora` 和 `greeting.ora`，各含 41 个绘制层。
编辑方式、导出命令、来源与验收边界见 `assets/azure-master/README.md`。

验证：使用现有 Windows Debug eve.exe 启动 `eve run --mcp-port 19385`，运行
`python verify_master_runtime.py`。需要 Pillow 和 numpy。864 种状态包含原要求的
576 种组合及“不穿外套”的额外状态；操作检查不等于逐一审美验收。
验证脚本在本地生成 `verification-master/report.json`，包含四组实际渲染与母稿完成图的像素误差。

原有事务文档编辑示例保存在 `document.nut`。设 `config.documentMode=true` 可进入，
它仍演示矩形图层的编辑器事务功能。仓库只包含可编辑母稿、最终素材、必要的
身体完整性基准和维护工具；生成源图、旧版素材、预览拼图与验证输出不提交。

本次未修改 C++ 引擎接口；运行验证使用 Workspace/Agents 的现有 Debug 二进制，
没有将整个引擎从当前 worktree 重新编译。架构源码契约与配套 fixtures 单独验证。

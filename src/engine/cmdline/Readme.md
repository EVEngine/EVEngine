Cmdline
=========

这是一个命令行工具模块，是PC-dev版的核心入口，可以执行诸多命令行指令，包括：
1. 创建新工程 create
2. 运行当前程序 run
3. 运行开发服务器 dev-server
4. 打包 zip/package
5. 下载安装平台 SDK get
6. 自动测试 test
7. 查看文档 doc

其中核心是执行、测试、创建、打包

执行逻辑(目录)：
1. 创建脚本VM
2. 查看当前文件夹下是否有启动脚本 boot.nut
    有： 加载
    没有： 加载默认boot脚本
3. 运行脚本

执行逻辑(压缩包)：
1. 加载压缩包内文件系统
2. 创建脚本VM
3. 查看当前文件夹下是否有启动脚本 boot.nut
    有： 加载
    没有： 加载默认boot脚本
4. 运行脚本


开发服务器逻辑：
1. 创建脚本VM
2. 查看当前文件夹下是否有启动脚本
    有： 加载
    没有： 加载默认boot脚本
3. 监视当前文件夹下的文件变动
4. 记录重启点
5. 重载文件夹下脚本变动
6. 运行脚本

测试逻辑：
1. 检测是否有测试配置 test.nut
2. 加载脚本，根据命令行参数执行对应测试
3. 测试除了自动单元测试，还可以执行测试场景，加载人物、地图和剧情

查看文档
根据当前版本，自动查找符号并打开在线文档

构建（build）：
`eve build <platform> [game-path]` 把当前游戏（或指定目录）构建到对应平台。
平台名：win32 / linux / macosx / android / ios；默认 release，加 `-d` 为 debug。
例如 `eve build android` 会调用仓库 Makefile 的 `build/android` 目标（cmake +
NDK + gradlew）产出 APK。构建根目录自动从 cwd 向上查找（含 Makefile +
CMakeLists.txt），也可以 `--sdk <dir>` 或 `$EVENGINE_SDK` 指定。
Windows 宿主上 linux 走 WSL2 目标；macosx/ios 需要 macOS 宿主；android 需要
先安装 Android SDK（见下）。

获取 SDK（get）：
`eve get android` 自动下载并安装 Android SDK 工具链：command-line tools、
platform-tools、platforms;android-34、build-tools;34.0.0、NDK 26.1 以及
JDK 17（Temurin），默认安装到 `%LOCALAPPDATA%/Android/Sdk`（可用
`EVENGINE_ANDROID_SDK` 或 `ANDROID_HOME` 覆盖）。完成后 `eve build android`
即可直接构建 APK。环境变量持久化文件为 `<sdk>/eve-android.env`。

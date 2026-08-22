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
平台名：android（其它平台待支持，桌面目标可用 `eve package`）；默认 release，
加 `-d` 为 debug。完全基于打包好的 SDK，不依赖源码、Makefile、cmake 或 NDK：
SDK 自带 android 的 APK 工程模板（`platform/apk`）、预编译 native 库
（`lib/*.so`）和演示游戏壳（`platform/game-shell`）。
流程：复制模板到输出目录（`-o`，默认 `<游戏目录>/build/eve-android`）→ 注入
游戏资源 → 拷入 .so → 写 `local.properties`（sdk.dir）→ 调用 Gradle
assembleRelease/assembleDebug 产出 APK。SDK 根目录按 `--sdk <dir>`、
`$EVENGINE_SDK`、`<sdk>/bin` 下的 eve 可执行文件位置依次查找。

获取 SDK（get）：
`eve get android` 分两步：
1. 按当前 eve 版本（`v0.1.0-dev` 归一化为 `v0.1.0`）从 GitHub Release 下载
   EVEngine 官方 android SDK（`eve-sdk-android-<tag>.zip`，附 SHA256SUMS 校验），
   解压到 `%LOCALAPPDATA%/EVEngine/sdk/android`（可用 `EVE_SDK_INSTALL_ROOT`
   覆盖；dev 构建可用 `EVE_SDK_TAG` 指定目标 tag，镜像/测试可用
   `EVE_SDK_BASE_URL` 覆盖下载基址）。
2. 安装通用 Android 工具链：command-line tools、platform-tools、
   platforms;android-34、build-tools;34.0.0、Gradle 8.5 以及 JDK 17（Temurin），
   默认安装到 `%LOCALAPPDATA%/Android/Sdk`（可用 `EVENGINE_ANDROID_SDK` 或
   `ANDROID_HOME` 覆盖）。

完成后 `eve build android` 即可直接构建 APK，不需要源码 / Makefile /
Android Studio / NDK / 任何系统工具。环境变量持久化文件为
`<sdk>/eve-android.env`（含 ANDROID_HOME / JAVA_HOME / GRADLE_HOME /
EVENGINE_SDK），`eve build` 会自动读取它来定位 android SDK。

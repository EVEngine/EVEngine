# Android APK 构建与签名

这篇解决什么：使用发布版 EVEngine SDK，把游戏目录构建为 APK，并签名为可安装的文件。自己试玩优先用 Debug；分发 Release 时使用自己保管的密钥。

## 1. 准备 SDK 和工具

使用与你的开发机匹配的桌面 SDK 中的 `eve`，安装 Android 目标 SDK 和工具链：

```sh
eve get android
```

Android 目标 SDK 包含预编译 `.so` 和 APK 模板，脚本游戏打包不需要编译引擎，也不需要 NDK。`eve get android` 安装的 JDK、Gradle 和 Android SDK 路径记录在 `eve-android.env` 中，`eve build` 会读取它。手动配置时使用 JDK 17、Gradle 8.5、Android platform 34 和 Build Tools 34.0.0；签名示例使用 Build Tools 36.0.0 的 `zipalign -P 16`。

以下命令适用于 macOS/Linux 的 shell；Windows 可用 PowerShell，设置环境变量的语法需相应调整，工具文件名为 `zipalign.exe`、`apksigner.bat` 和 `adb.exe`。

```sh
export ANDROID_HOME="/你的/Android/sdk"
export JAVA_HOME="/你的/JDK17"
export GRADLE_HOME="/你的/gradle-8.5"
export PATH="$JAVA_HOME/bin:$PATH"
BT="$ANDROID_HOME/build-tools/36.0.0"
GAME="/你的/游戏目录"
OUT="/你的/打包输出目录"
```

`BT` 选择实际安装的版本，确认包含 `zipalign` 和 `apksigner`。需要 `-P 16` 支持时安装较新的 Build Tools。手动下载 Android 目标 SDK 时，在 `eve build` 命令中追加 `--sdk /解压后的/eve-sdk/android`，不要误传桌面 SDK。游戏目录应有 `config.nut` 和 `main.nut`。输出目录必须独立于游戏资源与密钥目录；当前 `eve build -o` 会删除并重建该目录，不要指向已有重要文件的目录。

## 2. 自己试玩：Debug 自动签名

```sh
eve build -d android "$GAME" -o "$OUT"
"$ANDROID_HOME/platform-tools/adb" install -r \
  "$OUT/apk/app/build/outputs/apk/debug/app-debug.apk"
```

Debug APK 由 Gradle 自动使用调试密钥签名，无需自己生成密钥。设备需开启 USB 调试并授权电脑。

## 3. 分发：生成并保管 Release 密钥

每个游戏只需首次生成一次；先创建自己的密钥目录，密钥放在源码和构建输出目录之外：

```sh
keytool -genkeypair \
  -keystore /你的/密钥目录/mygame-release.jks \
  -alias mygame \
  -keyalg RSA -keysize 2048 -validity 10000
```

按提示输入密码和证书信息。备份密钥、别名和密码，不要提交 Git，也不要把密码写在命令行或文档中。默认生成格式由 JDK 决定，`.jks` 扩展名并不强制 JKS 格式；`apksigner` 可以读取 JDK 17 默认的 PKCS12。

同一应用的后续更新应继续使用原签名密钥，并提高 `versionCode`。若使用 Google Play App Signing，需要另行区分上传密钥和应用签名密钥。

## 4. 构建、对齐、签名和检查

```sh
eve build android "$GAME" -o "$OUT"

UNSIGNED="$OUT/apk/app/build/outputs/apk/release/app-release-unsigned.apk"
ALIGNED="$OUT/mygame-aligned.apk"
SIGNED="$OUT/mygame-release.apk"

"$BT/zipalign" -P 16 -f 4 "$UNSIGNED" "$ALIGNED"
"$BT/apksigner" sign \
  --ks /你的/密钥目录/mygame-release.jks \
  --ks-key-alias mygame \
  --out "$SIGNED" "$ALIGNED"

"$BT/zipalign" -c -P 16 4 "$SIGNED"
"$BT/apksigner" verify --verbose --print-certs "$SIGNED"
```

`apksigner` 会交互式询问密码。验证应退出为 0，并显示 `Verifies` 和证书指纹。`minSdk=24` 的 APK 可以依靠 v2/v3 签名，看到 v1 为 false 不代表失败。

一定先对齐再签名；签名后修改 APK 会破坏签名。Gradle 通常已经对齐输出，这里显式执行并复查，以保证手动签名流程可重复。`-P 16` 处理 ZIP 内未压缩库的对齐，不等于证明 native 库的 ELF 对齐或所有设备兼容性。

## 5. 安装和启动

已有 Android SDK 和 `emulator` 程序，不一定已经安装系统镜像并配置虚拟设备。先检查：

```sh
"$ANDROID_HOME/emulator/emulator" -list-avds
"$ANDROID_HOME/platform-tools/adb" devices -l
```

如果没有 AVD，Apple Silicon 开发机可安装 ARM64 镜像并创建测试设备（首次需要下载镜像）：

```sh
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" \
  --install "system-images;android-35;google_apis;arm64-v8a"
"$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager" create avd \
  --name eve-apk-test --device pixel_6 \
  --package "system-images;android-35;google_apis;arm64-v8a"
"$ANDROID_HOME/emulator/emulator" -avd eve-apk-test \
  -gpu swiftshader -no-snapshot -partition-size 8192
```

`avdmanager` 询问是否自定义硬件配置时，可输入 `no`。等待 Android 启动完成后，在另一终端安装。其他主机需选择主机支持的系统镜像，并单独确认它可以运行当前 SDK 的 ARM64 native 库。

```sh
ADB="$ANDROID_HOME/platform-tools/adb"
"$ADB" devices -l
"$ADB" install -r "$SIGNED"
"$ADB" shell am start -W -n com.evengine.example/.EVEngineActivity
```

当前模板的包名是 `com.evengine.example`。正式发布应确定自己的应用标识、名称、图标和版本，再完成最终打包；以上启动命令随实际包名调整。

如果同时连接多台设备，每条 `adb` 命令都添加 `-s <设备序列号>`，避免安装到错误设备；序列号从 `adb devices -l` 获取。

如果设备已装了同包名但不同签名的 Debug/Release，更新安装会报 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`。先备份数据，再明确选择卸载旧包（会清除应用数据），或换用独立包名。

安装成功只证明包管理器接受 APK；还要检查启动日志、实际画面、触控、音效、前后台切换和持续运行。当前 Android SDK 的 native 目标是 `arm64-v8a`，游戏运行需要设备提供 Vulkan。

## 6. 实操记录

本节记录仓库维护者的实际构建与签名结果，不能代替具体游戏的真机验收。

2026-11-12，macOS Apple Silicon 实操：

| 项目 | 结果 |
| --- | --- |
| 桌面 CLI | 官方 `eve-sdk-macosx-arm64-v0.7.0.zip`，`eve --version` 为 `v0.7.0 git=e5e9622aec3f` |
| Android 目标 | 官方 `eve-sdk-android-v0.7.0.zip`，两个 SDK 压缩包均与发布的 `SHA256SUMS` 一致 |
| 工具 | JDK 17.0.20、Gradle 8.5、Android platform 34、签名工具 Build Tools 36.0.0 |
| 游戏 | 正式桌面 SDK 的 `share/eve/examples/basic`，原样打包 |
| 构建 | `eve build android <basic目录> --sdk <android目标SDK> -o <独立临时目录>`；`BUILD SUCCESSFUL`，生成 `app-release-unsigned.apk` |
| 密钥 | 本次临时生成的测试专用 RSA 2048 密钥，别名 `apk-signing-check`，有效期 30 天；不能用作正式发布密钥 |
| 对齐 | `zipalign -P 16 -f 4` 后签名，再执行 `zipalign -c -P 16 4`，退出为 0 |
| 签名 | `apksigner verify --verbose --print-certs` 退出为 0，`Verifies`，v2/v3 为 true，签名者数量为 1 |
| 内容 | ZIP 完整性通过；包内 `config.nut`、`main.nut` 与示例一致；四个 `.so` 与目标 SDK 原文件逐字节一致，ABI 为 `arm64-v8a` |
| 模拟器环境 | 本机原有 Android Studio、SDK、Emulator 37.1.11，但没有系统镜像和 AVD；本次安装 API 35 `google_apis;arm64-v8a` 镜像，创建独立测试 AVD，使用 SwiftShader |
| 安装运行 | 已签名 APK 在 Android 15 / API 35 ARM64 模拟器上安装返回 `Success`，Activity 启动返回 `Status: ok`；脚本及持续运行结果见下方 |

签名后 APK 约 367 MiB，SHA-256：

```text
685be10a13d1a0658ddfe0a082c77db296991c09a7b568af038e2e59809bf8ed
```

签名证书 SHA-256：

```text
2538f92baa06df4e892dbb17bf33cfcd867331a6bc51848ca488a5a24ea42076
```

此次自动化实操通过受限权限的临时密码文件向 `apksigner --ks-pass file:<文件>` 提供密码，未把密码写进命令行；私钥密码与密钥库密码相同时省略 `--key-pass`。日常手动操作使用上文的交互式密码输入即可。

模拟器运行日志确认 Vulkan 初始化成功，输出 `Basic step 1: 01-rectangle`、`eve_init done` 以及 `first present - window shows content`。随后每 10 秒采样一次，共观察 30 秒，应用 PID 始终为 4085，应用日志未出现 `Fatal signal`、`FATAL EXCEPTION` 或 `Run failed:`。验证完成后关闭本次测试模拟器。

这些是脚本执行与首帧提交的日志证据，不能代替引擎截图的视觉验证。测试使用 basic 第 1 课，不涉及触控、音效或完整玩法；仍需具体游戏的真机验收。

## 官方参考

- [apksigner：签名、验证及签名后不可修改 APK](https://developer.android.com/tools/apksigner)
- [zipalign：对齐顺序与 `-P 16`](https://developer.android.com/tools/zipalign)

# 音频播放模块

**脚本入口：** `eve.Audio()`

创建可播放 Source，控制音量、音高、循环、进度和 3D 声源位置。

浏览器 WebGPU 构建使用相同的 `eve.Sound()`、`eve.Audio()`、`Source` API。
`Sound.newSoundDataFromFile(path)` 从已打包的游戏资源读取文件，由浏览器解码为 PCM；
`Audio.newSource(data)` 播放音效，循环播放可用作背景音乐。浏览器可解码的格式取决于
浏览器实现，建议使用 WAV、MP3 或 Ogg。首次播放可能等待用户点击或按键，Emscripten
OpenAL 会在交互后恢复播放。浏览器解码目前会将整个文件载入内存；流式 Source 仍按块
向 OpenAL 提交 PCM，但不会节省压缩文件的解码内存。

## 脚本生成 PCM 音频

`audio.newPcmStream(sampleRate, channels)` 返回 Result，成功时 `value` 是脚本拥有的
`PcmStream`。采样率为 8000..192000，声道数为 1 或 2。`submit(samples)` 接收有限的
`[-1,1]` 浮点/整数采样数组，双声道按 L,R 交错，统一转为 PCM16；返回 Result 的 value
为接受的采样帧数。每块最多 4096 帧、最多排队 8 块，满队列返回 `conflict`，不会部分提交。

`play/pause/finishInput/reset` 都返回须检查的 Result；`finishInput` 后排空即 finished，
欠载为 starved，补入数据后自动续播。`reset` 清空并重新开放输入，`close` 永久释放且幂等。
`getInfo()` 查询 phase、sampleRate、channels、availableBlocks、maxBlockFrames、queuedFrames、
playedFrames（已完成整块帧数）。引擎每帧自动 pump，脚本只在主线程提前按块生成。
Audio 先销毁时会关闭流，脚本先释放流也安全；`stopAll()` 关闭所有 PCM 流。
流不持久化，热重载时关闭并重建。该路径无需 SoundData、文件或 Decoder。

完整调用、错误处理和容量/生命周期契约见
[pcm-2a03 示例](../../../examples/pcm-2a03/README.md)。所有波形、包络、变调和噪声合成都在脚本侧。

## 基本用法

```squirrel
local sound = eve.Sound();
local pcm = sound.newSoundDataEmpty(44100, 44100, 16, 1);
local audio = eve.Audio();
local src = audio.newSource(pcm);
src.setLooping(true);
src.setVolume(0.6);
src.play();
```

## 对象关系与调用时机

`Audio` 控制 listener 和全局音量；`Source` 控制单个声音实例；Source 引用 SoundData。音效通常复用 SoundData、按播放需要创建或池化 Source。

## 目标导向指南

### 播放循环背景音乐

从 SoundData 创建 Source，设置 `setLooping(true)` 和音量后 `play()`；暂停菜单用 `pause()`，继续用 `play()`，换场景时 `stop()`。用 `tell()` / `seek()` 实现继续播放。

### Source 淡入淡出与交叉淡化

淡变使用**注入的绝对模拟时间** `now`，不要用墙钟。`fadeTo(target, duration, now)` /
`fadeOutAndStop(duration, now)` / `crossfadeSources(from, to, toVolume, duration, now)` 只武装包络；
调用方每帧（或每次逻辑步进）必须 `advanceFade(now)`。`duration == 0` 立即到位。
`cancelFade()` 保留当前可听音量。`getVolume()` 返回最近一次 `advanceFade` 后的可听音量；
`getFadeWeight()` 为 `[0,1]` 进度（空闲为 1）。

```squirrel
src.fadeTo(1.0, 1.5, now);
// eve_update:
src.advanceFade(now);
```

`eve.crossfadeSources(from, to, 0.8, 1.0, now)` 会启动 `to`（若未在播），从 0 淡入到目标音量，并对 `from` 做 `fadeOutAndStop`；两侧都要各自 `advanceFade`。

### 环境立体声床

`eve.configureAmbientBed(src)` 将 Source 配成非空间环境床：`spatialBlend = 0`（relative、rolloff 0）、
`balance = 0`，保留 mono/stereo 通道。立体声环境音应走这条路径，而不是当 3D 点声源。

`setBalance([-1,1])`：优先 `AL_EXT_STEREO_ANGLES`；无扩展时 **relative mono** 用 `position.x` 近似；
relative stereo 且无扩展则返回失败（不会静默给出错误声像）。

`setSpatialBlend([0,1])`：`0` = 环境床；`1` = 完整世界衰减；中间值使用 `rolloff = blend`。
立体声且 `blend > 0` 需要 `AL_SOFT_source_spatialize`，否则失败——改用 mono，或 `configureAmbientBed`。

### 水下环境声

`applyUnderwaterAudio(down, up, ambience, playDown, playUp, loopAmbience, volume, fadeSeconds, now)`
消费水下状态机的一次性转换标志和循环状态。down/up 可为空，ambience 必须有效；`volume` 在 `[0,1]`。
`fadeSeconds == 0` 保持硬切；`> 0` 时对 ambience 使用 `fadeTo` / `fadeOutAndStop`，调用方须继续
`ambience.advanceFade(now)`。one-shot 下潜/上浮仍即时播放。所有 Source 始终由调用者持有。

### 设置 3D 声源

每帧同步 Source 的 `setPosition()`、`setVelocity()`，并同步 Audio listener 的位置和朝向。界面音效应
`setRelative(true)` 或 `setSpatialBlend(0)`，避免随摄像机衰减。世界点声源用 mono + `spatialBlend` 接近 1。

## 常见问题

- SoundData 生命周期短于 Source：必须保留底层声音数据。
- 把世界音效设为 relative：会失去距离衰减。
- 同一 Source 重叠播放：需要多个 Source 或对象池。
- 立体声文件当 3D 点声源：OpenAL 默认不按距离衰减；用 mono，或开 spatialize，或改环境床。
- 忘了 `advanceFade(now)`：淡变不会推进。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。

**源码：** [`src/modules/audio/`](../../../src/modules/audio/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `audio`。

### Pcg 环境音频区域

`AudioZoneItem` 包含 `volume`、`fadeInTime`、`fadeOutTime`、`duration`。
`AudioZoneProfile` 的 `x/y/z`、`radius`、`global`、`minimumBreakTime`、`maximumBreakTime`、
`deactivationTime` 描述区域与时序，使用 `addItem` 添加曲目并由 `getItemCount` 查询。
`AudioZoneState` 保存 `selectedTrack` 和 `playing`；`evaluateAudioZone` 接收绝对模拟时间、玩家坐标、
master volume 和显式 seed，返回 `play`、`stop`、`trackIndex`、`volume` 命令。实现保留 Pcg 的曲目淡入淡出、
不连续重复曲目；离开区域后在 `deactivationTime` 窗口内将音量从离开时的可听音量线性降到 0，到期再 `stop`。
调用者继续拥有实际 `Source`。选择 `trackIndex` 对应的 Source 后，可用
`applyAudioZoneOutput(source,play,stop,volume)` 原子应用命令。

# 音频播放模块

**脚本入口：** `eve.Audio()`

创建可播放 Source，控制音量、音高、循环、进度和 3D 声源位置。

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

`applyUnderwaterAudio(submergeDown,submergeUp,ambience,playDown,playUp,loopAmbience,volume)` 消费水下状态机的
一次性转换标志和循环状态。down/up 可为空，ambience 必须有效；函数先验证 `[0,1]` 音量，再设置三个借用
Source 的增益、一次性播放下潜/上浮声音，并启动或停止循环环境声。所有 Source 始终由调用者持有。

### 设置 3D 声源

每帧同步 Source 的 `setPosition()`、`setVelocity()`，并同步 Audio listener 的位置和朝向。界面音效应 `setRelative(true)`，避免随摄像机衰减。

## 常见问题

- SoundData 生命周期短于 Source：必须保留底层声音数据。
- 把世界音效设为 relative：会失去距离衰减。
- 同一 Source 重叠播放：需要多个 Source 或对象池。

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
master volume 和显式 seed，返回 `play`、`stop`、`trackIndex`、`volume` 命令。实现保留 Pcg 的淡入淡出、
不连续重复曲目和离开区域后默认 10 秒延迟停播规则，调用者继续拥有实际 `Source`。
选择 `trackIndex` 对应的 Source 后，可用 `applyAudioZoneOutput(source,play,stop,volume)` 原子应用命令。

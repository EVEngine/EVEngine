# 声音数据模块

**脚本入口：** `eve.Sound()`

读取声音数据、解码器和采样信息，供 Audio 创建 Source。

## 基本用法

```squirrel
local sound = eve.Sound();
local pcm = sound.newSoundDataEmpty(44100, 44100, 16, 1);
print("duration=" + pcm.getDuration() + "\n");
local src = eve.Audio().newSource(pcm);
```

## 对象关系与调用时机

`Decoder` 流式解码压缩数据；`SoundData` 保存 PCM；`Sound` 负责两者创建；`Audio` 才负责播放。短音效完整解码，长音乐优先流式策略（受当前绑定能力限制）。

## 目标导向指南

### 从解码器准备静态声音

文件内容先以 Data 读入，再传给 `newDecoder(data)`；短音效用 `newSoundDataFromDecoder(decoder)` 完整解码，随后交给 Audio。可用采样率、声道数和 bit depth 做资源诊断。

### 生成程序化声音

`newSoundDataEmpty(samples, rate, bitDepth, channels)` 创建 PCM 容器。当前脚本绑定若不提供逐样本写入，应由原生插件或 Demo 生成数据，再交给 `Audio.newSource()`。

## 常见问题

- 把 Decoder 直接当 Source：先转换为 SoundData 或用 Audio 对应工厂。
- 声道/位深参数不匹配：先读取 decoder 元数据。
- 对长音乐一次性解码造成内存高峰：评估时长和格式。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `decode()`、`getBitDepth()`、`getChannelCount()`、`getDuration()`、`getName()`、`getSampleCount()`、`getSampleRate()`、`getSize()`
- `isFinished()`、`isSeekable()`、`newDecoder()`、`newSoundData()`、`newSoundDataEmpty()`、`newSoundDataFromDecoder()`、`rewind()`、`seek()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/sound/`](../../../src/modules/sound/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `sound`。

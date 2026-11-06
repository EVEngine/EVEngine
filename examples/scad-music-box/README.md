# SCAD 八音盒

原创 OpenSCAD 参数化八音盒：木壳 → 铰链盖 → 黄铜底板 → 发条桶 → 销钉滚筒 →
音梳 → 调速扇 → 舞者。滚筒销钉与 EveScript PCM 拨弦合成同步，滚筒每转一圈
演奏一段《小星星》公共领域旋律。

## 运行

```sh
make run/linux-debug GAME=examples/scad-music-box
# Windows: make run/win32-debug GAME=examples/scad-music-box
```

默认 1440×900。Space 播放/暂停，L 开合盖子，M 静音，←/→ 环绕观察，
↑/↓ 调整俯仰，R 复位，H 隐藏 HUD。

## SCAD 创作约定

权威数据在 `tools/design.py`：

1. 关节树：`parent`、父空间 `pivot`（毫米 / Z-up）、旋转 `axis`、角度限位（度）。
2. 每个 `part` 在自己的关节局部坐标系建模；`musicbox.scad` 用 `part = "..."` 门控几何。
3. `musicbox.rig.json` 是 `eve.scad-rig/1` 侧车；`assets/tune.nut` 是销钉曲谱与音梳频率。
4. 齿轮比写在 `ratios.nut`：发条桶驱动滚筒与调速扇，舞者随滚筒减速自转。
5. 仓库附带 OBJ 批次，普通运行不需要 OpenSCAD。

重建：

```sh
python3 examples/scad-music-box/tools/bake.py --from-design-only
python3 examples/scad-music-box/tools/bake.py                  # 有 OpenSCAD 时验证 CSG 批次
python3 examples/scad-music-box/tools/bake.py --eve /abs/path/to/eve
```

音频走 `audio.newPcmStream`：脚本按滚筒角度触发音梳拨弦，合成正弦 + 奇次谐波
衰减音色，无需磁盘采样文件。

## 验证范围

- `tools/design.py` 生成 SCAD / rig / OBJ / 材质表 / 曲谱 / 齿轮比。
- 有 OpenSCAD 时 `bake.py` 对每个 `part` 做 `--hardwarnings` 编译并覆盖 OBJ。
- `scripts/check_examples.py` 与 `smoke_examples.sh` 覆盖本示例为可运行项目。

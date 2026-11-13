# Map Spawn Editor

Runnable editor for the forest sidecar, using actual RPG unit/template definitions and the existing native map,
transaction, property form, region tool and document services. Its explicit preview uses a separate private RPG/map world;
formal gameplay worlds are never advanced or populated by this editor.

Build with `map_rpg_spawn_editor` enabled (included in the 2d/3d authoring groups), then run from this directory:

```sh
../../build/macosx-debug/src/engine/eve run
```

From the repository root the usual `make run GAME=examples/spawn-editor` host recipe also selects this project.
The window uses a fixed 1280 by 800 logical-pixel layout with a 480-pixel square map canvas and wider configuration
form; keep it at least this size. Panels scroll vertically when their content exceeds the visible space.

- Cyan outlines show all committed regions, even before selecting a tool. White feedback shows the current gesture.
  Select an instance in the controls, then choose Circle, Points or Polygon and edit on the map.
  Source edits and undo/redo refresh the outlines; unapplied structure drafts do not replace committed regions.
- The preview panel starts from a frozen committed source with the explicit seed 42 and a separate scheduler owner.
  The example performs an initial zero-duration step so green markers show actual committed preview units.
  Start / restart admits the current committed source; Step 1s advances only the preview; Stop cleans up its units.
  Source edits do not change an existing preview. Scroll the panel for the canonical read-only scheduler diagnostics,
  including budget, activation predicate, phase, blocking reason and pinned template. Preview does not run combat or AI.
  The bundled initial source also contains an `alarm-camp` with two units, initially dormant.
  Click **Trigger event: camp.alarm** to activate it in the private preview; repeating the event does not exceed
  its target population. Saved editor documents continue to reopen their own source without replacement.
  The initial source also has a proximity-activated `nearby-camp`, centered at World2D `[90,224]` with radius 40.
  Enter `[[90,224]]` in **Player draft**, then click **Apply players** to activate it with a zero-duration step.
  Coordinates are world units, not screen pixels. Multiple positions are supported; `[]` clears the accepted frame.
  Ordinary steps and event input retain the last accepted frame. Moving outside the exit threshold pauses further
  generation while retaining existing units. Editing the draft alone does not submit it or modify the saved asset.
  Scroll the preview panel to **Defeat one: wolf-camp** to apply lethal HP damage to one actual private unit.
  It submits death through the canonical lifecycle bridge with a zero-duration step. In the bundled population
  template, two **Step 1s** actions then allow the delayed replacement. Repeating a stale identity cannot kill its
  replacement. For wave templates, defeat each member and step through the configured inter-wave delay.
- Placement diagnostics retain the last 256 actual sampling/final-check decisions and count older dropped rows.
  Scroll the preview panel for constraint reasons, World2D coordinates, subject and operation identities. Final checks
  may repeat for one operation. Spatial acceptance does not mean a domain unit was committed.
  Red map markers show historical rejected candidates with known positions; green markers remain actual living units.
  A polygon sampler rejection without a returned position has a diagnostic row and no invented marker. History resets
  on preview restart; it is transient and is not saved or exported. Formal runtime placement tracing is disabled by default.
- Points/Polygon use Enter to commit, Backspace to remove a point and Escape to cancel. The window must have foreground
  focus for native mouse capture; capture failure cancels the gesture and is shown in the session panel.
- The configuration form uses the existing undo/redo history. Edit structure starts a separate explicit candidate;
  Apply draft commits it atomically, Cancel draft discards that candidate.
- Save writes the editor document envelope under `Content/spawn-editor/forest.json` in this project. It does not overwrite
  `data/spawns.json`. The saved editor document is read on subsequent opens and must match the binding's map id.
- Prepare/Write in the runtime export panel writes bare sidecar JSON to `spawn-editor/forest.spawns.json` under the
  engine filesystem's `evengine-spawn-editor` save directory (the example explicitly configures this identity and creates
  the export parent directory before opening the editor). Copy that exported asset into a runtime project's sidecar location.
- Window close requests stay in the session panel. Save and resolve drafts, then choose Close when saved; or explicitly
  choose Discard changes and close. Keep editing cancels the close request. Forced process termination cannot preserve
  unapplied UI drafts.

Smoke from the repository root (set `EVE` to the built binary when using a custom build directory):

```sh
EVE="$PWD/build/macosx-debug/src/engine/eve" MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh spawn-editor
```

This example does not implement combat/AI simulation, full RPG/RTS save restoration, window resizing or hot reload.

The `rpgSpawnEditor` slot exposes `publishUnit`, `publishTemplate`, `open`, `update`, `render`, `shutdown` and the cheap
`isOpen` fact. Map mode also exposes `startPreview(fs, owner, seed, bodyRadius)`, `stepPreview(nanoseconds)` and
`stopPreview()`, `triggerPreviewEvent(name)` and `applyPreviewPlayers(players)`; event and player input use zero-duration steps.
`players` is a JSON array of finite World2D coordinate pairs, limited to 1024 positions. Invalid input does not advance
the preview. No player actor is created: the frame supplies activation and placement-exclusion facts.
`defeatPreviewUnit(subject)` accepts a non-nil persistent subject identity in the current preview's membership.
It applies real domain damage and then advances 0s. If advancement fails after damage, a later Step retries the retained
private death facts; already committed damage is not rolled back. Formal gameplay event queues remain untouched.
`update` returns a `preview_action` request plus `preview_event`, `preview_players` or `preview_subject`, which this script consumes outside
UI callbacks. Publish skill dependencies through the canonical `eve.RPG()` API before publishing units; missing skills
are rejected. Content publication is disabled while an editor session is open. Publish/open compile complete assets;
update drains at most 1024 native samples and explicitly cancels on overflow; render consumes visible tile chunks and
existing panel rebuilds. `shutdown(win, "require_clean")` preserves unresolved edits on failure; `"discard"` is an explicit
discard choice. Native-window-first teardown is detected through the SDL registry and never dereferences the retired
native handle. None of these calls retains a borrowed Window/UI/Graphics/Canvas pointer across frames.

Template content can be edited independently with [spawn-template-editor](../spawn-template-editor/README.md),
which uses the same canonical document, history, Save and Export services.

Event buttons come from the frozen runtime snapshot. Restart/Stop cancel pending requests, and retained callbacks
check the preview owner/epoch/incarnation. Empty or unknown event names return failure without advancing simulation.


Tiled 几何导入：先在预览面板 Stop，再在会话面板打开 Tiled geometry import。
填写路径 `data/import.tiled.json`，来源 UUID `01900000-0000-7000-8000-000000000041`；
示例通过 `eve.spawn.instance` 引用 `wolf-camp`，因此 prototype 可留空。
Prepare 只显示琥珀色候选；Commit 写入现有撤销历史，Cancel 不改源。
返回表单可 Undo/Redo，之后通过既有 Save/Export 保存。
创建未引用的对象时，prototype 必须填写已有实例 ID；其配置被显式复制，Tiled 只提供几何。
来源 UUID 应为资产登记的稳定身份，不能按路径或对象名临时重新生成。
待提交导入会阻止正常关闭和启动运行预览；错误保留候选以便检查或取消。


`data/import-new.tiled.json` 提供一个未引用已有实例的点对象。填写同一登记来源 UUID，并显式选择 prototype `wolf-camp` 后准备/提交，即可创建稳定 ID 的新实例；Undo 可整体撤销。

完整前台回归可在仓库根目录运行：

```sh
python3 scripts/spawn_editor_ui_smoke.py --exe build/spawn-map/src/engine/eve --output build/spawn-editor-ui-evidence
```

脚本使用正式示例的存储隔离副本，经原生 MCP 控件事件验证新实例/引用导入、取消、撤销/重做、保存、重开与导出；输出每轮独立的 JSON 回执、文件和引擎截图。它需要与窗口测试相同的图形环境。正常关闭完成前会保持退出请求，快速关闭也不会因启动保护而遗留空窗口。

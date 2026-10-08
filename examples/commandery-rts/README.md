# Commandery RTS Demo

A playable RTS + general/administration composition demo. Domain rules (governors,
salaries, rebellion, victory) live in this script. Units and shells are script ECS
entities (`Unit` / `Shell` + `eve.System`); engine modules still only store and
query generic facts (`Orders`, `Production`, `Authority`, `Social`, `Decision`,
`Sensing`).

Run from a current-source debug build:

```sh
# Linux (this Cloud VM / CI image)
make debug JOBS=4
cd examples/commandery-rts
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
ALSOFT_DRIVERS=null \
XDG_RUNTIME_DIR=/tmp/xdg-runtime \
xvfb-run -a ../../build/linux-debug/src/engine/eve run .

# Windows
build\win32-debug\src\engine\eve.exe run examples\commandery-rts
```

Headless MCP (screenshots via `eve_screenshot`, not the Xvfb framebuffer):

```sh
cd examples/commandery-rts
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a ../../build/linux-debug/src/engine/eve run --debug --mcp-port=7529 .
```

Controls:

- Left click a blue unit to select it. Drag to box-select friendly units.
- Right click the battlefield to issue a formation move (owned `Orders.replace`).
- Move units onto an economy point to capture it. Tanks capture slightly faster.
  Progress pauses while both factions are present.
- The unowned hospital at the lower-left is capturable the same way. While a
  faction holds it (and it is not contested), a script `HospitalRegenSystem`
  restores that faction's living infantry at 12 HP/s. Tanks are unaffected.
- Select one friendly infantry, then `E` (or the panel button) to toggle that
  soldier's repair skill. Only that unit changes: it slowly heals the nearest
  friendly tank within 72 px and cannot fire until toggled off. Other infantry
  keep their own repair state.
- `1` / `2`: train infantry / build a tank (refunds if enqueue fails).
- `G`: appoint or dismiss the northern governor.
- `P`: pay salary (loyalty up). `U`: withhold salary (loyalty down; rebellion if
  loyalty < 25, ambition > 70 and officer support is high).
- `R`: reset the scenario.
- The right-hand panel mirrors those buttons.

The opening deploys Crown on the north and bridge mines, Frontier on the east
mine. Frontier AI starts in `raid`: hold while outnumbered, leave a garrison,
contest the nearest Crown mine, and train up to four units without flooding the
queue. It only flips to `assault` after 15 seconds, and only if it outnumbers
Crown. The match ends when one side has no living units.

The engine module does not contain `General`, `Rebellion`, hospital regen, or a
fixed RTS AI type; those rules are all in `main.nut` / `ecs.nut`.

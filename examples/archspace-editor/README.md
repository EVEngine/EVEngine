# ArchSpace editor example

Smoke for the Pascal-like **ArchSpace** domain (not RTS `building`). Loads
`archspace` + `archspace_editing` + `archspace_editor`, then prints the Agent
command path. Project owns the viewport/UI shell.

```bash
cd examples/archspace-editor
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a ../../build/linux-debug/src/engine/eve run
```

C++/unit coverage: `test/editor_archspace_target.cpp`. Design:
`docs/dev/2026-09-14-archspace-editor.md`.

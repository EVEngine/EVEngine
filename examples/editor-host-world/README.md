# editor-host-world

Minimal gold sample for `EditorHost` + `EditorPackage` autoMount and Mode A MCP attach.

Loads Action and Voxel editor packages, mounts them on one host, and attaches the
host as the MCP projection scope (`exportMcp` / `eve.mcp.attachEditor`). A status
window shows the real mount count and package state. Domain panels are UI-neutral
descriptors; this example does not compose their full editing interfaces.

## Run

```sh
make run GAME=examples/editor-host-world
```

Headless smoke:

```sh
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh editor-host-world
```

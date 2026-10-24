# editor-host-world

Minimal gold sample for `EditorHost` + `EditorPackage` autoMount and Mode A MCP attach.

Loads Action and Voxel editor packages, mounts them on one host, and attaches the
host as the MCP projection scope (`exportMcp` / `eve.mcp.attachEditor`). Project
glue stays under 30 lines — domains own panels, commands, and schemas.

## Run

```sh
make run GAME=examples/editor-host-world
```

Headless smoke:

```sh
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh editor-host-world
```

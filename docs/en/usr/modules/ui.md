# UI: Getting Started

[中文完整手册](../../../usr/modules/ui.md) · [Module handbook](../MODULES.md)

This manually maintained English introduction covers the common workflows from the authoritative Chinese guide. Advanced editor-host, property-inspection and specialized widget sections remain in that guide.

**Script constructor:** `eve.UI()`. The normal game instance is `ui`. UI retains named Host trees; stable IDs identify controls. Events return full `host/id` paths, while setters search the currently selected Host.

## Create an interactive HUD

Build and mount once during initialization, consume events during update and render each frame:

```squirrel
eve_init <- function() {
    ui.beginBuild();
    ui.beginWindow("HUD", "root");
    ui.text("Ready", "status");
    ui.button("Start", "start");
    ui.end();
    ui.mountBuildAs("hud");
};
eve_update <- function(dt) {
    local id = ui.consumeClick();
    while (id != "") {
        if (id == "hud/start") ui.setText("status", "Running");
        id = ui.consumeClick();
    }
};
eve_render <- function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
```

Keep control IDs stable. Drain event queues instead of reading a single click and leaving the rest for later frames. Before interpreting gameplay input, check whether UI wants to capture it.

## Layout

Rows and columns arrange children without manual positioning:

```squirrel
ui.beginRow("toolbar", 8.0);
ui.setFlexJustify("space-between");
ui.iconButton("save", "Save", "save");
ui.spacer("sp");
ui.button("Quit", "quit");
ui.end();
```

`setItemFlexGrow`, `setItemFlexShrink`, `setItemFlexBasis`, `setItemAlignSelf` and `setItemSize` affect the most recently added child. `setFlexAlign` / `setFlexJustify` configure the current container. Use `setLayoutGaps(columnGap, rowGap)` for separate axis spacing and `setLayoutOverflow("visible" | "clip" | "scroll")` for overflow.

`beginGrid(columns, id, columnGap, rowGap)` creates equal-width columns; a child can span columns or derive a dimension from an aspect ratio. A SplitPane requires exactly two direct children. Its dragged ratio produces an ordinary value-change event and can be saved/restored with `getValue` / `setValue`.

Movable/resizable Hosts use stable IDs to retain ImGui layout. Choose logical UI coordinates consistently with the [window DPI guide](window.md).

## Reusable components

Derive a component from `eve.UIComponent`. Each instance has its own `props` and `state`. `setProps()` / `setState()` merge changes and mark it dirty. Mounted roots rebuild once before rendering and reconcile controls by stable ID; normal game code need not call `updateIfDirty()` manually.

Embed retained child instances with `renderChild(child, props)`. Child dirtiness propagates to the parent. Passing `replaceProps=true` replaces rather than merges old props. First mounting calls `onMount()`, later rebuilds call `onUpdated()`, and explicit `unmount()` recursively calls `onUnmount()` and hides the Host.

Use component `bindClick(id, fn)` / `bindChange(id, fn)` for owned handlers. Rebinding the same type/ID replaces the old handler. Unmounting unregisters handlers. `renderKeyed(key, factory, props)` reuses components for stable keys; removing a key unmounts it. Internal `componentOnClick` / `componentOnChange` bridges are not the recommended gameplay interface.

## Nine-patch assets

The loader accepts raw Android-style `.9.png` files. A single continuous black segment on the top and left borders defines stretch regions; optional bottom/right marks define content padding. The one-pixel marker frame is removed before rendering.

```squirrel
ui.beginBuild()
ui.beginWindow("Nine patch", "root")
if (ui.beginNinePatch("assets/panel.9.png", "panel", 320, 0)) {
    ui.text("Content uses the asset's padding", "content")
    ui.end()
}
ui.ninePatch("assets/button.9.png", "preview", 180, 48)
ui.end()
ui.mountBuildAs("nine-patch-demo")
```

Discontinuous stretch regions are deliberately unsupported: the call returns false with a diagnostic. Use `ninePatch()` for a leaf image and `beginNinePatch()` for a panel containing children.

## Persisted trees and troubleshooting

`saveTreeJson()` currently writes `schema: "eve.ui.tree"`, version 4. Loading treats unversioned documents as version 1, accepts versions 1–4 and rejects unknown schemas/future versions. Unknown fields are ignored; newer fields have compatible defaults.

If changes do not appear, check the selected Host and the control ID. If clicks affect gameplay too, check input capture. Rebuilding everything with unstable IDs loses retained control state. Use the source examples [ui_demo.nut](../../../../examples/basic/ui_demo.nut) and [ui_component.nut](../../../../examples/basic/ui_component.nut) as starting points.

For complete contracts, use [API conventions](../API-CONVENTIONS.md), your SDK's bindings and the [Chinese UI guide](../../../usr/modules/ui.md).

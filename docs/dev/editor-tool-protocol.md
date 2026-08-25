# Editor tool protocol architecture

The editor module is a framework of components, not a catalogue of supported
editor products. `EditorSession` knows only `IEditorTool`; adding a road spline,
voxel sculptor or game-specific placement tool must not add an enum case or a
data record to the session.

## Dependency rule

```text
viewport/UI adapter -> EditorSession -> IEditorTool
                                      -> IEditConstraint
IEditorTool -> EditorContext -> capability queried from IEditableTarget
            -> IEditCommand  -> EditorTransactions
            -> IEditorOverlay / IEditorInspector (host-owned sinks)
```

Concrete targets adapt existing game storage. `TileBufferTarget` exposes
`IIntFieldTarget`; `HeightmapTarget` exposes `IScalarFieldTarget`. A tool asks
for a capability and gracefully ignores incompatible targets. The core does
not keep a list of target or tool kinds.

## Extension points

1. Implement `IEditorTool` for new gestures, or use `ScriptEditorTool` for
   Squirrel callbacks.
2. Implement an `IEditableTarget` capability when existing integer/scalar grids
   are insufficient.
3. Compose `FieldBrushTool` from an `IBrushKernel` and
   `IFieldBrushOperation`, or replace either interface independently.
4. Represent mutations as `IEditCommand`; send them through
   `EditorContext::execute()` so constraints and undo remain universal.
5. Add project rules through `IEditConstraint`. Do not add project-specific
   checks to tools or the session.
6. Implement the presentation sinks in the chosen 2.5D/3D viewport and UI.

All registrations are non-owning. Hosts must remove or clear tools,
constraints, targets, kernels and operations before destroying those objects.

## Compatibility

`Brush`, `EditorHistory`, `EditorToolbar`, `EditorInspector` and `EditorDock`
remain available for existing scripts. They are convenience components, not
the extensibility boundary for new editor features.

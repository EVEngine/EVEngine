// ArchSpace authoring smoke: load the architectural-space domain (not building).
// Native code owns the document, DomainOperations, commands and mesh bake.
// This example only proves module wiring; compose your own viewport/UI shell.

persist arch = {
    module = null,
    editor = null,
    status = "boot",
}

eve_init = function() {
    arch.module = eve.ArchSpace();
    arch.editor = eve.ArchSpaceEditorModule();
    arch.status = "ArchSpace ready · type=archspace via eve_editor_target_create";
    print("ArchSpace example: " + arch.status + "\n");
    print("Commands: archspace.bootstrap.v1 / room.create.v1 / opening.create.v1 / item.place.v1\n");
    print("See docs/usr/modules/archspace.md\n");
};

eve_render = function() {
    gfx.clear();
};

# ---------------------------------------------------------------------------
# Host package: ui
# ---------------------------------------------------------------------------

# L4 -- ui
eve_declare_module(NAME ui LIB EVUI LAYER 4 SCRIPT UI SLOT ui
                   DEPS platform_event filesystem graphics image property_access timer window
                   OPTIONAL_DEPS animation
                   THIRDPARTY sdl2 poco
                   GROUP minimal 2d 3d web)

# L5 -- ui
eve_declare_module(NAME ui_editing LAYER 5
                   DEPS editing ui
                   GROUP 3d web)

# L7 -- ui
eve_declare_module(NAME ui_editor LAYER 7 DEPS editor ui ui_editing
                   SCRIPT UiEditorModule SLOT uiEditor GROUP 3d web)

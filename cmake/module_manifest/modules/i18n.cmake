# ---------------------------------------------------------------------------
# Host package: i18n
# ---------------------------------------------------------------------------

# L7 -- i18n
eve_declare_module(NAME localization_editor LAYER 7 DEPS editor localization_editing GROUP 2d 3d web)

# L6 -- i18n
eve_declare_module(NAME localization_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS audio dialogue
                   GROUP 2d 3d web)

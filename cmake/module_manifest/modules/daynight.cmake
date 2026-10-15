# ---------------------------------------------------------------------------
# Host package: daynight
# ---------------------------------------------------------------------------

# L4 -- daynight
eve_declare_module(NAME daynight LIB EVDayNight LAYER 4 SCRIPT DayNight SLOT daynight
                   DEPS graphics
                   GROUP 3d web)

# ---------------------------------------------------------------------------
# L1 -- platform services and resource decoding
# ---------------------------------------------------------------------------

eve_declare_module(NAME window REQUIRED LAYER 1 SCRIPT Window SLOT win
                   DEPS platform_event
                   THIRDPARTY sdl2)
eve_declare_module(NAME image LAYER 1 SCRIPT Image
                   DEPS filesystem
                   THIRDPARTY medialoader_image
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME asset_import LAYER 1
                   DEPS asset data cmdline
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME i18n LAYER 1 SCRIPT I18n SLOT i18n
                   DEPS filesystem
                   GROUP 2d 3d web)
eve_declare_module(NAME rx LAYER 1 SCRIPT Rx
                   DEPS platform_event
                   GROUP 2d 3d web)
eve_declare_module(NAME joystick LAYER 1 SCRIPT Joystick
                   DEPS platform_event
                   THIRDPARTY sdl2
                   GROUP minimal 2d 3d web)
# L4 -- model/resource orchestration
eve_declare_module(NAME model3d LIB EVModel3D LAYER 4 SCRIPT Model3D SLOT model3d
                   DEPS filesystem image
                   THIRDPARTY medialoader_model assimp
                   GROUP 3d)
# L1 -- platform services (continued)
eve_declare_module(NAME sound LAYER 1 SCRIPT Sound SLOT sound
                   DEPS filesystem
                   THIRDPARTY medialoader_sound audio_codecs
                   GROUP 2d 3d)
eve_declare_module(NAME network LAYER 1 SCRIPT Network
                   DEPS data platform_event
                   THIRDPARTY poco)

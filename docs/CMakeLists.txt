# Doxygen documentation: `cmake --build <build-dir> --target docs`
# Standalone (no engine configure): `cmake -S docs -B build/docs && cmake --build build/docs --target docs`
# Output: docs/api/html/index.html

cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(EVEngineDocs NONE)
    set(DOC_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
else()
    set(DOC_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
endif()

if(NOT DEFINED EVENGINE_MAJOR_VERSION)
    set(EVENGINE_MAJOR_VERSION "0")
    set(EVENGINE_MINOR_VERSION "3")
    set(EVENGINE_PATCH_VERSION "0")
endif()

get_filename_component(DOC_SOURCE_ROOT "${DOC_SOURCE_ROOT}" ABSOLUTE)
file(TO_CMAKE_PATH "${DOC_SOURCE_ROOT}" DOC_SOURCE_ROOT)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" DOC_TOP_DIR)

set(CMAKE_PROJECT_VERSION
    "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}.${EVENGINE_PATCH_VERSION}")

set(EVENGINE_DOC_LANGUAGE "Chinese" CACHE STRING
    "Doxygen OUTPUT_LANGUAGE and ENABLED_SECTIONS label (Chinese or English)")
set_property(CACHE EVENGINE_DOC_LANGUAGE PROPERTY STRINGS Chinese English)

set(DOC_LANGUAGE "${EVENGINE_DOC_LANGUAGE}")
set(DOC_VISIBILITY "PUBLIC")
set(DOC_OUTPUT_DIR "${DOC_TOP_DIR}/api")
set(DOC_EXCLUDE_SYMBOLS "")

# ---------------------------------------------------------------------------
# Generate Doxygen page wrappers.
#
# Doxygen turns a Markdown file into ONE page only when the `\page` command is
# its very first line, and the sidebar tree is built from `\subpage` relations.
# We do not put those commands in the repo's .md files (they would show up on
# GitHub); instead we generate page wrappers in the build directory with the
# metadata prepended. The generated tree mirrors the repository layout, so all
# relative links between the original .md files keep resolving.
# ---------------------------------------------------------------------------
set(_doc_pages_dir "${CMAKE_CURRENT_BINARY_DIR}/pages")
set(DOC_MAINPAGE "${DOC_TOP_DIR}/dox/mainpage.dox")

# Start from a clean tree so stale wrappers from previous configure runs
# (e.g. an older page set) never leak into the generated documentation.
file(REMOVE_RECURSE "${_doc_pages_dir}")
file(MAKE_DIRECTORY
    "${_doc_pages_dir}/docs/usr/modules"
    "${_doc_pages_dir}/docs/dev")

# User guide root: EVEngine 用户指南 -> language tutorial / module handbook /
# trimming / conventions.
file(READ "${DOC_TOP_DIR}/usr/README.md" _doc_usr_readme ENCODING UTF-8)
file(WRITE "${_doc_pages_dir}/docs/usr/README.md"
    "\\page usr_guide EVEngine 用户指南\n\n"
    "\\subpage usr_evescript\n\n"
    "\\subpage usr_modules\n\n"
    "\\subpage usr_trimming\n\n"
    "\\subpage usr_conventions\n\n"
    "${_doc_usr_readme}")

# EveScript language and migration tutorial.
file(READ "${DOC_TOP_DIR}/usr/EVESCRIPT.md" _doc_evescript ENCODING UTF-8)
file(WRITE "${_doc_pages_dir}/docs/usr/EVESCRIPT.md"
    "\\page usr_evescript EveScript 完整教程\n\n${_doc_evescript}")

# Module handbook: groups the five category pages (defined in docs/dox/).
file(READ "${DOC_TOP_DIR}/usr/MODULES.md" _doc_modules ENCODING UTF-8)
file(WRITE "${_doc_pages_dir}/docs/usr/MODULES.md"
    "\\page usr_modules 模块使用手册\n\n"
    "\\subpage usr_env\n\n"
    "\\subpage usr_assets\n\n"
    "\\subpage usr_gameplay\n\n"
    "\\subpage usr_scene\n\n"
    "\\subpage usr_advanced\n\n"
    "${_doc_modules}")

# Trimming guide and API conventions.
file(READ "${DOC_TOP_DIR}/usr/TRIMMING.md" _doc_trimming ENCODING UTF-8)
file(WRITE "${_doc_pages_dir}/docs/usr/TRIMMING.md"
    "\\page usr_trimming 按需裁剪模块\n\n${_doc_trimming}")
file(READ "${DOC_SOURCE_ROOT}/docs/dev/API-CONVENTIONS.md" _doc_conventions
    ENCODING UTF-8)
file(WRITE "${_doc_pages_dir}/docs/dev/API-CONVENTIONS.md"
    "\\page usr_conventions API 使用约定\n\n${_doc_conventions}")

# One module page per docs/usr/modules/<name>.md, format: name|page-id|title.
set(_doc_module_pages
    "animation.md|mod_animation|动画模块"
    "audio.md|mod_audio|音频播放模块"
    "building.md|mod_building|建筑放置模块"
    "cardgame.md|mod_cardgame|卡牌游戏工具模块"
    "climbing.md|mod_climbing|攀爬与跑酷模块"
    "data.md|mod_data|数据模块"
    "daynight.md|mod_daynight|昼夜循环模块"
    "demo.md|mod_demo|内置演示模块"
    "editor.md|mod_editor|编辑器构件模块"
    "entity.md|mod_entity|脚本 ECS模块"
    "event.md|mod_event|事件模块"
    "filesystem.md|mod_filesystem|文件系统与热重载模块"
    "font.md|mod_font|字体模块"
    "gpgpu.md|mod_gpgpu|GPU 计算模块"
    "graphics.md|mod_graphics|图形渲染模块"
    "i18n.md|mod_i18n|本地化模块"
    "ik.md|mod_ik|逆运动学模块"
    "image.md|mod_image|图像模块"
    "inventory.md|mod_inventory|背包 / 物品栏模块"
    "joystick.md|mod_joystick|手柄模块"
    "keyboard.md|mod_keyboard|键盘模块"
    "map.md|mod_map|Tilemap模块"
    "math.md|mod_math|数学模块"
    "model3d.md|mod_model3d|3D 模型模块"
    "mouse.md|mod_mouse|鼠标模块"
    "network.md|mod_network|网络模块"
    "particles.md|mod_particles|粒子模块"
    "pixelworld.md|mod_pixelworld|像素物质世界模块"
    "pixelworld_replay.md|mod_pixelworld_replay|PixelWorld 确定性重放模块"
    "pixelworld_streaming.md|mod_pixelworld_streaming|PixelWorld 兴趣区 Streaming 模块"
    "pixelworld_editor.md|mod_pixelworld_editor|PixelWorld Catalog 编辑器模块"
    "physics.md|mod_physics|物理模块（Box2D + Box3D + 布料 + 流体）"
    "plugins.md|mod_plugins|原生插件模块"
    "procgen.md|mod_procgen|程序化生成模块"
    "rpg.md|mod_rpg|RPG 系统模块"
    "rx.md|mod_rx|响应式编程模块"
    "scene.md|mod_scene|声明式场景树模块"
    "sound.md|mod_sound|声音数据模块"
    "spatial.md|mod_spatial|空间索引模块"
    "spritestack.md|mod_spritestack|Sprite-Stacking 伪 3D"
    "system.md|mod_system|系统信息模块"
    "tensor.md|mod_tensor|张量模块"
    "thread.md|mod_thread|线程与异步模块"
    "timer.md|mod_timer|计时器模块"
    "touch.md|mod_touch|触摸模块"
    "ui.md|mod_ui|声明式 UI模块"
    "voxel.md|mod_voxel|体素（Voxel）"
    "weather.md|mod_weather|天气模块"
    "window.md|mod_window|窗口模块")
foreach(_doc_spec IN LISTS _doc_module_pages)
    string(REPLACE "|" ";" _doc_parts "${_doc_spec}")
    list(GET _doc_parts 0 _doc_name)
    list(GET _doc_parts 1 _doc_id)
    list(GET _doc_parts 2 _doc_title)
    file(READ "${DOC_TOP_DIR}/usr/modules/${_doc_name}" _doc_content
        ENCODING UTF-8)
    file(WRITE "${_doc_pages_dir}/docs/usr/modules/${_doc_name}"
        "\\page ${_doc_id} ${_doc_title}\n\n${_doc_content}")
endforeach()

# ---------------------------------------------------------------------------
# Static sidebar navigation (plain HTML, no JavaScript).
#
# The doxygen treeview is rendered by JavaScript, which can be unavailable in
# embedded browsers; the static sidebar below is baked into every page header
# so "home / user guide / module handbook" is always visible and clickable.
# ---------------------------------------------------------------------------
set(_doc_sidebar_groups
    "usr_env|运行环境与输入|mod_window,mod_event,mod_rx,mod_timer,mod_system,mod_keyboard,mod_mouse,mod_touch,mod_joystick"
    "usr_assets|资源与 I/O|mod_filesystem,mod_data,mod_image,mod_font,mod_i18n,mod_sound,mod_audio,mod_model3d,mod_network"
    "usr_gameplay|游戏玩法|mod_entity,mod_physics,mod_map,mod_particles,mod_animation,mod_climbing,mod_rpg,mod_inventory,mod_cardgame,mod_building,mod_procgen"
    "usr_scene|表现与场景|mod_graphics,mod_voxel,mod_spritestack,mod_daynight,mod_weather,mod_ui,mod_scene"
    "usr_advanced|高级能力|mod_math,mod_spatial,mod_editor,mod_ik,mod_gpgpu,mod_tensor,mod_thread,mod_plugins,mod_demo")

set(_doc_module_titles "")
foreach(_doc_spec IN LISTS _doc_module_pages)
    string(REPLACE "|" ";" _doc_parts "${_doc_spec}")
    list(GET _doc_parts 1 _doc_id)
    list(GET _doc_parts 2 _doc_title)
    list(APPEND _doc_module_titles "${_doc_id}=${_doc_title}")
endforeach()

set(_doc_sidebar_html
    "<a href=\"index.html\">首页</a>"
    "<a href=\"usr_guide.html\">用户指南</a>"
    "<details open><summary>模块使用手册</summary>")
foreach(_doc_group IN LISTS _doc_sidebar_groups)
    string(REPLACE "|" ";" _doc_gparts "${_doc_group}")
    list(GET _doc_gparts 0 _doc_cat_id)
    list(GET _doc_gparts 1 _doc_cat_title)
    list(GET _doc_gparts 2 _doc_cat_mods)
    string(REPLACE "," ";" _doc_cat_mod_list "${_doc_cat_mods}")
    list(APPEND _doc_sidebar_html
        "<details><summary><a href=\"${_doc_cat_id}.html\">${_doc_cat_title}</a></summary>")
    foreach(_doc_mid IN LISTS _doc_cat_mod_list)
        set(_doc_mtitle "${_doc_mid}")
        foreach(_doc_pair IN LISTS _doc_module_titles)
            if(_doc_pair MATCHES "^${_doc_mid}=(.*)$")
                set(_doc_mtitle "${CMAKE_MATCH_1}")
            endif()
        endforeach()
        list(APPEND _doc_sidebar_html "<a href=\"${_doc_mid}.html\">${_doc_mtitle}</a>")
    endforeach()
    list(APPEND _doc_sidebar_html "</details>")
endforeach()
list(APPEND _doc_sidebar_html
    "</details>"
    "<a href=\"usr_trimming.html\">按需裁剪模块</a>"
    "<a href=\"usr_conventions.html\">API 使用约定</a>"
    "<div class=\"ev-nav-section\">C++ API 参考</div>"
    "<a href=\"namespaces.html\">命名空间</a>"
    "<a href=\"concepts.html\">概念</a>"
    "<a href=\"annotated.html\">类</a>"
    "<a href=\"files.html\">文件</a>"
    "<a href=\"pages.html\">全部页面</a>")
string(JOIN "\n" _doc_sidebar_nav ${_doc_sidebar_html})

file(READ "${DOC_TOP_DIR}/theme/header.template.html" _doc_header_tpl ENCODING UTF-8)
string(REPLACE "<!--EV_SIDEBAR-->" "${_doc_sidebar_nav}" _doc_header "${_doc_header_tpl}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/header.html" "${_doc_header}")

set(DOC_HEADER "${CMAKE_CURRENT_BINARY_DIR}/header.html")
set(DOC_FOOTER "${DOC_TOP_DIR}/theme/footer.html")

# Doxygen inputs: C++ sources, the generated page wrappers, and the
# doxygen-only category pages. REVIEW.md is deliberately not copied, so it is
# not part of the published user documentation.
set(_doc_inputs
    "${DOC_SOURCE_ROOT}/src"
    "${_doc_pages_dir}"
    "${DOC_TOP_DIR}/dox")
string(REPLACE ";" " " DOC_INPUT "${_doc_inputs}")

set(DOC_PROJECT_LOGO "")
if(EXISTS "${DOC_TOP_DIR}/images/evengine.png")
    set(DOC_PROJECT_LOGO "${DOC_TOP_DIR}/images/evengine.png")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    @ONLY)

find_package(Doxygen QUIET)
if(NOT DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E echo
            "doxygen not found; install it (apt install doxygen / brew install doxygen / choco install doxygen) to generate API docs"
        VERBATIM)
    return()
endif()

add_custom_target(docs
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DOC_OUTPUT_DIR}"
    COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${DOC_SOURCE_ROOT}"
    COMMENT "Generating Doxygen documentation (docs/api/html)"
    VERBATIM)

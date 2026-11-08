# Stylize owns its procedural-effect attribution in source and SDK distributions.
eve_module_enabled(stylize _has_stylize_notices)
if(_has_stylize_notices)
  # Raster coverage and depth ties depend on source binary64 operation order.
  # Keep this contract scoped to the PixelFX implementation, not unrelated modules.
  file(GLOB_RECURSE _eve_pixelfx_numeric_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/pixelfx/*.cpp")
  list(APPEND _eve_pixelfx_numeric_sources "${CMAKE_CURRENT_LIST_DIR}/PixelFx.cpp")
  if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set_property(SOURCE ${_eve_pixelfx_numeric_sources} APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE ${_eve_pixelfx_numeric_sources} APPEND PROPERTY COMPILE_OPTIONS
      -ffp-contract=off -fno-fast-math)
  endif()
  install(FILES
    "${CMAKE_CURRENT_LIST_DIR}/pixelfx/LICENSE.txt"
    "${CMAKE_CURRENT_LIST_DIR}/pixelfx/NOTICE.txt"
    "${CMAKE_CURRENT_LIST_DIR}/pixelfx/FrozenOrder.LICENSE.txt"
    DESTINATION share/eve/licenses/pixelfx
    COMPONENT PixelFxNotices)
endif()

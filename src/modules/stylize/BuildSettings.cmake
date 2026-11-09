# Stylize owns its procedural-effect attribution in source and SDK distributions.
include("${CMAKE_CURRENT_LIST_DIR}/pixelfx/NumericContract.cmake")
eve_module_enabled(stylize _has_stylize_notices)
if(_has_stylize_notices)
  # Raster coverage and depth ties depend on source binary64 operation order.
  # Keep this contract scoped to the PixelFX implementation, not unrelated modules.
  file(GLOB_RECURSE _eve_pixelfx_numeric_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/pixelfx/*.cpp")
  list(APPEND _eve_pixelfx_numeric_sources "${CMAKE_CURRENT_LIST_DIR}/PixelFx.cpp")
  eve_pixelfx_preserve_binary64_order(${_eve_pixelfx_numeric_sources})
  install(FILES
    "${CMAKE_CURRENT_LIST_DIR}/pixelfx/LICENSE.txt"
    "${CMAKE_CURRENT_LIST_DIR}/pixelfx/NOTICE.txt"
    "${CMAKE_CURRENT_LIST_DIR}/pixelfx/FrozenOrder.LICENSE.txt"
    DESTINATION share/eve/licenses/pixelfx
    COMPONENT PixelFxNotices)
endif()

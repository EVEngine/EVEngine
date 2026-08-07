# Desktop-only FreePats / TiMidity share data (win32, linux, macosx).
# Install/stage layout: share/eve/timidity/{timidity.cfg,instruments/}
# Users may trim instruments/; keep timidity.cfg in sync with remaining patches.

set(_eve_timidity_src "${CMAKE_SOURCE_DIR}/third-party/timidity-freepats")
set(_eve_timidity_dst_name "share/eve/timidity")

function(eve_timidity_is_desktop _out)
  if(BUILD_PLATFORM STREQUAL "macosx" OR BUILD_PLATFORM STREQUAL "linux" OR BUILD_PLATFORM STREQUAL "win32")
    set(${_out} TRUE PARENT_SCOPE)
  else()
    set(${_out} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(eve_stage_timidity_share)
  eve_timidity_is_desktop(_eve_desktop)
  if(NOT _eve_desktop)
    return()
  endif()
  if(NOT EXISTS "${_eve_timidity_src}/timidity.cfg")
    message(STATUS "TiMidity share: no ${_eve_timidity_src}/timidity.cfg (run setup.sh to fetch FreePats)")
    return()
  endif()

  set(_dst "${CMAKE_BINARY_DIR}/${_eve_timidity_dst_name}")
  file(MAKE_DIRECTORY "${_dst}")
  file(COPY "${_eve_timidity_src}/timidity.cfg" DESTINATION "${_dst}")
  if(EXISTS "${_eve_timidity_src}/README.md")
    file(COPY "${_eve_timidity_src}/README.md" DESTINATION "${_dst}")
  endif()
  if(EXISTS "${_eve_timidity_src}/instruments")
    file(COPY "${_eve_timidity_src}/instruments" DESTINATION "${_dst}")
    message(STATUS "TiMidity share staged → ${_dst} (with instruments)")
  else()
    message(STATUS "TiMidity share staged → ${_dst} (cfg only; instruments/ missing from third-party/timidity-freepats)")
  endif()
endfunction()

function(eve_install_timidity_share)
  eve_timidity_is_desktop(_eve_desktop)
  if(NOT _eve_desktop)
    return()
  endif()
  if(NOT EXISTS "${_eve_timidity_src}/timidity.cfg")
    return()
  endif()

  install(FILES "${_eve_timidity_src}/timidity.cfg"
    DESTINATION ${_eve_timidity_dst_name}
  )
  if(EXISTS "${_eve_timidity_src}/README.md")
    install(FILES "${_eve_timidity_src}/README.md"
      DESTINATION ${_eve_timidity_dst_name}
    )
  endif()
  if(EXISTS "${_eve_timidity_src}/instruments")
    install(DIRECTORY "${_eve_timidity_src}/instruments"
      DESTINATION ${_eve_timidity_dst_name}
    )
  endif()
endfunction()

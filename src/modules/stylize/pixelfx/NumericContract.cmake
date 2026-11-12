include_guard(GLOBAL)

# Inline PixelFX arithmetic must use the same binary64 evaluation order in
# runtime, tests and reference probes. Mixed modes can also select a different
# weak/COMDAT inline definition at link time.
function(eve_pixelfx_preserve_binary64_order)
  if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS
      -ffp-contract=off -fno-fast-math)
  endif()
endfunction()

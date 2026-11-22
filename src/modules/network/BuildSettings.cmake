# Network has no Vulkan dependency. Its private Poco/OpenSSL headers can be
# classified as external without changing the Vulkan header order of graphics
# consumers of the shared eve_engine_includes interface. Keep -Werror enabled
# for Network's own code; do not suppress OpenSSL deprecation diagnostics there.
function(eve_network_external_include_roots output)
  set(_roots)
  if(EVENGINE_TP_INSTALL_PREFIX)
    list(APPEND _roots "${EVENGINE_TP_INSTALL_PREFIX}/include")
  endif()
  if(NOT EVENGINE_THIRD_PARTY_BINARY_DIR)
    list(APPEND _roots
      "${CMAKE_SOURCE_DIR}/third-party/poco/NetSSL_OpenSSL/include"
      "${CMAKE_SOURCE_DIR}/third-party/poco/Crypto/include"
      "${CMAKE_SOURCE_DIR}/third-party/poco/Util/include")
  endif()
  set(${output} "${_roots}" PARENT_SCOPE)
endfunction()

# The full test binary also contains Vulkan consumers. Classify dependency
# headers only for the standalone TLS fixture TU, in each owning test directory.
function(eve_network_external_test_headers)
  if(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    eve_network_external_include_roots(_roots)
    foreach(_root IN LISTS _roots)
      set_property(SOURCE
        "${CMAKE_SOURCE_DIR}/test/integration/network/network_http_tls_tests.cpp"
        APPEND PROPERTY COMPILE_OPTIONS "-isystem${_root}")
    endforeach()
  endif()
endfunction()

if(TARGET EVNetwork)
  eve_network_external_include_roots(_roots)
  target_include_directories(EVNetwork SYSTEM PRIVATE ${_roots})
endif()

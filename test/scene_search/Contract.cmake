# Reuse the canonical module/protocol tests in a trimmed scene host. The normal
# per-domain suite still owns these sources in full CI configurations.
if(NOT TARGET EVScene OR NOT TARGET EVDevTools OR EVENGINE_ANDROID_TEST_APP OR EVENGINE_IOS_TEST_APP)
  return()
endif()

eve_add_test_executable(scene_search_contract_test EXCLUDE_FROM_ALL
  main.cpp unit/scene/scene_tests.cpp integration/mcp-protocol/mcp_tool_catalog.cpp)
target_link_libraries(scene_search_contract_test eve_engine_includes ${EVELIBS} zeroerr ${ThirdParty}
  ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
target_compile_options(scene_search_contract_test PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/UZEROERR_NO_ASSERT>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UZEROERR_NO_ASSERT>)
if(WIN32)
  target_link_libraries(scene_search_contract_test EVBacktrace)
  target_compile_definitions(scene_search_contract_test PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
endif()
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/scene_search_contract_test_zeroerr_tests.cmake")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/scene_search_contract_test_zeroerr_tests.cmake" "# populated on focused build\n")
endif()
zeroerr_discover_tests(scene_search_contract_test LABEL "scene_search_contract_test"
  NAME_PREFIX "scene-search-contract/")

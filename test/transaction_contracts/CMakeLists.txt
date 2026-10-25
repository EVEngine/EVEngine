cmake_minimum_required(VERSION 3.21)
project(EVETransactionContracts LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr)
target_compile_definitions(zeroerr PUBLIC ZEROERR_NO_MAIN)
add_executable(transaction_contracts
    transaction_contracts_main.cpp
    "${EVE_ROOT}/test/integration/transaction-authority/editor_authority_atomic.cpp"
    "${EVE_ROOT}/src/modules/editing/EditingValue.cpp"
    "${EVE_ROOT}/src/modules/editing/EditingAuthority.cpp"
    "${EVE_ROOT}/src/modules/editing/EditingResult.cpp"
    "${EVE_ROOT}/src/engine/common/Diagnostic.cpp"
    "${EVE_ROOT}/src/engine/common/Result.cpp"
    "${EVE_ROOT}/src/engine/common/Status.cpp"
    "${EVE_ROOT}/src/engine/common/Identity.cpp")
target_include_directories(transaction_contracts PRIVATE
    "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(transaction_contracts PRIVATE zeroerr)
if(MSVC)
    # Same DLL STL-interface exclusions as the engine's MSVC policy.
    target_compile_options(transaction_contracts PRIVATE /utf-8 /EHsc /W4 /WX /wd4251 /wd4275)
endif()
enable_testing()
add_test(NAME transaction.authority.contracts COMMAND transaction_contracts)

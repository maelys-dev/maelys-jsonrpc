# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MAELYS_JSONRPC_RELEASE_VERSION LIMIT_COUNT 1)
project(maelys_jsonrpc VERSION ${MAELYS_JSONRPC_RELEASE_VERSION} LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MAELYS_JSONRPC_TOP_LEVEL ON)
else()
    set(MAELYS_JSONRPC_TOP_LEVEL OFF)
endif()
option(MAELYS_JSONRPC_BUILD_TESTS "Build tests" ${MAELYS_JSONRPC_TOP_LEVEL})
option(MAELYS_JSONRPC_WERROR "Treat warnings as errors" OFF)
if(DEFINED ENV{MAELYS_DEPENDENCIES_DIR})
    set(MAELYS_JSONRPC_JSON_DEFAULT "$ENV{MAELYS_DEPENDENCIES_DIR}/maelys-json")
else()
    set(MAELYS_JSONRPC_JSON_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../maelys-json")
endif()
set(MAELYS_JSON_DIR "${MAELYS_JSONRPC_JSON_DEFAULT}" CACHE PATH "maelys-json checkout")
if(NOT TARGET maelys::json)
    if(EXISTS "${MAELYS_JSON_DIR}/CMakeLists.txt")
        set(MAELYS_JSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${MAELYS_JSON_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependency" EXCLUDE_FROM_ALL)
    else()
        find_package(maelys-json 0.3 CONFIG QUIET)
        if(NOT TARGET maelys::json)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(MAELYS_JSON REQUIRED IMPORTED_TARGET maelys-json>=0.3.0)
            add_library(maelys::json ALIAS PkgConfig::MAELYS_JSON)
        endif()
    endif()
endif()
set(MAELYS_JSONRPC_SOURCES
    src/reader.c
    src/message.c
    src/calls.c
    src/result.c)
add_library(maelys_jsonrpc STATIC ${MAELYS_JSONRPC_SOURCES})
add_library(maelys::jsonrpc ALIAS maelys_jsonrpc)
target_include_directories(maelys_jsonrpc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(maelys_jsonrpc PUBLIC
    $<BUILD_INTERFACE:maelys::json> $<INSTALL_INTERFACE:maelys::json>)
target_compile_options(maelys_jsonrpc PRIVATE -Wall -Wextra -Wpedantic -Wshadow
    -Wstrict-prototypes -Wmissing-prototypes -Wconversion -Wsign-conversion
    -Wcast-qual -Wformat=2 -Wvla -Wundef $<$<BOOL:${MAELYS_JSONRPC_WERROR}>:-Werror>)
set_target_properties(maelys_jsonrpc PROPERTIES OUTPUT_NAME maelys-jsonrpc EXPORT_NAME jsonrpc VERSION ${PROJECT_VERSION})
if(MAELYS_JSONRPC_BUILD_TESTS)
    enable_testing()
    add_executable(maelys_jsonrpc_tests tests/main.c tests/test_reader.c tests/test_message.c tests/test_calls.c tests/test_dialect.c)
    target_link_libraries(maelys_jsonrpc_tests PRIVATE maelys::jsonrpc)
    add_test(NAME maelys_jsonrpc COMMAND maelys_jsonrpc_tests)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(MAELYS_JSONRPC_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/maelys-jsonrpc")
configure_file(pkgconfig/maelys-jsonrpc.pc.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/maelys-jsonrpc.pc" @ONLY)
configure_package_config_file(cmake/maelys-jsonrpc-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/maelys-jsonrpc-config.cmake"
    INSTALL_DESTINATION "${MAELYS_JSONRPC_CMAKE_DIR}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/maelys-jsonrpc-config-version.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(TARGETS maelys_jsonrpc EXPORT maelys_jsonrpc_targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/maelys/jsonrpc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/maelys)
install(FILES include/maelys/jsonrpc.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/maelys)
install(EXPORT maelys_jsonrpc_targets NAMESPACE maelys:: FILE maelys-jsonrpc-targets.cmake DESTINATION "${MAELYS_JSONRPC_CMAKE_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/maelys-jsonrpc-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/maelys-jsonrpc-config-version.cmake" DESTINATION "${MAELYS_JSONRPC_CMAKE_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/maelys-jsonrpc.pc" DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

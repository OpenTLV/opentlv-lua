# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel

cmake_minimum_required(VERSION 3.24)
project(OpenTLVLua LANGUAGES C)

set(LUA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}")
set(LUA_BINARY "${CMAKE_CURRENT_BINARY_DIR}")
include(cmake/GenerateVersion.cmake)
add_custom_target(opentlv_lua_version
    COMMAND "${CMAKE_COMMAND}" "-DLUA_SOURCE=${LUA_SOURCE}" "-DLUA_BINARY=${LUA_BINARY}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/lua-version.json"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/lua-version.h"
    VERBATIM)

include(cmake/Core.cmake)

# Only the headers are required on every platform; LUA_LIBRARIES is only
# needed (and only linked) on Windows, so this deliberately does not pass
# REQUIRED, and does not fail here if a Lua install (such as one built by
# leafo/gh-actions-lua into a workspace-local prefix) exposes headers but no
# library CMake's FindLua recognizes.
find_package(Lua)
if(NOT LUA_INCLUDE_DIR)
    message(FATAL_ERROR "Lua headers not found; set LUA_INCLUDE_DIR.")
endif()

add_library(opentlv_lua MODULE
    src/module.c
    src/common.c
    src/common.h
    src/codec.c
    src/codec.h
    src/codec_builtin.c
    src/error.c
    src/error.h
    src/format.c
    src/format.h
    src/formats/fixed.c
    src/reader.c
    src/reader.h
    src/query.c
    src/query.h
    src/path_query.h
    src/query_schema.h
    src/program.c
    src/program.h
    src/document.h
    src/schema.c
    src/schema.h
    src/writer.c
    src/writer.h
    src/visitor.c
    src/visitor.h
    src/compat.h
    src/document.c
    src/formats/ber.c
    src/formats/cer.c
    src/formats/der.c
    src/formats/bluetooth_ltv.c
    src/formats/emv.c
    src/formats/nfc_type2.c
    src/formats/lldp.c
)

target_include_directories(opentlv_lua PRIVATE ${LUA_INCLUDE_DIR}
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(opentlv_lua opentlv_lua_version)
target_link_libraries(opentlv_lua PRIVATE OpenTLV::tlv)
target_compile_features(opentlv_lua PRIVATE c_std_99)
option(OPENTLV_WARNINGS_AS_ERRORS "Treat OpenTLV compiler warnings as errors" ON)
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 18)
    message(FATAL_ERROR "OpenTLV requires Clang 18 or newer.")
endif()
target_compile_options(opentlv_lua PRIVATE
    "$<$<C_COMPILER_ID:Clang>:-Wall;-Wextra;-Wpedantic;-Wstrict-prototypes>"
    "$<$<AND:$<BOOL:${OPENTLV_WARNINGS_AS_ERRORS}>,$<C_COMPILER_ID:GNU,Clang>>:-Werror>"
    "$<$<AND:$<BOOL:${OPENTLV_WARNINGS_AS_ERRORS}>,$<C_COMPILER_ID:MSVC>>:/WX>"
)
# A shared Core needs its runtime beside the module on Windows.
get_target_property(_core_type OpenTLV::tlv TYPE)
if(WIN32 AND _core_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET opentlv_lua POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:OpenTLV::tlv>" "$<TARGET_FILE_DIR:opentlv_lua>"
        VERBATIM)
endif()

if(WIN32)
    # Windows DLLs must resolve every imported symbol at link time; there is
    # no ELF/Mach-O-style "leave it undefined, resolve it against the
    # process that loads this module" for PE. So on Windows this must link
    # against the same shared lua5x.dll the embedding Lua interpreter uses:
    # two independently linked copies of the Lua runtime sharing one
    # lua_State corrupt memory on this module's calls back into Lua (see
    # README.md's Build section).
    #
    # find_package(Lua)'s own search above only checks standard system
    # locations, so it finds nothing for a Lua installed to a non-standard
    # prefix (for example one leafo/gh-actions-lua built into a
    # workspace-local directory in CI). Every such layout this binding has
    # actually been built against — gh-actions-lua, the upstream Lua
    # Makefile's own "install" target, vcpkg — puts "include" and "lib" (or
    # "bin", for some Windows Lua distributions' import libraries) as
    # siblings under one prefix, so the already-resolved LUA_INCLUDE_DIR
    # (unlike LUA_LIBDIR/LUA_LIBDIR_FILE, LuaRocks' "command" build type
    # does not expose any substitutable variable for the Lua library
    # directly; see opentlv-scm-1.rockspec) locates it without needing a
    # second, separately-resolved hint.
    if(NOT LUA_LIBRARIES)
        get_filename_component(_opentlv_lua_prefix "${LUA_INCLUDE_DIR}" DIRECTORY)
        find_library(LUA_LIBRARY
            NAMES lua54 lua5.4 lua-5.4 lua53 lua5.3 lua-5.3 lua52 lua5.2 lua-5.2 lua51 lua5.1 lua-5.1 lua
            PATHS "${_opentlv_lua_prefix}/lib" "${_opentlv_lua_prefix}/bin"
            NO_DEFAULT_PATH)
        if(LUA_LIBRARY)
            set(LUA_LIBRARIES "${LUA_LIBRARY}")
        endif()
    endif()
    if(NOT LUA_LIBRARIES)
        message(FATAL_ERROR "Lua library not found next to ${LUA_INCLUDE_DIR}; set LUA_LIBRARY explicitly.")
    endif()
    target_link_libraries(opentlv_lua PRIVATE ${LUA_LIBRARIES})
elseif(APPLE)
    # Deliberately not linked against LUA_LIBRARIES here either, for the
    # same reason as the Linux case below; -undefined dynamic_lookup defers
    # every lua_*/luaL_* symbol to load time instead of requiring them at
    # link time, since Mach-O otherwise resolves imports as strictly as PE.
    target_link_options(opentlv_lua PRIVATE -undefined dynamic_lookup)
endif()
# On Linux, GNU ld already permits a shared object to keep symbols
# undefined at link time; they resolve when the module is require()'d,
# against the same already-running Lua interpreter process. Deliberately
# not linking LUA_LIBRARIES here is what makes that the only copy of the
# Lua runtime in the process: linking it (as Windows must) would compile a
# second, independent copy of lua_pcall(), luaV_execute() and friends
# directly into this module, sharing the interpreter's lua_State with a
# runtime that never allocated it — the same corruption the Windows comment
# above describes, not something specific to how Windows loads DLLs.

# require("opentlv._core") looks for a shared object literally named
# opentlv/_core, with no platform library prefix, and expects the
# platform's native module extension except on macOS, where Lua modules are
# still named .so by ecosystem convention (LuaRocks does the same), not the
# default .dylib CMake would otherwise pick for a MODULE library.
# require("opentlv") itself resolves to lua/opentlv/init.lua, which returns
# this module unchanged; see src/module.c's file comment.
set_target_properties(opentlv_lua PROPERTIES
    OUTPUT_NAME _core
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/opentlv"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/opentlv"
    PREFIX ""
)
foreach(_lua_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(opentlv_lua PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${_lua_config} "${CMAKE_CURRENT_BINARY_DIR}/opentlv"
        RUNTIME_OUTPUT_DIRECTORY_${_lua_config} "${CMAKE_CURRENT_BINARY_DIR}/opentlv")
endforeach()
if(APPLE)
    set_target_properties(opentlv_lua PROPERTIES SUFFIX ".so")
elseif(WIN32)
    # MSVC does not export any symbols from a DLL by default;
    # luaopen_opentlv__core needs to be visible for the Lua runtime's
    # require() to find it.
    set_target_properties(opentlv_lua PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

option(OPENTLV_BUILD_LUA_TESTS "Register the busted test suite and examples for the Lua binding" ON)
if(OPENTLV_BUILD_LUA_TESTS)
    enable_testing()
    add_library(opentlv_lua_test_allocator MODULE tests/failing_allocator.c)
    add_dependencies(opentlv_lua opentlv_lua_test_allocator)
    target_include_directories(opentlv_lua_test_allocator PRIVATE ${LUA_INCLUDE_DIR} src)
    set_target_properties(opentlv_lua_test_allocator PROPERTIES
        OUTPUT_NAME opentlv_test_allocator PREFIX "")
    if(WIN32)
        target_link_libraries(opentlv_lua_test_allocator PRIVATE ${LUA_LIBRARIES})
        set_target_properties(opentlv_lua_test_allocator PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    elseif(APPLE)
        target_link_options(opentlv_lua_test_allocator PRIVATE -undefined dynamic_lookup)
        set_target_properties(opentlv_lua_test_allocator PROPERTIES SUFFIX ".so")
    endif()
    find_program(OPENTLV_LUA_EXECUTABLE NAMES lua lua5.4 lua5.3 lua5.1 luajit)
    find_program(OPENTLV_BUSTED_EXECUTABLE busted)

    # require("opentlv") resolves through lua/opentlv/init.lua (source tree,
    # nothing to build), so tests and examples need LUA_PATH in addition to
    # the LUA_CPATH entry for the compiled opentlv._core module; the
    # trailing ";;" keeps each interpreter's own default search path too.
    set(_lua_path "${CMAKE_CURRENT_SOURCE_DIR}/lua/?.lua;${CMAKE_CURRENT_SOURCE_DIR}/lua/?/init.lua;;")
    set(_lua_cpath "$<TARGET_FILE_DIR:opentlv_lua>/../?$<TARGET_FILE_SUFFIX:opentlv_lua>;$<TARGET_FILE_DIR:opentlv_lua_test_allocator>/?$<TARGET_FILE_SUFFIX:opentlv_lua_test_allocator>;;")
    # ENVIRONMENT is a CMake list; preserve separators inside each Lua path.
    string(REPLACE ";" "\\;" _lua_path "${_lua_path}")
    string(REPLACE ";" "\\;" _lua_cpath "${_lua_cpath}")
    set(_lua_test_environment "LUA_PATH=${_lua_path};LUA_CPATH=${_lua_cpath}")

    if(OPENTLV_BUSTED_EXECUTABLE)
        add_test(NAME Integration_lua
            COMMAND ${OPENTLV_BUSTED_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        set_tests_properties(Integration_lua PROPERTIES
            LABELS "integration;lua"
            ENVIRONMENT "${_lua_test_environment}")
    else()
        message(STATUS "busted unavailable; skipping the full Lua binding test suite.")
    endif()

    if(OPENTLV_LUA_EXECUTABLE)
        add_test(NAME Integration_lua_version
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/version.lua"
                "${CMAKE_CURRENT_BINARY_DIR}/lua-version.json")
        set_tests_properties(Integration_lua_version PROPERTIES
            LABELS "integration;lua" ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_lua_query_document
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/query_document_spec.lua")
        add_test(NAME Integration_lua_query_document_allocator
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/query_document_spec.lua" --allocator)
        set_tests_properties(Integration_lua_query_document Integration_lua_query_document_allocator
            PROPERTIES LABELS "integration;lua" ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_lua_codec
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/codec_spec.lua")
        add_test(NAME Integration_lua_codec_allocator
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/codec_spec.lua" --allocator)
        add_test(NAME Integration_example_lua_codec
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/examples/codec.lua")
        set_tests_properties(Integration_lua_codec Integration_lua_codec_allocator
            Integration_example_lua_codec PROPERTIES
            LABELS "integration;lua"
            ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_lua_schema
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/schema_spec.lua")
        set_tests_properties(Integration_lua_schema PROPERTIES
            LABELS "integration;lua"
            ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_example_lua_schema
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/examples/schema.lua")
        set_tests_properties(Integration_example_lua_schema PROPERTIES
            LABELS "integration;examples;lua"
            ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_lua_visitor
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/visitor_spec.lua")
        set_tests_properties(Integration_lua_visitor PROPERTIES
            LABELS "integration;lua"
            ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_lua_visitor_allocator
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/visitor_spec.lua" --allocator)
        set_tests_properties(Integration_lua_visitor_allocator PROPERTIES
            LABELS "integration;lua"
            ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_lua_writer
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/writer_spec.lua")
        set_tests_properties(Integration_lua_writer PROPERTIES
            LABELS "integration;lua"
            ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_lua_nfc_type2
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/nfc_type2.lua")
        set_tests_properties(Integration_lua_nfc_type2 PROPERTIES
            LABELS "integration;lua" ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_lua_components
            COMMAND ${OPENTLV_LUA_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/components.lua")
        set_tests_properties(Integration_lua_components PROPERTIES
            LABELS "integration;lua"
            ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_example_lua_quick_start
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/examples/quick_start.lua")
        set_tests_properties(Integration_example_lua_quick_start PROPERTIES
            LABELS "integration;examples;lua" ENVIRONMENT "${_lua_test_environment}"
            PASS_REGULAR_EXPRESSION "Hello, world!")
        add_test(NAME Integration_example_lua_parse
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/examples/parse.lua")
        set_tests_properties(Integration_example_lua_parse PROPERTIES
            LABELS "integration;examples;lua"
            ENVIRONMENT "${_lua_test_environment}")
    else()
        message(STATUS "lua interpreter not found; skipping the Lua binding examples.")
    endif()
    if(WIN32 AND _core_type STREQUAL "SHARED_LIBRARY")
        # Lua uses LoadLibraryEx without LOAD_WITH_ALTERED_SEARCH_PATH, so
        # staging tlv.dll beside _core.dll alone does not make it discoverable.
        get_property(_lua_tests DIRECTORY PROPERTY TESTS)
        if(_lua_tests)
            set_tests_properties(${_lua_tests} PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:opentlv_lua>")
        endif()
    endif()
endif()

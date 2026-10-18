# Experimental Lua binding: the OpenTLV C core plus a native extension
# module (src/) exposing Reader, Writer, Tree Writer, Schema, Query, Document,
# Element, Tag and traversal to Lua 5.1
# through 5.4 and LuaJIT, and a one-line pure-Lua entry point (lua/) that
# `require("opentlv")` resolves to — the same native/pure split as the
# Python opentlv-core/opentlv and Rust opentlv-sys/opentlv packages.
# Included only when OPENTLV_BUILD_LUA=ON, so native C/C++ builds never see
# it.
#
#   cmake -S . -B build-lua -DOPENTLV_BUILD_LUA=ON -DCMAKE_BUILD_TYPE=Release \
#       -DOPENTLV_BUILD_CXX=OFF -DOPENTLV_BUILD_TESTS=OFF \
#       -DOPENTLV_BUILD_EXAMPLES=OFF
#   cmake --build build-lua --target opentlv_lua

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
)
if(OPENTLV_DOCUMENT)
    target_sources(opentlv_lua PRIVATE src/document.c)
endif()
if(OPENTLV_FORMAT_BER)
    target_sources(opentlv_lua PRIVATE src/formats/ber.c)
endif()
if(OPENTLV_FORMAT_CER)
    target_sources(opentlv_lua PRIVATE src/formats/cer.c)
endif()
if(OPENTLV_FORMAT_DER)
    target_sources(opentlv_lua PRIVATE src/formats/der.c)
endif()
if(OPENTLV_BLUETOOTH)
    target_sources(opentlv_lua PRIVATE src/formats/bluetooth_ltv.c)
endif()
if(OPENTLV_EMV)
    target_sources(opentlv_lua PRIVATE src/formats/emv.c)
endif()
if(OPENTLV_NFC)
    target_sources(opentlv_lua PRIVATE src/formats/nfc_type2.c)
endif()
if(OPENTLV_LLDP)
    target_sources(opentlv_lua PRIVATE src/formats/lldp.c)
endif()

target_include_directories(opentlv_lua PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(opentlv_lua PRIVATE tlv)
opentlv_configure_compiler(opentlv_lua)

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

    # The existing suite exercises all original presets. Reduced builds use
    # the configuration-aware smoke test below.
    if(OPENTLV_BUSTED_EXECUTABLE AND OPENTLV_FORMAT_BER AND OPENTLV_FORMAT_CER
       AND OPENTLV_FORMAT_DER AND OPENTLV_BLUETOOTH)
        add_test(NAME Integration_lua
            COMMAND ${OPENTLV_BUSTED_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        set_tests_properties(Integration_lua PROPERTIES
            LABELS "integration;lua"
            ENVIRONMENT "${_lua_test_environment}")
    else()
        message(STATUS "busted or required presets unavailable; skipping the full Lua binding test suite.")
    endif()

    if(OPENTLV_LUA_EXECUTABLE)
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
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/components.lua"
                $<BOOL:${OPENTLV_FORMAT_BER}> $<BOOL:${OPENTLV_FORMAT_CER}>
                $<BOOL:${OPENTLV_FORMAT_DER}> $<BOOL:${OPENTLV_BLUETOOTH}>
                $<BOOL:${OPENTLV_LLDP}> $<BOOL:${OPENTLV_EMV}> $<BOOL:${OPENTLV_NFC}> $<BOOL:${OPENTLV_DOCUMENT}>)
        set_tests_properties(Integration_lua_components PROPERTIES
            LABELS "integration;lua"
            ENVIRONMENT "${_lua_test_environment}")
        add_test(NAME Integration_example_lua_quick_start
            COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/examples/quick_start.lua")
        set_tests_properties(Integration_example_lua_quick_start PROPERTIES
            LABELS "integration;examples;lua" ENVIRONMENT "${_lua_test_environment}"
            PASS_REGULAR_EXPRESSION "Hello, world!")
        if(OPENTLV_FORMAT_BER)
            foreach(example parse)
                add_test(NAME Integration_example_lua_${example}
                    COMMAND ${OPENTLV_LUA_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/examples/${example}.lua")
                set_tests_properties(Integration_example_lua_${example} PROPERTIES
                    LABELS "integration;examples;lua"
                    ENVIRONMENT "${_lua_test_environment}")
            endforeach()
        endif()
    else()
        message(STATUS "lua interpreter not found; skipping the Lua binding examples.")
    endif()
endif()

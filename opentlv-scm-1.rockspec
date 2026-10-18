package = "opentlv"
version = "scm-1"

source = {
   url = "git+https://github.com/marekcingel/OpenTLV.git"
}

description = {
   summary = "Lua bindings to the OpenTLV C library",
   detailed = [[
      Binds the OpenTLV C Reader, Writer, Tree Writer, Element, Tag and preorder
      tree traversal directly to Lua 5.1 through 5.4 and LuaJIT. Experimental;
      see https://marekcingel.github.io/OpenTLV/development/lua/.
   ]],
   homepage = "https://github.com/marekcingel/OpenTLV",
   license = "MIT"
}

dependencies = {
   "lua >= 5.1"
}

-- Drives the repository's own CMake build (this rockspec's directory is
-- bindings/lua/, two levels below the repository root) instead of LuaRocks'
-- "cmake" build type, since the CMakeLists.txt that defines the tlv target
-- this module links lives at the repository root, not here. Run
-- `luarocks make` from this directory.
build = {
   type = "command",
   -- Only LUA_INCLUDE_DIR is passed: CMakeLists.txt only requires (and
   -- only links) a Lua library on Windows, resolving lua_*/luaL_* symbols
   -- at module-load time everywhere else instead (see its own comments for
   -- why), and locates that Windows library itself from LUA_INCLUDE_DIR's
   -- sibling "lib"/"bin" directory rather than needing a second variable
   -- here. LuaRocks' "command" build type has no substitutable variable
   -- for the Lua library's directory or exact file (LUA_LIBDIR,
   -- LUA_LIBDIR_FILE and LUALIB, which its "make"/"cmake" build types get
   -- internally, are not exposed to build_command/install_command; using
   -- any of them here is a silent no-op, warned as "unmatched variable"
   -- and substituted as empty), so this does not attempt to pass one.
   build_command = "cmake -S ../.. -B build -DCMAKE_BUILD_TYPE=Release -DOPENTLV_BUILD_LUA=ON " ..
      "-DOPENTLV_BUILD_CXX=OFF -DOPENTLV_BUILD_TESTS=OFF " ..
      "-DOPENTLV_BUILD_EXAMPLES=OFF -DOPENTLV_BUILD_LUA_TESTS=OFF " ..
      "-DLUA_INCLUDE_DIR=$(LUA_INCDIR) && " ..
      "cmake --build build --target opentlv_lua --config Release",
   -- Installs both halves of the native/pure split (see README.md):
   -- opentlv._core (the compiled module) and lua/opentlv/init.lua, the
   -- one-line pure-Lua file require("opentlv") actually resolves to.
   install_command = "cmake -E make_directory $(LIBDIR)/opentlv && " ..
      "cmake -E copy build/bindings/lua/opentlv/_core.$(LIB_EXTENSION) $(LIBDIR)/opentlv/_core.$(LIB_EXTENSION) && " ..
      "cmake -E make_directory $(LUADIR)/opentlv && " ..
      "cmake -E copy lua/opentlv/init.lua $(LUADIR)/opentlv/init.lua"
}

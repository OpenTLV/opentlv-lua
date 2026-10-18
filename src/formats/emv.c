// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "../format.h"
#include <tlv/builtins/emv/format.h>

void opentlv_lua_register_emv(lua_State* L) {
    opentlv_lua_register_builtin(L, tlv_format_emv, 0, "emv");
}

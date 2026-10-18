// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "../format.h"
#include <tlv/builtins/bluetooth/bluetooth_ltv.h>

void opentlv_lua_register_bluetooth_ltv(lua_State* L) {
    opentlv_lua_register_builtin(L, tlv_format_bluetooth_ltv, 0, "bluetooth_ltv");
}

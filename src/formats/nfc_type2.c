// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "../format.h"
#include <tlv/builtins/nfc/type2.h>

void opentlv_lua_register_nfc_type2(lua_State* L) {
    opentlv_lua_register_builtin(L, tlv_format_nfc_type2, 0, "nfc_type2");
}

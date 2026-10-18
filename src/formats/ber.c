// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "../format.h"
#include <tlv/builtins/asn1/ber.h>

void opentlv_lua_register_ber(lua_State* L) {
    opentlv_lua_register_builtin(L, tlv_format_ber, 0, "ber");
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marek Cingel

#include "../format.h"
#include <tlv/builtins/asn1/der.h>

void opentlv_lua_register_der(lua_State* L) {
    opentlv_lua_register_builtin(L, tlv_format_der, 1, "der");
}

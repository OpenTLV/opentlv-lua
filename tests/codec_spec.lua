-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 Marek Cingel

local tlv = require("opentlv")
local c = tlv.codecs
local b = string.char
local checked = {}
local function same(a, z)
    if type(a) ~= "table" or type(z) ~= "table" then return a == z end
    for k, v in pairs(a) do if not same(v, z[k]) then return false end end
    for k, v in pairs(z) do if not same(v, a[k]) then return false end end
    return true
end
local function round(codec, wire, value)
    assert(codec, "missing codec")
    local decoded = codec:decode(wire)
    assert(same(decoded, value), "unexpected decoded value")
    assert(codec:encode(value) == wire, "unexpected encoded value")
    checked[codec] = true
end
local function fails(fn, code)
    local ok, err = pcall(fn)
    assert(not ok, "expected failure")
    if code then
        assert(type(err) == "table" and err.code == code)
        assert(type(err.message) == "string")
        assert(err.codec_detail ~= nil)
        assert(tostring(err):find(err.message, 1, true))
    end
    return err
end

round(c.uint8, b(255), 255)
round(c.uint16_be, b(0x12, 0x34), 4660)
round(c.uint16_le, b(0x34, 0x12), 4660)
round(c.uint32_be, b(255, 255, 255, 255), 4294967295)
round(c.uint32_le, b(0x78, 0x56, 0x34, 0x12), 305419896)
round(c.int64_minimal_be, b(255), -1)
round(c.bytes, "a\0b", "a\0b")
round(c.ipv4, b(192, 0, 2, 1), b(192, 0, 2, 1))
round(c.ipv4_list, b(192, 0, 2, 1, 127, 0, 0, 1), {b(192, 0, 2, 1), b(127, 0, 0, 1)})
assert(#c.ipv4_list:decode("") == 0 and c.ipv4_list:encode({}) == "")
round(c.dhcpv4.message_type, b(255), 255)
round(c.lldp.ttl, b(0, 120), 120)
round(c.lldp.text, "arbitrary\255", "arbitrary\255")
round(c.bluetooth.uuid16, b(0x0D, 0x18), 0x180D)
round(c.bluetooth.uuid32, b(1, 0, 0, 0), 1)

local unsigned = c.number {encoding = "binary_be", width = 8}
local maximum = "18446744073709551615"
round(unsigned, string.rep(b(255), 8), maximum)
for _, decimal in ipairs({"9223372036854775807", "-9223372036854775808"}) do
    local wire = c.int64_minimal_be:encode(decimal)
    assert(c.int64_minimal_be:encode(c.int64_minimal_be:decode(wire)) == wire)
    if not math.type then assert(c.int64_minimal_be:decode(wire) == decimal) end
end
round(c.number {encoding = "binary_le", width = 2}, b(0x34, 0x12), 4660)
round(c.number {encoding = "bcd", width = 2, digits = 4}, b(0x12, 0x34), 1234)
round(c.digits {width = 3}, b(0x00, 0x12, 0xFF), "0012")
round(c.digits {}, "", "")
round(c.text {width = 5, zero_padding = true}, "abc\0\0", "abc")
round(c.text {alphabet = "ascii_alnum"}, "A123", "A123")
fails(function() c.text {alphabet = "ascii_alnum"}:encode("a b") end, tlv.errors.INVALID_VALUE)
fails(function() c.uint8:decode("") end, tlv.errors.INVALID_VALUE)
fails(function() c.int64_minimal_be:decode(b(0, 1)) end, tlv.errors.INVALID_VALUE)
fails(function() c.ipv4_list:decode("abc") end, tlv.errors.INVALID_VALUE)
for _, value in ipairs({-1, 256, 1.5, "256", "", "12x", "1\0", true, {}}) do
    fails(function() c.uint8:encode(value) end)
end
for _, value in ipairs({"18446744073709551616", "-1", "1e3", " 1", 1/0, 0/0, 2^64}) do
    fails(function() unsigned:encode(value) end)
end
fails(function() c.int64_minimal_be:encode("9223372036854775808") end)
fails(function() c.int64_minimal_be:encode("-9223372036854775809") end)
fails(function() c.bytes:encode(123) end)
fails(function() c.bytes:decode(123) end)
fails(function() c.number {width = 1}:encode(256) end, tlv.errors.INVALID_VALUE)

if c.asn1 then
    local a = c.asn1
    round(a.boolean, b(255), true)
    round(a.integer, b(128), -128)
    round(a.enumerated, b(1), 1)
    round(a.bit_string, b(3, 0xA0), {unused_bits = 3, data = b(0xA0)})
    round(a.octet_string, "\0abc", "\0abc")
    round(a.null, "", nil)
    round(a.oid, b(42, 3), {1, 2, 3})
    round(a.relative_oid, b(1, 2, 3), {1, 2, 3})
    round(a.bmp_string, b(0, 65), b(0, 65))
    round(a.universal_string, b(0, 0, 0, 65), b(0, 0, 0, 65))
    local date = {year = 2026, month = 9, day = 30}
    local time = {hour = 12, minute = 34, second = 56}
    local datetime = {year = 2026, month = 9, day = 30, hour = 12, minute = 34, second = 56}
    round(a.date, "20260930", date)
    round(a.time_of_day, "123456", time)
    round(a.date_time, "20260930123456", datetime)
    round(a.utc_time, "260930123456Z", datetime)
    local fractional = {year = 2026, month = 9, day = 30, hour = 12, minute = 34,
                        second = 56, fraction_digits = "123"}
    round(a.generalized_time, "20260930123456.123Z", fractional)
    fractional.fraction_digits = ""
    round(a.generalized_time, "20260930123456Z", fractional)
    round(a.oid_iri, "/ISO/abc", {"ISO", "abc"})
    round(a.relative_oid_iri, "ISO/abc", {"ISO", "abc"})
    for _, name in ipairs({"utf8_string", "numeric_string", "printable_string", "ia5_string",
                          "visible_string", "object_descriptor", "teletex_string", "videotex_string",
                          "graphic_string", "general_string", "time"}) do
        round(a[name], "123", "123")
    end
    round(a.duration, "1DT2H", "1DT2H")
    fails(function() a.boolean:decode(b(1)) end, tlv.errors.INVALID_VALUE)
    fails(function() a.boolean:encode(1) end)
    fails(function() a.bit_string:encode {unused_bits = 1, data = b(1)} end, tlv.errors.INVALID_VALUE)
    fails(function() a.utf8_string:decode(b(0xC0, 0x80)) end, tlv.errors.INVALID_VALUE)
    fails(function() a.oid:encode {3, 0} end, tlv.errors.INVALID_VALUE)
    fails(function() a.bmp_string:encode("a") end)
    local arcs = {}; for i = 1, 33 do arcs[i] = 1 end
    fails(function() a.relative_oid:encode(arcs) end)
    local retained = a.generalized_time:decode("20260930123456.123Z")
    collectgarbage("collect")
    assert(retained.fraction_digits == "123")
    -- Fields synthesized by metamethods remain rooted during later allocations.
    local synthetic = setmetatable({}, {__index = function(_, key)
        collectgarbage("collect")
        if key == "data" then return string.rep("x", 1000) end
        return 0
    end})
    assert(a.bit_string:encode(synthetic) == b(0) .. string.rep("x", 1000))
end

if c.lldp.chassis_id then
    local l = c.lldp
    round(l.chassis_id, b(7) .. "id", {subtype = 7, identifier = "id"})
    round(l.port_id, b(7) .. "port", {subtype = 7, identifier = "port"})
    round(l.capabilities, b(0, 5, 0, 1), {supported = 5, enabled = 1})
    round(l.management_address, b(5, 1, 192, 0, 2, 1, 2, 0, 0, 0, 3, 0),
          {address_subtype = 1, address = b(192, 0, 2, 1), interface_subtype = 2, interface_number = 3, oid = ""})
    round(l.organisation, b(1, 2, 3, 4) .. "abc", {oui = b(1, 2, 3), subtype = 4, payload = "abc"})
    fails(function() l.capabilities:encode {supported = 1, enabled = 2} end, tlv.errors.INVALID_VALUE)
end

if c.bluetooth.flags then
    local bt = c.bluetooth
    round(bt.flags, b(6), b(6))
    round(bt.local_name, "name\0", "name\0")
    round(bt.tx_power, b(252), -4)
    local uuid = b(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16)
    round(bt.uuid128, uuid:reverse(), uuid)
    round(bt.uuid16_list, b(0x0D,0x18,0x0F,0x18), {0x180D, 0x180F})
    round(bt.uuid32_list, b(1,0,0,0,2,0,0,0), {1, 2})
    round(bt.uuid128_list, uuid:reverse(), {uuid})
    round(bt.service_data16, b(0x0D,0x18).."abc", {uuid=0x180D, payload="abc", raw=b(0x0D,0x18).."abc"})
    round(bt.service_data32, b(1,0,0,0).."abc", {uuid=1, payload="abc", raw=b(1,0,0,0).."abc"})
    round(bt.service_data128, uuid:reverse().."abc", {uuid=uuid, payload="abc", raw=uuid:reverse().."abc"})
    round(bt.manufacturer_data, b(0x34,0x12).."abc", {company_id=0x1234, payload="abc", raw=b(0x34,0x12).."abc"})
    assert(bt.manufacturer_data:encode {company_id=0x1234, payload="abc"} == b(0x34,0x12).."abc")
    fails(function() bt.tx_power:encode(-128) end, tlv.errors.INVALID_VALUE)
    fails(function() bt.uuid16_list:decode("a") end, tlv.errors.INVALID_VALUE)
    fails(function() bt.flags:decode(b(6,0)) end, tlv.errors.INVALID_VALUE)
end

if c.emv then
    round(c.emv.amount, b(0,0,0,0,0x12,0x34), 1234)
    round(c.emv.find(b(0x9F,0x02)), b(0,0,0,0,0x12,0x34), 1234)
    round(c.emv.find(b(0x5A)), b(0x12,0x34,0x5F), "12345")
    round(c.emv.find(b(0x5F,0x24)), b(0x26,0x09,0x30), {year=26, month=9, day=30})
    round(c.emv.find(b(0x9F,0x21)), b(0x12,0x34,0x56), {hour=12, minute=34, second=56})
    round(c.emv.find(b(0x5F,0x57)), b(0x10), 10)
    round(c.emv.find(b(0x9F,0x27)), b(0x80), {type=2, flags=0})
    round(c.emv.find(b(0x9F,0x34)), b(1,2,3), {method=1, condition=2, result=3})
    round(c.emv.find(b(0x94)), b(8,1,2,0), {{sfi=1, first_record=1, last_record=2, offline_auth_record_count=0}})
    round(c.emv.find(b(0x82)), b(0x12,0x34), 4660)
    round(c.emv.find(b(0x9F,0x3B)), b(0x09,0x78,0x08,0x40), {978, 840})
    round(c.emv.find(b(0x81), c.emv.contexts.BHT), b(8), 8)
    assert(c.emv.find(b(0x81), c.emv.contexts.BHT):decode(b(0,0,8)) == 8)
    round(c.emv.find(b(0x57)), b(0x12,0x34,0x5D,0x26,0x12,0x10,0x1F),
          {pan="12345", expiration_year=26, expiration_month=12, service_code=101, discretionary_data=""})
    assert(c.emv.find(b(0x50)) == nil) -- known raw text, no codec
    assert(c.emv.find(b(0xDF,0x01)) == nil)
    assert(c.emv.find(b(0x9F,0x02), c.emv.contexts.BIT) == nil)
end

local calls = 0
local spec = {
    decode = function(bytes) return {text = bytes} end,
    encode = function(value) calls = calls + 1; return value.text end
}
local custom = tlv.codec(spec)
spec.encode = function() error("mutated") end
round(custom, "hello", {text = "hello"})
assert(calls == 1, "encode callback must run once, including the size query")
local marker = {}
local bad = tlv.codec {decode = function() error(marker) end, encode = function() error(marker) end}
assert(fails(function() bad:decode("x") end) == marker)
assert(fails(function() bad:encode({}) end) == marker)
fails(function() tlv.codec {}:decode("") end, tlv.errors.UNSUPPORTED)
fails(function() tlv.codec {decode = function(s) return s end}:encode("") end, tlv.errors.UNSUPPORTED)
fails(function() tlv.codec {encode = function() return 12 end}:encode("") end)
assert(tlv.codec {decode = function() return nil end}:decode("") == nil)
assert(tlv.codec {encode = function(value) assert(value == nil); return "" end}:encode(nil) == "")
local recursive
recursive = tlv.codec {
    decode = function(s) if #s == 0 then return 0 end return 1 + recursive:decode(s:sub(2)) end,
    encode = function(n) if n == 0 then return "" end return "x" .. recursive:encode(n-1) end
}
round(recursive, "xxx", 3)
local thread = coroutine.create(function() round(custom, "thread", {text="thread"}) end)
assert(coroutine.resume(thread))
local weak = setmetatable({}, {__mode="v"})
do
    local cycle
    cycle = tlv.codec {decode = function() return cycle end}
    weak[1] = cycle
end
collectgarbage("collect"); collectgarbage("collect")
assert(weak[1] == nil, "callback cycle leaked")

-- Every exported static codec must have a successful round trip above.
local function coverage(t)
    for name, value in pairs(t) do
        if type(value) == "userdata" then assert(checked[value], "untested codec: " .. name)
        elseif type(value) == "table" then coverage(value) end
    end
end
coverage(c)
assert((c.asn1 ~= nil) == (tlv.formats.ber ~= nil))
assert((c.emv ~= nil) == (tlv.formats.emv ~= nil))
assert((c.bluetooth.flags ~= nil) == (tlv.formats.bluetooth_ltv ~= nil))
assert((c.lldp.chassis_id ~= nil) == (tlv.formats.lldp ~= nil))

if arg and arg[1] == "--allocator" then
    local allocate = require("opentlv_test_allocator")
    local payload = string.rep("a", 256)
    local callback = tlv.codec {decode = function(s) return {s, string.rep(s, 2)} end,
                                encode = function(v) return v .. v end}
    local operations = {
        function() callback:decode(payload) end,
        function() callback:encode(payload) end,
        function() c.bytes:decode(payload) end,
        function() c.ipv4_list:encode {b(1,2,3,4), b(5,6,7,8)} end,
        function() tlv.codec {decode = function(s) return s end} end,
    }
    for _, operation in ipairs(operations) do
        local failed, succeeded = false, false
        for budget = 0, 80 do
            if allocate(operation, budget) then succeeded = true else failed = true end
        end
        assert(failed and succeeded, "allocation failure coverage incomplete")
        operation()
    end
end
print("Lua codec tests passed")

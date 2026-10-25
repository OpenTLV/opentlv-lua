# Using OpenTLV from Lua

Start with setup below, then run the [Lua quick start](../examples/quick_start.lua)
for input, expected output and the public API. You can use this guide without
studying native C implementation contracts.

The `opentlv` module is an experimental Lua binding for OpenTLV. It binds a
Reader and a pull Tree Reader with incremental input, source preservation,
a Writer, Tree Writer with structural events and measurement, Schema,
tag/length/value `Element` tables and preorder tree
traversal, borrowed Query, an owned mutable Document with checked Nodes and
[custom Formats](#custom-formats) composed from Core's field encodings.
Document Builder remains unbound; see the
[binding matrix](https://github.com/OpenTLV/opentlv/blob/main/docs/concepts/bindings.md#capability-implementation-matrix) and
[Lua development guide](implementation.md).

## Setup

The binding is built and installed from the standalone
[opentlv-lua repository](https://github.com/OpenTLV/opentlv-lua). Follow its
[development instructions](development.md)
for CMake, external Core selection, Lua runtime requirements and Windows linking,
or its [LuaRocks instructions](development.md#luarocks-installation).
Core no longer builds or installs the Lua module.

```lua
local opentlv = require("opentlv")

print(opentlv.version()) -- Lua binding version, e.g. "0.1.0" for tag v0.1.0
```

`opentlv._VERSION` contains the same binding version. Development builds have
an explicit `scm` or dirty identity; see [versioning](versioning.md). Core's
version and commit SHA are not exposed through the Lua API.

Runnable version:
[quick_start.lua](../examples/quick_start.lua)
(`lua examples/quick_start.lua` from the `opentlv-lua` repository root).

## Reading

`opentlv.reader(data, format)` returns a Reader over `data`, a Lua string.
Using the reader directly as a generic-for iterator calls it as its own
iterator function each time, so `for element in reader do ... end` reads
sequentially with no explicit `next()`/`at_end()` loop:

```lua
local opentlv = require("opentlv")

local data = string.char(0x01, 0x02, 0xAA, 0xBB, 0x02, 0x00)
for element in opentlv.reader(data) do
    print(element.tag, element.length, element.value)
end
```

Each `element` is a plain table with `tag`, `raw_length`, `value` (all copied out
of `data`, since Lua strings cannot borrow foreign memory the way a C, C++,
Rust or Python view can) plus numeric `length` (the value size) and `offset`, the absolute position of the element's
tag within `data`. `format` defaults to `opentlv.formats.ber`; pass
`opentlv.formats.ber`, `.cer`, `.der`, `.bluetooth_ltv`, or
`opentlv.formats.fixed(tag_size, length_size, byte_order)` (`byte_order` is
`"big"` or `"little"`, equivalent to the C `tlv_fixed_format_t`) for another
wire format, or a [custom composed Format](#custom-formats). Constructed elements are not expanded automatically; construct a
new reader over `element.value` to descend, as in the runnable example below.

The explicit method form, `reader:next()` (`nil` at the end) and
`reader:at_end()`, is equivalent and reads the same way; `reader:position()`
and `reader:format()` report the reader's current byte offset and the
format it was constructed with.

`opentlv.read(data, format)` reads only the first element and returns it
with its encoded size; trailing bytes are not read and empty input returns
`nil`. `opentlv.read_source(data, format)` returns the element and its
[Source](#source-preservation) instead.

Runnable version, parsing a nested BER document and handling truncated
input:
[parse.lua](../examples/parse.lua)
(`lua examples/parse.lua`).

### Incremental input

`opentlv.reader(data, format, {final_input = false})` starts a Reader over
input that is not complete yet. When the current window ends inside an
element, `next()` raises an error with `code == opentlv.errors.NEED_MORE_DATA`
and `severity == "info"`, and the reader stays where it was.
`reader:set_input(data, discard, final_input)` then supplies a new window:

- `data` must start with the bytes of the old window after its first
  `discard` bytes, followed by any new bytes. Otherwise the call raises
  `INVALID_ARG` and changes nothing.
- `discard` (default 0) may be at most `reader:consumed()`, the bytes the
  reader no longer needs.
- `final_input` (default `true`) declares the end of the stream. Final input
  cannot be extended later (`INVALID_STATE`).

```lua
local reader = opentlv.reader(first_chunk, opentlv.formats.ber, {final_input = false})
local window = first_chunk
local ok, err = pcall(reader.next, reader)
if not ok and err.code == opentlv.errors.NEED_MORE_DATA then
    local discard = reader:consumed()
    window = window:sub(discard + 1) .. next_chunk
    reader:set_input(window, discard, false)
end
```

`reader:offset()` and element `offset` fields are absolute positions in the
whole stream, including discarded bytes; `reader:consumed()` (also
`reader:position()`) is relative to the current window. `reader:at_end()` is
true only at the end of final input. A Reader created without options reads
final input, as before.

### Source preservation

`reader:next_source()` returns `element, source` from the same single decode.
A Source (`opentlv.Source`) is the original encoding of one element:

- `source:bytes()` returns the exact encoded bytes, trailer included.
- `source:layout()` returns `size` and the `header`, `tag`, `length`,
  `value` and `trailer` ranges as `{offset, size}` tables relative to the
  element start; fields the Format does not report are absent.
- `source:element()`, `source:offset()` and `source:format()` return the
  decoded element, its absolute offset and the decoding Format.
- `source:preserve(element)` returns the original bytes through Core's
  `tlv_source_preserve()`, but only while `element` (by default the decoded
  one) still has the decoded Tag and Value. Changed content raises
  `INVALID_ARG` with `operation = "preserve"`.

`writer:preserve(source, element)` appends the same bytes to a Writer, see
[Writing](#writing). A Source keeps its input string and Format alive, so it
stays valid after the Reader is collected or given new input.

## Visitor and tree traversal

`opentlv.visit(data, format, callback)` visits sequential top-level elements
through the C Reader Visitor. It calls `callback(element)` with copied binary
`tag` and `value` strings, `length`, zero-based wire `offset`, and `constructed`.
It leaves nested bytes in the parent's Value, just like Reader:

```lua
local visited, stopped = opentlv.visit(data, opentlv.formats.ber, function(element)
    print(element.tag, element.value)
end)
```

Both visitor functions return `visited, stopped`. Only a boolean `false`
stops traversal; other callback results continue. Calls are synchronous and
callbacks cannot yield. Input, Format and callback remain alive during the call;
copied elements may be retained afterward. Callback references and native
traversal storage are released before returning or propagating an error.
Callbacks may invoke a separate traversal, including on the same input.

`opentlv.visit_tree(data, format, callback, opts)` visits every element of
`data` in preorder, calling `callback(element, depth)` for each; `element` has an
additional `constructed` boolean field. Returning `false` from `callback`
stops the traversal early:

```lua
opentlv.visit_tree(data, opentlv.formats.ber, function(element, depth)
    print(string.rep("  ", depth) .. element.tag)
end)
```

`opts` is an optional table with integer `max_depth` (default 64, the
library's `TLV_TREE_DEFAULT_DEPTH`) and `max_elements` (default 65536); pass an
explicit, tighter `max_elements` when traversing untrusted input, since the C
`tlv_tree_reader_visit()` this wraps requires a real bound. `callback` may be omitted
to validate structure and limits only. For `opentlv.formats.der`, traversal
uses the stricter `tlv_der_visit()`, applying `max_depth` and `max_elements`
while retaining the library's default DER input/value size limits. Omitted
DER options retain the C defaults (depth 32 and 100000 elements); explicit
DER depths may be up to 64. Limits must be nonnegative integers representable by native
`size_t`; zero is a real limit. Without a callback, `visited` is zero.

`reader:visit(callback)` runs the same Visitor over an existing Reader and
returns `visited, stopped`. It continues from the reader's position: after a
stop, or after `NEED_MORE_DATA` and `set_input()`, the next call resumes with
the next element and does not repeat elements already passed to the callback.
The callback must not use the same reader; such a call raises an error.

### Tree Reader

`opentlv.tree_reader(data, format, options)` pulls the elements of a tree one
at a time, in preorder, using the C Tree Reader. Each item is an element table
with `depth` (roots are 0) and `constructed`. Like a Reader, it can be used in a
generic for:

```lua
for item in opentlv.tree_reader(data, opentlv.formats.ber) do
    print(string.rep("  ", item.depth) .. item.tag, item.constructed)
end
```

Options are `final_input` (default `true`), `frame_capacity` and `max_depth`
(both default to 64, the C `TLV_TREE_DEFAULT_DEPTH`) and `max_elements`
(default 65536). The reader needs one frame for each constructed element it
enters. Exceeding `max_depth` or `max_elements` raises `LIMIT`; running out of
frames raises `BUFFER_TOO_SHORT`. Each Tree Reader uses the generic C Tree
Reader, including for `opentlv.formats.der`; `visit_tree()` remains the entry
point for the stricter DER traversal.

- `next()` returns the next item, or `nil` at the end of final input.
- `next_event()` returns structural events instead: `kind` is `"begin"` for a
  constructed element, `"element"` for a primitive one and `"end"` when a
  constructed element closes. Begin and element events carry the element
  fields and `constructed`; every event has `depth`, `offset` and `skipped`.
  An end event's `offset` is where the closed Value ends. Do not mix
  `next()` and `next_event()` on one reader if you need balanced events, since
  `next()` hides end events.
- `skip_subtree()` skips the children of the constructed item just returned,
  without decoding them; an event reader then returns an end event with
  `skipped = true`. It also works after a failed descent (`LIMIT` or
  `BUFFER_TOO_SHORT`), so traversal can continue with the next sibling.
  Without such an item it raises `INVALID_STATE`.
- `visit(callback)` calls `callback(element, depth)` for the remaining items,
  like `visit_tree()`, and returns `visited, stopped`. When the callback stops
  at a constructed element, the next pull or visit continues with its
  children unless you call `skip_subtree()` first.
- `set_input()`, `consumed()`, `offset()`, `at_end()` and `format()` work as for
  an [incremental Reader](#incremental-input). A constructed element is
  returned only when all of its bytes are available.

## Error handling

Every reading or traversal failure raises a table (via Lua's `error()`) with
a `code` (the raw `tlv_result_t` value, also available named under
`opentlv.errors`, for example `opentlv.errors.TRUNCATED`) and a
`message` (the C `tlv_result_string()` text); `tostring()` on it formats both
together. When the C API reports structured diagnostic detail for the
failure, `offset`, `expected`, `actual`, `operation` and `tag` carry it;
fields the failure does not report are absent, so check with `err.offset`
rather than assuming every field is present.

```lua
local ok, err = pcall(function()
    for _ in opentlv.reader(truncated, opentlv.formats.ber) do end
end)
if not ok then
    print(string.format("%s at offset %s (%s)", tostring(err), err.offset, err.operation))
end
```

A callback error inside `opentlv.visit` or `opentlv.visit_tree` propagates unchanged, so
`pcall` catches whatever your callback raised, not a wrapped copy.

## Writing

`opentlv.writer(format, options)` creates a sequential Writer over a buffer
owned by the Lua binding. `format` defaults to BER when enabled; otherwise
it is required. `options.capacity` defaults to 1024 bytes, never grows, and
may be zero. All encoding is performed by the OpenTLV C Writer and Format.

```lua
local tlv = require("opentlv")
local writer = tlv.writer(tlv.formats.ber, { capacity = 1024 })
writer:write(string.char(0x5F, 0x2A), string.char(0x09, 0x78))
writer:write(string.char(0x9F, 0x02), string.char(0, 0, 0, 0, 1, 0))
local encoded = writer:bytes()
```

- `write(tag, value)` appends one element. Both arguments are binary strings,
  including embedded zero bytes; numbers and arbitrary userdata are rejected.
  `nil` Tag represents an absent identifier when the Format permits it.
  An empty Tag string represents an explicit empty identifier.
- `write_element(element)` accepts a Reader-style table with `tag` and `value`.
  Length is derived from `value`; metadata such as `length` and `offset` does
  not override the content.
- `copy_encoded(bytes)` appends already encoded bytes unchanged, without
  checking their framing.
- `preserve(source, element)` appends the original bytes of a
  [Source](#source-preservation) while `element` (by default the decoded one)
  is unchanged, keeping framing such as a non-minimal BER length that
  re-encoding would replace. The writer's Format does not reinterpret them.
- `bytes()` returns an independent immutable string containing written bytes.
- `size()` and `remaining()` report written bytes and unused capacity.
- `format()` returns the retained Format object.

`opentlv.encoded_size(tag, value, format)` returns the encoded size of one
element without writing it. `value` is the Value string, or a number giving
only the Value length for Formats that can size an element without reading its
Value; a Format that needs the Value bytes rejects the number form.

Tag strings represent raw byte identity: `"5F2A"` is four ASCII bytes, not
`string.char(0x5F, 0x2A)`. Input strings are borrowed only during a write call.
The binding keeps the Format alive and releases its output buffer at garbage
collection. A failed sequential write does not advance the cursor; as in C,
encoder callback failures may modify unused destination bytes.

### Nested writing

`opentlv.tree_writer(format, options)` wraps the C Tree Writer. It supports
`write`, `write_element`, `format` and `size`, plus `begin(tag)`,
`end_element()` and `finish()`. `finish()` and `bytes()` both check that all
parents are closed and return an independent output string; neither closes
parents implicitly. `size()` reports only the finalized root prefix.

```lua
local tlv = require("opentlv")
local writer = tlv.tree_writer(tlv.formats.ber, { capacity = 1024 })
writer:begin(string.char(0xE1))
writer:write(string.char(0x04), "hello")
writer:end_element()
local encoded = writer:finish()
```

The selected C Format determines whether constructed encoding is supported.
Tree options are `capacity` (1024), `scratch_capacity` and `tag_capacity`
(both default to `capacity`), `frame_capacity` and `max_depth` (both default
to the C `TLV_TREE_DEFAULT_DEPTH`), and `max_elements` (unbounded by default).
Options must be nonnegative native integers. Scratch must hold the largest
closed parent Value; each open parent uses one frame. Maximum item depth
counts roots as zero. Open Tags are copied by the C Tree Writer into bounded
Tag storage, so temporary Lua strings can be collected safely.

`write_event(event)` consumes one structural event in the shape
`tree_reader:next_event()` returns: `kind`, `depth`, optional `skipped`, and
`tag` and `value` for begin and element events. A begin event's Value is
ignored. Events from a Tree Reader therefore rebuild the tree with the
writer's Format:

```lua
local reader = tlv.tree_reader(data, tlv.formats.ber)
local writer = tlv.tree_writer(tlv.formats.ber)
for event in function() return reader:next_event() end do
    writer:write_event(event)
end
local rebuilt = writer:finish()
```

Events at the wrong depth, an end without an open parent and skipped end
events raise errors and leave the writer unchanged.

`opentlv.measure_tree(format, items, options)` encodes a whole tree in one
call and returns the encoding; its length is the exact encoded size. `items`
is an array of preorder items (`tag`, `value`, `depth`, `constructed`; a
constructed item's `value` is ignored and may be omitted) or a function or
callable object returning one item per call and `nil` at the end, for example
a Tree Reader. `opentlv.measure_events(format, events, options)` does the
same for a balanced event sequence. Options and defaults are those of
`tree_writer()`; the output and scratch storage are fixed for the call. When
it is too small, the `BUFFER_TOO_SHORT` error has `required_data` or
`required_scratch`: at least that much is needed, so retry with more storage
and a fresh item source. Errors raised by an item function propagate
unchanged.

Runnable version, feeding chunked input through an incremental Tree Reader
into a Tree Writer and preserving one element's original framing:
[stream.lua](../examples/stream.lua) (`lua examples/stream.lua`).

### Writer errors

Core failures raise the same structured error-table type as Reader. In addition
to `code` and `message`, diagnostic operations copy `offset`, `operation`,
`tag`, `length`, `required`, `available`, `expected` and `actual` when supplied
by the C core. `required` and `available` describe the failing destination or
workspace, not necessarily the complete final document. Tree offsets refer
to the current provisional buffer. A failed `end_element()` preserves the
open parent and its children, following the C Tree Writer contract.

```lua
local ok, err = pcall(function()
    local writer = require("opentlv").writer(nil, { capacity = 0 })
    writer:write(string.char(0x04), "value")
end)
if not ok then
    print(err.code, err.message, err.required, err.available)
end
```

## Custom formats

`opentlv.formats.compose(config)` composes a Format from one identifier
encoding and one length encoding without writing C, and
`opentlv.formats.packed(config)` packs Tag and Length bits into one header
integer. The configuration tables mirror Core's `tlv/field/*.h` and
`tlv/formats/*.h` structures. Core validates them and implements all framing;
Lua only reads the tables.

```lua
local tlv = require("opentlv")
-- One-byte tags; lengths below 0xFF inline, otherwise 0xFF and two bytes.
local format = tlv.formats.compose {
    tag = {kind = "fixed", size = 1},
    length = {kind = "escaped", escape = 0xFF, extended_size = 2},
    constructed = {byte_index = 0, mask = 0x80, value = 0x80},
}
assert(format:encode(string.char(1), "Hello") == string.char(1, 5) .. "Hello")
local document = tlv.document(format:encode(string.char(0x81), ""), format)
```

`config.tag` and `config.length` are required tables selected by `kind`:

| Field | `kind` | Options |
| --- | --- | --- |
| `tag` | `"fixed"` | `size`: identifier width in bytes (`tlv_fixed_identifier_t`) |
| `tag` | `"variable"` | `inline_mask`, `escape`, `continuation_bit`, `payload_mask`, `max_size` and an optional `policy` (`tlv_variable_identifier_t`) |
| `length` | `"fixed"` | `size` (1–8) and `byte_order` (`tlv_fixed_length_t`) |
| `length` | `"variable"` | `long_form_bit`, `payload_mask`, `byte_order` and an optional `policy` (`tlv_variable_length_t`) |
| `length` | `"escaped"` | `escape`, `extended_size`, `byte_order`, `min_extended` and `max_length` (`tlv_escaped_length_t`) |

Options without a default below are required. `byte_order` is `"big"`
(default) or `"little"`. An identifier `policy` takes `forbidden_leading`, a
binary string of rejected first bytes (default empty), and the booleans
`reject_zero_first_payload` and `require_minimal` (default `false`). A length
`policy` takes the booleans `allow_short` and `allow_long` (default `true`) and
`require_minimal` (default `false`), `max_long_octets` (default 8) and
`max_value` (default `math.huge`, no limit). An escaped length's `min_extended`
defaults to zero and `max_length` to the largest `extended_size`-byte count.

Optional composition fields:

| Field | Default | Meaning |
| --- | --- | --- |
| `element_order` | `"tlv"` | `"ltv"` places the length before the identifier |
| `length_scope` | `"value"` | `"tag_and_value"` makes the count cover the identifier's wire bytes too |
| `tag_only` | `nil` | Dense array of binary identifiers encoded without Length and Value; they need an empty Value, TLV order and value scope |
| `constructed` | `nil` | `{byte_index, mask, value}`: an element is constructed when identifier byte `byte_index` (zero-based, default 0) masked with `mask` equals `value`; without it every Value is opaque |

The encoding pair selects Core's composition: fixed identifier and fixed
length use `tlv_fixed_format_init()` or, with `tag_only`,
`tlv_tagged_binary_format_init()`; variable identifier and variable length use
`tlv_variable_format_init()` or `tlv_variable_fields_init()` with
`tlv_tagged_fields_format_init()`; a fixed identifier with an escaped length uses
`tlv_escaped_format_init()`. Other pairs compose the field primitives through
`tlv_fields_format_init()` or `tlv_tagged_fields_format_init()`.

```lua
-- LLDP framing: a 7-bit type above a 9-bit length in one big-endian header.
local lldp = tlv.formats.packed {
    header_size = 2,
    tag = {bit_offset = 9, bit_width = 7},
    length = {bit_offset = 0, bit_width = 9},
}
local element = lldp:decode(string.char(6, 2, 0, 120)) -- tag string.char(3)
```

A packed layout (`tlv_packed_layout_t`) requires `header_size` (1–8 bytes) and
`tag` and `length` tables with `bit_offset` (from the least significant header
bit) and `bit_width`. Optional `byte_order`, `length_scope` and `constructed`
have the meanings above. Decoded Tags are the field value as `tag_size`
big-endian bytes, by default the fewest bytes holding the tag's `bit_width`.
The binding owns the canonical identifier table Core borrows, with one entry
per tag value, so tag fields are limited to 16 bits. The layout is checked with
`tlv_packed_layout_validate()`.

Integers follow the [codec integer rules](#integers-and-configured-codecs).
Invalid configuration rejected by Core raises `opentlv.Error` with
`INVALID_ARG`; missing or mistyped options, unknown `kind`, order or scope
strings and packed tag fields wider than 16 bits raise Lua errors. Both
constructors snapshot their tables: the Format userdata owns its descriptor,
configuration, policies and identifier tables, so later changes to the tables
do not change it. Readers, Writers, Documents, Query matchers and programs
retain the Format they use. As with Fixed formats, reuse the same Format
object for a compiled Query program and the Documents it evaluates.
`tostring()` reports `opentlv.Format<composed>` or `opentlv.Format<packed>`.

### Format operations

Every Format, builtin, fixed or composed, exposes the C `tlv_format_t`
operations independently of Reader and Writer:

| Method | Result |
| --- | --- |
| `can_read()`, `can_write()` | Whether the Format decodes, or measures and encodes (`tlv_format_can_read()`, `tlv_format_can_write()`) |
| `decode(data)` | `element, size` for one complete element at the start of `data` (`tlv_format_decode()`); `element` has the Reader fields with offset zero, `size` is the consumed byte count and trailing bytes are not read |
| `measure(tag, value)` | `{header, value, trailer, total}` logical sizes (`tlv_format_measure()`) |
| `encode(tag, value)` | Encoded element as a new string (`tlv_format_encode()`) |

`tag` and `value` are binary strings; a `nil` Tag is an absent identifier, as
for Writer. Failures raise `opentlv.Error` with `operation` (`"decode"`,
`"measure"` or `"encode"`) and the Format's own detail: `region` (`"header"`,
`"tag"`, `"length"`, `"value"` or `"trailer"`) and, when known, `offset`,
`required` and `tag_range`, `length_range` and `value_range` as
`{offset, size}` tables. Decode offsets are relative to the input; measure and
encode offsets to the would-be output. Empty input raises `END` without
detail.

### Packed bit fields

`tlv.fields.read_packed(field, data)` extracts one unsigned bit field from the
`header_size`-byte integer at the start of `data` (`tlv_packed_field_read()`).
`tlv.fields.write_packed(field, data, value)` returns a copy of `data` with the
field replaced, keeping every other bit (`tlv_packed_field_write()`). `field`
is `{header_size, bit_offset, bit_width, byte_order}`, with `byte_order`
defaulting to `"big"`.

```lua
local flags = {header_size = 1, bit_offset = 5, bit_width = 3}
assert(tlv.fields.read_packed(flags, string.char(0xA4)) == 5)
assert(tlv.fields.write_packed(flags, string.char(0xA4), 2) == string.char(0x44))
```

Values follow the codec integer rules. Short input raises `TRUNCATED`, short
output `BUFFER_TOO_SHORT`, a value wider than the field `OVERFLOW` and an
invalid field `INVALID_ARG`.

Runnable version, using one composed Format with Tree Writer, traversal,
Query and Document:
[compose.lua](../examples/compose.lua)
(`lua examples/compose.lua`).

## Structural schemas

`opentlv.schema(config)` creates an immutable structural Schema. It snapshots
the rule/group tables and retains tag strings, field names and child Schema
objects. Changing the original tables does not change the schema.
Validation delegates to `tlv_schema_validate_all_diag()`; Lua implements no
schema rules, wire parser or value decoding.

```lua
local tlv = require("opentlv")
local format = tlv.formats.fixed(1, 1, "big")
local schema = tlv.schema {
    rules = {
        {tag = string.char(1), name = "identifier", min_occurs = 1,
         max_occurs = 1, min_length = 2, max_length = 2, kind = "primitive"},
    },
}
local result = schema:validate(string.char(1, 1, 255), format)
if not result.ok then
    for _, diagnostic in ipairs(result.diagnostics) do
        print(diagnostic.code, diagnostic.offset, diagnostic.field)
        if diagnostic.length then
            print(diagnostic.length.minimum, diagnostic.length.maximum,
                  diagnostic.length.actual)
        end
    end
end
```

`config.rules` is a dense array of rule tables (default empty). Each rule
requires a binary-string `tag`; identifiers have exactly the same byte identity
as Reader tags. Optional fields are:

| Field | Default | Meaning |
| --- | --- | --- |
| `name` | `nil` | Diagnostic field name; embedded NUL bytes are rejected |
| `min_length`, `max_length` | `0`, `math.huge` | Inclusive Value-length bounds |
| `length_multiple` | `0` | Required length multiple; zero disables it |
| `flags` | `0` | C length flags; `tlv.SCHEMA_LENGTH_ENDPOINTS` permits only the bounds |
| `min_occurs`, `max_occurs` | `0`, `math.huge` | Inclusive occurrence bounds per parent |
| `kind` | `"any"` | `"any"`, `"primitive"` or `"constructed"` |
| `children` | `nil` | Another Schema object; requires `kind = "constructed"` |
| `group` | `0` | Alternative-group ID; zero means no group |

An omitted child schema leaves membership unrestricted; the C engine still
checks wire structure. Constructed status comes from Format, so a Fixed
format's values remain opaque. For example, when BER is enabled:

```lua
local child = tlv.schema {
    rules = {{tag = string.char(4), min_occurs = 1}},
}
local parent = tlv.schema {
    rules = {{tag = string.char(48), kind = "constructed", children = child}},
}
assert(parent:validate(string.char(48, 2, 4, 0), tlv.formats.ber).ok)
```

At scope level, `allow_unknown` defaults to `false` and `order` defaults to
`"any"`; `"sequence"` enforces relative rule order. Optional `groups` is a dense
array of `{id, min_occurs, max_occurs, name}` tables using named fields.
Group IDs must be nonzero and unique; bounds default to `0` and `math.huge`.
A group's bounds apply to the sum of all matching alternatives. Grouped rules
must have `min_occurs = 0`, and each rule's `max_occurs` still applies.
C validates these constraints, including duplicate tags and invalid bounds,
when validating the corresponding scope.

`schema:validate(data, format, options)` accepts a binary Lua string and an
existing Format object. Format defaults to BER when enabled; otherwise it is
required. Options are:

| Option | Default | Meaning |
| --- | --- | --- |
| `capacity` | `64` | Maximum stored schema diagnostics; zero counts only |
| `max_depth` | `32` | Native nesting limit |
| `max_elements` | `100000` | Total element limit; zero allows only empty input |
| `unknown` | `"by_schema"` | `"by_schema"`, `"allow"` or `"reject"` |

Numbers must be nonnegative native-size integers. `math.huge` represents
`SIZE_MAX` for upper length/occurrence bounds and `max_elements`. Lua versions
with floating-point-only numbers retain their usual integer precision limits.
Native fixed traversal/path capacities also apply; exceeding them returns
`LIMIT`. Format selects framing; Schema does not add semantic DER or protocol
value validation.

The result always contains `ok`, numeric `code`, `diagnostics`, `total_count`
and `truncated`. Success has code `OK` and an empty diagnostics array. Schema
violations return code `SCHEMA`; `total_count` counts every native violation,
even when `capacity` stores only a prefix. `truncated` reports an omitted suffix.
The C engine determines diagnostic ordering; within-scope order is unspecified.

Malformed input, invalid schema configuration and exceeded limits return
`ok = false` and the native error code. They return one basic diagnostic,
`total_count = 1` and `truncated = false`, even at `capacity = 0`; this capacity
limits schema reports only. C provides no partial schema report on these
failures. Lua argument/type errors and allocation failures raise Lua errors.

## Diagnostic tables

Schema reports, Reader and Writer use the same common native diagnostic
conversion. Tables own their strings and remain valid after the input,
Schema or Reader/Writer is collected. Common fields are `code`, `message`
(from `tlv_result_string()`), and `severity` (`"error"`, `"warning"`, `"info"`).
`offset`, `expected`, `actual`, `path` and `contexts` appear only when supplied
by C. Offsets remain zero-based bytes, independent of Lua array indexing.
`contexts` is an array of `{layer, key, value}` tables in native order.

Schema violations additionally provide:

- `kind`: `"missing"`, `"duplicate"`, `"unexpected"`, `"kind"`, `"length"` or `"order"`.
- `tag`: affected binary identifier; `path`: binary identifiers of enclosing
  scopes, outermost first, excluding `tag`.
- Optional `field` and boolean `is_group`.
- `occurrences` or `length`: `{minimum, maximum, actual}` when applicable.
  An unrestricted maximum is `math.huge`.
- `length_multiple` and `length_flags` for length failures.
- `form`: `{expected, actual_constructed}` for kind failures, with an expected
  kind string and an actual boolean.

These typed fields are copied from the C report. The binding does not invent
textual `expected`/`actual` descriptions. Missing-field reports use the native
parent-element offset when nested and have no offset at root level. Fatal
diagnostics carry only code/message/severity and the offset if C provided one;
the binding does not reparse the input to reconstruct tag or path information.

See the runnable [schema example](../examples/schema.lua).

## Value codecs

Select a codec explicitly and pass only an element's Value bytes:

```lua
local tlv = require("opentlv")
local codec = tlv.codecs.uint16_be
local value = codec:decode(string.char(0x12, 0x34)) -- 4660
local bytes = codec:encode(value) -- binary string, no TLV framing
```

Both methods invoke the existing C codec. They do not select a codec from a
Tag, validate the enclosing schema, or reinterpret framing. Encode uses the
C size query before allocating the output. Decoded strings and tables own
their contents and survive collection of the source string and codec.

`tlv.codecs` is a namespace of exported descriptors and constructors, not a
separate runtime registry. Standard Lua indexing provides name selection,
for example `tlv.codecs["uint16_be"]`; an absent name returns `nil`.

### Integers and configured codecs

Integer codecs accept native Lua integers, exactly represented integral Lua
numbers, or decimal strings. Lua 5.3+ returns a native integer when it fits
`lua_Integer`. On Lua 5.1/5.2 and LuaJIT, the default double-number build
returns numbers through ±(2^53−1). Larger results are decimal strings; this
also covers unsigned values above Lua's signed integer range. Float-number
builds use ±(2^24−1). Floating-point inputs outside that safe range, fractions,
NaN, infinity and overflowing decimal strings are rejected rather than rounded.
Use a decimal string to supply large exact values on every Lua version.

```lua
local number = tlv.codecs.number {encoding = "binary_be", width = 8}
local maximum = "18446744073709551615"
assert(number:decode(number:encode(maximum)) == maximum)

local text = tlv.codecs.text {alphabet = "ascii_printable", width = 5, zero_padding = true}
assert(text:encode("abc") == "abc\0\0")
local digits = tlv.codecs.digits {width = 3}
assert(digits:decode(string.char(0x00, 0x12, 0xFF)) == "0012")
```

All constructors take a table. Number `encoding` is `"binary_be"` (default),
`"binary_le"`, or `"bcd"`; `width` defaults to zero (minimal encoding), and
`digits` defaults to zero (supply the precision for BCD). Text `alphabet` is
`"ascii_printable"` (default) or `"ascii_alnum"`; `width` defaults to zero and
`zero_padding` to false. Digits `width` defaults to zero. The userdata owns
its C configuration. The C engine validates configuration and Value semantics
when the codec is invoked.

### Builtin representations

Names below are relative to `tlv.codecs`. Byte strings are binary Lua strings,
including embedded zeros. Arrays are dense, one-based Lua tables. Integer
fields use the exact integer rules above. Table field names match the C
representation; required fields must be supplied on encode.

| Codecs | Lua representation |
| --- | --- |
| `uint8`, `uint16_be`, `uint16_le`, `uint32_be`, `uint32_le`, `int64_minimal_be` | Integer |
| `bytes` | Byte string |
| `ipv4`, `ipv4_list` | Four network-order bytes; array of four-byte strings |
| `dhcpv4.message_type`, `lldp.ttl`, `lldp.text`, `bluetooth.uuid16`, `bluetooth.uuid32` | C source aliases: integer, integer, bytes, integer, integer |
| `asn1.boolean`, `asn1.integer`, `asn1.enumerated`, `asn1.null` | Boolean, signed integer, signed integer, `nil` |
| `asn1.bit_string` | `{unused_bits, data}`; `data` excludes the count octet |
| `asn1.oid`, `asn1.relative_oid` | Array of unsigned integer arcs |
| `asn1.oid_iri`, `asn1.relative_oid_iri` | Array of string arc labels |
| `asn1.octet_string`, `asn1.object_descriptor`, `asn1.*_string` | Byte string; BMP and Universal strings retain big-endian UCS-2/UCS-4 bytes |
| `asn1.time`, `asn1.duration` | String validated by the C codec |
| `asn1.date` | `{year, month, day}` |
| `asn1.time_of_day` | `{hour, minute, second}` |
| `asn1.utc_time`, `asn1.date_time` | `{year, month, day, hour, minute, second}` |
| `asn1.generalized_time` | Same timestamp fields plus `fraction_digits` (empty string for no fraction) |
| `bluetooth.flags`, `bluetooth.local_name`, `bluetooth.tx_power` | Bytes, UTF-8 string, signed integer |
| `bluetooth.uuid128` | 16 bytes in canonical printed UUID order; C handles wire reversal |
| `bluetooth.uuid16_list`, `bluetooth.uuid32_list`, `bluetooth.uuid128_list` | Arrays of the corresponding UUID representation |
| `bluetooth.service_data16`, `bluetooth.service_data32`, `bluetooth.service_data128` | `{uuid, payload, raw}`; `raw` is informational, optional and ignored on encode |
| `bluetooth.manufacturer_data` | `{company_id, payload, raw}`; same `raw` rule |
| `lldp.chassis_id`, `lldp.port_id` | `{subtype, identifier}` |
| `lldp.capabilities` | `{supported, enabled}` |
| `lldp.management_address` | `{address_subtype, address, interface_subtype, interface_number, oid}` |
| `lldp.organisation` | `{oui, subtype, payload}`; `oui` is exactly three bytes |
| `emv.amount` | Unsigned integer in unscaled minor units |

Official Lua packages include the ASN.1, Bluetooth, LLDP and EMV exports.
ASN.1 codecs preserve C's canonical content checks even when the caller reads
permissive BER. Individual Core modules cannot be disabled through Lua build
options; see the [build policy](development.md#components-and-tests).

### EMV dictionary selection

`tlv.codecs.emv.find(tag, context)` delegates to `tlv_emv_find()` and selects
the existing builtin descriptor. `tag` is a binary string; `context` defaults
to `tlv.codecs.emv.contexts.BASE`. Other context constants are `BIT`, `BHT`,
`BHT_FORMAT`, `BIT_GROUP`, `BIOMETRIC_COUNTERS`, `BIOMETRIC_ATTEMPTS` and
`BIOMETRIC_VERIFICATION`. There is no fallback between contexts. Unknown
entries and entries without a codec return `nil`.

```lua
local amount = tlv.codecs.emv.find(string.char(0x9F, 0x02))
local bytes = amount:encode(1234)
assert(amount:decode(bytes) == 1234)
```

The builtin C presentation adapter determines the representation: numbers,
flags, account and biometric types are integers; digits are strings; dates
and times are tables with the C field names; cryptogram information is
`{type, flags}`; CVM results are `{method, condition, result}`; number lists
are arrays; AFL is an array of `{sfi, first_record, last_record,
offline_auth_record_count}`; Track 2 is `{pan, expiration_year,
expiration_month, service_code, discretionary_data}`. This selection occurs
before conversion, and does not add codec metadata to generic Definition.

### Errors and custom codecs

C codec failures raise an `opentlv.Error` table with the shared `tlv.errors`
`code` and a `message` from `tlv_result_string()`. Optional `codec_detail` retains
operation, reported result, callback violation and delegated cause. The old
`codec_errors` and `codec_strerror` APIs have been removed.
No source offset or Tag is invented for a Value-only operation. Incorrect
Lua types and host representation bounds raise Lua argument errors.

```lua
local codec = tlv.codec {
    decode = function(bytes) return {payload = bytes} end,
    encode = function(value) return value.payload end,
}
assert(codec:decode(codec:encode {payload = "hello"}).payload == "hello")
```

Callbacks are optional and snapshotted at construction; a missing direction
reports `UNSUPPORTED`. Decode returns one Lua value, including `nil`; encode
must return a binary string. Encode calls the Lua function once and reuses
that string for the C size query and write. Callbacks run through a native
`tlv_codec_t` trampoline under protected Lua calls. Their original error
objects propagate unchanged after the C call returns. Recursive codec calls
and use from different coroutines are supported, but yielding through a
callback is not. Callback references participate in Lua garbage collection,
including cycles that capture the codec itself.

This API binds Value codecs. The separate `tlv_structure_codec_t` contract
for complete objects remains unbound. See the runnable
[codec example](../examples/codec.lua).

## Query and Document

`tlv.query(path)` compiles the C Query language: exact hexadecimal tags separated
by `/`, such as `E1/9F02`. Tags are byte identities; there are no wildcards,
indexes, predicates or recursive searches. `query:steps()` returns binary tag
strings. Query compilation errors raise `opentlv.Error` with a zero-based
character `offset`, including rejected embedded NUL characters.

`query:evaluate(data, format, options)` returns all matching entries in document
order, with the Reader fields `tag`, `length`, `value`, `offset`, plus `depth`
and `constructed`. The returned strings are copies. Evaluation delegates to the
native Query matcher over Tree Reader, scans the complete input (including
nonmatching branches), and reports Reader errors with available diagnostics.
`options.max_depth` defaults to the native tree default and `max_elements` to
65536. Limits must be nonnegative integers. This uses the supplied Format's
framing contract, not additional semantic Schema or full DER validation.

`query:matcher(format, options)` owns a resumable native matcher and Tree Reader.
Use `set_input(bytes, discard, final)`, `next()` or `visit(callback)`; returning
`false` stops after the current match, and subsequent calls resume. Nonfinal
exhaustion raises `NEED_MORE_DATA`, preserving state for input replacement.
`rebind(equivalent_query)` preserves suspended state; `reset()` clears both
matching and Reader state. `matches(tag, depth)` also supports manual preorder
feeds; reset before switching between manual and Reader input. Options bound
`max_depth`, `frame_capacity` and `max_elements`. Results own copied strings,
callback errors propagate, and callback reentry is rejected.

With `OPENTLV_DOCUMENT=ON` (the default), `tlv.document(data, format, options)`
parses into an owning native Document. Pass `nil` or an empty string to create
an empty document. The input can be released after parsing. The Document keeps
its Format alive, including configured Fixed formats. Options are native
`max_depth` and `max_elements`, with the C Document defaults. Query evaluation
and Document construction default to BER when the format is omitted; builds
without BER require an explicit format. Disabling Document omits
`tlv.document` and leaves Query available.

```lua
local tlv = require("opentlv")
local doc = tlv.document(string.char(0x9F, 0x02, 0x01, 0x05), tlv.formats.ber)
local amount = doc:find("9F02")
assert(amount:value() == string.char(5))
assert(doc:set("9F02", string.char(6)))
local result = doc:serialize()
```

Document operations:

| Method | Result and behavior |
| --- | --- |
| `find(path_or_query)` | First matching Node, or `nil`; searches past dead-end branches. |
| `query(path_or_query)` | Array of all matching Nodes in document order. |
| `first()` / `count()` | First root or `nil`; total number of nodes including descendants. |
| `set(target, value)` | Replace the first matched node's Value; returns `false` if absent, `true` on success. |
| `insert(tag, value, parent, before)` | Return a new Node; binary tag and Value strings are copied. Omitted parent means root level; omitted before means append. |
| `erase(target)` | Remove the node and descendants; returns whether a node was found. |
| `serialize(format)` | Encode the document, optionally with a compatible destination Format. |

Mutation targets and insertion positions accept a path, compiled Query or live
Node from the same Document. Missing explicit insertion positions and foreign
or invalidated Nodes raise Lua argument errors. `set` and `erase` affect only
the first path match; use `query` to select repeated entries individually.

Nodes expose `tag()`, `value()`, `is_constructed()`, `first_child()`, `next()`,
`parent()`, `next_same_tag()`, `set(value)`, `erase()` and `serialize(format)`.
Navigation returns `nil` at the end. `value()` returns copied primitive bytes,
including `""` for an empty primitive, and `nil` for constructed nodes; read
children or serialize the subtree instead. Constructed Value replacement and
insertion parse child bytes through the native Document format.

Each Node keeps its native Document alive. Removing a node invalidates all
handles to it and its descendants. Successful constructed Value replacement
invalidates handles to the former descendants; the parent and unrelated nodes
remain usable. Failed mutations leave both content and handles unchanged.
Invalidated handles raise argument errors before touching native node memory.

Native mutation and encoding failures raise `opentlv.Error` with the original
`code` and `message`. Document parse errors include a source `offset` when the
C API supplies one; mutation and encoding APIs do not provide detailed offsets,
so the binding does not invent them. Encoding uses the native Document/Tree
Writer contract: framing can be normalized, and incompatible destination
formats can reject the tree. Lua does not implement a separate DOM or mutation
engine.

## Compiled Query

`opentlv.query_program(text, format, options)` owns an immutable C program;
`query_program_load(image, format, options)` validates a same-release image.
Options include typed `variables`, named Tag maps and explicit compile limits.
Programs expose `info()`, `variables()`, `format()`, `explain()` and `image()`.

The program retains its Format, including configured Fixed Format userdata;
reuse that Format object with Documents evaluated by the program. Options
`tags = {id = ..., class_of = function(raw_tag) ... end, number_of = ...}`
supply optional semantic Tag capabilities. Callbacks return signed 64-bit
integers; IDs identify the contract when validating saved images. Missing
capabilities are rejected during compilation.

`resolve = function(namespace, name) ... end` returns a raw Tag string or `nil`
for an unknown name. It is mutually exclusive with a nonempty `names` map.
Checked compilation rejects resolver drift between preparation and commit,
including different Tags with equal byte lengths. Callback errors propagate.
`tlv.query_definition_resolver({scope = {{tag = raw_tag, name = "leaf"}}})`
snapshots the descriptors and returns a resolver using canonical C namespace and
ambiguity rules. `resolve = tlv.query_emv_resolve` selects the native base EMV
dictionary when enabled, including the `emv:PAN` alias.

`program:execution(options)` owns independent bounded state. `bind(name, type,
value)`, `set_input(bytes, discard, final)`, `next()`, `visit(callback)`,
`exists(early)`, `info()` and `reset()` delegate to C. Use
`evaluate_document(document)` for checked Nodes and `result()` for scalars.
Lua strings and match snapshots own copied bytes. Callback errors invalidate
the execution until reset, and reentrant calls are rejected. `close()` releases
native owners deterministically. Native failures raise tables with `code` and
Query diagnostic detail. `error.query.kind_name` is the canonical C category,
such as `syntax` or `state`. `error.query.expression` is the related expression
location, with kind `unknown` when `error.location` already is that span, and
`error.query.codec_detail.reported` is the raw provider result. A rejected path
text raises the same Query syntax or limit error. A Schema Query failure's
`error.schema` holds only the assertion's `kind`, `tag` and `field`, and is nil
for any other failure; its
location, path and expected text are the error's own. Reader `operation` includes `header`; Reader bounds
and field offsets retain their presence, and lengths beyond Lua's exact numeric
range use decimal strings. Codec detail adds `operation_name`, `cause_name` and
`violation_name` beside raw numeric values. See the
[binding diagnostic reference](https://github.com/OpenTLV/opentlv/blob/main/docs/reference/errors.md#binding-diagnostics) and
[consumer tests](../tests/program_spec.lua).

Compile options accept `providers = {num = {id = ..., decode = ...}, ...}` for
`num`, `bcd`, `text` and `date`. Callbacks receive a Value string and optional
copied event metadata; return an integer or a UTF-8 string for `text`. Set
`max_result_bytes` for text scratch and preserve IDs/contracts when loading
images. Provider errors propagate after returning through C; Query reentry from
providers is rejected.

`execution:feed(event)` accepts canonical event tables with `kind`, `tag`,
`value`, `depth` and `offset`. An optional `source` string contains exactly one
encoded element; C derives its Tag, Value and Layout through the program's
Format, preserving metadata such as `@hlen`. The execution retains the string.
Call `finish()` before pulling deferred results;
raw events and Reader input cannot be mixed before reset.
`next_ordinal()` returns an owned event table and traversal ordinal from a
completed retained selection, including Document selections. `next()` continues
to return checked Nodes for Documents.
`execution:edit_document(kind, tag, value, target_capacity)` edits a completed
selection and reports the applied count. Short target storage permits retry;
successful mutations invalidate checked Nodes.

`opentlv.query_schema_validate(rules, input, options)` validates a buffer string
or Document. Each rule has `context`, `assertion` and `name`; both programs are
retained during validation. Options bound depth, nodes, work, contexts and
Document Value snapshot capacity. Errors own the rule index, Query detail and
Schema diagnostic context. Provider callbacks cannot mutate or close the
Document being validated.

## Next step

Use [the basic model](https://github.com/OpenTLV/opentlv/blob/main/docs/concepts/learning-model.md) to choose borrowed processing
or owned editing, then [processing choices](https://github.com/OpenTLV/opentlv/blob/main/docs/guides/processing.md) for your next task.

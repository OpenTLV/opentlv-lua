# Generic Core API coverage

This is the reviewed baseline of the generic Core surface that the Lua
`opentlv` module supports. It was recorded for issue
[#31](https://github.com/OpenTLV/opentlv-lua/issues/31), which closes the
[coverage epic #25](https://github.com/OpenTLV/opentlv-lua/issues/25), against
Core commit `d87dcf96b2707e817dd51163a9f9efa36651e929` (15 commits after
0.7.1), as pinned by [`core-artifact.json`](../cmake/core-artifact.json). The
comparison baseline is Lua commit `9c27c7de77c4691e75b998f67d2778844eebcd13`,
`main` before this issue's changes.

Core coverage is **204/204 (100.0 %)** with 93 reviewed exclusions, up from
202/205 (98.5 %) with 92. Builtins stay at **61/291 (21.0 %)**. Builtins parity
is outside the epic.
[`Core API coverage`](quality-gates.md#core-api-coverage) now fails a pull
request or Full run when a generic Core symbol is neither referenced nor
excluded, or when an exclusion no longer matches the pinned Core.

## Closing the remaining gaps

Before this issue, the report listed three symbols as not referenced. Python
binds all three, and Rust binds the first two:

- `tlv_definition_find` was a real gap. `opentlv.definition_registry()`
  snapshots `{tag, name}` entry tables, and `registry:find(tag)` returns an
  owned copy of the first entry with an equal Tag and its 1-based index, or
  `nil`. `registry:definitions()` returns the entries in order. Python's
  equivalent is `DefinitionRegistry`, Rust's `DefinitionRegistry`. The entry
  shape is the one `opentlv.query_definition_resolver()` already accepted.
- `tlv_diagnostic_path_string` was a real gap. Every diagnostic table with a
  tracked path now has `path_text`, Core's formatting of that path, such as
  `"30 > A5"`. Python's equivalent is `OpenTLVError.path_text`, Rust's
  `path_text()`. The text is copied when the table is built, so it does not
  depend on the input or Schema staying alive. A truncated path ends in
  `" > ..."` and a tracked empty path is `""`. It is absent without a tracked
  path, and also for a path Core rejects, such as one with an empty Tag.
- `tlv_location_translate` adds a byte origin to a known location. Python uses
  it because its `write_element(storage, ..., offset=...)` writes at an offset
  into caller storage. Lua strings are immutable, so Lua has no caller
  destination, and every entry point passes Core the Lua string it received or
  owned output and reports Core's coordinates unchanged. Incremental Readers
  report absolute offsets through Core. The symbol is now a reviewed exclusion
  in [`api-coverage.json`](../cmake/api-coverage.json).

## Per-header inventory

`Referenced` counts the `TLV_API` symbols that `src` names, excluding the
reviewed exclusions. A referenced symbol shows that the module can reach it,
not that it covers all the functionality. This table records that mapping and
the binding tests behind it; the test files are in `tests/`.

| Core header | Referenced | Excluded | Public Lua API | Binding tests |
| --- | ---: | ---: | --- | --- |
| `tlv/codec/codec.h` | 2/2 | 0 | `codec:decode()` and `codec:encode()` of every codec | `codec_spec`, `codec_generator_spec` |
| `tlv/codec/diagnostic.h` | 5/5 | 0 | `codec_detail` with operation, cause, violation and reason names | `codec_spec`, `codec_generator_spec` |
| `tlv/codec/digits.h`, `number.h`, `text.h` | - | 6 | `codecs.digits{}`, `codecs.number{}`, `codecs.text{}` | `codec_spec` |
| `tlv/codec/ipv4.h` | 3/3 | 0 | `codecs.ipv4`, `codecs.ipv4_list` | `codec_spec` |
| `tlv/codec/structure.h` | 2/2 | 0 | `structure_codec()` | `codec_generator_spec` |
| `tlv/codec/values.h` | 7/7 | 0 | Integer and byte codecs such as `codecs.uint16_be`, `int64_minimal_be`, `bytes` | `codec_spec`, `document_query_schema_spec` |
| `tlv/definition.h` | 1/1 | 0 | `definition_registry()`, `find()`, `definitions()` | `definition_path_spec` |
| `tlv/diagnostic.h` | 4/4 | 10 | `location`, `severity`, `path_text` in diagnostic tables | `error_spec`, `schema_spec`, `definition_path_spec` |
| `tlv/document/document.h` | 41/41 | 0 | `document()`, Nodes, `revision()`, `visit()`, `source_location()`, `document_builder()` | `query_document_spec`, `document_query_schema_spec` |
| `tlv/field/escaped.h`, `fixed.h`, `variable.h` | 10/10 | 0 | `formats.compose()` field adapters | `compose_spec` |
| `tlv/field/packed.h` | 2/2 | 0 | `fields.read_packed()`, `fields.write_packed()` | `compose_spec` |
| `tlv/format.h` | 8/8 | 0 | Format methods, `constructed`, `source:preserve()` | `format_spec`, `compose_spec`, `stream_spec` |
| `tlv/formats/compose.h` | 3/3 | 12 | `formats.compose()` | `compose_spec` |
| `tlv/formats/escaped.h` | 1/1 | 3 | `formats.compose()` with an escaped length | `compose_spec` |
| `tlv/formats/fixed.h` | 1/1 | 0 | `formats.fixed()`, `formats.compose()` | `format_spec`, `compose_spec` |
| `tlv/formats/packed.h` | 4/4 | 0 | `formats.packed()` | `compose_spec` |
| `tlv/formats/variable.h` | 2/2 | 4 | `formats.compose()` with variable fields | `compose_spec` |
| `tlv/generator.h` | 2/2 | 0 | `generator()`, `generate()`, `workspace_size()` | `codec_generator_spec` |
| `tlv/query/adapters.h` | 3/3 | 0 | Query providers, codec providers, `query_definition_resolver()` | `program_spec`, `document_query_schema_spec` |
| `tlv/query/plan.h` | 2/2 | 0 | `query_plan_open()`, `plan_info()` | `document_query_schema_spec` |
| `tlv/query/program.h` | 31/31 | 0 | `query_program()`, executions, `reset()`, Query errors | `program_spec`, `document_query_schema_spec` |
| `tlv/query/query.h` | 9/9 | 1 | `query()`, `format()`, `visit()`, matcher `visit()` | `query_document_spec`, `document_query_schema_spec` |
| `tlv/reader/diagnostic.h` | 1/1 | 0 | Reader `operation` names | `reader_spec`, `stream_spec` |
| `tlv/reader/reader.h` | 11/11 | 2 | `reader()`, `read()`, `read_source()`, incremental input, `next_source()` | `reader_spec`, `stream_spec` |
| `tlv/reader/tree.h` | 9/9 | 2 | `tree_reader()` | `stream_spec` |
| `tlv/reader/visitor.h` | 2/2 | 1 | `visit()`, `visit_tree()`, `reader:visit()`, `tree_reader:visit()` | `visitor_spec`, `stream_spec` |
| `tlv/result.h` | 1/1 | 0 | `message`, `strerror()` | `error_spec`, `module_spec` |
| `tlv/schema/constraint.h` | 3/3 | 0 | `value_constraint()` | `document_query_schema_spec` |
| `tlv/schema/detail.h` | 2/2 | 0 | Schema `kind` and definition kind names | `schema_spec`, `codec_generator_spec` |
| `tlv/schema/number.h` | - | 2 | `codecs.schema_number{}` | `codec_generator_spec` |
| `tlv/schema/query.h` | 3/3 | 0 | `query_schema_validate()` | `program_spec`, `definition_path_spec` |
| `tlv/schema/schema.h` | 9/9 | 1 | `schema()`, `check()`, `first`, `length_schema()`, `cause` | `schema_spec`, `document_query_schema_spec` |
| `tlv/writer/tree.h` | 10/10 | 4 | `tree_writer()`, `write_event()`, `measure_tree()`, `measure_events()` | `writer_spec`, `stream_spec` |
| `tlv/writer/writer.h` | 10/10 | 11 | `writer()`, `copy_encoded()`, `preserve()`, `encoded_size()` | `writer_spec`, `stream_spec` |
| `tlv/copy.h`, `tlv/endian.h`, `tlv/size.h`, `tlv/tag.h`, `tlv/value.h` | - | 34 | Lua strings, string comparison and slicing | - |

The reason for every exclusion is in
[`api-coverage.json`](../cmake/api-coverage.json), and the
[quality gates](quality-gates.md#core-api-coverage) summarize the groups:
primitives, diagnostic producers, callbacks that bound descriptor initializers
and inline codec constructors install, NULL-diagnostic wrappers, the Tag/Value
and caller-storage write forms, `tlv_writer_copy_element`, `tlv_query_parse`,
`tlv_schema_check` and `tlv_location_translate`.

## Limitations

- Builtins-specific parity, including DOL, DER Schema, CER and DER validation,
  Bluetooth, DHCP and LLDP Schema APIs and the EMV tag constants, is tracked
  outside this epic. Official builds still compile every supported Core
  component and builtin, and existing Builtins bindings are unchanged.
- The gate counts names, not behavior. A reviewer still checks that a new
  reference is a usable public path with binding tests. Core behavior is tested
  in Core CI.
- Core's inline constructors `tlv_number_codec`, `tlv_text_codec`,
  `tlv_digits_codec` and `tlv_schema_number_codec` are not `TLV_API` symbols and
  do not appear in the report. The Lua codecs call them.
- Lua has no API for raw C codec or Format callbacks. Applications use Lua
  callbacks (custom codecs, structure codecs, Query providers) or compose
  Formats from Core's field encodings.
- In the pinned Core only Schema producers, including Query Schema validation,
  track enclosing paths, so `path`, `path_omitted` and `path_text` appear on
  those diagnostics only.

## Updating the Core pin

1. Run `python -B scripts/api_coverage.py --base origin/main` with the new lock,
   or `--core ../opentlv --commit <new> --base-commit <old>` before changing it.
   The pin-change table lists added and removed symbols and whether each added
   one is already referenced or excluded.
2. For every added symbol, bind it as public Lua API with binding tests, or add
   it to [`api-coverage.json`](../cmake/api-coverage.json) with a reason that
   explains why Lua has no separate path. Do not reference a symbol only to
   raise the count.
3. Remove ignore patterns the report lists as matching nothing.
4. Update this inventory, the exclusion summary in the
   [quality gates](quality-gates.md#core-api-coverage) and the
   [API guide](api.md) for any new Lua API.

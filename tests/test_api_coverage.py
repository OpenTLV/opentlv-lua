# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Marek Cingel
"""Exercise Core API coverage parsing, reporting and pin diffs against local repositories."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import api_coverage  # noqa: E402

HEADER = """\
#ifndef TLV_SAMPLE_H
#define TLV_SAMPLE_H
#define TLV_SAMPLE_DECLARE(name) TLV_API int name(void);
/* TLV_API int tlv_commented(void); */
#ifdef __cplusplus
extern "C" {
#endif
// TLV_API int tlv_line_commented(void);
TLV_API tlv_result_t tlv_multi_line(const uint8_t* data,
                                    size_t size, void (*visit)(void* context, int item));
TLV_NODISCARD TLV_API const char* TLV_PRINTF(1, 2) tlv_attributed(const char* format, ...);
extern TLV_API const tlv_codec_t tlv_codec_sample;
TLV_API extern const tlv_format_t tlv_format_sample;
extern TLV_API const uint8_t tlv_table[TLV_TABLE_SIZE][2];
typedef struct { int (*callback)(void); } tlv_plain_t;
#ifdef __cplusplus
}
#endif
#endif
"""


def git(repository, *args):
    return subprocess.run(['git', '-c', 'user.name=Test', '-c', 'user.email=test@example.com',
                           '-c', 'commit.gpgsign=false', '-C', str(repository), *args],
                          check=True, capture_output=True, text=True).stdout.strip()


def commit(repository, files):
    for name, text in files.items():
        path = repository / name
        if text is None:
            path.unlink()
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
    git(repository, 'add', '-A')
    git(repository, 'commit', '-q', '-m', 'change')
    return git(repository, 'rev-parse', 'HEAD')


def identifiers(name, text):
    return set(api_coverage.IDENTIFIER.findall(api_coverage.strip_comments(Path(name), text)))


class Parsing(unittest.TestCase):
    def test_declarations_cover_function_and_data_shapes(self):
        found = dict(api_coverage.declarations('tlv/include/tlv/sample.h', HEADER))
        self.assertEqual(found, {'tlv_multi_line': 'function', 'tlv_attributed': 'function',
                                 'tlv_codec_sample': 'data', 'tlv_format_sample': 'data',
                                 'tlv_table': 'data'})

    def test_unrecognized_declaration_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'Unrecognized TLV_API declaration'):
            list(api_coverage.declarations('tlv/x.h', 'TLV_API void (*tlv_handler(void))(int);'))

    def test_c_references_ignore_comments_but_keep_literals(self):
        c_text = ('static int l_read(lua_State* L) { tlv_a(L); // tlv_b\n'
                  '    /* tlv_c */ const char* s = "//"; tlv_d(\'"\'); }\n'
                  '/* lua_pushcfunction(L, tlv_e);\n   tlv_f */ luaL_Reg r = {"x", tlv_g};\n')
        found = identifiers('x.c', c_text)
        self.assertLessEqual({'tlv_a', 'tlv_d', 'tlv_g'}, found)
        self.assertFalse({'tlv_b', 'tlv_c', 'tlv_e', 'tlv_f'} & found)


class Configuration(unittest.TestCase):
    def test_ignores_require_a_reason_and_a_pattern(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'config.json'
            base = {'schema': 1, 'lock': 'lock.json', 'references': ['src']}
            for ignore in ({'symbols': ['tlv_x']}, {'reason': 'unbound'}):
                path.write_text(json.dumps(base | {'ignore': [ignore]}), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'reason'):
                    api_coverage.load_config(path)

    def test_ignored_symbols_and_stale_patterns(self):
        symbols = {'tlv_read_u16_be': {'kind': 'function', 'header': 'tlv/endian.h'},
                   'tlv_location_set': {'kind': 'function', 'header': 'tlv/diagnostic.h'},
                   'tlv_read': {'kind': 'function', 'header': 'tlv/reader/reader.h'}}
        config = {'ignore': [{'reason': 'primitives', 'headers': ['tlv/endian.h', 'tlv/gone.h']},
                             {'reason': 'producers', 'symbols': ['tlv_location_*', 'tlv_gone']}]}
        ignore, stale = api_coverage.ignored(config, symbols)
        self.assertEqual(ignore, {'tlv_read_u16_be': 'primitives', 'tlv_location_set': 'producers'})
        self.assertEqual(stale, ['tlv/gone.h', 'tlv_gone'])
        scopes = api_coverage.coverage(symbols, {'tlv_read', 'tlv_location_set'}, ignore)
        self.assertEqual(scopes['core']['headers']['tlv/diagnostic.h'],
                         {'referenced': [], 'missing': [], 'ignored': ['tlv_location_set']})
        self.assertEqual([scopes['core'][key] for key in ('referenced', 'missing', 'ignored')],
                         [1, 0, 2])

    def test_repository_configuration_measures_the_native_module(self):
        config = api_coverage.load_config(ROOT / 'cmake/api-coverage.json')
        self.assertEqual(config['lock'], 'cmake/core-artifact.json')
        self.assertTrue((ROOT / config['lock']).is_file())
        # Every Core call is in the native module; lua/opentlv/init.lua only re-exports it.
        self.assertEqual(config['references'], ['src'])
        facade = identifiers('init.lua', (ROOT / 'lua/opentlv/init.lua').read_text(encoding='utf-8'))
        self.assertEqual({name for name in facade if name.startswith('tlv_')}, set())


class Report(unittest.TestCase):
    def test_report_scopes_ignores_and_pin_diff(self):
        with tempfile.TemporaryDirectory() as directory:
            self.check_report(Path(directory))

    def check_report(self, tmp):
        core = tmp / 'core'
        core.mkdir()
        git(core, 'init', '-q')
        old = commit(core, {
            'tlv/include/tlv/reader.h': 'TLV_API int tlv_read(void);\nTLV_API int tlv_removed(void);\n',
            'tlv/include/tlv/endian.h': 'TLV_API int tlv_read_u16_be(void);\n',
            'tlv/include/tlv/builtins/emv/emv.h': 'extern TLV_API const tlv_format_t tlv_format_emv;\n'})
        new = commit(core, {
            'tlv/include/tlv/reader.h': 'TLV_API int tlv_read(void);\nTLV_API int tlv_added(void);\n'
                                        'TLV_API int tlv_producer(void);\n',
            'tlv/include/tlv/endian.h': None, 'tlv/include/tlv/builtins/emv/emv.h': None})
        binding = tmp / 'binding'
        binding.mkdir()
        git(binding, 'init', '-q')
        config = {'schema': 1, 'lock': 'cmake/core-artifact.json', 'references': ['src'],
                  'ignore': [{'reason': 'primitives', 'headers': ['tlv/endian.h']},
                             {'reason': 'producers', 'symbols': ['tlv_producer', 'tlv_gone']}]}
        # The Lua lock names no Core version.
        lock = {'schema': 1, 'repository': 'OpenTLV/opentlv', 'commit': old, 'release_tag': None,
                'allow_verified_source': True}
        base = commit(binding, {
            'cmake/core-artifact.json': json.dumps(lock),
            'cmake/api-coverage.json': json.dumps(config),
            # Outside the reference directories, so it stays missing.
            'tests/failing_allocator.c': 'int f(void) { return tlv_added(); }\n',
            'src/module.c': 'static int l_read(lua_State* L) { tlv_read(); '
                            'return tlv_format_emv.id; }\n/* tlv_added */\n// tlv_added\n'})
        commit(binding, {'cmake/core-artifact.json': json.dumps(lock | {'commit': new})})
        output = tmp / 'report.json'
        summary = tmp / 'summary.md'
        summary.write_text('previous\n', encoding='utf-8')
        result = subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/api_coverage.py'),
                                 '--core', str(core), '--base', base, '--output', str(output),
                                 '--summary', str(summary)],
                                cwd=binding, check=True, capture_output=True, text=True,
                                encoding='utf-8')
        report = json.loads(output.read_text(encoding='utf-8'))
        self.assertEqual(report['core'], {'repository': 'OpenTLV/opentlv', 'version': None,
                                          'commit': new})
        self.assertEqual(report['scopes']['core']['headers']['tlv/reader.h'], {
            'referenced': ['tlv_read'], 'missing': ['tlv_added'], 'ignored': ['tlv_producer']})
        self.assertEqual([report['scopes']['core'][key]
                          for key in ('referenced', 'missing', 'ignored')], [1, 1, 1])
        self.assertEqual(report['scopes']['builtins']['headers'], {})
        self.assertEqual(report['stale_ignores'], ['tlv/endian.h', 'tlv_gone'])
        self.assertEqual(report['diff']['base']['commit'], old)
        self.assertEqual([(item['name'], item.get('status')) for item in report['diff']['added']],
                         [('tlv_added', 'missing'), ('tlv_producer', 'ignored')])
        self.assertEqual({item['name'] for item in report['diff']['removed']},
                         {'tlv_format_emv', 'tlv_read_u16_be', 'tlv_removed'})
        self.assertIn('does not prove functional parity', result.stdout)
        self.assertIn('| core | 1/2 (50.0 %) | 1 |', result.stdout)
        self.assertIn(f'OpenTLV/opentlv `{old[:12]}` -> OpenTLV/opentlv `{new[:12]}`: '
                      '2 added, 3 removed', result.stdout)
        self.assertEqual(summary.read_text(encoding='utf-8'), 'previous\n' + result.stdout)


class Workflow(unittest.TestCase):
    def test_coverage_job_is_informational(self):
        checks = yaml.load((ROOT / '.github/workflows/lua-checks.yml').read_text(encoding='utf-8'),
                           Loader=yaml.BaseLoader)
        job = checks['jobs']['api_coverage']
        self.assertEqual(job['continue-on-error'], 'true')
        # lua-checks.yml serves PR and Full only; Main has its own jobs.
        self.assertNotIn('if', job)
        self.assertNotIn('api_coverage', checks['jobs']['gate']['needs'])
        checkout, report, upload = job['steps']
        self.assertEqual(checkout['with']['fetch-depth'], '0')
        self.assertEqual(report['env']['BASE_SHA'], '${{ github.event.pull_request.base.sha }}')
        self.assertIn('scripts/api_coverage.py', report['run'])
        self.assertIn('$GITHUB_STEP_SUMMARY', report['run'])
        self.assertEqual(upload['with']['path'], 'build/api-coverage.json')


if __name__ == '__main__':
    unittest.main()

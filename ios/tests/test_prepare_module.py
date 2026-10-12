#!/usr/bin/env python3
"""Original synthetic ELF + lifted-code pipeline tests. No Nintendo input."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('prepare_module', REPO/'ios/scripts/prepare_module.py')
prep = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prep)
MAIN = 0x7600000000
SDK = 0x7700000000


def elf(provider=False):
    data = bytearray(4096)
    data[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHIQQQIHHHHHH', data, 16, 3, 183, 1, 0x100, 64, 0, 0, 64, 56, 2, 0, 0, 0)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 7, 0, 0, 0, 0xc00, 0x2000, 1)
    names = [b'', b'sdk_add', b'sdk_data'] if provider else [b'', b'sdk_add', b'main_entry', b'optional_hook']
    strings = b'\0'
    offsets = []
    for name in names:
        if not name:
            offsets.append(0)
        else:
            offsets.append(len(strings)); strings += name + b'\0'
    needed = len(strings); strings += b'sdk\0'
    tags = [(4, 0x800), (5, 0x900), (6, 0x600), (10, len(strings)), (11, 24)]
    if not provider:
        tags.append((1, needed))
    tags.append((0, 0))
    struct.pack_into('<IIQQQQQQ', data, 120, 2, 6, 0x300, 0x300, 0, len(tags)*16, len(tags)*16, 1)
    for i, tag in enumerate(tags):
        struct.pack_into('<qQ', data, 0x300+i*16, *tag)
    data[0x900:0x900+len(strings)] = strings
    struct.pack_into('<II', data, 0x800, 1, len(names))
    for i, noff in enumerate(offsets[1:], 1):
        defined = provider or i == 2
        value = 0x1800 if provider and i == 2 else (0x100 if defined else 0)
        info = 0x11 if provider and i == 2 else (0x20 if i == 3 else 0x12)
        struct.pack_into('<IBBHQQ', data, 0x600+i*24, noff, info, 0, int(defined), value, 4 if defined else 0)
    return bytes(data)


def table(bias):
    return f'const FnEntry fn_table[] = {{\n {{0x{bias+0x100:x}ULL, f_{bias+0x100:x}}},\n}};\n'


def create_fixture(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    for module, bias, provider in [('main', MAIN, False), ('sdk', SDK, True)]:
        gen = directory/(module+'_gen'); gen.mkdir(exist_ok=True)
        (directory/(module+'.elf')).write_bytes(elf(provider))
        (gen/'table.c').write_text('#error original table must be excluded\n'+table(bias))
        (gen/'imports.c').write_text('#error original imports must be excluded\n')
        body = 'c->x[0] += 5;' if provider else f'c->x[30] = 0x{MAIN+0x108:x}ULL; host_import(c, 1);'
        (gen/'lifted.c').write_text(f'#include "runtime.h"\nvoid f_{bias+0x100:x}(Ctx *c) {{ {body} }}\n')
        subprocess.run([sys.executable, str(REPO/'ios/scripts/prepare_module.py'),
                        '--elf', str(directory/(module+'.elf')), '--functions', str(gen/'table.c'),
                        '--module', module, '--load-bias', hex(bias), '--function-addresses', 'guest',
                        '--output', str(directory/(module+'_metadata.c'))],
                       check=True, capture_output=True, text=True)
    harness = '''#include "GuestRuntime.h"
extern const SSBUIOSGuestModule ssbu_main_module, ssbu_sdk_module;
extern const SSBUIOSModuleSymbols ssbu_main_module_symbols, ssbu_sdk_module_symbols;
int main(void) {
    SSBUIOSGuestModule modules[] = {ssbu_main_module, ssbu_sdk_module};
    SSBUIOSModuleSymbols tables[] = {ssbu_sdk_module_symbols, ssbu_main_module_symbols};
    SSBUIOSModuleRegistry registry = {0}; SSBUIOSGuestRuntime *runtime = NULL;
    Ctx c = {0}; c.x[0] = 7;
    if (ssbu_ios_modules_init(&registry, modules, 2) ||
        ssbu_ios_runtime_create(&runtime, &registry, tables, 2) ||
        ssbu_ios_runtime_bind(runtime) || ssbu_ios_runtime_call(&c, UINT64_C(0x7600000100)) ||
        c.x[0] != 12 || ssbu_ios_runtime_close(&runtime) != -1) return 1;
    ssbu_ios_runtime_unbind();
    return ssbu_ios_runtime_close(&runtime);
}
'''
    (directory/'harness.c').write_text(harness)
    return directory


class ModulePreparation(unittest.TestCase):
    def prepare(self, data=None, text=None, **kwargs):
        return prep.prepare(elf() if data is None else data, table(MAIN) if text is None else text,
                            'main', 'main', MAIN, kwargs.get('mode', 'guest'), 'fn_table')

    def test_metadata_preserves_indices_offsets_and_dependencies(self):
        source, report = self.prepare()
        self.assertEqual(report['function_count'], 1)
        self.assertEqual(report['symbol_count'], 4)
        self.assertEqual(report['dependencies'], ['sdk'])
        self.assertEqual(report['guest_start'], hex(MAIN))
        self.assertIn('{UINT64_C(0x100), f_7600000100}', source)
        offset_source, _ = self.prepare(text=table(0), mode='offset')
        self.assertIn('{UINT64_C(0x100), f_100}', offset_source)
        self.assertEqual(prep.c_string(b'"\\\xff9'), '"\\042\\134\\377\\071"')

    def test_definition_eligibility_preserves_visibility_and_binding(self):
        for info, visibility, expected in [(0x12, 0, 1), (0x22, 0, 1),
                                            (0x12, 1, 0), (0x12, 2, 0),
                                            (0x12, 3, 0), (0x02, 0, 0)]:
            with self.subTest(info=info, visibility=visibility):
                data = bytearray(elf(True)); data[0x618+4] = info; data[0x618+5] = visibility
                source, report = prep.prepare(bytes(data), table(SDK), 'sdk', 'sdk', SDK, 'guest', 'fn_table')
                self.assertEqual(report['preemptible_definition_count'], expected)
                self.assertIn('ssbu_sdk_preemptible', source)

    def test_malformed_elf_symbol_and_bounds_rejection(self):
        mutations = [(18, '<H', 62), (32, '<Q', 4080), (64+40, '<Q', 1),
                     (120+8, '<Q', 0x310), (0x308, '<Q', 0x1800),
                     (0x804, '<I', 0xffffffff), (0x808, '<I', 0xffffffff), (0x618, '<I', 0xffffffff),
                     (0x600+2*24+6, '<H', 0xfff1), (0x600+2*24+8, '<Q', 0x3000)]
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset, value=value):
                data = bytearray(elf()); struct.pack_into(fmt, data, offset, value)
                with self.assertRaises(ValueError): self.prepare(bytes(data))
        data = bytearray(elf()); data[0x900:] = b'x'*(len(data)-0x900)
        with self.assertRaises(ValueError): self.prepare(bytes(data))

    def test_function_table_and_identifier_rejection(self):
        for text in [table(MAIN).replace('0x7600000100', '0x7600001800'),
                     table(MAIN).replace('0x7600000100', '0x7600000101'),
                     table(MAIN).replace('f_7600000100', '(void *)0'),
                     table(MAIN)+table(MAIN), 'const FnEntry fn_table[] = {};']:
            with self.subTest(text=text):
                with self.assertRaises(ValueError): self.prepare(text=text)
        with self.assertRaises(ValueError):
            prep.prepare(elf(), table(MAIN), 'bad-id', 'main', MAIN, 'guest', 'fn_table')

    def test_cli_invalid_input_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp); (d/'bad.elf').write_bytes(b'not an ELF')
            (d/'table.c').write_text(table(MAIN)); (d/'metadata.c').write_text('sentinel')
            (d/'metadata.json').write_text('report sentinel')
            result = subprocess.run([sys.executable, str(REPO/'ios/scripts/prepare_module.py'),
                '--elf', str(d/'bad.elf'), '--functions', str(d/'table.c'), '--module', 'main',
                '--load-bias', hex(MAIN), '--function-addresses', 'guest', '--output', str(d/'metadata.c')],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((d/'metadata.c').read_text(), 'sentinel')
            self.assertEqual((d/'metadata.json').read_text(), 'report sentinel')

    def test_two_module_archives_link_through_actual_runtime(self):
        with tempfile.TemporaryDirectory() as temp:
            d = create_fixture(temp)
            compiler = os.environ.get('CC', 'cc')
            common = [compiler, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                      '-I', str(REPO/'runtime'), '-I', str(REPO/'ios/Portability')]
            for module in ['main', 'sdk']:
                objects = []
                for tag, source in [('lifted', d/(module+'_gen')/'lifted.c'),
                                    ('metadata', d/(module+'_metadata.c'))]:
                    obj = d/(module+'_'+tag+'.o'); objects.append(str(obj))
                    subprocess.run(common+['-c', str(source), '-o', str(obj)], check=True, capture_output=True, text=True)
                subprocess.run(['ar', 'rcs', str(d/(module+'.a'))]+objects, check=True, capture_output=True, text=True)
            subprocess.run(common+['-pthread', str(d/'harness.c'),
                str(REPO/'ios/Portability/GuestModules.c'), str(REPO/'ios/Portability/GuestRuntime.c'),
                str(d/'main.a'), str(d/'sdk.a'), '-o', str(d/'pipeline-test')],
                check=True, capture_output=True, text=True)
            subprocess.run([str(d/'pipeline-test')], check=True)
            report = json.loads((d/'sdk_metadata.json').read_text())
            self.assertEqual(report['export_count'], 2)

    def test_cli_cannot_overwrite_input_via_source_or_report(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp); (d/'main.elf').write_bytes(elf())
            for name in ['metadata.c', 'metadata.json']:
                input_file = d/name; input_file.write_text(table(MAIN))
                result = subprocess.run([sys.executable, str(REPO/'ios/scripts/prepare_module.py'),
                    '--elf', str(d/'main.elf'), '--functions', str(input_file), '--module', 'main',
                    '--load-bias', hex(MAIN), '--function-addresses', 'guest',
                    '--output', str(d/'metadata.c')], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('output cannot replace input', result.stderr)
                self.assertEqual(input_file.read_text(), table(MAIN))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--fixture-dir', type=Path)
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ModulePreparation)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful(): raise SystemExit(1)
    if args.fixture_dir:
        create_fixture(args.fixture_dir)
        print('Prepared original private-path fixture for iOS archive CI')

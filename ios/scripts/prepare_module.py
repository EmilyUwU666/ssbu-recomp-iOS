#!/usr/bin/env python3
"""Prepare private static iOS module metadata from an AArch64 ELF and lifted table.

No code is lifted/executed here. Inputs and output derived from a title dump
must stay local. Only the upstream generated FnEntry table syntax is accepted.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

MAX_TABLE = 64 * 1024 * 1024
MAX_IMAGE = 512 * 1024 * 1024
MAX_SYMBOLS = 1048576
U64_MAX = (1 << 64) - 1


def require(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def number(value: str) -> int:
    result = int(value, 0)
    require(0 <= result <= U64_MAX, "load bias must be an unsigned 64-bit value")
    return result


def c_string(value: bytes) -> str:
    # Fixed-width octal prevents quotes, backslashes or adjacent hex digits
    # changing a generated C token; preserves exact ELF symbol-name bytes.
    return '"' + ''.join(f'\\{b:03o}' for b in value) + '"'


class ELF:
    def __init__(self, data: bytes):
        self.data = data
        require(len(data) >= 64 and data[:7] == b'\x7fELF\x02\x01\x01',
                "expected little-endian ELF64 v1")
        header = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)
        typ, machine, version, self.entry, phoff = header[:5]
        require((typ, machine, version) == (3, 183, 1), "expected AArch64 ET_DYN")
        require(header[7:9] == (64, 56), "invalid ELF/program header sizes")
        count = header[9]
        require(1 <= count <= 256 and phoff <= len(data) and count * 56 <= len(data) - phoff,
                "invalid program header table")
        self.loads = []
        dynamic = None
        for i in range(count):
            kind, flags, offset, va, _, filesz, memsz, _ = struct.unpack_from('<IIQQQQQQ', data, phoff+i*56)
            require(kind not in (3, 7), "PT_INTERP/TLS is unsupported")
            if kind == 1:
                require(0 < memsz and filesz <= memsz and va + memsz <= U64_MAX and
                        offset <= len(data) and filesz <= len(data) - offset,
                        "invalid load segment")
                self.loads.append((va, offset, filesz, memsz, flags))
            elif kind == 2:
                require(dynamic is None and filesz <= memsz, "invalid/duplicate dynamic segment")
                dynamic = (va, offset, filesz)
        self.loads.sort()
        require(bool(self.loads), "ELF contains no load segments")
        for a, b in zip(self.loads, self.loads[1:]):
            require(a[0] + a[3] <= b[0], "overlapping load segments")
        self.low = self.loads[0][0]
        self.high = max(v + m for v, _, _, m, _ in self.loads)
        require(self.high - self.low <= MAX_IMAGE, "guest image exceeds runtime limit")
        require(self.entry % 4 == 0 and self.contains(self.entry, 4, True),
                "ELF entry must be aligned and in executable file-backed data")
        require(dynamic is not None, "module metadata requires PT_DYNAMIC")
        va, offset, size = dynamic
        require(0 < size <= MAX_TABLE and size % 16 == 0, "invalid dynamic table size")
        raw, actual = self.file_table(va, size)
        require(offset == actual, "dynamic PHDR and load segment describe different bytes")
        self.tags = {}
        self.needed = []
        terminated = False
        for tag, value in struct.iter_unpack('<qQ', raw):
            if tag == 0:
                terminated = True
                break
            require(tag not in (17, 18, 19, 35, 36, 37, 0x6000000f, 0x60000010,
                                0x60000011, 0x60000012, 0x6ffffff0, 0x6ffffffc, 0x6ffffffe),
                    "REL/packed relocations or symbol versioning are unsupported")
            if tag == 1:
                self.needed.append(value)
            elif tag in (4, 5, 6, 10, 11):
                require(tag not in self.tags, "duplicate symbol-table dynamic tag")
                self.tags[tag] = value
        require(terminated, "unterminated dynamic table")
        require(set(self.tags) == {4, 5, 6, 10, 11} and self.tags[11] == 24,
                "requires SysV DT_HASH, SYMTAB/SYMENT=24 and STRTAB/STRSZ")
        hash_header, _ = self.file_table(self.tags[4], 8)
        buckets, self.symbol_count = struct.unpack('<II', hash_header)
        require(buckets > 0 and 1 <= self.symbol_count <= MAX_SYMBOLS, "invalid SysV hash counts")
        hashed, _ = self.file_table(self.tags[4], (2 + buckets + self.symbol_count) * 4)
        for (index,) in struct.iter_unpack('<I', hashed[8:]):
            require(index < self.symbol_count, "hash index outside symbol table")
        self.symbols, _ = self.file_table(self.tags[6], self.symbol_count * 24)
        self.strings, _ = self.file_table(self.tags[5], self.tags[10])
        require(self.strings[0] == 0 and self.symbols[:24] == bytes(24), "invalid null symbol/string")
        self.dependencies = [self.string(n) for n in self.needed]

    def file_table(self, address: int, size: int) -> tuple[bytes, int]:
        require(0 < size <= MAX_TABLE, "metadata table exceeds size limit")
        for va, offset, filesz, _, _ in self.loads:
            if va <= address <= va + filesz and size <= va + filesz - address:
                pos = offset + address - va
                return self.data[pos:pos+size], pos
        raise ValueError("metadata table is not file-backed by one load segment")

    def string(self, offset: int) -> bytes:
        require(0 <= offset < len(self.strings), "symbol/dependency string offset out of bounds")
        end = self.strings.find(b'\0', offset)
        require(end >= 0, "unterminated symbol/dependency name")
        return self.strings[offset:end]

    def contains(self, address: int, size: int = 1, executable: bool = False) -> bool:
        return any(va <= address and size <= va + (fs if executable else ms) - address and
                   address < va + (fs if executable else ms) and (not executable or flags & 1)
                   for va, _, fs, ms, flags in self.loads)


def lifted_functions(text: str, table: str, bias: int, address_kind: str, elf: ELF) -> list[tuple[int, str]]:
    require(bool(re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', table)), "invalid C function-table name")
    pattern = r'const\s+FnEntry\s+' + re.escape(table) + r'\s*\[\s*\]\s*=\s*\{(.*?)\};'
    matches = re.findall(pattern, text, re.S)
    require(len(matches) == 1, "expected exactly one named generated FnEntry table")
    body = matches[0]
    entry = re.compile(r'\s*\{\s*(0x[0-9a-fA-F]+)(?:ULL|UL|LL|U|L)?\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\}\s*(,?)')
    result = []
    pos = 0
    while body[pos:].strip():
        m = entry.match(body, pos)
        require(m is not None, "unsupported generated function-table syntax")
        value = int(m[1], 16)
        offset = value - bias if address_kind == 'guest' else value
        require(0 <= offset <= U64_MAX and offset % 4 == 0 and elf.contains(offset, 4, True),
                "function entry outside executable file-backed segment or unaligned")
        result.append((offset, m[2]))
        pos = m.end()
        require(not body[pos:].strip() or bool(m[3]), "function entries must be comma-separated")
        require(len(result) <= MAX_SYMBOLS, "too many lifted functions")
    require(bool(result), "empty lifted function table")
    require(len({a for a, _ in result}) == len(result), "duplicate lifted guest address")
    require(len({s for _, s in result}) == len(result), "duplicate lifted C function symbol")
    return sorted(result)


def prepare(data: bytes, table_text: str, module: str, name: str, bias: int,
            address_kind: str, table_name: str) -> tuple[str, dict]:
    require(bool(re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', module)), "module ID must be a C identifier")
    require(bool(name) and '\0' not in name, "module name cannot be empty or contain NUL")
    require(address_kind in ('guest', 'offset') and 0 <= bias <= U64_MAX, "invalid address mode/load bias")
    elf = ELF(data)
    require(elf.high <= U64_MAX - bias, "rebased module address overflows")
    functions = lifted_functions(table_text, table_name, bias, address_kind, elf)
    symbols = []
    exports = {}
    for i in range(elf.symbol_count):
        noff, info, other, section, value, size = struct.unpack_from('<IBBHQQ', elf.symbols, i*24)
        symbol_name = elf.string(noff)
        binding, typ = info >> 4, info & 15
        require(i == 0 or bool(symbol_name), "runtime DynSym cannot represent unnamed non-null symbols")
        require(binding <= 2 and typ <= 2 and other & ~3 == 0, "unsupported binding/type/PCS flags")
        require(section < 0xff00, "special/absolute symbols cannot be represented by runtime DynSym")
        if section:
            require(elf.contains(value, size or 1), "defined symbol outside load segments")
        elif i:
            require(binding in (1, 2) and other == 0, "invalid undefined symbol")
        symbols.append((symbol_name, value, int(bool(section))))
        if section and binding in (1, 2) and other in (0, 3):
            require(symbol_name not in exports, "duplicate exported symbol name")
            exports[symbol_name] = value
    require(all(elf.contains(value) for value in exports.values()), "invalid exported symbol offset")
    prefix = 'ssbu_' + module
    lines = ['/* PRIVATE derived module metadata: do not commit or redistribute. */',
             '#include "GuestRuntime.h"', '']
    lines += [f'extern void {symbol}(Ctx *);' for _, symbol in functions]
    lines += [f'const FnEntry {prefix}_functions[] = {{']
    lines += [f'    {{UINT64_C(0x{offset:x}), {symbol}}},' for offset, symbol in functions]
    lines += ['};', f'const DynSym {prefix}_symbols[] = {{']
    lines += [f'    {{{c_string(n)}, UINT64_C(0x{v:x}), {d}}},' for n, v, d in symbols]
    lines += ['};', f'const SSBUIOSGuestExport {prefix}_exports[] = {{']
    lines += [f'    {{{c_string(n)}, UINT64_C(0x{v:x})}},' for n, v in sorted(exports.items())] or ['    {NULL, 0},']
    cname = c_string(name.encode('utf-8'))
    lines += ['};', f'const SSBUIOSGuestModule {prefix}_module = {{',
              f'    {cname}, UINT64_C(0x{bias:x}), UINT64_C(0x{bias+elf.low:x}), UINT64_C(0x{elf.high-elf.low:x}),',
              f'    {prefix}_functions, {len(functions)}, {prefix}_exports, {len(exports)}', '};',
              f'const SSBUIOSModuleSymbols {prefix}_module_symbols = {{',
              f'    {cname}, {prefix}_symbols, {len(symbols)}', '};', '']
    manifest = dict(schema_version=1, module_id=module, module_name=name,
                    elf_sha256=hashlib.sha256(data).hexdigest(), load_bias=hex(bias),
                    guest_start=hex(bias+elf.low), guest_size=hex(elf.high-elf.low),
                    entry_offset=hex(elf.entry), function_count=len(functions),
                    symbol_count=len(symbols), export_count=len(exports),
                    dependencies=[n.decode('latin-1') for n in elf.dependencies],
                    function_addresses=address_kind, input_function_table=table_name,
                    descriptor_symbol=prefix+'_module', symbols_descriptor=prefix+'_module_symbols')
    return '\n'.join(lines), manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--functions', type=Path, required=True, help='Private upstream-generated table.c')
    parser.add_argument('--module', required=True, help='Unique C identifier for generated table symbols')
    parser.add_argument('--name', help='Exact registry/dependency name; defaults to module ID')
    parser.add_argument('--load-bias', type=number, required=True)
    parser.add_argument('--function-addresses', choices=['guest', 'offset'], required=True)
    parser.add_argument('--function-table', default='fn_table')
    parser.add_argument('--output', type=Path, required=True, help='Private output .c; report is adjacent .json')
    args = parser.parse_args()
    require(args.elf.stat().st_size <= 1024*1024*1024, 'ELF exceeds input size limit')
    require(args.functions.stat().st_size <= MAX_TABLE, 'lifted table exceeds input size limit')
    require(args.output.suffix == '.c', 'metadata output must end in .c')
    out = args.output.resolve()
    require({out, out.with_suffix('.json')}.isdisjoint(
            {args.elf.resolve(), args.functions.resolve()}), 'output cannot replace input')
    source, report = prepare(args.elf.read_bytes(), args.functions.read_text(), args.module,
                             args.name or args.module, args.load_bias,
                             args.function_addresses, args.function_table)
    # Finish ALL parsing/validation before changing existing output files.
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(source)
    out.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Prepared {report["function_count"]} functions, {report["symbol_count"]} symbols, {report["export_count"]} exports')
    print('PRIVATE metadata only: no guest code has been executed or linked into an app.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, UnicodeError, struct.error) as error:
        raise SystemExit(f'Module preparation failed: {error}')

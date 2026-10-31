"""Offline AMD64 PE32+ metadata for a thread-start RVA (standard library only).

Reads bytes, never executes a DLL, attaches to a process or downloads symbols.
Runtime-function records may describe a function portion; nearest exports do
not name the containing private routine. Conservative validation rejects
ambiguous/unsupported layouts. This is not a general malicious-file sandbox.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import uuid


class PEFormatError(ValueError):
    """A fixed diagnostic code, with no file bytes or arbitrary input text."""


def require(condition, code):
    if not condition:
        raise PEFormatError(code)


class PE:
    def __init__(self, data):
        self.data = data
        require(self.span(0, 2) == b'MZ', 'missing_dos_signature')
        pe = self.u32(0x3c)
        require(pe >= 0x40 and self.span(pe, 4) == b'PE\0\0', 'missing_pe_signature')
        self.span(pe, 24)
        require(self.u16(pe + 4) == 0x8664, 'unsupported_machine_requires_amd64')
        count, optional_size = self.u16(pe + 6), self.u16(pe + 20)
        require(0 < count <= 96, 'unsupported_section_count')
        optional = pe + 24
        self.span(optional, optional_size)
        require(optional_size >= 112 and self.u16(optional) == 0x20b, 'requires_pe32_plus')
        directory_count = self.u32(optional + 108)
        require(directory_count <= (optional_size - 112) // 8, 'directory_table_truncated')
        self.directories = [struct.unpack('<II', self.span(optional + 112 + i * 8, 8))
                            for i in range(directory_count)]
        self.image_size, self.headers = self.u32(optional + 56), self.u32(optional + 60)
        require(0 < self.headers <= len(data) and self.headers <= self.image_size, 'invalid_image_sizes')
        table = optional + optional_size
        self.span(table, count * 40)
        require(table + count * 40 <= self.headers, 'section_table_outside_headers')
        self.sections = []
        for i in range(count):
            offset = table + 40 * i
            virtual, rva, raw_size, raw = struct.unpack('<IIII', self.span(offset + 8, 16))
            size = max(virtual, raw_size)
            require(size > 0 and rva >= self.headers and rva + size <= self.image_size,
                    'section_outside_image')
            if raw_size:
                require(raw >= self.headers, 'section_raw_overlaps_headers')
                self.span(raw, raw_size)
            for other_rva, other_size, other_raw, other_raw_size in self.sections:
                require(rva + size <= other_rva or other_rva + other_size <= rva,
                        'ambiguous_virtual_sections')
                if raw_size and other_raw_size:
                    require(raw + raw_size <= other_raw or other_raw + other_raw_size <= raw,
                            'ambiguous_raw_sections')
            self.sections.append((rva, size, raw, raw_size))

    def span(self, offset, size):
        require(0 <= offset <= len(self.data) and 0 <= size <= len(self.data) - offset,
                'file_range_out_of_bounds')
        return self.data[offset:offset + size]

    def u16(self, offset):
        return struct.unpack('<H', self.span(offset, 2))[0]

    def u32(self, offset):
        return struct.unpack('<I', self.span(offset, 4))[0]

    def map(self, rva, size=1):
        require(size > 0 and 0 <= rva < self.image_size and size <= self.image_size - rva,
                'rva_outside_image')
        if rva < self.headers:
            require(size <= self.headers - rva, 'rva_crosses_headers')
            return rva, self.headers - rva
        for base, virtual_size, raw, raw_size in self.sections:
            if base <= rva < base + virtual_size:
                delta = rva - base
                require(delta < raw_size and size <= raw_size - delta, 'rva_not_file_backed')
                return raw + delta, raw_size - delta
        raise PEFormatError('rva_not_mapped')

    def virtual_address(self, rva):
        # EAT entries may export data in a zero-filled virtual tail. They need
        # an image location, not file bytes and not an assumption of executable code.
        require(0 < rva < self.image_size and
                (rva < self.headers or any(base <= rva < base + size
                                          for base, size, _, _ in self.sections)),
                'export_outside_mapped_image')

    def directory(self, index):
        rva, size = self.directories[index] if index < len(self.directories) else (0, 0)
        require(bool(rva) == bool(size), 'incomplete_directory')
        return (rva, size, self.map(rva, size)[0]) if size else (0, 0, None)

    def ascii(self, offset, maximum):
        data = self.span(offset, maximum)
        end = data.find(b'\0')
        require(end >= 0, 'unterminated_string')
        try:
            return data[:end].decode('ascii')
        except UnicodeDecodeError:
            raise PEFormatError('non_ascii_metadata') from None

    def rva_string(self, rva, maximum=None):
        offset, available = self.map(rva)
        return self.ascii(offset, min(available, maximum) if maximum is not None else available)


def inspect(path, address):
    data = path.read_bytes()
    pe = PE(data)
    pe.map(address)  # Reject RVAs outside file-backed image data.
    export_rva, export_size, table = pe.directory(0)
    exports = []
    if table is not None:
        require(export_size >= 40, 'export_directory_too_small')
        function_count, name_count = pe.u32(table + 20), pe.u32(table + 24)
        require(function_count > 0 or name_count == 0, 'names_without_functions')
        functions = pe.map(pe.u32(table + 28), function_count * 4)[0] if function_count else None
        names = pe.map(pe.u32(table + 32), name_count * 4)[0] if name_count else None
        ordinals = pe.map(pe.u32(table + 36), name_count * 2)[0] if name_count else None
        for i in range(name_count):
            ordinal = pe.u16(ordinals + i * 2)
            require(ordinal < function_count, 'export_ordinal_out_of_bounds')
            name = pe.rva_string(pe.u32(names + i * 4))
            target = pe.u32(functions + ordinal * 4)
            if not target:
                continue  # An unused EAT slot is not a routine address.
            if export_rva <= target < export_rva + export_size:
                pe.rva_string(target, export_rva + export_size - target)
                continue  # A forwarded export is not local code.
            pe.virtual_address(target)
            exports.append((target, name))
    exports.sort()
    before = [item for item in exports if item[0] < address]
    after = [item for item in exports if item[0] > address]
    _, exception_size, table = pe.directory(3)
    records = []
    if table is not None:
        require(exception_size % 12 == 0, 'partial_runtime_function_record')
        for offset in range(table, table + exception_size, 12):
            begin, end, unwind = struct.unpack('<III', pe.span(offset, 12))
            require(begin < end <= pe.image_size, 'invalid_runtime_function_range')
            pe.map(begin)
            pe.map(end - 1)
            pe.map(unwind, 4)
            if begin <= address < end:
                records.append(dict(begin_rva=hex(begin), end_rva=hex(end), unwind_rva=hex(unwind)))
    _, debug_size, table = pe.directory(6)
    pdb = []
    if table is not None:
        require(debug_size % 28 == 0, 'partial_debug_record')
        for offset in range(table, table + debug_size, 28):
            payload_size, raw = pe.u32(offset + 16), pe.u32(offset + 24)
            pe.span(raw, payload_size)
            if pe.u32(offset + 12) != 2:
                continue
            require(payload_size >= 4, 'codeview_record_too_small')
            if pe.span(raw, 4) == b'RSDS':
                require(payload_size >= 25, 'rsds_record_too_small')
                name = pe.ascii(raw + 24, payload_size - 24)
                require(bool(name), 'empty_pdb_name')
                pdb.append(dict(guid=str(uuid.UUID(bytes_le=pe.span(raw + 4, 16))),
                                age=pe.u32(raw + 20), name=name))
    return dict(path=str(path), sha256=hashlib.sha256(data).hexdigest(), machine='0x8664',
                target_rva=hex(address), exact_exports=[name for target, name in exports if target == address],
                nearest_export_before=[hex(before[-1][0]), before[-1][1]] if before else None,
                nearest_export_after=[hex(after[0][0]), after[0][1]] if after else None,
                runtime_function_records=records, pdb_identity=pdb,
                limitation='Nearest exports are location markers, not routine attribution. Runtime records may cover function portions; they do not name private routines or identify thread creators.')


def publish(path, result):
    payload = json.dumps(result, indent=2) + '\n'
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n',
                                         prefix=path.name + '.', suffix='.tmp',
                                         dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--rva', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        source, output = args.image.resolve(), args.output.resolve()
        require(source != output, 'output_must_not_replace_input')
        result = inspect(source, args.rva)  # Validate fully before touching output.
        publish(output, result)
    except (OSError, PEFormatError) as error:
        detail = dict(error_type=type(error).__name__)
        if isinstance(error, PEFormatError):
            detail['code'] = str(error)
        print(json.dumps(detail), file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

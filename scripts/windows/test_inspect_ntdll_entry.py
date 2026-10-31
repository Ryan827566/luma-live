"""Synthetic PE tests; no native execution, process access or symbol service."""
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import inspect_ntdll_entry as target


def image():
    data = bytearray(0xe00)
    def put(offset, fmt, *values): struct.pack_into('<' + fmt, data, offset, *values)
    data[:2] = b'MZ'
    put(0x3c, 'I', 0x80)
    data[0x80:0x84] = b'PE\0\0'
    put(0x84, 'HH', 0x8664, 3)
    put(0x94, 'H', 240)
    put(0x98, 'H', 0x20b)
    put(0x98 + 56, 'II', 0x4000, 0x200)
    put(0x98 + 108, 'I', 16)
    for index, rva, size in ((0, 0x2000, 0x100), (3, 0x3000, 12), (6, 0x2100, 28)):
        put(0x108 + index * 8, 'II', rva, size)
    for i, (rva, raw, size) in enumerate(((0x1000, 0x200, 0x400),
                                         (0x2000, 0x600, 0x600), (0x3000, 0xc00, 0x200))):
        put(0x188 + 40 * i + 8, 'IIII', size, rva, size, raw)
    put(0x600 + 20, 'IIIII', 2, 2, 0x2040, 0x2050, 0x2060)
    put(0x640, 'II', 0x1080, 0x1200)
    put(0x650, 'II', 0x2080, 0x2090)
    put(0x660, 'HH', 0, 1)
    data[0x680:0x687] = b'Before\0'
    data[0x690:0x696] = b'After\0'
    put(0xc00, 'III', 0x1100, 0x1140, 0x2200)
    put(0x700 + 12, 'IIII', 2, 33, 0x2140, 0x740)
    data[0x740:0x744] = b'RSDS'
    data[0x744:0x754] = bytes(range(16))
    put(0x754, 'I', 1)
    data[0x758:0x761] = b'test.pdb\0'
    return data


class InspectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent,
                                                prefix='pe-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'fixture.dll'
        self.output = self.root / 'report.json'

    def inspect(self, data=None, rva=0x1100):
        self.source.write_bytes(image() if data is None else data)
        return target.inspect(self.source, rva)

    def test_private_routine_is_not_named_after_nearest_export(self):
        report = self.inspect()
        self.assertEqual(report['exact_exports'], [])
        self.assertEqual(report['nearest_export_before'], ['0x1080', 'Before'])
        self.assertEqual(report['nearest_export_after'], ['0x1200', 'After'])
        self.assertEqual(report['runtime_function_records'][0]['begin_rva'], '0x1100')
        self.assertEqual(report['pdb_identity'][0]['name'], 'test.pdb')
        self.assertNotIn('routine_name', report)

    def test_exact_export_and_exclusive_runtime_end(self):
        self.assertEqual(self.inspect(rva=0x1080)['exact_exports'], ['Before'])
        self.assertEqual(self.inspect(rva=0x1140)['runtime_function_records'], [])

    def test_absent_optional_directories_are_explicitly_empty(self):
        data = image()
        data[0x108:0x188] = bytes(128)
        report = self.inspect(data)
        self.assertEqual(report['exact_exports'], [])
        self.assertEqual(report['runtime_function_records'], [])
        self.assertEqual(report['pdb_identity'], [])

    def test_forwarded_export_does_not_become_a_local_code_symbol(self):
        data = image()
        struct.pack_into('<I', data, 0x640, 0x20a0)
        data[0x6a0:0x6ab] = b'other.Func\0'
        self.assertIsNone(self.inspect(data)['nearest_export_before'])

    def test_exported_zero_filled_data_is_a_valid_image_address(self):
        data = image()
        struct.pack_into('<I', data, 0x188 + 8, 0x800)
        struct.pack_into('<I', data, 0x640, 0x1600)
        report = self.inspect(data)
        self.assertEqual(report['exact_exports'], [])
        self.assertIsNone(report['nearest_export_before'])

    def test_truncated_headers_and_file_regions_rejected(self):
        for length in (0, 1, 0x3f, 0x98, 0x100, 0x1ff, 0xdff):
            with self.subTest(length=length), self.assertRaises(target.PEFormatError):
                self.inspect(image()[:length])

    def test_bad_numeric_ranges_rejected(self):
        patches = ((0x3c, 'I', 0xffffffff), (0x84, 'H', 0x14c), (0x98, 'H', 0x10b),
                   (0x98 + 108, 'I', 100), (0x188 + 40 + 12, 'I', 0x1000),
                   (0x188 + 20, 'I', 0xfffffff0), (0x600 + 28, 'I', 0x5000),
                   (0x600 + 24, 'I', 0xffffffff), (0x660, 'H', 2),
                   (0xc04, 'I', 0x1100), (0xc08, 'I', 0x5000),
                   (0x108 + 3 * 8 + 4, 'I', 13), (0x108 + 6 * 8 + 4, 'I', 29),
                   (0x700 + 24, 'I', 0xfffffff0), (0x700 + 16, 'I', 20))
        for offset, fmt, value in patches:
            with self.subTest(offset=offset, value=value):
                data = image()
                struct.pack_into('<' + fmt, data, offset, value)
                with self.assertRaises(target.PEFormatError):
                    self.inspect(data)

    def test_unterminated_export_and_rsds_strings_rejected(self):
        data = image()
        struct.pack_into('<I', data, 0x650, 0x25ff)
        data[0xbff] = 65
        with self.assertRaisesRegex(target.PEFormatError, 'unterminated_string'):
            self.inspect(data)
        data = image()
        data[0x760] = 65
        with self.assertRaisesRegex(target.PEFormatError, 'unterminated_string'):
            self.inspect(data)

    def test_target_rva_outside_image_or_unbacked_tail_rejected(self):
        for rva in (-1, 0x4000, 0xffffffff):
            with self.subTest(rva=rva), self.assertRaises(target.PEFormatError):
                self.inspect(rva=rva)
        data = image()
        struct.pack_into('<I', data, 0x188 + 8, 0x800)
        with self.assertRaisesRegex(target.PEFormatError, 'rva_not_file_backed'):
            self.inspect(data, rva=0x1600)

    def test_bad_input_preserves_existing_report(self):
        self.source.write_bytes(b'bad image')
        self.output.write_bytes(b'old report exact bytes\n')
        with redirect_stderr(io.StringIO()):
            code = target.main(['--image', str(self.source), '--rva', '0x1100', '--output', str(self.output)])
        self.assertEqual(code, 1)
        self.assertEqual(self.output.read_bytes(), b'old report exact bytes\n')
        self.assertEqual(list(self.root.glob('*.tmp')), [])

    def test_atomic_replace_failure_preserves_report_and_cleans_temp(self):
        self.output.write_bytes(b'old complete snapshot')
        with patch.object(target.os, 'replace', side_effect=PermissionError('replace failed')):
            with self.assertRaises(PermissionError):
                target.publish(self.output, {'new': True})
        self.assertEqual(self.output.read_bytes(), b'old complete snapshot')
        self.assertEqual(list(self.root.glob('*.tmp')), [])

    def test_successful_cli_and_input_overwrite_rejected(self):
        original = bytes(image())
        self.source.write_bytes(original)
        with redirect_stdout(io.StringIO()):
            self.assertEqual(target.main(['--image', str(self.source), '--rva', '0x1100',
                                          '--output', str(self.output)]), 0)
        self.assertEqual(json.loads(self.output.read_text())['target_rva'], '0x1100')
        with redirect_stderr(io.StringIO()):
            self.assertEqual(target.main(['--image', str(self.source), '--rva', '0x1100',
                                          '--output', str(self.source)]), 1)
        self.assertEqual(self.source.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()

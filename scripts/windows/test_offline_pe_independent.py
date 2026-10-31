"""Independent, synthetic PE fixtures: no native image execution or DLL loading."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


def pe_fixture():
    b = bytearray(0xa00)
    def w16(offset, value): struct.pack_into('<H', b, offset, value)
    def w32(offset, value): struct.pack_into('<I', b, offset, value)
    b[:2] = b'MZ'; w32(0x3c, 0x80); b[0x80:0x84] = b'PE\0\0'
    w16(0x84, 0x8664); w16(0x86, 1); w16(0x94, 0xf0)
    w16(0x98, 0x20b); w32(0x98 + 32, 0x1000); w32(0x98 + 36, 0x200)
    w32(0x98 + 56, 0x2000); w32(0x98 + 60, 0x400); w32(0x98 + 108, 16)
    def directory(index, rva, size):
        w32(0x98 + 112 + index * 8, rva); w32(0x98 + 116 + index * 8, size)
    directory(0, 0x1100, 0x60); directory(3, 0x1200, 12); directory(6, 0x1300, 28)
    b[0x188:0x190] = b'.text\0\0\0'
    for at, value in [(8, 0x600), (12, 0x1000), (16, 0x600), (20, 0x400), (36, 0x60000020)]:
        w32(0x188 + at, value)
    # One named export pointing to a small non-forwarded routine.
    for at, value in [(16, 1), (20, 1), (24, 1), (28, 0x1180), (32, 0x1190), (36, 0x11a0)]:
        w32(0x500 + at, value)
    w32(0x580, 0x1000); w32(0x590, 0x11b0); w16(0x5a0, 0)
    b[0x5b0:0x5b7] = b'Target\0'
    struct.pack_into('<III', b, 0x600, 0x1000, 0x1010, 0x1220)
    b[0x620:0x624] = b'\x01\0\0\0'
    rsds = b'RSDS' + bytes(range(16)) + struct.pack('<I', 1) + b'unit.pdb\0'
    w32(0x70c, 2); w32(0x710, len(rsds)); w32(0x714, 0x1380); w32(0x718, 0x780)
    b[0x780:0x780+len(rsds)] = rsds
    return b


class OfflinePeIndependent(unittest.TestCase):
    def inspect(self, data, rva=0x1000):
        with tempfile.TemporaryDirectory(prefix='qa-pe-', dir=ROOT) as temp:
            path = Path(temp) / 'synthetic.dll'
            path.write_bytes(data)
            return MODULE.inspect(path, rva)

    def reject(self, data, rva=0x1000):
        with self.assertRaises((ValueError, MODULE.PeFormatError) if hasattr(MODULE, 'PeFormatError') else ValueError):
            self.inspect(data, rva)

    def test_valid_named_routine_and_pdb(self):
        value = self.inspect(pe_fixture())
        self.assertEqual(value['exact_exports'], ['Target'])
        self.assertEqual(value['runtime_function_records'][0]['begin_rva'], '0x1000')
        self.assertEqual(value['pdb_identity'][0]['name'], 'unit.pdb')

    def test_truncated_headers(self):
        for size in [0, 1, 59, 63, 100, 160, 350, 400]:
            with self.subTest(size=size): self.reject(pe_fixture()[:size])

    def test_bad_pe_pointer(self):
        b=pe_fixture(); struct.pack_into('<I',b,0x3c,0xfffffff0); self.reject(b)

    def test_wrong_magic(self):
        b=pe_fixture(); struct.pack_into('<H',b,0x98,0x10b); self.reject(b)

    def test_section_outside_file(self):
        b=pe_fixture(); struct.pack_into('<I',b,0x188+20,0x900); self.reject(b)

    def test_section_count_outside_headers(self):
        b=pe_fixture(); struct.pack_into('<H',b,0x86,0xffff); self.reject(b)

    def test_target_not_file_backed(self):
        b=pe_fixture(); struct.pack_into('<I',b,0x188+8,0x800); self.reject(b,0x1700)

    def test_negative_and_out_of_image_target(self):
        for rva in [-1, 0x100000000, 0x3000]:
            with self.subTest(rva=rva): self.reject(pe_fixture(),rva)

    def test_export_array_count_overflow(self):
        b=pe_fixture(); struct.pack_into('<I',b,0x500+24,0xffffffff); self.reject(b)

    def test_invalid_export_ordinal(self):
        b=pe_fixture(); struct.pack_into('<H',b,0x5a0,1); self.reject(b)

    def test_unterminated_export_name(self):
        b=pe_fixture(); struct.pack_into('<I',b,0x590,0x15fe); b[-2:]=b'xx'; self.reject(b)

    def test_runtime_record_remainder(self):
        b=pe_fixture(); struct.pack_into('<I',b,0x98+116+3*8,13); self.reject(b)

    def test_debug_blob_outside_file(self):
        b=pe_fixture(); struct.pack_into('<I',b,0x718,0x9ff); self.reject(b)

    def test_rsds_unterminated_inside_declared_blob(self):
        b=pe_fixture(); b[0x780+32]=ord('x'); self.reject(b)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--result',type=Path,required=True); args=parser.parse_args()
    ROOT=Path(__file__).resolve().parent
    spec=importlib.util.spec_from_file_location('qa_offline_pe',args.source.resolve())
    MODULE=importlib.util.module_from_spec(spec); spec.loader.exec_module(MODULE)
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(OfflinePeIndependent))
    args.result.write_text(json.dumps({'source':str(args.source.resolve()),'source_sha256':hashlib.sha256(args.source.read_bytes()).hexdigest(),'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'skipped':len(result.skipped),'test_type':'synthetic offline PE; no DLL loaded, no processes attached','passed':result.wasSuccessful()},indent=2)+'\n',encoding='utf-8')
    raise SystemExit(0 if result.wasSuccessful() else 1)

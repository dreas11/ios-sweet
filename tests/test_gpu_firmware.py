import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import adt
import gpu_firmware as fw
import mkpayload


def synthetic_images():
    gmu = struct.pack('<4I', 0x1000, 9, 0, 0)
    sqe = struct.pack('<2I', 0, 1)
    zap = bytearray(88)
    zap[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HHIIIIIHHHHHH', zap, 16, 2, 164, 1, 0x5000,
                     52, 0, 0, 52, 32, 1, 0, 0, 0)
    struct.pack_into('<8I', zap, 52, 1, 84, 0x5000, 0x5000, 4, 4, 7, 4)
    zap[84:] = b'test'
    return gmu, sqe, bytes(zap)


def node(name):
    result = adt.Node()
    result.props = [('name', 0, name.encode() + b'\0')]
    return result


class GPUFirmwareTests(unittest.TestCase):
    def test_integrity_and_directory_bounds(self):
        blob = fw.build(*synthetic_images())
        self.assertEqual([e['kind'] for e in fw.validate(blob)], [1, 2, 3])
        for mutated in (blob[:-1], blob + bytes(64), bytes(32)):
            with self.assertRaises(ValueError):
                fw.validate(mutated)
        bad = bytearray(blob)
        bad[fw.validate(blob)[0]['offset'] + 8] ^= 1
        with self.assertRaisesRegex(ValueError, 'integrity'):
            fw.validate(bad)
        bad = bytearray(blob)
        struct.pack_into('<Q', bad, fw.HEADER.size + fw.ENTRY.size + 8, 256)
        with self.assertRaisesRegex(ValueError, 'overlapping'):
            fw.validate(bad)
        gmu, sqe, zap = synthetic_images()
        with self.assertRaises(ValueError):
            fw.build(gmu, sqe, zap[:-2])

    def test_payload_reserves_firmware_after_ramdisk(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            # Small synthetic ARM64 Mach-O, with one segment and an entry point.
            k = bytearray(0x4000)
            va = 0xfffffe0007004000
            struct.pack_into('<8I', k, 0, 0xfeedfacf, 0x100000c, 0, 2, 2, 360, 0, 0)
            struct.pack_into('<II16s4Q4I', k, 32, 0x19, 72, b'__TEXT',
                             va, len(k), 0, len(k), 5, 5, 0, 0)
            struct.pack_into('<4I', k, 104, 5, 288, 6, 68)
            struct.pack_into('<Q', k, 104 + 16 + 32 * 8, va + 0x1000)
            kernel = root / 'kernel'; kernel.write_bytes(k)
            tree = node('root'); chosen = node('chosen'); mmap = node('memory-map')
            tree.children.append(chosen); chosen.children.append(mmap)
            chosen.props += [('dram-base', 0, struct.pack('<Q', 0xb0000000)),
                             ('dram-size', 0, struct.pack('<Q', 0x10000000))]
            dt = root / 'adt'; dt.write_bytes(adt.serialize(tree))
            bundle = root / 'fw'; bundle.write_bytes(fw.build(*synthetic_images()))
            rd = root / 'rd'; rd.write_bytes(b'ramdisk-test')
            blob, info = mkpayload.build(kernel, dt, 'cpus=6', rd, bundle)
            self.assertEqual(struct.unpack_from('<I', blob, 12)[0], 3)
            adt_off, adt_size, kernel_end = struct.unpack_from('<3Q', blob, 32)
            result, _ = adt.parse(blob[adt_off:adt_off + adt_size])
            memory = result.children[0].children[0]
            fw_pa, fw_len = struct.unpack('<2Q', memory.prop('GPUFirmware'))
            rd_pa, rd_len = struct.unpack('<2Q', memory.prop('RAMDisk'))
            self.assertGreaterEqual(fw_pa, rd_pa + rd_len)
            self.assertEqual(fw_len, len(bundle.read_bytes()))
            self.assertEqual(info['gpu_firmware']['physical_address'], hex(fw_pa))
            segment = struct.unpack_from('<4Q', blob, mkpayload.SEG_TABLE + 2 * 32)
            self.assertEqual(segment[0] + segment[1], kernel_end)
            self.assertEqual(blob[segment[2]:segment[2] + segment[3]], bundle.read_bytes())
            self.assertEqual(segment[1] % mkpayload.PAGE, 0)
            # Duplicate mappings and insufficient RAM must fail before output.
            dt.write_bytes(adt.serialize(result))
            with self.assertRaisesRegex(ValueError, 'already has GPUFirmware'):
                mkpayload.build(kernel, dt, 'cpus=6', gpu_firmware_path=bundle)
            chosen.props[-1] = ('dram-size', 0, struct.pack('<Q', 0x1000000))
            dt.write_bytes(adt.serialize(tree))
            with self.assertRaisesRegex(ValueError, 'exceeds'):
                mkpayload.build(kernel, dt, 'cpus=6', gpu_firmware_path=bundle)


if __name__ == '__main__':
    unittest.main()

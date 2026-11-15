import importlib.util
from pathlib import Path
import struct
import unittest

p = Path(__file__).resolve().parents[1] / 'tools/adreno-firmware-check.py'
s = importlib.util.spec_from_file_location('adreno_firmware', p)
fw = importlib.util.module_from_spec(s)
s.loader.exec_module(fw)


class AdrenoFirmwareTests(unittest.TestCase):
    def setUp(self):
        self.gmu = struct.pack('<II', 0x43f80, 9) + bytes(24)
        self.sqe = struct.pack('<III', 0, 0x1234, 0x5678)

    def test_legacy_layout(self):
        result = fw.inspect(self.gmu, self.sqe)
        self.assertEqual(result['gmu']['format'], 'legacy-flat')
        self.assertEqual(result['sqe']['payload_words'], 2)
        self.assertFalse(result['hardware_execution_verified'])

    def test_gmu_limits_and_vectors(self):
        bad = [b'', self.gmu + b'x', self.gmu + bytes(0x8000),
               struct.pack('<II', 0x43f80, 8) + bytes(24),
               struct.pack('<II', 0x43f81, 9) + bytes(24),
               struct.pack('<II', 0x43f80, 0x10001) + bytes(24)]
        for image in bad:
            with self.assertRaises(ValueError):
                fw.inspect(image, self.sqe)

    def test_sqe_bounds(self):
        for image in (b'', bytes(4), bytes(9)):
            with self.assertRaises(ValueError):
                fw.inspect(self.gmu, image)


if __name__ == '__main__':
    unittest.main()

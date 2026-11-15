"""Exercise the exact kernel parser with synthetic files, never vendor firmware."""
import ctypes
import hashlib
import sys
import pathlib
import random
import struct
import subprocess
import tempfile
import unittest

from test_gpu_arc import added_file
from test_gpu_firmware import synthetic_images
import gpu_firmware as fw

ROOT = pathlib.Path(__file__).resolve().parents[1]


class GPUFirmwareReaderTests(unittest.TestCase):
    def test_kernel_matches_packer_and_rejects_corruption(self):
        patch = (ROOT / 'xnu/patches/0085-sweet-gpu-firmware-reader.patch').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / 'sweet_gpu_firmware.h').write_text(added_file(patch, 'sweet_gpu_firmware.h'))
            (root / 'reader.c').write_text('''
#include <string.h>
#include "sweet_gpu_firmware.h"
int validate(const uint8_t *data, size_t n, sweet_gpu_fw_hash hash) {
    struct sweet_gpu_fw_image images[3] = {{0}}, before[3];
    memcpy(before, images, sizeof(images));
    bool ok = sweet_gpu_fw_parse(data, n, hash, images);
    if (!ok && memcmp(images, before, sizeof(images))) return -1;
    return ok;
}
''')
            lib = root / 'reader.so'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            *(['-dynamiclib'] if sys.platform == 'darwin' else ['-shared', '-fPIC']),
                            str(root / 'reader.c'), '-o', str(lib)],
                           check=True, capture_output=True)
            reader = ctypes.CDLL(str(lib)).validate
            hashtype = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p)
            hashcheck = hashtype(lambda data, size, expected: hashlib.sha256(ctypes.string_at(data, size)).digest() == ctypes.string_at(expected, 32))
            reader.argtypes = (ctypes.c_char_p, ctypes.c_size_t, hashtype)
            reader.restype = ctypes.c_int

            def compare(data):
                try:
                    fw.validate(data)
                    expected = 1
                except ValueError:
                    expected = 0
                self.assertEqual(reader(data, len(data), hashcheck), expected)

            blob = fw.build(*synthetic_images())
            self.assertEqual(reader(blob, len(blob), hashcheck), 1)
            for end in range(len(blob)):
                compare(blob[:end])
            for offset in range(len(blob)):
                damaged = bytearray(blob)
                damaged[offset] ^= 0x80
                compare(bytes(damaged))
            for offset in (40, 48, 96, 104, 152, 160):
                for value in (0, 2**32, 2**64 - 1):
                    damaged = bytearray(blob)
                    struct.pack_into('<Q', damaged, offset, value)
                    compare(bytes(damaged))
            # Recompute hashes so malformed inner images reach format checks.
            for kind, relative in ((0, 0), (0, 4), (2, 18), (2, 28), (2, 68), (2, 72)):
                damaged = bytearray(blob)
                entry = 32 + kind * 56
                offset, size = struct.unpack_from('<QQ', damaged, entry + 8)
                damaged[offset + relative] ^= 0x80
                damaged[entry + 24:entry + 56] = hashlib.sha256(damaged[offset:offset+size]).digest()
                compare(bytes(damaged))
            rng = random.Random(618)
            for _ in range(200):
                compare(rng.randbytes(rng.randrange(0, 1024)))


if __name__ == '__main__':
    unittest.main()

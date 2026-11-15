#!/usr/bin/env python3
"""Inspect the sweet Adreno 618 legacy GMU and SQE images, without loading them.

The A615/A616/A618 path in Xiaomi adreno_a6xx_gmu.c uses a flat image,
not the newer GMU block-header format. adreno_a6xx.c skips the first SQE
word before placing microcode in GPU-visible memory. Structural checks
and hashes do NOT authenticate firmware or prove it can execute on iOS.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(gmu, sqe):
    if len(gmu) < 8 or len(gmu) > 0x8000 or len(gmu) % 4:
        raise ValueError('legacy GMU image must be word-aligned and at most 32 KiB')
    stack, reset = struct.unpack_from('<II', gmu)
    # Cortex-M vector sanity checks, not a firmware signature or compatibility test.
    if not stack or stack % 8 or not reset & 1 or (reset & ~1) >= len(gmu):
        raise ValueError('invalid legacy GMU initial stack/reset vector')
    if len(sqe) < 8 or len(sqe) % 4:
        raise ValueError('SQE needs a header word and nonempty word-aligned microcode')
    return {
        'target': 'sweet / Adreno 618',
        'gmu': {'format': 'legacy-flat', 'bytes': len(gmu),
                'initial_stack': hex(stack), 'reset_vector': hex(reset),
                'sha256': hashlib.sha256(gmu).hexdigest()},
        'sqe': {'format': 'header-word-plus-microcode', 'bytes': len(sqe),
                'header_word': hex(struct.unpack_from('<I', sqe)[0]),
                'payload_offset': 4, 'payload_words': (len(sqe) - 4) // 4,
                'sha256': hashlib.sha256(sqe).hexdigest(),
                'payload_sha256': hashlib.sha256(sqe[4:]).hexdigest()},
        'signature_verified': False,
        'hardware_execution_verified': False,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gmu', required=True, type=Path)
    p.add_argument('--sqe', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    result = inspect(a.gmu.read_bytes(), a.sqe.read_bytes())
    with a.output.open('x') as f:
        json.dump(result, f, indent=2)
        f.write('\n')
    print('Legacy GMU structure and SQE payload bounds OK; no firmware loaded.')


if __name__ == '__main__':
    main()

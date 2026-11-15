#!/usr/bin/env python3
"""Pack private Adreno firmware for a reserved SWEET boot-payload segment.

This transports files; it neither authenticates nor executes firmware. It must
not be published with the source code. No ELF address is a host RAM destination.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import struct

MAGIC = b'SWGPUFW1'
HEADER = struct.Struct('<8sII16s')
ENTRY = struct.Struct('<IIQQ32s')
LIMIT = 1024 * 1024
KINDS = (1, 2, 3)  # legacy GMU, complete SQE (including first word), complete ZAP ELF


def align(n):
    return (n + 63) & ~63


def check_images(gmu, sqe, zap):
    spec = importlib.util.spec_from_file_location('sweet_adreno_check',
        Path(__file__).with_name('adreno-firmware-check.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.inspect(gmu, sqe)
    if len(sqe) > 0x40000 or len(zap) > 0x40000:
        raise ValueError('firmware exceeds transport policy limit')
    if len(zap) < 52 or zap[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('ZAP must be the complete little-endian ELF32 file')
    _, machine, version, _, phoff, _, _, ehsize, phsize, count, _, _, _ = struct.unpack_from('<HHIIIIIHHHHHH', zap, 16)
    if machine != 164 or version != 1 or ehsize != 52 or phsize != 32 or not 1 <= count <= 16 or phoff < 52 or phoff + count * phsize > len(zap):
        raise ValueError('unsupported ZAP ELF header')
    loaded = 0
    for i in range(count):
        kind, off, va, pa, size, mem, flags, _ = struct.unpack_from('<8I', zap, phoff + i * phsize)
        if kind != 1 or (flags & 0x07000000) == 0x02000000:
            continue
        if size > mem or off > len(zap) or size > len(zap) - off or pa + mem > 2**32 or va + mem > 2**32:
            raise ValueError('invalid ZAP loadable segment')
        loaded += bool(size)
    if not loaded:
        raise ValueError('ZAP has no file-backed loadable segment')


def build(gmu, sqe, zap):
    check_images(gmu, sqe, zap)
    out = bytearray(align(HEADER.size + 3 * ENTRY.size))
    HEADER.pack_into(out, 0, MAGIC, 1, 3, bytes(16))
    for i, (kind, image) in enumerate(zip(KINDS, (gmu, sqe, zap))):
        offset = len(out)
        ENTRY.pack_into(out, HEADER.size + i * ENTRY.size,
                        kind, 0, offset, len(image), hashlib.sha256(image).digest())
        out += image
        out += bytes(align(len(out)) - len(out))
    result = bytes(out)
    validate(result)
    return result


def validate(data):
    if not HEADER.size <= len(data) <= LIMIT:
        raise ValueError('invalid GPU firmware container size')
    magic, version, count, reserved = HEADER.unpack_from(data)
    if (magic, version, count, reserved) != (MAGIC, 1, 3, bytes(16)):
        raise ValueError('unsupported GPU firmware container header')
    cursor = align(HEADER.size + count * ENTRY.size)
    if cursor > len(data) or any(data[HEADER.size + count * ENTRY.size:cursor]):
        raise ValueError('truncated directory or nonzero directory padding')
    images, entries = [], []
    for i, expected in enumerate(KINDS):
        kind, flags, offset, size, digest = ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)
        if kind != expected or flags or offset != cursor or not size or offset > len(data) or size > len(data) - offset:
            raise ValueError('invalid, overlapping or truncated firmware entry')
        image = data[offset:offset + size]
        cursor = align(offset + size)
        if cursor > len(data) or any(data[offset + size:cursor]):
            raise ValueError('invalid firmware padding')
        if hashlib.sha256(image).digest() != digest:
            raise ValueError('GPU firmware integrity mismatch')
        images.append(image)
        entries.append({'kind': kind, 'offset': offset, 'size': size, 'sha256': digest.hex()})
    if cursor != len(data):
        raise ValueError('unexpected trailing data')
    check_images(*images)
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('gmu', 'sqe', 'zap', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    blob = build(args.gmu.read_bytes(), args.sqe.read_bytes(), args.zap.read_bytes())
    with args.output.open('xb') as f:
        f.write(blob)
    print(f'{len(blob)} bytes; SHA256 {hashlib.sha256(blob).hexdigest()}; transport only, no execution')


if __name__ == '__main__':
    main()

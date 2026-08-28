#!/usr/bin/env python3
"""Print exact Mario boot bounds from a US SM64 ROM asset segment."""

from __future__ import annotations

import struct
import sys
from pathlib import Path


def mio0_decode(data: bytes) -> bytes:
    if data[:4] != b"MIO0":
        return data
    size, compressed_offset, raw_offset = struct.unpack_from(">III", data, 4)
    output = bytearray()
    mask_offset = 16
    mask = 0
    bits_left = 0
    while len(output) < size:
        if bits_left == 0:
            mask = struct.unpack_from(">I", data, mask_offset)[0]
            mask_offset += 4
            bits_left = 32
        if mask & 0x80000000:
            output.append(data[raw_offset])
            raw_offset += 1
        else:
            token = struct.unpack_from(">H", data, compressed_offset)[0]
            compressed_offset += 2
            length = (token >> 12) + 3
            distance = (token & 0x0FFF) + 1
            for _ in range(length):
                output.append(output[-distance])
                if len(output) == size:
                    break
        mask = (mask << 1) & 0xFFFFFFFF
        bits_left -= 1
    return bytes(output)


def vertices(segment: bytes, offset: int, size: int):
    return [
        struct.unpack_from(">hhh", segment, cursor)
        for cursor in range(offset, offset + size, 16)
    ]


def bounds(points):
    return tuple((min(axis), max(axis)) for axis in zip(*points))


def main() -> None:
    rom = Path(sys.argv[1]).read_bytes()
    segment = mio0_decode(rom[0x114750:0x114750 + 78432])
    left = vertices(segment, 0xE9C8, 256) + vertices(segment, 0xEAC8, 240)
    right = vertices(segment, 0xF290, 240) + vertices(segment, 0xF380, 128)
    print("left", bounds(left))
    print("right", bounds(right))


if __name__ == "__main__":
    main()

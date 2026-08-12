#!/usr/bin/env python3
import struct
import sys
from pathlib import Path

from PIL import Image


def mio0(data: bytes) -> bytes:
    magic, size, compressed, raw = struct.unpack_from(">4sIII", data)
    if magic != b"MIO0":
        raise ValueError("segment is not MIO0")
    output = bytearray()
    control_offset = 16
    bits = 0
    remaining = 0
    while len(output) < size:
        if remaining == 0:
            bits = struct.unpack_from(">I", data, control_offset)[0]
            control_offset += 4
            remaining = 32
        if bits & 0x80000000:
            output.append(data[raw])
            raw += 1
        else:
            value = struct.unpack_from(">H", data, compressed)[0]
            compressed += 2
            count = (value >> 12) + 3
            distance = (value & 0xFFF) + 1
            for _ in range(count):
                output.append(output[-distance])
        bits = (bits << 1) & 0xFFFFFFFF
        remaining -= 1
    return bytes(output)


def main() -> None:
    rom_path, output_path, offset_text = sys.argv[1:4]
    width = int(sys.argv[4]) if len(sys.argv) > 4 else 32
    height = 2048 // width
    rom = Path(rom_path).read_bytes()
    segment = mio0(rom[0x2A65B0 : 0x2A65B0 + 22255])
    offset = int(offset_text, 0)
    pixels = []
    for (value,) in struct.iter_unpack(">H", segment[offset : offset + 4096]):
        pixels.append(((value >> 11 & 31) * 255 // 31,
                       (value >> 6 & 31) * 255 // 31,
                       (value >> 1 & 31) * 255 // 31,
                       255 if value & 1 else 0))
    image = Image.new("RGBA", (width, height))
    image.putdata(pixels)
    image.save(output_path)


if __name__ == "__main__":
    main()

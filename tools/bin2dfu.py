#!/usr/bin/env python3
"""Wrap a raw firmware image into a DfuSe (.dfu) file for STM32 DFU tools.

    tools/bin2dfu.py firmware.bin firmware.dfu [load address, default 0x08000000]
"""
import struct
import sys
import zlib


def build(image, address):
    element = struct.pack("<2I", address, len(image)) + image
    target = struct.pack("<6sBI255s2I", b"Target", 0, 1, b"Flipper OS 2", len(element), 1) + element
    body = struct.pack("<5sBIB", b"DfuSe", 1, 11 + len(target), 1) + target
    # suffix: bcdDevice, idProduct, idVendor (ST DFU bootloader), bcdDFU, "UFD", length
    body += struct.pack("<4H3sB", 0xFFFF, 0xDF11, 0x0483, 0x011A, b"UFD", 16)
    crc = (~zlib.crc32(body)) & 0xFFFFFFFF
    return body + struct.pack("<I", crc)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    address = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x08000000
    with open(src, "rb") as f:
        image = f.read()
    with open(dst, "wb") as f:
        f.write(build(image, address))
    print("%s: %d bytes at 0x%08X" % (dst, len(image), address))


if __name__ == "__main__":
    main()

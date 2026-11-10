import argparse
import struct
import sys
from pathlib import Path

import make_image

PLAY_BASE = 0x03092000
PLAY_CAPACITY = 0xD000


def packed(elf):
    sections = make_image.elf_sections(elf)
    overlay = sections.get(".overlay")
    if overlay is None or overlay.address != PLAY_BASE:
        raise make_image.ImageError(f"the play overlay needs an .overlay section at {PLAY_BASE:#010x}")
    content = overlay.content
    body = content[make_image.OVERLAY_HEADER_SIZE:]
    header = make_image.OVERLAY_MAGIC + struct.pack("<III", len(content), make_image.crc32(body), 0)
    return header + body


def main(argv=None):
    parser = argparse.ArgumentParser(description="Pack the play fill overlay for the card")
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args(argv)
    data = packed(arguments.elf.read_bytes())
    if len(data) > PLAY_CAPACITY:
        print(f"play overlay is {len(data):,} bytes, over {PLAY_CAPACITY:,}")
        return 1
    arguments.output.write_bytes(data)
    print(f"play overlay {len(data):,} bytes -> {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

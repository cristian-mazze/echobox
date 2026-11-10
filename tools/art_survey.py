import argparse
import json
import statistics
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

import cover_export

JPEG_BASELINE = "jpeg-baseline"
JPEG_PROGRESSIVE = "jpeg-progressive"
PNG_PLAIN = "png"
PNG_ADAM7 = "png-adam7"
UNSUPPORTED = "unsupported"
NO_ART = "no-art"
KINDS = (JPEG_BASELINE, JPEG_PROGRESSIVE, PNG_PLAIN, PNG_ADAM7, UNSUPPORTED)

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_HEADER = struct.Struct(">I4sIIBBBBB")
PNG_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
JPEG_SOI = b"\xff\xd8"
JPEG_SEQUENTIAL = (0xC0, 0xC1)
JPEG_PROGRESSIVE_MARKER = 0xC2
JPEG_NOT_FRAMES = (0xC4, 0xC8, 0xCC)
JPEG_STANDALONE = set(range(0xD0, 0xD8)) | {0x01}
JPEG_START_OF_SCAN = 0xDA
DEFAULT_PNG_ROW_LIMIT = 8192
ROW_LIMITS = (4096, 8192, 16384, 32768)


@dataclass
class ArtFacts:
    kind: str
    width: int = 0
    height: int = 0
    sampling: str = ""
    row_bytes: int = 0


@dataclass
class AlbumArt:
    artist: str
    title: str
    source: str
    size: int
    facts: ArtFacts


def png_facts(data):
    if len(data) < len(PNG_SIGNATURE) + PNG_HEADER.size:
        return ArtFacts(UNSUPPORTED)
    fields = PNG_HEADER.unpack_from(data, len(PNG_SIGNATURE))
    _, chunk, width, height, depth, color, _, _, interlace = fields
    channels = PNG_CHANNELS.get(color)
    if chunk != b"IHDR" or channels is None:
        return ArtFacts(UNSUPPORTED)
    row_bytes = (width * channels * depth + 7) // 8
    kind = PNG_ADAM7 if interlace == 1 else PNG_PLAIN
    return ArtFacts(kind, width, height, row_bytes=row_bytes)


def jpeg_frame(data, offset, kind):
    if offset + 8 > len(data):
        return ArtFacts(UNSUPPORTED)
    height, width = struct.unpack_from(">HH", data, offset + 3)
    count = data[offset + 7]
    factors = data[offset + 8:offset + 8 + count * 3]
    sampling = ",".join(f"{byte >> 4}x{byte & 15}" for byte in factors[1::3])
    return ArtFacts(kind, width, height, sampling)


def frame_kind(marker):
    if marker in JPEG_SEQUENTIAL:
        return JPEG_BASELINE
    if marker == JPEG_PROGRESSIVE_MARKER:
        return JPEG_PROGRESSIVE
    if 0xC3 <= marker <= 0xCF and marker not in JPEG_NOT_FRAMES:
        return UNSUPPORTED
    return None


def next_marker(data, offset):
    while offset < len(data) and data[offset] != 0xFF:
        offset += 1
    while offset < len(data) and data[offset] == 0xFF:
        offset += 1
    return (data[offset], offset + 1) if offset < len(data) else (None, offset)


def jpeg_facts(data):
    offset = len(JPEG_SOI)
    while True:
        marker, offset = next_marker(data, offset)
        if marker is None or marker == JPEG_START_OF_SCAN or offset + 2 > len(data):
            return ArtFacts(UNSUPPORTED)
        if marker in JPEG_STANDALONE:
            continue
        kind = frame_kind(marker)
        if kind is not None:
            return jpeg_frame(data, offset, kind) if kind != UNSUPPORTED else ArtFacts(kind)
        offset += struct.unpack_from(">H", data, offset)[0]


def art_facts(data):
    if data.startswith(PNG_SIGNATURE):
        return png_facts(data)
    if data.startswith(JPEG_SOI):
        return jpeg_facts(data)
    return ArtFacts(UNSUPPORTED)


def eligible(facts, row_limit):
    if facts.kind == JPEG_BASELINE:
        return True
    return facts.kind == PNG_PLAIN and facts.row_bytes <= row_limit


def survey_album(root, source):
    art = cover_export.locate_art(root, source.paths)
    if art is None:
        return AlbumArt(source.artist, source.title, "", 0, ArtFacts(NO_ART))
    where = "embedded" if art.picture_length is not None else "file"
    return AlbumArt(source.artist, source.title, where, len(art.data), art_facts(art.data))


def survey(root, sources):
    seen = set()
    albums = []
    for source in sources:
        if source.key not in seen:
            seen.add(source.key)
            albums.append(survey_album(root, source))
    return albums


def with_art(albums):
    return [album for album in albums if album.facts.kind != NO_ART]


def share(count, total):
    return 0.0 if total == 0 else round(100.0 * count / total, 1)


def eligible_count(albums, row_limit):
    return sum(1 for album in albums if eligible(album.facts, row_limit))


def size_summary(albums):
    sizes = [album.size for album in albums]
    if not sizes:
        return {"median": 0, "max": 0}
    return {"median": int(statistics.median(sizes)), "max": max(sizes)}


def summary(albums, row_limit):
    art = with_art(albums)
    kinds = {kind: sum(1 for album in art if album.facts.kind == kind) for kind in KINDS}
    samplings = {}
    for album in art:
        if album.facts.kind == JPEG_BASELINE:
            samplings[album.facts.sampling] = samplings.get(album.facts.sampling, 0) + 1
    return {
        "albums": len(albums),
        "with_art": len(art),
        "kinds": kinds,
        "jpeg_sampling": samplings,
        "bytes": size_summary(art),
        "png_row_limit": row_limit,
        "eligible": eligible_count(art, row_limit),
        "eligible_percent": share(eligible_count(art, row_limit), len(art)),
        "eligible_by_row_limit": {str(limit): eligible_count(art, limit) for limit in ROW_LIMITS},
    }


def table_lines(result):
    total = result["with_art"]
    lines = [f"{result['albums']} albums, {total} with art"]
    for kind, count in result["kinds"].items():
        lines.append(f"  {kind:<17} {count:>6}  {share(count, total):5.1f} %")
    for sampling, count in sorted(result["jpeg_sampling"].items(), key=lambda item: -item[1]):
        lines.append(f"  baseline {sampling or '?':<9}{count:>6}")
    lines.append(f"  art bytes: median {result['bytes']['median']:,}, max {result['bytes']['max']:,}")
    for limit, count in result["eligible_by_row_limit"].items():
        lines.append(f"  eligible with PNG rows <= {int(limit):,} B: {count} ({share(count, total)} %)")
    lines.append(f"eligible for level 2: {result['eligible']} of {total} "
                 f"({result['eligible_percent']} %)")
    return lines


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Count which covers can load while playing")
    parser.add_argument("--music", type=Path, required=True, help="music root (read only)")
    parser.add_argument("--library", type=Path,
                        help="an existing library.db for this music root, to skip the tag scan")
    parser.add_argument("--json", type=Path, help="write the summary and every album here")
    parser.add_argument("--png-row-limit", type=int, default=DEFAULT_PNG_ROW_LIMIT)
    return parser.parse_args(argv)


def album_sources(arguments, root):
    if arguments.library is not None:
        return cover_export.album_sources_from_library(arguments.library.read_bytes())
    return cover_export.scanned_sources(root)


def main(argv=None):
    arguments = parse_arguments(argv)
    root = arguments.music.resolve()
    albums = survey(root, album_sources(arguments, root))
    result = summary(albums, arguments.png_row_limit)
    print("\n".join(table_lines(result)))
    if arguments.json is not None:
        report = {"summary": result, "albums": [asdict(album) for album in albums]}
        arguments.json.write_text(json.dumps(report, indent=1, ensure_ascii=False), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())

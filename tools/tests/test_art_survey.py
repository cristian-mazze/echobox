import io
import json
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import art_survey
import cover_export
from PIL import Image
from test_cover_export import add_picture
from test_library_index import write_silent_flac


def jpeg_bytes(size=(40, 30), **options):
    buffer = io.BytesIO()
    Image.new("RGB", size, (200, 30, 30)).save(buffer, format="JPEG", **options)
    return buffer.getvalue()


def png_bytes(size=(40, 30), mode="RGB"):
    buffer = io.BytesIO()
    Image.new(mode, size).save(buffer, format="PNG")
    return buffer.getvalue()


def png_chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def interlaced_png(width, height):
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 1)
    return art_survey.PNG_SIGNATURE + png_chunk(b"IHDR", header) + png_chunk(b"IEND", b"")


def separate_scans(data):
    start = data.index(bytes([0xFF, 0xDA]))
    return data[:start + 4] + bytes([1]) + data[start + 5:]


def lossless_jpeg():
    frame = b"\xff\xc3" + struct.pack(">HBHHB", 11, 8, 30, 40, 1) + b"\x01\x11\x00"
    return b"\xff\xd8" + frame + b"\xff\xd9"


class ArtFactsTest(unittest.TestCase):
    def test_a_baseline_jpeg_gives_its_size_and_sampling(self):
        facts = art_survey.art_facts(jpeg_bytes(subsampling=2))
        self.assertEqual(art_survey.JPEG_BASELINE, facts.kind)
        self.assertEqual((40, 30), (facts.width, facts.height))
        self.assertEqual("2x2,1x1,1x1", facts.sampling)

    def test_a_baseline_jpeg_with_one_scan_per_component_is_not_interleaved(self):
        self.assertTrue(art_survey.art_facts(jpeg_bytes()).interleaved)
        facts = art_survey.art_facts(separate_scans(jpeg_bytes()))
        self.assertEqual(art_survey.JPEG_BASELINE, facts.kind)
        self.assertFalse(facts.interleaved)

    def test_a_progressive_jpeg_is_told_apart(self):
        facts = art_survey.art_facts(jpeg_bytes(progressive=True))
        self.assertEqual(art_survey.JPEG_PROGRESSIVE, facts.kind)
        self.assertEqual(40, facts.width)

    def test_other_jpeg_frames_are_unsupported(self):
        self.assertEqual(art_survey.UNSUPPORTED, art_survey.art_facts(lossless_jpeg()).kind)

    def test_a_png_gives_the_bytes_of_one_row(self):
        facts = art_survey.art_facts(png_bytes(mode="RGBA"))
        self.assertEqual(art_survey.PNG_PLAIN, facts.kind)
        self.assertEqual(160, facts.row_bytes)

    def test_an_interlaced_png_is_adam7(self):
        facts = art_survey.art_facts(interlaced_png(100, 50))
        self.assertEqual(art_survey.PNG_ADAM7, facts.kind)
        self.assertEqual(300, facts.row_bytes)

    def test_anything_else_is_unsupported(self):
        self.assertEqual(art_survey.UNSUPPORTED, art_survey.art_facts(b"GIF89a....").kind)
        self.assertEqual(art_survey.UNSUPPORTED, art_survey.art_facts(b"\xff\xd8\xff").kind)
        self.assertEqual(art_survey.UNSUPPORTED, art_survey.art_facts(b"\x89PNG\r\n\x1a\n").kind)


class EligibleTest(unittest.TestCase):
    def test_baseline_jpegs_and_narrow_pngs_can_load_while_playing(self):
        self.assertTrue(art_survey.eligible(art_survey.ArtFacts(art_survey.JPEG_BASELINE), 0))
        separate = art_survey.ArtFacts(art_survey.JPEG_BASELINE, interleaved=False)
        self.assertFalse(art_survey.eligible(separate, 0))
        png = art_survey.ArtFacts(art_survey.PNG_PLAIN, row_bytes=8192)
        self.assertTrue(art_survey.eligible(png, 8192))
        self.assertFalse(art_survey.eligible(png, 8191))

    def test_progressive_and_interlaced_art_waits_for_a_pause(self):
        for kind in (art_survey.JPEG_PROGRESSIVE, art_survey.PNG_ADAM7, art_survey.UNSUPPORTED):
            self.assertFalse(art_survey.eligible(art_survey.ArtFacts(kind, row_bytes=1), 8192))


class SurveyTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.sources = []
        self.add_album("Baseline", jpeg_bytes())
        self.add_album("Progressive", jpeg_bytes(progressive=True))
        self.add_album("Wide", png_bytes(size=(4000, 4), mode="RGBA"))
        self.add_album("Bare", None)

    def tearDown(self):
        self.directory.cleanup()

    def add_album(self, title, art):
        album = self.root / "Artist" / title
        album.mkdir(parents=True)
        write_silent_flac(album / "01.flac", 1)
        if art is not None:
            add_picture(album / "01.flac", art, 3)
        key = cover_export.album_key("Artist", title)
        self.sources.append(cover_export.AlbumSource(key, title, "Artist", [f"Artist/{title}/01.flac"]))

    def test_every_album_is_classified_once(self):
        albums = art_survey.survey(self.root, self.sources + self.sources[:1])
        kinds = [album.facts.kind for album in albums]
        self.assertEqual([art_survey.JPEG_BASELINE, art_survey.JPEG_PROGRESSIVE,
                          art_survey.PNG_PLAIN, art_survey.NO_ART], kinds)
        self.assertEqual("embedded", albums[0].source)

    def test_the_summary_counts_albums_with_art_only(self):
        result = art_survey.summary(art_survey.survey(self.root, self.sources), 8192)
        self.assertEqual(4, result["albums"])
        self.assertEqual(3, result["with_art"])
        self.assertEqual(1, result["eligible"])
        self.assertEqual(33.3, result["eligible_percent"])
        self.assertEqual(2, result["eligible_by_row_limit"]["16384"])

    def test_the_command_prints_a_table_and_writes_json(self):
        original = cover_export.scanned_sources
        cover_export.scanned_sources = lambda root: self.sources
        output = io.StringIO()
        report = self.root / "survey.json"
        try:
            sys.stdout, saved = output, sys.stdout
            status = art_survey.main(["--music", str(self.root), "--json", str(report)])
        finally:
            sys.stdout = saved
            cover_export.scanned_sources = original
        self.assertEqual(0, status)
        self.assertIn("eligible for level 2: 1 of 3 (33.3 %)", output.getvalue())
        written = json.loads(report.read_text(encoding="utf-8"))
        self.assertEqual(4, len(written["albums"]))
        self.assertEqual("png", written["albums"][2]["facts"]["kind"])


if __name__ == "__main__":
    unittest.main()

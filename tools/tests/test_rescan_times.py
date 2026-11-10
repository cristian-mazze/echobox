import struct
import sys
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import rescan_times


def record(values, counters, magic=b"EMRT", version=2):
    header = rescan_times.HEADER.pack(magic, version, 0)
    body = rescan_times.body_for(version if version in rescan_times.COUNTERS else 2)
    data = header + body.pack(*values, *counters)
    return data + bytes(4096 - len(data))


def sample_values():
    micros = [1_000, 2_000, 3_000, 4_000, 20_000, 5_000, 6_000, 7_000, 8_000, 30_000]
    calls = [1, 2, 3, 4, 1, 5, 6, 7, 8, 1]
    return micros + calls


class ParseTest(unittest.TestCase):
    def test_a_record_reads_back_its_passes_and_cover_counters(self):
        parsed = rescan_times.parse(record(sample_values(), [300, 280, 270, 20, 10, 2, 5]))
        library, covers = parsed["passes"]
        self.assertEqual(20_000, library["micros"]["total"])
        self.assertEqual(3, library["calls"]["read"])
        self.assertEqual(8_000, covers["micros"]["write"])
        self.assertEqual({"albums": 300, "covers": 280, "reused": 270, "missing": 20, "decoded": 10,
                          "overflow": 2, "pending": 5}, parsed["covers"])

    def test_a_first_version_record_still_reads(self):
        parsed = rescan_times.parse(record(sample_values(), [3, 2, 1, 1, 1], version=1))
        self.assertEqual({"albums": 3, "covers": 2, "reused": 1, "missing": 1, "decoded": 1},
                         parsed["covers"])

    def test_cpu_is_the_total_minus_the_io(self):
        library = rescan_times.parse(record(sample_values(), [0] * 7))["passes"][0]
        self.assertEqual(10_000, rescan_times.cpu_micros(library))

    def test_the_erst_header_is_rejected(self):
        with self.assertRaises(rescan_times.TimesError):
            rescan_times.parse(record(sample_values(), [0] * 7, magic=b"ERST", version=2))

    def test_the_old_headerless_dump_is_rejected(self):
        with self.assertRaises(rescan_times.TimesError):
            rescan_times.parse(struct.pack("<20I", *sample_values()))

    def test_an_unknown_version_is_rejected(self):
        with self.assertRaises(rescan_times.TimesError):
            rescan_times.parse(record(sample_values(), [0] * 7, version=3))

    def test_the_description_names_every_pass_and_counter(self):
        text = rescan_times.describe(rescan_times.parse(record(sample_values(), [3, 2, 1, 1, 1, 0, 4])))
        self.assertIn("library pass: 0.020 s", text)
        self.assertIn("covers pass: 0.030 s", text)
        self.assertIn("decoded 1", text)
        self.assertIn("pending 4", text)


if __name__ == "__main__":
    unittest.main()

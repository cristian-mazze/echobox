import argparse
import struct
import sys
from pathlib import Path

MAGIC = b"EMRT"
VERSION = 1
HEADER = struct.Struct("<4sHH")
KINDS = ("list", "open", "read", "write", "total")
PASSES = ("library", "covers")
SLOTS = len(KINDS) * len(PASSES)
COUNTERS = ("albums", "covers", "reused", "missing", "decoded")
BODY = struct.Struct("<" + "I" * (SLOTS * 2 + len(COUNTERS)))
RECORD_SIZE = HEADER.size + BODY.size
IO_KINDS = ("list", "open", "read", "write")


class TimesError(ValueError):
    pass


def parse(data):
    if len(data) < RECORD_SIZE:
        raise TimesError(f"file is {len(data)} bytes, an EMRT record needs {RECORD_SIZE}")
    magic, version, _ = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise TimesError(f"magic {magic!r} is not {MAGIC!r}; this file was not written by EchoPod")
    if version != VERSION:
        raise TimesError(f"version {version} is not supported, expected {VERSION}")
    values = BODY.unpack_from(data, HEADER.size)
    return {
        "passes": [pass_times(values, index) for index in range(len(PASSES))],
        "covers": dict(zip(COUNTERS, values[SLOTS * 2:])),
    }


def pass_times(values, index):
    first = index * len(KINDS)
    micros = dict(zip(KINDS, values[first:first + len(KINDS)]))
    calls = dict(zip(KINDS, values[SLOTS + first:SLOTS + first + len(KINDS)]))
    return {"name": PASSES[index], "micros": micros, "calls": calls}


def cpu_micros(times):
    spent_on_io = sum(times["micros"][kind] for kind in IO_KINDS)
    return max(times["micros"]["total"] - spent_on_io, 0)


def seconds(micros):
    return f"{micros / 1_000_000:.3f} s"


def describe_pass(times):
    lines = [f"{times['name']} pass: {seconds(times['micros']['total'])}"]
    for kind in IO_KINDS:
        lines.append(f"  {kind:<5} {seconds(times['micros'][kind]):>12}  {times['calls'][kind]:>8} calls")
    lines.append(f"  cpu   {seconds(cpu_micros(times)):>12}  (total minus I/O)")
    return lines


def describe(record):
    lines = []
    for times in record["passes"]:
        lines += describe_pass(times)
    lines.append("covers: " + ", ".join(f"{name} {value}" for name, value in record["covers"].items()))
    return "\n".join(lines)


def main(arguments=None):
    parser = argparse.ArgumentParser(description="Print the timings EchoPod saved after a rescan.")
    parser.add_argument("path", type=Path, help="rescan_times.bin copied from /.echo on the card")
    options = parser.parse_args(arguments)
    try:
        print(describe(parse(options.path.read_bytes())))
    except TimesError as error:
        print(f"{options.path}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

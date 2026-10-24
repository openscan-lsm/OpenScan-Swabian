#!/usr/bin/env python3
"""Print a Time Tagger tag stream dump (.ttdump) in a human-readable form.

Reads the file format produced by the vendor SDK's `Dump` measurement
(driver/include/measurements/Dump.h): a sequence of 16-byte (128-bit)
records with no file header, one per event. This is NOT the same format
as `FileWriter`'s .ttbin files, which are compressed and framed
differently.

Record layout (little-endian), from the `Tag` struct in TimeTagger.h:
    offset 0   1 byte   type            (0=TimeTag, 1=Error,
                                          2=OverflowBegin, 3=OverflowEnd,
                                          4=MissedEvents)
    offset 1   1 byte   reserved        (unused)
    offset 2   2 bytes  missed_events   (uint16, meaningful only for
                                          MissedEvents records)
    offset 4   4 bytes  channel         (int32; sign encodes edge --
                                          positive = rising, negative =
                                          falling)
    offset 8   8 bytes  time            (int64, picoseconds)

Files ending in .zst (.ttdump.zst) are decompressed while reading.

Requires Python 3.14+ (for compression.zstd).
"""

import sys

if sys.version_info < (3, 14):
    sys.exit("print_ttdump.py requires Python 3.14+")

import argparse
import compression.zstd
import struct

RECORD_SIZE = 16
RECORD_FORMAT = "<BBHiq"  # type, reserved, missed_events, channel, time

TAG_TYPE_NAMES = {
    0: "TimeTag",
    1: "Error",
    2: "OverflowBegin",
    3: "OverflowEnd",
    4: "MissedEvents",
}


def format_record(index, tag_type, reserved, missed_events, channel, time_ps):
    type_name = TAG_TYPE_NAMES.get(tag_type, f"Unknown({tag_type})")
    line = f"[{index:8d}] t={time_ps:>18d} ps  {type_name:<13s}"

    if tag_type == 0:  # TimeTag
        line += f"  channel={channel:+d}"
    elif tag_type == 4:  # MissedEvents
        line += f"  channel={channel:+d}  missed={missed_events}"
    elif tag_type in (1, 2, 3):  # Error, OverflowBegin, OverflowEnd
        pass  # channel/missed_events are not meaningful for these
    else:
        line += f"  channel={channel:+d}  missed={missed_events}"

    if reserved != 0:
        line += f"  (reserved={reserved}, expected 0)"

    return line


def open_ttdump(path):
    # ZstdFile decompresses incrementally, so it does not need the frame
    # header to record the content size (ours never do).
    if path.endswith(".zst"):
        return compression.zstd.open(path, "rb")
    return open(path, "rb")


def print_ttdump(path, max_tags, out):
    index = 0
    with open_ttdump(path) as f:
        while max_tags is None or index < max_tags:
            record = f.read(RECORD_SIZE)
            if len(record) < RECORD_SIZE:
                if record:
                    print(
                        f"warning: ignoring trailing {len(record)} byte(s) "
                        f"after {index} complete {RECORD_SIZE}-byte "
                        "record(s) (truncated or not a tag stream dump "
                        "(.ttdump)?)",
                        file=sys.stderr,
                    )
                break
            tag_type, reserved, missed_events, channel, time_ps = \
                struct.unpack(RECORD_FORMAT, record)
            print(format_record(index, tag_type, reserved, missed_events,
                                channel, time_ps), file=out)
            index += 1
        else:
            print(f"... stopped after {max_tags} tag(s)", file=out)
            return

    print(f"\n{index} tag(s) printed (end of file)", file=out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", help="tag stream dump (.ttdump) file")
    parser.add_argument(
        "-n", "--max-tags", type=int, default=None,
        help="stop after printing this many tags (default: print all)",
    )
    args = parser.parse_args()

    print_ttdump(args.file, args.max_tags, sys.stdout)


if __name__ == "__main__":
    main()

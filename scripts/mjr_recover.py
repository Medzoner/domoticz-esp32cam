#!/usr/bin/env python3
"""Inspect MJR1 or recover its structurally complete prefix without editing it."""

import argparse
import json
import os
from pathlib import Path
import struct
import tempfile

MAGIC = b"MJR1\r\n\x1a\n"
MAX_FRAME_BYTES = 2 * 1024 * 1024


def read_up_to(source, size):
    data = bytearray()
    while len(data) < size:
        block = source.read(size - len(data))
        if not block:
            break
        data.extend(block)
    return data


def scan(source, output=None):
    """Only commit complete header/payload pairs. Do not decode JPEG contents."""
    if read_up_to(source, len(MAGIC)) != MAGIC:
        raise ValueError("missing or invalid MJR1 header")
    if output is not None and output.write(MAGIC) != len(MAGIC):
        raise OSError("short output write")
    frames = 0
    good_bytes = len(MAGIC)
    while True:
        header = read_up_to(source, 4)
        if not header:
            status = "complete"
            break
        if len(header) != 4:
            status = "truncated_header"
            break
        size = struct.unpack("<I", header)[0]
        if size == 0 or size > MAX_FRAME_BYTES:
            status = "invalid_frame_size"
            break
        payload = read_up_to(source, size)
        if len(payload) != size:
            status = "truncated_payload"
            break
        if output is not None:
            if output.write(header) != len(header) or output.write(payload) != size:
                raise OSError("short output write")
        frames += 1
        good_bytes += 4 + size
    return {"status": status, "frames": frames, "good_bytes": good_bytes}


def recover(source_path, output_path):
    """Stage in the destination filesystem; publish without overwriting any path.

    Hard-link publication is atomic and no-clobber, including a racing creator.
    Filesystems without hard-link support fail explicitly instead of falling
    back to a potentially destructive rename. The original is opened read-only.
    """
    source_path = Path(source_path)
    output_path = Path(output_path)
    temporary_path = None
    try:
        with source_path.open("rb") as source:
            with tempfile.NamedTemporaryFile(
                prefix=".mjr-recovery-", dir=output_path.parent, delete=False
            ) as output:
                temporary_path = Path(output.name)
                report = scan(source, output)
                output.flush()
                os.fsync(output.fileno())
        os.link(temporary_path, output_path)
        return report
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="offline MJR1 file (read-only)")
    parser.add_argument("--output", type=Path, help="new file; must not already exist")
    args = parser.parse_args(argv)
    try:
        if args.output is None:
            with args.source.open("rb") as source:
                report = scan(source)
        else:
            report = recover(args.source, args.output)
    except (OSError, ValueError) as error:
        print(json.dumps({"error": str(error)}))
        return 1
    print(json.dumps(report))
    # Inspection distinguishes a damaged tail; successful recovery is success
    # even when a tail was discarded. The report always retains its status.
    return 0 if args.output is not None or report["status"] == "complete" else 2


if __name__ == "__main__":
    raise SystemExit(main())

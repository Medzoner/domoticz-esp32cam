#!/usr/bin/env python3
"""Check compiler-reported per-function frames, not total task-stack usage."""

import argparse
from pathlib import Path

FRAME_LIMIT = 1024
REQUIRED = ("RecordingPlaybackServer::play(", "RecordingPlaybackServer::download(")


def check(report):
    seen = set()
    frames = []
    for line in report.splitlines():
        function, size, kind = line.rsplit("\t", 2)
        size = int(size)
        if size < 0 or kind != "static" or size > FRAME_LIMIT:
            raise ValueError(f"unbounded or oversized stack frame: {function}: {size} ({kind})")
        for name in REQUIRED:
            if name in function:
                seen.add(name)
                frames.append((name, size))
    if seen != set(REQUIRED):
        raise ValueError("missing playback/download stack reports; rebuild with -fstack-usage")
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("environments", nargs="+")
    args = parser.parse_args()
    for environment in args.environments:
        path = (Path(".pio/build") / environment / "src/adapters/recording"
                / "recording_playback_server.cpp.su")
        for name, size in check(path.read_text(encoding="utf-8")):
            print(f"{environment}: {name} {size} bytes (frame limit {FRAME_LIMIT})")


if __name__ == "__main__":
    main()

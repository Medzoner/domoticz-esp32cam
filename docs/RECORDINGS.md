# MJR1 integrity and interrupted recording recovery

MJR1 is deliberately small. It is not a journal, video codec or power-loss-safe
storage format. SD/FAT corruption and lost writes after sudden power failure
cannot be repaired by the firmware. Work on a downloaded, offline copy; keep
the original until you have independently verified any recovered result.

## On-disk format

All fields are packed, with no padding:

| Field | Encoding |
| --- | --- |
| File header | 8 bytes: `MJR1\r\n\x1a\n` |
| Frame length | 4-byte unsigned little-endian count of following payload bytes |
| Frame payload | Exactly that many bytes, an already-encoded JPEG |

Frame length must be 1 through 2 MiB inclusive (the recording reader/writer
limit). Frame records repeat until EOF. There is no final index, checksum,
timestamp, commit marker or expected total frame count. A file ending exactly
at a complete frame boundary is structurally complete, but that cannot prove
that later frames were never lost or that the JPEG bytes are uncorrupted.

## Firmware behavior

- Short header or payload writes fail the append. The recorder increments
  `recording_failures`, closes the segment and does not count that record as a
  completed frame. It does not retry the whole record after partial progress.
- Playback checks frame lengths against the remaining file size before sending
  an MJPEG part header. A known incomplete tail is not advertised as a new
  frame. Previously complete frames can still be played; playback stops at the
  first invalid, truncated or unreadable record, without guessing a new boundary.
- Positive short reads are accumulated. A zero read before the advertised file
  size is reached is a read failure, not a successful EOF. Downloads close early
  and log the read failure rather than claiming successful completion.
- Preflight uses the file size captured when opening the reader. Media can fail
  after a part was announced; clients may still receive an incomplete response
  on a later physical read/socket failure. There is no checksum verification.
- `File.flush()` in the pinned Arduino API does not provide a usable success
  result here. A full-length `write()` is not a durability guarantee, and this
  change does not detect every card/controller/filesystem failure.

## Non-destructive host recovery

Python 3, standard library only:

```sh
# Inspect; emits a JSON report, does not create or modify a recording.
python scripts/mjr_recover.py recording.mjr

# Write only the structurally complete prefix to a NEW file.
python scripts/mjr_recover.py recording.mjr --output recovered.mjr
```

Example report keys: `status`, `frames`, `good_bytes` (including the 8-byte file
header). Status is `complete`, `truncated_header`, `truncated_payload` or
`invalid_frame_size`. Recovery stops at the first defect, not at a guessed JPEG
signature. It cannot recover bytes that the filesystem no longer exposes or
validate a JPEG bitstream. An empty but valid container can be recovered as the
8-byte header only; this means zero recovered frames, not recovered footage.

The source is opened read-only and must not be modified concurrently. Output
is staged in the destination directory, flushed and fsynced, then published with
an atomic no-clobber hard link. An existing output path (including the source,
another link to it or a symlink) is never overwritten. Staging is cleaned up on
errors; filesystems without hard-link support fail explicitly. The final
directory entry is not promised durable through a subsequent power loss.

Exit codes:

- `0`: structurally complete inspection, or a successfully saved recovery
  (even if a damaged tail was discarded; inspect the report).
- `1`: invalid file header, I/O, publication or command error. No new output is
  published when recovery fails before publication.
- `2`: inspection found an incomplete or invalid tail. CLI syntax errors also
  use Python argparse's standard exit code `2` and do not emit a report.

Do not publish private camera recordings when reporting a bug. Reproduce using
synthetic bytes or a non-sensitive scene. See [RELIABILITY.md](RELIABILITY.md)
for physical SD fault-injection and soak-test requirements.

## Automated checks and limits

```sh
pio test -e native -e native-sanitized
python -m unittest discover -s test/python -v
```

Native tests exercise positive short reads, EOF vs truncation/read failure,
length bounds, endian agreement, and short header/payload writes. Host tests
exercise truncation at every offset of a small generated container, size bounds,
no-overwrite recovery and failed publication cleanup. No real SD media,
filesystem fault, power interruption or video decoder is exercised by these
tests. Back up valuable media before any hardware fault injection.

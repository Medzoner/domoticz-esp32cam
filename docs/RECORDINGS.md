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

- Playback and download reuse one 4 KiB I/O buffer owned by the server, outside
  its 4 KiB FreeRTOS task stack. The global server instance and sequential worker
  give that buffer a stable lifetime and a single owner; no per-request allocation
  is required. Do not instantiate this server as a task-local object or call its
  request handlers concurrently without changing that ownership model.
- Firmware builds emit GCC `.su` stack reports. CI checks the playback translation
  unit for static per-function frames of at most 1 KiB, including both delivery
  routes (`python scripts/check_playback_stack.py esp32cam`). This catches a
  reintroduced stack-local I/O buffer; it is not a whole-call-chain stack bound
  or a replacement for physical playback/download testing.
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

## Concurrency and temporary SD unavailability

The SD adapter is initialized once in setup before the worker tasks are started.
After initialization its mutex protects file handles, reader metadata and all
multi-field/64-bit statistics. Only the active/pending capture-demand flags are
read without that mutex; they are C++ atomics, not `volatile`. They are a capture
hint, not a transactional snapshot.

- A failed stats lock returns an explicitly invalid snapshot, without reading
  live counters. `/health` exposes `recording_stats_valid: false` and degraded
  status. Other recording fields in that response are placeholders, **not**
  evidence of an idle recorder, empty card or zero capacity. Disabled SD support
  has a valid, unsupported snapshot instead of pretending the mutex failed.
- The dashboard shows an unavailable/retrying state, disables recording actions
  and does not automatically fetch a misleading empty listing while stats are
  invalid. Normal polling restores controls when telemetry becomes available.
- `/recording/stop` returns HTTP 503 if the stop request could not be accepted.
  A successful response means queued, not flushed/stopped; poll health afterward.
  An accepted command survives a later service-lock timeout and can be serviced
  on a subsequent iteration. Failed requests may be retried by the client.
  Commands are serialized under the mutex: the last **accepted** command wins.
  A start while active cancels a queued stop without resetting the segment;
  a stop cancels a pending start. A failed command changes neither intent.
- Listing failure is distinct from an empty list. `/recordings` returns HTTP 503
  on failed telemetry/list acquisition (HTTP 409 still denotes a known active
  recording). No filesystem listing is attempted after a lock failure.
- Reader size is copied under the mutex with explicit failure, not inferred as
  an empty file on timeout. Reader closure also reports failure. The playback
  worker closes the client socket and retries reader cleanup, yielding between
  attempts, before accepting another client. It cannot silently abandon a live
  reader that would block later recording starts. If the underlying driver
  remains stuck, cleanup can remain pending indefinitely; this is not a global
  SD-operation deadline or a guarantee of playback availability.

The internal RecordingPort now reports success explicitly for stop, list,
reader-size and reader-close operations. Alternative adapters must preserve
these contracts, including zeroing list/size outputs on failure and retaining
reader ownership until successful closure.

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
pio test -e native-storage -e native-storage-sanitized
python -m unittest discover -s test/python -v
node --test test/web/*.test.cjs
```

Native tests exercise positive short reads, EOF vs truncation/read failure,
length bounds, endian agreement, and short header/payload writes. Host tests
exercise truncation at every offset of a small generated container, size bounds,
no-overwrite recovery and failed publication cleanup. No real SD media,
filesystem fault, power interruption or video decoder is exercised by these
tests. Back up valuable media before any hardware fault injection.

The dedicated storage environments compile the **real** SdMjpegRecorder source
against test-only FreeRTOS/filesystem doubles in `test/support/sd_recorder`.
They force lock failures, verify invalid telemetry/explicit command failures,
exercise retryable reader cleanup and short writes, and instrument filesystem
calls to reject access without the fake mutex. The doubles do not model task
scheduling, priority inheritance, FAT, DMA or a real SD driver. ASan/UBSan do
not detect all data races; this is not a ThreadSanitizer or hardware concurrency
test. Node 18+ tests exercise dashboard data behavior with a minimal fake DOM,
not browser layout. CI's pinned Ubuntu image supplies Node; no npm packages are
needed.

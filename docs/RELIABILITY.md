# Reliability contract and hardware validation

This project aims to be a useful, inspectable ESP32 camera reference, not an
Espressif-certified product. A successful build is **not** a successful hardware
test. Native tests cannot validate lwIP, radio behavior, SD media, the camera
driver, power integrity, or FreeRTOS scheduling.

## Enforced software boundaries

- At most four live MJPEG sessions; a fifth receives an unavailable response.
- One captured JPEG is broadcast sequentially. Slow clients can delay subsequent
  clients; this is not independent per-client scheduling.
- Live and playback payload writes use `send(..., MSG_DONTWAIT)` with one elapsed
  time budget per `writeAll` call. Partial progress does not reset that budget;
  retryable errors yield a FreeRTOS tick. Expiry or a fatal error closes the
  session. Deadline arithmetic handles the 32-bit `millis()` rollover.
- A live frame has separate preamble and payload writes per client; playback
  and download have a separate budget for each block. There is no whole-file
  deadline and no hard real-time guarantee: scheduling adds latency.
- The stream task reserves a single 512 KiB PSRAM-only delivery buffer before
  opening its listener. Reservation failure marks startup failed and deletes the
  task; HTTP diagnostics remain available. There is no DRAM fallback. On listener
  startup failure the reservation is freed. Successful streaming keeps it until
  reboot; no frame-loop allocations are performed.
- Each JPEG is validated and copied while its camera lease is held. That lease
  is released exactly once before network or SD delivery, including rejection
  paths. The copy remains valid even when the driver immediately reuses its
  frame. This bounds copy memory, not the duration of camera capture or memcpy.
- Oversized JPEGs are dropped whole rather than truncated or used to grow the
  buffer. `/health` exposes the configured `stream_frame_capacity`, total
  `stream_dropped_frames` (acquisition, validation or formatting failure), and
  `stream_oversized_frames` (the oversized subset). Frames count only completed
  broadcast iterations; it does not assert every client received the frame or
  SD append succeeded. The 512 KiB limit is a deliberate behavior change for
  high-resolution/low-compression scenes; lower resolution or increase numeric
  JPEG quality if necessary. Snapshots are not subject to this stream-copy cap.
- Slow network/SD delivery still serializes the stream task and delays its next
  frame. It no longer holds the camera mutex, but this does not isolate clients,
  bound an SD driver call, or guarantee that other tasks meet latency targets.
- Stream telemetry and control flags shared between HTTP and stream tasks use
  C++ atomics, not `volatile`. Individual fields are synchronized; a health
  response is not an atomic snapshot of all counters. Stop requests are consumed
  with an atomic exchange so a concurrent new request is not cleared afterward.
- Live and playback services expose `not_started`, `starting`, `running` or
  `failed`. Task allocation success means scheduled, not ready; readiness is
  published only after `WiFiServer` reports a listening socket. Failed socket
  startup closes the listener and deletes that task. Repeated starts do not
  create duplicate tasks; failure is sticky until reboot.
- A live/playback startup failure does not trigger a reboot loop. HTTP controls
  still start (if their own allocation succeeds), and `/health` reports
  `status: "degraded"` with `stream_service` and `playback_service` states.
  For compatibility, `/health` remains HTTP 200 when diagnostic JSON is available;
  monitors must check `status` for readiness, not just the HTTP code. HTTP
  availability is not camera/video readiness. Recording start returns HTTP 503
  until the stream task is running, since that task services the recorder.
- Request-line/header byte limits and read timeouts protect admission, but
  admission itself currently runs in the stream task and can delay video.
- Authentication is optional and HTTP is unencrypted. Use an isolated trusted
  network; never forward device ports to the Internet. See [SECURITY.md](../SECURITY.md).

## Automated checks

```sh
pio test -e native
pio test -e native-sanitized
pio check -e native
pio check -e esp32cam
pio run -e esp32cam -e esp-eye -e esp32s3-cam-lcd -e m5stack-camera
```

The sanitized environment runs AddressSanitizer and UndefinedBehaviorSanitizer
with recovery disabled. It needs a host GCC/Clang with sanitizer support, not an
ESP32. Transport tests inject a clock and a nonblocking sender to cover partial
writes, stalled readers, disconnects, timeout despite progress, late final
writes, rollover and invalid input. These are deterministic unit tests, not
claims about throughput or radio reliability.

Camera-copy tests use a fake camera that overwrites its bytes on release. They
verify independent copied data, exactly-once release after successful acquire,
release of invalid/oversized frames, no partial copy, no release after failed
acquire, and buffer reuse after failures. The snapshot path uses the same lease
helper, including invalid successful acquisitions and allocation failure; error
responses are sent only after releasing the lease. Snapshot allocation policy
(PSRAM then internal-heap fallback) is unchanged. Tests do not exercise real
PSRAM, the HTTP server or DMA.

Service-startup tests inject task-creation success/failure and early worker
execution, check idempotence and sticky failure, and distinguish allocation
success from readiness. They do not allocate FreeRTOS tasks. Verify the adapter
failure paths on hardware by using a disposable test build that forces each
`xTaskCreatePinnedToCore` failure separately, then forces each listener startup
failure. Confirm `/health` reports degradation, `/recording/start` is rejected when
the stream service failed, and controls remain responsive. Reboot with the
normal build afterward. Also verify normal boot reports both services running.

## Hardware acceptance protocol

Use a dedicated test network and synthetic/non-sensitive scene. For every
result, record the commit, board revision, sensor, PSRAM, power supply, SD model,
SDK/tool versions, resolution/quality, client versions and scenario duration.
Do not publish credentials, cookies, private addresses, recordings or raw
serial logs without redacting them.

Capture `/health` and serial diagnostics before, during and after each case.
Record actual FPS, delivery latency, free heap/PSRAM, task stack headroom,
adaptive pressure and reset reason. Compare heap at the same workload after
warm-up; a one-off heap delta is not evidence of a leak.

| Scenario | Procedure | Acceptance criteria |
| --- | --- | --- |
| Cold boot | 20 power cycles with a stable rated supply | No boot loop; camera and health recover every cycle |
| Normal fan-out | Run 1, 2 and 4 consumers, then attempt a fifth | Bounded admission; valid MJPEG; fifth rejected; existing sessions remain usable |
| Slow reader | Use a socket client with a small receive buffer; request `/stream`, then stop reading until TCP back-pressure occurs | Stalled session eventually closes; other clients resume; controls recover; no reset |
| Abrupt disconnect | Repeatedly connect and kill clients, including mid-JPEG | Slots reclaimed; later connections work; no sustained resource loss |
| Slow headers | Send request/header bytes slowly and exceed size limits | Connection dropped within configured bounds plus scheduling margin; device recovers |
| Wi-Fi loss | Disable AP for 60 seconds, restore it; repeat 10 times | Reconnection without manual reboot; stale sessions removed; new stream works |
| Load recovery | Add radio contention/weak signal, then restore good conditions | Pressure rises and FPS drops; healthy windows produce gradual recovery |
| SD concurrency | On AI Thinker, record with 4 viewers and playback/download | Valid segments, bounded network stalls, honest error state; no crash |
| Camera contention | With a stalled live reader and SD load, request camera controls and snapshots repeatedly | Capture lease released before delivery; measure actual control/snapshot latency and verify recovery without claiming a hard bound |
| Copy capacity | In a disposable test build reduce the frame cap below JPEG size, then restore it | Oversized and dropped counters rise; no partial JPEG is emitted or recorded; controls work; normal build resumes delivery |
| Copy allocation | In a disposable build force the PSRAM reservation to fail | Stream reports failed; no live listener; HTTP diagnosis works; no DRAM fallback or reboot loop |
| SD full/failure | Fill a disposable test card; separately test missing/bad media | Explicit failure; existing files not silently deleted; controls usable |
| Soak | At least 24 hours with periodic connects/disconnects and health samples | No unexplained reset, deadlock, progressive heap loss or exhausted client slots |
| Rollover | Exercise a test firmware clock near wrap or run beyond 49.7 days | No indefinite socket wait or broken interval scheduling |

Do not remove a mounted SD card or cut power during writes on valuable media.
Use disposable cards and expect the last segment/filesystem to need recovery:
the recorder is not a power-loss-safe filesystem.

## Evidence levels and remaining priorities

Report **build-only**, **native-tested**, or **hardware-tested** per board and
scenario, with artifacts and limitations. Leave unexecuted cases **not tested**;
never infer hardware coverage from the build matrix or use a screenshot as a
performance benchmark.

Before describing a release as a community reference validated on hardware,
collect the matrix above. Further work should prioritize auditing shared state
in the other adapters, measuring camera capture/copy occupancy, SD blocking
and interrupted-recording recovery, malformed-input fuzzing, and repeatable
hardware-in-the-loop fault injection. Track these as separate, reviewable
changes rather than claiming this document or a single patch solves them.

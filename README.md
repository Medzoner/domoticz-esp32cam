# ESP32-CAM Multi-Client Streamer

PlatformIO firmware for the AI Thinker ESP32-CAM. It provides a responsive
control interface, JPEG snapshots, and one shared MJPEG stream for multiple
consumers such as MotionEye, Home Assistant, and OctoPrint.

## Features

- Up to four simultaneous MJPEG clients
- Fixed-rate 10 FPS stream to limit CPU and network usage
- Runtime camera controls for resolution, JPEG quality, brightness, contrast,
  vertical flip, and horizontal mirror
- Health telemetry for Wi-Fi, heap usage, stream rate, sessions, and task stack
  headroom
- Static IPv4 configuration, DHCP hostname, and mDNS discovery
- Native unit tests and Cppcheck static analysis
- GitHub Actions CI and tagged firmware releases

## Streaming Performance

The camera uses two PSRAM frame buffers with latest-frame capture to minimize
latency. The MJPEG server combines each multipart boundary and JPEG header into
one TCP write, then sends image data in 16 KiB blocks. This reduces socket calls
while retaining a bounded timeout for slow consumers.

The browser interface uses asynchronous image decoding and automatically
reconnects with exponential backoff after a stream interruption. It disconnects
its own stream while the page is hidden, which avoids wasting Wi-Fi bandwidth,
browser decoding time, and ESP32 processing capacity in background tabs.

The default 10 FPS cap favors stable multi-client delivery over maximum frame
rate. JPEG quality has the greatest effect on bandwidth; increase the numeric
quality value when more simultaneous clients or a weaker Wi-Fi signal require
smaller frames.

### Adaptive resource controller

The stream server evaluates system pressure every two seconds from the active
client count, average frame delivery time, Wi-Fi RSSI, free heap, and socket
write failures. It selects one of five profiles ranging from 10 FPS at the
configured JPEG quality to an emergency 4 FPS profile with additional JPEG
compression.

Degradation is immediate when pressure rises. Recovery requires three healthy
evaluation windows per level, which prevents rapid profile oscillation. The
configured JPEG quality remains the user's baseline; adaptive compression is a
temporary offset and is removed when conditions recover. `/health` and the web
interface expose the target FPS, delivery time, quality offset, and pressure
level.

The web interface renders the complete `/health` payload as a system health
panel. It refreshes every two seconds, prevents overlapping requests, disables
HTTP caching, and pauses polling while the browser tab is hidden.

## Hardware

- AI Thinker ESP32-CAM with an OV2640 sensor
- PSRAM-enabled board configuration
- USB-to-serial adapter for initial flashing

## Configuration

Create the local configuration file:

```bash
cp include/secrets.example.h include/secrets.h
```

Set the Wi-Fi credentials, hostname, static address, and gateway in
`include/secrets.h`. This file is ignored by Git and must never be committed.

The default hostname in the example configuration is `octoprint-cam`. Once the
device is online, supported clients can reach it at
`http://octoprint-cam.local`.

## Local Development

Install the pinned CI toolchain:

```bash
python -m pip install --requirement requirements-ci.txt
```

Run the project checks:

```bash
pio test --environment native
pio check --environment native
pio check --environment esp32cam
pio run --environment esp32cam
```

Flash and monitor a connected board:

```bash
pio run --environment esp32cam --target upload
pio device monitor --baud 115200
```

The local `platformio.ini` defaults to `/dev/ttyUSB0`. Override the port on the
command line when needed.

## HTTP API

The control server listens on port 80:

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Responsive camera interface |
| `GET` | `/health` | Device, Wi-Fi, memory, and stream telemetry |
| `GET` | `/status` | Current camera settings |
| `GET` | `/capture` | One JPEG snapshot |
| `GET` | `/control?var=<name>&val=<value>` | Update one validated camera setting |
| `GET` | `/stream-stop` | Disconnect all MJPEG consumers |

The multi-client MJPEG stream listens separately on port 81:

```text
http://octoprint-cam.local:81/stream
```

## Architecture

Dependencies point from adapters toward the core and ports:

- `src/core/` contains platform-independent validation and MJPEG protocol code.
- `src/ports/` defines the camera contract used by the application.
- `src/adapters/` implements the OV2640 camera, Wi-Fi, HTTP API, and MJPEG server.
- `src/config/` contains compile-time application settings.
- `src/ui/` contains the embedded web interface.
- `src/main.cpp` is the composition root.
- `test/test_core/` contains native Unity tests.

## Continuous Integration

`.github/workflows/ci.yml` runs on pull requests and pushes to `main`. It:

1. installs the pinned PlatformIO version;
2. creates a build-only configuration from `secrets.example.h`;
3. runs native unit tests;
4. runs native and ESP32 Cppcheck analysis;
5. builds the ESP32 firmware; and
6. uploads firmware artifacts for 14 days.

## Releases

Push a semantic version tag to build and publish a GitHub Release:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The release includes a standalone application binary and a compressed flashing
bundle. The bundle contains the bootloader, partition table, OTA bootstrap,
application firmware, checksums, and the complete `esptool.py` command with the
required flash offsets.

The release workflow can also be started manually for an existing tag from the
GitHub Actions interface.

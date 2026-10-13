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
- First-boot provisioning portal with network scanning and captive portal
  detection
- Same-origin checks and rate limiting on the control interface
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

The same file also holds `CAMERA_SETUP_AP_PASSWORD` for the onboarding access
point.

The default hostname in the example configuration is `octoprint-cam`. Once the
device is online, supported clients can reach it at
`http://octoprint-cam.local`.

## First Boot Onboarding

The firmware starts a provisioning portal instead of the camera stack when
`WIFI_SSID` still holds the placeholder `your-wifi`, or when the stored
credentials fail to associate:

1. The board publishes the `<hostname>-setup` access point (`esp32cam-setup` as
   a fallback), protected by `CAMERA_SETUP_AP_PASSWORD` when a value is set.
2. Connect from a phone or laptop and open any address. A wildcard DNS server
   redirects the request to the portal, so no URL has to be remembered.
3. Use **Scan networks** to list nearby access points, then submit the network
   name and passphrase. The form accepts open networks and WPA2 passphrases of
   8 to 63 characters, or a 64 character hexadecimal key.
4. The camera stores the credentials in NVS and reboots into station mode.

Provisioning stops after 2.5 seconds once credentials are saved, or after ten
minutes without a submission when the portal was opened by a failed connection.

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

| Method | Path | Access | Description |
|---|---|---|---|
| `GET` | `/` | Open | Responsive camera interface |
| `GET` | `/health` | Open | Device, Wi-Fi, memory, and stream telemetry |
| `GET` | `/status` | Open | Current camera settings |
| `GET` | `/capture` | Open | One JPEG snapshot |
| `POST` | `/control?var=<name>&val=<value>` | Same origin | Update one validated camera setting |
| `POST` | `/stream-stop` | Same origin | Disconnect all MJPEG consumers |

Mutating browser requests require an `Origin` or `Referer` header that matches
the camera's own `Host`, accept only `POST`, and are limited to one accepted
mutation every 250 ms. Trusted non-browser IIoT clients may omit those headers.

The multi-client MJPEG stream listens separately on port 81 and stays open so
existing consumers keep working:

```text
http://octoprint-cam.local:81/stream
```

## Security

The camera is an unauthenticated plain HTTP device intended for an isolated,
trusted IIoT network. Every host on the segment can read the video feed and
invoke the API, so network segmentation and firewall policy are the security
boundary.

- `/control` and `/stream-stop` accept `POST` only. The camera compares the
  `Origin` (or `Referer`) header against its own `Host` and answers `403` for a
  foreign browser origin, which blocks cross-site requests.
- Mutations are rate limited to one accepted change every 250 ms, and numeric
  values go through a strict integer parser instead of `atoi()`.
- `/capture` copies the frame out of the camera buffer before writing to the
  network, so a slow client can no longer hold the camera mutex.
- The MJPEG accept path drains request headers under a 250 ms deadline and a
  1 KiB byte budget, so a client that streams headers forever cannot block the
  stream task.
- Responses no longer send `Access-Control-Allow-Origin: *`. The interface also
  sets `X-Content-Type-Options`, `X-Frame-Options`, and `Referrer-Policy`.
- All endpoints stay open for OctoPrint, MotionEye, and other IIoT consumers.

Known residual risks: no TLS, no application authentication, no secure boot,
and no flash encryption. Do not expose the device directly to an untrusted LAN
or the Internet.

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

Third-party actions are pinned to full commit SHAs with the release version in
a trailing comment. The release workflow grants `contents: write` only to the
publishing job and validates the release tag against `v<major>.<minor>.<patch>`
before creating anything.

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

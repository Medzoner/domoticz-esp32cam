#pragma once

// Copy this file to secrets.h and fill in the local values.
// secrets.h is ignored by Git.
#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"

#define CAMERA_HOSTNAME "octoprint-cam"
#define CAMERA_IP_1 192
#define CAMERA_IP_2 168
#define CAMERA_IP_3 1
#define CAMERA_IP_4 105
#define CAMERA_GATEWAY_1 192
#define CAMERA_GATEWAY_2 168
#define CAMERA_GATEWAY_3 1
#define CAMERA_GATEWAY_4 1

// Passphrase of the Wi-Fi setup access point. Leave it empty to publish an
// open access point instead. Change this value before sharing the firmware.
#define CAMERA_SETUP_AP_PASSWORD "change-me-setup"

// Administrative credentials for HTTP Basic authentication on the control
// interface (/, /status, /control, /stream-stop). Leave the password empty to
// disable authentication, which is only acceptable on a trusted lab network.
// Change these values before sharing the firmware.
#define CAMERA_ADMIN_USER "admin"
#define CAMERA_ADMIN_PASSWORD "change-me-admin"

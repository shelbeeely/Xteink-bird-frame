# Xteink X4 firmware display node

This PlatformIO project runs the Inky Bird Frame display node directly on the
Xteink device firmware stack instead of on a Raspberry Pi. The Python
controller remains responsible for discovery, generation, review, catalog
publication, and the read-only HTTP service.

## What it does

- connects the device to Wi-Fi;
- fetches the active catalog from `/v1/catalog`;
- preserves rotation state in NVS;
- downloads `display.png`, verifies the catalog SHA-256 checksum, decodes the
  PNG, and renders it through the community SDK's `EInkDisplay` driver; and
- sleeps until the next rotation cycle.

The current firmware project targets the community SDK checked in at
`firmware/open-x4-sdk` and uses its `EInkDisplay` library directly.

## Configure

Copy the example config and edit the values for your installation:

```bash
cp /home/runner/work/Xteink-bird-frame/Xteink-bird-frame/firmware/include/config.h.example \
  /home/runner/work/Xteink-bird-frame/Xteink-bird-frame/firmware/include/config.h
```

Set:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `CONTROLLER_URL`
- `ROTATION_MODE`
- `ROTATION_MINUTES`
- `PRIORITIZE_LATEST_DETECTION`

`config.h` stays ignored so installation-specific Wi-Fi credentials are not
committed.

## Build

Initialize the SDK submodule and compile from the repository root:

```bash
git submodule update --init --recursive
cd /home/runner/work/Xteink-bird-frame/Xteink-bird-frame/firmware
pio run
```

## Flash

Connect the device over USB and upload with PlatformIO:

```bash
cd /home/runner/work/Xteink-bird-frame/Xteink-bird-frame/firmware
pio run --target upload
```

## Notes

- The firmware client still requires a separate controller reachable at
  `CONTROLLER_URL`.
- The existing Raspberry Pi display-node workflow remains supported for
  installations that prefer a Linux-based display client.

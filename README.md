# FARLock v3 firmware

Firmware for the third generation FARLock, an open source motorised door
deadbolt. This version is a **Matter over Thread** device built on Zephyr and
the nRF Connect SDK, meant to be commissioned into Home Assistant through a
Thread border router. It replaces the v2 firmware
([farlock_nrf52](https://github.com/frazieje/farlock_nrf52)), which used
Bluetooth 6LoWPAN, MQTT and custom apps.

Hardware: <https://github.com/frazieje/farlock3-hw> (nRF52840, TB6612FNG
h-bridge, QSPI flash for DFU, USB console, NFC antenna).

## Features

- Matter Door Lock cluster with users, PIN credentials and optional schedules
  (Thread sleepy end device, ICD).
- Real bolt control: TB6612FNG motor driver, limit switch feedback, handedness
  switch, jam detection reported as a Matter `LockJammed` alarm, manual
  thumb-turn changes reported as `Manual` operations.
- NFC: the lock emulates a writable NFC Forum Type 4 Tag. Write a Home
  Assistant tag into it with the companion app, then tapping the lock with a
  phone fires `tag_scanned` in Home Assistant and an automation locks or
  unlocks through Matter.
- Matter OTA with MCUboot, secondary slot in external QSPI flash.
- USB CDC ACM console and shell in debug builds.

## Repository layout

This repository is the manifest repository of a west workspace (T2 topology).

```
farlock3/            this repo (manifest)
├── app/             the Zephyr application (derived from NCS samples/matter/lock)
├── boards/frazieje/farlock3/   custom board definition
├── dts/bindings/    devicetree bindings for the motor and switches
└── west.yml         pins nRF Connect SDK v3.3.0
```

## Building

```sh
west init -m https://github.com/frazieje/farlock3 --mr main farlock3-ws
cd farlock3-ws
west update

# debug build for the real board
west build -b farlock3 farlock3/app

# release build (no console, no USB, watchdog)
west build -b farlock3 farlock3/app -- -DFILE_SUFFIX=release

# nRF52840 DK bench build (motor on P0.28..P0.31, switches on buttons 3/4)
west build -b nrf52840dk/nrf52840 farlock3/app

# flash
west flash
```

With the toolchain installed by `nrfutil sdk-manager`, prefix the build with
`nrfutil toolchain-manager launch --ncs-version v3.3.0 --`.

Useful options (`-DCONFIG_...=y` or in a `.conf`):

| Option | Purpose |
|---|---|
| `FARLOCK3_LOCK_SIMULATED` | No motor or switches, actions complete after a delay |
| `FARLOCK3_MOTOR_TIMEOUT_MS` | Max motor run before a jam is reported (default 3000) |
| `FARLOCK3_MOTOR_DUTY_CYCLE` | Motor PWM duty in percent (default 100) |
| `LOCK_SCHEDULES` | Week day, year day and holiday schedules (`-DEXTRA_CONF_FILE=schedules.conf`) |

## Bench testing

Debug builds expose a `farlock` shell command on the USB console
(`/dev/serial/by-id/usb-frazieje_FARLock_v3_*`, 115200 8N1):

```
farlock status   # bolt state, switch inputs, motor state
farlock lock     # drive the bolt to the locked position
farlock unlock   # drive the bolt to the unlocked position
farlock toggle
```

On the nRF52840 DK, button 2 toggles the bolt, button 3 is the handedness
switch and button 4 is the position switch. Resetting the board re-enumerates
the USB console, so reopen the terminal after a reset or factory reset.

## Commissioning

The device uses the Matter test vendor ID and the example lock product ID
(0x8006) with test attestation data generated at build time. Home Assistant's
Matter server accepts test devices. The QR code and manual pairing code are
printed on the console at boot. Hold the program button for more than 3 s to
factory reset.

## Licence

Application sources derived from the nRF Connect SDK keep their
`LicenseRef-Nordic-5-Clause` headers; new files are Apache-2.0.

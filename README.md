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

To check the external QSPI flash, wake it first: the application puts it
into deep power-down at boot, and the flash shell panics on a suspended
device.

```
pm resume mx25r6435f@0
flash read mx25r6435f@0 0 32
flash erase mx25r6435f@0 0x7F0000 0x1000
flash write mx25r6435f@0 0x7F0000 0xDEADBEEF 0x01234567
flash read mx25r6435f@0 0x7F0000 16
flash erase mx25r6435f@0 0x7F0000 0x1000
pm suspend mx25r6435f@0
```

The flash shell parses sizes as hexadecimal, so erase sizes must be written
as `0x1000`, not `4096`. The last sector (0x7F0000) is outside every
partition. Keep shell lines under about 60 characters; the USB console drops
input beyond its receive buffer.

## Firmware update

Once the board carries this partition map (MCUboot 64 KB at 0x0, application
slot at 0x10000), it never needs the SWD header again. Three update paths
exist; all of them take the signed application image that every build
produces (`build-*/app/zephyr/zephyr.signed.hex`, or the Matter OTA file
`build-*/dfu_application.zip`).

### USB serial recovery (MCUboot)

MCUboot enumerates as `1915:0005 FARLock v3 bootloader` and keeps the
connection LED lit while in recovery mode. It enters recovery when:

- the program button (P0.12, J6 pin 3) is held while power is applied,
- the running application asks for it (`farlock dfu` on the debug console),
- there is no bootable application in the primary slot.

Upload with any MCUmgr client. `smpmgr` (pip) is known to work; nrfutil does
not recognise this bootloader because it identifies MCUboot devices by a
fixed VID:PID list. One-time setup in the west workspace root:

```
python3 -m venv .venv-smp && .venv-smp/bin/pip install smpmgr
```

Then, with the board in recovery mode:

```
P=/dev/serial/by-id/usb-frazieje_FARLock_v3_bootloader_*-if00
.venv-smp/bin/smpmgr --port $P --line-buffers 8 image state-read
.venv-smp/bin/smpmgr --port $P --line-buffers 8 image upload \
    build-farlock3/app/zephyr/zephyr.signed.bin
.venv-smp/bin/smpmgr --port $P os reset
```

`--line-buffers 8` matches MCUboot's receive buffering and makes the upload
about twice as fast (roughly one minute for a debug image). The upload goes
straight into the primary slot, so it does not depend on the external flash;
the bootloader refuses to erase that slot, and a half-finished upload simply
leaves the device in recovery mode (no bootable image) for another attempt.

### Bluetooth LE (nRF Connect Device Manager)

A short press of the program button starts the SMP update service over
Bluetooth LE for as long as the connection lasts. Before commissioning it is
started together with the Matter commissioning advertisement; afterwards the
press starts only the update service. Open the nRF Connect Device Manager
app (Android or iOS), connect to `FARLock3`, upload `zephyr.signed.bin` from
`dfu_application.zip` and choose test-and-confirm. The image lands in the
QSPI secondary slot, MCUboot swaps it in on the following reboot, and the
application confirms it after a successful start, so a broken image reverts
by itself. There is no authentication on the SMP service; the physical
button press is the access control.

The same works from a Linux host with a Bluetooth adapter, using the smpmgr
client from the section above (the lock advertises as `FARLock3`; its random
address changes, so scan first):

```
.venv-smp/bin/smpmgr --ble <address> image upload build-farlock3/app/zephyr/zephyr.signed.bin
.venv-smp/bin/smpmgr --ble <address> image state-write <hash of the new image>
.venv-smp/bin/smpmgr --ble <address> os reset
```

The image version (`CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION`) and the Matter
software version in `prj.conf` / `prj_release.conf` must be bumped for every
release: MCUmgr refuses to schedule a swap to an image identical to the
running one, and Matter OTA only accepts a higher software version.

### Matter OTA

The build also produces the Matter OTA image (`build-*/matter.ota`) for an
OTA provider on the fabric. This path needs the device commissioned on
Thread first.

## Commissioning

The device uses the Matter test vendor ID and the example lock product ID
(0x8006) with test attestation data generated at build time. Home Assistant's
Matter server accepts test devices. The QR code and manual pairing code are
printed on the console at boot. Hold the program button for more than 3 s to
factory reset.

## Licence

Application sources derived from the nRF Connect SDK keep their
`LicenseRef-Nordic-5-Clause` headers; new files are Apache-2.0.

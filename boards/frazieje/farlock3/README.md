# FARLock v3 board

`farlock3` is the 3rd generation FARLock controller board, based on the nRF52840
(Raytac MDBT50Q-U1M). Hardware design: <https://github.com/frazieje/farlock3-hw>.

## Pin map (hardware v02)

| Function | Pin | Notes |
|---|---|---|
| Motor PWMA (TB6612FNG) | P0.28 | `pwm0` channel 0 |
| Motor AIN1 / AIN2 | P0.29 / P0.30 | direction |
| Motor STBY | P0.31 | high = driver enabled |
| Connection LED | P0.14 | J3, active low |
| Access granted LED | P0.15 | J5, active low |
| Access denied LED | P0.16 | J5, active low |
| Debug LED (blue, on-board) | P0.13 | active low |
| Program button | P0.12 | 10 k pull-up, also J6; `mcuboot-button0`: hold at power-up for USB serial recovery |
| Bolt position switch | P0.25 | J5 pin 2, 10 k pull-up, closed = GND |
| Handedness switch (SW1) | P0.24 | 10 k pull-up, closed = GND = left-handed, open = right-handed |
| QSPI flash (MX25R6435F) | P0.17, P0.19 to P0.23 | same as nRF52840 DK |
| NFC antenna | P0.09 / P0.10 | J4 |
| USB | D+/D- | console (CDC ACM), power |
| SWD | J2 | SWDIO, SWDCLK, RESET (P0.18), GND, 3V3 |

There is no UART routed on this board; the Zephyr console and shell are on
USB CDC ACM.

## Devicetree nodes

- `leds` / `buttons`: consumed by the nRF Connect SDK `dk_buttons_and_leds`
  library through the Matter board layer.
- `lock_switches` (`frazieje,lock-switches`): bolt position and handedness
  inputs, read by the application's `lock_hw` module with level-sensitive,
  low-power interrupts.
- `motor` (`frazieje,tb6612-motor`): PWM plus direction and standby GPIOs.

Bindings live in `../../../dts/bindings`.

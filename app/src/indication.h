/*
 * FARLock v3 LED indication.
 *
 * Three LEDs: the connection LED (J3), the green "access granted" and the red
 * "access denied" LEDs (J5). The lock lives on a battery, so nothing is lit
 * in steady state: every pattern is started by an event and ends by itself.
 *
 *   locked (any source)        green, one 1 s flash
 *   unlocked (any source)      red, one 1 s flash
 *   motor running              target colour blinks until the bolt arrives
 *   jam                        red and green alternate, five times
 *   NFC tag read               green, one short flash
 *   commissioning advertising  connection LED, short blink once a second
 *   phone connected (BLE)      connection LED, fast blink
 *   joined Thread              connection LED, three quick flashes
 *   button tap, commissioned   connection LED, one flash on Thread, three if not
 *   idle                       everything off
 *
 * The factory-reset warning (all LEDs blinking while the button is held) and
 * the Matter Identify blink come from the nRF Connect SDK board layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FARLOCK3_INDICATION_H
#define FARLOCK3_INDICATION_H

#ifdef __cplusplus
namespace Indication
{
/** Hooked into the board layer: Nrf::GetBoard().Init(buttons, StatusLedHandler). */
void StatusLedHandler();

/** The motor started moving the bolt towards locked (true) or unlocked. */
void LockOperationStarted(bool locking);
/** The bolt arrived (motor, thumbturn or key); locked tells which way. */
void LockOperationCompleted(bool locked);
/** The motor timed out without reaching the position switch. */
void LockJammed();
/** Short press of the program button on a commissioned lock. */
void ButtonTap(bool threadAttached);
/** Matter Identify finished; the board layer blinked the green LED. */
void IdentifyDone();
} /* namespace Indication */

extern "C" {
#endif

/** A reader fetched the NDEF message. Callable from interrupt context. */
void indication_nfc_tap(void);

#ifdef __cplusplus
}
#endif

#endif /* FARLOCK3_INDICATION_H */

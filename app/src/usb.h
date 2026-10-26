/*
 * USB device (CDC ACM console) bring-up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FARLOCK3_USB_H
#define FARLOCK3_USB_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the USB device stack. Waits briefly for VBUS and for a terminal
 * to open the port so that early boot logs are not lost; returns a negative
 * errno if no host showed up, which is not an error for a battery powered
 * device.
 */
int start_usb(void);

#ifdef __cplusplus
}
#endif

#endif /* FARLOCK3_USB_H */

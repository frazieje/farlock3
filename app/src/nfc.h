/*
 * FARLock v3 NFC tag: a writable NFC Forum Type 4 Tag whose NDEF file is
 * persisted in the settings subsystem. The Home Assistant companion app writes
 * its tag record into it; afterwards a phone tap triggers HA automations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FARLOCK3_NFC_H
#define FARLOCK3_NFC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load the stored NDEF file (or start with an empty one), set up Type 4 Tag
 * emulation in read/write mode and start sensing the field.
 *
 * Must be called after the settings subsystem is usable.
 */
int nfc_tag_init(void);

/** Length of the NDEF message currently presented (0 when the tag is empty). */
size_t nfc_tag_ndef_len(void);

#ifdef __cplusplus
}
#endif

#endif /* FARLOCK3_NFC_H */

/*
 * Persistent storage for the NFC NDEF file, backed by the Zephyr settings
 * subsystem (key "farlock3/ndef").
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FARLOCK3_NDEF_STORAGE_H
#define FARLOCK3_NDEF_STORAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the settings subsystem (if needed) and load the stored file into RAM. */
int ndef_storage_init(void);

/**
 * Get a pointer to the file loaded at init (or last written).
 * @p data_size is 0 when nothing is stored.
 */
int ndef_storage_read(const void **data, size_t *data_size);

/** Persist @p data_size bytes. Blocks while the settings backend writes flash. */
int ndef_storage_write(const void *data, size_t data_size);

#ifdef __cplusplus
}
#endif

#endif /* FARLOCK3_NDEF_STORAGE_H */

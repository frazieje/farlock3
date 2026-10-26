/*
 * Persistent storage for the NFC NDEF file, backed by the Zephyr settings
 * subsystem.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ndef_storage.h"

#include <errno.h>
#include <string.h>

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ndef_storage, CONFIG_CHIP_APP_LOG_LEVEL);

#define NDEF_SUBTREE "farlock3"
#define NDEF_KEY NDEF_SUBTREE "/ndef"
#define NDEF_MAX_SIZE CONFIG_NDEF_FILE_SIZE

static uint8_t ndef_buffer[NDEF_MAX_SIZE];
static size_t ndef_len;

/* Called by the settings subsystem for every key under NDEF_SUBTREE during load. */
static int ndef_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (!settings_name_steq(key, "ndef", &next) || next) {
		return -ENOENT;
	}

	if (len > NDEF_MAX_SIZE) {
		LOG_ERR("Stored NDEF file too large (%u)", (unsigned int)len);
		return -EINVAL;
	}

	ssize_t bytes = read_cb(cb_arg, ndef_buffer, len);
	if (bytes < 0) {
		return (int)bytes;
	}

	ndef_len = (size_t)bytes;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(farlock3_ndef, NDEF_SUBTREE, NULL, ndef_settings_set, NULL, NULL);

int ndef_storage_init(void)
{
	int err = settings_subsys_init();

	if (err) {
		LOG_ERR("settings_subsys_init failed (%d)", err);
		return err;
	}

	err = settings_load_subtree(NDEF_SUBTREE);
	if (err) {
		LOG_ERR("settings_load_subtree failed (%d)", err);
		return err;
	}

	return 0;
}

int ndef_storage_write(const void *data, size_t data_size)
{
	if (data == NULL) {
		return -EINVAL;
	}

	if (data_size > NDEF_MAX_SIZE) {
		return -EINVAL;
	}

	memcpy(ndef_buffer, data, data_size);
	ndef_len = data_size;

	return settings_save_one(NDEF_KEY, ndef_buffer, ndef_len);
}

int ndef_storage_read(const void **data, size_t *data_size)
{
	if (data == NULL || data_size == NULL) {
		return -EINVAL;
	}

	*data = ndef_buffer;
	*data_size = ndef_len;

	return 0;
}

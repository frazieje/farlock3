/*
 * FARLock v3 NFC tag (writable Type 4 Tag, persisted NDEF file).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "nfc.h"
#include "ndef_storage.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include <nfc_t4t_lib.h>
#include <nfc/t4t/ndef_file.h>

LOG_MODULE_REGISTER(nfc_tag, CONFIG_CHIP_APP_LOG_LEVEL);

/*
 * Emulation buffer. The T4T library reads from and writes into it directly, so
 * it must live for the lifetime of the emulation. Layout: 2-byte NLEN (big
 * endian) followed by the NDEF message.
 */
static uint8_t ndef_file_buf[CONFIG_NDEF_FILE_SIZE];

/* Snapshot of a freshly written file, waiting to be persisted. */
static uint8_t pending_buf[CONFIG_NDEF_FILE_SIZE];
static size_t pending_len;
static atomic_t pending;
static struct k_work persist_work;

static size_t file_ndef_len(const uint8_t *file)
{
	return sys_get_be16(file);
}

static void persist_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	int err = ndef_storage_write(pending_buf, pending_len);

	if (err) {
		LOG_ERR("Failed to persist NDEF file (%d)", err);
	} else {
		LOG_INF("Persisted NDEF file (%u byte message)", (unsigned int)file_ndef_len(pending_buf));
		LOG_HEXDUMP_DBG(pending_buf, pending_len, "NDEF file");
	}

	atomic_set(&pending, 0);
}

static void nfc_callback(void *context, nfc_t4t_event_t event, const uint8_t *data, size_t data_length,
			 uint32_t flags)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(flags);

	switch (event) {
	case NFC_T4T_EVENT_FIELD_ON:
		LOG_DBG("NFC field on");
		break;
	case NFC_T4T_EVENT_FIELD_OFF:
		LOG_DBG("NFC field off");
		break;
	case NFC_T4T_EVENT_NDEF_READ:
		LOG_INF("NDEF message read by a reader");
		break;
	case NFC_T4T_EVENT_NDEF_UPDATED:
		/* data_length is the new NLEN; 0 means the reader is mid-update (NLEN cleared). */
		if (data_length == 0) {
			break;
		}
		if (data_length + NFC_NDEF_FILE_NLEN_FIELD_SIZE > sizeof(ndef_file_buf)) {
			LOG_ERR("NDEF update too large (%u)", (unsigned int)data_length);
			break;
		}
		if (!atomic_cas(&pending, 0, 1)) {
			LOG_WRN("Previous NDEF update still being persisted, discarding this one");
			break;
		}
		pending_len = data_length + NFC_NDEF_FILE_NLEN_FIELD_SIZE;
		memcpy(pending_buf, ndef_file_buf, pending_len);
		k_work_submit(&persist_work);
		LOG_INF("NDEF message updated by a writer (%u bytes)", (unsigned int)data_length);
		break;
	default:
		break;
	}
}

size_t nfc_tag_ndef_len(void)
{
	return file_ndef_len(ndef_file_buf);
}

int nfc_tag_init(void)
{
	int err;
	const void *stored = NULL;
	size_t stored_len = 0;

	k_work_init(&persist_work, persist_work_fn);

	err = ndef_storage_init();
	if (err) {
		LOG_ERR("NDEF storage init failed (%d)", err);
		return err;
	}

	(void)ndef_storage_read(&stored, &stored_len);

	if (stored_len >= NFC_NDEF_FILE_NLEN_FIELD_SIZE && stored_len <= sizeof(ndef_file_buf) &&
	    file_ndef_len(stored) + NFC_NDEF_FILE_NLEN_FIELD_SIZE <= stored_len) {
		memcpy(ndef_file_buf, stored, stored_len);
		LOG_INF("Restored NDEF file from storage (%u byte message)", (unsigned int)nfc_tag_ndef_len());
	} else {
		if (stored_len != 0) {
			LOG_WRN("Stored NDEF file is invalid (%u bytes), starting empty", (unsigned int)stored_len);
		}
		/* Empty, writable NDEF file: NLEN = 0 */
		memset(ndef_file_buf, 0, sizeof(ndef_file_buf));
		LOG_INF("NFC tag is empty, waiting to be written");
	}

	err = nfc_t4t_setup(nfc_callback, NULL);
	if (err) {
		LOG_ERR("nfc_t4t_setup failed (%d)", err);
		return err;
	}

	err = nfc_t4t_ndef_rwpayload_set(ndef_file_buf, sizeof(ndef_file_buf));
	if (err) {
		LOG_ERR("nfc_t4t_ndef_rwpayload_set failed (%d)", err);
		return err;
	}

	err = nfc_t4t_emulation_start();
	if (err) {
		LOG_ERR("nfc_t4t_emulation_start failed (%d)", err);
		return err;
	}

	LOG_INF("NFC Type 4 Tag emulation started");
	return 0;
}

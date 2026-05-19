#include "ndef_storage.h"
#include <stddef.h>
#include <zephyr/settings/settings.h>

#define NDEF_KEY "farlock3/ndef"
#define NDEF_MAX_SIZE 2048

static uint8_t ndef_buffer[NDEF_MAX_SIZE];
static size_t ndef_len;

/* Handler called by settings subsystem during load */
static int ndef_settings_set(const char *key, size_t len,
                              settings_read_cb read_cb, void *cb_arg)
{
    /* `key` is the part of the full key after our subtree prefix.
     * Since we register the handler for "farlock3" subtree and our key
     * is "farlock3/ndef", the key passed here will be just "ndef". */
    
    if (strcmp(key, "ndef") != 0) {
        return -ENOENT;
    }
    
    if (len > NDEF_MAX_SIZE) {
        return -EINVAL;
    }
    
    ssize_t bytes = read_cb(cb_arg, ndef_buffer, len);
    if (bytes < 0) {
        return bytes;
    }
    
    ndef_len = bytes;
    return 0;
}

/* Register the handler at file scope */
SETTINGS_STATIC_HANDLER_DEFINE(farlock3_ndef, "farlock3",
                                NULL,                /* get handler, not needed */
                                ndef_settings_set,   /* set handler, called on load */
                                NULL,                /* commit handler, not needed */
                                NULL);               /* export handler, not needed */

int ndef_storage_init() {
    int err;
    
    err = settings_subsys_init();
    if (err) {
        return err;
    }
    
    /* This loads all stored settings under "farlock3" and dispatches them
     * to our registered handler. */
    err = settings_load_subtree("farlock3");
    if (err) {
        return err;
    }
    
    /* After this returns, ndef_buffer and ndef_len contain the
     * previously-stored content (or zero/empty if nothing was stored yet). */
    return 0;
}

int ndef_storage_write(const void *data, size_t data_size) { 

    if (data == NULL) {
        return -1;
    }

    if (data_size > NDEF_MAX_SIZE) {
        return -EINVAL;
    }
    
    /* Copy into our buffer first */
    memcpy(ndef_buffer, data, data_size);
    ndef_len = data_size;
    
    /* Then persist */
    return settings_save_one(NDEF_KEY, ndef_buffer, ndef_len);
}

int ndef_storage_read(const void **data, size_t *size_t data_size) {

    if (data == NULL || len == NULL) {
        return -EINVAL;
    }

    *data = ndef_buffer;
    *len = ndef_len;

    return 0;
}

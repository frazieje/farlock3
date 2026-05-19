#include "ndef_storage.h"
#include "ndef_storage_c.h"

extern "C" {

void ndef_storage_init(void) {
    NDEFStorage::Instance().Init();
}

int ndef_storage_read(void *data, size_t data_size, size_t *out_size) {
    size_t local = 0;
    bool ok = NDEFStorage::Instance().Load(data, data_size, local);
    if (out_size) *out_size = local;  
    if (ok) {
        return 0;
    } else {
	return -1;
    }
}

int ndef_storage_write(const void *data, size_t data_size) {
    return NDEFStorage::Instance().Store(data, data_size);
}

}

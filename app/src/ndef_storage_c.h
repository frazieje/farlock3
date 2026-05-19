#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void ndef_storage_init(void);
int  ndef_storage_read(void *data, size_t data_size, size_t *out_size);
int  ndef_storage_write(const void* data, size_t data_size);

#ifdef __cplusplus
}
#endif

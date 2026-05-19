#include "nfc.h"

char key[] = "dt/ndef/tag";

int start_nfc() {

    

    char test[] = "Hello NDEF Record";

    int ret = ndef_storage_write(test, strlen(test));
    return ret;
}

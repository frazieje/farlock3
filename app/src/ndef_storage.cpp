#include "ndef_storage.h"
#include <persistent_storage/persistent_storage.h>

namespace {

/* Prefer to use CONFIG_NCS_SAMPLE_MATTER_SECURE_STORAGE_BACKEND if both backends are set simultaneously */
#ifdef CONFIG_NCS_SAMPLE_MATTER_SECURE_STORAGE_BACKEND
#define PSInit SecureInit
#define PSStore SecureStore
#define PSLoad SecureLoad
#elif defined(CONFIG_NCS_SAMPLE_MATTER_SETTINGS_STORAGE_BACKEND)
#define PSInit NonSecureInit
#define PSStore NonSecureStore
#define PSLoad NonSecureLoad
#endif

    constexpr auto kDataPrefix = "dt";

    char keyName[] = "dt/ndef/tag";
}

    bool NDEFStorage::Init() {
        static Nrf::PersistentStorageNode rootNode{ kDataPrefix, strlen(kDataPrefix) };

	return Nrf::PSErrorCode::Success == Nrf::GetPersistentStorage().PSInit(&rootNode);
    }


    bool NDEFStorage::Store(const void *data, size_t dataSize) { 

	if (data == nullptr) {
	    return false;
	}

	Nrf::PersistentStorageNode node{ keyName, strlen(keyName) + 1 };
	bool ret = (Nrf::PSErrorCode::Success == Nrf::GetPersistentStorage().PSStore(&node, data, dataSize));

	return ret;
    }

    bool NDEFStorageStorage::Load(void *data, size_t dataSize, size_t &outSize) {

	if (data == nullptr) {
	    return false;
	}

	Nrf::PersistentStorageNode node{ keyName, strlen(keyName) + 1 };
	Nrf::PSErrorCode result = Nrf::GetPersistentStorage().PSLoad(&node, data, dataSize, outSize);

	return (Nrf::PSErrorCode::Success == result);
    }

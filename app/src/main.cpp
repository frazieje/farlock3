/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

extern "C" {
#include "usb.h"
}

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);

int main()
{
	int ret = start_usb();
	if (ret) {
	    LOG_INF("USB host not detected, code %d", ret);
	}
	
//	ret = start_nfc();
//	if (ret) {
//	    LOG_INF("NFC failed to start, code %d", ret);
//	}

	CHIP_ERROR err = AppTask::Instance().StartApp();

	LOG_ERR("Exited with code %" CHIP_ERROR_FORMAT, err.Format());
	return err == CHIP_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE;
}

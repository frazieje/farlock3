/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"
#include "bolt_lock_manager.h"
#include "clusters/identify.h"
#include "indication.h"
#include "nfc.h"

#include "app/matter_init.h"
#include "app/task_executor.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/door-lock-server/door-lock-server.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <dk_buttons_and_leds.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;

namespace
{
constexpr EndpointId kLockEndpointId = 1;

/*
 * A second push button toggles the bolt for bench testing on development kits.
 * The real board only has the program button, which the Nordic board layer
 * uses for commissioning and factory reset.
 */
#if DT_NODE_EXISTS(DT_NODELABEL(button1))
#define APPLICATION_BUTTON_MASK DK_BTN2_MSK
#endif

#ifndef CONFIG_CHIP_FACTORY_RESET_ERASE_SETTINGS
void AppFactoryResetHandler(const ChipDeviceEvent *event, intptr_t /* unused */)
{
	switch (event->Type) {
	case DeviceEventType::kFactoryReset:
		BoltLockMgr().FactoryReset();
		break;
	default:
		break;
	}
}
#endif

Nrf::Matter::IdentifyCluster sIdentifyCluster(kLockEndpointId, false, []() {
	Nrf::PostTask([] { Indication::IdentifyDone(); });
});
} /* namespace */

void AppTask::ButtonEventHandler(Nrf::ButtonState state, Nrf::ButtonMask hasChanged)
{
	/*
	 * Program button. The board layer owns its functions (advertising, DFU,
	 * factory reset); this only adds the on-demand connectivity flash for a
	 * short press on a commissioned lock. Longer holds are the factory-reset
	 * sequence and already blink every LED.
	 */
	static int64_t pressedAt;

	if (hasChanged & DK_BTN1_MSK) {
		if (state & DK_BTN1_MSK) {
			pressedAt = k_uptime_get();
		} else if (k_uptime_get() - pressedAt < Nrf::FactoryResetConsts::kFactoryResetTriggerTimeout) {
			Nrf::PostTask([] {
				if (chip::DeviceLayer::ConnectivityMgr().IsThreadProvisioned()) {
					Indication::ButtonTap(chip::DeviceLayer::ConnectivityMgr().IsThreadAttached());
				}
			});
		}
	}

#ifdef APPLICATION_BUTTON_MASK
	if ((APPLICATION_BUTTON_MASK & hasChanged) & state) {
		Nrf::PostTask([] { LockActionEventHandler(); });
	}
#endif
}

void AppTask::LockActionEventHandler()
{
	if (BoltLockMgr().IsLocked()) {
		BoltLockMgr().Unlock(BoltLockManager::OperationSource::kButton);
	} else {
		BoltLockMgr().Lock(BoltLockManager::OperationSource::kButton);
	}
}

void AppTask::LockStateChanged(const BoltLockManager::StateData &stateData)
{
	switch (stateData.mState) {
	case BoltLockManager::State::kLockingInitiated:
		LOG_INF("Lock action initiated");
		Indication::LockOperationStarted(true);
		break;
	case BoltLockManager::State::kLockingCompleted:
		LOG_INF("Lock action completed");
		Indication::LockOperationCompleted(true);
		break;
	case BoltLockManager::State::kUnlockingInitiated:
		LOG_INF("Unlock action initiated");
		Indication::LockOperationStarted(false);
		break;
	case BoltLockManager::State::kUnlockingCompleted:
		LOG_INF("Unlock action completed");
		Indication::LockOperationCompleted(false);
		break;
	}

	/* Handle changing attribute state in the application */
	Instance().UpdateClusterState(stateData);
}

void AppTask::UpdateClusterState(const BoltLockManager::StateData &stateData)
{
	BoltLockManager::StateData *stateDataCopy = Platform::New<BoltLockManager::StateData>(stateData);

	if (stateDataCopy == nullptr) {
		LOG_ERR("Failed to allocate memory for BoltLockManager::StateData");
		return;
	}

	CHIP_ERROR err = SystemLayer().ScheduleLambda([stateDataCopy]() {
		UpdateClusterStateHandler(*stateDataCopy);
		Platform::Delete(stateDataCopy);
	});

	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Failed to schedule lambda: %" CHIP_ERROR_FORMAT, err.Format());
		Platform::Delete(stateDataCopy);
	}
}

void AppTask::UpdateClusterStateHandler(const BoltLockManager::StateData &stateData)
{
	using namespace chip::app::Clusters::DoorLock::Attributes;

	DlLockState newLockState;

	switch (stateData.mState) {
	case BoltLockManager::State::kLockingCompleted:
		newLockState = DlLockState::kLocked;
		break;
	case BoltLockManager::State::kUnlockingCompleted:
		newLockState = DlLockState::kUnlocked;
		break;
	default:
		newLockState = DlLockState::kNotFullyLocked;
		break;
	}

	Nullable<DlLockState> currentLockState;
	LockState::Get(kLockEndpointId, currentLockState);

	if (currentLockState.IsNull()) {
		/* Initialize lock state with start value, but not invoke lock/unlock. */
		LockState::Set(kLockEndpointId, newLockState);
	} else {
		LOG_INF("Updating LockState attribute");

		Nullable<uint16_t> userId;
		Nullable<List<const LockOpCredentials>> credentials;
#ifdef CONFIG_LOCK_PASS_CREDENTIALS_TO_SET_LOCK_STATE
		List<const LockOpCredentials> credentialList;
#endif

		if (!stateData.mValidatePINResult.IsNull()) {
			userId = { stateData.mValidatePINResult.Value().mUserId };

#ifdef CONFIG_LOCK_PASS_CREDENTIALS_TO_SET_LOCK_STATE
			/* `DoorLockServer::SetLockState` exptects list of `LockOpCredentials`,
			   however in case of PIN validation it makes no sense to have more than one
			   credential corresponding to validation result. For simplicity we wrap single
			   credential in list here. */
			credentialList = { &stateData.mValidatePINResult.Value().mCredential, 1 };
			credentials = { credentialList };
#endif
		}

		if (!DoorLockServer::Instance().SetLockState(kLockEndpointId, newLockState, stateData.mSource, userId,
							     credentials, stateData.mFabricIdx, stateData.mNodeId)) {
			LOG_ERR("Failed to update LockState attribute");
		}
	}
}

void AppTask::ReportLockJammed()
{
	Indication::LockJammed();

	CHIP_ERROR err = SystemLayer().ScheduleLambda([]() {
		if (!DoorLockServer::Instance().SendLockAlarmEvent(kLockEndpointId, AlarmCodeEnum::kLockJammed)) {
			LOG_ERR("Failed to send LockJammed alarm event");
		}
	});

	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Failed to schedule LockJammed alarm: %" CHIP_ERROR_FORMAT, err.Format());
	}
}

#ifdef CONFIG_NCS_SAMPLE_MATTER_TEST_EVENT_TRIGGERS
CHIP_ERROR AppTask::DoorLockJammedEventCallback(Nrf::Matter::TestEventTrigger::TriggerValue)
{
	VerifyOrReturnError(DoorLockServer::Instance().SendLockAlarmEvent(kLockEndpointId, AlarmCodeEnum::kLockJammed),
			    CHIP_ERROR_INTERNAL);
	LOG_ERR("Event Trigger: Doorlock jammed.");
	return CHIP_NO_ERROR;
}
#endif

CHIP_ERROR AppTask::Init()
{
	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());

	if (!Nrf::GetBoard().Init(ButtonEventHandler, Indication::StatusLedHandler)) {
		LOG_ERR("User interface initialization failed.");
		return CHIP_ERROR_INCORRECT_STATE;
	}

	/* Register Matter event handler that tracks the network state for the connection LED (see indication.h). */
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));

#ifndef CONFIG_CHIP_FACTORY_RESET_ERASE_SETTINGS
	/* Register factory reset event handler.
	 * With this configuration we have to manually clean up the storage,
	 * as whole settings partition won't be erased.
	 * */
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(AppFactoryResetHandler, 0));
#endif

	/* Initialize lock manager (motor, switches, credentials) */
	BoltLockMgr().Init(LockStateChanged);

	/* NFC tag. The settings subsystem is up at this point (PrepareServer initialised it). */
	int nfcErr = nfc_tag_init();
	if (nfcErr) {
		LOG_ERR("NFC tag initialisation failed (%d), continuing without NFC", nfcErr);
	}

	/* Register Door Lock test event trigger */
#ifdef CONFIG_NCS_SAMPLE_MATTER_TEST_EVENT_TRIGGERS
	ReturnErrorOnFailure(Nrf::Matter::TestEventTrigger::Instance().RegisterTestEventTrigger(
		kDoorLockJammedEventTriggerId,
		Nrf::Matter::TestEventTrigger::EventTrigger{ 0, DoorLockJammedEventCallback }));
#endif

	ReturnErrorOnFailure(sIdentifyCluster.Init());

	return Nrf::Matter::StartServer();
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}

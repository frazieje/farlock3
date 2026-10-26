/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "bolt_lock_manager.h"
#include "app/task_executor.h"

#include "app_task.h"

#ifndef CONFIG_FARLOCK3_LOCK_SIMULATED
#include "lock_hw.h"
#endif

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip;

namespace
{
#ifdef CONFIG_FARLOCK3_LOCK_SIMULATED
/* Simulated bolt: the "motor" simply takes this long. */
constexpr uint32_t kActuatorTimeoutMs = 2000;
#else
/* Real bolt: give up and report a jam if the position switch has not changed by then. */
constexpr uint32_t kActuatorTimeoutMs = CONFIG_FARLOCK3_MOTOR_TIMEOUT_MS;
#endif
} /* namespace */

BoltLockManager BoltLockManager::sLock;

void BoltLockManager::Init(StateChangeCallback callback)
{
	mStateChangeCallback = callback;

	k_timer_init(&mActuatorTimer, &BoltLockManager::ActuatorTimerEventHandler, nullptr);
	k_timer_user_data_set(&mActuatorTimer, this);

	AccessMgr::Instance().Init();

#ifndef CONFIG_FARLOCK3_LOCK_SIMULATED
	int err = lock_hw_init(PositionChangedHandler);
	if (err) {
		LOG_ERR("Lock hardware initialisation failed (%d); bolt state is unknown", err);
	} else {
		/* Start from the real position of the bolt rather than assuming "locked". */
		mStateData.mState = lock_hw_is_locked() ? State::kLockingCompleted : State::kUnlockingCompleted;
		mStateData.mSource = OperationSource::kManual;
	}
#endif

	/* Set the default state */
	Nrf::GetBoard().GetLED(Nrf::DeviceLeds::LED2).Set(IsLocked());
}

bool BoltLockManager::GetUser(uint16_t userIndex, EmberAfPluginDoorLockUserInfo &user)
{
	return AccessMgr::Instance().GetUserInfo(userIndex, user);
}

bool BoltLockManager::SetUser(uint16_t userIndex, FabricIndex creator, FabricIndex modifier, const CharSpan &userName,
			      uint32_t uniqueId, UserStatusEnum userStatus, UserTypeEnum userType,
			      CredentialRuleEnum credentialRule, const CredentialStruct *credentials,
			      size_t totalCredentials)
{
	return AccessMgr::Instance().SetUser(userIndex, creator, modifier, userName, uniqueId, userStatus, userType,
					     credentialRule, credentials, totalCredentials);
}

bool BoltLockManager::GetCredential(uint16_t credentialIndex, CredentialTypeEnum credentialType,
				    EmberAfPluginDoorLockCredentialInfo &credential)
{
	return AccessMgr::Instance().GetCredential(credentialIndex, credentialType, credential);
}

bool BoltLockManager::SetCredential(uint16_t credentialIndex, FabricIndex creator, FabricIndex modifier,
				    DlCredentialStatus credentialStatus, CredentialTypeEnum credentialType,
				    const ByteSpan &secret)
{
	return AccessMgr::Instance().SetCredential(credentialIndex, creator, modifier, credentialStatus, credentialType,
						   secret);
}

#ifdef CONFIG_LOCK_SCHEDULES

DlStatus BoltLockManager::GetWeekDaySchedule(uint8_t weekdayIndex, uint16_t userIndex,
					     EmberAfPluginDoorLockWeekDaySchedule &schedule)
{
	return AccessMgr::Instance().GetWeekDaySchedule(weekdayIndex, userIndex, schedule);
}

DlStatus BoltLockManager::SetWeekDaySchedule(uint8_t weekdayIndex, uint16_t userIndex, DlScheduleStatus status,
					     DaysMaskMap daysMask, uint8_t startHour, uint8_t startMinute,
					     uint8_t endHour, uint8_t endMinute)
{
	return AccessMgr::Instance().SetWeekDaySchedule(weekdayIndex, userIndex, status, daysMask, startHour,
							startMinute, endHour, endMinute);
}

DlStatus BoltLockManager::GetYearDaySchedule(uint8_t yearDayIndex, uint16_t userIndex,
					     EmberAfPluginDoorLockYearDaySchedule &schedule)
{
	return AccessMgr::Instance().GetYearDaySchedule(yearDayIndex, userIndex, schedule);
}

DlStatus BoltLockManager::SetYearDaySchedule(uint8_t yeardayIndex, uint16_t userIndex, DlScheduleStatus status,
					     uint32_t localStartTime, uint32_t localEndTime)
{
	return AccessMgr::Instance().SetYearDaySchedule(yeardayIndex, userIndex, status, localStartTime, localEndTime);
}

DlStatus BoltLockManager::GetHolidaySchedule(uint8_t holidayIndex, EmberAfPluginDoorLockHolidaySchedule &schedule)
{
	return AccessMgr::Instance().GetHolidaySchedule(holidayIndex, schedule);
}

DlStatus BoltLockManager::SetHolidaySchedule(uint8_t holidayIndex, DlScheduleStatus status, uint32_t localStartTime,
					     uint32_t localEndTime, OperatingModeEnum operatingMode)
{
	return AccessMgr::Instance().SetHolidaySchedule(holidayIndex, status, localStartTime, localEndTime,
							operatingMode);
}

#endif /* CONFIG_LOCK_SCHEDULES */

bool BoltLockManager::ValidatePIN(const Optional<chip::ByteSpan> &pinCode, OperationErrorEnum &err,
				  Nullable<ValidatePINResult> &result)
{
	return AccessMgr::Instance().ValidatePIN(pinCode, err, result);
}

void BoltLockManager::SetRequirePIN(bool require)
{
	return AccessMgr::Instance().SetRequirePIN(require);
}
bool BoltLockManager::GetRequirePIN()
{
	return AccessMgr::Instance().GetRequirePIN();
}

void BoltLockManager::Lock(const OperationSource source, const Nullable<chip::FabricIndex> &fabricIdx,
			   const Nullable<chip::NodeId> &nodeId, const Nullable<ValidatePINResult> &validatePINResult)
{
	VerifyOrReturn(mStateData.mState != State::kLockingCompleted);
	VerifyOrReturn(!IsActuating(), LOG_WRN("Lock requested while the bolt is moving, ignored"));

	StateData newStateData{ State::kLockingInitiated, source, fabricIdx, nodeId, validatePINResult };
	SetStateData(newStateData);

	StartActuator(true);
}

void BoltLockManager::Unlock(const OperationSource source, const Nullable<chip::FabricIndex> &fabricIdx,
			     const Nullable<chip::NodeId> &nodeId, const Nullable<ValidatePINResult> &validatePINResult)
{
	VerifyOrReturn(mStateData.mState != State::kUnlockingCompleted);
	VerifyOrReturn(!IsActuating(), LOG_WRN("Unlock requested while the bolt is moving, ignored"));

	StateData newStateData{ State::kUnlockingInitiated, source, fabricIdx, nodeId, validatePINResult };
	SetStateData(newStateData);

	StartActuator(false);
}

void BoltLockManager::StartActuator(bool lock)
{
#ifndef CONFIG_FARLOCK3_LOCK_SIMULATED
	enum lock_hw_direction dir = lock_hw_lock_direction();
	if (!lock) {
		dir = lock_hw_opposite(dir);
	}

	int err = lock_hw_motor_run(dir, CONFIG_FARLOCK3_MOTOR_DUTY_CYCLE);
	if (err) {
		LOG_ERR("Failed to start motor (%d)", err);
	}
#else
	ARG_UNUSED(lock);
#endif

	k_timer_start(&mActuatorTimer, K_MSEC(kActuatorTimeoutMs), K_NO_WAIT);
}

void BoltLockManager::ActuatorTimerEventHandler(k_timer *timer)
{
	/*
	 * The timer event handler is called in the context of the system clock ISR.
	 * Post an event to the application task queue to process the event in the
	 * context of the application thread.
	 */

	BoltLockManagerEvent event;
	event.manager = static_cast<BoltLockManager *>(k_timer_user_data_get(timer));
	Nrf::PostTask([event] { ActuatorAppEventHandler(event); });
}

void BoltLockManager::ActuatorAppEventHandler(const BoltLockManagerEvent &event)
{
	BoltLockManager *lock = reinterpret_cast<BoltLockManager *>(event.manager);
	lock->HandleActuatorTimeout();
}

void BoltLockManager::HandleActuatorTimeout()
{
#ifdef CONFIG_FARLOCK3_LOCK_SIMULATED
	switch (mStateData.mState) {
	case State::kLockingInitiated:
		SetState(State::kLockingCompleted);
		break;
	case State::kUnlockingInitiated:
		SetState(State::kUnlockingCompleted);
		break;
	default:
		break;
	}
#else
	(void)lock_hw_motor_stop();

	if (!IsActuating()) {
		return;
	}

	const bool wantedLocked = (mStateData.mState == State::kLockingInitiated);
	const bool isLocked = lock_hw_is_locked();

	if (isLocked == wantedLocked) {
		/* The switch edge was missed but the bolt is where we wanted it. */
		LOG_WRN("Actuator timeout, but the bolt reached the %s position", isLocked ? "locked" : "unlocked");
		SetState(isLocked ? State::kLockingCompleted : State::kUnlockingCompleted);
	} else {
		LOG_ERR("Bolt jammed: motor ran for %u ms without reaching the %s position", kActuatorTimeoutMs,
			wantedLocked ? "locked" : "unlocked");
		SetState(isLocked ? State::kLockingCompleted : State::kUnlockingCompleted);
		AppTask::Instance().ReportLockJammed();
	}
#endif
}

void BoltLockManager::PositionChangedHandler(bool /* positionActive */)
{
	/* Runs on the system work queue; move to the application thread. */
	Nrf::PostTask([] { sLock.HandlePositionChanged(); });
}

void BoltLockManager::HandlePositionChanged()
{
#ifndef CONFIG_FARLOCK3_LOCK_SIMULATED
	const bool isLocked = lock_hw_is_locked();

	if (IsActuating()) {
		/* The bolt reached the other end position: stop immediately (the debounce already added a delay). */
		k_timer_stop(&mActuatorTimer);
		(void)lock_hw_motor_stop();

		const bool wantedLocked = (mStateData.mState == State::kLockingInitiated);
		if (isLocked != wantedLocked) {
			LOG_WRN("Bolt stopped in the %s position while trying to %s", isLocked ? "locked" : "unlocked",
				wantedLocked ? "lock" : "unlock");
		}
		SetState(isLocked ? State::kLockingCompleted : State::kUnlockingCompleted);
		return;
	}

	/* Not actuating: someone turned the thumb-turn or key by hand. */
	const State newState = isLocked ? State::kLockingCompleted : State::kUnlockingCompleted;
	if (newState != mStateData.mState) {
		LOG_INF("Bolt moved manually to the %s position", isLocked ? "locked" : "unlocked");
		SetStateData(StateData{ newState, OperationSource::kManual, {}, {}, {} });
	}
#endif
}

void BoltLockManager::SetState(State state)
{
	mStateData.mState = state;

	if (mStateChangeCallback != nullptr) {
		mStateChangeCallback(mStateData);
	}
}

void BoltLockManager::SetStateData(const StateData &stateData)
{
	mStateData = stateData;

	if (mStateChangeCallback != nullptr) {
		mStateChangeCallback(mStateData);
	}
}

void BoltLockManager::FactoryReset()
{
	AccessMgr::Instance().FactoryReset();
}

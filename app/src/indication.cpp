/*
 * FARLock v3 LED indication. See indication.h for the language.
 *
 * All requests are queued to one delayable work item, so they may come from
 * any thread or from an interrupt (the NFC callback). A new request replaces
 * the running sequence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "indication.h"

#include "board/board.h"

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

namespace
{
enum LedMask : uint8_t {
	kLedConnection = BIT(0), /* LED1, J3 */
	kLedGreen = BIT(1), /* LED2, J5 access granted */
	kLedRed = BIT(2), /* LED3, J5 access denied */
};

constexpr Nrf::DeviceLeds kLeds[] = { Nrf::DeviceLeds::LED1, Nrf::DeviceLeds::LED2, Nrf::DeviceLeds::LED3 };

struct Step {
	uint8_t leds;
	uint16_t onMs;
	uint16_t offMs;
};

struct Sequence {
	const Step *steps;
	uint8_t stepCount;
	uint8_t repeat;
};

constexpr Step kLockedSteps[] = { { kLedGreen, 1000, 0 } };
constexpr Step kUnlockedSteps[] = { { kLedRed, 1000, 0 } };
constexpr Step kJamSteps[] = { { kLedRed, 80, 0 }, { kLedGreen, 80, 0 } };
constexpr Step kNfcSteps[] = { { kLedGreen, 100, 0 } };
constexpr Step kConnectionFlash[] = { { kLedConnection, 100, 100 } };

constexpr Sequence kLocked = { kLockedSteps, 1, 1 };
constexpr Sequence kUnlocked = { kUnlockedSteps, 1, 1 };
constexpr Sequence kJam = { kJamSteps, 2, 5 };
constexpr Sequence kNfc = { kNfcSteps, 1, 1 };
constexpr Sequence kJoined = { kConnectionFlash, 1, 3 };
constexpr Sequence kTapAttached = { kConnectionFlash, 1, 1 };
constexpr Sequence kTapDetached = { kConnectionFlash, 1, 3 };

/* What the work item should do next. Written under sLock, consumed by Run(). */
enum class Request : uint8_t { None, Sequence, Blink, Off };

struct {
	Request request;
	const Sequence *sequence;
	uint8_t blinkLeds;
	uint16_t blinkOnMs;
	uint16_t blinkOffMs;
} sPending;

/* Sequencer state, only touched from the work item. */
struct {
	const Sequence *sequence;
	uint8_t step;
	uint8_t repeat;
	bool lit;
} sRun;

k_spinlock sLock;
k_work_delayable sWork;

void SetLeds(uint8_t mask, bool on)
{
	for (size_t i = 0; i < ARRAY_SIZE(kLeds); i++) {
		if (mask & BIT(i)) {
			Nrf::GetBoard().GetLED(kLeds[i]).Set(on);
		}
	}
}

void Run(k_work *work)
{
	ARG_UNUSED(work);

	Request request;
	const Sequence *sequence;
	uint8_t blinkLeds;
	uint16_t blinkOnMs, blinkOffMs;

	{
		k_spinlock_key_t key = k_spin_lock(&sLock);
		request = sPending.request;
		sequence = sPending.sequence;
		blinkLeds = sPending.blinkLeds;
		blinkOnMs = sPending.blinkOnMs;
		blinkOffMs = sPending.blinkOffMs;
		sPending.request = Request::None;
		k_spin_unlock(&sLock, key);
	}

	switch (request) {
	case Request::Off:
		SetLeds(kLedGreen | kLedRed, false);
		sRun.sequence = nullptr;
		return;
	case Request::Blink:
		/* Continuous blink driven by the LED widget's own timer, ended by the next request. */
		SetLeds(kLedGreen | kLedRed, false);
		sRun.sequence = nullptr;
		for (size_t i = 0; i < ARRAY_SIZE(kLeds); i++) {
			if (blinkLeds & BIT(i)) {
				Nrf::GetBoard().GetLED(kLeds[i]).Blink(blinkOnMs, blinkOffMs);
			}
		}
		return;
	case Request::Sequence:
		if (sRun.sequence) {
			SetLeds(sRun.sequence->steps[sRun.step].leds, false);
		}
		SetLeds(kLedGreen | kLedRed, false);
		sRun = { sequence, 0, 0, false };
		break;
	case Request::None:
		break;
	}

	if (!sRun.sequence) {
		return;
	}

	const Step &step = sRun.sequence->steps[sRun.step];

	if (!sRun.lit) {
		SetLeds(step.leds, true);
		sRun.lit = true;
		k_work_schedule(&sWork, K_MSEC(step.onMs));
		return;
	}

	SetLeds(step.leds, false);
	sRun.lit = false;

	if (++sRun.step >= sRun.sequence->stepCount) {
		sRun.step = 0;
		if (++sRun.repeat >= sRun.sequence->repeat) {
			sRun.sequence = nullptr;
			return;
		}
	}
	k_work_schedule(&sWork, K_MSEC(step.offMs));
}

void Submit(Request request, const Sequence *sequence = nullptr, uint8_t leds = 0, uint16_t onMs = 0,
	    uint16_t offMs = 0)
{
	static bool initialised;

	k_spinlock_key_t key = k_spin_lock(&sLock);
	if (!initialised) {
		k_work_init_delayable(&sWork, Run);
		initialised = true;
	}
	sPending = { request, sequence, leds, onMs, offMs };
	k_spin_unlock(&sLock, key);

	k_work_reschedule(&sWork, K_NO_WAIT);
}
} /* namespace */

namespace Indication
{
void StatusLedHandler()
{
	Nrf::LEDWidget &led = Nrf::GetBoard().GetLED(Nrf::DeviceLeds::LED1);

	switch (Nrf::GetBoard().GetDeviceState()) {
	case Nrf::DeviceState::DeviceAdvertisingBLE:
		led.Blink(Nrf::LedConsts::StatusLed::Disconnected::kOn_ms,
			  Nrf::LedConsts::StatusLed::Disconnected::kOff_ms);
		break;
	case Nrf::DeviceState::DeviceConnectedBLE:
		led.Blink(Nrf::LedConsts::StatusLed::BleConnected::kOn_ms,
			  Nrf::LedConsts::StatusLed::BleConnected::kOff_ms);
		break;
	case Nrf::DeviceState::DeviceProvisioned:
		led.Set(false);
		Submit(Request::Sequence, &kJoined);
		break;
	case Nrf::DeviceState::DeviceDisconnected:
	default:
		led.Set(false);
		break;
	}
}

void LockOperationStarted(bool locking)
{
	Submit(Request::Blink, nullptr, locking ? kLedGreen : kLedRed, 50, 50);
}

void LockOperationCompleted(bool locked)
{
	Submit(Request::Sequence, locked ? &kLocked : &kUnlocked);
}

void LockJammed()
{
	Submit(Request::Sequence, &kJam);
}

void ButtonTap(bool threadAttached)
{
	Submit(Request::Sequence, threadAttached ? &kTapAttached : &kTapDetached);
}

void IdentifyDone()
{
	Submit(Request::Off);
}
} /* namespace Indication */

extern "C" void indication_nfc_tap(void)
{
	Submit(Request::Sequence, &kNfc);
}

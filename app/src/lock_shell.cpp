/*
 * Bench shell commands for the FARLock v3 bolt (debug builds only).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bolt_lock_manager.h"

#include "app/task_executor.h"

#include <zephyr/shell/shell.h>

#ifdef CONFIG_RETENTION_BOOT_MODE
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/reboot.h>
#endif

#ifndef CONFIG_FARLOCK3_LOCK_SIMULATED
#include "lock_hw.h"
#endif

namespace
{
const char *StateName(BoltLockManager::State state)
{
	switch (state) {
	case BoltLockManager::State::kLockingInitiated:
		return "locking";
	case BoltLockManager::State::kLockingCompleted:
		return "locked";
	case BoltLockManager::State::kUnlockingInitiated:
		return "unlocking";
	case BoltLockManager::State::kUnlockingCompleted:
		return "unlocked";
	}
	return "?";
}

int CmdLock(const struct shell *sh, size_t, char **)
{
	shell_print(sh, "Locking (source: button)");
	Nrf::PostTask([] { BoltLockMgr().Lock(BoltLockManager::OperationSource::kButton); });
	return 0;
}

int CmdUnlock(const struct shell *sh, size_t, char **)
{
	shell_print(sh, "Unlocking (source: button)");
	Nrf::PostTask([] { BoltLockMgr().Unlock(BoltLockManager::OperationSource::kButton); });
	return 0;
}

int CmdToggle(const struct shell *sh, size_t, char **)
{
	shell_print(sh, "Toggling");
	Nrf::PostTask([] {
		if (BoltLockMgr().IsLocked()) {
			BoltLockMgr().Unlock(BoltLockManager::OperationSource::kButton);
		} else {
			BoltLockMgr().Lock(BoltLockManager::OperationSource::kButton);
		}
	});
	return 0;
}

int CmdStatus(const struct shell *sh, size_t, char **)
{
	const auto &data = BoltLockMgr().GetState();

	shell_print(sh, "state: %s (source %u)", StateName(data.mState), static_cast<unsigned>(data.mSource));
#ifndef CONFIG_FARLOCK3_LOCK_SIMULATED
	shell_print(sh, "position switch: %s, lock direction: %s, bolt: %s, motor: %s",
		    lock_hw_position_active() ? "active" : "inactive",
		    lock_hw_lock_direction() == LOCK_HW_DIR_RIGHT ? "right" : "left",
		    lock_hw_is_locked() ? "locked" : "unlocked", lock_hw_motor_running() ? "running" : "stopped");
#else
	shell_print(sh, "simulated bolt (CONFIG_FARLOCK3_LOCK_SIMULATED)");
#endif
	return 0;
}

#ifdef CONFIG_RETENTION_BOOT_MODE
int CmdDfu(const struct shell *sh, size_t, char **)
{
	int err = bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);

	if (err) {
		shell_error(sh, "bootmode_set failed: %d", err);
		return err;
	}
	shell_print(sh, "Rebooting into MCUboot USB serial recovery (this console goes away)");
	k_sleep(K_MSEC(100));
	sys_reboot(SYS_REBOOT_WARM);
	return 0;
}
#endif
} /* namespace */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_farlock,
			       SHELL_CMD(lock, NULL, "Drive the bolt to the locked position", CmdLock),
			       SHELL_CMD(unlock, NULL, "Drive the bolt to the unlocked position", CmdUnlock),
			       SHELL_CMD(toggle, NULL, "Lock if unlocked, unlock if locked", CmdToggle),
			       SHELL_CMD(status, NULL, "Show bolt, switch and motor state", CmdStatus),
#ifdef CONFIG_RETENTION_BOOT_MODE
			       SHELL_CMD(dfu, NULL, "Reboot into MCUboot USB serial recovery", CmdDfu),
#endif
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(farlock, &sub_farlock, "FARLock bolt bench commands", NULL);

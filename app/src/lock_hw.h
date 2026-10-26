/*
 * FARLock v3 bolt hardware: TB6612FNG motor driver and the position and
 * handedness switches.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FARLOCK3_LOCK_HW_H
#define FARLOCK3_LOCK_HW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum lock_hw_direction {
	LOCK_HW_DIR_LEFT = 0,
	LOCK_HW_DIR_RIGHT = 1,
};

/**
 * Called from the system work queue, after debouncing, whenever the bolt
 * position switch changes state.
 *
 * @param position_active true when the position switch is closed.
 */
typedef void (*lock_hw_position_cb_t)(bool position_active);

/** Configure the motor outputs (off) and switch inputs, arm the position interrupt. */
int lock_hw_init(lock_hw_position_cb_t position_cb);

/** Raw state of the bolt position switch. */
bool lock_hw_position_active(void);

/** Direction the motor must turn to lock, as selected by the handedness switch. */
enum lock_hw_direction lock_hw_lock_direction(void);

/** Opposite of @p dir. */
enum lock_hw_direction lock_hw_opposite(enum lock_hw_direction dir);

/** Derived bolt state: locked when the position switch agrees with the handedness switch. */
bool lock_hw_is_locked(void);

/** Enable the driver and run the motor at @p duty_percent (1..100) in @p dir. */
int lock_hw_motor_run(enum lock_hw_direction dir, uint8_t duty_percent);

/** Stop the motor and put the driver into standby. Safe to call when already stopped. */
int lock_hw_motor_stop(void);

bool lock_hw_motor_running(void);

#ifdef __cplusplus
}
#endif

#endif /* FARLOCK3_LOCK_HW_H */

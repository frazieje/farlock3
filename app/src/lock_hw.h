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

enum lock_hw_switch {
	LOCK_HW_SW_POSITION, /* bolt position, follows the thumb-turn and the motor */
	LOCK_HW_SW_DIRECTION, /* handedness, set once by the installer but may be flipped any time */
};

/**
 * Called from the system work queue, after debouncing, whenever one of the
 * switches changes state.
 *
 * @param sw which switch changed.
 * @param active true when that switch now reads active.
 */
typedef void (*lock_hw_switch_cb_t)(enum lock_hw_switch sw, bool active);

/** Configure the motor outputs (off) and switch inputs, arm both switch interrupts. */
int lock_hw_init(lock_hw_switch_cb_t switch_cb);

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

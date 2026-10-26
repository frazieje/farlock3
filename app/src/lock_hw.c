/*
 * FARLock v3 bolt hardware: TB6612FNG motor driver and the position and
 * handedness switches.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lock_hw.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lock_hw, CONFIG_CHIP_APP_LOG_LEVEL);

#define MOTOR_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(frazieje_tb6612_motor)
#define SWITCHES_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(frazieje_lock_switches)

BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(frazieje_tb6612_motor),
	     "No enabled frazieje,tb6612-motor node. Add one to the board or set CONFIG_FARLOCK3_LOCK_SIMULATED.");
BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(frazieje_lock_switches),
	     "No enabled frazieje,lock-switches node. Add one to the board or set CONFIG_FARLOCK3_LOCK_SIMULATED.");

static const struct pwm_dt_spec motor_pwm = PWM_DT_SPEC_GET(MOTOR_NODE);
static const struct gpio_dt_spec motor_in1 = GPIO_DT_SPEC_GET(MOTOR_NODE, in1_gpios);
static const struct gpio_dt_spec motor_in2 = GPIO_DT_SPEC_GET(MOTOR_NODE, in2_gpios);
static const struct gpio_dt_spec motor_stby = GPIO_DT_SPEC_GET(MOTOR_NODE, stby_gpios);

static const struct gpio_dt_spec sw_position = GPIO_DT_SPEC_GET(SWITCHES_NODE, position_gpios);
static const struct gpio_dt_spec sw_direction = GPIO_DT_SPEC_GET(SWITCHES_NODE, direction_gpios);

static struct gpio_callback position_cb_data;
static struct k_work_delayable position_work;
static lock_hw_position_cb_t position_cb;
static bool last_position;
static bool motor_running;

/*
 * The position switch is sensed with level interrupts rather than edge
 * interrupts. On nRF52 an edge interrupt occupies a GPIOTE channel in event
 * mode, which keeps a high-frequency clock request alive and costs tens of
 * microamps forever. A level interrupt uses the PORT/SENSE mechanism, which is
 * free. The price is that we must re-arm with the opposite level each time.
 */
static int arm_position_interrupt(bool currently_active)
{
	return gpio_pin_interrupt_configure_dt(&sw_position, currently_active ? GPIO_INT_LEVEL_INACTIVE :
										GPIO_INT_LEVEL_ACTIVE);
}

static void position_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	bool now = gpio_pin_get_dt(&sw_position) > 0;

	if (now != last_position) {
		last_position = now;
		LOG_INF("Position switch %s", now ? "active" : "inactive");
		if (position_cb) {
			position_cb(now);
		}
	}

	int err = arm_position_interrupt(now);
	if (err) {
		LOG_ERR("Failed to re-arm position interrupt (%d)", err);
	}
}

static void position_isr(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* Level interrupts fire continuously; mask until the debounce work has run. */
	(void)gpio_pin_interrupt_configure_dt(&sw_position, GPIO_INT_DISABLE);
	k_work_reschedule(&position_work, K_MSEC(CONFIG_FARLOCK3_SWITCH_DEBOUNCE_MS));
}

static int configure_output(const struct gpio_dt_spec *spec, const char *name)
{
	if (!gpio_is_ready_dt(spec)) {
		LOG_ERR("%s GPIO port not ready", name);
		return -ENODEV;
	}

	int err = gpio_pin_configure_dt(spec, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Failed to configure %s (%d)", name, err);
	}
	return err;
}

static int configure_input(const struct gpio_dt_spec *spec, const char *name)
{
	if (!gpio_is_ready_dt(spec)) {
		LOG_ERR("%s GPIO port not ready", name);
		return -ENODEV;
	}

	int err = gpio_pin_configure_dt(spec, GPIO_INPUT);
	if (err) {
		LOG_ERR("Failed to configure %s (%d)", name, err);
	}
	return err;
}

int lock_hw_init(lock_hw_position_cb_t cb)
{
	int err;

	if (!pwm_is_ready_dt(&motor_pwm)) {
		LOG_ERR("Motor PWM device not ready");
		return -ENODEV;
	}

	err = configure_output(&motor_stby, "motor STBY");
	err = err ? err : configure_output(&motor_in1, "motor IN1");
	err = err ? err : configure_output(&motor_in2, "motor IN2");
	err = err ? err : pwm_set_pulse_dt(&motor_pwm, 0);
	if (err) {
		return err;
	}

	err = configure_input(&sw_position, "position switch");
	err = err ? err : configure_input(&sw_direction, "direction switch");
	if (err) {
		return err;
	}

	position_cb = cb;
	last_position = gpio_pin_get_dt(&sw_position) > 0;

	k_work_init_delayable(&position_work, position_work_fn);
	gpio_init_callback(&position_cb_data, position_isr, BIT(sw_position.pin));
	err = gpio_add_callback(sw_position.port, &position_cb_data);
	if (err) {
		LOG_ERR("Failed to add position callback (%d)", err);
		return err;
	}

	err = arm_position_interrupt(last_position);
	if (err) {
		LOG_ERR("Failed to arm position interrupt (%d)", err);
		return err;
	}

	LOG_INF("Lock hardware ready: position %s, direction %s, bolt %s", last_position ? "active" : "inactive",
		lock_hw_lock_direction() == LOCK_HW_DIR_RIGHT ? "right" : "left",
		lock_hw_is_locked() ? "locked" : "unlocked");

	return 0;
}

bool lock_hw_position_active(void)
{
	return gpio_pin_get_dt(&sw_position) > 0;
}

enum lock_hw_direction lock_hw_lock_direction(void)
{
	return gpio_pin_get_dt(&sw_direction) > 0 ? LOCK_HW_DIR_RIGHT : LOCK_HW_DIR_LEFT;
}

enum lock_hw_direction lock_hw_opposite(enum lock_hw_direction dir)
{
	return dir == LOCK_HW_DIR_RIGHT ? LOCK_HW_DIR_LEFT : LOCK_HW_DIR_RIGHT;
}

bool lock_hw_is_locked(void)
{
	/* Same truth table as the v2 firmware (set_lock_state_from_switch_pos). */
	return lock_hw_position_active() == (lock_hw_lock_direction() == LOCK_HW_DIR_RIGHT);
}

int lock_hw_motor_run(enum lock_hw_direction dir, uint8_t duty_percent)
{
	int err;

	if (duty_percent == 0 || duty_percent > 100) {
		return -EINVAL;
	}

	err = gpio_pin_set_dt(&motor_stby, 1);
	if (err) {
		return err;
	}

	/* v2 convention: RIGHT = IN1 low / IN2 high, LEFT = IN1 high / IN2 low */
	err = gpio_pin_set_dt(&motor_in1, dir == LOCK_HW_DIR_LEFT);
	err = err ? err : gpio_pin_set_dt(&motor_in2, dir == LOCK_HW_DIR_RIGHT);
	if (err) {
		(void)lock_hw_motor_stop();
		return err;
	}

	uint32_t pulse = (uint32_t)(((uint64_t)motor_pwm.period * duty_percent) / 100U);

	err = pwm_set_pulse_dt(&motor_pwm, pulse);
	if (err) {
		LOG_ERR("Failed to set motor PWM (%d)", err);
		(void)lock_hw_motor_stop();
		return err;
	}

	motor_running = true;
	LOG_INF("Motor running %s at %u%%", dir == LOCK_HW_DIR_RIGHT ? "right" : "left", duty_percent);
	return 0;
}

int lock_hw_motor_stop(void)
{
	int err = pwm_set_pulse_dt(&motor_pwm, 0);

	(void)gpio_pin_set_dt(&motor_in1, 0);
	(void)gpio_pin_set_dt(&motor_in2, 0);
	(void)gpio_pin_set_dt(&motor_stby, 0);

	if (motor_running) {
		LOG_INF("Motor stopped");
	}
	motor_running = false;
	return err;
}

bool lock_hw_motor_running(void)
{
	return motor_running;
}

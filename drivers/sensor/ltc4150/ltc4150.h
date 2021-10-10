/*
 * Copyright (c) 2021 arithmetics.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_LTC4150_LTC4150_H_
#define ZEPHYR_DRIVERS_SENSOR_LTC4150_LTC4150_H_

#include <drivers/sensor.h>
#include <drivers/gpio.h>

struct ltc4150_data {
	int16_t charge_counter;

	struct gpio_callback gpio_cb;

	struct k_mutex trigger_mutex;
	sensor_trigger_handler_t drdy_handler;
	struct sensor_trigger drdy_trigger;
	const struct device *dev;

#ifdef CONFIG_LTC4150_TRIGGER_OWN_THREAD
	K_THREAD_STACK_MEMBER(thread_stack, CONFIG_LTC4150_THREAD_STACK_SIZE);
	struct k_sem gpio_sem;
	struct k_thread thread;
#elif CONFIG_LTC4150_TRIGGER_GLOBAL_THREAD
	struct k_work work;
#endif
};

struct ltc4150_config {
	const struct device *int_gpio;
	gpio_pin_t int_pin;
	gpio_dt_flags_t int_flags;

	const struct device *int_clr_gpio;
	gpio_pin_t int_clr_pin;
	gpio_dt_flags_t int_clr_flags;

	const struct device *pol_gpio;
	gpio_pin_t pol_pin;
	gpio_dt_flags_t pol_flags;

	const struct device *shdn_gpio;
	gpio_pin_t shdn_pin;
	gpio_dt_flags_t shdn_flags;

	uint8_t shunt_resistance;
	uint16_t design_capacity;
};

int ltc4150_init_interrupt(const struct device *dev);

int ltc4150_trigger_set(const struct device *dev,
			const struct sensor_trigger *trig,
			sensor_trigger_handler_t handler);

#endif  /* ZEPHYR_DRIVERS_SENSOR_LTC4150_LTC4150_H_ */

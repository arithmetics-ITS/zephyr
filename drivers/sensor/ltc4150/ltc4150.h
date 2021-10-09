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
	int16_t charge_count;
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
};

#endif  /* ZEPHYR_DRIVERS_SENSOR_LTC4150_LTC4150_H_ */

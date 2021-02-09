/*
 * Copyright (c) 2021 arithmetics ITS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <drivers/motor.h>

void main(void)
{
	const struct device *dev = device_get_binding(DT_LABEL(DT_INST(0, nxp_pca9629a)));

	if (dev == NULL) {
		printk("Could not get %s device\n", DT_LABEL(DT_INST(0, nxp_pca9629a)));
		return;
	}

	motor_control(dev, MOTOR_STOP);

	motor_set_direction(dev, MOTOR_CW);

	motor_set_speed(dev, MOTOR_CW, 1538);
	motor_set_speed(dev, MOTOR_CCW, 1538);

	motor_attr_set(dev, MOTOR_ATTR_CW_STEPS, 500);
	motor_attr_set(dev, MOTOR_ATTR_CCW_STEPS, 500);
	motor_attr_set(dev, MOTOR_ATTR_RAMP_DOWN, 0);
	motor_attr_set(dev, MOTOR_ATTR_RAMP_UP, 0);
	motor_attr_set(dev, MOTOR_ATTR_ROTATION_MODE, 1);

	motor_control(dev, MOTOR_START);

	while (1) {
		k_sleep(K_MSEC(1000));
	}
}

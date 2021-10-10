/*
 * Copyright (c) 2021 arithmetics.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <sys/util.h>
#include <drivers/sensor.h>
#include <stdio.h>

K_SEM_DEFINE(sem, 0, 1);

static void trigger_handler(const struct device *dev,
			    struct sensor_trigger *trigger)
{
	switch (trigger->type) {
	case SENSOR_TRIG_DATA_READY:
		k_sem_give(&sem);
		break;
	default:
		printk("Unknown trigger\n");
	}
}

#ifdef CONFIG_PM_DEVICE
static void pm_info(enum pm_device_state state, int status)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(arg);

	switch (state) {
	case PM_DEVICE_STATE_ACTIVE:
		printk("Enter ACTIVE_STATE ");
		break;
	case PM_DEVICE_STATE_OFF:
		printk("Enter OFF_STATE ");
		break;
	}

	if (status) {
		printk("Fail\n");
	} else {
		printk("Success\n");
	}
}
#endif

#define DEVICE_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(adi_ltc4150)

void main(void)
{
	struct sensor_value val;

	const struct device *dev = DEVICE_DT_GET(DEVICE_NODE);

	if (!device_is_ready(dev)) {
		printk("Device %s is not ready\n", dev->name);
		return;
	}

	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = SENSOR_CHAN_ALL,
	};

	if (sensor_trigger_set(dev, &trig, trigger_handler)) {
		printk("Could not set trigger\n");
		return;
	}

#ifdef CONFIG_PM_DEVICE
	/* Testing the power modes */
	enum pm_device_state p_state;
	int ret;

	p_state = PM_DEVICE_STATE_OFF;
	ret = pm_device_state_set(dev, p_state);
	pm_info(p_state, ret);

	p_state = PM_DEVICE_STATE_ACTIVE;
	ret = pm_device_state_set(dev, p_state);
	pm_info(p_state, ret);
#endif

	while (1) {
		k_sem_take(&sem, K_FOREVER);

		sensor_channel_get(dev, SENSOR_CHAN_GAUGE_COULOMB_COUNT, &val);
		printk("Coulomb count: %i\n", val.val1);

		sensor_channel_get(dev, SENSOR_CHAN_GAUGE_FULL_AVAIL_CAPACITY, &val);
		printk("Full capacity: %i mAh\n", val.val1);

		sensor_channel_get(dev, SENSOR_CHAN_GAUGE_REMAINING_CHARGE_CAPACITY, &val);
		printf("Remaning capacity: %f mAh\n", sensor_value_to_double(&val));

		sensor_channel_get(dev, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &val);
		printf("SoC: %f %%\n", sensor_value_to_double(&val));

		sensor_channel_get(dev, SENSOR_CHAN_GAUGE_ACCUMULATED_CAPACITY, &val);
		printf("Capacity consumed: %f mAh\n\n", sensor_value_to_double(&val));
	}
}

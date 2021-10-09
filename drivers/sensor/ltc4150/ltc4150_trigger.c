/*
 * Copyright (c) 2021 arithmetics.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT adi_ltc4150

#include <device.h>
#include <drivers/gpio.h>
#include <sys/util.h>
#include <kernel.h>
#include <drivers/sensor.h>
#include "ltc4150.h"

#include <stdio.h>

#include <logging/log.h>
LOG_MODULE_DECLARE(LTC4150, CONFIG_SENSOR_LOG_LEVEL);

static void ltc4150_thread_cb(const struct device *dev)
{
	struct ltc4150_data *drv_data = dev->data;
	const struct ltc4150_config *cfg = dev->config;

	/* Clear the INT pin by setting the CLR pin for 20us */
	if (cfg->int_clr_gpio->name) {
		gpio_pin_set(cfg->int_clr_gpio, cfg->int_clr_pin, 1);
		k_sleep(K_USEC(20));
		gpio_pin_set(cfg->int_clr_gpio, cfg->int_clr_pin, 0);
	}

	/* Increment/decrement charge_count based on the polarity pin.
	    Increment = battery charging, decrement = battery draining */
	if (gpio_pin_get(cfg->pol_gpio, cfg->pol_pin)) {
		drv_data->charge_count++;
	} else {
		drv_data->charge_count--;
	}

	k_mutex_lock(&drv_data->trigger_mutex, K_FOREVER);
	if ((drv_data->drdy_handler != NULL)) {
		drv_data->drdy_handler(dev, &drv_data->drdy_trigger);
	}
	k_mutex_unlock(&drv_data->trigger_mutex);
}

static void ltc4150_gpio_callback(const struct device *dev,
				  struct gpio_callback *cb, uint32_t pins)
{
	struct ltc4150_data *drv_data =
		CONTAINER_OF(cb, struct ltc4150_data, gpio_cb);

#ifdef CONFIG_LTC4150_TRIGGER_OWN_THREAD
	k_sem_give(&drv_data->gpio_sem);
#elif CONFIG_LTC4150_TRIGGER_GLOBAL_THREAD
	k_work_submit(&drv_data->work);
#endif
}

#ifdef CONFIG_LTC4150_TRIGGER_OWN_THREAD
static void ltc4150_thread(struct ltc4150_data *drv_data)
{
	while (true) {
		k_sem_take(&drv_data->gpio_sem, K_FOREVER);
		ltc4150_thread_cb(drv_data->dev);
	}
}

#elif CONFIG_LTC4150_TRIGGER_GLOBAL_THREAD
static void ltc4150_work_cb(struct k_work *work)
{
	struct ltc4150_data *drv_data =
		CONTAINER_OF(work, struct ltc4150_data, work);

	ltc4150_thread_cb(drv_data->dev);
}
#endif

int ltc4150_trigger_set(const struct device *dev,
			const struct sensor_trigger *trig,
			sensor_trigger_handler_t handler)
{
	struct ltc4150_data *drv_data = dev->data;
	const struct ltc4150_config *cfg = dev->config;
	int ret;

	gpio_pin_interrupt_configure(cfg->int_gpio, cfg->int_pin,
				     GPIO_INT_DISABLE);

	switch (trig->type) {
	case SENSOR_TRIG_DATA_READY:
		k_mutex_lock(&drv_data->trigger_mutex, K_FOREVER);
		drv_data->drdy_handler = handler;
		drv_data->drdy_trigger = *trig;
		k_mutex_unlock(&drv_data->trigger_mutex);
		break;
	default:
		LOG_ERR("Unsupported sensor trigger");
		ret = -ENOTSUP;
		goto out;
	}

out:
	gpio_pin_interrupt_configure(cfg->int_gpio, cfg->int_pin,
				     GPIO_INT_EDGE_TO_ACTIVE);

	return ret;
}

int ltc4150_init_interrupt(const struct device *dev)
{
	struct ltc4150_data *drv_data = dev->data;
	const struct ltc4150_config *cfg = dev->config;
	int ret;

	k_mutex_init(&drv_data->trigger_mutex);

	if (!device_is_ready(cfg->int_gpio)) {
		LOG_ERR("%s: int_gpio device not ready", cfg->int_gpio->name);
		return -ENODEV;
	}

	gpio_pin_configure(cfg->int_gpio, cfg->int_pin,
			   GPIO_INPUT | cfg->int_flags);

	gpio_init_callback(&drv_data->gpio_cb,
			   ltc4150_gpio_callback,
			   BIT(cfg->int_pin));

	if (gpio_add_callback(cfg->int_gpio, &drv_data->gpio_cb) < 0) {
		LOG_ERR("Failed to set gpio callback!");
		return -EIO;
	}

	if (cfg->int_clr_gpio->name) {
		if (device_is_ready(cfg->int_clr_gpio)) {
			gpio_pin_configure(cfg->int_clr_gpio, cfg->int_clr_pin,
					   GPIO_OUTPUT_ACTIVE | cfg->int_clr_flags);
		} else {
			LOG_ERR("%s: int_clr__gpio device not ready", cfg->int_clr_gpio->name);
			return -ENODEV;
		}
	}

	drv_data->dev = dev;

#ifdef CONFIG_LTC4150_TRIGGER_OWN_THREAD
	k_sem_init(&drv_data->gpio_sem, 0, UINT_MAX);

	k_thread_create(&drv_data->thread, drv_data->thread_stack,
			CONFIG_LTC4150_THREAD_STACK_SIZE,
			(k_thread_entry_t)ltc4150_thread,
			drv_data, 0, NULL,
			K_PRIO_COOP(CONFIG_LTC4150_THREAD_PRIORITY),
			0, K_NO_WAIT);
#elif CONFIG_LTC4150_TRIGGER_GLOBAL_THREAD
	drv_data->work.handler = ltc4150_work_cb;
#endif

	return 0;
}

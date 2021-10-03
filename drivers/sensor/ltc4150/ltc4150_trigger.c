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
	uint16_t status;

	k_mutex_lock(&drv_data->trigger_mutex, K_FOREVER);
	if ((drv_data->drdy_handler != NULL) && LTC4150_STATUS_DRDY(status)) {
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
	uint16_t status, int_mask, int_en;
	int ret;

	gpio_pin_interrupt_configure(cfg->intb_gpio, cfg->intb_pin,
				     GPIO_INT_DISABLE);

	switch (trig->type) {
	case SENSOR_TRIG_DATA_READY:
		k_mutex_lock(&drv_data->trigger_mutex, K_FOREVER);
		drv_data->drdy_handler = handler;
		drv_data->drdy_trigger = *trig;
		k_mutex_unlock(&drv_data->trigger_mutex);

		int_mask = LTC4150_ERROR_CONFIG_DRDY_2INT_MSK;
		break;
	default:
		LOG_ERR("Unsupported sensor trigger");
		ret = -ENOTSUP;
		goto out;
	}

	if (handler) {
		int_en = int_mask;
		drv_data->int_config |= int_mask;
	} else {
		int_en = 0U;
	}

	ret = ltc4150_reg_write_mask(dev,
				     LTC4150_ERROR_CONFIG, int_mask, int_en);
out:
	gpio_pin_interrupt_configure(cfg->intb_gpio, cfg->intb_pin,
				     GPIO_INT_EDGE_TO_ACTIVE);

	return ret;
}

int ltc4150_init_interrupt(const struct device *dev)
{
	struct ltc4150_data *drv_data = dev->data;
	const struct ltc4150_config *cfg = dev->config;
	int ret;

	k_mutex_init(&drv_data->trigger_mutex);

	if (!device_is_ready(cfg->intb_gpio)) {
		LOG_ERR("%s: int_gpio device not ready", cfg->intb_gpio->name);
		return -ENODEV;
	}

	ret = ltc4150_set_interrupt_pin(dev, true);
	if (ret) {
		return ret;
	}

	gpio_pin_configure(cfg->intb_gpio, cfg->intb_pin,
			   GPIO_INPUT | cfg->intb_flags);

	gpio_init_callback(&drv_data->gpio_cb,
			   ltc4150_gpio_callback,
			   BIT(cfg->int_pin));

	if (gpio_add_callback(cfg->intb_gpio, &drv_data->gpio_cb) < 0) {
		LOG_ERR("Failed to set gpio callback!");
		return -EIO;
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

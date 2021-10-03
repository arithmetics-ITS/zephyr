/* ltc4150.c - Driver for the Analog Devices LTC4150 */

/*
 * Copyright (c) 2021 arithmetics.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_ltc4150

#include <device.h>
#include <sys/util.h>
#include <logging/log.h>

#include "ltc4150.h"

LOG_MODULE_REGISTER(LTC4150, CONFIG_SENSOR_LOG_LEVEL);

#ifdef CONFIG_PM_DEVICE

/**
 * Gauge Enable/disable
 * @param dev - The device structure.
 * @param enable - True = enable ge pin, false = disable ge pin
 * @return 0 in case of success, negative error code otherwise.
 */
static int ltc4150_set_shutdown(const struct device *dev, bool enable)
{
	const struct ltc4150_config *cfg = dev->config;
	int ret = 0;

	gpio_pin_set(cfg->ge_gpio, cfg->shdn_pin, enable);

	return ret;
}

/**
 * Set the Device Power Management State.
 * @param dev - The device structure.
 * @param pm_state - power management state
 * @return 0 in case of success, negative error code otherwise.
 */
static int ltc4150_device_pm_ctrl(const struct device *dev,
				  enum pm_device_action action)
{
	int ret;
	struct ltc4150_data *data = dev->data;
	const struct ltc4150_config *cfg = dev->config;
	enum pm_device_state curr_state;

	(void)pm_device_state_get(dev, &curr_state);

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		if (curr_state == PM_DEVICE_STATE_OFF) {
			ret = ltc4150_set_shutdown(dev, false);
		}
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		if (cfg->ge_gpio->name) {
			ret = ltc4150_set_shutdown(dev, true);
		} else {
			LOG_ERR("SHDN pin not defined");
			ret = -ENOTSUP;
		}
		break;
	default:
		return -ENOTSUP;
	}

	return ret;
}
#endif

/**
 * Fetch sensor data from the device.
 * @param dev - The device structure.
 * @param chan - The sensor channel type.
 * @return 0 in case of success, negative error code otherwise.
 */
static int ltc4150_sample_fetch(const struct device *dev,
				enum sensor_channel chan)
{
#ifdef CONFIG_PM_DEVICE
	struct ltc4150_data *data = dev->data;
	enum pm_device_state state;

	(void)pm_device_state_get(dev, &state);
	if (state != PM_DEVICE_STATE_ACTIVE) {
		LOG_ERR("Sample fetch failed, device is not in active mode");
		return -ENXIO;
	}
#endif
	/* you need to implement this function */
	return -ENOTSUP;
}

/**
 * Get sensor channel value from the device.
 * @param dev - The device structure.
 * @param chan - The sensor channel type.
 * @param val - The sensor channel value.
 * @return 0 in case of success, negative error code otherwise.
 */
static int ltc4150_channel_get(const struct device *dev,
			       enum sensor_channel chan,
			       struct sensor_value *val)
{
	const struct ltc4150_config *cfg = dev->config;
	double data;

	switch ((int16_t)chan) {
	default:
		LOG_ERR("Channel type not supported.");
		return -ENOTSUP;
	}

	return 0;
}

static const struct sensor_driver_api ltc4150_api_funcs = {
	.sample_fetch = ltc4150_sample_fetch,
	.channel_get = ltc4150_channel_get
};

static int ltc4150_init_config(const struct device *dev)
{
	int ret;
	const struct ltc4150_config *cfg = dev->config;
	struct ltc4150_data *data = dev->data;

	return 0;
}

/**
 * Initialize the Gauge Enable Pin.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int ltc4150_init_shdn_pin(const struct device *dev)
{
	const struct ltc4150_config *cfg = dev->config;

	if (!device_is_ready(cfg->ge_gpio)) {
		LOG_ERR("%s: ge_gpio device not ready", cfg->ge_gpio->name);
		return -ENODEV;
	}

	gpio_pin_configure(cfg->ge_gpio, cfg->ge_pin,
			   GPIO_OUTPUT_INACTIVE | cfg->ge_flags);

	return 0;
}

/**
 * Initialization of the device.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int ltc4150_init(const struct device *dev)
{
	const struct ltc4150_config *cfg = dev->config;

	if (cfg->ge_gpio->name) {
		if (ltc4150_init_ge_pin(dev) < 0) {
			return -ENODEV;
		}
	}

	if (ltc4150_init_config(dev) < 0) {
		return -EIO;
	}

	return 0;
}

#define LTC4150_INT_PROPS(n)						    \
	.int_gpio = DEVICE_DT_GET(DT_GPIO_CTLR(DT_DRV_INST(n), int_gpios)), \
	.int_pin = DT_INST_GPIO_PIN(n, int_gpios),			    \
	.int_flags = DT_INST_GPIO_FLAGS(n, int_gpios),			    \

#define LTC4150_POL_PROPS(n)						    \
	.pol_gpio = DEVICE_DT_GET(DT_GPIO_CTLR(DT_DRV_INST(n), pol_gpios)), \
	.pol_pin = DT_INST_GPIO_PIN(n, pol_gpios),			    \
	.pol_flags = DT_INST_GPIO_FLAGS(n, pol_gpios),			    \

#define LTC4150_SHDN_PROPS(n)						      \
	.shdn_gpio = DEVICE_DT_GET(DT_GPIO_CTLR(DT_DRV_INST(n), shdn_gpios)), \
	.shdn_pin = DT_INST_GPIO_PIN(n, shdn_gpios),			      \
	.shdn_flags = DT_INST_GPIO_FLAGS(n, shdn_gpios),		      \

#define LTC4150_INT(n)					\
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, int_gpios),	\
		   (LTC4150_INT_PROPS(n)))

#define LTC4150_POL(n)					\
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, pol_gpios),	\
		   (LTC4150_POL_PROPS(n)))

#define LTC4150_SHDN(n)					 \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, shdn_gpios), \
		   (LTC4150_SHDN_PROPS(n)))

#define LTC4150_INIT(n)						  \
	static struct ltc4150_data ltc4150_data_##n;		  \
								  \
	static const struct ltc4150_config ltc4150_config_##n = { \
		LTC4150_INT(n)					  \
		LTC4150_POL(n)					  \
		LTC4150_SHDN(n)					  \
	};							  \
								  \
	DEVICE_DT_INST_DEFINE(n,				  \
			      ltc4150_init,			  \
			      ltc4150_device_pm_ctrl,		  \
			      &ltc4150_data_##n,		  \
			      &ltc4150_config_##n,		  \
			      POST_KERNEL,			  \
			      CONFIG_SENSOR_INIT_PRIORITY,	  \
			      &ltc4150_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(LTC4150_INIT)

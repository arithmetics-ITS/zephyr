/* ltc4150.c - Driver for the Analog Devices LTC4150 */

/*
 * Copyright (c) 2021 arithmetics.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT adi_ltc4150

#include <device.h>
#include <sys/util.h>
#include <logging/log.h>

#include "ltc4150.h"

LOG_MODULE_REGISTER(LTC4150, CONFIG_SENSOR_LOG_LEVEL);

#ifdef CONFIG_PM_DEVICE
/**
 * Gauge Enable/disable
 * @param dev - The device structure.
 * @param enable - True = enable SHDN pin, false = disable SHDN pin
 * @return 0 in case of success, negative error code otherwise.
 */
static int ltc4150_set_shutdown(const struct device *dev, bool enable)
{
	const struct ltc4150_config *cfg = dev->config;
	int ret = 0;

	gpio_pin_set(cfg->shdn_gpio, cfg->shdn_pin, enable);

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
	//const struct ltc4150_config *cfg = dev->config;
    struct ltc4150_data *data = dev->data;

	switch ((int16_t)chan) {
	case SENSOR_CHAN_GAUGE_COULOMB_COUNT:
		val->val1 = (uint32_t)data->charge_count;
		val->val2 = 0;
		break;
	default:
		LOG_ERR("Channel type not supported.");
		return -ENOTSUP;
	}

	return 0;
}

static const struct sensor_driver_api ltc4150_api_funcs = {
	.channel_get = ltc4150_channel_get,
};

static int ltc4150_init_config(const struct device *dev)
{
	//const struct ltc4150_config *cfg = dev->config;
	//struct ltc4150_data *data = dev->data;

	return 0;
}

/**
 * Initialize the POL Pin.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int ltc4150_init_pol_pin(const struct device *dev)
{
	const struct ltc4150_config *cfg = dev->config;

	if (!device_is_ready(cfg->pol_gpio)) {
		LOG_ERR("%s: pol_gpio device not ready", cfg->pol_gpio->name);
		return -ENODEV;
	}

	gpio_pin_configure(cfg->pol_gpio, cfg->pol_pin,
			   GPIO_INPUT | cfg->pol_flags);

	return 0;
}

/**
 * Initialize the SHDN Pin.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int ltc4150_init_shdn_pin(const struct device *dev)
{
	const struct ltc4150_config *cfg = dev->config;

	if (!device_is_ready(cfg->shdn_gpio)) {
		LOG_ERR("%s: ge_gpio device not ready", cfg->shdn_gpio->name);
		return -ENODEV;
	}

	gpio_pin_configure(cfg->shdn_gpio, cfg->shdn_pin,
			   GPIO_OUTPUT_INACTIVE | cfg->shdn_flags);

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

	if (cfg->shdn_gpio->name) {
		if (ltc4150_init_shdn_pin(dev) < 0) {
			return -ENODEV;
		}
	}

	if (cfg->pol_gpio->name) {
		if (ltc4150_init_pol_pin(dev) < 0) {
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

#define LTC4150_INT_CLR_PROPS(n)						    \
	.int_clr_gpio = DEVICE_DT_GET(DT_GPIO_CTLR(DT_DRV_INST(n), int_clr_gpios)), \
	.int_clr_pin = DT_INST_GPIO_PIN(n, int_clr_gpios),			    \
	.int_clr_flags = DT_INST_GPIO_FLAGS(n, int_clr_gpios),			    \

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

#define LTC4150_INT_CLR(n)				    \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, int_clr_gpios), \
		   (LTC4150_INT_CLR_PROPS(n)))

#define LTC4150_POL(n)					\
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, pol_gpios),	\
		   (LTC4150_POL_PROPS(n)))

#define LTC4150_SHDN(n)					 \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, shdn_gpios), \
		   (LTC4150_SHDN_PROPS(n)))

#define LTC4150_INIT(n)						     \
	static struct ltc4150_data ltc4150_data_##n;		     \
								     \
	static const struct ltc4150_config ltc4150_config_##n = {    \
		LTC4150_INT(n)					     \
		LTC4150_INT_CLR(n)				     \
		LTC4150_POL(n)					     \
		LTC4150_SHDN(n)					     \
		.shunt_resistance = DT_INST_PROP(n, shunt_resistance) \
	};							     \
								     \
	DEVICE_DT_INST_DEFINE(n,				     \
			      ltc4150_init,			     \
			      ltc4150_device_pm_ctrl,		     \
			      &ltc4150_data_##n,		     \
			      &ltc4150_config_##n,		     \
			      POST_KERNEL,			     \
			      CONFIG_SENSOR_INIT_PRIORITY,	     \
			      &ltc4150_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(LTC4150_INIT)

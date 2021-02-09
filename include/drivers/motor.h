/**
 * @file
 * @brief Public Motor Driver APIs
 */

/*
 * Copyright (c) 2021 arithmetics ITS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MOTOR_H_
#define ZEPHYR_INCLUDE_DRIVERS_MOTOR_H_

#include <device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Motor Interface
 * @defgroup motor_interface Motor Interface
 * @ingroup io_interfaces
 * @{
 */

/** @brief Motor directions */
enum motor_direction {
	/** Rotate Clockwise */
	MOTOR_CW,
	/** Rotate Counter-clockwise */
	MOTOR_CCW,

	/** Auto reversal mode: rotate clockwise/counter-clockwise first and then
	 *  reverse the rotation.
	 *  Examples:
	 *          x steps cw then x steps ccw
	 * 	        x seconds ccw then x seconds cw
	 * 			cw until endstop 0, then ccw until endstop 1
	 *          ...
	 */
	MOTOR_CW_FIRST_THEN_CCW,
	MOTOR_CCW_FIRST_THEN_CW,
};

/**
 * @brief Motor attribute types.
 */
enum motor_attribute {
	/** Clockwise step count value */
	MOTOR_ATTR_CW_STEPS,
	/** Counter-clockwise step count value */
	MOTOR_ATTR_CCW_STEPS,
	/** Ramp down value. Smaller value means slower deacceleration.  */
	MOTOR_ATTR_RAMP_DOWN,
	/** Ramp up value. Smaller value means slower acceleration. */
	MOTOR_ATTR_RAMP_UP,
	/** Perform rotation either continously (=0) or a fixed number of
	 * times (=1+).
	 * A fixed number of actions will perform the user set
	 * rotation cycle x-times. For example: 1 x 500 steps or 5 x 2 seconds.
	 */
	MOTOR_ATTR_ROTATION_MODE,
	/**
	 * Number of all common sensor attributes.
	 */
	MOTOR_ATTR_COMMON_COUNT,

	/**
	 * This and higher values are motor driver specific.
	 * Refer to the motor driver header file.
	 */
	MOTOR_ATTR_PRIV_START   = MOTOR_ATTR_COMMON_COUNT,

	/**
	 * Maximum value describing a motor attribute type.
	 */
	MOTOR_ATTR_MAX          = INT16_MAX,
};

/**
 * @brief Motor control operations.
 */
enum motor_ctrl {
	MOTOR_START,
	MOTOR_STOP,
	/** Apply new rotation settings immediately without stopping the motor */
	MOTOR_RESTART,
	/** Stop the motor with the highest possible priority */
	MOTOR_EMERGENCY_STOP,
};

/**
 * @typedef motor_control_t
 * @brief Callback API for controlling the motor
 */
typedef int (*motor_control_t)(const struct device *dev,
			       enum motor_ctrl ctrl);

/**
 * @typedef motor_set_direction_t
 * @brief Callback API for setting the rotation direction
 */
typedef int (*motor_set_direction_t)(const struct device *dev,
				     enum motor_direction dir);

/**
 * @typedef motor_set_speed_t
 * @brief Callback API for setting the motor speed
 */
typedef int (*motor_set_speed_t)(const struct device *dev,
				 enum motor_direction dir,
				 uint32_t pulse);

/**
 * @typedef motor_attr_set_t
 * @brief Callback API upon setting a motor attribute
 */
typedef int (*motor_attr_set_t)(const struct device *dev,
				enum motor_attribute attr,
				int32_t value);

/**
 * @typedef motor_attr_get_t
 * @brief Callback API upon getting a motor attribute
 */
typedef int (*motor_attr_get_t)(const struct device *dev,
				enum motor_attribute attr,
				int32_t *value);


/** @brief Motor driver API definition. */
__subsystem struct motor_driver_api {
	motor_control_t control;
	motor_set_direction_t set_direction;
	motor_set_speed_t set_speed;
	motor_attr_set_t attr_set;
	motor_attr_get_t attr_get;
};

/**
 * @brief Control the motor operation
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param ctrl The motor control operation
 *
 * @return 0 if successful, negative errno code if failure.
 */
__syscall int motor_control(const struct device *dev,
			    enum motor_ctrl ctrl);

static inline int z_impl_motor_control(const struct device *dev,
				       enum motor_ctrl ctrl)
{
	const struct motor_driver_api *api =
		(const struct motor_driver_api *)dev->api;

	return api->control(dev, ctrl);
}

/**
 * @brief Set the rotation direction of the motor
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param dir Motor direction
 *
 * @return 0 if successful, negative errno code if failure.
 */
__syscall int motor_set_direction(const struct device *dev,
				  enum motor_direction dir);

static inline int z_impl_motor_set_direction(const struct device *dev,
					     enum motor_direction dir)
{
	const struct motor_driver_api *api =
		(const struct motor_driver_api *)dev->api;

	return api->set_direction(dev, dir);
}

/**
 * @brief Set the speed (pulse width) of the motor
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param dir Motor direction to set the speed for
 * @param pulse Pulse width (in microseconds) to set as motor speed
 *
 * @return 0 if successful, negative errno code if failure.
 */
__syscall int motor_set_speed(const struct device *dev,
			      enum motor_direction dir,
			      uint32_t pulse);

static inline int z_impl_motor_set_speed(const struct device *dev,
					 enum motor_direction dir,
					 uint32_t pulse)
{
	const struct motor_driver_api *api =
		(const struct motor_driver_api *)dev->api;

	return api->set_speed(dev, dir, pulse);
}

/**
 * @brief Set an attribute for a motor driver
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param attr The attribute to set
 * @param value The value to set the attribute to
 *
 * @return 0 if successful, negative errno code if failure.
 */
__syscall int motor_attr_set(const struct device *dev,
			     enum motor_attribute attr,
			     int32_t value);

static inline int z_impl_motor_attr_set(const struct device *dev,
					enum motor_attribute attr,
					int32_t value)
{
	const struct motor_driver_api *api =
		(const struct motor_driver_api *)dev->api;

	if (api->attr_set == NULL) {
		return -ENOTSUP;
	}

	return api->attr_set(dev, attr, value);
}

/**
 * @brief Get an attribute of a motor driver
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param attr The attribute to get
 * @param value Pointer to where to store the attribute
 *
 * @return 0 if successful, negative errno code if failure.
 */
__syscall int motor_attr_get(const struct device *dev,
			     enum motor_attribute attr,
			     int32_t *value);

static inline int z_impl_motor_attr_get(const struct device *dev,
					enum motor_attribute attr,
					int32_t *value)
{
	const struct motor_driver_api *api =
		(const struct motor_driver_api *)dev->api;

	if (api->attr_get == NULL) {
		return -ENOTSUP;
	}

	return api->attr_get(dev, attr, value);
}

/**
 * @brief Helper function for converting pulse width (microseconds) to
 * pulses per second
 *
 * @param val A pointer to a motor_value struct.
 * @return The converted value.
 */
static inline double motor_pulse_to_pps(uint32_t usec)
{
	return 1.0 / ((double)usec / 1000000.0);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#include <syscalls/motor.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_MOTOR_H_ */

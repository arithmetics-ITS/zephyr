/* pca9629a.c - Driver for the NXP I2C-bus advanced stepper motor controller */

/*
 * Copyright (c) 2021 arithmetics ITS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_pca9629a

#include "motor_pca9629a.h"
#include <logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(PCA9629A, CONFIG_MOTOR_LOG_LEVEL);
/**
 * Read/Write from device.
 * @param dev - The device structure.
 * @param reg - The register address. Use PCA9629A_REG_READ(x) or PCA9629_REG_WRITE(x).
 * @param data - The register data.
 * @param length - Number of bytes being read
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_bus_access(const struct device *dev, uint8_t reg,
			       void *data, size_t length)
{
	struct pca9629a_data *pca9629a_data = dev->data;
	const struct pca9629a_config *cfg = dev->config;

	if (reg & PCA9629A_READ) {
		return i2c_burst_read(pca9629a_data->bus, cfg->i2c_addr,
				      (PCA9629A_TO_I2C_REG(reg) | PCA9629A_AUTO_INCREMENT),
				      (uint8_t *) data, length);
	} else {
		if (length < 1) {
			return -EINVAL;
		}

		uint8_t buf[length + 1];
		buf[0] = (PCA9629A_TO_I2C_REG(reg) | PCA9629A_AUTO_INCREMENT);
		memcpy(buf + 1, data, length * sizeof(uint8_t));

		return i2c_write(pca9629a_data->bus, buf,
				 sizeof(buf), cfg->i2c_addr);
	}
}

/**
 * Read from device.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param reg_data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_reg_read(const struct device *dev,
			     uint8_t reg_addr,
			     uint8_t *reg_data)
{
	return pca9629a_bus_access(dev, PCA9629A_REG_READ(reg_addr), reg_data, 1);
}

/**
 * Read (16 Bit) from device.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param reg_data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_reg_read_16(const struct device *dev,
				uint8_t reg_addr,
				uint16_t *reg_data)
{
	uint8_t buf[2];
	int ret;

	ret = pca9629a_bus_access(dev, PCA9629A_REG_READ(reg_addr), buf, 2);
	*reg_data = ((uint16_t)buf[1] << 8) | buf[0];

	return ret;
}

/**
 * Write (8 Bit) to device.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param reg_data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_reg_write(const struct device *dev,
			      uint8_t reg_addr,
			      uint8_t reg_data)
{
	LOG_DBG("[0x%x] = 0x%x", reg_addr, reg_data);

	return pca9629a_bus_access(dev, PCA9629A_REG_WRITE(reg_addr), &reg_data, 1);
}

/**
 * Write (16 Bit) to device.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param reg_data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_reg_write_16(const struct device *dev,
				 uint8_t reg_addr,
				 uint16_t reg_data)
{
	LOG_DBG("[0x%x] = 0x%x", reg_addr, reg_data);

	uint8_t buf[2];

	buf[0] = (uint8_t)reg_data;
	buf[1] = (uint8_t)(reg_data >> 8);

	return pca9629a_bus_access(dev, PCA9629A_REG_WRITE(reg_addr), buf, 2);
}

/**
 * I2C write (8 Bit) to device using a mask.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param mask - The mask.
 * @param data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
int pca9629a_reg_write_mask(const struct device *dev,
			    uint8_t reg_addr,
			    uint16_t mask,
			    uint8_t data)
{
	int ret;
	uint8_t tmp;

	ret = pca9629a_reg_read(dev, reg_addr, &tmp);
	if (ret) {
		return ret;
	}
	LOG_DBG("read [0x%x] = 0x%x", reg_addr, tmp);
	LOG_DBG("mask: 0x%x", mask);

	tmp &= ~mask;
	tmp |= data;

	return pca9629a_reg_write(dev, reg_addr, tmp);
}

/**
 * Set the motor phase (One-phase, two-phase or half-step drive)
 * @param dev - The device structure.
 * @param phase - Phase waveform
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_set_motor_phase(const struct device *dev,
				    uint8_t phase)
{
	return pca9629a_reg_write_mask(dev,
				       PCA9629A_OP_CFG_PHS,
				       PCA9629A_OP_CFG_PHS_PHASE_MSK,
				       PCA9629A_OP_CFG_PHS_PHASE_SET(phase));
}

/**
 * Set the motor step rate (speed of the motor in pulses per second)
 * for the specific direction (CW or CCW)
 * @param dev - The device structure.
 * @param dir - Motor direction for setting the pps
 * @param pps - Pulses per second
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_set_pps(const struct device *dev,
			    enum motor_direction dir,
			    double pps)
{
	uint8_t prescaler = 0;
	uint8_t ratio;
	uint16_t step_pulse_width;
	uint8_t reg_addr;

	// check valid range
	if (pps < PCA9629A_STEP_PPS_MIN) {
		pps = PCA9629A_STEP_PPS_MIN;
		LOG_DBG("PPS out of range. Setting it to minimum: 0.3179");
	} else if (pps > PCA9629A_STEP_PPS_MAX) {
		pps = PCA9629A_STEP_PPS_MAX;
		LOG_DBG("PPS out of range. Setting it to maximum: 333333");
	}

	if (dir == MOTOR_CW) {
		reg_addr = PCA9629A_CWPWL;
	} else if (dir == MOTOR_CCW) {
		reg_addr = PCA9629A_CCWPWL;
	} else {
		return -EINVAL;
	}

	/* Prescaler ranges:
	   0 = 333333.0 to 40.6901 pps (3 µs to 24.576 ms)
	   1 = 166666.5 to 20.3450 pps (6 µs to 49.152 ms)
	   2 =  83333.3 to 10.1725 pps (12 µs to 98.304 ms)
	   3 =  41666.6 to  5.0862 pps (24 µs to 196.608 ms)
	   4 =  20833.3 to  2.5431 pps (48 µs to 393.216 ms)
	   5 =  10416.6 to  1.2715 pps (96 µs to 786.432 ms)
	   6 =   5208.3 to  0.6357 pps (192 µs to 1572.864 ms)
	   7 =   2604.2 to  0.3179 pps (384 µs to 3145.728 ms)
	 */
	ratio = (uint8_t)(40.6901 / pps);

	prescaler = (ratio & 0x01) ? 1 : prescaler;
	prescaler = (ratio & 0x02) ? 2 : prescaler;
	prescaler = (ratio & 0x04) ? 3 : prescaler;
	prescaler = (ratio & 0x08) ? 4 : prescaler;
	prescaler = (ratio & 0x10) ? 5 : prescaler;
	prescaler = (ratio & 0x20) ? 6 : prescaler;
	prescaler = (ratio & 0x40) ? 7 : prescaler;

	step_pulse_width = PCA9629A_STEP_PPS_MAX /
			   ((1 << prescaler) * pps);

	step_pulse_width |= (prescaler << 13);

	return pca9629a_reg_write_16(dev,
				     reg_addr,
				     step_pulse_width);
}

/**
 * Set output pin behavior for a specific direction after motor stops
 * @param dev - The device structure.
 * @param dir - Motor direction for setting the output state
 * @param state - Output state to set
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_set_output_state(const struct device *dev,
				     enum motor_direction dir,
				     uint8_t state)
{
	uint8_t msk;
	uint8_t bit;

	if (dir == MOTOR_CW) {
		msk = PCA9629A_OP_STAT_TO_OUT_CW_STATE_MSK;
		bit = PCA9629A_OP_STAT_TO_OUT_CW_STATE_SET(state);
	} else if (dir == MOTOR_CCW) {
		msk = PCA9629A_OP_STAT_TO_OUT_CCW_STATE_MSK;
		bit = PCA9629A_OP_STAT_TO_OUT_CCW_STATE_SET(state);
	} else {
		return -EINVAL;
	}

	return pca9629a_reg_write_mask(dev,
				       PCA9629A_OP_STAT_TO, msk, bit);
}

/**
 * Set the motor state
 * @param dev - The device structure.
 * @param state - State to set
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_set_motor_state(const struct device *dev,
				    enum pca9629a_motor_states state)
{
	return pca9629a_reg_write_mask(dev,
				       PCA9629A_MCNTL,
				       PCA9629A_MCNTL_MOTOR_STATE_MSK,
				       PCA9629A_MCNTL_MOTOR_STATE_SET(state));
}

/**
 * Set the step count for the specific motor direction
 * @param dev - The device structure.
 * @param dir - Motor direction for setting the step count
 * @param steps - Amount of steps
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_set_step_count(const struct device *dev,
				   enum motor_direction dir,
				   uint16_t steps)
{
	uint8_t reg_addr;

	if (dir == MOTOR_CW) {
		reg_addr = PCA9629A_CWSCOUNTL;
	} else if (dir == MOTOR_CCW) {
		reg_addr = PCA9629A_CCWSCOUNTL;
	} else {
		return -EINVAL;
	}
	return pca9629a_reg_write_16(dev, reg_addr, steps);
}

/**
 * Set the ramp rate (ramp up or ramp down)
 * @param dev - The device structure.
 * @param mode -Ramp mode
 * @param ramp_factor - Ramp factor
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_set_ramp(const struct device *dev,
			     enum pca9629a_ramp_mode mode,
			     uint8_t ramp_factor)
{
	uint8_t reg_addr;

	if (mode == PCA9629A_RAMP_UP) {
		reg_addr = PCA9629A_RUCNTL;
	} else if (mode == PCA9629A_RAMP_DOWN) {
		reg_addr = PCA9629A_RDCTNL;
	} else {
		return -EINVAL;
	}

	if (ramp_factor > PCA9629A_RAMP_FACTOR_MAX) {
		return -EINVAL;
	}

	return pca9629a_reg_write(dev, reg_addr,
				  ramp_factor ? (0x30 | ramp_factor) : 0x00);
}

/**
 * Set the perform multiple of actions control (PMA)
 * @param dev - The device structure.
 * @param num_actions - Number of actions to perform
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_set_pma(const struct device *dev, uint8_t num_actions)
{
	return pca9629a_reg_write(dev, PCA9629A_PMA, num_actions);
}

/**
 * Emergency stop the motor immediately (has the highest priority than all
 * other features)
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_emergency_stop(const struct device *dev)
{
	return pca9629a_reg_write_mask(dev,
				       PCA9629A_MCNTL,
				       PCA9629A_MCNTL_EMERGENCY_STOP_MSK,
				       PCA9629A_MCNTL_EMERGENCY_STOP_SET(1));
}

/**
 * Set the motor direction
 * @param dev - The device structure.
 * @param dir - Motor direction to rotate
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_set_direction(const struct device *dev,
				  enum motor_direction dir)
{
	uint8_t ret;

	switch (dir) {
	case MOTOR_CW:
	case MOTOR_CCW:
	case MOTOR_CW_FIRST_THEN_CCW:
	case MOTOR_CCW_FIRST_THEN_CW:
		ret = pca9629a_reg_write_mask(dev, PCA9629A_MCNTL,
					      PCA9629A_MCNTL_DIRECTION_CTRL_MSK,
					      PCA9629A_MCNTL_DIRECTION_CTRL_SET(dir));
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/**
 * Starts the motor
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_start(const struct device *dev)
{
	return pca9629a_set_motor_state(dev, PCA9629A_START);
}

/**
 * Stops the motor
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_stop(const struct device *dev)
{
	return pca9629a_set_motor_state(dev, PCA9629A_STOP);
}

/**
 * Restarts the motor to change current motor speed and operation
 * without stopping the motor. Applys only if the total number
 * of steps is not completed yet
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_restart(const struct device *dev)
{
	return pca9629a_reg_write_mask(dev,
				       PCA9629A_MCNTL,
				       PCA9629A_MCNTL_RESTART_MOTOR_MSK,
				       PCA9629A_MCNTL_RESTART_MOTOR_SET(1));
}

/**
 * Set the stepper motor operation state
 * @param dev - The device structure.
 * @param op_state - The operation state
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_control(const struct device *dev,
			    enum motor_ctrl ctrl)
{
	int ret = 0;

	switch (ctrl) {
	case MOTOR_START:
		ret = pca9629a_start(dev);
		break;
	case MOTOR_STOP:
		ret = pca9629a_stop(dev);
		break;
	case MOTOR_RESTART:
		ret = pca9629a_restart(dev);
		break;
	case MOTOR_EMERGENCY_STOP:
		ret = pca9629a_emergency_stop(dev);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}
	return ret;
}

/**
 * Set the speed (pulse width) of the motor
 * @param dev - The device structure.
 * @param dir - Motor direction to set the rate for
 * @param rate - Step rate value
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_set_speed(const struct device *dev,
			      enum motor_direction dir,
			      uint32_t pulse)
{
	double pps = motor_pulse_to_pps(pulse);

	return pca9629a_set_pps(dev, dir, pps);
}

/**
 * Set attributes for the device.
 * @param dev - The device structure.
 * @param attr - The attribute to set.
 * @param value - The attribute value.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_attr_set(const struct device *dev,
			     enum motor_attribute attr,
			     int32_t value)
{
	int ret = 0;

	switch (attr) {
	case MOTOR_ATTR_CW_STEPS:
		ret = pca9629a_set_step_count(dev, MOTOR_CW, value);
		break;
	case MOTOR_ATTR_CCW_STEPS:
		ret = pca9629a_set_step_count(dev, MOTOR_CCW, value);
		break;
	case MOTOR_ATTR_RAMP_DOWN:
		ret = pca9629a_set_ramp(dev, PCA9629A_RAMP_DOWN, value);
		break;
	case MOTOR_ATTR_RAMP_UP:
		ret = pca9629a_set_ramp(dev, PCA9629A_RAMP_UP, value);
		break;
	case MOTOR_ATTR_ROTATION_MODE:
		ret = pca9629a_set_pma(dev, value);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}
	return ret;
}


/**
 * Set attributes for the device.
 * @param dev - The device structure.
 * @param attr - The attribute to get.
 * @param value - Pointer to where to store the attribute.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_attr_get(const struct device *dev,
			     enum motor_attribute attr,
			     int32_t *value)
{
	int ret = 0;

	// switch (attr) {
	// case MOTOR_ATTR_CW_STEPS:
	// 	ret = pca9629a_set_step_count(dev, MOTOR_CW, value);
	// 	break;
	// case MOTOR_ATTR_CCW_STEPS:
	// 	ret = pca9629a_set_step_count(dev, MOTOR_CCW, value);
	// 	break;
	// case MOTOR_ATTR_RAMP_DOWN:
	// 	ret = pca9629a_set_ramp(dev, PCA9629A_RAMP_DOWN, value);
	// 	break;
	// case MOTOR_ATTR_RAMP_UP:
	// 	ret = pca9629a_set_ramp(dev, PCA9629A_RAMP_UP, value);
	// 	break;
	// default:
	// 	ret = -ENOTSUP;
	// 	break;
	// }
	return ret;
}

static const struct motor_driver_api pca9629a_api_funcs = {
	.control = pca9629a_control,
	.set_direction = pca9629a_set_direction,
	.set_speed = pca9629a_set_speed,
	.attr_set = pca9629a_attr_set,
	.attr_get = pca9629a_attr_get,
};

/**
 * Software reset the device.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_sw_reset(const struct device *dev)
{
	return 0;
}

/**
 * Initialization of the device.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int pca9629a_init(const struct device *dev)
{
	struct pca9629a_data *data = dev->data;
	const struct pca9629a_config *cfg = dev->config;

	data->bus = device_get_binding(cfg->i2c_name);
	if (data->bus  == NULL) {
		LOG_ERR("Failed to get pointer to %s device!", cfg->i2c_name);
		return -EINVAL;
	}

	pca9629a_sw_reset(dev);

	pca9629a_set_motor_phase(dev, cfg->phase);
	pca9629a_set_output_state(dev, MOTOR_CW, cfg->cw_state_after_stop);
	pca9629a_set_output_state(dev, MOTOR_CCW, cfg->ccw_state_after_stop);

	return 0;
}

static struct pca9629a_data pca9629a_data;

#define PCA9629A_INIT(n)					       \
								       \
	static const struct pca9629a_config pca9629a_config_##n = {    \
		.i2c_name = DT_INST_BUS_LABEL(n),		       \
		.i2c_addr = DT_INST_REG_ADDR(n),		       \
		.phase = DT_ENUM_IDX(DT_DRV_INST(n), phase),	       \
		.cw_state_after_stop =				       \
			DT_ENUM_IDX(DT_DRV_INST(n), cw_output_state),  \
		.ccw_state_after_stop =				       \
			DT_ENUM_IDX(DT_DRV_INST(n), ccw_output_state), \
	};							       \
								       \
	DEVICE_AND_API_INIT(pca9629a_##n,			       \
			    DT_INST_LABEL(n),			       \
			    pca9629a_init,			       \
			    &pca9629a_data,			       \
			    &pca9629a_config_##n,		       \
			    POST_KERNEL,			       \
			    CONFIG_MOTOR_INIT_PRIORITY,		       \
			    &pca9629a_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(PCA9629A_INIT)
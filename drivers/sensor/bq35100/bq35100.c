/* bq35100.c - Driver for the Texas Instruments BQ35100 */

/*
 * Copyright (c) 2021 arithmetics.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_bq35100

#include <device.h>
#include <sys/util.h>
#include <logging/log.h>
#include <math.h>

#include "bq35100.h"
#include "drivers/sensor/bq35100.h"

LOG_MODULE_REGISTER(BQ35100, CONFIG_SENSOR_LOG_LEVEL);

static int bq35100_set_security_mode(const struct device *dev, bq35100_security_mode_t security_mode);
static int bq35100_compute_checksum(const char *data, size_t length);
static int bq35100_get_security_mode(const struct device *dev);
static int bq35100_get_status(const struct device *dev, uint16_t *status);
static int bq35100_set_design_capacity(const struct device *dev, uint16_t capacity);
void bq35100_float_to_DF(float val, char *result);
static int bq35100_use_int_temp(const struct device *dev, bool internal);

/**
 * Read/Write from device.
 * @param dev - The device structure.
 * @param reg - The register address. Use BQ35100_REG_READ(x) or
 *				BQ35100_REG_WRITE(x).
 * @param data - The register data.
 * @param length - Number of bytes being read
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_bus_access(const struct device *dev, uint8_t reg,
			      uint8_t *data, size_t length)
{
	const struct bq35100_config *cfg = dev->config;

	if (reg & BQ35100_READ) {
		return i2c_burst_read(cfg->bus,
				      cfg->i2c_addr,
				      BQ35100_TO_I2C_REG(reg),
				      data, length);
	} else {
		if (length != 2) {
			return -EINVAL;
		}

		uint8_t buf[3];

		buf[0] = BQ35100_TO_I2C_REG(reg);
		memcpy(buf + 1, data, sizeof(uint16_t));

		return i2c_write(cfg->bus, buf,
				 sizeof(buf), cfg->i2c_addr);
	}
}

/**
 * Read 8, 16 or 32 Bit from device.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param reg_data - The register data.
 * @param length - Number of bytes being read
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_reg_read(const struct device *dev,
			    uint8_t reg_addr,
			    void *reg_data,
			    size_t length)
{
	uint8_t buf[length];
	int ret;

	ret = bq35100_bus_access(dev, BQ35100_REG_READ(reg_addr), buf, length);

	/* Little Endian */
	switch (length) {
	case 1:
		*(uint8_t *)reg_data = buf[0];
		break;
	case 2:
		*(uint16_t *)reg_data = ((uint16_t)buf[1] << 8) | (uint16_t)buf[0];
		break;
	case 4:
		*(uint32_t *)reg_data = ((uint32_t)buf[3] << 24) | ((uint32_t)buf[2] << 16) |
					((uint32_t)buf[1] << 8) | (uint32_t)buf[0];
		break;
	default:
		return -ENOTSUP;
	}

	return ret;
}

/**
 * Write (16 Bit) to device.
 * @param dev - The device structure.
 * @param reg_addr - The register address.
 * @param reg_data - The register data.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_reg_write(const struct device *dev,
			     uint8_t reg_addr,
			     uint16_t reg_data)
{
	LOG_DBG("[0x%x] = 0x%x", reg_addr, reg_data);

	uint8_t buf[2];

	/* Little Endian */
	buf[0] = (uint8_t)reg_data;
	buf[1] = (uint8_t)(reg_data >> 8);

	return bq35100_bus_access(dev, BQ35100_REG_WRITE(reg_addr), buf, 2);
}

/**
 * Write a subcommand to the device.
 * @param dev - The device structure.
 * @param subcommand - the subcommand register address
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_control_reg_write(const struct device *dev,
				     uint16_t subcommand)
{
	//LOG_DBG("[0x%x] = 0x%x", BQ35100_CMD_MAC_CONTROL, subcommand);

	uint8_t buf[2];
	uint8_t ret;

	/* Little Endian */
	buf[0] = (uint8_t)subcommand;
	buf[1] = (uint8_t)(subcommand >> 8);

	ret = bq35100_bus_access(dev, BQ35100_REG_WRITE(BQ35100_CMD_MAC_CONTROL),
				 buf, 2);

	/* maybe increase it to test, if set_security still doesnt work */
	k_sleep(K_MSEC(10));

	return ret;
}

/**
 * Read the response data of the previous subcommand from the device.
 * @param dev - The device structure.
 * @param data - The data response from the previous subcommand
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_control_reg_read(const struct device *dev,
				    uint16_t *data)
{
	uint8_t buf[2];
	int ret;

	ret = bq35100_bus_access(dev, BQ35100_REG_READ(BQ35100_CMD_MAC_DATA),
				 buf, 2);

	/* Little Endian */
	*data = ((uint16_t)buf[1] << 8) | buf[0];

	return ret;
}

/**
 * Read data of given length from a given address.
 * @param dev - The device structure.
 * @param address - The address to be read.
 * @param buf
 * @param length - Number of bytes to be read.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_read_extended_data(const struct device *dev, uint16_t address,
				      uint8_t *buf, size_t length)
{
	const struct bq35100_config *cfg = dev->config;
	struct bq35100_data *dev_data = dev->data;
	size_t length_read;
	uint8_t data[32 + 2 + 2]; /* 32 bytes of data, 2 bytes of address,
	                             1 byte of MACDataSum and 1 byte of MACDataLen */

	bool success = false;
	bq35100_security_mode_t previous_security_mode = dev_data->security_mode;

	if (dev_data->security_mode == BQ35100_SECURITY_UNKNOWN) {
		LOG_ERR("Unkown Security Mode");
		return -EIO;
	}

	if (address < 0x4000 || address > 0x43FF || !buf) {
		LOG_ERR("Invalid address or data");
		return -EIO;
	}

	if (dev_data->security_mode == BQ35100_SECURITY_SEALED
	    && !bq35100_set_security_mode(dev, BQ35100_SECURITY_UNSEALED)) {
		LOG_ERR("Current mode is Sealed, Unseal it first");
		return -EIO;
	}

	k_sleep(K_MSEC(1000));

	if ((bq35100_control_reg_write(dev, address) < 0)) {
		LOG_ERR("Unable to write to ManufacturerAccessControl in readExtended");
		return -EIO;
	}

	k_sleep(K_MSEC(500));

	if (i2c_burst_read(cfg->bus, cfg->i2c_addr, BQ35100_CMD_MAC_CONTROL,
			   data, sizeof(data)) < 0) {
		LOG_ERR("Unable to read from ManufacturerAccessControl");
		return -EIO;
	}

	// Address match check
	if (data[0] != (uint8_t)address || data[1] != (uint8_t)(address >> 8)) {
		LOG_ERR("Address didn't match (expected 0x%04X, received 0x%02X%02X)", address, data[1], data[0]);
		return -1;
	}

	if (data[34] != bq35100_compute_checksum(data, data[35] - 2)) {
		LOG_ERR("Checksum didn't match (0x%02X expected)", data[34]);
		return -EIO;
	}

	length_read = data[35] - 4; // Subtracting addresses, MACDataSum and MACDataLen

	if (length_read > length) {
		length_read = length;
	}

	memcpy(buf, data + 2, length_read);
	success = true;

	// Change back the security mode if it was changed
	if (previous_security_mode != dev_data->security_mode) {
		success = bq35100_set_security_mode(dev, previous_security_mode);
	}

	return success;
}

/**
 * Write data of given length from a given address.
 * @param dev - The device structure.
 * @param address - The address is going to be written.
 * @param data - The data is going to be written.
 * @param length - Number of bytes to be written.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_write_extended_data(const struct device *dev, uint16_t address, const uint8_t *data, size_t length)
{
	const struct bq35100_config *cfg = dev->config;
	struct bq35100_data *dev_data = dev->data;
	uint16_t status;
	char d[32 + 3]; // Max data len + header

	int success = false;
	bq35100_security_mode_t previous_security_mode = dev_data->security_mode;

	if (dev_data->security_mode == BQ35100_SECURITY_UNKNOWN) {
		LOG_ERR("Unkown Security Mode in write extended");
		return -EIO;
	}

	if (address < 0x4000 || address > 0x43FF || length < 1 || length > 32 || !data) {
		LOG_ERR("Invalid address or data");
		return -EIO;
	}

	if (dev_data->security_mode == BQ35100_SECURITY_SEALED && !bq35100_set_security_mode(dev, BQ35100_SECURITY_UNSEALED)) {
		LOG_ERR("Current mode is Sealed, Unseal it first");
		return false;
	}

	if ((bq35100_control_reg_write(dev, address) < 0)) {
		LOG_ERR("Unable to write to ManufacturerAccessControl in writeExtended");
		return -EIO;
	}

	d[0] = BQ35100_CMD_MAC_CONTROL;
	d[1] = address & UCHAR_MAX;
	d[2] = address >> 8;

	memcpy(d + 3, data, length); // Remaining bytes are the d bytes we wish to write
	k_sleep(K_MSEC(1000));

	if (i2c_write(cfg->bus, d, 3 + length, cfg->i2c_addr) < 0) {
		LOG_ERR("Unable to write to MAC");
		return -EIO;
	}

	d[0] = BQ35100_CMD_MAC_DATA_SUM;
	d[1] = bq35100_compute_checksum(d + 1, length + 2);

	if (i2c_write(cfg->bus, d, 2, cfg->i2c_addr) < 0) {
		LOG_ERR("Unable to write to MAC Data Sum");
		return -EIO;
	}

	// Write 4 + length to MAC Data Length
	d[0] = BQ35100_CMD_MAC_DATA_LEN;
	d[1] = length + 4;

	if (i2c_write(cfg->bus, d, 2, cfg->i2c_addr) < 0) {
		LOG_ERR("Unable to write to MAC Data Sum");
		return -EIO;
	}

	k_sleep(K_MSEC(100));

	if (bq35100_get_status(dev, &status)) {
		LOG_ERR("Unable to read CMD_CONTROL");
		return -EIO;
	}

	if (status & BQ35100_FLASHF_BIT_MASK) {
		LOG_ERR("Writing failed");
		return -EIO;
	}

	success = true;
	LOG_DBG("Write successful");

	// Change back the security mode if it was changed
	if (previous_security_mode != dev_data->security_mode) {
		success = bq35100_set_security_mode(dev, previous_security_mode);
	}

	return success;
}

/**
 * Compute Checksum
 * @param data - data to compute checksum for
 * @param length - length of  data
 * @return checksum
 */
static int bq35100_compute_checksum(const char *data, size_t length)
{
	uint8_t checksum = 0;
	uint8_t x = 0;

	if (data) {
		for (x = 1; x <= length; x++) {
			checksum += *data;
			data++;
		}

		checksum = 0xFF - checksum;
	}

	return checksum;
}

/**
 * Change the security mode.
 * @param dev - The device structure.
 * @param security_mode - The security mode wanted to be set.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_set_security_mode(const struct device *dev, bq35100_security_mode_t security_mode)
{
	struct bq35100_data *data = dev->data;
	int status;
	uint8_t buf[4];
	uint8_t i;
	bool success = false;
	uint32_t half_access_code;

	if (data->security_mode == security_mode) {
		LOG_DBG("Already inside desired mode");
		return 0; // We are already in this mode
	}

	for (i = 0; (i < 3) && !success; i++) {
		switch (security_mode) {
		case BQ35100_SECURITY_UNKNOWN:
			LOG_ERR("Unkown mode");
			return 0;
		case  BQ35100_SECURITY_FULL_ACCESS:
			if (data->security_mode == BQ35100_SECURITY_SEALED &&
			    !bq35100_set_security_mode(dev, BQ35100_SECURITY_UNSEALED)) {
				LOG_ERR("Unsealing failed");
				return -EIO;
			}

			status = bq35100_read_extended_data(dev,
							    BQ35100_FLASH_FULL_UNSEAL_STEP1, buf, 4);
			if (status < 0) {
				LOG_ERR("Unable to read from DataFlash");
				return -EIO;
			}

			uint32_t full_access_codes = (buf[0] << 24) + (buf[1] << 16) +
						     (buf[2] << 8) + buf[3];
							 
			half_access_code = (uint16_t)(full_access_codes >> 16);
			status = bq35100_control_reg_write(dev, half_access_code);

			if (!(status < 0)) {
				half_access_code = (uint16_t)(full_access_codes);
				bq35100_control_reg_write(dev, half_access_code);
			}
			break;
		case BQ35100_SECURITY_UNSEALED:
			if (data->security_mode == BQ35100_SECURITY_FULL_ACCESS &&
			    !bq35100_set_security_mode(dev, BQ35100_SECURITY_SEALED)) {
				LOG_ERR("Seal first if in Full Access mode");
				return -EIO;
			}
			half_access_code = (uint16_t)(BQ35100_DEFAULT_SEAL_CODES >> 16);
			status = bq35100_control_reg_write(dev, half_access_code);
			if (!(status < 0)) {
				half_access_code = (uint16_t)(BQ35100_DEFAULT_SEAL_CODES);
			    bq35100_control_reg_write(dev, half_access_code);
			}
			break;
		case BQ35100_SECURITY_SEALED:
			bq35100_control_reg_write(dev, BQ35100_CTRL_SEALED);
			break;
		default:
			LOG_ERR("Invalid mode");
			return -EIO;
			break;
		}
		k_sleep(K_MSEC(100));

		data->security_mode = bq35100_get_security_mode(dev);

		if (data->security_mode == security_mode) {
			success = true;
			LOG_DBG("Security mode set as 0x%02X", data->security_mode);

		} else {
			LOG_ERR("Security mode set failed (wanted 0x%02X, got 0x%02X), will retry", security_mode, data->security_mode);
			k_sleep(K_MSEC(40));
		}
	}

	return 0;
}

/**
 * Waiting for the CONTROL_STATUS register to change after a state change
 * @param dev - The device structure.
 * @param expected - result bit mask to be matched
 * @param mask - bit mask to be matched
 * @param wait_ms - how long to wait before repeat in milliseconds
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_wait_for_status(const struct device *dev, uint16_t expected,
				   uint16_t mask, uint16_t wait_ms)
{
	uint16_t status;
	uint8_t i;

	for (i = 0; i < 5; i++) {
		if (bq35100_get_status(dev, &status) < 0) {
			LOG_DBG("Getting status failed");
			return -1;
		}

		if ((status & mask) == expected) {
			// LOG_DBG("Status match");
			return 0;

		} else {
			LOG_ERR("Status not yet in requested state read: %04X expected: %04X", status, expected);
			k_sleep(K_MSEC(wait_ms));
		}
	}

	return -1;
}

/**
 * Set Gauge Mode.
 * @param dev - The device structure.
 * @param gauge_mode - The Gauge mode wanted to be set
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_set_gauge_mode(const struct device *dev, bq35100_gauge_mode_t gauge_mode)
{
	struct bq35100_data *dev_data = dev->data;
	uint8_t buf;
	int status;

	if (gauge_mode == BQ35100_UNKNOWN_MODE) {
		LOG_ERR("Unkown mode");
		return -1;
	}

	// Operation Config A
	if ((bq35100_read_extended_data(dev, BQ35100_FLASH_OPERATION_CFG_A,
					&buf, 1)) < 0) {
		LOG_ERR("Unable to read Operation Config A");
		return -EIO;
	}

	if ((buf & 0b11) != gauge_mode) {
		buf = buf & ~0b11;
		buf |= (uint8_t)gauge_mode;

		k_sleep(K_MSEC(100));

		status = bq35100_write_extended_data(dev, BQ35100_FLASH_OPERATION_CFG_A,
						     &buf, 1);
		if (status < 0) {
			LOG_ERR("Unable to write Operation Config A");
			return -EIO;
		}
	}

	dev_data->gauge_mode = buf & 0b11;

	return 0;
}

/**
 * Triggers the device to enter ACTIVE mode.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_gauge_start(const struct device *dev)
{
	int status;
	struct bq35100_data *dev_data = dev->data;

	if (dev_data->gauge_enabled) {
		LOG_WRN("Gauge already enabled");
		return 0;
	}

	status = bq35100_control_reg_write(dev, BQ35100_CTRL_GAUGE_START);
	if (status < 0) {
		LOG_ERR("Unable to write control register");
		return -EIO;
	}

	status = bq35100_wait_for_status(dev, BQ35100_GA_BIT_MASK,
					 BQ35100_GA_BIT_MASK, 100);

	if (status < 0) {
		LOG_ERR("Gauge not enabled");
		dev_data->gauge_enabled = false;
	} else {
		LOG_DBG("Gauge enabled");
		dev_data->gauge_enabled = true;
	}

	return 0;
}

/**
 * Triggers the device to stop gauging and complete all outstanding tasks.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_gauge_stop(const struct device *dev)
{
	int status;
	struct bq35100_data *dev_data = dev->data;

	if (!dev_data->gauge_enabled) {
		LOG_WRN("Gauge already disabled");
		return 0;
	}

	status = bq35100_control_reg_write(dev, BQ35100_CTRL_GAUGE_STOP);
	if (status < 0) {
		LOG_ERR("Unable to write control register");
		return -EIO;
	}

	// Stopping takes a lot of time
	if (bq35100_wait_for_status(dev, 0, BQ35100_GA_BIT_MASK, 500) < 0) {
		LOG_ERR("Gauge not stopped");
		dev_data->gauge_enabled = true;

	} else {
		LOG_DBG("Gauge stopped");
		dev_data->gauge_enabled = false;
	}

	return 0;
}

/**
 * Indicates a new battery has been insterted.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_new_battery(const struct device *dev, uint16_t capacity)
{
	int status;

	if (capacity != 0 && bq35100_set_design_capacity(dev,capacity)) {
		return -EIO;
	}

	status = bq35100_control_reg_write(dev, BQ35100_CTRL_NEW_BATTERY);
	if (status < 0) {
		LOG_ERR("Unable to set new battery");
		return -EIO;
	}

	return 0;
}

/**
 * Get the temperature register data
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_temp(const struct device *dev)
{
	struct bq35100_data *data = dev->data;

	return bq35100_reg_read(dev, BQ35100_CMD_TEMPERATURE, &data->temperature, 2);
}

/**
 * Get the internal temperature register data
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise. 
 */
static int bq35100_get_internal_temp(const struct device *dev)
{
	struct bq35100_data *data = dev->data;

	return bq35100_reg_read(dev, BQ35100_CMD_INTERNAL_TEMP,
				&data->internal_temperature, 2);
}

/**
 * Get the voltage register data
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_voltage(const struct device *dev)
{
	struct bq35100_data *data = dev->data;

	return bq35100_reg_read(dev, BQ35100_CMD_VOLTAGE, &data->voltage, 2);
}

/**
 * Get the current register data
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_avg_current(const struct device *dev)
{
	struct bq35100_data *data = dev->data;

	if (data->gauge_enabled != true) {
		LOG_ERR("To measure current first enable gauge");
		return -EIO;
	}

	return bq35100_reg_read(dev, BQ35100_CMD_CURRENT, &data->avg_current, 2);
}

/**
 * Get the state of health register data
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_state_of_health(const struct device *dev)
{
	struct bq35100_data *data = dev->data;

	return bq35100_reg_read(dev, BQ35100_CMD_SOH, &data->state_of_health, 1);
}

/**
 * Get the accumulated capacity register data
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_acc_capacity(const struct device *dev)
{
	struct bq35100_data *data = dev->data;

	return bq35100_reg_read(dev, BQ35100_CMD_ACCUMULATED_CAPACITY, 
				&data->acc_capacity, 4);
}

/**
 * Set the design capacity register data
 * @param dev - The device structure.
 * @param capacity - the capacity is going to be set.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_set_design_capacity(const struct device *dev, uint16_t capacity)
{
	uint8_t buf[2];
	int status;

	buf[0] = capacity >> 8;
	buf[1] = capacity;

	LOG_DBG("Setting designed cell capacity to %i mAh", capacity);

	status = bq35100_write_extended_data(dev, BQ35100_FLASH_CELL_DESIGN_CAPACITY_MAH,
				buf, 2);

	if (status < 0) {
		LOG_ERR("Unable to set the design capacity");
		return -EIO;
	}

	return 0;
}

/**
 * Get the design capacity register data
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_design_capacity(const struct device *dev)
{
	struct bq35100_data *data = dev->data;

	return bq35100_reg_read(dev, BQ35100_CMD_DESIGN_CAPACITY,
				&data->design_capacity, 2);
}

/**
 * Read CONTROL register from the device to get CONTROL_STATUS.
 * @param dev - The device structure.
 * @param status - The Control reg data
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_status(const struct device *dev, uint16_t *status)
{
	return bq35100_reg_read(dev, BQ35100_CMD_CONTROL,
				status, 2);
}

/**
 * Get the gauge mode from the device.
 * @param dev - The device structure.
 * @return 0 in case of successi negative error code otherwise.
 */
static int bq35100_get_gauge_mode(const struct device *dev)
{
	struct bq35100_data *dev_data = dev->data;
	uint8_t gauge = dev_data->gauge_mode;

	switch (gauge) {
	case BQ35100_ACCUMULATOR_MODE:
		LOG_DBG("Device is in Accumulator Mode");
		break;

	case BQ35100_SOH_MODE:
		LOG_DBG("Device is in SOH Mode");
		break;

	case BQ35100_EOS_MODE:
		LOG_DBG("Device is in EOS Mode");
		break;

	case BQ35100_UNKNOWN_MODE:
		LOG_DBG("Device is in Unkown Gauge Mode");
		break;

	default:
		LOG_ERR("Invalid Gauge Mode");
		break;
	}

	return 0;
}

/**
 * Get the security status from the device.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_security_mode(const struct device *dev)
{
	struct bq35100_data *dev_data = dev->data;
	uint16_t data;

	if (bq35100_get_status(dev, &data) < 0) {
		return -EIO;
	}

	switch ((data >> 13) & 0b011) {
	case BQ35100_SECURITY_UNKNOWN:
		LOG_DBG("Device is in UNKNOWN Security mode");
		break;

	case BQ35100_SECURITY_FULL_ACCESS:
		LOG_DBG("Device is in FULL ACCESS mode");
		break;

	case BQ35100_SECURITY_UNSEALED:
		LOG_DBG("Device is in UNSEALED mode");
		break;

	case BQ35100_SECURITY_SEALED:
		LOG_DBG("Device is in SEALED mode");
		break;

	default:
		LOG_ERR("Invalid device mode");
		return -1;
	}

	dev_data->security_mode = (data >> 13) & 0b011;

	return dev_data->security_mode;
}

/**
 * Get Battery Status.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_battery_status(const struct device *dev)
{
	struct bq35100_data *data = dev->data;
	int status;

	status = bq35100_reg_read(dev, BQ35100_CMD_BATTERY_STATUS,
				&data->battery_status, 1);
	if (status < 0) {
		LOG_ERR("Unable to read Battery Status");
	}

	LOG_DBG("Battery status: 0x%02X", data->battery_status);

	if (((data->battery_status >> 2) & 0b01) == 1) {
		LOG_DBG("ALERT is active");
	}

	if ((data->battery_status & 0b01) == 1) {
		LOG_DBG("Discharge current is detected");
	}

	return 0;
}

/**
 * Set battery alert
 * @param dev - The device structure.
 * @param alert - Battery alert register
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_set_battery_alert(const struct device *dev, uint8_t alert)
{
	uint8_t buf[1];
	int status;
	buf[0] = alert;

	status = bq35100_write_extended_data(dev, BQ35100_FLASH_ALERT_CFG, buf, 1);
	if (status < 0) {
		LOG_ERR("Unable to set battery alert");
		return -EIO;
	}

	LOG_DBG("Battery alert set to: %02X", alert);

	return 0;
}

/**
 * Get the battery alert
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_battery_alert(const struct device *dev)
{
	struct bq35100_data *data = dev->data;
	int status;

	status = bq35100_reg_read(dev, BQ35100_CMD_BATTERY_ALERT,
				&data->battery_alert, 1);
	if (status < 0) {
		LOG_ERR("Unable to read battery alert");
		return -EIO;
	}

	LOG_DBG("Battery Alert: 0x%02X",data->battery_alert);

	if (data->battery_alert & 0b00000001) {
		LOG_DBG("ALERT is triggered because of INITCOMP");
	}
	if (data->battery_alert & 0b00000010) {
		LOG_DBG("ALERT is triggered because of GDONE");
	}
	if (data->battery_alert & 0b00001000) {
		LOG_DBG("ALERT is triggered because of EOS");
	}
	if (data->battery_alert & 0b00010000) {
		LOG_DBG("ALERT is triggered because of SOHLOW");
	}
	if (data->battery_alert & 0b00100000) {
		LOG_DBG("ALERT is triggered because of TEMPHIGH");
	}
	if (data->battery_alert & 0b01000000) {
		LOG_DBG("ALERT is triggered because of TEMPLOW");
	}
	if (data->battery_alert & 0b10000000) {
		LOG_DBG("ALERT is triggered because of BATLOW");
	}

	return 0;
}

/**
 * Whether to use internal temperature sensor for calculations
 * @param dev - The device structure.
 * @param enable - true = internal, false = external
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_use_int_temp(const struct device *dev, bool enable)
{
	uint8_t buf;
	int status;

	status = bq35100_read_extended_data(dev, BQ35100_FLASH_OPERATION_CFG_A, &buf, 1);
	if (status < 0) {
		return -EIO;
	}

	if (!(buf >> 7) != enable) {
		if (enable) {
			buf &= ~0b10000000;

		} else {
            buf |= 0b10000000;
        }

		k_sleep(K_MSEC(50));

		status = bq35100_write_extended_data(dev, BQ35100_FLASH_OPERATION_CFG_A, &buf, 1);
		if (status < 0) {
			return -EIO;
		}
		LOG_DBG("Temperature setting set to %s", enable ? "internal" : "external");

	} else {
		LOG_ERR("Temperature setting already set");
	}

	return 0;
}

#ifdef CONFIG_BQ35100_CALIBRATION_MODE
/**
 * Enter/exit calibration mode
 * @param dev - The device structure.
 * @param enable - determines enter(1) or exit (0)
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_enter_cal_mode(const struct device *dev, bool enable)
{
	k_sleep(K_MSEC(1000));

	if (bq35100_control_reg_write(dev, enable ?
			BQ35100_CTRL_ENTER_CAL : BQ35100_CTRL_EXIT_CAL) < 0) {
		LOG_ERR("Unable to change calibration mode");
		return -EIO;
	}

	if (bq35100_wait_for_status(dev, enable ? BQ35100_CAL_MODE_BIT_MASK : 0,
			BQ35100_CAL_MODE_BIT_MASK, 1000) < 0) {
		LOG_ERR("Calibration error/timeout");
	}

	LOG_DBG("Calibration mode %s", enable ? "enabled" : "disabled");

	return 0;
}

/**
 * 
 * @param dev - The device structure.
 * @param command - command of the register to read
 * @param result - a place to put the read data
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_raw_cal_data(const struct device *dev, uint16_t command, int16_t *result)
{
	uint16_t buf[2];
	uint8_t adc_counter_prev = 0;
	uint8_t counter = 0;
    int32_t avg = 0;

	if (bq35100_enter_cal_mode(dev, true) < 0) {
		LOG_ERR("Unable to enter calibration mode");
		return -EIO;
	}

	while (true) {
		k_sleep(K_MSEC(200));

		if (bq35100_reg_read(dev, BQ35100_CMD_CAL_COUNT, buf, 2) < 0) {
			LOG_ERR("Unable to get cal count");
			return -EIO;
		}

		if (adc_counter_prev == buf[0]) {
            continue;
        }

		adc_counter_prev = buf[0];
		if (bq35100_reg_read(dev, command, buf, 2) < 0) {
			LOG_ERR("Unable to get data");
			return -EIO;
		}

		avg += (buf[1] << 8) + buf[0];
		LOG_DBG("Cal avg: %i", (buf[1] << 8) + buf[0]);

		counter++;
		if (counter == 4) {
			break;
		}
	}

	avg /= 4;
	LOG_DBG("Final cal avg: %i", avg);

	if (result) {
		*result = (int16_t)avg;
	}

	if(bq35100_enter_cal_mode(dev, false) < 0) {
		LOG_ERR("Unable to exit cal mode");
		return -EIO;
	}

	return 0;
}

/**
 * Calibrate with known current
 * @param dev - The device structure.
 * @param voltage - known voltage in [mV].
 * @return 0 in case of success, negative error code otherwise. 
 */
static int bq35100_cal_voltage(const struct device *dev, uint16_t voltage)
{
	uint8_t buf[1];
	int16_t avg_voltage;
    int32_t offset;

	if (bq35100_get_raw_cal_data(dev, BQ35100_CMD_CAL_VOLTAGE, &avg_voltage) < 0) {
		LOG_ERR("Unable to get raw cal data for voltage calibration");
	}

	offset = voltage - avg_voltage;
	LOG_DBG("Voltage calibration difference: %i", offset);

	if (offset < -128 || offset > 127) {
        LOG_ERR("Invalid voltage offset");
        return -EIO;
    }

	buf[0] = (int8_t)offset;
	k_sleep(K_MSEC(1000));

	if (bq35100_write_extended_data(dev, BQ35100_FLASH_PACK_V_OFFSET, buf, 1) < 0) {
		LOG_ERR("Unable to write extenden in cal_voltage()");
		return -EIO;
	}

	return 0;
}

/**
 * Calibrate with known current flowing
 * @param dev - The device structure.
 * @param current - known constant current in [mA].
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_cal_current(const struct device *dev, uint16_t current)
{
	uint8_t buf[4];
    int16_t avg_current;
    int16_t cc_offset;
    int8_t board_offset;

	if (bq35100_read_extended_data(dev, BQ35100_FLASH_CC_OFFSET, buf, 2) < 0) {
		return -EIO;
	}

	cc_offset = (buf[0] << 0) | buf[1];

	if (bq35100_read_extended_data(dev, BQ35100_FLASH_BOARD_OFFSET, buf, 1) < 0) {
		return -EIO;
	}

	board_offset = buf[0];

	k_sleep(K_MSEC(1000));

	if (bq35100_get_raw_cal_data(dev, BQ35100_CMD_CAL_CURRENT, &avg_current) < 0) {
		LOG_ERR("Unable to get raw cal data for current calibration");
		return -EIO;
	}

	float cc_gain = (float)current / (avg_current - (cc_offset + board_offset) / 16);

	/*if (cc_gain < 2.00E-02 || cc_gain > 10.00E+00) {
        LOG_ERR("Invalid CC gain result");
        return -EIO;
    }*/

	float cc_delta = cc_gain * 1193046;

	/*if (cc_delta < 2.98262E+04 || cc_delta > 5.677445E+06) {
        LOG_ERR("Invalid CC delta result");
        return -EIO;
    }*/

	// from now something is really wrong
	bq35100_float_to_DF(cc_gain, buf);
	k_sleep(K_MSEC(1000));

	if (bq35100_write_extended_data(dev, BQ35100_FLASH_CC_GAIN, buf, 2) < 0) {
		return -EIO;
	}

	bq35100_float_to_DF(cc_delta, buf);
	k_sleep(K_MSEC(50));

	if (bq35100_write_extended_data(dev, BQ35100_FLASH_CC_DELTA, buf, 2) < 0) {
		return -EIO;
	}

	return 0;
}

 /**
 * Calibrate internal/external temperature.
 * To determine which tempearture source is selected calling
 * useInternalTemp(true/false) is recommended prior to this.
 * 
 * @param dev - The device structure.
 * @param temp - Temperature in 0.1 Kelvin.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_cal_temp(const struct device *dev, uint16_t temp)
{
	uint8_t buf[1];
    int16_t avg_temp;
    bool external;
    int32_t offset;

	if (bq35100_read_extended_data(dev, BQ35100_FLASH_OPERATION_CFG_A, buf, 1) < 0) {
		return -EIO;
	}

	// Determine which temperature source is selected
    external = (buf[0] & 0b10000000);

	LOG_DBG("Calibrating %s temperature", external ? "external" : "internal");
	k_sleep(K_MSEC(1000));

	if (bq35100_get_raw_cal_data(dev, BQ35100_CMD_CAL_TEMPERATURE, &avg_temp) < 0) {
		LOG_ERR("Unable to get raw cal data for temperature calibration");
		return -EIO;
	}

	offset = (int32_t)temp - avg_temp;

    LOG_DBG("Temperature calibration difference: %i", offset);

    if (offset < -128 || offset > 127) {
        LOG_ERR("Invalid temperature offset");
        return -EIO;
    }

	buf[0] = (int8_t)offset;
	k_sleep(K_MSEC(1000));

	if (bq35100_write_extended_data(dev, external ?
		BQ35100_FLASH_EXT_TEMP_OFFSET : BQ35100_FLASH_INT_TEMP_OFFSET, buf, 1) < 0) {
		return -EIO;
	}

	return 0;
}

/**
 * Floating pointconversion
 * @param val - value to be converted
 * @param result - a place to put the read data (4 bytes long)
 */
void bq35100_float_to_DF(float val, char *result)
{
	int32_t exp = 0;
    float mod_val = val;
    float tmp_val = 0;
    uint8_t data[4];

    if (val < 0.0) {
        mod_val *= -1;
    }

    tmp_val = mod_val;

    tmp_val *= (1 + pow(2, -25));

    if (tmp_val < 0.5) {
        while (tmp_val < 0.5) {
            tmp_val *= 2;
            exp--;
        }

    } else if (tmp_val >= 1.0) {
        while (tmp_val >= 1.0) {
            tmp_val /= 2;
            exp--;
        }
    }

    if (exp > 127) {
        exp = 127;

    } else if (exp < -128) {
        exp = -128;
    }

    tmp_val = pow(2, 8 - exp) * val - 128;

    data[2] = (uint8_t)tmp_val;
    tmp_val = pow(2, 8) * (tmp_val - data[2]);
    data[1] = (uint8_t)tmp_val;
    tmp_val = pow(2, 8) * (tmp_val - data[1]);
    data[0] = (uint8_t)tmp_val;

    if (val < 0.0) {
        data[2] |= 0x80;
    }

    data[3] = exp + 128;

    if (result) {
        memcpy(result, data, 4);
    }
}
#endif

#ifdef CONFIG_PM_DEVICE

/**
 * Gauge Enable/disable
 * @param dev - The device structure.
 * @param enable - True = enable ge pin, false = disable ge pin
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_set_gauge_enable(const struct device *dev, bool enable)
{
	const struct bq35100_config *cfg = dev->config;
	int ret = 0;

	gpio_pin_set(cfg->ge_gpio, cfg->ge_pin, enable);

	return ret;
}

/**
 * Set the Device Power Management State.
 * @param dev - The device structure.
 * @param pm_state - power management state
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_device_pm_ctrl(const struct device *dev,
				  enum pm_device_action action)
{
	int ret = 0;
	// struct bq35100_data *data = dev->data;
	const struct bq35100_config *cfg = dev->config;
	enum pm_device_state curr_state;

	(void)pm_device_state_get(dev, &curr_state);

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		if (curr_state == PM_DEVICE_STATE_OFF) {
			ret = bq35100_set_gauge_enable(dev, true);
			k_sleep(K_MSEC(200));
			ret = bq35100_gauge_start(dev);
		}
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		ret = -ENOTSUP;
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		if (cfg->ge_gpio->name) {
			bq35100_gauge_stop(dev);
			ret = bq35100_set_gauge_enable(dev, false);
		} else {
			LOG_ERR("GE pin not defined");
			ret = -ENOTSUP;
		}
		break;
	default:
		ret = -ENOTSUP;
	}

	return ret;
}
#endif

/**
 * Set attributes for the device.
 * @param dev - The device structure.
 * @param chan - The sensor channel type.
 * @param attr - The sensor attribute.
 * @param value - The sensor attribute value.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_attr_set(const struct device *dev,
			    enum sensor_channel chan,
			    enum sensor_attribute attr,
			    const struct sensor_value *val)
{
	ARG_UNUSED(chan);
	/* here you could set parameters at runtime through main. You can ignore
	   chan for now. */

	switch ((int16_t)attr) {
	case SENSOR_ATTR_BQ35100_EXAMPLE1:
		LOG_DBG("Example function call from main. Parameters are 1. %x, 2. %x", val->val1, val->val2);
	default:
		LOG_DBG("Attribute not supported");
		return -ENOTSUP;
	}
}

/**
 * Read sensor data from the device.
 * @param dev - The device structure.
 * @param cap_data - The sensor value data.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_get_sensor_data(const struct device *dev)
{
	bq35100_get_temp(dev);
	bq35100_get_internal_temp(dev);
	bq35100_get_voltage(dev);
	bq35100_get_avg_current(dev);
	bq35100_get_state_of_health(dev);
	bq35100_get_acc_capacity(dev);
	bq35100_get_design_capacity(dev);

	return 0;
}

/**
 * Read sensor data from the device.
 * @param dev - The device structure.
 * @param chan - The sensor channel type.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_sample_fetch(const struct device *dev,
				enum sensor_channel chan)
{
#ifdef CONFIG_PM_DEVICE
	// struct bq35100_data *data = dev->data;
	enum pm_device_state state;

	(void)pm_device_state_get(dev, &state);
	if (state != PM_DEVICE_STATE_ACTIVE) {
		LOG_ERR("Sample fetch failed, device is not in active mode");
		return -ENXIO;
	}
#endif

	return bq35100_get_sensor_data(dev);
}

/**
 * Get the sensor channel value which was fetched by bq35100_sample_fetch()
 * @param dev - The device structure.
 * @param chan - The sensor channel type.
 * @param val - The sensor channel value.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_channel_get(const struct device *dev,
			       enum sensor_channel chan,
			       struct sensor_value *val)
{
	// const struct bq35100_config *cfg = dev->config;
	struct bq35100_data *data = dev->data;

	switch ((int16_t)chan) {
	case SENSOR_CHAN_GAUGE_TEMP:
		val->val1 = ((uint16_t)(data->temperature) - 2731) / 10;
		val->val2 = ((uint16_t)((data->temperature) - 2731) % 10 * 100000);
		break;
	case SENSOR_CHAN_BQ35100_GAUGE_INT_TEMP:
		val->val1 = ((uint16_t)(data->internal_temperature) - 2731) / 10;
		val->val2 = ((uint16_t)((data->internal_temperature) - 2731) % 10 * 100000);
		break;
	case SENSON_CHAN_BQ35100_GAUGE_DES_CAP:
		val->val1 = (int16_t)data->design_capacity;
		val->val2 = 0;
		break;
	case SENSOR_CHAN_GAUGE_VOLTAGE:
		val->val1 = (uint16_t)data->voltage / 1000;
		val->val2 = ((uint16_t)data->voltage % 1000 * 1000);
		break;
	case SENSOR_CHAN_GAUGE_AVG_CURRENT:
		val->val1 = (int16_t)data->avg_current;
		val->val2 = 0;
		break;
	case SENSOR_CHAN_GAUGE_STATE_OF_HEALTH:
		val->val1 = (uint8_t)data->state_of_health;
		val->val2 = 0;
		break;
	case SENSOR_CHAN_GAUGE_ACCUMULATED_CAPACITY:
		val->val1 = (uint32_t)(ULONG_MAX - data->acc_capacity + 1) / 1000;
		val->val2 = ((uint32_t)(ULONG_MAX - data->acc_capacity + 1) % 1000) * 1000;
		break;
	default:
		LOG_ERR("Channel type not supported.");
		return -ENOTSUP;
	}

	return 0;
}

static const struct sensor_driver_api bq35100_api_funcs = {
	.attr_set = bq35100_attr_set,
	.sample_fetch = bq35100_sample_fetch,
	.channel_get = bq35100_channel_get
};

/**
 * Probe device (Check if it is the correct device by reading the
 * BQ35100_CTRL_DEVICE_TYPE register).
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_probe(const struct device *dev)
{
	int status;
	uint16_t device_type;

	status = bq35100_control_reg_write(dev, BQ35100_CTRL_DEVICE_TYPE);
	if (status < 0) {
		LOG_ERR("Unable to write control register");
		return -EIO;
	}

	k_sleep(K_MSEC(100));
	status = bq35100_control_reg_read(dev, &device_type);
	if (status < 0) {
		LOG_ERR("Unable to read register");
		return -EIO;
	}

	if (device_type != BQ35100_DEVICE_TYPE_ID) {
		LOG_ERR("Wrong device type. Should be 0x%x, but is 0x%x",
			BQ35100_DEVICE_TYPE_ID, device_type);
		return -ENODEV;
	}

	return 0;
}

/**
 * Initialize the Gauge Enable Pin.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_init_ge_pin(const struct device *dev)
{
	const struct bq35100_config *cfg = dev->config;

	if (!device_is_ready(cfg->ge_gpio)) {
		LOG_ERR("%s: ge_gpio device not ready", cfg->ge_gpio->name);
		return -ENODEV;
	}

	gpio_pin_configure(cfg->ge_gpio, cfg->ge_pin,
			   GPIO_OUTPUT_ACTIVE | cfg->ge_flags);

	return 0;
}

/**
 * Initialization of the device.
 * @param dev - The device structure.
 * @return 0 in case of success, negative error code otherwise.
 */
static int bq35100_init(const struct device *dev)
{
	const struct bq35100_config *cfg = dev->config;
	struct bq35100_data *data = dev->data;
	data->gauge_enabled = false;

	if (cfg->ge_gpio->name) {
		if (bq35100_init_ge_pin(dev) < 0) {
			return -ENODEV;
		}
		k_sleep(K_MSEC(1000));
	}

	if (!device_is_ready(cfg->bus)) {
		LOG_ERR("%s: bq35100 device not ready", dev->name);
		return -ENODEV;
	}

	if (bq35100_probe(dev) < 0) {
		return -ENODEV;
	}

	/*if (bq35100_get_battery_status(dev) < 0) {
		return -EIO;
	}*/

	if (bq35100_get_security_mode(dev) < 0) {
		return -EIO;
	}

	// if (bq35100_set_security_mode(dev, BQ35100_SECURITY_UNSEALED)) {
	// 	return EIO;
	// }

	/*if (bq35100_get_battery_alert(dev) < 0) {
	        return -EIO;
	   }*/

	// not sure if this works
	/*if (bq35100_set_battery_alert(dev, 0b0010) < 0) {
	        return -EIO;
	   }*/

	// if (bq35100_set_security_mode(dev, BQ35100_SECURITY_FULL_ACCESS)) {
	//     return EIO;
	// }

	/*if (bq35100_new_battery(dev, 2200) < 0) {
	        return -EIO;
	   }*/


	// if (bq35100_get_gauge_mode(dev) < 0) {
	//     return -EIO;
	// }

	if (bq35100_set_gauge_mode(dev, cfg->gauge_mode)) {
		return EIO;
	}

	if (bq35100_use_int_temp(dev, cfg->temp_sensor_src) < 0) {
		return -EIO;
	}

	if (bq35100_set_design_capacity(dev, cfg->design_capacity) < 0) {
		return -EIO;
	}

	if (bq35100_set_security_mode(dev, BQ35100_SECURITY_SEALED)) {
		return EIO;
	}

	if (bq35100_gauge_start(dev) < 0) {
		return -EIO;
	}

	/*if (bq35100_enter_cal_mode(dev, true) < 0) {
	        return -EIO;
	   }*/

	/*if (bq35100_cal_voltage(dev, 3615) < 0) {
	        return -EIO;
	   }*/

	/*if (bq35100_cal_current(dev, 13) < 0) {
	        return -EIO;
	   }*/

	/*if (bq35100_cal_temp(dev, 2950) < 0) {
	        return -EIO;
	   }*/

	/*if(bq35100_gauge_stop(dev) < 0) {
	     return -EIO;
	   }*/

	return 0;
}

#define BQ35100_GE_PROPS(n)						  \
	.ge_gpio = DEVICE_DT_GET(DT_GPIO_CTLR(DT_DRV_INST(n), ge_gpios)), \
	.ge_pin = DT_INST_GPIO_PIN(n, ge_gpios),			  \
	.ge_flags = DT_INST_GPIO_FLAGS(n, ge_gpios),			  \

#define BQ35100_GE(n)				       \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, ge_gpios), \
		   (BQ35100_GE_PROPS(n)))

#define BQ35100_INIT(n)						  \
	static struct bq35100_data bq35100_data_##n;		  \
								  \
	static const struct bq35100_config bq35100_config_##n = { \
		.bus = DEVICE_DT_GET(DT_INST_BUS(n)),		  \
		.i2c_addr = DT_INST_REG_ADDR(n),		  \
		BQ35100_GE(n)					  \
		.design_capacity = DT_INST_PROP(n, capacity),	  \
		.gauge_mode =					  \
			DT_ENUM_IDX(DT_DRV_INST(n), gauge_mode),  \
		.temp_sensor_src =					  \
			DT_ENUM_IDX(DT_DRV_INST(n), temp_sensor_src), \
	};							  \
								  \
	DEVICE_DT_INST_DEFINE(n,				  \
			      bq35100_init,			  \
			      bq35100_device_pm_ctrl,		  \
			      &bq35100_data_##n,		  \
			      &bq35100_config_##n,		  \
			      POST_KERNEL,			  \
			      CONFIG_SENSOR_INIT_PRIORITY,	  \
			      &bq35100_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(BQ35100_INIT)

/*
 * Copyright (c) 2021 arithmetics ITS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_STEPPER_PCA9629A_H_
#define ZEPHYR_DRIVERS_STEPPER_PCA9629A_H_

#include <drivers/motor.h>
#include <drivers/i2c.h>
#include <drivers/gpio.h>

/*
 * PCA9629A registers definition
 */
#define PCA9629A_MODE           0x00u   /* Mode */
#define PCA9629A_WDTOI          0x01u   /* Watchdog time-out interval */
#define PCA9629A_WDCNTL         0x02u   /* Watchdog control */
#define PCA9629A_IO_CFG         0x03u   /* I/O Configuration */
#define PCA9629A_INTMODE        0x04u   /* Interrupt mode */
#define PCA9629A_MSK            0x05u   /* Mask interrupt */
#define PCA9629A_INTSTAT        0x06u   /* Interrupt status */
#define PCA9629A_IP             0x07u   /* Input port */
#define PCA9629A_INT_MTR_ACT    0x08u   /* Interrupt motor action control */
#define PCA9629A_EXTRASTEPS0    0x09u   /* Count val for extra steps for INTP0 */
#define PCA9629A_EXTRASTEPS1    0x0Au   /* Count val for extra steps for INTP1 */
#define PCA9629A_OP_CFG_PHS     0x0Bu   /* Output port cfg and phase control */
#define PCA9629A_OP_STAT_TO     0x0Cu   /* Output port state and time-out ctrl */
#define PCA9629A_RUCNTL         0x0Du   /* Ramp up control */
#define PCA9629A_RDCTNL         0x0Eu   /* Ramp down control */
#define PCA9629A_PMA            0x0Fu   /* Perform multiple of actions control */
#define PCA9629A_LOOPDLY_CW     0x10u   /* Loop delay time reverse from CW to CCW */
#define PCA9629A_LOOPDLY_CCW    0x11u   /* Loop delay time reverse from CCW to CW */
#define PCA9629A_CWSCOUNTL      0x12u   /* Number of steps CW low byte */
#define PCA9629A_CWSCOUNTH      0x13u   /* Number of steps CW high byte */
#define PCA9629A_CCWSCOUNTL     0x14u   /* Number of steps CCW low byte */
#define PCA9629A_CCWSCOUNTH     0x15u   /* Number of steps CCW high byte */
#define PCA9629A_CWPWL          0x16u   /* Step pulse width CW rotation low byte */
#define PCA9629A_CWPWH          0x17u   /* Step pulse width CW rotation high byte */
#define PCA9629A_CCWPWL         0x18u   /* Step pulse width CCW rotation low byte */
#define PCA9629A_CCWPWH         0x19u   /* Step pulse width CCW rotation high byte */
#define PCA9629A_MCNTL          0x1Au   /* Motor start/stop and rotate direction ctrl */
#define PCA9629A_SUBADR1        0x1Bu   /* I2C-bus subaddress 1 */
#define PCA9629A_SUBADR2        0x1Cu   /* I2C-bus subaddress 2 */
#define PCA9629A_SUBADR3        0x1Du   /* I2C-bus subaddress 3 */
#define PCA9629A_ALLCALLADR     0x1Eu   /* All Call I2C-bus address */
#define PCA9629A_STEPCOUNT0     0x1Fu   /* Step counter byte 0 */
#define PCA9629A_STEPCOUNT1     0x20u   /* Step counter byte 1 */
#define PCA9629A_STEPCOUNT2     0x21u   /* Step counter byte 2 */
#define PCA9629A_STEPCOUNT3     0x22u   /* Step counter byte 3 */

#define PCA9629A_AUTO_INCREMENT          0x80
#define PCA9629A_READ                    0x01u
#define PCA9629A_REG_READ(x)             (((x & 0xFF) << 1) | PCA9629A_READ)
#define PCA9629A_REG_WRITE(x)            ((x & 0xFF) << 1)
#define PCA9629A_TO_I2C_REG(x)           ((x) >> 1)

#define PCA9629A_STEP_PPS_MIN            0.317891
#define PCA9629A_STEP_PPS_MAX            333333  /* 1sec/0.000003s = 333333 pps */
#define PCA9629A_RAMP_FACTOR_MAX         13

/* MODE Field Descriptions */
#define PCA9629A_MODE_SLEEP_MODE_MSK            BIT(6)
#define PCA9629A_MODE_SLEEP_MODE_SET(x)         (((x) & 0x1) << 6)
#define PCA9629A_MODE_DISABLE_INT_PIN_MSK       BIT(5)
#define PCA9629A_MODE_DISABLE_INT_PIN_SET(x)    (((x) & 0x1) << 5)
#define PCA9629A_MODE_OUT_ON_STOP_MSK           BIT(4)
#define PCA9629A_MODE_OUT_ON_STOP_SET(x)        (((x) & 0x1) << 4)
#define PCA9629A_MODE_RESP_SUBADDR1_MSK         BIT(3)
#define PCA9629A_MODE_RESP_SUBADDR1_SET(x)      (((x) & 0x1) << 3)
#define PCA9629A_MODE_RESP_SUBADDR2_MSK         BIT(2)
#define PCA9629A_MODE_RESP_SUBADDR2_SET(x)      (((x) & 0x1) << 2)
#define PCA9629A_MODE_RESP_SUBADDR3_MSK         BIT(1)
#define PCA9629A_MODE_RESP_SUBADDR3_SET(x)      (((x) & 0x1) << 1)
#define PCA9629A_MODE_RESP_ALL_ADDR_MSK         BIT(0)
#define PCA9629A_MODE_RESP_ALL_ADDR_SET(x)      (((x) & 0x1) << 0)

/* WDCNTL Field Descriptions */
#define PCA9629A_WDCNTL_WDMOD_MSK               GENMASK(2, 1)
#define PCA9629A_WDCNTL_WDMOD_SET(x)            (((x) & 0x1) << 1)
#define PCA9629A_WDCNTL_WDEN_MSK                BIT(0)
#define PCA9629A_WDCNTL_WDEN_SET(x)             (((x) & 0x1) << 0)

/* IO_CFG Field Descriptions */
#define PCA9629A_P_OUTPUT_STATE_MSK             GENMASK(7, 4)
#define PCA9629A_P_OUTPUT_STATE_SET(x)          (((x) & 0xF) << 4)
#define PCA9629A_P_OUTPUT_STATE_GET(x)          (((x) >> 4) & 0xF)
#define PCA9629A_P3_MSK                         BIT(3)
#define PCA9629A_P3_SET(x)                      (((x) & 0x1) << 3)
#define PCA9629A_P2_MSK                         BIT(2)
#define PCA9629A_P2_SET(x)                      (((x) & 0x1) << 2)
#define PCA9629A_P1_MSK                         BIT(1)
#define PCA9629A_P1_SET(x)                      (((x) & 0x1) << 1)
#define PCA9629A_P0_MSK                         BIT(0)
#define PCA9629A_P0_SET(x)                      (((x) & 0x1) << 0)

/* INTMODE Field Descriptions*/
#define PCA9629A_INTMOD_PULSE_WIDTH_MSK         GENMASK(6, 4)
#define PCA9629A_INTMOD_PULSE_WIDTH_SET(x)      (((x) & 0xF) << 4)
#define PCA9629A_INTMOD_INT_P3_MSK              BIT(3)
#define PCA9629A_INTMOD_INT_P3_SET(x)           (((x) & 0x1) << 3)
#define PCA9629A_INTMOD_INT_P2_MSK              BIT(2)
#define PCA9629A_INTMOD_INT_P2_SET(x)           (((x) & 0x1) << 2)
#define PCA9629A_INTMOD_INT_P1_MSK              BIT(1)
#define PCA9629A_INTMOD_INT_P1_SET(x)           (((x) & 0x1) << 1)
#define PCA9629A_INTMOD_INT_P0_MSK              BIT(0)
#define PCA9629A_INTMOD_INT_P0_SET(x)           (((x) & 0x1) << 0)

/* MSK Field Descriptions */
#define PCA9629A_MSK_INT_EN_MSK                 BIT(4)
#define PCA9629A_MSK_INT_EN_SET(x)              (((x) & 0x1) << 4)
#define PCA9629A_MSK_INT_EN_P3_MSK              BIT(3)
#define PCA9629A_MSK_INT_EN_P3_SET(x)           (((x) & 0x1) << 3)
#define PCA9629A_MSK_INT_EN_P2_MSK              BIT(2)
#define PCA9629A_MSK_INT_EN_P2_SET(x)           (((x) & 0x1) << 2)
#define PCA9629A_MSK_INT_EN_P1_MSK              BIT(1)
#define PCA9629A_MSK_INT_EN_P1_SET(x)           (((x) & 0x1) << 1)
#define PCA9629A_MSK_INT_EN_P0_MSK              BIT(0)
#define PCA9629A_MSK_INT_EN_P0_SET(x)           (((x) & 0x1) << 0)

/* INTSTAT Field Descriptions */
#define PCA9629A_INTSTAT_WDINT(x)               (((x) >> 5) & 0x1)
#define PCA9629A_INTSTAT_MOTOR_STOP(x)          (((x) >> 4) & 0x1)
#define PCA9629A_INTSTAT_INTP3(x)               (((x) >> 3) & 0x1)
#define PCA9629A_INTSTAT_INTP2(x)               (((x) >> 2) & 0x1)
#define PCA9629A_INTSTAT_INTP1(x)               (((x) >> 1) & 0x1)
#define PCA9629A_INTSTAT_INTP0(x)               (((x) >> 0) & 0x1)

/* IP Field Description */
#define PCA9629A_IP_P_INPUT_STATE(x)            ((x) & 0xF)

/* INT_MTR_ACT Field Description */
#define PCA9629A_INT_MTR_ACT_RESTART_MSK        GENMASK(7, 5)
#define PCA9629A_INT_MTR_ACT_RESTART_SET        (((x) & 0x7) << 5)
#define PCA9629A_INT_MTR_ACT_AUTO_CLEAR_MSK     GENMASK(4, 3)
#define PCA9629A_INT_MTR_ACT_AUTO_CLEAR_SET     (((x) & 0x3) << 3)
#define PCA9629A_INT_MTR_ACT_INT_EN_MSK         BIT(0)
#define PCA9629A_INT_MTR_ACT_INT_EN_SET         (((x) & 0x1) << 0)

/* OP_CFG_PHS Field Description */
#define PCA9629A_OP_CFG_PHS_PHASE_MSK           GENMASK(7, 6)
#define PCA9629A_OP_CFG_PHS_PHASE_SET(x)        (((x) & 0x3) << 6)
#define PCA9629A_OP_CFG_PHS_OUT_CFG_MSK         BIT(4)
#define PCA9629A_OP_CFG_PHS_OUT_CFG_SET(x)      (((x) & 0x1) << 4)
#define PCA9629A_OP_CFG_PHS_OUT_STATE_MSK       GENMASK(3, 0)
#define PCA9629A_OP_CFG_PHS_OUT_STATE_SET(x)    (((x) & 0xF) << 0)

/* OP_STAT_TO Field Description */
#define PCA9629A_OP_STAT_TO_STOP_TIMEOUT_MSK    GENMASK(7, 5)
#define PCA9629A_OP_STAT_TO_STOP_TIMEOUT_SET(x) (((x) & 0x7) << 5)
#define PCA9629A_OP_STAT_TO_OUT_CCW_STATE_MSK    GENMASK(3, 2)
#define PCA9629A_OP_STAT_TO_OUT_CCW_STATE_SET(x) (((x) & 0x3) << 2)
#define PCA9629A_OP_STAT_TO_OUT_CW_STATE_MSK     GENMASK(1, 0)
#define PCA9629A_OP_STAT_TO_OUT_CW_STATE_SET(x)  (((x) & 0x3) << 0)

/* RUCNTL Field Description */
#define PCA9629A_RUCNTL_RAMP_UP_EN_MSK          BIT(5)
#define PCA9629A_RUCNTL_RAMP_UP_EN_SET(x)       (((x) & 0x1) << 5)
#define PCA9629A_RUCNTL_RAMP_UP_RE_EN_MSK       BIT(4)
#define PCA9629A_RUCNTL_RAMP_UP_RE_EN_SET(x)    (((x) & 0x1) << 4)
#define PCA9629A_RUCNTL_RAMP_UP_FACTOR_MSK      GENMASK(3, 0)
#define PCA9629A_RUCNTL_RAMP_UP_FACTOR_SET(x)   (((x) & 0xF) << 0)

/* RDCNTL Field Description */
#define PCA9629A_RDCNTL_RAMP_DOWN_EN_MSK        BIT(5)
#define PCA9629A_RDCNTL_RAMP_DOWN_EN_SET(x)     (((x) & 0x1) << 5)
#define PCA9629A_RDCNTL_RAMP_DOWN_RE_EN_MSK     BIT(4)
#define PCA9629A_RDCNTL_RAMP_DOWN_RE_EN_SET(x)  (((x) & 0x1) << 4)
#define PCA9629A_RDCNTL_RAMP_DOWN_FACTOR_MSK    GENMASK(3, 0)
#define PCA9629A_RDCNTL_RAMP_DOWN_FACTOR_SET(x) (((x) & 0xF) << 0)

/* CWPW Field Description */
#define PCA9629A_CWPW_PRESCALER_MSK             GENMASK(15, 13)
#define PCA9629A_CWPW_PRESCALER_SET(x)          (((x) & 0x7) << 13)
#define PCA9629A_CWPW_STEP_PULSE_WIDTH_MSK      GENMASK(12, 0)
#define PCA9629A_CWPW_STEP_PULSE_WIDTH_SET(x)   (((x) & 0x1FFF) << 0)

/* CCWPW Field Description */
#define PCA9629A_CCWPW_PRESCALER_MSK            GENMASK(15, 13)
#define PCA9629A_CCWPW_PRESCALER_SET(x)         (((x) & 0x7) << 13)
#define PCA9629A_CCWPW_STEP_PULSE_WIDTH_MSK     GENMASK(12, 0)
#define PCA9629A_CCWPW_STEP_PULSE_WIDTH_SET(x)  (((x) & 0x1FFF) << 0)

/* MCNTL Field Description */
#define PCA9629A_MCNTL_MOTOR_STATE_MSK          BIT(7)
#define PCA9629A_MCNTL_MOTOR_STATE_SET(x)       (((x) & 0x1) << 7)
#define PCA9629A_MCNTL_RESTART_MOTOR_MSK        BIT(6)
#define PCA9629A_MCNTL_RESTART_MOTOR_SET(x)     (((x) & 0x1) << 6)
#define PCA9629A_MCNTL_EMERGENCY_STOP_MSK       BIT(5)
#define PCA9629A_MCNTL_EMERGENCY_STOP_SET(x)    (((x) & 0x1) << 5)
#define PCA9629A_MCNTL_IGNORE_START_MSK         BIT(4)
#define PCA9629A_MCNTL_IGNORE_START_SET(x)      (((x) & 0x1) << 4)
#define PCA9629A_MCNTL_IGNORE_POLARITY_MSK      BIT(3)
#define PCA9629A_MCNTL_IGNORE_POLARITY_SET(x)   (((x) & 0x1) << 3)
#define PCA9629A_MCNTL_DIRECTION_CTRL_MSK       GENMASK(1, 0)
#define PCA9629A_MCNTL_DIRECTION_CTRL_SET(x)    (((x) & 0x3) << 0)

/* SUBADR1 Field Description */
#define PCA9629A_SUBADR1_MSK                    GENMASK(7, 1)
#define PCA9629A_SUBADR1_SET(x)                 (((x) & 0x7F) << 1)

/* SUBADR2 Field Description */
#define PCA9629A_SUBADR2_MSK                    GENMASK(7, 1)
#define PCA9629A_SUBADR2_SET(x)                 (((x) & 0x7F) << 1)

/* SUBADR3 Field Description */
#define PCA9629A_SUBADR3_MSK                    GENMASK(7, 1)
#define PCA9629A_SUBADR3_SET(x)                 (((x) & 0x7F) << 1)

/* ALLCALLADR Field Description */
#define PCA9629A_ALLCALLADR_MSK                 GENMASK(7, 1)
#define PCA9629A_ALLCALLADR_SET(x)              (((x) & 0x7F) << 1)


enum pca9629a_motor_states {
	PCA9629A_STOP,
	PCA9629A_START
};

enum pca9629a_pma {
	PCA9629A_RUN_CONTINUOUSLY,
	PCA9629A_RUN_ONCE
};

enum pca9629a_ramp_mode {
	PCA9629A_RAMP_DOWN,
	PCA9629A_RAMP_UP
};

struct pca9629a_data {
	const struct device *bus;
	const struct device *gpio;

	uint32_t *channel_buf;
};


struct pca9629a_config {
	const char *i2c_name;
	uint8_t i2c_addr;

	uint8_t phase;
	uint8_t cw_state_after_stop;
	uint8_t ccw_state_after_stop;
};


#endif  /* ZEPHYR_DRIVERS_STEPPER_PCA9629A_H_ */

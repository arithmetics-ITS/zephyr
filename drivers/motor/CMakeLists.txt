# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_MOTOR_PCA9629A motor_pca9629a.c)

zephyr_library_sources_ifdef(CONFIG_USERSPACE motor_handlers.c)

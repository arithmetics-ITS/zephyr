#
#  Copyright (c) 2021 arithmetics.io
#
#  SPDX-License-Identifier: Apache-2.0
#
zephyr_library()

zephyr_library_sources(ltc4150.c)
zephyr_library_sources_ifdef(CONFIG_LTC4150_TRIGGER ltc4150_trigger.c)
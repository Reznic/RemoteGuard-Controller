#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_MQTT_SAMPLE_GNSS)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gnss.c)
else()
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gnss_stub.c)
endif()

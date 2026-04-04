#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_MQTT_SAMPLE_GNSS_MOCK)
  target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/../tests/utils/gnss_mock.c)
elseif(CONFIG_MQTT_SAMPLE_GNSS)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gnss.c)
endif()

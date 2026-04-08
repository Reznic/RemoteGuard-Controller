#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
if(CONFIG_MQTT_SAMPLE_CAMERA)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/camera.c)
else()
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/camera_stub.c)
endif()

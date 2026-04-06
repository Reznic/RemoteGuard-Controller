#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_MQTT_SAMPLE_GNSS)
  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

  if(CONFIG_BOARD_NATIVE_SIM)
    if(DEFINED ZEPHYR_NRFXLIB_MODULE_DIR)
      zephyr_include_directories(${ZEPHYR_NRFXLIB_MODULE_DIR}/nrf_modem/include)
    else()
      zephyr_include_directories(${WEST_TOPDIR}/nrfxlib/nrf_modem/include)
    endif()
  endif()

  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gnss.c)
  if(CONFIG_BOARD_NATIVE_SIM)
    target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/../tests/utils/gnss_modem_mock.c)
  else()
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gnss_modem_api_nrf.c)
  endif()
endif()

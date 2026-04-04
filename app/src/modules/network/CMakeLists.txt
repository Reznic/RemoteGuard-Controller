#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/network.c)

if(CONFIG_BOARD_NATIVE_SIM)
  target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/../tests/utils/network_native_sim_fixup.c)
endif()

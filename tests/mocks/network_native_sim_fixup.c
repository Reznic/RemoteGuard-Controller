/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Linked only for native_sim (see network/CMakeLists.txt). Overrides the weak
 * network_after_connect_hook() in network.c.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/conn_mgr_monitor.h>

void network_after_connect_hook(void)
{
	conn_mgr_mon_resend_status();
}

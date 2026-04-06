/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * native_sim: no modem. Delivers one synthetic PVT with a valid fix.
 * Coordinates must match tests/integration/mqtt/test_mqtt_gnss_mock_roundtrip.py.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <nrf_modem_gnss.h>

#include "gnss_modem_api.h"


static struct nrf_modem_gnss_pvt_data_frame fake_gps_coords = {
	.latitude = 59.913900,
	.longitude = 10.752200,
	.accuracy = 5.0f,
	.flags = NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID,
};
static gnss_modem_event_cb_t event_cb;
static bool gnss_running;

static void deliver_pvt(struct k_work *work);
K_WORK_DEFINE(pvt_work, deliver_pvt);

static void deliver_pvt(struct k_work *work)
{
	ARG_UNUSED(work);

	if (event_cb != NULL) {
		event_cb(NRF_MODEM_GNSS_EVT_PVT);
	}
}

int gnss_modem_lte_activate_gnss(void)
{
	return 0;
}

int gnss_modem_event_handler_set(gnss_modem_event_cb_t handler)
{
	event_cb = handler;
	return 0;
}

int gnss_modem_use_case_set(uint8_t use_case)
{
	ARG_UNUSED(use_case);
	return 0;
}

int gnss_modem_power_mode_set(uint8_t power_mode)
{
	ARG_UNUSED(power_mode);
	return 0;
}

int gnss_modem_fix_retry_set(uint16_t fix_retry_s)
{
	ARG_UNUSED(fix_retry_s);
	return 0;
}

int gnss_modem_fix_interval_set(uint16_t fix_interval)
{
	ARG_UNUSED(fix_interval);
	return 0;
}

int gnss_modem_start(void)
{
	if (gnss_running) {
		return 0;
	}

	gnss_running = true;

	/* Defer so gnss_start_single_fix() can finish before the callback runs. */
	k_work_submit(&pvt_work);
	return 0;
}

int gnss_modem_stop(void)
{
	gnss_running = false;
	return 0;
}

int gnss_modem_read_pvt(struct nrf_modem_gnss_pvt_data_frame *pvt)
{
	if (pvt == NULL) {
		return -1;
	}

	memcpy(pvt, &fake_gps_coords, sizeof(*pvt));
	return 0;
}

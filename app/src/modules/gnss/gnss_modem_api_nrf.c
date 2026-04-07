/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Real nRF91 modem GNSS / LTE calls (not used on native_sim).
 */

#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>

#include "gnss_modem_api.h"

int gnss_modem_lte_activate_gnss(void)
{
	return lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);
}

int gnss_modem_event_handler_set(gnss_modem_event_cb_t handler)
{
	return (int)nrf_modem_gnss_event_handler_set(
		(nrf_modem_gnss_event_handler_type_t)handler);
}

int gnss_modem_use_case_set(uint8_t use_case)
{
	return (int)nrf_modem_gnss_use_case_set(use_case);
}

int gnss_modem_power_mode_set(uint8_t power_mode)
{
	return (int)nrf_modem_gnss_power_mode_set(power_mode);
}

int gnss_modem_fix_retry_set(uint16_t fix_retry_s)
{
	return (int)nrf_modem_gnss_fix_retry_set(fix_retry_s);
}

int gnss_modem_fix_interval_set(uint16_t fix_interval)
{
	return (int)nrf_modem_gnss_fix_interval_set(fix_interval);
}

int gnss_modem_start(void)
{
	return (int)nrf_modem_gnss_start();
}

int gnss_modem_stop(void)
{
	return (int)nrf_modem_gnss_stop();
}

int gnss_modem_read_pvt(struct nrf_modem_gnss_pvt_data_frame *pvt)
{
	return (int)nrf_modem_gnss_read(pvt, sizeof(*pvt), NRF_MODEM_GNSS_DATA_PVT);
}

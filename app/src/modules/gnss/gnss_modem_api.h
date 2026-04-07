/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Thin port around LTE + nrf_modem_gnss calls used by gnss.c.
 * Production boards link gnss_modem_api_nrf.c; native_sim links tests/mocks/gnss_modem_mock.c.
 * Unit tests can mock these symbols (e.g. fff) without pulling in the modem.
 */

#ifndef GNSS_MODEM_API_H_
#define GNSS_MODEM_API_H_

#include <stdint.h>

#include <nrf_modem_gnss.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Same shape as nrf_modem_gnss_event_handler_type_t. */
typedef void (*gnss_modem_event_cb_t)(int event);

int gnss_modem_lte_activate_gnss(void);
int gnss_modem_event_handler_set(gnss_modem_event_cb_t handler);
int gnss_modem_use_case_set(uint8_t use_case);
int gnss_modem_power_mode_set(uint8_t power_mode);
int gnss_modem_fix_retry_set(uint16_t fix_retry_s);
int gnss_modem_fix_interval_set(uint16_t fix_interval);
int gnss_modem_start(void);
int gnss_modem_stop(void);
int gnss_modem_read_pvt(struct nrf_modem_gnss_pvt_data_frame *pvt);

#ifdef __cplusplus
}
#endif

#endif /* GNSS_MODEM_API_H_ */

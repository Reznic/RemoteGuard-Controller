/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <nrf_modem_gnss.h>
#include <modem/lte_lc.h>
#include <string.h>

#include "message_channel.h"

/* Register log module */
LOG_MODULE_REGISTER(gnss, CONFIG_APP_LOG_LEVEL);

/* Register subscriber */
ZBUS_SUBSCRIBER_DEFINE(gnss, CONFIG_MQTT_SAMPLE_GNSS_MESSAGE_QUEUE_SIZE);

/* GNSS state */
static bool gnss_initialized = false;
static bool gnss_running = false;
static struct nrf_modem_gnss_pvt_data_frame last_pvt;
static K_SEM_DEFINE(pvt_data_sem, 0, 1);

/* Forward declaration */
static void publish_gnss_error(enum gnss_error_type error);

/* GNSS event handler */
static void gnss_event_handler(int event)
{
	int retval;

	switch (event) {
	case NRF_MODEM_GNSS_EVT_PVT:
		retval = nrf_modem_gnss_read(&last_pvt, sizeof(last_pvt), NRF_MODEM_GNSS_DATA_PVT);
		if (retval == 0) {
			k_sem_give(&pvt_data_sem);
		} else {
			LOG_ERR("Failed to read PVT data: %d", retval);
			publish_gnss_error(GNSS_ERROR_PVT_READ_FAILED);
		}
		break;

	case NRF_MODEM_GNSS_EVT_FIX:
		/* Fix obtained, will read PVT data and stop GNSS */
		LOG_INF("GNSS fix obtained");
		break;

	default:
		break;
	}
}

/* Helper function to publish GNSS error */
static void publish_gnss_error(enum gnss_error_type error)
{
	int err = zbus_chan_pub(&GNSS_ERROR_CHAN, &error, K_SECONDS(1));
	if (err) {
		LOG_ERR("Failed to publish GNSS error: %d", err);
	}
}

/* Initialize GNSS */
static int gnss_init(void)
{
	int err;

	if (gnss_initialized) {
		return 0;
	}

	/* Enable GNSS functional mode */
	err = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);
	if (err) {
		LOG_ERR("Failed to activate GNSS functional mode: %d", err);
		publish_gnss_error(GNSS_ERROR_ACTIVATION_FAILED);
		return err;
	}

	/* Set GNSS event handler */
	err = nrf_modem_gnss_event_handler_set(gnss_event_handler);
	if (err) {
		LOG_ERR("Failed to set GNSS event handler: %d", err);
		publish_gnss_error(GNSS_ERROR_EVENT_HANDLER_FAILED);
		return err;
	}

	/* Set use case */
	uint8_t use_case = NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START;
	err = nrf_modem_gnss_use_case_set(use_case);
	if (err) {
		LOG_WRN("Failed to set GNSS use case: %d", err);
		/* Non-critical, don't publish error */
	}

	/* Set power saving mode for low power */
	uint8_t power_mode = NRF_MODEM_GNSS_PSM_DUTY_CYCLING_POWER;
	err = nrf_modem_gnss_power_mode_set(power_mode);
	if (err) {
		LOG_ERR("Failed to set GNSS power saving mode: %d", err);
		publish_gnss_error(GNSS_ERROR_CONFIG_FAILED);
		return err;
	}

	gnss_initialized = true;
	LOG_INF("GNSS initialized");

	return 0;
}

/* Start GNSS for single fix */
static int gnss_start_single_fix(void)
{
	int err;

	if (!gnss_initialized) {
		err = gnss_init();
		if (err) {
			return err;
		}
	}

	if (gnss_running) {
		LOG_WRN("GNSS already running");
		return 0;
	}

	/* Configure for single fix mode */
	err = nrf_modem_gnss_fix_retry_set(CONFIG_MQTT_SAMPLE_GNSS_FIX_TIMEOUT);
	if (err) {
		LOG_ERR("Failed to set GNSS fix retry: %d", err);
		publish_gnss_error(GNSS_ERROR_CONFIG_FAILED);
		return err;
	}

	err = nrf_modem_gnss_fix_interval_set(0);
	if (err) {
		LOG_ERR("Failed to set GNSS fix interval: %d", err);
		publish_gnss_error(GNSS_ERROR_CONFIG_FAILED);
		return err;
	}

	/* Start GNSS */
	err = nrf_modem_gnss_start();
	if (err) {
		LOG_ERR("Failed to start GNSS: %d", err);
		publish_gnss_error(GNSS_ERROR_START_FAILED);
		return err;
	}

	gnss_running = true;
	LOG_INF("GNSS started for single fix");

	return 0;
}

/* Stop GNSS */
static int gnss_stop(void)
{
	int err;

	if (!gnss_running) {
		return 0;
	}

	err = nrf_modem_gnss_stop();
	if (err) {
		LOG_ERR("Failed to stop GNSS: %d", err);
		publish_gnss_error(GNSS_ERROR_STOP_FAILED);
		return err;
	}

	gnss_running = false;
	LOG_INF("GNSS stopped");

	return 0;
}

/* Handle get location command */
static int handle_get_location(void)
{
	int err;
	struct gps_data gps;
	int64_t deadline_ms = k_uptime_get() +
			      (int64_t)K_SECONDS(CONFIG_MQTT_SAMPLE_GNSS_FIX_TIMEOUT).ticks;

	LOG_INF("Handling get location command");

	err = gnss_start_single_fix();
	if (err) {
		LOG_ERR("Failed to start GNSS: %d", err);
		/* Error already published in gnss_start_single_fix */
		return err;
	}

	/* Wait for a valid fix (PVT frames can arrive before FIX_VALID is set). */
	while (true) {
		int64_t now_ms = k_uptime_get();
		int64_t remaining_ms = deadline_ms - now_ms;

		if (remaining_ms <= 0) {
			LOG_ERR("Timeout waiting for GPS fix");
			gnss_stop();
			publish_gnss_error(GNSS_ERROR_TIMEOUT);
			return -ETIMEDOUT;
		}

		err = k_sem_take(&pvt_data_sem, K_MSEC(remaining_ms));
		if (err) {
			LOG_ERR("Timeout waiting for GPS fix");
			gnss_stop();
			publish_gnss_error(GNSS_ERROR_TIMEOUT);
			return -ETIMEDOUT;
		}

		if (last_pvt.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) {
			break;
		}

		LOG_WRN("GPS fix not valid yet (flags=0x%08x), waiting...", last_pvt.flags);
	}

	/* Extract GPS data */
	gps.latitude = last_pvt.latitude;
	gps.longitude = last_pvt.longitude;
	gps.accuracy = (float)last_pvt.accuracy;

	LOG_INF("GPS fix obtained: lat=%d.%06d, lon=%d.%06d, accuracy=%d.%01d m",
		(int)gps.latitude, abs((int)(gps.latitude * 1000000) % 1000000),
		(int)gps.longitude, abs((int)(gps.longitude * 1000000) % 1000000),
		(int)gps.accuracy, abs((int)(gps.accuracy * 10) % 10));

	/* Stop GNSS to save power */
	gnss_stop();

	/* Publish GPS data */
	err = zbus_chan_pub(&GPS_DATA_CHAN, &gps, K_SECONDS(1));
	if (err) {
		LOG_ERR("Failed to publish GPS data: %d", err);
		return err;
	}

	return 0;
}

/* GNSS task */
static void gnss_task(void)
{
	int err;
	const struct zbus_channel *chan;
	enum gnss_cmd cmd;

	LOG_INF("GNSS module initialized");

	while (!zbus_sub_wait(&gnss, &chan, K_FOREVER)) {
		if (chan == &GNSS_CMD_CHAN) {
			err = zbus_chan_read(&GNSS_CMD_CHAN, &cmd, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				continue;
			}

			LOG_INF("Received GNSS command: %d", cmd);

			switch (cmd) {
			case GNSS_CMD_GET_LOCATION:
				err = handle_get_location();
				if (err) {
					LOG_ERR("Failed to get location: %d", err);
				}
				break;

			default:
				LOG_ERR("Unknown GNSS command: %d", cmd);
				break;
			}
		}
	}
}

K_THREAD_DEFINE(gnss_task_id,
		CONFIG_MQTT_SAMPLE_GNSS_THREAD_STACK_SIZE,
		gnss_task, NULL, NULL, NULL, 3, 0, 0);

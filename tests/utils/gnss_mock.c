/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * GNSS test double for integration builds (CONFIG_MQTT_SAMPLE_GNSS_MOCK=y).
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "message_channel.h"

LOG_MODULE_REGISTER(gnss_mock, CONFIG_MQTT_SAMPLE_GNSS_MOCK_LOG_LEVEL);

ZBUS_SUBSCRIBER_DEFINE(gnss, CONFIG_MQTT_SAMPLE_GNSS_MOCK_MESSAGE_QUEUE_SIZE);

static struct gps_data mock_gps_from_kconfig(void)
{
	struct gps_data gps;

	gps.latitude = strtod(CONFIG_MQTT_SAMPLE_GNSS_MOCK_LATITUDE, NULL);
	gps.longitude = strtod(CONFIG_MQTT_SAMPLE_GNSS_MOCK_LONGITUDE, NULL);
	gps.accuracy = (float)strtod(CONFIG_MQTT_SAMPLE_GNSS_MOCK_ACCURACY, NULL);

	return gps;
}

static void gnss_task(void)
{
	int err;
	const struct zbus_channel *chan;
	enum gnss_cmd cmd;

	LOG_INF("GNSS mock initialized");

	while (!zbus_sub_wait(&gnss, &chan, K_FOREVER)) {
		if (chan != &GNSS_CMD_CHAN) {
			continue;
		}

		err = zbus_chan_read(&GNSS_CMD_CHAN, &cmd, K_SECONDS(1));
		if (err) {
			LOG_ERR("zbus_chan_read, error: %d", err);
			continue;
		}

		switch (cmd) {
		case GNSS_CMD_GET_LOCATION: {
			struct gps_data gps = mock_gps_from_kconfig();

			LOG_INF("GNSS mock: publishing fixed fix lat=%f lon=%f", gps.latitude,
				gps.longitude);

			err = zbus_chan_pub(&GPS_DATA_CHAN, &gps, K_SECONDS(1));
			if (err) {
				LOG_ERR("Failed to publish mock GPS data: %d", err);
			}
			break;
		}
		default:
			LOG_WRN("Unknown GNSS command: %d", cmd);
			break;
		}
	}
}

K_THREAD_DEFINE(gnss_task_id, CONFIG_MQTT_SAMPLE_GNSS_MOCK_THREAD_STACK_SIZE, gnss_task, NULL,
		NULL, NULL, 3, 0, 0);

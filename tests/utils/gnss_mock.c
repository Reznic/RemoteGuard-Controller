/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * GNSS test double for integration builds (CONFIG_MQTT_SAMPLE_GNSS_MOCK=y).
 * Fixed coordinates — duplicate in tests/integration/mqtt/test_mqtt_gnss_mock_roundtrip.py.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "message_channel.h"

LOG_MODULE_REGISTER(gnss_mock, CONFIG_MQTT_SAMPLE_GNSS_MOCK_LOG_LEVEL);

ZBUS_SUBSCRIBER_DEFINE(gnss, CONFIG_MQTT_SAMPLE_GNSS_MOCK_MESSAGE_QUEUE_SIZE);

/* Must match GNSS_MOCK_* in test_mqtt_gnss_mock_roundtrip.py */
struct gps_data fake_gps_data = {
	.latitude = 59.913900,
	.longitude = 10.752200,
	.accuracy = 5.0f
};

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

			LOG_INF("GNSS mock: publishing fake lat=%f lon=%f", fake_gps_data.latitude,
				fake_gps_data.longitude);

			err = zbus_chan_pub(&GPS_DATA_CHAN, &fake_gps_data, K_SECONDS(1));
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

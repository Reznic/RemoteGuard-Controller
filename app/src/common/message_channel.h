/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _MESSAGE_CHANNEL_H_
#define _MESSAGE_CHANNEL_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log_ctrl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Macro used to send a message on the FATAL_ERROR_CHANNEL.
 *	   The message will be handled in the error module.
 */
#define SEND_FATAL_ERROR()									\
	int not_used = -1;									\
	if (zbus_chan_pub(&FATAL_ERROR_CHAN, &not_used, K_SECONDS(10))) {			\
		LOG_ERR("Sending a message on the fatal error channel failed, rebooting");	\
		LOG_PANIC();									\
		IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)));					\
	}

enum network_status {
	NETWORK_DISCONNECTED,
	NETWORK_CONNECTED,
};

enum camera_cmd {
	CAMERA_CMD_TAKE_PHOTO,
	CAMERA_CMD_FLASH_ON,
	CAMERA_CMD_FLASH_OFF,
	CAMERA_CMD_CAMERA_ON,
	CAMERA_CMD_CAMERA_OFF,
};

enum camera_error_type {
	CAMERA_ERROR_NRF_INTERNAL,
	CAMERA_ERROR_ESP32_NOT_RESPONDING,
	CAMERA_ERROR_ESP32_CAMERA_ACCESS_FAILED,
};

struct camera_chunk {
	uint8_t data[CONFIG_MQTT_SAMPLE_CAMERA_CHUNK_SIZE];
	uint32_t size;
	uint32_t sequence;
	uint32_t total_chunks;
	uint32_t total_size;
};

enum gnss_cmd {
	GNSS_CMD_GET_LOCATION,
};

enum gnss_error_type {
	GNSS_ERROR_NRF_INTERNAL,
	GNSS_ERROR_INIT_FAILED,
	GNSS_ERROR_ACTIVATION_FAILED,
	GNSS_ERROR_EVENT_HANDLER_FAILED,
	GNSS_ERROR_CONFIG_FAILED,
	GNSS_ERROR_START_FAILED,
	GNSS_ERROR_STOP_FAILED,
	GNSS_ERROR_TIMEOUT,
	GNSS_ERROR_INVALID_FIX,
	GNSS_ERROR_PVT_READ_FAILED,
};

struct gps_data {
	double latitude;
	double longitude;
	float accuracy;
};

ZBUS_CHAN_DECLARE(NETWORK_CHAN, FATAL_ERROR_CHAN, CAMERA_CMD_CHAN, CAMERA_CHUNK_CHAN,
		  CAMERA_ERROR_CHAN, GNSS_CMD_CHAN, GPS_DATA_CHAN, GNSS_ERROR_CHAN);

#ifdef __cplusplus
}
#endif

#endif /* _MESSAGE_CHANNEL_H_ */

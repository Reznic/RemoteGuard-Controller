/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <net/mqtt_client.h>
#include <zephyr/net/mqtt.h>

#include "publish_msg_factory.h"

/* client_id, pub_topic, gps_pub_topic are defined in transport.c */

LOG_MODULE_REGISTER(publish_msg_factory, CONFIG_APP_LOG_LEVEL);

void publish_camera_chunk(struct camera_chunk *chunk)
{
	// int err;
	// static char photo_meta_topic[sizeof(client_id) + 32];
	// static char photo_chunk_topic[sizeof(client_id) + 64];

	// if (chunk->sequence == 0) {
	// 	if (!topic_vfmt(photo_meta_topic, sizeof(photo_meta_topic), "%s/camera/photo/meta",
	// 			client_id)) {
	// 		LOG_ERR("Photo meta topic buffer too small");
	// 		return;
	// 	}

	// 	char meta_buf[128];
	// 	int len = snprintk(meta_buf, sizeof(meta_buf),
	// 			   "{\"id\":%u,\"chunks\":%u,\"size\":%u}",
	// 			   (uint32_t)k_uptime_get_32(), chunk->total_chunks, chunk->total_size);
	// 	if (len < 0 || len >= (int)sizeof(meta_buf)) {
	// 		LOG_ERR("Meta buffer too small");
	// 		return;
	// 	}

	// 	err = publish((const uint8_t *)photo_meta_topic, strlen(photo_meta_topic),
	// 			   meta_buf, (size_t)len);
	// 	if (err) {
	// 		LOG_ERR("Failed to publish photo meta, err: %d", err);
	// 		return;
	// 	}

	// 	LOG_INF("Published photo meta: %s", meta_buf);
	// }

	// err = mqtt_client_publish_str(photo_chunk_topic, chunk->data);
	// if (err) {
	// 	LOG_ERR("Failed to publish photo chunk %u, err: %d", chunk->sequence, err);
	// 	return;
	// }

	// LOG_DBG("Published photo chunk %u/%u (%u bytes)", chunk->sequence + 1, chunk->total_chunks,
	// 	chunk->size);

	// if (chunk->sequence == chunk->total_chunks - 1) {
	// 	LOG_INF("Photo upload complete: %u chunks", chunk->total_chunks);
	// }
}

void publish_gps_data(struct gps_data *gps)
{
	char gps_json[128];
	int len;

	len = snprintk(gps_json, sizeof(gps_json), "{\"lat\":%.6f,\"lon\":%.6f,\"accuracy\":%.1f}",
		       (double)gps->latitude, (double)gps->longitude, (double)gps->accuracy);
	if (len < 0 || len >= (int)sizeof(gps_json)) {
		LOG_ERR("GPS JSON buffer too small");
		return;
	}

	int err = mqtt_client_publish_str(gps_pub_topic, gps_json);
	if (err) {
		LOG_ERR("Failed to publish GPS data, err: %d", err);
		return;
	}

	LOG_INF("Published GPS data: %s", gps_json);
}

void publish_gps_error(enum gnss_error_type error)
{
	const char *error_str;

	switch (error) {
	case GNSS_ERROR_NRF_INTERNAL:
		error_str = "nrf_internal_error";
		break;
	case GNSS_ERROR_INIT_FAILED:
		error_str = "gnss_init_failed";
		break;
	case GNSS_ERROR_ACTIVATION_FAILED:
		error_str = "gnss_activation_failed";
		break;
	case GNSS_ERROR_EVENT_HANDLER_FAILED:
		error_str = "gnss_event_handler_failed";
		break;
	case GNSS_ERROR_CONFIG_FAILED:
		error_str = "gnss_config_failed";
		break;
	case GNSS_ERROR_START_FAILED:
		error_str = "gnss_start_failed";
		break;
	case GNSS_ERROR_STOP_FAILED:
		error_str = "gnss_stop_failed";
		break;
	case GNSS_ERROR_TIMEOUT:
		error_str = "gnss_timeout";
		break;
	case GNSS_ERROR_INVALID_FIX:
		error_str = "gnss_invalid_fix";
		break;
	case GNSS_ERROR_PVT_READ_FAILED:
		error_str = "gnss_pvt_read_failed";
		break;
	default:
		error_str = "unknown";
		break;
	}

	int err = mqtt_client_publish_str("/device/gps/error", error_str);
	if (err) {
		LOG_ERR("Failed to publish GPS error, err: %d", err);
		return;
	}

	LOG_INF("Published GPS error: %s", error_str);
}

void publish_camera_error(enum camera_error_type error)
{
	const char *error_str;

	switch (error) {
	case CAMERA_ERROR_NRF_INTERNAL:
		error_str = "nrf_internal_error";
		break;
	case CAMERA_ERROR_ESP32_NOT_RESPONDING:
		error_str = "cam_module_not_responding";
		break;
	case CAMERA_ERROR_ESP32_CAMERA_ACCESS_FAILED:
		error_str = "camera_access_failed";
		break;
	default:
		error_str = "unknown";
		break;
	}

	int err = mqtt_client_publish_str("/camera/error", error_str);
	if (err) {
		LOG_ERR("Failed to publish camera error, err: %d", err);
		return;
	}

	LOG_INF("Published camera error: %s", error_str);
}

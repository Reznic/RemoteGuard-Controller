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

/* client_id, gps_pub_topic, and publish topics are defined in transport.c */

LOG_MODULE_REGISTER(publish_msg_factory, CONFIG_APP_LOG_LEVEL);


void publish_camera_chunk(struct camera_chunk *chunk)
{
	int err;
	int len;
	static char photo_chunk_topic[sizeof(camera_photo_chunk_base) + 16];
	static bool meta_published = false;
	static uint32_t last_sequence = UINT32_MAX;

	/* Publish meta on first chunk */
	if (chunk->sequence == 0) {
		/* Meta payload: JSON with id, chunks, size */
		char meta_buf[128];
		len = snprintk(meta_buf, sizeof(meta_buf),
			       "{\"id\":%u,\"chunks\":%u,\"size\":%u}",
			       (uint32_t)k_uptime_get_32(), chunk->total_chunks, chunk->total_size);
		if (len < 0 || len >= sizeof(meta_buf)) {
			LOG_ERR("Meta buffer too small");
			return;
		}

		err = mqtt_client_publish_str((char *)camera_photo_meta_topic, meta_buf);
		if (err) {
			LOG_ERR("Failed to publish photo meta, err: %d", err);
			return;
		}

		LOG_INF("Published photo meta: %s", meta_buf);
		meta_published = true;
	}

	/* Publish chunk */
	len = snprintk(photo_chunk_topic, sizeof(photo_chunk_topic), "%s/%u",
			   (char *)camera_photo_chunk_base, chunk->sequence);
	if (len < 0 || len >= sizeof(photo_chunk_topic)) {
		LOG_ERR("Photo chunk topic buffer too small");
		return;
	}

	err = mqtt_client_publish_str(photo_chunk_topic, chunk->data);
	if (err) {
		LOG_ERR("Failed to publish photo chunk %u, err: %d", chunk->sequence, err);
		return;
	}

	LOG_DBG("Published photo chunk %u/%u (%u bytes)", chunk->sequence + 1,
		chunk->total_chunks, chunk->size);

	/* Reset meta flag on last chunk */
	if (chunk->sequence == chunk->total_chunks - 1) {
		meta_published = false;
		last_sequence = UINT32_MAX;
		LOG_INF("Photo upload complete: %u chunks", chunk->total_chunks);
	}
}

/* Picolibc maps snprintk → snprintf; without PICOLIBC_IO_FLOAT, %f becomes "*float*". */
void split_fixed(double v, unsigned int dec, int *ip_out, unsigned int *fp_out, bool *neg_out)
{
	int64_t scale = 1;

	for (unsigned int i = 0; i < dec; i++) {
		scale *= 10;
	}

	double rounded = v * (double)scale + (v >= 0.0 ? 0.5 : -0.5);
	int64_t scaled = (int64_t)rounded;

	*neg_out = scaled < 0;
	if (*neg_out) {
		scaled = -scaled;
	}

	*ip_out = (int)(scaled / scale);
	*fp_out = (unsigned int)(scaled % scale);
}

/* Publish GPS data over MQTT */
void publish_gps_data(struct gps_data *gps)
{
	int err;
	char gps_json[128];
	int len;
	int lat_i, lon_i;
	unsigned int lat_f, lon_f;
	bool lat_neg, lon_neg;
	int acc10;

	split_fixed(gps->latitude, 6, &lat_i, &lat_f, &lat_neg);
	split_fixed(gps->longitude, 6, &lon_i, &lon_f, &lon_neg);

	acc10 = (int)((double)gps->accuracy * 10.0 + 0.5);
	if (acc10 < 0) {
		acc10 = 0;
	}

	len = snprintk(gps_json, sizeof(gps_json),
		       "{\"lat\":%s%d.%06u,\"lon\":%s%d.%06u,\"accuracy\":%d.%d}",
		       lat_neg ? "-" : "", lat_i, lat_f, lon_neg ? "-" : "", lon_i, lon_f,
		       acc10 / 10, acc10 % 10);
	if (len < 0 || len >= sizeof(gps_json)) {
		LOG_ERR("GPS JSON buffer too small");
		return;
	}

	err = mqtt_client_publish_str(gps_pub_topic, gps_json);
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

	int err = mqtt_client_publish_str((char *)gps_error_topic, error_str);
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

	int err = mqtt_client_publish_str((char *)camera_error_topic, error_str);
	if (err) {
		LOG_ERR("Failed to publish camera error, err: %d", err);
		return;
	}

	LOG_INF("Published camera error: %s", error_str);
}

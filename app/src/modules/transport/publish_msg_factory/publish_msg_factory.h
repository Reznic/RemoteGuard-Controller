/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef PUBLISH_MSG_FACTORY_H_
#define PUBLISH_MSG_FACTORY_H_

#include <zephyr/zbus/zbus.h>

#include "message_channel.h"

extern char client_id[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE];

#if IS_ENABLED(CONFIG_MQTT_SAMPLE_GNSS)
extern uint8_t gps_pub_topic[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE +
			    sizeof(CONFIG_MQTT_GPS_DATA_TOPIC)];
extern uint8_t gps_error_topic[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE +
			       sizeof(CONFIG_MQTT_GPS_ERROR_TOPIC)];
#endif

#if IS_ENABLED(CONFIG_MQTT_SAMPLE_CAMERA)
extern uint8_t camera_photo_meta_topic[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE +
				       sizeof(CONFIG_MQTT_CAMERA_PHOTO_META_TOPIC)];
extern uint8_t camera_photo_chunk_base[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE +
				       sizeof(CONFIG_MQTT_CAMERA_PHOTO_CHUNK_TOPIC)];
extern uint8_t camera_error_topic[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE +
				  sizeof(CONFIG_MQTT_CAMERA_ERROR_TOPIC)];
#endif

#if IS_ENABLED(CONFIG_MQTT_SAMPLE_CAMERA)
void publish_camera_chunk(struct camera_chunk *chunk);
void publish_camera_error(enum camera_error_type error);
#endif

#if IS_ENABLED(CONFIG_MQTT_SAMPLE_GNSS)
void publish_gps_data(struct gps_data *gps);
void publish_gps_error(enum gnss_error_type error);
#endif

#endif /* PUBLISH_MSG_FACTORY_H_ */

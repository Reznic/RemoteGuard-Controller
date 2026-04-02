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
extern uint8_t pub_topic[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE +
			sizeof(CONFIG_MQTT_SAMPLE_TRANSPORT_PUBLISH_TOPIC)];
extern uint8_t gps_pub_topic[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE +
			      sizeof(CONFIG_MQTT_GPS_DATA_TOPIC)];

void publish_camera_chunk(struct camera_chunk *chunk);
void publish_gps_data(struct gps_data *gps);
void publish_gps_error(enum gnss_error_type error);
void publish_camera_error(enum camera_error_type error);

#endif /* PUBLISH_MSG_FACTORY_H_ */

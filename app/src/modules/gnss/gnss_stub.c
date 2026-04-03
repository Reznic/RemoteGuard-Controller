/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/zbus/zbus.h>

#include "message_channel.h"

#if !IS_ENABLED(CONFIG_MQTT_SAMPLE_GNSS)
/* Minimal zbus subscriber stub to satisfy GNSS_CMD_CHAN observers when GNSS is disabled. */
ZBUS_SUBSCRIBER_DEFINE(gnss, 1);
#endif

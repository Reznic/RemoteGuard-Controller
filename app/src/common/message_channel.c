/*** Define the zbus message channels for the application ***/
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "message_channel.h"

ZBUS_CHAN_DEFINE(NETWORK_CHAN,
		 enum network_status,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(transport IF_ENABLED(CONFIG_MQTT_SAMPLE_LED, (, led))),
		 ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DEFINE(FATAL_ERROR_CHAN,
		 int,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(error),
		 ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DEFINE(CAMERA_CMD_CHAN,
		 enum camera_cmd,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(camera),
		 ZBUS_MSG_INIT(CAMERA_CMD_TAKE_PHOTO)
);

ZBUS_CHAN_DEFINE(CAMERA_CHUNK_CHAN,
		 struct camera_chunk,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(transport),
		 ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DEFINE(CAMERA_ERROR_CHAN,
		 enum camera_error_type,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(transport),
		 ZBUS_MSG_INIT(CAMERA_ERROR_NRF_INTERNAL)
);

ZBUS_CHAN_DEFINE(GNSS_CMD_CHAN,
		 enum gnss_cmd,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(gnss),
		 ZBUS_MSG_INIT(GNSS_CMD_GET_LOCATION)
);

ZBUS_CHAN_DEFINE(GPS_DATA_CHAN,
		 struct gps_data,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(transport),
		 ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DEFINE(GNSS_ERROR_CHAN,
		 enum gnss_error_type,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS(transport),
		 ZBUS_MSG_INIT(GNSS_ERROR_NRF_INTERNAL)
);

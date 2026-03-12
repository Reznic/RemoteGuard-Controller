#include <zephyr/zbus/zbus.h>
#include "message_channel.h"

#if !IS_ENABLED(CONFIG_MQTT_SAMPLE_CAMERA)
/* Minimal zbus subscriber stub to satisfy CAMERA_CMD_CHAN observers
 * when the camera module is disabled.
 */
ZBUS_SUBSCRIBER_DEFINE(camera, 1);
#endif


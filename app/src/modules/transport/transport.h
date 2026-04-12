#ifndef TRANSPORT_H__
#define TRANSPORT_H__

/**
 * Call this when device is going to sleep / disconnect from network.
 * It will publish a retained "offline_clean" message on the LWT topic,
 *
 * @retval 0 On success.
 * @retval -ENOTCONN MQTT is not connected.
 * @retval -ENOTSUP Last will is disabled (see @c CONFIG_MQTT_CLIENT_LAST_WILL).
 * @return Negative errno or MQTT stack error code on publish/disconnect failure.
 */
int transport_mqtt_clean_offline(void);

#endif /* TRANSPORT_H__ */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/smf.h>
#include <net/mqtt_client.h>
#include <zephyr/net/mqtt.h>
#include <stdbool.h>
#include <string.h>

#include "client_id.h"
#include "message_channel.h"
#include "transport.h"

/* Register log module */
LOG_MODULE_REGISTER(transport, CONFIG_APP_LOG_LEVEL);

/* Register subscriber */
ZBUS_SUBSCRIBER_DEFINE(transport, CONFIG_MQTT_SAMPLE_TRANSPORT_MESSAGE_QUEUE_SIZE);

/* ID for subscribe topic - Used to verify that a subscription succeeded in on_mqtt_suback(). */
#define SUBSCRIBE_TOPIC_ID 2469
#define GET_GPS_TOPIC_ID 2470

/* Forward declarations */
static const struct smf_state state[];
static void connect_work_fn(struct k_work *work);

/* Define connection work - Used to handle reconnection attempts to the MQTT broker */
static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);

/* Define stack_area of application workqueue */
K_THREAD_STACK_DEFINE(stack_area, CONFIG_MQTT_SAMPLE_TRANSPORT_WORKQUEUE_STACK_SIZE);

/* Declare application workqueue. This workqueue is used to call mqtt_client_connect(), and
 * schedule reconnectionn attempts upon network loss or disconnection from MQTT.
 */
static struct k_work_q transport_queue;

/* Internal states */
enum module_state { MQTT_CONNECTED, MQTT_DISCONNECTED };

/* MQTT client ID buffer */
static char client_id[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE];

static uint8_t pub_topic[sizeof(client_id) + sizeof(CONFIG_MQTT_SAMPLE_TRANSPORT_PUBLISH_TOPIC)];
static uint8_t sub_topic[sizeof(client_id) + sizeof(CONFIG_MQTT_SAMPLE_TRANSPORT_SUBSCRIBE_TOPIC)];
static uint8_t get_gps_topic[sizeof(client_id) + sizeof(CONFIG_MQTT_GPS_CMD_TOPIC)];
static uint8_t gps_pub_topic[sizeof(client_id) + sizeof(CONFIG_MQTT_GPS_DATA_TOPIC)];
#if defined(CONFIG_MQTT_CLIENT_LAST_WILL)
static uint8_t lwt_topic[sizeof(client_id) + sizeof(CONFIG_MQTT_CLIENT_LAST_WILL_TOPIC) + 2];
#endif

static atomic_t transport_mqtt_connected = ATOMIC_INIT(0);

/* User defined state object.
 * Used to transfer data between state changes.
 */
static struct s_object {
	/* This must be first */
	struct smf_ctx ctx;

	/* Last channel type that a message was received on */
	const struct zbus_channel *chan;

	/* Network status */
	enum network_status status;

	/* Payload */
	struct payload payload;
} s_obj;

/* Callback handlers from local mqtt_client library.
 * The functions are called whenever specific MQTT packets are received from the broker, or
 * some library state has changed.
 */
static const char *mqtt_connack_reason_str(enum mqtt_conn_return_code rc)
{
	switch (rc) {
	case MQTT_CONNECTION_ACCEPTED:
		return "connection accepted";
	case MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
		return "unacceptable protocol version";
	case MQTT_IDENTIFIER_REJECTED:
		return "client identifier rejected";
	case MQTT_SERVER_UNAVAILABLE:
		return "server unavailable";
	case MQTT_BAD_USER_NAME_OR_PASSWORD:
		return "bad user name or password";
	case MQTT_NOT_AUTHORIZED:
		return "not authorized";
	default:
		return "unknown CONNACK code";
	}
}

static void on_mqtt_connack(enum mqtt_conn_return_code return_code, bool session_present)
{
	if (return_code == MQTT_CONNECTION_ACCEPTED) {
		LOG_INF("MQTT CONNACK: %s (session_present=%d)",
			mqtt_connack_reason_str(return_code), session_present);
		atomic_set(&transport_mqtt_connected, 1);
		smf_set_state(SMF_CTX(&s_obj), &state[MQTT_CONNECTED]);
	} else {
		LOG_ERR("MQTT CONNACK refused: %s (0x%02x), session_present=%d",
			mqtt_connack_reason_str(return_code), return_code, session_present);
		atomic_set(&transport_mqtt_connected, 0);
		smf_set_state(SMF_CTX(&s_obj), &state[MQTT_DISCONNECTED]);
	}
}

static void on_mqtt_disconnect(int result)
{
	atomic_set(&transport_mqtt_connected, 0);
	smf_set_state(SMF_CTX(&s_obj), &state[MQTT_DISCONNECTED]);

	if (result == 0) {
		LOG_INF("MQTT disconnected after clean DISCONNECT");
	} else if (result == -ECONNABORTED) {
		LOG_WRN("MQTT disconnected (aborted), result=%d", result);
	} else {
		LOG_WRN("MQTT disconnected unexpectedly, result=%d", result);
	}
}

/* Helper function to check if payload matches a string exactly */
static bool payload_matches(const struct mqtt_client_buf *payload, const char *str)
{
	size_t str_len = strlen(str);
	return (payload->size == str_len) && (strncmp(payload->ptr, str, str_len) == 0);
}

/* Helper function to check if topic matches a string */
static bool topic_matches(const struct mqtt_client_buf *topic, const char *str)
{
	size_t str_len = strlen(str);
	return (topic->size == str_len) && (strncmp(topic->ptr, str, str_len) == 0);
}

static void on_mqtt_publish(struct mqtt_client_buf topic, struct mqtt_client_buf payload)
{
	enum camera_cmd cmd;
	enum gnss_cmd gnss_cmd;
	int err;

	/* Check for GPS location request */
	if (topic_matches(&topic, (char *)get_gps_topic)) {
		gnss_cmd = GNSS_CMD_GET_LOCATION;
		err = zbus_chan_pub(&GNSS_CMD_CHAN, &gnss_cmd, K_SECONDS(1));
		if (err) {
			LOG_ERR("Failed to publish GNSS command: %d", err);
		} else {
			LOG_INF("Published get_location command to GNSS");
		}
		return;
	}

	/* Check for camera commands */
	if (payload_matches(&payload, "take_photo")) {
		cmd = CAMERA_CMD_TAKE_PHOTO;
		err = zbus_chan_pub(&CAMERA_CMD_CHAN, &cmd, K_SECONDS(1));
		if (err) {
			LOG_ERR("Failed to publish camera command: %d", err);
		} else {
			LOG_INF("Published take_photo command to camera");
		}
	} else if (payload_matches(&payload, "flash_on")) {
		cmd = CAMERA_CMD_FLASH_ON;
		err = zbus_chan_pub(&CAMERA_CMD_CHAN, &cmd, K_SECONDS(1));
		if (err) {
			LOG_ERR("Failed to publish camera command: %d", err);
		} else {
			LOG_INF("Published flash_on command to camera");
		}
	} else if (payload_matches(&payload, "flash_off")) {
		cmd = CAMERA_CMD_FLASH_OFF;
		err = zbus_chan_pub(&CAMERA_CMD_CHAN, &cmd, K_SECONDS(1));
		if (err) {
			LOG_ERR("Failed to publish camera command: %d", err);
		} else {
			LOG_INF("Published flash_off command to camera");
		}
	} else if (payload_matches(&payload, "camera_on")) {
		cmd = CAMERA_CMD_CAMERA_ON;
		err = zbus_chan_pub(&CAMERA_CMD_CHAN, &cmd, K_SECONDS(1));
		if (err) {
			LOG_ERR("Failed to publish camera command: %d", err);
		} else {
			LOG_INF("Published camera_on command to camera");
		}
	} else if (payload_matches(&payload, "camera_off")) {
		cmd = CAMERA_CMD_CAMERA_OFF;
		err = zbus_chan_pub(&CAMERA_CMD_CHAN, &cmd, K_SECONDS(1));
		if (err) {
			LOG_ERR("Failed to publish camera command: %d", err);
		} else {
			LOG_INF("Published camera_off command to camera");
		}
	} else if (payload_matches(&payload, "activate motor")) {
		LOG_INF("Received payload: %.*s on topic: %.*s", payload.size,
							 payload.ptr,
							 topic.size,
							 topic.ptr);
	} else if (payload_matches(&payload, "stop motor")) {
		LOG_INF("Received payload: %.*s on topic: %.*s", payload.size,
							 payload.ptr,
							 topic.size,
							 topic.ptr);
	} else {
		LOG_INF("Received payload: %.*s on topic: %.*s", payload.size,
							 payload.ptr,
							 topic.size,
							 topic.ptr);
	}
}

static void on_mqtt_suback(uint16_t message_id, int result)
{
	if ((message_id == SUBSCRIBE_TOPIC_ID) && (result == 0)) {
		LOG_INF("Subscribed to payload topic");
	} else if ((message_id == GET_GPS_TOPIC_ID) && (result == 0)) {
		LOG_INF("Subscribed to GPS topic");
	} else if (result) {
		LOG_ERR("Topic subscription failed, error: %d", result);
	} else {
		LOG_WRN("Subscribed to unknown topic, id: %d", message_id);
	}
}

/* Local convenience functions */

/* Function that prefixes topics with the Client ID. */
static int topics_prefix(void)
{
	int len;

	len = snprintk(pub_topic, sizeof(pub_topic), "%s/%s", client_id,
		       CONFIG_MQTT_SAMPLE_TRANSPORT_PUBLISH_TOPIC);
	if ((len < 0) || (len >= sizeof(pub_topic))) {
		LOG_ERR("Publish topic buffer too small");
		return -EMSGSIZE;
	}

	len = snprintk(sub_topic, sizeof(sub_topic), "%s/%s", client_id,
		       CONFIG_MQTT_SAMPLE_TRANSPORT_SUBSCRIBE_TOPIC);
	if ((len < 0) || (len >= sizeof(sub_topic))) {
		LOG_ERR("Subscribe topic buffer too small");
		return -EMSGSIZE;
	}

	len = snprintk(get_gps_topic, sizeof(get_gps_topic), "%s/%s", client_id, CONFIG_MQTT_GPS_CMD_TOPIC);
	if ((len < 0) || (len >= sizeof(get_gps_topic))) {
		LOG_ERR("Get GPS topic buffer too small");
		return -EMSGSIZE;
	}

	len = snprintk(gps_pub_topic, sizeof(gps_pub_topic), "%s/%s", client_id, CONFIG_MQTT_GPS_DATA_TOPIC);
	if ((len < 0) || (len >= sizeof(gps_pub_topic))) {
		LOG_ERR("GPS publish topic buffer too small");
		return -EMSGSIZE;
	}

#if defined(CONFIG_MQTT_CLIENT_LAST_WILL)
	len = snprintk(lwt_topic, sizeof(lwt_topic), "%s/%s", client_id,
		       CONFIG_MQTT_CLIENT_LAST_WILL_TOPIC);
	if ((len < 0) || (len >= sizeof(lwt_topic))) {
		LOG_ERR("LWT topic buffer too small");
		return -EMSGSIZE;
	}
#endif

	return 0;
}

static void publish(struct payload *payload)
{
	int err;

	struct mqtt_publish_param param = {
		.message.payload.data = payload->string,
		.message.payload.len = strlen(payload->string),
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message_id = mqtt_client_msg_id_get(),
		.message.topic.topic.utf8 = pub_topic,
		.message.topic.topic.size = strlen(pub_topic),
	};

	err = mqtt_client_publish(&param);
	if (err) {
		LOG_WRN("Failed to send payload, err: %d", err);
		return;
	}

	LOG_INF("Published message: \"%.*s\" on topic: \"%.*s\"", param.message.payload.len,
								  param.message.payload.data,
								  param.message.topic.topic.size,
								  param.message.topic.topic.utf8);
}

/* Publish camera chunk over MQTT */
static void publish_camera_chunk(struct camera_chunk *chunk)
{
	int err;
	static char photo_meta_topic[sizeof(client_id) + 32];
	static char photo_chunk_topic[sizeof(client_id) + 64];
	static bool meta_published = false;
	static uint32_t last_sequence = UINT32_MAX;

	/* Publish meta on first chunk */
	if (chunk->sequence == 0) {
		int len = snprintk(photo_meta_topic, sizeof(photo_meta_topic),
				   "%s/camera/photo/meta", client_id);
		if (len < 0 || len >= sizeof(photo_meta_topic)) {
			LOG_ERR("Photo meta topic buffer too small");
			return;
		}

		/* Meta payload: JSON with id, chunks, size */
		char meta_buf[128];
		len = snprintk(meta_buf, sizeof(meta_buf),
			       "{\"id\":%u,\"chunks\":%u,\"size\":%u}",
			       (uint32_t)k_uptime_get_32(), chunk->total_chunks, chunk->total_size);
		if (len < 0 || len >= sizeof(meta_buf)) {
			LOG_ERR("Meta buffer too small");
			return;
		}

		struct mqtt_publish_param meta_param = {
			.message.payload.data = meta_buf,
			.message.payload.len = len,
			.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
			.message_id = mqtt_client_msg_id_get(),
			.message.topic.topic.utf8 = photo_meta_topic,
			.message.topic.topic.size = strlen(photo_meta_topic),
		};

		err = mqtt_client_publish(&meta_param);
		if (err) {
			LOG_ERR("Failed to publish photo meta, err: %d", err);
			return;
		}

		LOG_INF("Published photo meta: %s", meta_buf);
		meta_published = true;
	}

	/* Publish chunk */
	int len = snprintk(photo_chunk_topic, sizeof(photo_chunk_topic),
			   "%s/camera/photo/chunk/%u", client_id, chunk->sequence);
	if (len < 0 || len >= sizeof(photo_chunk_topic)) {
		LOG_ERR("Photo chunk topic buffer too small");
		return;
	}

	struct mqtt_publish_param chunk_param = {
		.message.payload.data = chunk->data,
		.message.payload.len = chunk->size,
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message_id = mqtt_client_msg_id_get(),
		.message.topic.topic.utf8 = photo_chunk_topic,
		.message.topic.topic.size = strlen(photo_chunk_topic),
	};

	err = mqtt_client_publish(&chunk_param);
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

/* Publish GPS data over MQTT */
static void publish_gps_data(struct gps_data *gps)
{
	int err;
	char gps_json[128];
	int len;

	len = snprintk(gps_json, sizeof(gps_json),
			"{\"lat\":%.6f,\"lon\":%.6f,\"accuracy\":%.1f}",
			gps->latitude, gps->longitude, gps->accuracy);
	if (len < 0 || len >= sizeof(gps_json)) {
		LOG_ERR("GPS JSON buffer too small");
		return;
	}

	struct mqtt_publish_param param = {
		.message.payload.data = (uint8_t *)gps_json,
		.message.payload.len = len,
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message_id = mqtt_client_msg_id_get(),
		.message.topic.topic.utf8 = gps_pub_topic,
		.message.topic.topic.size = strlen(gps_pub_topic),
	};

	err = mqtt_client_publish(&param);
	if (err) {
		LOG_ERR("Failed to publish GPS data, err: %d", err);
		return;
	}

	LOG_INF("Published GPS data: %s", gps_json);
}

/* Publish GPS error over MQTT */
static void publish_gps_error(enum gnss_error_type error)
{
	int err;
	static char error_topic[sizeof(client_id) + 32];
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

	int len = snprintk(error_topic, sizeof(error_topic), "%s/device/gps/error", client_id);
	if (len < 0 || len >= sizeof(error_topic)) {
		LOG_ERR("GPS error topic buffer too small");
		return;
	}

	struct mqtt_publish_param param = {
		.message.payload.data = (uint8_t *)error_str,
		.message.payload.len = strlen(error_str),
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message_id = mqtt_client_msg_id_get(),
		.message.topic.topic.utf8 = error_topic,
		.message.topic.topic.size = strlen(error_topic),
	};

	err = mqtt_client_publish(&param);
	if (err) {
		LOG_ERR("Failed to publish GPS error, err: %d", err);
		return;
	}

	LOG_INF("Published GPS error: %s", error_str);
}

/* Publish camera error over MQTT */
static void publish_camera_error(enum camera_error_type error)
{
	int err;
	static char error_topic[sizeof(client_id) + 32];
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

	int len = snprintk(error_topic, sizeof(error_topic), "%s/camera/error", client_id);
	if (len < 0 || len >= sizeof(error_topic)) {
		LOG_ERR("Error topic buffer too small");
		return;
	}

	struct mqtt_publish_param param = {
		.message.payload.data = (uint8_t *)error_str,
		.message.payload.len = strlen(error_str),
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message_id = mqtt_client_msg_id_get(),
		.message.topic.topic.utf8 = error_topic,
		.message.topic.topic.size = strlen(error_topic),
	};

	err = mqtt_client_publish(&param);
	if (err) {
		LOG_ERR("Failed to publish camera error, err: %d", err);
		return;
	}

	LOG_INF("Published camera error: %s", error_str);
}

static void subscribe(void)
{
	int err;

	/* Subscribe to main topic */
	struct mqtt_topic topics[] = {
		{
			.topic.utf8 = sub_topic,
			.topic.size = strlen(sub_topic),
		},
	};
	struct mqtt_subscription_list list = {
		.list = topics,
		.list_count = ARRAY_SIZE(topics),
		.message_id = SUBSCRIBE_TOPIC_ID,
	};

	LOG_INF("Subscribing to: %s", (char *)list.list[0].topic.utf8);
	err = mqtt_client_subscribe(&list);
	if (err) {
		LOG_ERR("Failed to subscribe to topics, error: %d", err);
		return;
	}

	/* Subscribe to GPS topic */
	struct mqtt_topic gps_topics[] = {
		{
			.topic.utf8 = get_gps_topic,
			.topic.size = strlen(get_gps_topic),
		},
	};
	struct mqtt_subscription_list gps_list = {
		.list = gps_topics,
		.list_count = ARRAY_SIZE(gps_topics),
		.message_id = GET_GPS_TOPIC_ID,
	};

	LOG_INF("Subscribing to: %s", (char *)gps_list.list[0].topic.utf8);
	err = mqtt_client_subscribe(&gps_list);
	if (err) {
		LOG_ERR("Failed to subscribe to GPS topic, error: %d", err);
		return;
	}
}

/* Connect work - Used to establish a connection to the MQTT broker and schedule reconnection
 * attempts.
 */
static void connect_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	int err;
	struct mqtt_client_conn_params conn_params = {
		.hostname.ptr = CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME,
		.hostname.size = strlen(CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME),
		.device_id.ptr = client_id,
		.device_id.size = strlen(client_id),
	};

	err = client_id_get(client_id, sizeof(client_id));
	if (err) {
		LOG_ERR("client_id_get, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	err = topics_prefix();
	if (err) {
		LOG_ERR("topics_prefix, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	err = mqtt_client_connect(&conn_params);
	if (err) {
		LOG_ERR("Failed connecting to MQTT, error code: %d", err);
	}

	k_work_reschedule_for_queue(&transport_queue, &connect_work,
			  K_SECONDS(CONFIG_MQTT_SAMPLE_TRANSPORT_RECONNECTION_TIMEOUT_SECONDS));
}

/* Zephyr State Machine framework handlers */

/* Function executed when the module enters the disconnected state. */
static void disconnected_entry(void *o)
{
	struct s_object *user_object = o;

	/* Reschedule a connection attempt if we are connected to network and we enter the
	 * disconnected state.
	 */
	if (user_object->status == NETWORK_CONNECTED) {
		k_work_reschedule_for_queue(&transport_queue, &connect_work, K_NO_WAIT);
	}
}

/* Function executed when the module is in the disconnected state. */
static void disconnected_run(void *o)
{
	struct s_object *user_object = o;

	if ((user_object->status == NETWORK_DISCONNECTED) && (user_object->chan == &NETWORK_CHAN)) {
		/* If NETWORK_DISCONNECTED is received after the MQTT connection is closed,
		 * we cancel the connect work if it is onging.
		 */
		k_work_cancel_delayable(&connect_work);
	}

	else if ((user_object->status == NETWORK_CONNECTED) && (user_object->chan == &NETWORK_CHAN)) {

		/* Wait for 5 seconds to ensure that the network stack is ready before
		 * attempting to connect to MQTT. This delay is only needed when building for
		 * Wi-Fi.
		 */
		k_work_reschedule_for_queue(&transport_queue, &connect_work, K_SECONDS(5));
	}
}

/* Function executed when the module enters the connected state. */
static void connected_entry(void *o)
{
	LOG_INF("Connected to MQTT broker");
	LOG_INF("Hostname: %s", CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME);
	LOG_INF("Client ID: %s", client_id);
	LOG_INF("Port: %d", CONFIG_MQTT_CLIENT_PORT);
	LOG_INF("TLS: %s", IS_ENABLED(CONFIG_MQTT_LIB_TLS) ? "Yes" : "No");

	ARG_UNUSED(o);

	/* Cancel any ongoing connect work when we enter connected state */
	k_work_cancel_delayable(&connect_work);

	subscribe();
}

/* Function executed when the module is in the connected state. */
static void connected_run(void *o)
{
	struct s_object *user_object = o;

	if ((user_object->status == NETWORK_DISCONNECTED) && (user_object->chan == &NETWORK_CHAN)) {
		/* Explicitly disconnect the MQTT transport when losing network connectivity.
		 * This is to cleanup any internal library state.
		 * The call to this function will cause on_mqtt_disconnect() to be called.
		 */
		(void)mqtt_client_disconnect();
		return;
	}

	if (user_object->chan != &PAYLOAD_CHAN) {
		return;
	}

	publish(&user_object->payload);
}

/* Function executed when the module exits the connected state. */
static void connected_exit(void *o)
{
	ARG_UNUSED(o);

	LOG_INF("Disconnected from MQTT broker");
}

/* Construct state table */
static const struct smf_state state[] = {
	[MQTT_DISCONNECTED] = SMF_CREATE_STATE(disconnected_entry, disconnected_run, NULL,
					       NULL, NULL),
	[MQTT_CONNECTED] = SMF_CREATE_STATE(connected_entry, connected_run, connected_exit,
					    NULL, NULL),
};

static void transport_task(void)
{
	int err;
	const struct zbus_channel *chan;
	enum network_status status;
	struct payload payload;
	struct mqtt_client_cfg cfg = {
		.cb = {
			.on_connack = on_mqtt_connack,
			.on_disconnect = on_mqtt_disconnect,
			.on_publish = on_mqtt_publish,
			.on_suback = on_mqtt_suback,
		},
	};

	/* Initialize and start application workqueue.
	 * This workqueue can be used to offload tasks and/or as a timer when wanting to
	 * schedule functionality using the 'k_work' API.
	 */
	k_work_queue_init(&transport_queue);
	k_work_queue_start(&transport_queue, stack_area,
			   K_THREAD_STACK_SIZEOF(stack_area),
			   K_HIGHEST_APPLICATION_THREAD_PRIO,
			   NULL);

	err = mqtt_client_lib_init(&cfg);
	if (err) {
		LOG_ERR("mqtt_client_lib_init, error: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	/* Set initial state */
	smf_set_initial(SMF_CTX(&s_obj), &state[MQTT_DISCONNECTED]);

	while (!zbus_sub_wait(&transport, &chan, K_FOREVER)) {

		s_obj.chan = chan;

		if (&NETWORK_CHAN == chan) {

			err = zbus_chan_read(&NETWORK_CHAN, &status, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				return;
			}

			s_obj.status = status;
		}

		else if (&PAYLOAD_CHAN == chan) {

			err = zbus_chan_read(&PAYLOAD_CHAN, &payload, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				return;
			}

			s_obj.payload = payload;
		}

		else if (&CAMERA_CHUNK_CHAN == chan) {
			struct camera_chunk chunk;

			err = zbus_chan_read(&CAMERA_CHUNK_CHAN, &chunk, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				continue;
			}

			/* Only publish if MQTT is connected */
			if (s_obj.status == NETWORK_CONNECTED ) {
			    // && smf_get_current_leaf_state(SMF_CTX(&s_obj)) == &state[MQTT_CONNECTED]) {
				publish_camera_chunk(&chunk);
			}
			continue;
		}

		else if (&CAMERA_ERROR_CHAN == chan) {
			enum camera_error_type error;

			err = zbus_chan_read(&CAMERA_ERROR_CHAN, &error, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				continue;
			}

			/* Only publish if MQTT is connected */
			if (s_obj.status == NETWORK_CONNECTED ) {
			    // && smf_get_current_leaf_state(SMF_CTX(&s_obj)) == &state[MQTT_CONNECTED]) {
				publish_camera_error(error);
			}
			continue;
		}

		else if (&GPS_DATA_CHAN == chan) {
			struct gps_data gps;

			err = zbus_chan_read(&GPS_DATA_CHAN, &gps, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				continue;
			}

			/* Only publish if MQTT is connected */
			if (s_obj.status == NETWORK_CONNECTED ) {
			    // && smf_get_current_leaf_state(SMF_CTX(&s_obj)) == &state[MQTT_CONNECTED]) {
				publish_gps_data(&gps);
			}
			continue;
		}

		else if (&GNSS_ERROR_CHAN == chan) {
			enum gnss_error_type error;

			err = zbus_chan_read(&GNSS_ERROR_CHAN, &error, K_SECONDS(1));
			if (err) {
				LOG_ERR("zbus_chan_read, error: %d", err);
				SEND_FATAL_ERROR();
				continue;
			}

			/* Only publish if MQTT is connected */
			if (s_obj.status == NETWORK_CONNECTED ) {
			    // && smf_get_current_leaf_state(SMF_CTX(&s_obj)) == &state[MQTT_CONNECTED]) {
				publish_gps_error(error);
			}
			continue;
		}

		err = smf_run_state(SMF_CTX(&s_obj));
		if (err) {
			LOG_ERR("smf_run_state, error: %d", err);
			SEND_FATAL_ERROR();
			return;
		}
	}
}

#if defined(CONFIG_MQTT_CLIENT_LAST_WILL)
int transport_mqtt_clean_offline(void)
{
	static const char payload[] = "offline_clean";
	int err;

	if (atomic_get(&transport_mqtt_connected) == 0) {
		LOG_WRN("MQTT not connected; cannot publish clean LWT");
		return -ENOTCONN;
	}

	struct mqtt_publish_param param = {
		.message.payload.data = (uint8_t *)payload,
		.message.payload.len = sizeof(payload) - 1,
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message_id = mqtt_client_msg_id_get(),
		.message.topic.topic.utf8 = lwt_topic,
		.message.topic.topic.size = strlen(lwt_topic),
		.dup_flag = 0,
		.retain_flag = 1,
	};

	err = mqtt_client_publish(&param);
	if (err) {
		LOG_ERR("Failed to publish offline_clean: %d", err);
		return err;
	}

	err = mqtt_client_disconnect();
	if (err) {
		LOG_ERR("mqtt_client_disconnect failed: %d", err);
		return err;
	}

	LOG_INF("Published offline_clean on LWT topic; disconnected");
	return 0;
}
#else
int transport_mqtt_clean_offline(void)
{
	return -ENOTSUP;
}
#endif

K_THREAD_DEFINE(transport_task_id,
		CONFIG_MQTT_SAMPLE_TRANSPORT_THREAD_STACK_SIZE,
		transport_task, NULL, NULL, NULL, 3, 0, 0);

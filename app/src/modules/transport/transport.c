#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/smf.h>
#include <net/mqtt_client.h>
#include <zephyr/net/mqtt.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "client_id.h"
#include "message_channel.h"
#include "publish_msg_factory.h"
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
static void mqtt_connect_work_fn(struct k_work *work);

/* Define connection work - Used to handle reconnection attempts to the MQTT broker */
static K_WORK_DELAYABLE_DEFINE(mqtt_connect_work, mqtt_connect_work_fn);

/* Define stack_area of application workqueue */
K_THREAD_STACK_DEFINE(stack_area, CONFIG_MQTT_SAMPLE_TRANSPORT_WORKQUEUE_STACK_SIZE);

/* Declare application workqueue. This workqueue is used to call mqtt_client_connect(), and
 * schedule reconnectionn attempts upon network loss or disconnection from MQTT.
 */
static struct k_work_q transport_queue;

/* Internal states */
enum module_state { MQTT_CONNECTED, MQTT_DISCONNECTED };

/* MQTT client ID buffer (shared with publish_msg_factory.c) */
char client_id[CONFIG_MQTT_SAMPLE_TRANSPORT_CLIENT_ID_BUFFER_SIZE];

static uint8_t sub_topic[sizeof(client_id) + sizeof(CONFIG_MQTT_SAMPLE_TRANSPORT_SUBSCRIBE_TOPIC)];
static uint8_t get_gps_topic[sizeof(client_id) + sizeof(CONFIG_MQTT_GPS_CMD_TOPIC)];
uint8_t gps_pub_topic[sizeof(client_id) + sizeof(CONFIG_MQTT_GPS_DATA_TOPIC)];
#if defined(CONFIG_MQTT_CLIENT_LAST_WILL)
static uint8_t lwt_topic[sizeof(client_id) + sizeof(CONFIG_MQTT_CLIENT_LAST_WILL_TOPIC) + 2];
#endif

static atomic_t transport_mqtt_connected = ATOMIC_INIT(0);

static uint8_t mqtt_broker_reconnect_failures = 0U;
static void retry_connect_to_broker()
{
	if (mqtt_broker_reconnect_failures >= CONFIG_MQTT_SAMPLE_TRANSPORT_RECONNECT_MAX_ATTEMPTS) {
		LOG_ERR("Failed to connect to MQTT broker. Max failed attempts reached, giving up");
		return;
	}
	mqtt_broker_reconnect_failures++;
	LOG_INF("Retrying connecting to mqtt broker");
	k_work_reschedule_for_queue(&transport_queue, &mqtt_connect_work, K_SECONDS(CONFIG_MQTT_SAMPLE_TRANSPORT_RECONNECTION_TIMEOUT_SECONDS));
}

/* User defined state object.
 * Used to transfer data between state changes.
 */
static struct s_object {
	/* This must be first */
	struct smf_ctx ctx;

	/* Last channel type that a message was received on */
	const struct zbus_channel *chan;

	/* Network status */
	enum network_status network_connection_status;
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
		mqtt_broker_reconnect_failures = 0U;
		LOG_INF("MQTT CONNACK: %s (session_present=%d)",
			mqtt_connack_reason_str(return_code), session_present);
		atomic_set(&transport_mqtt_connected, 1);
		smf_set_state(SMF_CTX(&s_obj), &state[MQTT_CONNECTED]);
	} else {
		LOG_ERR("MQTT CONNACK refused: %s (0x%02x), session_present=%d",
			mqtt_connack_reason_str(return_code), return_code, session_present);
		atomic_set(&transport_mqtt_connected, 0);
		smf_set_state(SMF_CTX(&s_obj), &state[MQTT_DISCONNECTED]);
		/* Still in MQTT_DISCONNECTED from before CONNECT; SMF entry does not run again. */
		if (s_obj.network_connection_status == NETWORK_CONNECTED) {
			retry_connect_to_broker();
		}
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
	if (result != 0) {
		LOG_ERR("MQTT Topic subscription failed, error: %d, topic id: %d", result, message_id);
		return;
	}
	switch (message_id) {
	case SUBSCRIBE_TOPIC_ID:
		LOG_INF("MQTT Subscribed to payload topic");
		break;
	case GET_GPS_TOPIC_ID:
		LOG_INF("MQTT Subscribed to GPS topic");
		break;
	default:
		LOG_WRN("MQTT Subscribed to unknown topic id: %d", message_id);
	}
}

/* Local convenience functions */

/* Function that prefixes topics with the Client ID. */
static int topics_prefix(void)
{
	int len;

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
static void mqtt_connect_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	int err;

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

	struct mqtt_client_conn_params conn_params = {
		.hostname.ptr = CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME,
		.hostname.size = strlen(CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME),
		.device_id.ptr = client_id,
		.device_id.size = strlen(client_id),
	};

	err = mqtt_client_connect(&conn_params);
	if (err) {
		LOG_ERR("Failed connecting to MQTT, error code: %d.", err);
		retry_connect_to_broker();
	}
}

/* Zephyr State Machine framework handlers */

/* Function executed when the module enters the disconnected state. */
static void mqtt_disconnected_entry(void *o)
{
	struct s_object *ctx = o;

	/* Reschedule a connection attempt if we are connected to network and we enter the
	 * disconnected state.
	 */
	if (ctx->network_connection_status == NETWORK_CONNECTED) {
		retry_connect_to_broker();
	}
}

/* Function executed when the module is in the disconnected state. */
static void mqtt_disconnected_run(void *o)
{
	struct s_object *ctx = o;

	if ((ctx->network_connection_status == NETWORK_DISCONNECTED) && (ctx->chan == &NETWORK_CHAN)) {
		/* If NETWORK_DISCONNECTED is received after the MQTT connection is closed,
		 * we cancel the connect work if it is onging.
		 */
		k_work_cancel_delayable(&mqtt_connect_work);
	}

	else if ((ctx->network_connection_status == NETWORK_CONNECTED) && (ctx->chan == &NETWORK_CHAN)) {

		/* Wait for 5 seconds to ensure that the network stack is ready before
		 * attempting to connect to MQTT. This delay is only needed when building for
		 * Wi-Fi.
		 */
		k_work_reschedule_for_queue(&transport_queue, &mqtt_connect_work, K_SECONDS(5));
	}
}

/* Function executed when the module enters the connected state. */
static void mqtt_connected_entry(void *o)
{
	LOG_INF("Connected to MQTT broker");
	LOG_INF("Hostname: %s", CONFIG_MQTT_SAMPLE_TRANSPORT_BROKER_HOSTNAME);
	LOG_INF("Client ID: %s", client_id);
	LOG_INF("Port: %d", CONFIG_MQTT_CLIENT_PORT);
	LOG_INF("TLS: %s", IS_ENABLED(CONFIG_MQTT_LIB_TLS) ? "Yes" : "No");

	ARG_UNUSED(o);

	/* Cancel any ongoing connect work when we enter connected state */
	k_work_cancel_delayable(&mqtt_connect_work);

	subscribe();
}

/* Function executed when the module is in the connected state. */
static void mqtt_connected_run(void *o)
{
	struct s_object *ctx = o;

	if ((ctx->network_connection_status == NETWORK_DISCONNECTED) && (ctx->chan == &NETWORK_CHAN)) {
		/* Explicitly disconnect the MQTT transport when losing network connectivity.
		 * This is to cleanup any internal library state.
		 * The call to this function will cause on_mqtt_disconnect() to be called.
		 */
		(void)mqtt_client_disconnect();
		return;
	}
	mqtt_client_publish_str(pub_topic, ctx->payload.string);
}

/* Function executed when the module exits the connected state. */
static void mqtt_connected_exit(void *o)
{
	ARG_UNUSED(o);

	LOG_INF("Disconnected from MQTT broker");
}

/* Construct state table */
static const struct smf_state state[] = {
	[MQTT_DISCONNECTED] = SMF_CREATE_STATE(mqtt_disconnected_entry, mqtt_disconnected_run, NULL,
					       NULL, NULL),
	[MQTT_CONNECTED] = SMF_CREATE_STATE(mqtt_connected_entry, mqtt_connected_run, mqtt_connected_exit,
					    NULL, NULL),
};

static void transport_task(void)
{
	int err;
	const struct zbus_channel *chan;
	enum network_status status;
	struct mqtt_helper_cfg cfg = {
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

			s_obj.network_connection_status = status;
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
			if (s_obj.network_connection_status == NETWORK_CONNECTED ) {
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
			if (s_obj.network_connection_status == NETWORK_CONNECTED ) {
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
			if (s_obj.network_connection_status == NETWORK_CONNECTED ) {
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
			if (s_obj.network_connection_status == NETWORK_CONNECTED ) {
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
	int err;

	if (atomic_get(&transport_mqtt_connected) == 0) {
		LOG_WRN("MQTT not connected; cannot publish clean LWT");
		return -ENOTCONN;
	}

	err = mqtt_client_publish_str_with_qos(lwt_topic, "offline_clean", MQTT_QOS_1_AT_LEAST_ONCE, /*retain*/true, /*dup*/false);
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

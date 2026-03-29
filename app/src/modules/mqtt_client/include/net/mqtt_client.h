/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MQTT_CLIENT__
#define MQTT_CLIENT__

/**
 * @defgroup mqtt_client Local MQTT client library
 * @{
 * @brief Convenience library that simplifies Zephyr MQTT API and socket handling.
 */

#include <stdio.h>
#include <zephyr/net/mqtt.h>

#ifdef __cplusplus
extern "C" {
#endif

enum mqtt_state {
	MQTT_STATE_UNINIT,
	MQTT_STATE_DISCONNECTED,
	MQTT_STATE_TRANSPORT_CONNECTING,
	MQTT_STATE_CONNECTING,
	MQTT_STATE_TRANSPORT_CONNECTED,
	MQTT_STATE_CONNECTED,
	MQTT_STATE_DISCONNECTING,

	MQTT_STATE_COUNT,
};

enum mqtt_client_error {
	/** The received payload is larger than the payload buffer. */
	MQTT_CLIENT_ERROR_MSG_SIZE,
};

struct mqtt_client_buf {
	/** Pointer to buffer. */
	char *ptr;

	/** Size of buffer. */
	size_t size;
};

/** @brief Handler invoked for events that are received from the MQTT stack.
 *	   This callback handler can be used to filter incoming MQTT events before they are
 *	   processed by the MQTT client library.
 *
 *  @param client Pointer to the MQTT client instance.
 *  @param event Pointer to the MQTT event.
 *
 *  @retval 0 if the event is handled by the caller. No further processing of the MQTT event will
 *	      be carried out by the MQTT client library.
 *  @retval 1 if the MQTT client library should continue to process the event after
 *	      the handler returns.
 */
typedef bool (*mqtt_client_on_all_events_t)(struct mqtt_client *const client,
					    const struct mqtt_evt *const event);
typedef void (*mqtt_client_on_connack_t)(enum mqtt_conn_return_code return_code,
					 bool session_present);
typedef void (*mqtt_client_on_disconnect_t)(int result);
typedef void (*mqtt_client_on_publish_t)(struct mqtt_client_buf topic_buf,
					 struct mqtt_client_buf payload_buf);
typedef void (*mqtt_client_on_puback_t)(uint16_t message_id, int result);
typedef void (*mqtt_client_on_suback_t)(uint16_t message_id, int result);
typedef void (*mqtt_client_on_pingresp_t)(void);
typedef void (*mqtt_client_on_error_t)(enum mqtt_client_error error);

struct mqtt_client_cfg {
	struct {
		mqtt_client_on_all_events_t on_all_events;
		mqtt_client_on_connack_t on_connack;
		mqtt_client_on_disconnect_t on_disconnect;
		mqtt_client_on_publish_t on_publish;
		mqtt_client_on_puback_t on_puback;
		mqtt_client_on_suback_t on_suback;
		mqtt_client_on_pingresp_t on_pingresp;
		mqtt_client_on_error_t on_error;
	} cb;
};

struct mqtt_client_conn_params {
	/* The hostname must be null-terminated. */
	struct mqtt_client_buf hostname;
	struct mqtt_client_buf device_id;
	struct mqtt_client_buf user_name;
	struct mqtt_client_buf password;
};

/** @brief Initialize the MQTT client library.
 *
 *  @retval 0 if successful.
 *  @retval -EOPNOTSUPP if operation is not supported in the current state.
 *  @return Otherwise a negative error code.
 */
int mqtt_client_lib_init(struct mqtt_client_cfg *cfg);


/** @brief Connect to an MQTT broker.
 *
 *  @retval 0 if successful.
 *  @retval -EOPNOTSUPP if operation is not supported in the current state.
 *  @return A positive error code in case of DNS error, corresponding to ``getaddrinfo()`` return
 *	    values.
 *  @return Otherwise a negative error code.
 */
int mqtt_client_connect(struct mqtt_client_conn_params *conn_params);

/** @brief Disconnect from the MQTT broker.
 *
 *  @retval 0 if successful.
 *  @retval -EOPNOTSUPP if operation is not supported in the current state.
 *  @return Otherwise a negative error code.
 */
int mqtt_client_disconnect(void);

/** @brief Subscribe to MQTT topics.
 *
 *  @retval 0 if successful.
 *  @retval -EOPNOTSUPP if operation is not supported in the current state.
 *  @return Otherwise a negative error code.
 */
int mqtt_client_subscribe(struct mqtt_subscription_list *sub_list);

/** @brief Publish an MQTT message.
 *
 *  @retval 0 if successful.
 *  @retval -EOPNOTSUPP if operation is not supported in the current state.
 *  @return Otherwise a negative error code.
 */
int mqtt_client_publish(const struct mqtt_publish_param *param);

/** @brief Get a message ID.
 *
 *  @note Will not return 0 as it is reserved for invalid message IDs, see MQTT specification.
 *	  Returned values increment by one for each call.
 *
 *  @return Message ID, positive non-zero value.
 */
uint16_t mqtt_client_msg_id_get(void);

/** @brief Deinitialize library. Must be called when all MQTT operations are done to
 *	   release resources and allow for a new client. The client must be in a disconnected state.
 *
 *  @retval 0 if successful.
 *  @retval -EOPNOTSUPP if operation is not supported in the current state.
 *  @return Otherwise a negative error code.
 */
int mqtt_client_deinit(void);


#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* MQTT_CLIENT__ */

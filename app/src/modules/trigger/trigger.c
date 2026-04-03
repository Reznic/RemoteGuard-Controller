/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#if !IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#endif

#include "message_channel.h"

/* Register log module */
LOG_MODULE_REGISTER(trigger, CONFIG_APP_LOG_LEVEL);

#if !IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)
static const struct gpio_dt_spec user_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
#endif
#if !IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)
static struct gpio_callback user_button_cb;
static struct k_work gnss_request_work;

static void request_gnss_fix(void)
{
	enum gnss_cmd cmd = GNSS_CMD_GET_LOCATION;
	int err;

	err = zbus_chan_pub(&GNSS_CMD_CHAN, &cmd, K_SECONDS(1));
	if (err) {
		LOG_ERR("Failed to request GNSS fix, err: %d", err);
		SEND_FATAL_ERROR();
	}
}

static void gnss_request_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	request_gnss_fix();
}

#endif /* !CONFIG_BOARD_NATIVE_SIM */

#if !IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)
static void user_button_isr(const struct device *port,
			    struct gpio_callback *cb,
			    gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit(&gnss_request_work);
}
#endif

static void trigger_task(void)
{
#if IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)
	/* No GPIO on native_sim integration builds; trigger is inactive. */
	k_sleep(K_FOREVER);
#else
	int err;

	if (!gpio_is_ready_dt(&user_button)) {
		LOG_ERR("User button GPIO not ready");
		SEND_FATAL_ERROR();
		return;
	}

	k_work_init(&gnss_request_work, gnss_request_work_handler);

	err = gpio_pin_configure_dt(&user_button, GPIO_INPUT);
	if (err) {
		LOG_ERR("gpio_pin_configure_dt failed: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	err = gpio_pin_interrupt_configure_dt(&user_button, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("gpio_pin_interrupt_configure_dt failed: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	gpio_init_callback(&user_button_cb, user_button_isr, BIT(user_button.pin));
	err = gpio_add_callback(user_button.port, &user_button_cb);
	if (err) {
		LOG_ERR("gpio_add_callback failed: %d", err);
		SEND_FATAL_ERROR();
		return;
	}

	/* Button-only behavior: wait forever. */
	k_sleep(K_FOREVER);
#endif /* !CONFIG_BOARD_NATIVE_SIM */
}

K_THREAD_DEFINE(trigger_task_id,
		CONFIG_MQTT_SAMPLE_TRIGGER_THREAD_STACK_SIZE,
		trigger_task, NULL, NULL, NULL, 3, 0, 0);

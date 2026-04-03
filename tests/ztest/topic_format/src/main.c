/*
 * Copyright (c) Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * ztest: MQTT topic string layout (same pattern as transport topics_prefix).
 */

#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#define CLIENT_ID   "remoteguard-test-001"
#define PUB_SUFFIX  "my/publish/topic"

ZTEST_SUITE(topic_format_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(topic_format_tests, test_client_prefixed_topic)
{
	uint8_t buf[sizeof(CLIENT_ID) + sizeof(PUB_SUFFIX) + 8];
	static const char expect[] = CLIENT_ID "/" PUB_SUFFIX;
	int len;

	len = snprintk((char *)buf, sizeof(buf), "%s/%s", CLIENT_ID, PUB_SUFFIX);
	zassert_true(len > 0 && len < (int)sizeof(buf), "len=%d", len);
	zassert_equal((size_t)len, strlen(expect), NULL);
	zassert_mem_equal(buf, expect, (size_t)len, NULL);
}

ZTEST(topic_format_tests, test_snprintk_rejects_tiny_buffer)
{
	char tiny[4];
	int len = snprintk(tiny, sizeof(tiny), "%s/%s", CLIENT_ID, PUB_SUFFIX);

	zassert_true(len < 0 || len >= (int)sizeof(tiny), "expected overflow/truncation, got len=%d",
		     len);
}

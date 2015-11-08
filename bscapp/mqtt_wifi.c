#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <nuttx/clock.h>

#include "app_utils.h"
#include "wifi_bridge.h"
#include "mqtt_wifi.h"

struct mqtt_wifi g_mw;

int mqtt_wifi_subscribe(void *h_mw, char *topic, mqtt_msg_handler_t mh)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	if (!mw)
		return -EINVAL;

	bsc_info("subscribing to %s\n", topic);
	mw->wb->msg_handler = mh;
	esp_mqtt_subscribe(mw->wb, topic, 1);

	return OK;
}

int mqtt_wifi_process(void *h_mw)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	if (!mw)
		return -EINVAL;
	esp_process(&mw->wb->ed);
	return OK;
}

int mqtt_wifi_publish(void *h_mw, char *topic, char *payload)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	char msgbuf[MQTT_BUF_MAX_LEN];
	int ret = OK;

	if (mw == NULL || topic == NULL || payload == NULL)
		return -EINVAL;

	bsc_dbg("pub topic: %s\n", topic);
	bsc_dbg("payload  : %s\n", payload);

	bzero(msgbuf, sizeof(msgbuf));
	if (strlen(payload) > MQTT_BUF_MAX_LEN) {
		bsc_warn("can't handle payload length>%d (%d)\n", MQTT_BUF_MAX_LEN, strlen(payload));
		return -EOVERFLOW;
	}

	strcpy(msgbuf, payload);

	/* do publish */
	esp_mqtt_publish(mw->wb, topic, msgbuf, 0, 0);

	/* FIXME set a timeout here? or just ignore published flag? */
	while (!mw->wb->mqtt_published) {
		bsc_info("wait until publish done\n");
	}

	return ret;
}

int mqtt_wifi_connect(void *h_mw)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	if (!mw)
		return -EINVAL;
	bsc_info("connecting to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	esp_mqtt_connect(mw->wb, MQTT_BROKER_IP, MQTT_BROKER_PORT, false);
	while (!mw->wb->mqtt_connected);
	bsc_info("connected to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	return OK;
}

int mqtt_wifi_disconnect(void *h_mw)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	if (!mw)
		return -EINVAL;
	esp_mqtt_disconnect(mw->wb);
	bsc_info("disconnecting mqtt...\n");
	while (mw->wb->mqtt_connected);
	return OK;
}

void *mqtt_wifi_init(struct mqtt_param *param)
{
	struct mqtt_wifi *mw = &g_mw;

	bzero(&g_mw, sizeof(struct mqtt_wifi));

	mw->wb = (struct wifi_bridge *)wifi_bridge_init();
	if (!mw->wb)
		return NULL;
	mw->mp = param;

	esp_reset(&mw->wb->ed);
	while (!esp_ready(&mw->wb->ed))
		bsc_info("wait for esp\n");
	bsc_info("esp ready.\n");

	bsc_info("connecting wifi\n");
	esp_wifi_connect(mw->wb, mw->mp->ssid, mw->mp->psk);
	while (!mw->wb->wifi_connected) {
		esp_process(&mw->wb->ed);
	}
	bsc_info("wifi connected.\n");

	while (!esp_mqtt_setup(mw->wb, mw->mp->uid, mw->mp->username, mw->mp->password, 120, 1))
		bsc_info("wait for mqtt setup\n");
	bsc_info("mqtt setup settled.\n");

	return (void *)&g_mw;
}

void mqtt_wifi_deinit(void **h_mw)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)*h_mw;
	if (!mw)
		return;
	wifi_bridge_deinit(mw->wb);
	*h_mw = NULL;
}

int mqtt_wifi_unit_test(struct mqtt_param *param)
{
	struct mqtt_wifi *mw = NULL;
	int ret;
	bsc_info("in\n");
#if 0
	mw = (struct mqtt_wifi *)mqtt_wifi_init(param);
	if (!mw) {
		bsc_err("failed\n");
		return -EFAULT;
	}

	mqtt_wifi_connect(mw);
	mqtt_wifi_subscribe(mw, "/down/stress", NULL);
	mqtt_wifi_publish(mw, "/down/stress", "data0");

	uint32_t ms = 0;
	char buf[32] = "";
	while (1) {
		mqtt_wifi_process(mw);
		if (millis() - ms > 2000) {
			bzero(buf, sizeof(buf));
			sprintf(buf, "%d", millis());
			bsc_info("time: %d, buf: %s\n", millis(), buf);
			mqtt_wifi_publish(mw, "/down/stress", buf);
			ms = millis();
		}
	}

	mqtt_wifi_deinit(&mw);
#else
	wifi_bridge_unit_test(&g_mw.wb);
#endif

	bsc_info("out\n");
	return ret;
}

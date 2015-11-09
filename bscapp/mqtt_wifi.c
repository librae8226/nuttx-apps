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

int mqtt_wifi_process(void *h_mw)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	if (!mw)
		return -EINVAL;

	pthread_mutex_lock(&mw->lock);

	esp_process(&mw->wb->ed);

	pthread_mutex_unlock(&mw->lock);

	return OK;
}

int mqtt_wifi_subscribe(void *h_mw, char *topic, mqtt_msg_handler_t mh)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	if (!mw)
		return -EINVAL;

	pthread_mutex_lock(&mw->lock);

	mw->wb->msg_handler = mh;
	esp_mqtt_subscribe(mw->wb, topic, 1);

	pthread_mutex_unlock(&mw->lock);

	return OK;
}

int mqtt_wifi_publish(void *h_mw, char *topic, char *payload)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	char msgbuf[MQTT_BUF_MAX_LEN];
	int ret = OK;

	if (mw == NULL || topic == NULL || payload == NULL)
		return -EINVAL;

	pthread_mutex_lock(&mw->lock);

	bzero(msgbuf, sizeof(msgbuf));
	if (strlen(payload) > MQTT_BUF_MAX_LEN) {
		bsc_warn("can't handle payload length>%d (%d)\n", MQTT_BUF_MAX_LEN, strlen(payload));
		return -EOVERFLOW;
	}

	strcpy(msgbuf, payload);

	/* do publish */
	esp_mqtt_publish(mw->wb, topic, msgbuf, 0, 0);
#if 0
	/* FIXME set a timeout here? or just ignore published flag? */
	while (!mw->wb->mqtt_published)
		mqtt_wifi_process(mw);
#endif

	pthread_mutex_unlock(&mw->lock);
	return ret;
}

int mqtt_wifi_connect(void *h_mw)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	if (!mw)
		return -EINVAL;

	pthread_mutex_lock(&mw->lock);

	bsc_info("connecting to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	esp_mqtt_connect(mw->wb, MQTT_BROKER_IP, MQTT_BROKER_PORT, false);
	while (!mw->wb->mqtt_connected)
		mqtt_wifi_process(mw);
	bsc_info("connected to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);

	pthread_mutex_unlock(&mw->lock);

	return OK;
}

int mqtt_wifi_disconnect(void *h_mw)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)h_mw;
	if (!mw)
		return -EINVAL;

	pthread_mutex_lock(&mw->lock);

	esp_mqtt_disconnect(mw->wb);
#if 0 /* we may not need to wait */
	bsc_info("disconnecting mqtt...\n");
	while (mw->wb->mqtt_connected)
		mqtt_wifi_process(mw);
#endif
	pthread_mutex_unlock(&mw->lock);

	return OK;
}

enum wifi_init_stat {
	WIFI_INIT_STATE_0 = 0,
	WIFI_INIT_STATE_1,
	WIFI_INIT_STATE_2,
	WIFI_INIT_STATE_3
};

static int wifi_stat = WIFI_INIT_STATE_0;

void *mqtt_wifi_init(struct mqtt_param *param)
{
	struct mqtt_wifi *mw = &g_mw;
#if 1
	switch (wifi_stat) {
		case WIFI_INIT_STATE_0:
			bzero(&g_mw, sizeof(struct mqtt_wifi));
			mw->wb = (struct wifi_bridge *)wifi_bridge_init();
			if (!mw->wb)
				return NULL;
			mw->mp = param;
			pthread_mutex_init(&mw->lock, NULL);
			esp_reset(&mw->wb->ed);
			wifi_stat = WIFI_INIT_STATE_1;
			break;

		case WIFI_INIT_STATE_1:
			if (!esp_ready(&mw->wb->ed)) {
				bsc_info("wait for esp\n");
			} else {
				bsc_info("connecting wifi\n");
				esp_wifi_connect(mw->wb, mw->mp->ssid, mw->mp->psk);
				wifi_stat = WIFI_INIT_STATE_2;
			}
			break;

		case WIFI_INIT_STATE_2:
			if (!mw->wb->wifi_connected) {
				mqtt_wifi_process(mw);
			} else {
				bsc_info("wifi connected.\n");
				wifi_stat = WIFI_INIT_STATE_3;
			}
			break;

		case WIFI_INIT_STATE_3:
			if (!esp_mqtt_setup(mw->wb, mw->mp->uid, mw->mp->username, mw->mp->password, 30, 0)) {
				bsc_info("wait for mqtt setup\n");
			} else {
				bsc_info("mqtt setup settled.\n");

				/*
				 * Now we should be ready and return the handle.
				 * Should be navigate to state_0 if called again,
				 * which means something is wrong and need re-init.
				 */
				wifi_stat = WIFI_INIT_STATE_0;
				return (void *)&g_mw;
			}
			break;

		default:
			bsc_err("unknow wifi init state: %d\n", wifi_stat);
			break;
	}
	return NULL;
#else
	bzero(&g_mw, sizeof(struct mqtt_wifi));

	mw->wb = (struct wifi_bridge *)wifi_bridge_init();
	if (!mw->wb)
		return NULL;
	mw->mp = param;

	pthread_mutex_init(&mw->lock, NULL);

	esp_reset(&mw->wb->ed);
	while (!esp_ready(&mw->wb->ed))
		bsc_info("wait for esp\n");
	bsc_info("esp ready.\n");

	bsc_info("connecting wifi\n");
	esp_wifi_connect(mw->wb, mw->mp->ssid, mw->mp->psk);
	while (!mw->wb->wifi_connected)
		mqtt_wifi_process(mw);
	bsc_info("wifi connected.\n");

	while (!esp_mqtt_setup(mw->wb, mw->mp->uid, mw->mp->username, mw->mp->password, 30, 0))
		bsc_info("wait for mqtt setup\n");
	bsc_info("mqtt setup settled.\n");

	return (void *)&g_mw;
#endif
}

void mqtt_wifi_deinit(void **h_mw)
{
	struct mqtt_wifi *mw = (struct mqtt_wifi *)*h_mw;
	if (!mw)
		return;
	pthread_mutex_destroy(&mw->lock);
	wifi_bridge_deinit(mw->wb);
	wifi_stat = WIFI_INIT_STATE_0;
	*h_mw = NULL;
}

int mqtt_wifi_unit_test(struct mqtt_param *param)
{
	struct mqtt_wifi *mw = NULL;
	int ret;
	bsc_info("in\n");
#if 1
	mw = (struct mqtt_wifi *)mqtt_wifi_init(param);
	if (!mw) {
		bsc_err("failed\n");
		return -EFAULT;
	}

	mqtt_wifi_connect(mw);
	mqtt_wifi_subscribe(mw, "/down/stress/#", NULL);
	mqtt_wifi_publish(mw, "/down/stress/0", "data0");

	uint32_t ms = 0;
	char buf[32] = "";
	while (1) {
		mqtt_wifi_process(mw);
		if (millis() - ms > 2000) {
			bzero(buf, sizeof(buf));
			sprintf(buf, "time %d", millis());
			bsc_info("buf: %s\n", buf);
			mqtt_wifi_publish(mw, "/down/stress/1", buf);

			bzero(buf, sizeof(buf));
			sprintf(buf, "time %d", millis());
			bsc_info("buf: %s\n", buf);
			mqtt_wifi_publish(mw, "/down/stress/2", buf);

			bzero(buf, sizeof(buf));
			sprintf(buf, "time %d", millis());
			bsc_info("buf: %s\n", buf);
			mqtt_wifi_publish(mw, "/down/stress/3", buf);

			bzero(buf, sizeof(buf));
			sprintf(buf, "time %d", millis());
			bsc_info("buf: %s\n", buf);
			mqtt_wifi_publish(mw, "/down/stress/4", buf);

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

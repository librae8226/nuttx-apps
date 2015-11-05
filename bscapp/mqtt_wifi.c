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

#if 0
int mqtt_wifi_subscribe(struct bscapp_data *priv, char *topic, mqtt_msg_handler_t mh)
{
	bsc_info("subscribing to %s\n", topic);
	while (!priv->exit_mqttsub_thread) {
		/* do process */
		usleep(100000);
	}
	return OK;
}

int mqtt_wifi_publish(struct bscapp_data *priv, char *topic, char *payload)
{
	char msgbuf[MQTT_BUF_MAX_LEN];
	int ret = OK;

	if (topic == NULL || payload == NULL)
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

	return ret;
}

int mqtt_wifi_connect(struct bscapp_data *priv)
{
	bsc_info("connecting to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	bsc_info("connecting mqtt...\n");
	bsc_info("connected to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	return OK;
}

int mqtt_wifi_disconnect(struct bscapp_data *priv)
{
	return OK;
}
#endif

void *mqtt_wifi_init(void)
{
	/* TODO malloc? */
	return (void *)&g_mw;
}

void mqtt_wifi_deinit(void **h_mw)
{
	*h_mw = NULL;
}

int mqtt_wifi_unit_test(void **h_mw)
{
	struct mqtt_wifi *mw = NULL;
	int ret;
	bsc_info("in\n");

	mw = (struct mqtt_wifi *)mqtt_wifi_init();
	if (!mw) {
		bsc_err("failed");
		return -EFAULT;
	}

	*h_mw = (void *)mw;

	ret = wifi_bridge_unit_test(&mw->h_wb);

	mqtt_wifi_deinit(h_mw);

	bsc_info("out\n");
	return ret;
}

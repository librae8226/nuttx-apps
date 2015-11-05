#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <debug.h>
#include <errno.h>
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

#include <apps/netutils/netlib.h>
#include <apps/netutils/webclient.h>
#include <apps/netutils/MQTTClient.h>
#include "app_utils.h"
#include "mqtt_eth.h"
#include "bscapp.h"

static mqtt_msg_handler_t g_eth_mh = NULL;

static void mqtt_eth_msg_handler(MessageData *md)
{
	MQTTMessage *message = md->message;
	char *topic = md->topicName->lenstring.data;
	int topic_len = md->topicName->lenstring.len;
	char *payload = (char *)message->payload;
	int payload_len = message->payloadlen;

	if (g_eth_mh)
		g_eth_mh(topic, topic_len, payload, payload_len);
	else
		bsc_warn("no msg handler!\n");
}

int mqtt_eth_subscribe(struct bscapp_data *priv, char *topic, mqtt_msg_handler_t mh)
{
	int ret = OK;
	int val;

	g_eth_mh = mh;

	do {
		ret = MQTTSubscribe(&priv->c, topic, QOS1, mqtt_eth_msg_handler);
		if (ret < 0) {
			bsc_warn("subscribe failed: %d, try again\n", ret);
			usleep(100000);
		} else {
			bsc_info("subscribe ok\n");
		}
	} while (ret < 0);

	ret = sem_getvalue(&priv->sem, &val);
	if (ret < 0)
		bsc_dbg("could not get semaphore value\n");
	else
		bsc_dbg("semaphore value: %d\n", val);

	if (val < 0) {
		bsc_dbg("posting semaphore\n");
		ret = sem_post(&priv->sem);
		if (ret != 0)
			bsc_err("sem_post failed\n");
	} else {
		bsc_dbg("val > 0\n");
	}

	while (!priv->exit_mqttsub_thread) {
#ifdef MQTT_MUTEX_ENABLE
		ret = pthread_mutex_lock(&priv->mutex_mqtt);
		if (ret != 0)
			bsc_err("failed to lock mutex: %d\n", ret);
#endif

		ret = MQTTYield(&priv->c, 100);
		if (ret < 0)
			bsc_dbg("process ret: %d\n", ret);

#ifdef MQTT_MUTEX_ENABLE
		ret = pthread_mutex_unlock(&priv->mutex_mqtt);
		if (ret != 0)
			bsc_err("failed to unlock mutex: %d\n", ret);
#endif
	}

	return OK;
}

int mqtt_eth_publish(struct bscapp_data *priv, char *topic, char *payload)
{
	char msgbuf[MQTT_BUF_MAX_LEN];
	MQTTMessage msg;
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
	msg.qos = QOS0;
	msg.retained = false;
	msg.dup = false;
	msg.payload = (void *)msgbuf;
	msg.payloadlen = strlen(msgbuf) + 1;

#ifdef MQTT_MUTEX_ENABLE
	ret = pthread_mutex_lock(&priv->mutex_mqtt);
	if (ret != 0)
		bsc_err("failed to lock mutex: %d\n", ret);
#endif

	ret = MQTTPublish(&priv->c, topic, &msg);
	if (ret != 0) {
		bsc_err("error publish, ret: %d\n", ret);
	} else {
		if (msg.qos > 0) {
			ret = MQTTYield(&priv->c, 100);
			if (ret < 0)
				bsc_dbg("yield ret: %d\n", ret);
		}
	}

#ifdef MQTT_MUTEX_ENABLE
	ret = pthread_mutex_unlock(&priv->mutex_mqtt);
	if (ret != 0)
		bsc_err("failed to unlock mutex: %d\n", ret);
#endif

	return ret;
}

int mqtt_eth_connect(struct bscapp_data *priv)
{
	int ret;

	bsc_info("connecting to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	NewNetwork(&priv->n);
	while (ConnectNetwork(&priv->n, MQTT_BROKER_IP, MQTT_BROKER_PORT) != OK) {
		bsc_warn("failed to connect network, try again\n");
		usleep(100000);
	}
	MQTTClient(&priv->c, &priv->n, MQTT_CMD_TIMEOUT,
			priv->buf, MQTT_BUF_MAX_LEN, priv->readbuf, MQTT_BUF_MAX_LEN);

	MQTTPacket_connectData conn_data = MQTTPacket_connectData_initializer;
	conn_data.willFlag = 0;
	conn_data.MQTTVersion = 3;
	conn_data.clientID.cstring = priv->uid;
	conn_data.username.cstring = NULL;
	conn_data.password.cstring = NULL;
	conn_data.keepAliveInterval = 30;
	conn_data.cleansession = 1;

	bsc_info("connecting mqtt...\n");
	do {
		ret = MQTTConnect(&priv->c, &conn_data);
		if (ret < 0) {
			bsc_warn("connect failed: %d, try again\n", ret);
			usleep(100000);
		} else {
			bsc_info("connected to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
		}
	} while (ret < 0);
	return OK;
}

int mqtt_eth_disconnect(struct bscapp_data *priv)
{
	MQTTDisconnect(&priv->c);
	priv->n.disconnect(&priv->n);
	return OK;
}

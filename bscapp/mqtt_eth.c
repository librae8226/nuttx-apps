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

static struct mqtt_eth g_me;

/* external functions */
int nsh_netinit(void);

static void mqtt_eth_msg_handler(MessageData *md)
{
	MQTTMessage *message = md->message;
	char *topic = md->topicName->lenstring.data;
	int topic_len = md->topicName->lenstring.len;
	char *payload = (char *)message->payload;
	int payload_len = message->payloadlen;

	if (g_me.msg_handler)
		g_me.msg_handler(topic, topic_len, payload, payload_len);
	else
		bsc_warn("no msg handler!\n");
}

int mqtt_eth_subscribe(void *h_me, char *topic, mqtt_msg_handler_t mh)
{
	struct mqtt_eth *me = (struct mqtt_eth *)h_me;
	int ret = OK;

	if (!me)
		return -EINVAL;

	me->msg_handler = mh;

	do {
		ret = MQTTSubscribe(&me->c, topic, QOS1, mqtt_eth_msg_handler);
		if (ret < 0) {
			bsc_warn("subscribe failed: %d, try again\n", ret);
			usleep(100000);
		} else {
			bsc_info("subscribe ok\n");
		}
	} while (ret < 0);

	return ret;
}

int mqtt_eth_process(void *h_me)
{
	int ret = OK;
	struct mqtt_eth *me = (struct mqtt_eth *)h_me;
	if (!me)
		return -EINVAL;

#ifdef MQTT_MUTEX_ENABLE
	pthread_mutex_lock(&me->mutex_mqtt);
#endif
	ret = MQTTYield(&me->c, 100);
#ifdef MQTT_MUTEX_ENABLE
	pthread_mutex_unlock(&me->mutex_mqtt);
#endif
	return ret;
}

int mqtt_eth_publish(void *h_me, char *topic, char *payload)
{
	struct mqtt_eth *me = (struct mqtt_eth *)h_me;
	char msgbuf[MQTT_BUF_MAX_LEN];
	MQTTMessage msg;
	int ret = OK;

	if (!me)
		return -EINVAL;

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
	ret = pthread_mutex_lock(&me->mutex_mqtt);
	if (ret != 0)
		bsc_err("failed to lock mutex: %d\n", ret);
#endif

	ret = MQTTPublish(&me->c, topic, &msg);
	if (ret != 0) {
		bsc_err("error publish, ret: %d\n", ret);
	} else {
		if (msg.qos > 0) {
			ret = MQTTYield(&me->c, 100);
			if (ret < 0)
				bsc_dbg("yield ret: %d\n", ret);
		}
	}

#ifdef MQTT_MUTEX_ENABLE
	ret = pthread_mutex_unlock(&me->mutex_mqtt);
	if (ret != 0)
		bsc_err("failed to unlock mutex: %d\n", ret);
#endif

	return ret;
}

int mqtt_eth_connect(void *h_me)
{
	int ret;

	struct mqtt_eth *me = (struct mqtt_eth *)h_me;
	if (!me)
		return -EINVAL;

	bsc_info("param: 0x%p\n", me->mp);
	bsc_info("wbuf: 0x%p\n", me->mp->wbuf);
	bsc_info("rbuf: 0x%p\n", me->mp->rbuf);

	bsc_info("connecting to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	NewNetwork(&me->n);
	while (ConnectNetwork(&me->n, MQTT_BROKER_IP, MQTT_BROKER_PORT) != OK) {
		bsc_warn("failed to connect network, try again\n");
		usleep(100000);
	}
	MQTTClient(&me->c, &me->n, MQTT_CMD_TIMEOUT,
		   me->mp->wbuf, MQTT_BUF_MAX_LEN,
		   me->mp->rbuf, MQTT_BUF_MAX_LEN);

	MQTTPacket_connectData conn_data = MQTTPacket_connectData_initializer;
	conn_data.willFlag = 0;
	conn_data.MQTTVersion = 3;
	conn_data.clientID.cstring = (char *)me->mp->uid;
	conn_data.username.cstring = (char *)me->mp->username;
	conn_data.password.cstring = (char *)me->mp->password;
	conn_data.keepAliveInterval = 30;
	conn_data.cleansession = 1;

	bsc_info("connecting mqtt...\n");
	do {
		ret = MQTTConnect(&me->c, &conn_data);
		if (ret < 0) {
			bsc_warn("connect failed: %d, try again\n", ret);
			usleep(100000);
		} else {
			bsc_info("connected to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
		}
	} while (ret < 0);
	return OK;
}

int mqtt_eth_disconnect(void *h_me)
{
	struct mqtt_eth *me = (struct mqtt_eth *)h_me;
	if (!me)
		return -EINVAL;
	MQTTDisconnect(&me->c);
	me->n.disconnect(&me->n);
	return OK;
}

void *mqtt_eth_init(struct mqtt_param *param)
{
	struct mqtt_eth *me = &g_me;

	bzero(me, sizeof(struct mqtt_eth));

	me->mp = param;
	pthread_mutex_init(&me->mutex_mqtt, NULL);

	/* FIXME netinit has dhcp operations, do we need a timeout? */
	nsh_netinit();

	return (void *)me;
}

void mqtt_eth_deinit(void **h_me)
{
	struct mqtt_eth *me = (struct mqtt_eth *)*h_me;
	if (!me)
		return;
	pthread_mutex_destroy(&me->mutex_mqtt);
	*h_me = NULL;
}

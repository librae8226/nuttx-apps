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

	/* TODO add logic to ensure tcp is connected before publishing */

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

static void wget_callback(FAR char **buffer, int offset, int datend,
                          FAR int *buflen, FAR void *arg)
{
	int i;
	bsc_info("%s\n", &(*buffer)[offset]);
#if 0
	for (i = offset; i <= datend; i++)
		bsc_printf("%c", (*buffer)[i]);
	bsc_printf("\n");
#endif
}

enum eth_init_stat {
	ETH_INIT_STATE_0 = 0,
	ETH_INIT_STATE_1,
	ETH_INIT_STATE_2
};

static int eth_stat = ETH_INIT_STATE_0;

void *mqtt_eth_init(struct mqtt_param *param)
{
	struct mqtt_eth *me = &g_me;
	struct in_addr iaddr;
	char *buffer_wget;
	int ret;
#if 1
	switch (eth_stat) {
		case ETH_INIT_STATE_0:
			/* FIXME netinit has dhcp operations, do we need a timeout? */
			if (nsh_netinit() != OK)
				return NULL;
			me->mp = param;
			pthread_mutex_init(&me->mutex_mqtt, NULL);
			eth_stat = ETH_INIT_STATE_1;
			break;
		case ETH_INIT_STATE_1:
			if (netlib_get_ipv4addr("eth0", &iaddr) < 0) {
				bsc_err("netlib_get_ipv4addr failed\n");
			} else {
				if (iaddr.s_addr != 0x0 && iaddr.s_addr != 0xdeadbeef) {
					eth_stat = ETH_INIT_STATE_2;
				} else {
					bsc_info("wait for ip 0x%08x\n", iaddr.s_addr);
				}
			}
			break;
		case ETH_INIT_STATE_2:
			buffer_wget = malloc(512);
			/* FIXME should be able to recover */
			DEBUGASSERT(buffer_wget);
			bzero(buffer_wget, 512);
			ret = wget(URL_INET_ACCESS, buffer_wget, 512, wget_callback, NULL);
			free(buffer_wget);
			if (ret == 0) {
				eth_stat = ETH_INIT_STATE_0;
				return (void *)me;
			} else {
				bsc_info("wait for internet\n");
			}
			break;
		default:
			bsc_err("unknow eth init state: %d\n", eth_stat);
			break;
	}
	return NULL;
#else
	bzero(me, sizeof(struct mqtt_eth));

	me->mp = param;
	pthread_mutex_init(&me->mutex_mqtt, NULL);

	/* FIXME netinit has dhcp operations, do we need a timeout? */
	if (nsh_netinit() != OK)
		return NULL;

	return (void *)me;
#endif
}

void mqtt_eth_deinit(void **h_me)
{
	struct mqtt_eth *me = (struct mqtt_eth *)*h_me;
	eth_stat = ETH_INIT_STATE_0;
	if (!me)
		return;
	pthread_mutex_destroy(&me->mutex_mqtt);
	*h_me = NULL;
}

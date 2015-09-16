#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <debug.h>
#include <string.h>

#include <apps/netutils/netlib.h>
#include <apps/netutils/webclient.h>
#include <apps/netutils/MQTTClient.h>

//#define BSCAPP_DEBUG

#ifdef BSCAPP_DEBUG
#define MQTT_BROKER_IP		"123.57.208.39"
#define MQTT_BROKER_PORT	1883
#define URL_INET_ACCESS		"http://123.57.208.39:8080/config.json"
#else
#define MQTT_BROKER_IP		"123.57.208.39"
#define MQTT_BROKER_PORT	1883
#define URL_INET_ACCESS		"http://123.57.208.39:8080/config.json"
#endif

#define BSCAPP_UID_LEN		64
#define MQTT_BUF_MAX_LEN	128
#define MQTT_CMD_TIMEOUT	1000
#define MQTT_TOPIC_LEN		128
#define MQTT_TOPIC_HEADER_LEN	64
#define MQTT_SUBTOPIC_LEN	32

struct bscapp_data {
	Network n;
	Client c;
	sem_t sem;
	pthread_t tid_mqttsub_thread;
	pthread_t tid_mqttpub_thread;
	unsigned char buf[MQTT_BUF_MAX_LEN];
	unsigned char readbuf[MQTT_BUF_MAX_LEN];
	volatile int exit;
	volatile int exit_mqttsub_thread;
	volatile int exit_mqttpub_thread;
	char uid[BSCAPP_UID_LEN];
	char topic_sub_header[MQTT_TOPIC_HEADER_LEN];
	char topic_pub_header[MQTT_TOPIC_HEADER_LEN];
};

enum relays_e {
	RELAY_1 = 1,
	RELAY_2,
	RELAY_3,
	RELAY_4,
	RELAY_5,
	RELAY_6
};

enum output_type {
	OUTPUT_RELAY,
	OUTPUT_PWM
};

struct output_resource {
	char *name;
	int id;
	enum output_type type;
};

static struct output_resource output_map[] = {
	{
		.name = "RELAY1",
		.id = 1,
		.type = OUTPUT_RELAY,
	},
	{
		.name = "RELAY2",
		.id = 2,
		.type = OUTPUT_RELAY,
	},
	{
		.name = "RELAY3",
		.id = 3,
		.type = OUTPUT_RELAY,
	},
	{
		.name = "RELAY4",
		.id = 4,
		.type = OUTPUT_RELAY,
	},
	{
		.name = "RELAY5",
		.id = 5,
		.type = OUTPUT_RELAY,
	},
	{
		.name = "RELAY6",
		.id = 6,
		.type = OUTPUT_RELAY,
	},
	{
		.name = "PWM1",
		.id = 1,
		.type = OUTPUT_PWM
	},
	{
		.name = "PWM2",
		.id = 2,
		.type = OUTPUT_PWM
	},
	{
		.name = NULL,
		.id = -1,
		.type = -1,
	}
};

#define RELAY_MIN	RELAY_1
#define RELAY_MAX	RELAY_6

static struct bscapp_data g_priv;

static void printstrbylen(char *msg, char *str, int len)
{
	int i;
	DEBUGASSERT(str != NULL);
	printf("%s (len: %d)\n\t", msg, len);
	for (i = 0; i < len; i++)
		printf("%c", str[i]);
	printf("\n\t");
	for (i = 0; i < len; i++)
		printf("%02x ", str[i]);
	printf("\n");
}

static int exec_match_output(char *subtopic, char *act)
{
	struct output_resource *res = NULL;

	if (subtopic == NULL) {
		printf("%s, no subtopic\n", subtopic);
		return -EINVAL;
	}

	for (res = &output_map[0]; res->name != NULL; res++) {
		if (strcmp(res->name, subtopic) == 0) {
			printf("match: %s, id: %d, type: %d\n", res->name, res->id, res->type);
			switch (res->type) {
				case OUTPUT_RELAY:
					printf("%s, hit relay\n", __func__);
					if (strcmp(act, "on") == 0) {
						printf("%s, ACT relay: %s\n", __func__, act);
						relays_setstat(res->id - 1, true);
					} else if (strcmp(act, "off") == 0) {
						printf("%s, ACT relay: %s\n", __func__, act);
						relays_setstat(res->id - 1, false);
					} else {
						printf("%s, unsupported relay act: %s\n", __func__, act);
						return -EINVAL;
					}
					break;
				case OUTPUT_PWM:
					printf("%s, hit pwm\n", __func__);
					break;
				default:
					return -EINVAL;
					break;
			}
		}
	}

	return OK;
}

static void msg_handler(MessageData *md)
{
	MQTTMessage *message = md->message;
	char *topic = md->topicName->lenstring.data;
	int topic_len = md->topicName->lenstring.len;
	char *payload = (char *)message->payload;
	int payload_len = message->payloadlen;

	char subtopic[MQTT_SUBTOPIC_LEN] = {0};
	char header_len = 0;
	char *token = NULL;
	int ret = -1;

	if (payload_len < MQTT_BUF_MAX_LEN)
		payload[payload_len] = '\0';

	printstrbylen("topic:", topic, topic_len);
	printstrbylen("payload:", payload, payload_len);

	header_len = strlen(g_priv.topic_sub_header);

	if (strncmp(topic, g_priv.topic_sub_header, header_len) != 0) {
		printf("%s, unexpected topic header\n", __func__);
		return;
	}

	if (topic_len - header_len >= MQTT_SUBTOPIC_LEN) {
		printf("%s, can't handle subtopic length>%d (%d)\n", __func__, MQTT_SUBTOPIC_LEN, topic_len - header_len);
		return;
	}
	strncpy(subtopic, topic + header_len, topic_len - header_len);
	printf("%s, subtopic: %s\n", __func__, subtopic);

	token = strtok(subtopic, "/");
	if (token == NULL) {
		printf("%s, no subtopic, ignore\n", __func__);
		return;
	}
	if (strcmp(token, "output") == 0) {
		printf("%s, hit output\n", __func__);
		token = strtok(NULL, "/");
		if (token == NULL) {
			printf("%s, no output subtopic, ignore\n", __func__);
			return;
		}
		ret = exec_match_output(token, payload);
		if (ret != OK)
			printf("%s, unsupported output %s with payload: %s\n", __func__, token, payload);
#if 0
		if (strcmp(token, "relay") == 0) {
			token = strtok(NULL, "/");
			printf("%s, hit relay: %s\n", __func__, token);
			idx = atoi(token);
			if (idx >= RELAY_MIN && idx <= RELAY_MAX) {
				if (strcmp(payload, "on") == 0) {
					printf("%s, ACT relay: %s\n", __func__, payload);
					relays_setstat(idx - 1, true);
				} else if (strcmp(payload, "off") == 0) {
					printf("%s, ACT relay: %s\n", __func__, payload);
					relays_setstat(idx - 1, false);
				} else {
					printf("%s, unsupported: %s\n", __func__, payload);
				}
			} else {
				printf("%s, idx %d invalid\n", __func__, idx);
			}
		} else if (strcmp(token, "pwm") == 0) {
			printf("%s, hit pwm\n", __func__);
		} else {
			printf("%s, unsupported: %s\n", __func__, token);
		}
#endif
	} else if (strcmp(token, "config") == 0) {
		printf("%s, hit config\n", __func__);
	} else if (strcmp(token, "exit") == 0) {
		printf("%s, hit exit\n", __func__);
		//g_priv.exit_mqttsub_thread = 1;
	} else {
		printf("%s, unsupported: %s\n", __func__, token);
	}
}

int bsc_mqtt_subscribe(struct bscapp_data *priv, char *topic)
{
	int rc = 0;
	int ret;
	int val;

	printf("Subscribing to %s\n", topic);
	do {
		rc = MQTTSubscribe(&priv->c, topic, QOS2, msg_handler);
		if (rc < 0) {
			printf("Subscribe fail %d, try again\n", rc);
			usleep(100000);
		} else {
			printf("Subscribed %d\n", rc);
		}
	} while (rc < 0);

	ret = sem_getvalue(&priv->sem, &val);
	if (ret < 0)
		printf("%s, could not get semaphore value\n", __func__);
	else
		printf("%s, semaphore value: %d\n", __func__, val);

	if (val < 0) {
		printf("%s, posting semaphore\n", __func__);
		ret = sem_post(&priv->sem);
		if (ret != 0)
			printf("%s, sem_post failed\n", __func__);
	} else {
		printf("val > 0\n");
	}

	while (!priv->exit_mqttsub_thread) {
		MQTTYield(&priv->c, 1000);
	}

	priv->exit = 1;

	return OK;
}

int bsc_mqtt_publish(struct bscapp_data *priv, char *topic, char *payload)
{
	MQTTMessage msg;
	char msgbuf[MQTT_BUF_MAX_LEN];
	int rc = 0;

	printf("%s in\n", __func__);

	if (topic == NULL || payload == NULL)
		return -EINVAL;

	printf("topic: %s\n", topic);
	printf("payload: %s\n", payload);

	bzero(msgbuf, sizeof(msgbuf));
	msg.qos = QOS0;
	msg.retained = false;
	msg.dup = false;
	strcpy(msgbuf, payload);
	msg.payload = (void *)msgbuf;
	msg.payloadlen = strlen(msgbuf) + 1;

	rc = MQTTPublish(&priv->c, topic, &msg);
	if (rc != 0)
		printf("Error publish, rc: %d\n", rc);
	if (msg.qos > 0)
		MQTTYield(&priv->c, 100);

	printf("%s out\n", __func__);
	return OK;
}

int bsc_mqtt_connect(struct bscapp_data *priv)
{
	int ret;

	NewNetwork(&priv->n);
	while (ConnectNetwork(&priv->n, MQTT_BROKER_IP, MQTT_BROKER_PORT) != OK) {
		printf("Failed to connect network, try again\n");
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
	conn_data.keepAliveInterval = 10;
	conn_data.cleansession = 1;

	printf("Connecting to %s %d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	do {
		ret = MQTTConnect(&priv->c, &conn_data);
		if (ret < 0) {
			printf("Connect fail %d, try again\n", ret);
			usleep(100000);
		} else {
			printf("Connected %d\n", ret);
		}
	} while (ret < 0);
	return OK;
}

static int wait_for_ip(void)
{
	struct in_addr iaddr;
	int ret = -1;
	while (1) {
		ret = netlib_get_ipv4addr("eth0", &iaddr);
		if (ret < 0)
			printf("netlib_get_ipv4addr failed.\n");
		if (iaddr.s_addr != 0x0)
			break;
		printf("%s... 0x%08x\n", __func__, iaddr.s_addr);
		sleep(1);
	}
	return OK;
}

static void wget_callback(FAR char **buffer, int offset, int datend,
                          FAR int *buflen, FAR void *arg)
{
	int i;
	printf("%s, len: %d\n", __func__, *buflen);
	for (i = offset; i <= datend; i++)
		printf("%c", (*buffer)[i]);
	printf("\n");
}

static int wait_for_internet(void)
{
	int ret;
	char *buffer_wget = malloc(512);

	/* FIXME should be able to recover */
	DEBUGASSERT(buffer_wget);

	while (1) {
		ret = wget(URL_INET_ACCESS, buffer_wget, 512, wget_callback, NULL);
		if (ret == 0)
			break;
		printf("%s...\n", __func__);
		sleep(1);
	}

	free(buffer_wget);
	return OK;
}

static pthread_addr_t mqttsub_thread(pthread_addr_t arg)
{
	struct bscapp_data *priv = (struct bscapp_data *)arg;
	char t[MQTT_TOPIC_LEN];
	sprintf(t, "%s/#", priv->topic_sub_header);
	bsc_mqtt_subscribe(priv, t);
	printf("%s, exited.\n", __func__);
	return NULL;
}

static int start_mqttsub_thread(struct bscapp_data *priv)
{
	struct sched_param sparam;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	sparam.sched_priority = 50;
	(void)pthread_attr_setschedparam(&attr, &sparam);
	(void)pthread_attr_setstacksize(&attr, 2048);
	pthread_setname_np(priv->tid_mqttsub_thread, "mqttsub_thread");

	printf("starting mqttsub thread\n");
	ret = pthread_create(&priv->tid_mqttsub_thread, &attr, mqttsub_thread, priv);
	if (ret != OK) {
		printf("ERROR: Failed to create thread: %d\n", ret);
		return -EFAULT;
	}

	return ret;
}

static pthread_addr_t mqttpub_thread(pthread_addr_t arg)
{
	struct bscapp_data *priv = (struct bscapp_data *)arg;
	char t[MQTT_TOPIC_LEN];
	while (!g_priv.exit_mqttpub_thread) {
		sprintf(t, "%s/input/AI1", priv->topic_pub_header);
		bsc_mqtt_publish(&g_priv, t, "990");
		sprintf(t, "%s/input/AI2", priv->topic_pub_header);
		bsc_mqtt_publish(&g_priv, t, "880");
		sprintf(t, "%s/input/AI3", priv->topic_pub_header);
		bsc_mqtt_publish(&g_priv, t, "770");
		sprintf(t, "%s/input/AI4", priv->topic_pub_header);
		bsc_mqtt_publish(&g_priv, t, "660");
		sleep(10);
	}
	printf("%s, exited.\n", __func__);
	return NULL;
}

static int start_mqttpub_thread(struct bscapp_data *priv)
{
	struct sched_param sparam;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	sparam.sched_priority = 50;
	(void)pthread_attr_setschedparam(&attr, &sparam);
	(void)pthread_attr_setstacksize(&attr, 2048);
	pthread_setname_np(priv->tid_mqttpub_thread, "mqttpub_thread");

	printf("starting mqttpub thread\n");
	ret = pthread_create(&priv->tid_mqttpub_thread, &attr, mqttpub_thread, priv);
	if (ret != OK) {
		printf("ERROR: Failed to create thread: %d\n", ret);
		return -EFAULT;
	}

	return ret;
}

static int selftest_mqtt(struct bscapp_data *priv)
{
	int i;
	char t[MQTT_TOPIC_LEN] = {0};

	for (i = RELAY_MIN; i <= RELAY_MAX; i++) {
		sprintf(t, "%s/output/RELAY%d", priv->topic_sub_header, i);
		bsc_mqtt_publish(priv, t, "on");
	}
	for (i = RELAY_MIN; i <= RELAY_MAX; i++) {
		sprintf(t, "%s/output/RELAY%d", priv->topic_sub_header, i);
		bsc_mqtt_publish(priv, t, "off");
	}

	return OK;
}

static int bscapp_init(struct bscapp_data *priv)
{
	int ret;
	printf("%s in\n", __func__);

	bzero(priv, sizeof(struct bscapp_data));
	sem_init(&priv->sem, 0, 0);

	uint32_t uid_0_31 = (*(volatile uint32_t *)(0x1ffff7e8));
	uint32_t uid_32_63 = (*(volatile uint32_t *)(0x1ffff7e8 + 4));
	uint32_t uid_64_95 = (*(volatile uint32_t *)(0x1ffff7e8 + 8));

#ifdef BSCAPP_DEBUG
	sprintf(priv->uid, "864-test");
#else
	sprintf(priv->uid, "864-%08x%08x%08x", uid_0_31, uid_32_63, uid_64_95);
#endif
	printf("uid: %s\n", priv->uid);

	sprintf(priv->topic_sub_header, "/down/bs/%s", priv->uid);
	sprintf(priv->topic_pub_header, "/up/bs/%s", priv->uid);
	printf("sub: %s\n", priv->topic_sub_header);
	printf("pub: %s\n", priv->topic_pub_header);

	bsc_mqtt_connect(priv);
	start_mqttsub_thread(priv);

	ret = sem_wait(&priv->sem);
	if (ret != 0)
		printf("sem_wait failed\n");

	/* self test */
	selftest_mqtt(&g_priv);

	start_mqttpub_thread(priv);
	bsc_mqtt_publish(priv, "/up/bs/checkin", priv->uid);

	printf("%s out\n", __func__);
	return OK;
}

static int bscapp_deinit(struct bscapp_data *priv)
{
	printf("%s in\n", __func__);
	MQTTDisconnect(&priv->c);
	priv->n.disconnect(&priv->n);
	printf("%s out\n", __func__);
	return OK;
}

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int bscapp_main(int argc, char *argv[])
#endif
{
	printf("%s entry\n", __func__);

	wait_for_ip();
	wait_for_internet();
	bscapp_init(&g_priv);
	while (!g_priv.exit) {
		sleep(1);
	}

	pthread_join(g_priv.tid_mqttsub_thread, NULL);
	bscapp_deinit(&g_priv);

	printf("%s exited\n", __func__);
	return OK;
}

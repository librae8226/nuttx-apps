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

//#define BSCAPP_TEST
//#define BSCAPP_DEBUG

#ifdef BSCAPP_DEBUG
#define bsc_dbg(format, ...) \
	syslog(LOG_DEBUG, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define bsc_printf(format, ...) \
	printf(format, ##__VA_ARGS__)
#else
#define bsc_dbg(format, ...)
#define bsc_printf(format, ...)
#endif

#define bsc_info(format, ...) \
	syslog(LOG_INFO, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define bsc_warn(format, ...) \
	syslog(LOG_WARNING, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define bsc_err(format, ...) \
	syslog(LOG_ERR, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)

#ifdef BSCAPP_TEST
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
	if (str == NULL)
		return;
	bsc_info("%s (%d): ", msg, len);
	for (i = 0; i < len; i++)
		printf("%c", str[i]);
	printf("\n");
#if 0
	printf("\t");
	for (i = 0; i < len; i++)
		printf("%02x ", str[i]);
	printf("\n");
#endif
}

static int exec_match_output(char *subtopic, char *act)
{
	int ret = OK;
	struct output_resource *res = NULL;

	if (subtopic == NULL) {
		bsc_info("no subtopic\n", subtopic);
		return -EINVAL;
	}

	for (res = &output_map[0]; res->name != NULL; res++) {
		if (strcmp(res->name, subtopic) == 0) {
			bsc_dbg("match: %s, id: %d, type: %d\n", res->name, res->id, res->type);
			switch (res->type) {
				case OUTPUT_RELAY:
					bsc_info("hit relay %d\n", res->id);
					if (strcmp(act, "on") == 0) {
						bsc_info("ACT RELAY%d: %s\n", res->id, act);
						relays_setstat(res->id - 1, true);
					} else if (strcmp(act, "off") == 0) {
						bsc_info("ACT RELAY%d: %s\n", res->id, act);
						relays_setstat(res->id - 1, false);
					} else {
						bsc_info("unsupported relay act: %s\n", act);
						ret = -EINVAL;
					}
					break;
				case OUTPUT_PWM:
					bsc_info("hit pwm\n");
					break;
				default:
					bsc_info("hit default\n");
					ret = -ENOENT;
					break;
			}
		}
	}

	return ret;
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

	printstrbylen("sub topic", topic, topic_len);
	printstrbylen("payload", payload, payload_len);

	header_len = strlen(g_priv.topic_sub_header);

	if (strncmp(topic, g_priv.topic_sub_header, header_len) != 0) {
		bsc_warn("unexpected topic header\n");
		return;
	}

	if (topic_len - header_len >= MQTT_SUBTOPIC_LEN) {
		bsc_warn("can't handle subtopic length>%d (%d)\n", MQTT_SUBTOPIC_LEN, topic_len - header_len);
		return;
	}
	strncpy(subtopic, topic + header_len, topic_len - header_len);
	bsc_dbg("subtopic: %s\n", subtopic);

	token = strtok(subtopic, "/");
	if (token == NULL) {
		bsc_warn("no subtopic, ignore\n");
		return;
	}
	if (strcmp(token, "output") == 0) {
		bsc_info("hit output\n");
		token = strtok(NULL, "/");
		if (token == NULL) {
			bsc_warn("no output subtopic, ignore\n");
			return;
		}
		ret = exec_match_output(token, payload);
		if (ret != OK)
			bsc_warn("unsupported output %s with payload: %s\n", token, payload);
#if 0
		if (strcmp(token, "relay") == 0) {
			token = strtok(NULL, "/");
			bsc_info("hit relay: %s\n", token);
			idx = atoi(token);
			if (idx >= RELAY_MIN && idx <= RELAY_MAX) {
				if (strcmp(payload, "on") == 0) {
					bsc_info("ACT relay: %s\n", payload);
					relays_setstat(idx - 1, true);
				} else if (strcmp(payload, "off") == 0) {
					bsc_info("ACT relay: %s\n", payload);
					relays_setstat(idx - 1, false);
				} else {
					bsc_info("unsupported: %s\n", payload);
				}
			} else {
				bsc_info("idx %d invalid\n", idx);
			}
		} else if (strcmp(token, "pwm") == 0) {
			bsc_info("hit pwm\n");
		} else {
			bsc_info("unsupported: %s\n", token);
		}
#endif
	} else if (strcmp(token, "config") == 0) {
		bsc_info("hit config\n");
	} else if (strcmp(token, "exit") == 0) {
		bsc_info("hit exit\n");
		//g_priv.exit_mqttsub_thread = 1;
	} else {
		bsc_info("unsupported: %s\n", token);
	}
}

int bsc_mqtt_subscribe(struct bscapp_data *priv, char *topic)
{
	int ret = 0;
	int val;

	/* TODO ensure mqtt connection is alive */

	bsc_info("subscribing to %s\n", topic);
	do {
		ret = MQTTSubscribe(&priv->c, topic, QOS1, msg_handler);
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
		ret = MQTTYield(&priv->c, 1000);
		if (ret < 0)
			bsc_err("error yield, ret: %d\n", ret);
	}

	return OK;
}

int bsc_mqtt_publish(struct bscapp_data *priv, char *topic, char *payload)
{
	MQTTMessage msg;
	char msgbuf[MQTT_BUF_MAX_LEN];
	int ret = OK;

	if (topic == NULL || payload == NULL)
		return -EINVAL;

	bsc_info("pub topic: %s\n", topic);
	bsc_info("payload  : %s\n", payload);

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

	/* TODO ensure mqtt connection is alive */

	ret = MQTTPublish(&priv->c, topic, &msg);
	if (ret != 0) {
		bsc_err("error publish, ret: %d\n", ret);
	} else {
		if (msg.qos > 0) {
			ret = MQTTYield(&priv->c, 100);
			if (ret < 0)
				bsc_err("error yield, ret: %d\n", ret);
		}
	}

	return ret;
}

int bsc_mqtt_connect(struct bscapp_data *priv)
{
	int ret;

	bsc_info("connecting to broker %s:%d...\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
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
	conn_data.keepAliveInterval = 10;
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

int bsc_mqtt_disconnect(struct bscapp_data *priv)
{
	MQTTDisconnect(&priv->c);
	priv->n.disconnect(&priv->n);
	return OK;
}

static int wait_for_ip(void)
{
	struct in_addr iaddr;
	int ret = -1;
	while (1) {
		bsc_info("... 0x%08x\n", iaddr.s_addr);
		ret = netlib_get_ipv4addr("eth0", &iaddr);
		if (ret < 0) {
			bsc_err("netlib_get_ipv4addr failed.\n");
			continue;
		}
		if (iaddr.s_addr != 0x0)
			break;
		sleep(1);
	}
	return OK;
}

static void wget_callback(FAR char **buffer, int offset, int datend,
                          FAR int *buflen, FAR void *arg)
{
	int i;
	bsc_info("len: %d\n", *buflen);
	for (i = offset; i <= datend; i++)
		bsc_printf("%c", (*buffer)[i]);
	bsc_printf("\n");
}

static int wait_for_internet(void)
{
	int ret;
	char *buffer_wget = malloc(512);

	/* FIXME should be able to recover */
	DEBUGASSERT(buffer_wget);

	while (1) {
		bsc_info("...\n");
		ret = wget(URL_INET_ACCESS, buffer_wget, 512, wget_callback, NULL);
		if (ret == 0)
			break;
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
	bsc_info("exited.\n");
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

	bsc_info("starting mqttsub thread\n");
	ret = pthread_create(&priv->tid_mqttsub_thread, &attr, mqttsub_thread, priv);
	if (ret != OK) {
		bsc_err("failed to create thread: %d\n", ret);
		return -EFAULT;
	}

	return ret;
}

static pthread_addr_t mqttpub_thread(pthread_addr_t arg)
{
	struct bscapp_data *priv = (struct bscapp_data *)arg;
	char t[MQTT_TOPIC_LEN];
	while (!g_priv.exit_mqttpub_thread) {
#if 0
		sprintf(t, "%s/input/AI1", priv->topic_pub_header);
		bsc_mqtt_publish(&g_priv, t, "990");
		sprintf(t, "%s/input/AI2", priv->topic_pub_header);
		bsc_mqtt_publish(&g_priv, t, "880");
		sprintf(t, "%s/input/AI3", priv->topic_pub_header);
		bsc_mqtt_publish(&g_priv, t, "770");
		sprintf(t, "%s/input/AI4", priv->topic_pub_header);
		bsc_mqtt_publish(&g_priv, t, "660");
#endif
		sleep(1);
	}
	bsc_info("exited.\n");
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

	bsc_info("starting mqttpub thread\n");
	ret = pthread_create(&priv->tid_mqttpub_thread, &attr, mqttpub_thread, priv);
	if (ret != OK) {
		bsc_err("failed to create thread: %d\n", ret);
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
	bsc_dbg("in\n");

	bzero(priv, sizeof(struct bscapp_data));
	sem_init(&priv->sem, 0, 0);

	uint32_t uid_0_31 = (*(volatile uint32_t *)(0x1ffff7e8));
	uint32_t uid_32_63 = (*(volatile uint32_t *)(0x1ffff7e8 + 4));
	uint32_t uid_64_95 = (*(volatile uint32_t *)(0x1ffff7e8 + 8));

#ifdef BSCAPP_TEST
	sprintf(priv->uid, "864-test");
#else
	sprintf(priv->uid, "864-%08x%08x%08x", uid_0_31, uid_32_63, uid_64_95);
#endif
	bsc_info("uid: %s\n", priv->uid);

	sprintf(priv->topic_sub_header, "/down/bs/%s", priv->uid);
	sprintf(priv->topic_pub_header, "/up/bs/%s", priv->uid);
	bsc_info("sub: %s\n", priv->topic_sub_header);
	bsc_info("pub: %s\n", priv->topic_pub_header);

	bsc_mqtt_connect(priv);

	bsc_dbg("out\n");
	return OK;
}

static int bscapp_deinit(struct bscapp_data *priv)
{
	bsc_dbg("in\n");
	bsc_mqtt_disconnect(priv);
	bsc_dbg("out\n");
	return OK;
}

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int bscapp_main(int argc, char *argv[])
#endif
{
	struct bscapp_data *priv = &g_priv;
	int ret;

	bsc_info("entry\n");

	wait_for_ip();
	wait_for_internet();
	bscapp_init(priv);

	start_mqttsub_thread(priv);

	ret = sem_wait(&priv->sem);
	if (ret != 0)
		bsc_err("sem_wait failed\n");
#ifdef BSCAPP_TEST
	selftest_mqtt(priv);
#endif
	do {
		ret = bsc_mqtt_publish(priv, "/up/bs/checkin", priv->uid);
		if (ret < 0)
			bsc_warn("checkin failed: %d, retry", ret);
	} while (ret < 0);

	start_mqttpub_thread(priv);

	while (!priv->exit)
		sleep(1);

	pthread_join(priv->tid_mqttsub_thread, NULL);
	bscapp_deinit(priv);

	bsc_info("exited\n");
	return OK;
}

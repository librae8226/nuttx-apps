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
#include "mqtt_wifi.h"
#include "bscapp.h"

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
	char             *name;
	int              id;
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
		.type = OUTPUT_PWM,
	},
	{
		.name = "PWM2",
		.id = 2,
		.type = OUTPUT_PWM,
	},
	{
		.name = NULL,
		.id = -1,
		.type = -1,
	},
};

#define RELAY_MIN	RELAY_1
#define RELAY_MAX	RELAY_6

enum input_type {
	INPUT_AI,
	INPUT_DI_TEMPERATURE,
	INPUT_DI_HUMIDITY,
	INPUT_DI_COUNTER,
	INPUT_DI_SWITCH,
	INPUT_ONEWIRE,
};

struct input_resource {
	char            *name;
	char            *st;
	int             id;
	bool            valid;
	uint32_t        value;
	enum input_type type;
};

static struct input_resource input_map[] = {
	{
		.name  = "AI1",
		.st    = "/input/AI1",
		.id    = 1,
		.valid = true,
		.value = 0,
		.type  = INPUT_AI,
	},
	{
		.name  = "AI2",
		.st    = "/input/AI2",
		.id    = 2,
		.valid = true,
		.value = 0,
		.type  = INPUT_AI,
	},
	{
		.name  = "AI3",
		.st    = "/input/AI3",
		.id    = 3,
		.valid = true,
		.value = 0,
		.type  = INPUT_AI,
	},
	{
		.name  = "AI4",
		.st    = "/input/AI4",
		.id    = 4,
		.valid = true,
		.value = 0,
		.type  = INPUT_AI,
	},
	{
		.name  = NULL,
		.st    = NULL,
		.id    = -1,
		.valid = false,
		.value = -1,
		.type  = -1,
	},
};

#define ADC_CH_AI1	10
#define ADC_CH_AI2	12
#define ADC_CH_AI3	13
#define ADC_CH_AI4	9
#define ADC_CH_MAX	4

static int adc_ch_ai[ADC_CH_MAX] = {
	ADC_CH_AI1,
	ADC_CH_AI2,
	ADC_CH_AI3,
	ADC_CH_AI4
};

static struct bscapp_data g_priv;

/* external functions */
int adc_init(void);
uint16_t adc_measure(unsigned int channel);
void relays_setstat(int relays, bool stat);
int bsc_pwm_init(void);
int bsc_pwm_enable(int ch);
int bsc_pwm_disable(int ch);
int bsc_pwm_output(int ch, int freq, int duty);
int nsh_telnetstart(void);
void *mqtt_eth_init(struct mqtt_param *param);
void mqtt_eth_deinit(void **h_me);
int mqtt_eth_subscribe(void *h_me, char *topic, mqtt_msg_handler_t mh);
int mqtt_eth_process(void *h_me);
int mqtt_eth_publish(void *h_me, char *topic, char *payload);
int mqtt_eth_connect(void *h_me);
int mqtt_eth_disconnect(void *h_me);
void *mqtt_wifi_init(struct mqtt_param *param);
void mqtt_wifi_deinit(void **h_mw);
int mqtt_wifi_subscribe(void *h_mw, char *topic, mqtt_msg_handler_t mh);
int mqtt_wifi_process(void *h_mw);
int mqtt_wifi_publish(void *h_mw, char *topic, char *payload);
int mqtt_wifi_connect(void *h_mw);
int mqtt_wifi_disconnect(void *h_mw);

static void printstrbylen(char *msg, char *str, int len)
{
	int i;
	if (str == NULL)
		return;
	bsc_dbg("%s (%d): ", msg, len);
	for (i = 0; i < len; i++)
		bsc_printf("%c", str[i]);
	bsc_printf("\n");
}

static int exec_match_config(char *subtopic, char *content)
{
	int ret = OK;
	char *token = NULL;

	if (subtopic == NULL) {
		bsc_info("no subtopic\n");
	}
	if (content == NULL) {
		bsc_err("no content\n");
		return -EINVAL;
	}
	bsc_info("subtopic: %s, content: %s\n", subtopic?subtopic:"null", content);

	/*
	 * FIXME
	 * Hack here.
	 * We'd better use .json for such config.
	 */
	if (strlen(content) > 4 &&
	    content[0] == 's' &&
	    content[1] == 's' &&
	    content[2] == 'i' &&
	    content[3] == 'd') {
		token = strtok(content, ",");
		if (token) {
			bzero(g_priv.mparam.ssid, WIFI_SSID_LEN);
			strncpy(g_priv.mparam.ssid, token+5, strlen(token+5));
			bsc_info("saved ssid: %s\n", token+5);
		}
		token = strtok(NULL, ",");
		if (token) {
			bzero(g_priv.mparam.psk, WIFI_PSK_LEN);
			strncpy(g_priv.mparam.psk, token+9, strlen(token+9));
			bsc_info("saved psk: %s\n", token+9);

			/* now we do rework and get network re-inited. */
			bsc_info("restarting network threads\n");
			g_priv.rework = true;
		}
	}

	return ret;
}

static int exec_match_output(char *subtopic, char *act)
{
	int ret = OK;
	struct output_resource *res = NULL;

	if (subtopic == NULL) {
		bsc_err("no subtopic\n");
		return -EINVAL;
	}

	for (res = &output_map[0]; res->name != NULL; res++) {
		if (strcmp(res->name, subtopic) == 0) {
			bsc_dbg("match: %s, id: %d, type: %d\n", res->name, res->id, res->type);
			switch (res->type) {
				case OUTPUT_RELAY:
					bsc_dbg("hit relay %d\n", res->id);
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
					bsc_dbg("hit pwm %d\n", res->id);
					bsc_pwm_enable(0);
					bsc_pwm_output(0, 50, atoi(act));
					break;
				default:
					bsc_dbg("hit default\n");
					ret = -ENOENT;
					break;
			}
		}
	}

	return ret;
}

static void mqtt_msg_handler(char *topic, int topic_len, char *payload, int payload_len)
{
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
		bsc_dbg("hit output\n");
		token = strtok(NULL, "/");
		if (token == NULL) {
			bsc_warn("no output subtopic, ignore\n");
			return;
		}
		ret = exec_match_output(token, payload);
		if (ret != OK)
			bsc_warn("unsupported output %s with payload: %s\n", token, payload);
	} else if (strcmp(token, "config") == 0) {
		bsc_dbg("hit config\n");
		token = strtok(NULL, "/");
		ret = exec_match_config(token, payload);
		if (ret != OK)
			bsc_warn("unsupported config %s with payload: %s\n", token, payload);
#ifdef MQTT_SELFPING_ENABLE
	} else if (strcmp(token, "selfping") == 0) {
		bsc_dbg("hit selfping\n");
		g_priv.selfping = false;
		ret = sem_post(&g_priv.sem_sp);
		if (ret != 0)
			bsc_err("sem_post failed\n");
#endif
	} else {
		bsc_warn("unsupported: %s\n", token);
	}
}

int bsc_mqtt_subscribe(struct bscapp_data *priv, char *topic)
{
	int ret = OK;

	bsc_info("subscribing to %s\n", topic);
	switch (priv->net_intf) {
		case NET_INTF_ETH:
			ret = mqtt_eth_subscribe(priv->h_me, topic, mqtt_msg_handler);
			break;
		case NET_INTF_WIFI:
			ret = mqtt_wifi_subscribe(priv->h_mw, topic, mqtt_msg_handler);
			break;
		default:
			ret = -1;
			bsc_warn("unsupported net intf: %d\n", priv->net_intf);
			break;
	}
	return ret;
}

int bsc_mqtt_publish(struct bscapp_data *priv, char *topic, char *payload)
{
	int ret = OK;

	if (priv == NULL || topic == NULL || payload == NULL)
		return -EINVAL;

	bsc_dbg("pub topic: %s\n", topic);
	bsc_dbg("payload  : %s\n", payload);

	switch (priv->net_intf) {
		case NET_INTF_ETH:
			ret = mqtt_eth_publish(priv->h_me, topic, payload);
			break;
		case NET_INTF_WIFI:
			ret = mqtt_wifi_publish(priv->h_mw, topic, payload);
			break;
		default:
			ret = -1;
			bsc_warn("unsupported net intf: %d\n", priv->net_intf);
			break;
	}
	return ret;
}

int bsc_mqtt_connect(struct bscapp_data *priv)
{
	int ret;
#ifdef MQTT_SELFPING_ENABLE
	priv->selfping = false;
#endif
	switch (priv->net_intf) {
		case NET_INTF_ETH:
			ret = mqtt_eth_connect(priv->h_me);
			break;
		case NET_INTF_WIFI:
			ret = mqtt_wifi_connect(priv->h_mw);
			break;
		default:
			ret = -1;
			bsc_warn("unsupported net intf: %d\n", priv->net_intf);
			break;
	}
	return ret;
}

int bsc_mqtt_disconnect(struct bscapp_data *priv)
{
	int ret;
	switch (priv->net_intf) {
		case NET_INTF_ETH:
			ret = mqtt_eth_disconnect(priv->h_me);
			break;
		case NET_INTF_WIFI:
			ret = mqtt_wifi_disconnect(priv->h_mw);
			break;
		default:
			ret = -1;
			bsc_warn("unsupported net intf: %d\n", priv->net_intf);
			break;
	}
	return ret;
}

static pthread_addr_t mqttsub_thread(pthread_addr_t arg)
{
	struct bscapp_data *priv = (struct bscapp_data *)arg;
	char t[MQTT_TOPIC_LEN];
	int val;
	int ret;

	bsc_info("running\n");
	sprintf(t, "%s/#", priv->topic_sub_header);
	bsc_mqtt_subscribe(priv, t);

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
		switch (priv->net_intf) {
			case NET_INTF_ETH:
				ret = mqtt_eth_process(priv->h_me);
				break;
			case NET_INTF_WIFI:
				ret = mqtt_wifi_process(priv->h_mw);
				break;
			default:
				ret = -1;
				bsc_warn("unsupported net intf: %d\n", priv->net_intf);
				break;
		}
#if 0
		if (ret < 0)
			bsc_dbg("mqtt process: %d\n", ret);
#endif
	}
	sleep(1);
	bsc_info("exiting\n");
	return NULL;
}

static pthread_addr_t mqttpub_thread(pthread_addr_t arg)
{
	struct bscapp_data *priv = (struct bscapp_data *)arg;
	char t[MQTT_TOPIC_LEN];
	char payload[8];
	struct input_resource *res = NULL;
	int ret;

	bsc_info("running\n");

	do {
		ret = bsc_mqtt_publish(priv, "/up/bs/checkin", priv->mparam.uid);
		if (ret < 0) {
			bsc_warn("checkin failed: %d, retry\n", ret);
			sleep(1);
		}
	} while (ret < 0);

	while (!priv->exit_mqttpub_thread) {
		res = &input_map[0];
		for (; res->name != NULL; res++) {
			if (res->valid == false) {
				bsc_dbg("%s isn't valid, continue\n", res->name);
				continue;
			}
			sprintf(t, "%s%s", priv->topic_pub_header, res->st);
			sprintf(payload, "%d", res->value);
			bsc_mqtt_publish(priv, t, payload);
		}
#ifdef MQTT_SELFPING_ENABLE
		if (priv->selfping == true) {
			sprintf(t, "%s/selfping", priv->topic_sub_header);
			sprintf(payload, "%s", "p");
			bsc_mqtt_publish(priv, t, payload);
		}
#endif
		sleep(5);
	}
	sleep(1);
	bsc_info("exiting\n");
	return NULL;
}

static int start_mqttsub(struct bscapp_data *priv)
{
	struct sched_param sparam;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	sparam.sched_priority = 50;
	(void)pthread_attr_setschedparam(&attr, &sparam);
	(void)pthread_attr_setstacksize(&attr, 2048);

	bsc_info("starting mqttsub thread\n");
	ret = pthread_create(&priv->tid_mqttsub_thread, &attr, mqttsub_thread, priv);
	if (ret != OK) {
		bsc_err("failed to create thread: %d\n", ret);
		return -EFAULT;
	}
	pthread_setname_np(priv->tid_mqttsub_thread, "mqttsub_thread");

	return ret;
}

static int start_mqttpub(struct bscapp_data *priv)
{
	struct sched_param sparam;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	sparam.sched_priority = 50;
	(void)pthread_attr_setschedparam(&attr, &sparam);
	(void)pthread_attr_setstacksize(&attr, 2048);

	bsc_info("starting mqttpub thread\n");
	ret = pthread_create(&priv->tid_mqttpub_thread, &attr, mqttpub_thread, priv);
	if (ret != OK) {
		bsc_err("failed to create thread: %d\n", ret);
		return -EFAULT;
	}
	pthread_setname_np(priv->tid_mqttpub_thread, "mqttpub_thread");

	return ret;
}

static int stop_thread(struct bscapp_data *priv, pthread_t tid, volatile bool *p_exit)
{
	int ret, result;
	char name[32] = "null";

	ret = pthread_mutex_lock(&priv->mutex_exit);
	if (ret != 0)
		bsc_err("failed to lock mutex: %d\n", ret);

	*p_exit = true;

	pthread_getname_np(tid, name);
	bsc_info("stopping %s()\n", name);
	ret = pthread_join(tid, (pthread_addr_t *)&result);
	if (ret != 0)
		bsc_err("pthread_join failed, ret=%d, result=%d\n", ret, result);

	*p_exit = false;

	bsc_info("stopped %s()\n", name);

	ret = pthread_mutex_unlock(&priv->mutex_exit);
	if (ret != 0)
		bsc_err("failed to unlock mutex: %d\n", ret);
	return OK;
}

static int feed_input_ai(struct input_resource *res)
{
	uint16_t adc_ret = 0;

	if (res == NULL) {
		bsc_err("res is NULL!\n");
		return -EINVAL;
	}

	if (res->valid == false)
		return OK;

	adc_ret = adc_measure(adc_ch_ai[res->id]);

	if (adc_ret == 0xffff) {
		bsc_err("adc ch%d error!\n", res->id);
		return -EFAULT;
	}

	res->value = adc_ret;
	return OK;
}

static int feed_input_di_temperature(struct input_resource *res)
{
	return OK;
}

static int feed_input_di_humidity(struct input_resource *res)
{
	return OK;
}

static int feed_input_di_counter(struct input_resource *res)
{
	return OK;
}

static int feed_input_di_switch(struct input_resource *res)
{
	return OK;
}

static int feed_input_onewire(struct input_resource *res)
{
	return OK;
}

static pthread_addr_t sample_thread(pthread_addr_t arg)
{
	struct bscapp_data *priv = (struct bscapp_data *)arg;
	struct input_resource *res = NULL;
	int ret = 0;

	bsc_info("running\n");

	while (!priv->exit_sample_thread) {
		for (res = &input_map[0]; res->name != NULL; res++) {
			switch (res->type) {
				case INPUT_AI:
					ret = feed_input_ai(res);
					break;
				case INPUT_DI_TEMPERATURE:
					ret = feed_input_di_temperature(res);
					break;
				case INPUT_DI_HUMIDITY:
					ret = feed_input_di_humidity(res);
					break;
				case INPUT_DI_COUNTER:
					ret = feed_input_di_counter(res);
					break;
				case INPUT_DI_SWITCH:
					ret = feed_input_di_switch(res);
					break;
				case INPUT_ONEWIRE:
					ret = feed_input_onewire(res);
					break;
				default:
					break;
			}
			if (ret < 0)
				bsc_warn("sampling failure occured\n");
		}
		usleep(1000000);
	}
	sleep(1);
	bsc_info("exiting\n");
	return NULL;
}

static int start_sample(struct bscapp_data *priv)
{
	struct sched_param sparam;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	sparam.sched_priority = 50;
	(void)pthread_attr_setschedparam(&attr, &sparam);
	(void)pthread_attr_setstacksize(&attr, 2048);

	bsc_info("starting sample thread\n");
	ret = pthread_create(&priv->tid_sample_thread, &attr, sample_thread, priv);
	if (ret != OK) {
		bsc_err("failed to create thread: %d\n", ret);
		return -EFAULT;
	}
	pthread_setname_np(priv->tid_sample_thread, "sample_thread");

	return ret;
}

static int selftest_mqtt(struct bscapp_data *priv)
{
#if 0
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
#endif
	return OK;
}

#ifdef MQTT_SELFPING_ENABLE
static void selfping_timeout(int signo, siginfo_t *info, void *ucontext)
{
	bsc_err("signo: %d\n", signo);
	g_priv.rework = true;
}
#endif

static int bsc_timer_stop(timer_t timer_id)
{
	int ret;
	struct sigaction act, oact;

	bsc_dbg("deleting timer\n" );
	ret = timer_delete(timer_id);
	if (ret != OK)
		bsc_err("timer_delete() failed, ret=%d\n", ret);

	act.sa_sigaction = SIG_DFL;
	ret = sigaction(SIGALRM, &act, &oact);
	if (ret != OK)
		bsc_err("sigaction() failed, ret=%d\n", ret);

	bsc_dbg("done\n" );
	return ret;
}

static int bsc_timer_start(timer_t *p_timer_id, int tsec, void (*timer_cb)(int, siginfo_t *, void *))
{
	struct sigaction act, oact;
	struct itimerspec timer;
	int ret;

	if (tsec <= 0 || timer_cb == NULL)
		return -EINVAL;

	bsc_dbg("registering signal handler\n" );
	act.sa_sigaction = timer_cb;
	act.sa_flags  = SA_SIGINFO;

	ret = sigaction(SIGALRM, &act, &oact);
	if (ret != OK) {
		bsc_err("sigaction() failed, ret=%d\n", ret);
	}

	bsc_dbg("creating timer\n" );
	ret = timer_create(CLOCK_REALTIME, NULL, p_timer_id);
	if (ret != OK) {
		bsc_err("timer_create() failed, ret=%d\n", ret);
		goto err_out_timer_create;
	}

	bsc_dbg("starting timer\n" );

	timer.it_value.tv_sec     = tsec;
	timer.it_value.tv_nsec    = 0;

	ret = timer_settime(*p_timer_id, 0, &timer, NULL);
	if (ret != OK) {
		bsc_err("timer_settime() failed, ret=%d\n", ret);
		goto err_out_timer_settime;
	}
	return OK;

err_out_timer_settime:
	bsc_timer_stop(*p_timer_id);
err_out_timer_create:
	return ERROR;
}

static bool network_ready(struct bscapp_data *priv)
{
//	bsc_dbg("? wait...\n");
	return priv->net_wifi_ready || priv->net_eth_ready;
}

static pthread_addr_t probe_eth_thread(pthread_addr_t arg)
{
	struct bscapp_data *priv = (struct bscapp_data *)arg;

	bsc_info("running\n");
	priv->net_eth_ready = false;

	while (!network_ready(priv)) {
		/*
		 * FIXME
		 * It may stuck here for dhcp and eth thread cannot exit.
		 * In fact we need to exit this thread if other net interface (wifi)
		 * has got ready before eth, so that next time we can re-init eth.
		 */
		priv->h_me = mqtt_eth_init(&priv->mparam);
		if (!priv->h_me) {
			bsc_info("mqtt_eth_init failed, need retry\n");
		} else {
			priv->net_eth_ready = true;
			/*
			 * FIXME Trick here, when eth ready,
			 * de-init wifi to reset wifi init state,
			 * in case we'll need to re-init wifi then.
			 */
			mqtt_wifi_deinit(&priv->h_mw);
		}
#if 0
		ret = netlib_get_ipv4addr("eth0", &iaddr);
		if (ret < 0) {
			bsc_err("netlib_get_ipv4addr failed.\n");
			sleep(1);
			continue;
		}

		if (iaddr.s_addr != 0x0 && iaddr.s_addr != 0xdeadbeef) {
			buffer_wget = malloc(512);
			/* FIXME should be able to recover */
			DEBUGASSERT(buffer_wget);
			ret = wget(URL_INET_ACCESS, buffer_wget, 512, wget_callback, NULL);
			free(buffer_wget);
			if (ret == 0) {
				priv->net_eth_ready = true;
				/*
				 * FIXME Trick here, when eth ready,
				 * de-init wifi to reset wifi init state,
				 * in case we'll need to re-init wifi then.
				 */
				mqtt_wifi_deinit(&priv->h_mw);
				break;
			} else {
				bsc_info("wait for internet\n");
				sleep(1);
			}
		} else {
			bsc_info("wait for ip 0x%08x\n", iaddr.s_addr);
			sleep(1);
		}
#endif
		sleep(1);
	}

	bsc_info("exiting\n");
	return NULL;
}

static pthread_addr_t probe_wifi_thread(pthread_addr_t arg)
{
	struct bscapp_data *priv = (struct bscapp_data *)arg;

	bsc_info("running\n");
	priv->net_wifi_ready = false;
#if 0
	mqtt_wifi_unit_test(&priv->mparam);
#else
	while (!network_ready(priv)) {
		priv->h_mw = mqtt_wifi_init(&priv->mparam);
		if (!priv->h_mw) {
			usleep(1);
		} else {
			priv->net_wifi_ready = true;
			/*
			 * FIXME Trick here, when wifi ready,
			 * de-init eth to reset eth init state,
			 * in case we'll need to re-init eth then.
			 */
			mqtt_eth_deinit(&priv->h_me);
		}
	}
#endif
	sleep(1);

	bsc_info("exiting\n");
	return NULL;
}

static int start_probe_eth(struct bscapp_data *priv)
{
	struct sched_param sparam;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	sparam.sched_priority = 50;
	(void)pthread_attr_setschedparam(&attr, &sparam);
	(void)pthread_attr_setstacksize(&attr, 2048);

	bsc_info("starting probe eth thread\n");
	ret = pthread_create(&priv->tid_probe_eth_thread, &attr, probe_eth_thread, priv);
	if (ret != OK) {
		bsc_err("failed to create thread: %d\n", ret);
		return -EFAULT;
	}
	pthread_setname_np(priv->tid_probe_eth_thread, "probe_eth_thread");
	pthread_detach(priv->tid_probe_eth_thread);

	return ret;
}

static int start_probe_wifi(struct bscapp_data *priv)
{
	struct sched_param sparam;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	sparam.sched_priority = 50;
	(void)pthread_attr_setschedparam(&attr, &sparam);
	(void)pthread_attr_setstacksize(&attr, 2048);

	bsc_info("starting probe wifi thread\n");
	ret = pthread_create(&priv->tid_probe_wifi_thread, &attr, probe_wifi_thread, priv);
	if (ret != OK) {
		bsc_err("failed to create thread: %d\n", ret);
		return -EFAULT;
	}
	pthread_setname_np(priv->tid_probe_wifi_thread, "probe_wifi_thread");
	pthread_detach(priv->tid_probe_wifi_thread);

	return ret;
}

static void network_probe(struct bscapp_data *priv)
{
	start_probe_eth(priv);
	usleep(100000);
	start_probe_wifi(priv);
	usleep(100000);
}

static void network_remove(struct bscapp_data *priv)
{
#if 0
	switch (priv->net_intf) {
		case NET_INTF_ETH:
			mqtt_eth_deinit(&priv->h_me);
			break;
		case NET_INTF_WIFI:
			mqtt_wifi_deinit(&priv->h_mw);
			break;
		default:
			bsc_warn("unsupported net intf: %d\n", priv->net_intf);
			break;
	}
#else
	mqtt_eth_deinit(&priv->h_me);
	mqtt_wifi_deinit(&priv->h_mw);
#endif
	priv->net_wifi_ready = false;
	priv->net_eth_ready = false;
}

static void network_arbitrate(struct bscapp_data *priv)
{
	if (priv->net_eth_ready) {
		priv->net_intf = NET_INTF_ETH;
		bsc_info("use ethernet\n");
	} else if (priv->net_wifi_ready) {
		priv->net_intf = NET_INTF_WIFI;
		bsc_info("use wifi\n");
	} else {
		DEBUGASSERT(0); /* FIXME: shouldn't happen */
	}
}

static int bscapp_init(struct bscapp_data *priv)
{
	bsc_dbg("in\n");

	bzero(priv, sizeof(struct bscapp_data));
	sem_init(&priv->sem, 0, 0);
	pthread_mutex_init(&priv->mutex_exit, NULL);

#if 0
	uint32_t uid_0_31 = (*(volatile uint32_t *)(0x1ffff7e8));
	uint32_t uid_32_63 = (*(volatile uint32_t *)(0x1ffff7e8 + 4));
#endif
	uint32_t uid_64_95 = (*(volatile uint32_t *)(0x1ffff7e8 + 8));
#if BUILD_SPECIAL == BSCAPP_BUILD_TEST
	sprintf(priv->mparam.uid, "864-test");
#elif BUILD_SPECIAL == BSCAPP_BUILD_DEV
	sprintf(priv->mparam.uid, "864-dev");
#else
	sprintf(priv->mparam.uid, "864-%08x", uid_64_95);
#endif
	bsc_info("uid: %s\n", priv->mparam.uid);
	sprintf(priv->topic_sub_header, "/down/bs/%s", priv->mparam.uid);
	sprintf(priv->topic_pub_header, "/up/bs/%s", priv->mparam.uid);
	bsc_info("sub: %s\n", priv->topic_sub_header);
	bsc_info("pub: %s\n", priv->topic_pub_header);

	strcpy(priv->mparam.ssid, "wifi_ssid");
	strcpy(priv->mparam.psk, "wifi_psk");
	strcpy(priv->mparam.username, "admin");
	strcpy(priv->mparam.password, "Isb_C4OGD4c3");

	bsc_dbg("out\n");
	return OK;
}

static int bscapp_deinit(struct bscapp_data *priv)
{
	bsc_dbg("in\n");
	pthread_mutex_destroy(&priv->mutex_exit);
	sem_destroy(&priv->sem);
	network_remove(priv);
	bsc_dbg("out\n");
	return OK;
}

static int bscapp_hw_init(struct bscapp_data *priv)
{
	adc_init();
	bsc_pwm_init();
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

	bzero(priv, sizeof(struct bscapp_data));
	bscapp_hw_init(priv);
	bscapp_init(priv);

	do {
		/* start network probing threads */
		network_probe(priv);

		while (!network_ready(priv))
			sleep(1);

		network_arbitrate(priv);

		bsc_mqtt_connect(priv);
		start_mqttsub(priv);

		ret = sem_wait(&priv->sem);
		if (ret != 0)
			bsc_err("sem_wait failed\n");

#if BUILD_SPECIAL != BSCAPP_BUILD_RELEASE
		selftest_mqtt(priv);
#endif
		start_mqttpub(priv);
		usleep(100000);
		start_sample(priv);
		usleep(100000);

		/* TODO stop other net interface probe threads */

		while (!priv->rework) {
#ifdef MQTT_SELFPING_ENABLE
			sem_init(&priv->sem_sp, 0, 0);
			bsc_timer_start(&priv->timer_sp, MQTT_SELFPING_TIMEOUT, selfping_timeout);
			priv->selfping = true;
			bsc_dbg("waiting on semaphore\n");
			ret = sem_wait(&priv->sem_sp);
			if (ret != 0) {
				if (errno == EINTR)
					bsc_err("sem_wait() interrupted by signal, timeout\n" );
				else
					bsc_warn("sem_wait() unexpectedly interrupted\n" );
			} else {
				bsc_dbg("sem_wait() awaken normally\n" );
			}
			sem_destroy(&priv->sem_sp);
			bsc_timer_stop(priv->timer_sp);
			sleep(MQTT_SELFPING_INTERVAL - 1);
#endif
			sleep(1);
		}
		priv->rework = false;

		bsc_warn("prepare to rework\n");
		stop_thread(priv, priv->tid_mqttsub_thread, &priv->exit_mqttsub_thread);
		stop_thread(priv, priv->tid_mqttpub_thread, &priv->exit_mqttpub_thread);
		stop_thread(priv, priv->tid_sample_thread, &priv->exit_sample_thread);
		bsc_mqtt_disconnect(priv);
		sleep(1); /* wait a bit for disconnect packet flushing */
		network_remove(priv);
		bsc_info("---------- cut off ----------\n\n");
		sleep(1);
	} while (!priv->exit);

	bscapp_deinit(priv);

	bsc_info("exited\n");
	return OK;
}

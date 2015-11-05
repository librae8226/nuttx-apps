#ifndef __BSCAPP_H__
#define __BSCAPP_H__

struct bscapp_data {
	/* network type and status */
	int net_intf;
	bool net_eth_ready;
	bool net_wifi_ready;

	/* mqtt eth */
	Network n;
	Client c;
	void *h_me;


	/* mqtt wifi */
	void *h_mw;

	/* workers */
	pthread_t tid_mqttsub_thread;
	pthread_t tid_mqttpub_thread;
	pthread_t tid_sample_thread;
	pthread_t tid_probe_eth_thread;
	pthread_t tid_probe_wifi_thread;

	/* sync resources */
	sem_t sem;
	pthread_mutex_t mutex_exit;
	pthread_mutex_t mutex_mqtt;

	/* flags */
	volatile bool exit;
	volatile bool exit_mqttsub_thread;
	volatile bool exit_mqttpub_thread;
	volatile bool exit_sample_thread;
	volatile bool rework;

	/* buffers and strings */
	unsigned char buf[MQTT_BUF_MAX_LEN];
	unsigned char readbuf[MQTT_BUF_MAX_LEN];
	char uid[BSCAPP_UID_LEN];
	char topic_sub_header[MQTT_TOPIC_HEADER_LEN];
	char topic_pub_header[MQTT_TOPIC_HEADER_LEN];

#ifdef MQTT_SELFPING_ENABLE
	/* self ping */
	volatile bool selfping;
	timer_t timer_sp;
	sem_t sem_sp;
#endif
};

#endif /* __BSCAPP_H__ */

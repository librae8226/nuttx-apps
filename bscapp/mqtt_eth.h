#ifndef __MQTT_ETH_H__
#define __MQTT_ETH_H__

struct mqtt_eth {
	Network n;
	Client c;
	mqtt_msg_handler_t msg_handler;
	pthread_mutex_t mutex_mqtt;
	struct mqtt_param *mp;
};

#endif /* __MQTT_ETH_H__ */

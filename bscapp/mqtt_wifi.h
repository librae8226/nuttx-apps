#ifndef __MQTT_WIFI_H__
#define __MQTT_WIFI_H__

struct mqtt_wifi {
	void *h_wb; /* not used, coupled with wifi bridge */
	struct wifi_bridge *wb;
	struct mqtt_param *mp;
};

#endif /* __MQTT_WIFI_H__ */

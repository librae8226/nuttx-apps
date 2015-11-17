#ifndef __APP_UTILS_H__
#define __APP_UTILS_H__

#define BSCAPP_BUILD_RELEASE	0
#define BSCAPP_BUILD_TEST	1
#define BSCAPP_BUILD_DEV	2

/*
 * Experiment features for improvement.
 */
#define MQTT_SELFPING_ENABLE
#define MQTT_MUTEX_ENABLE
#define MQTT_JSON_ENABLE

/*
 * Comment out below lines to build release.
 */
#define BUILD_SPECIAL		BSCAPP_BUILD_DEV

#if BUILD_SPECIAL != BSCAPP_BUILD_RELEASE
//#define BSCAPP_DEBUG
#endif

#ifdef BSCAPP_DEBUG
#define bsc_dbg(format, ...) \
	syslog(LOG_DEBUG, "D/"EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define bsc_printf(format, ...) \
	printf(format, ##__VA_ARGS__)
#else
#define bsc_dbg(format, ...)
#define bsc_printf(format, ...)
#endif

#define bsc_info(format, ...) \
	syslog(LOG_INFO, "I/"EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define bsc_warn(format, ...) \
	syslog(LOG_WARNING, "W/"EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define bsc_err(format, ...) \
	syslog(LOG_ERR, "E/"EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)

#if BUILD_SPECIAL == BSCAPP_BUILD_DEV
#define MQTT_BROKER_IP		"123.57.208.39"
#define MQTT_BROKER_PORT	1883
#define URL_INET_ACCESS		"http://ipinfo.io"
#else /* RELEASE & TEST */
#define MQTT_BROKER_IP		"server.from-il.com"
#define MQTT_BROKER_PORT	1883
#define URL_INET_ACCESS		"http://ipinfo.io"
#endif

#define BSCAPP_UID_LEN		16
#define MQTT_BUF_MAX_LEN	128
#define MQTT_CMD_TIMEOUT	1000
#define MQTT_TOPIC_LEN		128
#define MQTT_TOPIC_HEADER_LEN	64
#define MQTT_SUBTOPIC_LEN	32
#define MQTT_USERNAME_LEN	32
#define MQTT_PASSWORD_LEN	32

/* it takes at most 60s to discover connection lost */
#define MQTT_SELFPING_TIMEOUT	30
#define MQTT_SELFPING_INTERVAL	30

#define WIFI_SSID_LEN		32
#define WIFI_PSK_LEN		32

#define NET_INTF_NULL		0
#define NET_INTF_WIFI		1
#define NET_INTF_ETH		2

typedef void (*mqtt_msg_handler_t)(char *topic, int topic_len, char *payload, int payload_len);

struct mqtt_param {
	uint8_t wbuf[MQTT_BUF_MAX_LEN];
	uint8_t rbuf[MQTT_BUF_MAX_LEN];
	uint8_t uid[BSCAPP_UID_LEN];
	uint8_t username[MQTT_USERNAME_LEN];
	uint8_t password[MQTT_PASSWORD_LEN];
	uint8_t ssid[WIFI_SSID_LEN];
	uint8_t psk[WIFI_PSK_LEN];
};

uint32_t millis(void);

#endif /* __APP_UTILS_H__ */

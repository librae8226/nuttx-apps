#ifndef __MQTT_WIFI_H__
#define __MQTT_WIFI_H__

#define ESP_TIMEOUT	2000

#define SLIP_START	0x7E
#define SLIP_END	0x7F
#define SLIP_REPL	0x7D
#define SLIP_ESC(x)	(x^0x20)

typedef enum
{
	CMD_NULL = 0,
	CMD_RESET,
	CMD_IS_READY,
	CMD_WIFI_CONNECT,
	CMD_MQTT_SETUP,
	CMD_MQTT_CONNECT,
	CMD_MQTT_DISCONNECT,
	CMD_MQTT_PUBLISH,
	CMD_MQTT_SUBSCRIBE,
	CMD_MQTT_LWT,
	CMD_MQTT_EVENTS,
	CMD_REST_SETUP,
	CMD_REST_REQUEST,
	CMD_REST_SETHEADER,
	CMD_REST_EVENTS
} CMD_NAME;

enum WIFI_STATUS {
	STATION_IDLE = 0,
	STATION_CONNECTING,
	STATION_WRONG_PASSWORD,
	STATION_NO_AP_FOUND,
	STATION_CONNECT_FAIL,
	STATION_GOT_IP
};

typedef struct {
	uint8_t *buf;
	uint16_t bufSize;
	uint16_t dataLen;
	uint8_t isEsc;
	uint8_t isBegin;
} PROTO;

typedef struct __attribute((__packed__)) {
	uint16_t len;
	uint8_t data;
} ARGS;

typedef struct __attribute((__packed__)) {
	uint16_t cmd;
	uint32_t callback;
	uint32_t _return;
	uint16_t argc;
	ARGS args;
} PACKET_CMD;

typedef void (*fp_cmd_callback)(void *);

struct resp_data {
	uint16_t arg_num;
	uint8_t *arg_ptr;
	PACKET_CMD *cmd;
	uint8_t buf[128]; /* FIXME MQTT_BUF_MAX_LEN duplicated! */
};

struct esp_data {
	uint32_t return_value;
	uint16_t return_cmd;
	bool is_return;
	bool _debugEn;
	PROTO _proto;
	uint8_t _protoBuf[512];
	int _chip_pd;
};

struct mqtt_data {
	uint32_t remote_instance;
};

struct mwifi_data {
	int fd;
	struct resp_data rd;
	struct esp_data ed;
	struct mqtt_data md;
};

#endif /* __MQTT_WIFI_H__ */

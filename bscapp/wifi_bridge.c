#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <debug.h>
#include <errno.h>
#include <fcntl.h>
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
#include <nuttx/clock.h>

#include "app_utils.h"
#include "wifi_bridge.h"

struct wifi_bridge g_wb;
static int g_slip_fd = 0;
static bool g_wifi_connected = false;
static bool g_mqtt_connected = false;

static int slip_open(char *dev)
{
	return open(dev, O_RDWR | O_NOCTTY);
}

static void slip_close(int fd)
{
	close(fd);
}

static int slip_write(int fd, char *buf, int n)
{
	int ret;
	if (!buf)
		return -EINVAL;
	ret = write(fd, buf, n);
	if (ret < 0)
		bsc_err("ret: %d, fd: %d, buf: %p, n: %d\n", ret, fd, buf, n);
	return ret;
}

#if 0
static int slip_write_str(int fd, char *str)
{
	int len;
	if (!str)
		return -EINVAL;
	len = strlen(str);
	return write(fd, str, len);
}
#endif

static bool slip_try_read(int fd, char *buf, uint32_t nb, uint32_t *nb_read, int timeout_ms)
{
	bool            res = true;
	ssize_t         ret;
	fd_set          rfds;
	struct timeval  tv;

	/* FIXME timeout_ms should be less than 1000 */

	tv.tv_sec = 0;
	tv.tv_usec = timeout_ms * 1000;
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	/*
	 * Wait until character received or timeout.
	 * Recover in case of an interrupted read system call.
	 */

	do {
		if (select(fd + 1, &rfds, NULL, NULL, &tv) == -1) {
			if (errno != EINTR)
				res = false;
		} else if (FD_ISSET(fd, &rfds)) {
			if ((ret = read(fd, buf, nb)) <= 0) {
				res = false;
			} else {
				*nb_read = (uint32_t)ret;
				break;
			}
		} else {
			*nb_read = 0;
			res = false;
			break;
		}
	} while (res == true);
	return res;
}

static bool slip_try_read_char(int fd, char *pch)
{
	uint32_t nread;
	return slip_try_read(fd, pch, 1, &nread, 10);
}

void *wifi_bridge_init(void)
{
	struct wifi_bridge *wb = &g_wb;

	/* TODO malloc? */

	if (!wb) {
		bsc_err("failed");
		return NULL;
	}

	bzero(wb, sizeof(struct wifi_bridge));

	wb->fd = slip_open("/dev/ttyS1");
	if (wb->fd < 0) {
		bsc_err("fd: %d, failed\n", wb->fd);
		return NULL;
	}

	g_slip_fd = wb->fd;

	return (void *)wb;
}

void wifi_bridge_deinit(struct wifi_bridge *wb)
{
	slip_close(wb->fd);
}

static uint32_t millis(void)
{
	uint32_t ticktime, sec, remainder;
	ticktime = clock_systimer();
	sec = ticktime / CLOCKS_PER_SEC;
	remainder = (uint32_t)(ticktime % CLOCKS_PER_SEC);
	return sec*1000+remainder;
}

static uint16_t crc16_add(uint8_t b, uint16_t acc)
{
	acc ^= b;
	acc  = (acc >> 8) | (acc << 8);
	acc ^= (acc & 0xff00) << 4;
	acc ^= (acc >> 8) >> 4;
	acc ^= (acc & 0xff00) >> 5;
	return acc;
}

static uint16_t crc16_data(const uint8_t *data, int len, uint16_t acc)
{
	int i;

	for(i = 0; i < len; ++i) {
		acc = crc16_add(*data, acc);
		++data;
	}
	return acc;
}

/* _serial->write() */
static int __esp_slip_write(char c)
{
	return slip_write(g_slip_fd, &c, 1);
}

static int __esp_slip_try_read(char *pch)
{
	return slip_try_read_char(g_slip_fd, pch);
}

static void __esp_write_1(uint8_t data)
{
	switch(data){
		case SLIP_START:
		case SLIP_END:
		case SLIP_REPL:
			__esp_slip_write(SLIP_REPL);
			__esp_slip_write(SLIP_ESC(data));
			break;
		default:
			__esp_slip_write(data);
	}
}

static void __esp_write_2(uint8_t* data, uint16_t len)
{
	while(len --)
		__esp_write_1(*data ++);
}

static uint16_t __esp_request_4(uint16_t cmd, uint32_t callback, uint32_t _return, uint16_t argc)
{
	uint16_t crc = 0;
	__esp_slip_write(0x7E);
	__esp_write_2((uint8_t*)&cmd, 2);
	crc = crc16_data((uint8_t*)&cmd, 2, crc);

	__esp_write_2((uint8_t*)&callback, 4);
	crc = crc16_data((uint8_t*)&callback, 4, crc);

	__esp_write_2((uint8_t*)&_return, 4);
	crc = crc16_data((uint8_t*)&_return, 4, crc);

	__esp_write_2((uint8_t*)&argc, 2);
	crc = crc16_data((uint8_t*)&argc, 2, crc);
	return crc;
}

static uint16_t __esp_request_3(uint16_t crc_in, uint8_t* data, uint16_t len)
{
	uint8_t temp = 0;
	uint16_t pad_len = len;
	while(pad_len % 4 != 0)
		pad_len++;
	__esp_write_2((uint8_t*)&pad_len, 2);
	crc_in = crc16_data((uint8_t*)&pad_len, 2, crc_in);
	while(len --){
		__esp_write_1(*data);
		crc_in = crc16_data((uint8_t*)data, 1, crc_in);
		data ++;
		if(pad_len > 0) pad_len --;
	}

	while(pad_len --){
		__esp_write_1(temp);
		crc_in = crc16_data((uint8_t*)&temp, 1, crc_in);
	}
	return crc_in;
}

static void __esp_request_1(uint16_t crc)
{
	__esp_write_2((uint8_t*)&crc, 2);
	__esp_slip_write(0x7F);
}

static void esp_init(struct esp_data *e)
{
	e->_debugEn = true;
	e->_proto.buf = e->_protoBuf;
	e->_proto.bufSize = sizeof(e->_protoBuf);
	e->_proto.dataLen = 0;
	e->_proto.isEsc = 0;
	e->_proto.isBegin = 0;
//	pinMode(_chip_pd, OUTPUT);
}

static void esp_protoCompletedCb(struct esp_data *e)
{
	PACKET_CMD *cmd = (PACKET_CMD*)e->_proto.buf;
	uint16_t crc = 0, argc, len, resp_crc;
	uint8_t *data_ptr;
#if 0
	int i;
	bsc_info("len: %d\n", e->_proto.dataLen);
	bsc_info("size: %d\n", e->_proto.bufSize);
	bsc_info("buf: \n");
	for (i = 0; i < e->_proto.dataLen; i++) {
		if (i % 32 == 0)
			bsc_printf("\n");
		bsc_printf("%02x ", e->_proto.buf[i]);
	}
	bsc_printf("\n");
#endif
	argc = cmd->argc;
	data_ptr = (uint8_t*)&cmd->args;
	crc = crc16_data((uint8_t*)&cmd->cmd, 12, crc);

	while(argc--){
		len = *((uint16_t*)data_ptr);
		crc = crc16_data(data_ptr, 2, crc);
		data_ptr += 2;
		while(len--){
			crc = crc16_data(data_ptr, 1, crc);
			data_ptr ++;
		}
	}

	resp_crc =  *(uint16_t*)data_ptr;
	if(crc != resp_crc) {
		bsc_info("Invalid CRC\n");
		return;
	}

	fp_cmd_callback fp;
	if(cmd->callback != 0){
		fp = (fp_cmd_callback)cmd->callback;

		e->return_cmd = cmd->cmd;
		e->return_value = cmd->_return;

		if (fp)
			fp((void *)cmd);
	} else {
		if(cmd->argc == 0) {
			e->is_return = true;
			e->return_cmd = cmd->cmd;
			e->return_value = cmd->_return;
		}

	}
#if 0
	bsc_info("is_return %d, return_cmd %d, return_value %d\n",
			e->is_return, e->return_cmd, e->return_value);
#endif
}

static void esp_process(struct esp_data *e)
{
	char value;
	while (__esp_slip_try_read(&value)) {
		switch (value) {
			case 0x7D:
				e->_proto.isEsc = 1;
				break;

			case 0x7E:
				e->_proto.dataLen = 0;
				e->_proto.isEsc = 0;
				e->_proto.isBegin = 1;
				break;

			case 0x7F:
				esp_protoCompletedCb(e);
				e->_proto.isBegin = 0;
				break;

			default:
				if(e->_proto.isBegin == 0) {
					if(e->_debugEn) {
						bsc_printf("%c", value);
					}
					break;
				}
				if(e->_proto.isEsc){
					value ^= 0x20;
					e->_proto.isEsc = 0;
				}

				if(e->_proto.dataLen < e->_proto.bufSize)
					e->_proto.buf[e->_proto.dataLen++] = value;

				break;
		}
	}
}
static void esp_enable(struct esp_data *e)
{
	usleep(500000);
}

static void esp_reset(struct esp_data *e)
{
	uint16_t crc = __esp_request_4(CMD_RESET, 0, 0, 0);
	__esp_request_1(crc);
	usleep(500000);
}

static bool esp_ready(struct esp_data *e)
{
	uint32_t wait;
	uint16_t crc;
	uint8_t wait_time;

	for (wait_time = 5; wait_time > 0; wait_time--) {
		e->is_return = false;
		e->return_value = 0;
		crc = __esp_request_4(CMD_IS_READY, 0, 1, 0);
		__esp_request_1(crc);
		wait = millis();
		while (e->is_return == false && (millis() - wait < 1000)) {
			esp_process(e);
		}
		if(e->is_return && e->return_value)
			return true;
	}
	return false;
}

static bool esp_waitReturn(struct esp_data *e, uint32_t timeout)
{
	uint32_t wait;

	e->is_return = false;
	e->return_value = 0;
	e->return_cmd = 0;

	wait = millis();
	while(e->is_return == false && (millis() - wait < timeout)) {
		esp_process(e);
	}
	return e->is_return;
}

static void esp_resp_create(struct resp_data *r, void *response)
{
	r->cmd = (PACKET_CMD*)response;
	r->arg_ptr = (uint8_t*)&r->cmd->args;
	r->arg_num = 0;
	bzero(r->buf, MQTT_BUF_MAX_LEN);
}

static uint16_t esp_resp_getArgc(struct resp_data *r)
{
	return r->cmd->argc;
}

static uint16_t esp_resp_argLen(struct resp_data *r)
{
	return *(uint16_t*)r->arg_ptr;
}

static int32_t esp_resp_popArgs(struct resp_data *r, uint8_t *data, uint16_t maxLen)
{
	uint16_t length, len, incLen = 0;

	if(r->arg_num >= r->cmd->argc)
		return -1;

	length = *(uint16_t*)r->arg_ptr;
	len = length;
	r->arg_ptr += 2;

	while(length --){
		*data ++ = *(r->arg_ptr)++;
		incLen ++;
		if(incLen > maxLen){
			r->arg_num ++;
			return maxLen;
		}

	}
	r->arg_num++;
	return len;
}

static char *esp_resp_popString(struct resp_data *r)
{
	uint8_t *pbuf = r->buf;
	uint16_t len = *(uint16_t*)r->arg_ptr;

	/* FIXME truncated */
	if (len > MQTT_BUF_MAX_LEN)
		len = MQTT_BUF_MAX_LEN;

	r->arg_ptr += 2;
	strncpy((char *)pbuf, (const char *)r->arg_ptr, len);
	r->arg_ptr += len;
	r->arg_num++;

	return (char *)r->buf;
}

static void esp_wifi_cb(void* response)
{
	uint32_t status;
	struct resp_data rd;
	esp_resp_create(&rd, response);

	if(esp_resp_getArgc(&rd) == 1) {
		esp_resp_popArgs(&rd, (uint8_t*)&status, 4);
		if(status == STATION_GOT_IP) {
			bsc_info("WIFI CONNECTED\n");
			g_wifi_connected = true;
		} else {
			g_wifi_connected = false;
//			esp_mqtt_disconnect(wb); /* FIXME wifi disconnect handling */
			bsc_info("wifi status: %d\n", status);
		}
	}
}

static void esp_wifi_connect(struct wifi_bridge *wb, const char* ssid, const char* password)
{
	uint16_t crc;
	crc = __esp_request_4(CMD_WIFI_CONNECT, (uint32_t)&esp_wifi_cb, 0, 2);
	crc = __esp_request_3(crc,(uint8_t*)ssid, strlen(ssid));
	crc = __esp_request_3(crc,(uint8_t*)password, strlen(password));
	__esp_request_1(crc);
}

static void esp_mqtt_connected_cb(void* response)
{
	bsc_info("Connected\n");
	g_mqtt_connected = true;
}

static void esp_mqtt_disconnected_cb(void* response)
{
	bsc_info("Disconnected\n");
	g_mqtt_connected = false;
}

static void esp_mqtt_data_cb(void* response)
{
	struct resp_data rd;
	esp_resp_create(&rd, response);

	bsc_info("Received topic: %s\n", esp_resp_popString(&rd));
	bsc_info("data: %s\n", esp_resp_popString(&rd));
}

static void esp_mqtt_published_cb(void* response)
{
	bsc_info("Published\n");
}

static bool esp_mqtt_lwt(struct wifi_bridge *wb, const char* topic, const char* message, uint8_t qos, uint8_t retain)
{
	struct esp_data *e = &wb->ed;
	struct mqtt_data *m = &wb->md;
	uint16_t crc;

	crc = __esp_request_4(CMD_MQTT_LWT, 0, 1, 5);
	crc = __esp_request_3(crc,(uint8_t*)&m->remote_instance, 4);
	crc = __esp_request_3(crc,(uint8_t*)topic, strlen(topic));
	crc = __esp_request_3(crc,(uint8_t*)message, strlen(message));
	crc = __esp_request_3(crc,(uint8_t*)&qos, 1);
	crc = __esp_request_3(crc,(uint8_t*)&retain, 1);
	__esp_request_1(crc);
	if(esp_waitReturn(e, ESP_TIMEOUT) && e->return_value)
		return true;
	return false;
}

static void esp_mqtt_connect(struct wifi_bridge *wb, const char* host, uint32_t port, bool security)
{
	struct mqtt_data *m = &wb->md;
	uint16_t crc;
	crc = __esp_request_4(CMD_MQTT_CONNECT, 0, 0, 4);
	crc = __esp_request_3(crc,(uint8_t*)&m->remote_instance, 4);
	crc = __esp_request_3(crc,(uint8_t*)host, strlen(host));
	crc = __esp_request_3(crc,(uint8_t*)&port, 4);
	crc = __esp_request_3(crc,(uint8_t*)&security, 1);
	__esp_request_1(crc);
}

static void esp_mqtt_disconnect(struct wifi_bridge *wb)
{
	struct mqtt_data *m = &wb->md;
	uint16_t crc;
	crc = __esp_request_4(CMD_MQTT_DISCONNECT, 0, 0, 1);
	crc = __esp_request_3(crc,(uint8_t*)&m->remote_instance, 4);
	__esp_request_1(crc);
}

static void esp_mqtt_subscribe(struct wifi_bridge *wb, const char* topic, uint8_t qos)
{
	struct mqtt_data *m = &wb->md;
	uint16_t crc;
	crc = __esp_request_4(CMD_MQTT_SUBSCRIBE, 0, 0, 3);
	crc = __esp_request_3(crc,(uint8_t*)&m->remote_instance, 4);
	crc = __esp_request_3(crc,(uint8_t*)topic, strlen(topic));
	crc = __esp_request_3(crc,(uint8_t*)&qos, 1);
	__esp_request_1(crc);

}

static void esp_mqtt_publish(struct wifi_bridge *wb, const char* topic, char* data, uint8_t qos, uint8_t retain)
{
	struct mqtt_data *m = &wb->md;
	uint16_t crc;
	uint16_t len = strlen(data);
	crc = __esp_request_4(CMD_MQTT_PUBLISH, 0, 0, 6);
	crc = __esp_request_3(crc,(uint8_t*)&m->remote_instance, 4);
	crc = __esp_request_3(crc,(uint8_t*)topic, strlen(topic));
	crc = __esp_request_3(crc,(uint8_t*)data, len);
	crc = __esp_request_3(crc,(uint8_t*)&len, 2);
	crc = __esp_request_3(crc,(uint8_t*)&qos, 1);
	crc = __esp_request_3(crc,(uint8_t*)&retain, 1);
	__esp_request_1(crc);
}

/* mqtt.begin() */
static bool esp_mqtt_setup(struct wifi_bridge *wb, const char* client_id, const char* user, const char* pass, uint16_t keep_alive, bool clean_seasion)
{
	struct esp_data *e = &wb->ed;
	struct mqtt_data *m = &wb->md;
	uint16_t crc;
	uint32_t cb_data;

	crc = __esp_request_4(CMD_MQTT_SETUP, 0, 1, 9);
	crc = __esp_request_3(crc,(uint8_t*)client_id, strlen(client_id));
	crc = __esp_request_3(crc,(uint8_t*)user, strlen(user));
	crc = __esp_request_3(crc,(uint8_t*)pass, strlen(pass));
	crc = __esp_request_3(crc,(uint8_t*)&keep_alive, 2);
	crc = __esp_request_3(crc,(uint8_t*)&clean_seasion, 1);
	cb_data = (uint32_t)&esp_mqtt_connected_cb;
	crc = __esp_request_3(crc,(uint8_t*)&cb_data, 4);

	cb_data = (uint32_t)&esp_mqtt_disconnected_cb;
	crc = __esp_request_3(crc,(uint8_t*)&cb_data, 4);

	cb_data = (uint32_t)&esp_mqtt_published_cb;

	crc = __esp_request_3(crc,(uint8_t*)&cb_data, 4);

	cb_data = (uint32_t)&esp_mqtt_data_cb;
	crc = __esp_request_3(crc,(uint8_t*)&cb_data, 4);
	__esp_request_1(crc);

	if(esp_waitReturn(e, ESP_TIMEOUT) == false || e->return_cmd == 0 || e->return_value == 0)
		return false;
	m->remote_instance = e->return_value;
	return true;
}

int wifi_bridge_unit_test(void **h_wb)
{
	struct wifi_bridge *wb = NULL;
	bsc_info("in\n");

	wb = (struct wifi_bridge *)wifi_bridge_init();
	if (!wb) {
		bsc_err("failed");
		return -EFAULT;
	}
	*h_wb = (void *)wb;

	esp_init(&wb->ed);
	esp_enable(&wb->ed);
	esp_reset(&wb->ed);
	while (!esp_ready(&wb->ed)) {
		bsc_info("wait for esp\n");
	}
	bsc_info("esp ready.\n");

	while (!esp_mqtt_setup(wb, "DVES_duino", "admin", "Isb_C4OGD4c3", 120, 1)) {
		bsc_info("wait for mqtt setup\n");
	}
	bsc_info("mqtt setup settled.\n");

	while (!esp_mqtt_lwt(wb, "/lwt", "offline", 0, 0)) {
		bsc_info("wait for mqtt lwt\n");
	}
	bsc_info("mqtt lwp done.\n");

	esp_wifi_connect(wb, "Xiaomi_FD26", "basicbox565");

	while (!g_wifi_connected) {
		esp_process(&wb->ed);
	}

	esp_mqtt_connect(wb, "123.57.208.39", 1883, false);

	while (!g_mqtt_connected) {
		esp_process(&wb->ed);
	}

	esp_mqtt_subscribe(wb, "#", 1);
	esp_mqtt_publish(wb, "/topic/0", "data0", 0, 0);

	while (1) {
		esp_process(&wb->ed);
	}

	wifi_bridge_deinit(wb);
	bsc_info("out\n");
	return 0;
}

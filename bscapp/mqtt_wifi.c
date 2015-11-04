#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <debug.h>
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

#include <apps/netutils/MQTTClient.h>
#include "mqtt_wifi.h"
#include "bscapp.h"

static int g_slip_fd;

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

	/* Wait until character received or timeout. Recover in case of an
	 * interrupted read system call.
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

int mqtt_wifi_subscribe(struct bscapp_data *priv, char *topic, mqtt_msg_handler_t mh)
{
	bsc_info("subscribing to %s\n", topic);
	while (!priv->exit_mqttsub_thread) {
		/* do process */
		usleep(100000);
	}
	return OK;
}

int mqtt_wifi_publish(struct bscapp_data *priv, char *topic, char *payload)
{
	char msgbuf[MQTT_BUF_MAX_LEN];
	int ret = OK;

	if (topic == NULL || payload == NULL)
		return -EINVAL;

	bsc_dbg("pub topic: %s\n", topic);
	bsc_dbg("payload  : %s\n", payload);

	bzero(msgbuf, sizeof(msgbuf));
	if (strlen(payload) > MQTT_BUF_MAX_LEN) {
		bsc_warn("can't handle payload length>%d (%d)\n", MQTT_BUF_MAX_LEN, strlen(payload));
		return -EOVERFLOW;
	}

	strcpy(msgbuf, payload);

	/* do publish */

	return ret;
}

int mqtt_wifi_connect(struct bscapp_data *priv)
{
	bsc_info("connecting to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	bsc_info("connecting mqtt...\n");
	bsc_info("connected to broker %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
	return OK;
}

int mqtt_wifi_disconnect(struct bscapp_data *priv)
{
	return OK;
}

int mqtt_wifi_init(struct mwifi_data *mw)
{
	bzero(mw, sizeof(struct mwifi_data));

	mw->fd = slip_open("/dev/ttyS1");
	if (mw->fd < 0)
		bsc_err("fd: %d, failed\n", mw->fd);

	g_slip_fd = mw->fd;

	return mw->fd;
}

void mqtt_wifi_deinit(struct mwifi_data *mw)
{
	slip_close(mw->fd);
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

static void esp_init(struct mwifi_data *mw)
{
	struct esp_data *e = &mw->ed;

	e->_debugEn = true;
	e->_proto.buf = e->_protoBuf;
	e->_proto.bufSize = sizeof(e->_protoBuf);
	e->_proto.dataLen = 0;
	e->_proto.isEsc = 0;
	e->_proto.isBegin = 0;
//	pinMode(_chip_pd, OUTPUT);
}

static void esp_protoCompletedCb(struct mwifi_data *mw)
{
	struct esp_data *e = &mw->ed;
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

//	FP<void, void*> *fp;
	fp_cmd_callback fp;
	if(cmd->callback != 0){
//		fp = (FP<void, void*>*)cmd->callback;
		fp = (fp_cmd_callback)cmd->callback;

		e->return_cmd = cmd->cmd;
		e->return_value = cmd->_return;

//		if(fp->attached())
//			(*fp)((void*)cmd);
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
	bsc_info("is_return %d, return_cmd %d, return_value %d\n", e->is_return, e->return_cmd, e->return_value);
#endif
}

static void esp_process(struct mwifi_data *mw)
{
	struct esp_data *e = &mw->ed;
	char value;
	while (slip_try_read_char(mw->fd, &value)) {
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
				esp_protoCompletedCb(mw);
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
static void esp_enable(struct mwifi_data *mw)
{
	usleep(500000);
}

static void esp_reset(struct mwifi_data *mw)
{
	uint16_t crc = __esp_request_4(CMD_RESET, 0, 0, 0);
	__esp_request_1(crc);
	usleep(500000);
}

static bool esp_ready(struct mwifi_data *mw)
{
	struct esp_data *e = &mw->ed;
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
			esp_process(mw);
		}
		if(e->is_return && e->return_value)
			return true;
	}
	return false;
}

static bool esp_waitReturn(struct mwifi_data *mw, uint32_t timeout)
{
	struct esp_data *e = &mw->ed;
	uint32_t wait;

	e->is_return = false;
	e->return_value = 0;
	e->return_cmd = 0;

	wait = millis();
	while(e->is_return == false && (millis() - wait < timeout)) {
		esp_process(mw);
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
	r->arg_num ++;
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
	r->arg_num++;

	return (char *)r->buf;
}

#if 0
void esp_resp_popString(struct resp_data *r, String* data)
{
	uint16_t len = *(uint16_t*)r->arg_ptr;
	r->arg_ptr += 2;
	while(len --)
		data->concat( (char)*(r->arg_ptr) ++);
	arg_num ++;
}
#endif

static void esp_mqtt_connected_cb(void* response)
{
	bsc_info("Connected\n");
/*
	esp_mqtt_subscribe("/topic/0"); //or mqtt.subscribe("topic"); with qos = 0
	esp_mqtt_subscribe("/topic/1");
	esp_mqtt_subscribe("/topic/2");
	esp_mqtt_publish("/topic/0", "data0");
*/
}

static void esp_mqtt_disconnected_cb(void* response)
{
	bsc_info("Disconnected\n");
}

static void esp_mqtt_data_cb(void* response)
{
//	RESPONSE res(response);
	struct resp_data rd;
	esp_resp_create(&rd, response);

	bsc_info("Received topic: %s\n", esp_resp_popString(&rd));
	//String topic = res.popString();

	bsc_info("data: %s\n", esp_resp_popString(&rd));
	//String data = res.popString();
}

static void esp_mqtt_published_cb(void* response)
{
	bsc_info("Published\n");
}

static bool esp_mqtt_lwt(struct mwifi_data *mw, const char* topic, const char* message, uint8_t qos, uint8_t retain)
{
	struct esp_data *e = &mw->ed;
	struct mqtt_data *m = &mw->md;
	uint16_t crc;

	crc = __esp_request_4(CMD_MQTT_LWT, 0, 1, 5);
	crc = __esp_request_3(crc,(uint8_t*)&m->remote_instance, 4);
	crc = __esp_request_3(crc,(uint8_t*)topic, strlen(topic));
	crc = __esp_request_3(crc,(uint8_t*)message, strlen(message));
	crc = __esp_request_3(crc,(uint8_t*)&qos, 1);
	crc = __esp_request_3(crc,(uint8_t*)&retain, 1);
	__esp_request_1(crc);
	if(esp_waitReturn(mw, ESP_TIMEOUT) && e->return_value)
		return true;
	return false;
}

static void esp_mqtt_connect(struct mwifi_data *mw, const char* host, uint32_t port, bool security)
{
	struct mqtt_data *m = &mw->md;
	uint16_t crc;
	crc = __esp_request_4(CMD_MQTT_CONNECT, 0, 0, 4);
	crc = __esp_request_3(crc,(uint8_t*)&m->remote_instance, 4);
	crc = __esp_request_3(crc,(uint8_t*)host, strlen(host));
	crc = __esp_request_3(crc,(uint8_t*)&port, 4);
	crc = __esp_request_3(crc,(uint8_t*)&security, 1);
	__esp_request_1(crc);
}

static void esp_mqtt_disconnect(struct mwifi_data *mw)
{
	struct mqtt_data *m = &mw->md;
	uint16_t crc;
	crc = __esp_request_4(CMD_MQTT_DISCONNECT, 0, 0, 1);
	crc = __esp_request_3(crc,(uint8_t*)&m->remote_instance, 4);
	__esp_request_1(crc);
}

static void esp_mqtt_subscribe(struct mwifi_data *mw, const char* topic, uint8_t qos)
{
	struct mqtt_data *m = &mw->md;
	uint16_t crc;
	crc = __esp_request_4(CMD_MQTT_SUBSCRIBE, 0, 0, 3);
	crc = __esp_request_3(crc,(uint8_t*)&m->remote_instance, 4);
	crc = __esp_request_3(crc,(uint8_t*)topic, strlen(topic));
	crc = __esp_request_3(crc,(uint8_t*)&qos, 1);
	__esp_request_1(crc);

}

static void esp_mqtt_publish(struct mwifi_data *mw, const char* topic, char* data, uint8_t qos, uint8_t retain)
{
	struct mqtt_data *m = &mw->md;
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
static bool esp_mqtt_setup(struct mwifi_data *mw, const char* client_id, const char* user, const char* pass, uint16_t keep_alive, bool clean_seasion)
{
	struct esp_data *e = &mw->ed;
	struct mqtt_data *m = &mw->md;
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

	if(esp_waitReturn(mw, ESP_TIMEOUT) == false || e->return_cmd == 0 || e->return_value == 0)
		return false;
	m->remote_instance = e->return_value;
	return true;
}

bool mqtt_wifi_test(struct mwifi_data *mw)
{
	bsc_printf("%s entry\n", __func__);
	esp_init(mw);
	esp_enable(mw);
	esp_reset(mw);
	while (!esp_ready(mw)) {
		bsc_info("wait for esp\n");
	}
	bsc_info("esp ready.\n");

	while (!esp_mqtt_setup(mw, "DVES_duino", "admin", "Isb_C4OGD4c3", 120, 1)) {
		bsc_info("wait for mqtt setup\n");
	}
	bsc_info("mqtt setup settled.\n");

	while (!esp_mqtt_lwt(mw, "/lwt", "offline", 0, 0)) {
		bsc_info("wait for mqtt lwt\n");
	}
	bsc_info("mqtt lwp done.\n");
#if 0
	char ch = 0;
	uint32_t nread = 0;
	while (1) {
		if (slip_try_read(mw->fd, &ch, 1, &nread, 10))
			bsc_info("got: 0x%02x, nread: %d\n", ch, nread);
	}
#endif
	return true;
}

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

#include <apps/netutils/MQTTClient.h>
#include "mqtt_wifi.h"
#include "bscapp.h"

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
	if (!buf)
		return -EINVAL;
	return write(fd, buf, n);
}

static int slip_write_str(int fd, char *str)
{
	int len;
	if (!str)
		return -EINVAL;
	len = strlen(str);
	return write(fd, str, len);
}

static int slip_write_char(int fd, char c)
{
	return write(fd, &c, 1);
}

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

int mqtt_wifi_init(struct bscapp_data *priv)
{
	struct mwifi_data *mw = &priv->mwd;
	bzero(mw, sizeof(struct mwifi_data));

	mw->fd = slip_open("/dev/ttyS1");
	if (mw->fd < 0)
		bsc_err("fd: %d, failed\n", mw->fd);
	return mw->fd;
}

void mqtt_wifi_deinit(struct bscapp_data *priv)
{
	struct mwifi_data *mw = &priv->mwd;
	slip_close(mw->fd);
}

void mqtt_wifi_test(struct bscapp_data *priv)
{
	struct mwifi_data *mw = &priv->mwd;
	char ch = 0;
	uint32_t nread = 0;
	while (1) {
		if (slip_try_read(mw->fd, &ch, 1, &nread, 10))
			bsc_info("got: 0x%02x, nread: %d\n", ch, nread);
	}
}

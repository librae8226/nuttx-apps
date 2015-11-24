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

#include <apps/app_utils.h>

#include "neurite.h"

struct neurite_priv_s {
	int fd;
};

struct neurite_s g_nr;
struct neurite_priv_s g_priv;

static int nr_open(char *dev)
{
	return open(dev, O_RDWR | O_NOCTTY);
}

static void nr_close(int fd)
{
	close(fd);
}

static int nr_write(int fd, char *buf, int n)
{
	int ret;
	if (!buf)
		return -EINVAL;
	ret = write(fd, buf, n);
	if (ret < 0)
		log_err("ret: %d, fd: %d, buf: %p, n: %d\n", ret, fd, buf, n);
	return ret;
}

#if 0
static int neurite_write_str(int fd, char *str)
{
	int len;
	if (!str)
		return -EINVAL;
	len = strlen(str);
	return write(fd, str, len);
}
#endif

static bool nr_try_read(int fd, char *buf, uint32_t nb, uint32_t *nb_read, int timeout_ms)
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

static bool nr_try_read_char(int fd, char *pch)
{
	uint32_t nread;
	return nr_try_read(fd, pch, 1, &nread, 1);
}

/* _serial->write() */
static int neurite_write(void *h, char c)
{
	struct neurite_priv_s *priv = (struct neurite_priv_s *)h;
	return nr_write(priv->fd, &c, 1);
}

static int neurite_try_read(void *h, char *pch)
{
	struct neurite_priv_s *priv = (struct neurite_priv_s *)h;
	return nr_try_read_char(priv->fd, pch);
}

#if 0
static void esp_protoCompletedCb(struct esp_data *e)
{
	PACKET_CMD *cmd = (PACKET_CMD*)e->_proto.buf;
	uint16_t crc = 0, argc, len, resp_crc;
	uint8_t *data_ptr;
#if 0
	int i;
	log_info("len: %d\n", e->_proto.dataLen);
	log_info("size: %d\n", e->_proto.bufSize);
	log_info("buf: \n");
	for (i = 0; i < e->_proto.dataLen; i++) {
		if (i % 32 == 0)
			log_printf("\n");
		log_printf("%02x ", e->_proto.buf[i]);
	}
	log_printf("\n");
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
		log_err("Invalid CRC\n");
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
}
void esp_process(struct esp_data *e)
{
	char value;
	while (neurite_try_read(&value)) {
		switch (value) {
			case 0x7D:
				break;

			case 0x7E:
				break;

			case 0x7F:
				break;

			default:
//				if(e->_proto.dataLen < e->_proto.bufSize)
//					e->_proto.buf[e->_proto.dataLen++] = value;
				break;
		}
	}
}
#endif

static void neurite_deinit(void *h)
{
	struct neurite_priv_s *priv = (struct neurite_priv_s *)h;
	nr_close(priv->fd);
}

struct neurite_s *neurite_init(void)
{
	struct neurite_priv_s *priv = &g_priv;
	struct neurite_s *nr = &g_nr;

	/* TODO malloc? */

	if (!priv) {
		log_err("failed\n");
		return NULL;
	}

	bzero(priv, sizeof(struct neurite_priv_s));

	priv->fd = nr_open("/dev/ttyS1");
	if (priv->fd < 0) {
		log_err("fd: %d, failed\n", priv->fd);
		return NULL;
	}

	nr->priv = priv;
	nr->deinit = neurite_deinit;
	nr->write = neurite_write;
	nr->try_read = neurite_try_read;

	return (void *)nr;
}

int neurite_unit_test(void)
{
	struct neurite_priv_s *priv = NULL;
	struct neurite_s *nr = NULL;
	log_info("in\n");

	nr = (struct neurite_priv_s *)neurite_init();
	if (!nr) {
		log_err("failed");
		return -EFAULT;
	}
	nr->deinit(nr->priv);
	log_info("out\n");
	return 0;
}

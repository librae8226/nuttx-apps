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

#include <apps/netutils/cJSON.h>
#include <apps/app_utils.h>

#include "neurite.h"

#define NR_BUF_SIZE	26
#define NR_NROWS	16
static char nr_buf[NR_BUF_SIZE];
static char nr_buf_len;
static int nr_display_row;
static char display_buf[NR_NROWS][NR_BUF_SIZE];

static void display_refresh(void)
{
	int i, j;
	for (i = 0; i < NR_NROWS; i++) {
		if (display_buf[i][0] != 0) {
			nr_display_clear_row(i);
			nr_display_str(display_buf[i], 0, i);
		}
	}
}

static void display_scroll(void)
{
	int i;
	for (i = 0; i < NR_NROWS - 1; i++) {
		memset(display_buf[i], ' ', NR_BUF_SIZE);
		strncpy(display_buf[i], display_buf[i+1], NR_BUF_SIZE);
	}
}

static void display_add_str(char *str)
{
	DEBUGASSERT(str);
	if (nr_display_row == NR_NROWS) {
		display_scroll();
		strncpy(display_buf[nr_display_row-1], str, NR_BUF_SIZE);
	} else {
		strncpy(display_buf[nr_display_row], str, NR_BUF_SIZE);
		nr_display_row++;
	}
}

static void frame_end_callback(void)
{
	display_add_str(nr_buf);
	display_refresh();
}

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int neuron_main(int argc, char *argv[])
#endif
{
	int ret = OK;
	struct neurite_s *nr = NULL;

	log_info("entry\n");

	bzero(nr_buf, sizeof(nr_buf));
	nr_buf_len = 0;
	bzero(display_buf, sizeof(display_buf));
	nr_display_row = 0;

	ret = nr_display_init();
	if (ret < 0) {
		log_err("nr_display_init failed\n");
		goto neuron_exit;
	}

	nr_display_clear();
	nr_display_str("Made with love by", 4, 7);
	nr_display_str("linkgo.io", 8, 8);
	sleep(2);
	nr_display_clear();

	nr = neurite_init();
	if (!nr) {
		log_err("neurite_init failed\n");
		goto neuron_exit;
	}

	while (1) {
		char value;
		while (nr->try_read(nr->priv, &value)) {
			switch (value) {
				case 0x0a:
					frame_end_callback();
					nr_buf_len = 0;
					bzero(nr_buf, sizeof(nr_buf));
					break;
				default:
//					log_dbg("input: %d\n", value);
					if (nr_buf_len < NR_BUF_SIZE)
						nr_buf[nr_buf_len++] = value;
					break;
			}
		}
	}

	nr->deinit(nr->priv);
	nr_display_deinit();

neuron_exit:
	log_info("exited\n");
	return ret;
}

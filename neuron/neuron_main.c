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

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int neuron_main(int argc, char *argv[])
#endif
{
	int ret = OK;

	log_info("entry\n");

	ret = nr_display_init();
	if (ret < 0) {
		log_info("entry\n");
		goto neuron_exit;
	}

	nr_display_str("hi linkgo.io 0", 0, 0);
	nr_display_str("hi linkgo.io 1", 0, 1);
	nr_display_str("hi linkgo.io 2", 0, 2);
	nr_display_str("hi linkgo.io 3 a long long string", 0, 3);
	nr_display_str("hi linkgo.io 4", 0, 4);
	nr_display_str("hi linkgo.io 5 a long long string", 0, 5);
	nr_display_str("hi linkgo.io 6", 0, 6);
	nr_display_str("hi linkgo.io 7 a long long string", 0, 7);
	nr_display_str("hi linkgo.io 8", 0, 8);
	nr_display_str("hi linkgo.io 9", 0, 9);
	nr_display_str("hi linkgo.io 10", 0, 10);
	nr_display_str("hi linkgo.io 11", 0, 11);
	nr_display_str("hi linkgo.io 12", 0, 12);
	nr_display_str("hi linkgo.io 13", 0, 13);
	nr_display_str("hi linkgo.io 14", 0, 14);
	nr_display_str("hi linkgo.io 15", 0, 15);
	nr_display_str("hi linkgo.io 16", 0, 16);
	nr_display_str("hi linkgo.io 17", 0, 17);
	nr_display_str("hi linkgo.io 18", 0, 18);
	sleep(1);
	nr_display_str("clearing all...", 0, 0);
	nr_display_clear();
	sleep(1);
	nr_display_str("linkgo.io", 8, 7);
	nr_display_str("try to put it in center", 1, 8);
	sleep(3);
	nr_display_deinit();

neuron_exit:
	log_info("exited\n");
	return ret;
}

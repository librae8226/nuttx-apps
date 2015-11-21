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

	nr_display_str("hi linkgo.io", 0, 0);

	nr_display_deinit();

neuron_exit:
	log_info("exited\n");
	return ret;
}

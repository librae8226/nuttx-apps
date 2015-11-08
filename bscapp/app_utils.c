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

uint32_t millis(void)
{
	uint32_t ticktime, sec, remainder;
	ticktime = clock_systimer();
	sec = ticktime / CLOCKS_PER_SEC;
	remainder = (uint32_t)(ticktime % CLOCKS_PER_SEC);
	return sec*1000+remainder;
}

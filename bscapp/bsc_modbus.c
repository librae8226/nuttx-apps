#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>

#include <apps/modbus/mb.h>
#include <apps/modbus/mbport.h>

#include "app_utils.h"

#define BSC_MODBUS_PORT 502
#define BSC_MODBUS_BAUD B38400
#define BSC_MODBUS_PARITY MB_PAR_EVEN
#define BSC_MODBUS_REG_INPUT_START 0
#define BSC_MODBUS_REG_INPUT_NREGS 4
#define BSC_MODBUS_REG_HOLDING_START 0
#define BSC_MODBUS_REG_HOLDING_NREGS 256

enum modbus_threadstate_e
{
	STOPPED = 0,
	RUNNING,
	SHUTDOWN
};

struct modbus_state_s
{
	enum modbus_threadstate_e threadstate;
	uint16_t reginput[BSC_MODBUS_REG_INPUT_NREGS];
	uint16_t regholding[BSC_MODBUS_REG_HOLDING_NREGS];
	pthread_t threadid;
	pthread_mutex_t lock;
	volatile bool quit;
};

static inline int modbus_initialize(void);
static void *modbus_pollthread(void *pvarg);
static inline int modbus_create_pollthread(void);
static void modbus_showusage(FAR const char *progname, int exitcode);

static struct modbus_state_s g_modbus;
static const uint8_t g_slaveid[] = { 0xaa, 0xbb, 0xcc };

/****************************************************************************
 * Name: modbus_initialize
 *
 * Description:
 *   Called from the ModBus polling thread in order to initialized the
 *   FreeModBus interface.
 *
 ****************************************************************************/

static inline int modbus_initialize(void)
{
	eMBErrorCode mberr;
	int status;

	/* Verify that we are in the stopped state */

	if (g_modbus.threadstate != STOPPED)
	{
		bsc_err("modbus_main: "
				"ERROR: Bad state: %d\n", g_modbus.threadstate);
		return EINVAL;
	}

	/* Initialize the ModBus demo data structures */

	status = pthread_mutex_init(&g_modbus.lock, NULL);
	if (status != 0)
	{
		bsc_err("modbus_main: "
				"ERROR: pthread_mutex_init failed: %d\n",  status);
		return status;
	}

	status = ENODEV;

	/* Initialize the FreeModBus library.
	 *
	 * MB_RTU                        = RTU mode
	 * 0x0a                          = Slave address
	 * BSC_MODBUS_PORT   = port, default=0 (i.e., /dev/ttyS0)
	 * BSC_MODBUS_BAUD   = baud, default=B38400
	 * BSC_MODBUS_PARITY = parity, default=MB_PAR_EVEN
	 */

	mberr = eMBTCPInit(BSC_MODBUS_PORT);
	if (mberr != MB_ENOERR)
	{
		bsc_err("modbus_main: "
				"ERROR: eMBTCPInit failed: %d\n", mberr);
		goto errout_with_mutex;
	}

	/* Set the slave ID
	 *
	 * 0x34        = Slave ID
	 * true        = Is running (run indicator status = 0xff)
	 * g_slaveid   = Additional values to be returned with the slave ID
	 * 3           = Length of additional values (in bytes)
	 */

	mberr = eMBSetSlaveID(0x34, true, g_slaveid, 3);
	if (mberr != MB_ENOERR)
	{
		bsc_err("modbus_main: "
				"ERROR: eMBSetSlaveID failed: %d\n", mberr);
		goto errout_with_modbus;
	}

	/* Enable FreeModBus */

	mberr = eMBEnable();
	if (mberr != MB_ENOERR)
	{
		bsc_err("modbus_main: "
				"ERROR: eMBEnable failed: %d\n", mberr);
		goto errout_with_modbus;
	}

	/* Successfully initialized */

	g_modbus.threadstate = RUNNING;
	bsc_info("modbus running\n");
	return OK;

errout_with_modbus:
	/* Release hardware resources. */

	(void)eMBClose();

errout_with_mutex:

	/* Free/uninitialize data structures */

	(void)pthread_mutex_destroy(&g_modbus.lock);

	g_modbus.threadstate = STOPPED;
	return status;
}

/****************************************************************************
 * Name: modbus_pollthread
 *
 * Description:
 *   This is the ModBus polling thread.
 *
 ****************************************************************************/

static void *modbus_pollthread(void *pvarg)
{
	eMBErrorCode mberr;
	int ret;

	/* Initialize the modbus */

	ret = modbus_initialize();
	if (ret != OK)
	{
		bsc_err("modbus_main: "
				"ERROR: modbus_initialize failed: %d\n", ret);
		return NULL;
	}

	srand(time(NULL));

	/* Then loop until we are commanded to shutdown */

	do
	{
		/* Poll */

		mberr = eMBPoll();
		if (mberr != MB_ENOERR)
		{
			break;
		}

		/* Generate some random input */

		g_modbus.reginput[0] = (uint16_t)rand();
	}
	while (g_modbus.threadstate != SHUTDOWN);

	/* Disable */

	(void)eMBDisable();

	/* Release hardware resources. */

	(void)eMBClose();

	/* Free/uninitialize data structures */

	(void)pthread_mutex_destroy(&g_modbus.lock);
	g_modbus.threadstate = STOPPED;
	return NULL;
}

/****************************************************************************
 * Name: modbus_create_pollthread
 *
 * Description:
 *   Start the ModBus polling thread
 *
 ****************************************************************************/

static inline int modbus_create_pollthread(void)
{
	int ret;

	if (g_modbus.threadstate == STOPPED)
	{
		ret = pthread_create(&g_modbus.threadid, NULL, modbus_pollthread, NULL);
		pthread_setname_np(&g_modbus.threadid, "modbus_pollthread");
	}
	else
	{
		ret = EINVAL;
	}

	return ret;
}

/****************************************************************************
 * Name: modbus_showusage
 *
 * Description:
 *   Show usage of the demo program and exit
 *
 ****************************************************************************/

static void modbus_showusage(FAR const char *progname, int exitcode)
{
	printf("USAGE: %s [-d|e|s|q|h]\n\n", progname);
	printf("Where:\n");
	printf("  -d : Disable protocol stack\n");
	printf("  -e : Enable the protocol stack\n");
	printf("  -s : Show current status\n");
	printf("  -q : Quit application\n");
	printf("  -h : Show this information\n");
	printf("\n");
	exit(exitcode);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: modbus_main
 *
 * Description:
 *   This is the main entry point to the demo program
 *
 ****************************************************************************/

int bsc_modbus_main(int argc, char *argv[])
{
	int option;
	int ret;

	/* Handle command line arguments */

	g_modbus.quit = false;

	while ((option = getopt(argc, argv, "desqh")) != ERROR)
	{
		switch (option)
		{
			case 'd': /* Disable protocol stack */
				(void)pthread_mutex_lock(&g_modbus.lock);
				g_modbus.threadstate = SHUTDOWN;
				(void)pthread_mutex_unlock(&g_modbus.lock);
				break;

			case 'e': /* Enable the protocol stack */
				{
					ret = modbus_create_pollthread();
					if (ret != OK)
					{
						bsc_err("modbus_main: "
								"ERROR: modbus_create_pollthread failed: %d\n", ret);
						exit(EXIT_FAILURE);
					}
				}
				break;

			case 's': /* Show current status */
				switch (g_modbus.threadstate)
				{
					case RUNNING:
						bsc_info("modbus_main: Protocol stack is running\n");
						break;

					case STOPPED:
						bsc_info("modbus_main: Protocol stack is stopped\n");
						break;

					case SHUTDOWN:
						bsc_info("modbus_main: Protocol stack is shutting down\n");
						break;

					default:
						bsc_err("modbus_main: "
								"ERROR: Invalid thread state: %d\n",
								g_modbus.threadstate);
						break;
				}
				break;

			case 'q': /* Quit application */
				g_modbus.quit = true;
				pthread_kill(g_modbus.threadid, 9);
				break;

			case 'h': /* Show help info */
				modbus_showusage(argv[0], EXIT_SUCCESS);
				break;

			default:
				bsc_err("modbus_main: "
						"ERROR: Unrecognized option: '%c'\n", option);
				modbus_showusage(argv[0], EXIT_FAILURE);
				break;
		}
	}

	return EXIT_SUCCESS;
}

/****************************************************************************
 * Name: eMBRegInputCB
 *
 * Description:
 *   Required FreeModBus callback function
 *
 ****************************************************************************/

eMBErrorCode eMBRegInputCB(uint8_t *buffer, uint16_t address, uint16_t nregs)
{
	eMBErrorCode mberr = MB_ENOERR;
	int          index;

	bsc_info("in\n");
	if ((address >= BSC_MODBUS_REG_INPUT_START) &&
			(address + nregs <=
			 BSC_MODBUS_REG_INPUT_START +
			 BSC_MODBUS_REG_INPUT_NREGS))
	{
		index = (int)(address - BSC_MODBUS_REG_INPUT_START);
		while (nregs > 0)
		{
			*buffer++ = (uint8_t)(g_modbus.reginput[index] >> 8);
			*buffer++ = (uint8_t)(g_modbus.reginput[index] & 0xff);
			index++;
			nregs--;
		}
	}
	else
	{
		mberr = MB_ENOREG;
	}

	bsc_info("out, mberr: %d\n", mberr);
	return mberr;
}

/****************************************************************************
 * Name: eMBRegHoldingCB
 *
 * Description:
 *   Required FreeModBus callback function
 *
 ****************************************************************************/

eMBErrorCode eMBRegHoldingCB(uint8_t *buffer, uint16_t address, uint16_t nregs, eMBRegisterMode mode)
{
	eMBErrorCode    mberr = MB_ENOERR;
	int             index;

	//bsc_info("in, addr: %d, nregs: %d, mode: %d\n", address, nregs, mode);
	if ((address >= BSC_MODBUS_REG_HOLDING_START) &&
			(address + nregs <=
			 BSC_MODBUS_REG_HOLDING_START +
			 BSC_MODBUS_REG_HOLDING_NREGS))
	{
		index = (int)(address - BSC_MODBUS_REG_HOLDING_START);
		switch (mode)
		{
			/* Pass current register values to the protocol stack. */
			case MB_REG_READ:
				while (nregs > 0)
				{
					*buffer++ = (uint8_t)(g_modbus.regholding[index] >> 8);
					*buffer++ = (uint8_t)(g_modbus.regholding[index] & 0xff);
					index++;
					nregs--;
				}
				break;

				/* Update current register values with new values from the
				 * protocol stack.
				 */

			case MB_REG_WRITE:
				while (nregs > 0)
				{
					g_modbus.regholding[index] = *buffer++ << 8;
					g_modbus.regholding[index] |= *buffer++;
					index++;
					nregs--;
				}
				break;
		}
	}
	else
	{
		mberr = MB_ENOREG;
		bsc_info("illegal register address.\n");
	}

	return mberr;
}

/****************************************************************************
 * Name: eMBRegCoilsCB
 *
 * Description:
 *   Required FreeModBus callback function
 *
 ****************************************************************************/

eMBErrorCode eMBRegCoilsCB(uint8_t *buffer, uint16_t address, uint16_t ncoils, eMBRegisterMode mode)
{
	return MB_ENOREG;
}

/****************************************************************************
 * Name: eMBRegDiscreteCB
 *
 * Description:
 *   Required FreeModBus callback function
 *
 ****************************************************************************/

eMBErrorCode eMBRegDiscreteCB(uint8_t *buffer, uint16_t address, uint16_t ndiscrete)
{
	return MB_ENOREG;
}

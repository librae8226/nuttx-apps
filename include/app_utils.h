#ifndef __APP_UTILS_H__
#define __APP_UTILS_H__

#define APP_UTILS_DEBUG

#ifdef APP_UTILS_DEBUG
#define log_dbg(format, ...) \
	syslog(LOG_DEBUG, "D/"EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define log_printf(format, ...) \
	printf(format, ##__VA_ARGS__)
#else /* !APP_UTILS_DEBUG */
#define log_dbg(format, ...)
#define log_printf(format, ...)
#endif /* APP_UTILS_DEBUG */

#define log_info(format, ...) \
	syslog(LOG_INFO, "I/"EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define log_warn(format, ...) \
	syslog(LOG_WARNING, "W/"EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#define log_err(format, ...) \
	syslog(LOG_ERR, "E/"EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)

#endif /* __APP_UTILS_H__ */

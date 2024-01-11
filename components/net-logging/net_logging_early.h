#ifndef NET_LOGGING_EARLY_H_
#define NET_LOGGING_EARLY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>
#include "net_logging_log.h"

// The size, in bytes, required to hold each item in the message (basically the max len of a message thats going to be sent)
#define NET_LOGGING_xItemSize 256u

// The total number of bytes (not messages) the message buffer will be able to hold at any one time for the sender thread, before it is able to run
#define NET_LOGGING_xBufferSizeBytes (8u * NET_LOGGING_xItemSize)

// The size of the early log buffer
#define NET_LOGGING_EARLY_LOG_SIZE (1<<12)

int net_logging_retreive_early_log(void* dest, int size);

//

int net_logging_early_vprintf(const char *fmt, va_list l);
int net_logging_early_printf(const char *fmt, ...);
void net_logging_early_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_LOGGING_EARLY_H_ */

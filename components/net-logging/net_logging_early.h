#ifndef NET_LOGGING_EARLY_H_
#define NET_LOGGING_EARLY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>

// The size, in bytes, required to hold each item in the message (basically the max len of a message thats going to be sent)
#define xItemSize 256

// The total number of bytes (not messages) the message buffer will be able to hold at any one time for the sender thread, before it is able to run
#define xBufferSizeBytes (8 * xItemSize)

int net_logging_early_vprintf(const char *fmt, va_list l);
int net_logging_early_printf(const char *fmt, ...);
void net_logging_early_init(unsigned int id, bool enableStdout, bool initlate);

int net_logging_retreive_log(void* dest, int size);

#ifdef __cplusplus
}
#endif

#endif /* NET_LOGGING_EARLY_H_ */

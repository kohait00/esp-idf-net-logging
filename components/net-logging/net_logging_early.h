#ifndef NET_LOGGING_EARLY_H_
#define NET_LOGGING_EARLY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>

// The total number of bytes (not messages) the message buffer will be able to hold at any one time.
#define xBufferSizeBytes 1024
// The size, in bytes, required to hold each item in the message,
#define xItemSize 256

int retreive_early_log(void* dest, int size);
int logging_early_vprintf(const char *fmt, va_list args);
void net_logging_early_init(bool enableStdout);

#ifdef __cplusplus
}
#endif

#endif /* NET_LOGGING_EARLY_H_ */

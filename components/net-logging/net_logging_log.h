/*
 * net_logging_log.h
 *
 *  Created on: Jan 10, 2024
 *      Author: master
 */

#ifndef NET_LOGGING_LOG_H_
#define NET_LOGGING_LOG_H_

#include <esp_log.h>

#if defined(__cplusplus) && (__cplusplus >  201703L)
#if CONFIG_LOG_TIMESTAMP_SOURCE_RTOS
#define NET_LOG_LEVEL(level, tag, format, log_tag_letter, ...) do {                     \
        net_log_write(level,      tag, LOG_FORMAT(log_tag_letter, format), esp_log_timestamp(), tag __VA_OPT__(,) __VA_ARGS__); \
    } while(0)
#elif CONFIG_LOG_TIMESTAMP_SOURCE_SYSTEM
#define NET_LOG_LEVEL(level, tag, format, log_tag_letter, ...) do {                     \
        net_log_write(level,      tag, LOG_SYSTEM_TIME_FORMAT(log_tag_letter, format), esp_log_system_timestamp(), tag __VA_OPT__(,) __VA_ARGS__); \
    } while(0)
#endif //CONFIG_LOG_TIMESTAMP_SOURCE_xxx
#else // !(defined(__cplusplus) && (__cplusplus >  201703L))
#if CONFIG_LOG_TIMESTAMP_SOURCE_RTOS
#define NET_LOG_LEVEL(level, tag, format, log_tag_letter, ...) do {                     \
        net_log_write(level,      tag, LOG_FORMAT(log_tag_letter, format), esp_log_timestamp(), tag, ##__VA_ARGS__); \
    } while(0)
#elif CONFIG_LOG_TIMESTAMP_SOURCE_SYSTEM
#define NET_LOG_LEVEL(level, tag, format, log_tag_letter, ...) do {                     \
        net_log_write(level,      tag, LOG_SYSTEM_TIME_FORMAT(log_tag_letter, format), esp_log_system_timestamp(), tag, ##__VA_ARGS__); \
    } while(0)
#endif //CONFIG_LOG_TIMESTAMP_SOURCE_xxx
#endif // !(defined(__cplusplus) && (__cplusplus >  201703L))

#if defined(__cplusplus) && (__cplusplus >  201703L)
#define NET_LOGE( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_ERROR,   tag, format, E __VA_OPT__(,) __VA_ARGS__)
#define NET_LOGW( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_WARN,    tag, format, W __VA_OPT__(,) __VA_ARGS__)
#define NET_LOGI( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_INFO,    tag, format, I __VA_OPT__(,) __VA_ARGS__)
#define NET_LOGD( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_DEBUG,   tag, format, D __VA_OPT__(,) __VA_ARGS__)
#define NET_LOGV( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_VERBOSE, tag, format, V __VA_OPT__(,) __VA_ARGS__)
#else // !(defined(__cplusplus) && (__cplusplus >  201703L))
#define NET_LOGE( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_ERROR,   tag, format, E, ##__VA_ARGS__)
#define NET_LOGW( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_WARN,    tag, format, W, ##__VA_ARGS__)
#define NET_LOGI( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_INFO,    tag, format, I, ##__VA_ARGS__)
#define NET_LOGD( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_DEBUG,   tag, format, D, ##__VA_ARGS__)
#define NET_LOGV( tag, format, ... ) NET_LOG_LEVEL(ESP_LOG_VERBOSE, tag, format, V, ##__VA_ARGS__)
#endif // !(defined(__cplusplus) && (__cplusplus >  201703L))

extern void net_log_write(esp_log_level_t level,
                   const char *tag,
                   const char *format, ...);

#endif /* NET_LOGGING_LOG_H_ */

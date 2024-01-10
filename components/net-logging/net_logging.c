#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"
#include <freertos/timers.h>


#include "esp_system.h"
#include "esp_log.h"

#include "net_logging.h"

#define EARLY_LOG_SIZE (1<<12)

#	define min(x, y) ((x) < (y) ? (x) : (y))

////////

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

////////

MessageBufferHandle_t xMessageBufferTrans_udp = NULL;
MessageBufferHandle_t xMessageBufferTrans_tcp = NULL;
MessageBufferHandle_t xMessageBufferTrans_mqqt = NULL;
MessageBufferHandle_t xMessageBufferTrans_http = NULL;

bool bWriteToStdout = true; //default value for early log
bool bLoggersActive = false;

unsigned int xId = 0;

unsigned int xEarlyLogIdx = 0;
char xEarlyLog[EARLY_LOG_SIZE] = {0};
unsigned int xEarlyLogIdxSent_udp = 0;
unsigned int xEarlyLogIdxSent_tcp = 0;
unsigned int xEarlyLogIdxSent_mqqt = 0;
unsigned int xEarlyLogIdxSent_http = 0;

early_vprintf_like_t xPrevious_early_vprintf_like = NULL;
vprintf_like_t xPrevious_vprintf_like = NULL;

int net_logging_retreive_log(void* dest, int size)
{
	int len = min(size, xEarlyLogIdx);
	memcpy(dest, xEarlyLog, len);
	printf("LOG RETREIVE %d, %d", xEarlyLogIdx, len);
	return xEarlyLogIdx;
}

static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

void some_function(void)
{
    taskENTER_CRITICAL(&my_spinlock);
    // We are now in a critical section
    taskEXIT_CRITICAL(&my_spinlock);
}

static TimerHandle_t xTimer = NULL;
//static char xbuffer[xItemSize]; //for timer to be used as destination for prints, since its stack is small

static int net_logging_vprintf_internal( bool writeToStdout, bool writeEarlyLog, bool writeNetLog, char* buffer, unsigned int buffer_size, const char *fmt, va_list l );
static int net_logging_printf_internal( bool writeToStdout, bool writeEarlyLog, bool writeNetLog, char* buffer, unsigned int buffer_size, const char *fmt, ...);
static unsigned int net_logging_out_raw(const char* buffer, unsigned int buffer_len);
static unsigned int net_logging_out(const char* buffer, unsigned int buffer_len);

static unsigned int _net_logging_out_raw(const char* buffer, unsigned int buffer_len)
{
    return buffer_len;
}

#define __net_logging_out_raw (a, b) (b)

static void net_logging_DelayedLOG(TimerHandle_t _xTimer)
{
//	char xbuffer[xItemSize];

	taskENTER_CRITICAL(&my_spinlock);
	unsigned int left2send = xEarlyLogIdx - xEarlyLogIdxSent_udp;
	taskEXIT_CRITICAL(&my_spinlock);

	size_t sent = 0;

	if(net_logging_is_enabled())
	{
		if(xEarlyLogIdxSent_udp <= 0)
		{
			NET_LOGW("NETLOG", "Transfer of %d starting", left2send);
//			net_logging_printf_internal(false, false, true, xbuffer, sizeof(xbuffer), "Transfer of %d starting\n", xEarlyLogIdx);
		}
		sent = net_logging_out_raw(&xEarlyLog[xEarlyLogIdxSent_udp], min(left2send, xItemSize/2));
	}

//	printf("delayed LOG buffer=%d %d %d\n", xEarlyLogIdx, xEarlyLogIdxSent_udp, sent);

//	taskENTER_CRITICAL(&my_spinlock);
	xEarlyLogIdxSent_udp = xEarlyLogIdxSent_udp + sent;
//	taskEXIT_CRITICAL(&my_spinlock);

	left2send -= sent;

	if(left2send > 0)
	    xTimerStart(_xTimer, 0);
	else
	{
//		printf("LOG timer done\n");
		NET_LOGW("NETLOG", "Transfer of %d done", xEarlyLogIdxSent_udp);
//		net_logging_printf_internal(false, false, true, xbuffer, sizeof(xbuffer), "Transfer of %d done\n", xEarlyLogIdxSent_udp);

		xTimerDelete(_xTimer, 0);
		xTimer = NULL;
	}
}

static void StartDeleyedLOG(void)
{
    xTimer = xTimerCreate ( "DelayedLOG",
                       100 / portTICK_PERIOD_MS, //100 ms
                       pdFALSE, //no autoreload
                       NULL,
                       &net_logging_DelayedLOG );
    xTimerStart(xTimer, 0);
}

static unsigned int net_logging_out_raw(const char* buffer, unsigned int buffer_len)
{
	if(buffer_len <= 0)
		return 0;

	// Send MessageBuffer
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	size_t sent = 0;

	if(xMessageBufferTrans_udp != NULL) {
//		printf("raw log %d", buffer_len);
		sent = xMessageBufferSendFromISR(xMessageBufferTrans_udp, buffer, buffer_len, &xHigherPriorityTaskWoken);
	}
	if(xMessageBufferTrans_tcp != NULL) {
		sent = xMessageBufferSendFromISR(xMessageBufferTrans_tcp, buffer, buffer_len, &xHigherPriorityTaskWoken);
	}
	if(xMessageBufferTrans_mqqt != NULL) {
		sent = xMessageBufferSendFromISR(xMessageBufferTrans_mqqt, buffer, buffer_len, &xHigherPriorityTaskWoken);
	}
	if(xMessageBufferTrans_http != NULL) {
		sent = xMessageBufferSendFromISR(xMessageBufferTrans_http, buffer, buffer_len, &xHigherPriorityTaskWoken);
	}

//	assert(sent == buffer_len);
	return buffer_len;
}

//todo take care of first sending the already existing buffer
//this is done by examining the sent position and the current early log position
//note that everytime there is more left in the buffer that hasnt been sent yet, we need to trigger a log entry from another thread to wake this up
//so if xEarlyLogIdxSent_udp == xEarlyLogIdx just sent right away
//if there is sth left to sendfrom buffer and it is not
static unsigned int net_logging_out(const char* buffer, unsigned int buffer_len)
{
	if(buffer_len <= 0)
		return 0;

	if(!net_logging_is_enabled())
		return 0;

//	size_t sent = 0;

	if(xMessageBufferTrans_udp != NULL) {
//		unsigned int left2send = xEarlyLogIdx - xEarlyLogIdxSent_udp;
//		if(left2send <= 0)
		{
			net_logging_out_raw(buffer, buffer_len);
		}
//		xEarlyLogIdxSent_udp += sent;
	}
	if(xMessageBufferTrans_tcp != NULL) {
//		unsigned int left2send = 0;//xEarlyLogIdx - xEarlyLogIdxSent_tcp;
//		if(left2send <= 0)
		{
			net_logging_out_raw(buffer, buffer_len);
		}
//		xEarlyLogIdxSent_tcp += sent;
	}
	if(xMessageBufferTrans_mqqt != NULL) {
//		unsigned int left2send = 0;//xEarlyLogIdx - xEarlyLogIdxSent_mqqt;
//		if(left2send <= 0)
		{
			net_logging_out_raw(buffer, buffer_len);
		}
//		xEarlyLogIdxSent_mqqt += sent;
	}
	if(xMessageBufferTrans_http != NULL) {
//		unsigned int left2send = 0;//xEarlyLogIdx - xEarlyLogIdxSent_http;
//		if(left2send <= 0)
		{
			net_logging_out_raw(buffer, buffer_len);
		}
//		xEarlyLogIdxSent_http += sent;
	}

	return buffer_len;
}

static void _memcpy(void* dest, const void* src, size_t size)
{
	char* d = (const char*)dest;
	const char* s = (const char*)src;

	for(unsigned int i = 0; i<size; i++)
	{
		d = s;
		d++;
		s++;
	}
}

static int net_logging_vprintf_internal( bool writeToStdout, bool writeEarlyLog, bool writeNetLog, char* buffer, unsigned int buffer_size, const char *fmt, va_list l )
{
//	// Convert according to format
//	char buffer[xItemSize];
	//printf("logging_vprintf buffer_i=%d\n",buffer_i);
	//printf("logging_vprintf buffer=[%.*s]\n", buffer_i, buffer);
	unsigned int buffer_i = 0;

	snprintf(&buffer[buffer_i], buffer_size - buffer_i, "[%06X]", (int)xId);
	buffer_i += strlen(&buffer[buffer_i]);

	vsnprintf(&buffer[buffer_i], buffer_size - buffer_i, fmt, l);
	buffer_i += strlen(&buffer[buffer_i]);
//	buffer_i = 50;

	size_t sent = 0;

//	printf("writeNetLog = %d", writeNetLog);

	if( (writeEarlyLog) )// && ((sizeof(xEarlyLog) - xEarlyLogIdx) > 450))
	{
		taskENTER_CRITICAL(&my_spinlock);
		unsigned int xEarlyLogIdx__ = xEarlyLogIdx;
		taskEXIT_CRITICAL(&my_spinlock);

		//write to the early buffer in any case
		unsigned int len = min(buffer_i, (sizeof(xEarlyLog) - xEarlyLogIdx__));
		if(len > 0)
		{
//			printf("%d %d", xEarlyLogIdx__, len);
			memcpy(&xEarlyLog[xEarlyLogIdx__], buffer, len); //is this the problem todo?
			xEarlyLogIdx__ += len;
		}

		taskENTER_CRITICAL(&my_spinlock);
		xEarlyLogIdx = xEarlyLogIdx__;
		taskEXIT_CRITICAL(&my_spinlock);

	}

	if(writeNetLog)
	{
		sent = net_logging_out(buffer, buffer_i);
	}

	// Write to stdout
	if (xPrevious_vprintf_like != NULL && (writeToStdout)) { //if no logger active ignore the writeToStdout and print anyway
		return xPrevious_vprintf_like( fmt, l );
	} else {
		return sent;
	}
}

static int net_logging_printf_internal( bool writeToStdout, bool writeEarlyLog, bool writeNetLog, char* buffer, unsigned int buffer_size, const char *fmt, ...)
{
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = net_logging_vprintf_internal(writeToStdout, writeEarlyLog, writeNetLog, buffer, buffer_size, fmt, ap);
    va_end(ap);

    return ret;
}

void net_log_write(esp_log_level_t level,
                   const char *tag,
                   const char *format, ...)
{
	char buffer[xItemSize];
    va_list list;
    va_start(list, format);
    net_logging_vprintf_internal(false, false, true, buffer, sizeof(buffer), format, list);
    va_end(list);
}

/////////////

//no need to take care of multithreading
int net_logging_early_vprintf(const char *fmt, va_list l)
{
	unsigned int left = sizeof(xEarlyLog) - xEarlyLogIdx;

	if(left < 8)
		return 0; //dont bother if hardly room left for id

	unsigned int ret = 0;
	if(true)
	{
	ret = snprintf(&xEarlyLog[xEarlyLogIdx], sizeof(xEarlyLog) - xEarlyLogIdx, "[%06X]", (int)xId);
	xEarlyLogIdx += ret;
	if(xEarlyLogIdx > sizeof(xEarlyLog))
		xEarlyLogIdx = sizeof(xEarlyLog);

	ret = vsnprintf(&xEarlyLog[xEarlyLogIdx], sizeof(xEarlyLog) - xEarlyLogIdx, fmt, l);
	xEarlyLogIdx += ret;
	if(xEarlyLogIdx > sizeof(xEarlyLog))
		xEarlyLogIdx = sizeof(xEarlyLog);
	}
    return ret;
}


int net_logging_early_printf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = net_logging_early_vprintf(fmt, ap);
    va_end(ap);

    return ret;
}

int net_logging_vprintf( const char *fmt, va_list l )
{
	char buffer[xItemSize];
//	memset(buffer, 'y', sizeof(buffer));
//	for(unsigned int i = 0; i < sizeof(buffer); i++)
//	{
//		buffer[i] = 'A' + (i % 25);
//	}

	return net_logging_vprintf_internal(bWriteToStdout, true, (xTimer == NULL), buffer, sizeof(buffer), fmt, l);
}

int net_logging_printf(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = net_logging_vprintf(fmt, ap);
    va_end(ap);

    return ret;
}

void net_logging_early_init(unsigned int id, bool enableStdout, bool initlate)
{
	if(xPrevious_early_vprintf_like == NULL) //prevent loops
		xPrevious_early_vprintf_like = esp_log_set_early_vprintf(net_logging_early_printf);

	net_logging_set_id(id);
	bWriteToStdout = enableStdout;

	if(initlate)
		net_logging_init(id, enableStdout);
}

void net_logging_init(unsigned int id, bool enableStdout)
{
	if(xPrevious_vprintf_like == NULL) //prevent loops
		xPrevious_vprintf_like = esp_log_set_vprintf(net_logging_vprintf);

//	memset(xEarlyLog, 'x', sizeof(xEarlyLog));
//	for(unsigned int i = 0; i < sizeof(xEarlyLog); i++)
//	{
//		xEarlyLog[i] = 'a' + (i % 25);
//	}

	net_logging_set_id(id);
	bWriteToStdout = enableStdout;
}

bool net_logging_is_enabled(void)
{
	return  bLoggersActive && (
			   (xMessageBufferTrans_udp != NULL)
			|| (xMessageBufferTrans_tcp != NULL)
			|| (xMessageBufferTrans_mqqt != NULL)
			|| (xMessageBufferTrans_http != NULL)
			);
}

void net_logging_enable_log(void)
{
	bLoggersActive = true;
}

void net_logging_disable_log(void)
{
	bLoggersActive = false;
}

void net_logging_set_id(unsigned int id)
{
	xId = id;
}

void udp_client(void *pvParameters);

esp_err_t udp_logging_init(char *ipaddr, unsigned long port) {
	printf("start udp logging: ipaddr=[%s] port=%ld\n", ipaddr, port);

	// Create MessageBuffer
	xMessageBufferTrans_udp = xMessageBufferCreate(xBufferSizeBytes);
	configASSERT( xMessageBufferTrans_udp );

	// Start UDP task
	PARAMETER_t param;
	param.port = port;
	strcpy(param.ipv4, ipaddr);
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(udp_client, "UDP", 1024*6, (void *)&param, 2, NULL);

	// Wait for ready to receive notify
	ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
//	net_logging_enable_log();
	StartDeleyedLOG();
	return ESP_OK;
}

void tcp_client(void *pvParameters);

esp_err_t tcp_logging_init(char *ipaddr, unsigned long port) {
	printf("start tcp logging: ipaddr=[%s] port=%ld\n", ipaddr, port);

	// Create MessageBuffer
	xMessageBufferTrans_tcp = xMessageBufferCreate(xBufferSizeBytes);
	configASSERT( xMessageBufferTrans_tcp );

	// Start TCP task
	PARAMETER_t param;
	param.port = port;
	strcpy(param.ipv4, ipaddr);
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(tcp_client, "TCP", 1024*6, (void *)&param, 2, NULL);

	// Wait for ready to receive notify
	ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
//	net_logging_enable_log();
	return ESP_OK;
}

void mqtt_pub(void *pvParameters);

esp_err_t mqtt_logging_init(char *url, char *topic) {
	printf("start mqtt logging: url=[%s] topic=[%s]\n", url, topic);

	// Create MessageBuffer
	xMessageBufferTrans_mqqt = xMessageBufferCreate(xBufferSizeBytes);
	configASSERT( xMessageBufferTrans_mqqt );

	// Start MQTT task
	PARAMETER_t param;
	strcpy(param.url, url);
	strcpy(param.topic, topic);
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(mqtt_pub, "MQTT", 1024*6, (void *)&param, 2, NULL);

	// Wait for ready to receive notify
	ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
//	net_logging_enable_log();
	return ESP_OK;
}

void http_client(void *pvParameters);

esp_err_t http_logging_init(char *url) {
	printf("start http logging: url=[%s]\n", url);

	// Create MessageBuffer
	xMessageBufferTrans_http = xMessageBufferCreate(xBufferSizeBytes);
	configASSERT( xMessageBufferTrans_http );

	// Start HTTP task
	PARAMETER_t param;
	strcpy(param.url, url);
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(http_client, "HTTP", 1024*4, (void *)&param, 2, NULL);

	// Wait for ready to receive notify
	ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
//	net_logging_enable_log();
	return ESP_OK;
}

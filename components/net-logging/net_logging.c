#include <string.h>
#include <assert.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/message_buffer.h>
#include <freertos/timers.h>

#include "esp_system.h"

#include "net_logging.h"

#	define min(x, y) ((x) < (y) ? (x) : (y))

static unsigned int net_logging_out(const char* buffer, unsigned int buffer_len, bool force);

////////

MessageBufferHandle_t xMessageBufferTrans[NET_LOGGING_DEST_COUNT] = {0};

static bool bWriteToStdout = true; //default value for early log
static bool bLoggersActive = false;

static unsigned int xId = 0;

static unsigned int xEarlyLogIdx = 0;
static char xEarlyLog[NET_LOGGING_EARLY_LOG_SIZE] = {0};

static unsigned int xaEarlyLogIdxSent[NET_LOGGING_DEST_COUNT] = {0};
static TimerHandle_t xaTimer[NET_LOGGING_DEST_COUNT] = {0};

static early_vprintf_like_t xPrevious_early_vprintf_like = NULL;
static vprintf_like_t xPrevious_vprintf_like = NULL;

static portMUX_TYPE xSpinlock = portMUX_INITIALIZER_UNLOCKED;

//

int net_logging_retreive_early_log(void* dest, int size)
{
	int len = min(size, xEarlyLogIdx);
	memcpy(dest, xEarlyLog, len);
	return len;
}

static void net_logging_DelayedLOG(TimerHandle_t _xTimer)
{
	unsigned short dest = 0;

	for(dest = 0; dest < NET_LOGGING_DEST_COUNT; dest++)
	{
		if(xaTimer[dest] == _xTimer)
			break;
	}

	assert(dest < NET_LOGGING_DEST_COUNT);

	taskENTER_CRITICAL(&xSpinlock);
	unsigned int left2send = xEarlyLogIdx - xaEarlyLogIdxSent[dest];
	taskEXIT_CRITICAL(&xSpinlock);

	size_t sent = 0;

	if(net_logging_is_enabled())
	{
		if(xaEarlyLogIdxSent[dest] <= 0)
		{
			NET_LOGW("NETLOG", "Transfer of %d starting", left2send);
//			printf("Transfer of %d starting", left2send);
		}
		if(xMessageBufferTrans[dest] != NULL) {
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;
			sent = xMessageBufferSendFromISR(xMessageBufferTrans[dest], &xEarlyLog[ xaEarlyLogIdxSent[dest] ], min(left2send, NET_LOGGING_xItemSize/2), &xHigherPriorityTaskWoken);
		}
	}

//	taskENTER_CRITICAL(&xSpinlock);
	xaEarlyLogIdxSent[dest] = xaEarlyLogIdxSent[dest] + sent;
//	taskEXIT_CRITICAL(&xSpinlock);

	left2send -= sent;

	if(left2send > 0)
	    xTimerStart(_xTimer, 0);
	else
	{
		NET_LOGW("NETLOG", "Transfer of %d done", xaEarlyLogIdxSent[dest]);
//		printf("Transfer of %d done", xaEarlyLogIdxSent[dest]);

		xTimerDelete(_xTimer, 0);
		xaTimer[dest] = NULL;
	}
}

static void StartSendingDeleyedLOG(NET_LOGGING_DEST eDest)
{
    xaTimer[eDest] = xTimerCreate ( "DelayedLOG",
                       100 / portTICK_PERIOD_MS, //100 ms
                       pdFALSE, //no autoreload
                       NULL,
                       &net_logging_DelayedLOG );
    xTimerStart(xaTimer[eDest], 0);
}

static unsigned int net_logging_out(const char* buffer, unsigned int buffer_len, bool force)
{
	if(buffer_len <= 0)
		return 0;

	// Send MessageBuffer
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	size_t sent = 0;

	if((xMessageBufferTrans[NET_LOGGING_DEST_UDP] != NULL) && ((xaTimer[NET_LOGGING_DEST_UDP] == NULL) || force)) {
		sent = xMessageBufferSendFromISR(xMessageBufferTrans[NET_LOGGING_DEST_UDP], buffer, buffer_len, &xHigherPriorityTaskWoken);
	}
	if((xMessageBufferTrans[NET_LOGGING_DEST_TCP] != NULL) && ((xaTimer[NET_LOGGING_DEST_TCP] == NULL) || force)) {
		sent = xMessageBufferSendFromISR(xMessageBufferTrans[NET_LOGGING_DEST_TCP], buffer, buffer_len, &xHigherPriorityTaskWoken);
	}
	if((xMessageBufferTrans[NET_LOGGING_DEST_MQQT] != NULL) && ((xaTimer[NET_LOGGING_DEST_MQQT] == NULL) || force)) {
		sent = xMessageBufferSendFromISR(xMessageBufferTrans[NET_LOGGING_DEST_MQQT], buffer, buffer_len, &xHigherPriorityTaskWoken);
	}
	if((xMessageBufferTrans[NET_LOGGING_DEST_HTTP] != NULL) && ((xaTimer[NET_LOGGING_DEST_HTTP] == NULL) || force)) {
		sent = xMessageBufferSendFromISR(xMessageBufferTrans[NET_LOGGING_DEST_HTTP], buffer, buffer_len, &xHigherPriorityTaskWoken);
	}

//	assert(sent == buffer_len);
	return buffer_len;
}

static int net_logging_vprintf_buff( char* buffer, unsigned int buffer_size, const char *fmt, va_list l )
{
	// Convert according to format
	unsigned int buffer_i = 0;

	snprintf(&buffer[buffer_i], buffer_size - buffer_i, "[%06X]", (int)xId);
	buffer_i += strlen(&buffer[buffer_i]);

	vsnprintf(&buffer[buffer_i], buffer_size - buffer_i, fmt, l);
	buffer_i += strlen(&buffer[buffer_i]);

	return buffer_i;
}

/////////////

void net_log_write(esp_log_level_t level,
                   const char *tag,
                   const char *format, ...)
{
	char buffer[NET_LOGGING_xItemSize];
    va_list list;
    va_start(list, format);

	unsigned int buffer_i = net_logging_vprintf_buff(buffer, sizeof(buffer), format, list);
	//printf(">>%s", buffer);
	net_logging_out(buffer, buffer_i, true);

    va_end(list);
}

//no need to take care of multi threading
int net_logging_early_vprintf(const char *fmt, va_list l)
{
	unsigned int left = sizeof(xEarlyLog) - xEarlyLogIdx;

	if(left < 8)
		return 0; //dont bother if hardly room left for id

	unsigned int buffer_i = net_logging_vprintf_buff(&xEarlyLog[xEarlyLogIdx], left, fmt, l);

	xEarlyLogIdx += buffer_i;

	if(xEarlyLogIdx > sizeof(xEarlyLog))
		xEarlyLogIdx = sizeof(xEarlyLog);

	return buffer_i;
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
	char buffer[NET_LOGGING_xItemSize];

	// Convert according to format
	unsigned int buffer_i = net_logging_vprintf_buff(buffer, sizeof(buffer), fmt, l);

	size_t sent = 0;

	{
		taskENTER_CRITICAL(&xSpinlock);
		unsigned int xEarlyLogIdx__ = xEarlyLogIdx;
		taskEXIT_CRITICAL(&xSpinlock);

		//write to the early buffer in any case
		unsigned int len = min(buffer_i, (sizeof(xEarlyLog) - xEarlyLogIdx__));
		if(len > 0)
		{
			memcpy(&xEarlyLog[xEarlyLogIdx__], buffer, len); //is this the problem todo?
			xEarlyLogIdx__ += len;
		}

		taskENTER_CRITICAL(&xSpinlock);
		xEarlyLogIdx = xEarlyLogIdx__;
		taskEXIT_CRITICAL(&xSpinlock);
	}

	if(net_logging_is_enabled())
	{
		sent = net_logging_out(buffer, buffer_i, false);
	}

	if ((xPrevious_vprintf_like != NULL) && (bWriteToStdout)) { //if no logger active ignore the writeToStdout and print anyway
		return xPrevious_vprintf_like( fmt, l );
	} else {
		return sent;
	}
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

bool net_logging_is_enabled(void)
{
	return  bLoggersActive && (
			   (xMessageBufferTrans[NET_LOGGING_DEST_UDP] != NULL)
			|| (xMessageBufferTrans[NET_LOGGING_DEST_TCP] != NULL)
			|| (xMessageBufferTrans[NET_LOGGING_DEST_MQQT] != NULL)
			|| (xMessageBufferTrans[NET_LOGGING_DEST_HTTP] != NULL)
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

	net_logging_set_id(id);
	bWriteToStdout = enableStdout;
}

void udp_client(void *pvParameters);

esp_err_t udp_logging_init(char *ipaddr, unsigned long port) {
	printf("start udp logging: ipaddr=[%s] port=%ld\n", ipaddr, port);

	// Create MessageBuffer
	xMessageBufferTrans[NET_LOGGING_DEST_UDP] = xMessageBufferCreate(NET_LOGGING_xBufferSizeBytes);
	configASSERT( xMessageBufferTrans[NET_LOGGING_DEST_UDP] );

	// Start UDP task
	NET_LOGGING_PARAMETER_T param;
	param.port = port;
	strcpy(param.ipv4, ipaddr);
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(udp_client, "UDP", 1024*6, (void *)&param, 2, NULL);

	// Wait for ready to receive notify
	ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

	StartSendingDeleyedLOG(NET_LOGGING_DEST_UDP);
	return ESP_OK;
}

void tcp_client(void *pvParameters);

esp_err_t tcp_logging_init(char *ipaddr, unsigned long port) {
	printf("start tcp logging: ipaddr=[%s] port=%ld\n", ipaddr, port);

	// Create MessageBuffer
	xMessageBufferTrans[NET_LOGGING_DEST_TCP] = xMessageBufferCreate(NET_LOGGING_xBufferSizeBytes);
	configASSERT( xMessageBufferTrans[NET_LOGGING_DEST_TCP] );

	// Start TCP task
	NET_LOGGING_PARAMETER_T param;
	param.port = port;
	strcpy(param.ipv4, ipaddr);
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(tcp_client, "TCP", 1024*6, (void *)&param, 2, NULL);

	// Wait for ready to receive notify
	ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

	StartSendingDeleyedLOG(NET_LOGGING_DEST_TCP);
	return ESP_OK;
}

void mqtt_pub(void *pvParameters);

esp_err_t mqtt_logging_init(char *url, char *topic) {
	printf("start mqtt logging: url=[%s] topic=[%s]\n", url, topic);

	// Create MessageBuffer
	xMessageBufferTrans[NET_LOGGING_DEST_MQQT] = xMessageBufferCreate(NET_LOGGING_xBufferSizeBytes);
	configASSERT( xMessageBufferTrans[NET_LOGGING_DEST_MQQT] );

	// Start MQTT task
	NET_LOGGING_PARAMETER_T param;
	strcpy(param.url, url);
	strcpy(param.topic, topic);
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(mqtt_pub, "MQTT", 1024*6, (void *)&param, 2, NULL);

	// Wait for ready to receive notify
	ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

	StartSendingDeleyedLOG(NET_LOGGING_DEST_MQQT);
	return ESP_OK;
}

void http_client(void *pvParameters);

esp_err_t http_logging_init(char *url) {
	printf("start http logging: url=[%s]\n", url);

	// Create MessageBuffer
	xMessageBufferTrans[NET_LOGGING_DEST_HTTP] = xMessageBufferCreate(NET_LOGGING_xBufferSizeBytes);
	configASSERT( xMessageBufferTrans[NET_LOGGING_DEST_HTTP] );

	// Start HTTP task
	NET_LOGGING_PARAMETER_T param;
	strcpy(param.url, url);
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(http_client, "HTTP", 1024*4, (void *)&param, 2, NULL);

	// Wait for ready to receive notify
	ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

	StartSendingDeleyedLOG(NET_LOGGING_DEST_HTTP);
	return ESP_OK;
}

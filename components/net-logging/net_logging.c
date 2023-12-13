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

MessageBufferHandle_t xMessageBufferTrans_udp = NULL;
MessageBufferHandle_t xMessageBufferTrans_tcp = NULL;
MessageBufferHandle_t xMessageBufferTrans_mqqt = NULL;
MessageBufferHandle_t xMessageBufferTrans_http = NULL;

bool writeToStdout = true; //default value for early log
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

static TimerHandle_t xTimer = NULL;

static unsigned int net_logging_out_raw(const char* buffer, unsigned int buffer_len);
static void net_logging_DelayedLOG(TimerHandle_t _xTimer)
{
	unsigned int left2send = xEarlyLogIdx - xEarlyLogIdxSent_udp;
	size_t sent = net_logging_out_raw(&xEarlyLog[xEarlyLogIdxSent_udp], min(left2send, xItemSize));

	printf("delayed LOG buffer=%d %d %d\n", xEarlyLogIdx, xEarlyLogIdxSent_udp, sent);

	xEarlyLogIdxSent_udp += sent;
	left2send -= sent;

	if(left2send > 0)
	    xTimerStart(_xTimer, 0);
	else
	{
		printf("LOG timer done");
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
	if(!bLoggersActive || buffer_len <= 0)
		return 0;

	// Send MessageBuffer
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	size_t sent = 0;

	if(xMessageBufferTrans_udp != NULL) {
		printf("raw log %d", buffer_len);
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
	if(!bLoggersActive || buffer_len <= 0)
		return 0;

//	size_t sent = 0;

	if(xMessageBufferTrans_udp != NULL) {
//		unsigned int left2send = xEarlyLogIdx - xEarlyLogIdxSent_udp;
//		if(left2send <= 0)
		if(xTimer == NULL)
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

/////////////

int net_logging_early_vprintf(const char *fmt, va_list l)
{
    int ret = vsnprintf(&xEarlyLog[xEarlyLogIdx], sizeof(xEarlyLog) - xEarlyLogIdx, fmt, l);
    xEarlyLogIdx += ret;
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
	// Convert according to format
	char buffer[xItemSize];
	//printf("logging_vprintf buffer_len=%d\n",buffer_len);
	//printf("logging_vprintf buffer=[%.*s]\n", buffer_len, buffer);
	unsigned int buffer_len = 0;
	buffer_len += snprintf(&buffer[buffer_len], sizeof(buffer) - buffer_len, "[%X] ", (int)xId);
	buffer_len += vsnprintf(&buffer[buffer_len], sizeof(buffer) - buffer_len, fmt, l);
	size_t sent = 0;

	//write to the early buffer in any case
	unsigned int len = min(buffer_len, (sizeof(xEarlyLog) - xEarlyLogIdx));
	if(len > 0)
	{
		memcpy(&xEarlyLog[xEarlyLogIdx], buffer, len);
		xEarlyLogIdx += len;
	}

	sent = net_logging_out(buffer, buffer_len);

	// Write to stdout
	if (xPrevious_vprintf_like != NULL && (writeToStdout || (!bLoggersActive))) { //if no logger active ignore the writeToStdout and print anyway
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

void net_logging_early_init(unsigned int id, bool enableStdout, bool initlate)
{
	if(xPrevious_early_vprintf_like == NULL) //prevent loops
		xPrevious_early_vprintf_like = esp_log_set_early_vprintf(net_logging_early_printf);

	xId = id;
	writeToStdout = enableStdout;

	if(initlate)
		net_logging_init(id, enableStdout);
}

void net_logging_init(unsigned int id, bool enableStdout)
{
	if(xPrevious_vprintf_like == NULL) //prevent loops
		xPrevious_vprintf_like = esp_log_set_vprintf(net_logging_vprintf);

	xId = id;
	writeToStdout = enableStdout;
}

void net_logging_enable_log(void)
{
	// Set function used to output log entries.
	bLoggersActive = (xMessageBufferTrans_udp != NULL)
							|| (xMessageBufferTrans_tcp != NULL)
							|| (xMessageBufferTrans_mqqt != NULL)
							|| (xMessageBufferTrans_http != NULL)
							;
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
	net_logging_enable_log();

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
	net_logging_enable_log();
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
	net_logging_enable_log();
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
	net_logging_enable_log();
	return ESP_OK;
}

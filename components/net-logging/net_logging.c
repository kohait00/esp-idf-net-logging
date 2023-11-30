#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"

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

int xId = 0;
int xEarlyLogIdx = 0;
char xEarlyLog[EARLY_LOG_SIZE] = {0};
unsigned int xEarlyLogIdxSent = 0;
early_vprintf_like_t xPrevious_early_vprintf_like = NULL;
vprintf_like_t xPrevious_vprintf_like = NULL;

int net_logging_retreive_log(void* dest, int size)
{
	int len = min(size, xEarlyLogIdx);
	memcpy(dest, xEarlyLog, len);

	printf("LOG RETREIVE %d, %d", xEarlyLogIdx, len);

	return xEarlyLogIdx;
}

int net_logging_out(const char* buffer, unsigned int buffer_len)
{
	if(!bLoggersActive)
		return 0;

	if (buffer_len > 0) {
		// Send MessageBuffer
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;

		if(xMessageBufferTrans_udp != NULL) {
			size_t sent = xMessageBufferSendFromISR(xMessageBufferTrans_udp, buffer, buffer_len, &xHigherPriorityTaskWoken);
//			printf("logging_vprintf sent=%d\n",sent);
//			assert(sent == buffer_len);
		}
		if(xMessageBufferTrans_tcp != NULL) {
			size_t sent = xMessageBufferSendFromISR(xMessageBufferTrans_tcp, buffer, buffer_len, &xHigherPriorityTaskWoken);
			assert(sent == buffer_len);
		}
		if(xMessageBufferTrans_mqqt != NULL) {
			size_t sent = xMessageBufferSendFromISR(xMessageBufferTrans_mqqt, buffer, buffer_len, &xHigherPriorityTaskWoken);
			assert(sent == buffer_len);
		}
		if(xMessageBufferTrans_http != NULL) {
			size_t sent = xMessageBufferSendFromISR(xMessageBufferTrans_http, buffer, buffer_len, &xHigherPriorityTaskWoken);
			assert(sent == buffer_len);
		}
	}

	return buffer_len;
}

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
	//convenience, send history once possible
#if 0
/////////////
	//we might have remainders
	unsigned int left2send = 0;

	while ((left2send = xEarlyLogIdx - xEarlyLogIdxSent) > 0)
	{
		int sent = net_logging_out(&xEarlyLog[xEarlyLogIdxSent], min(left2send, xBufferSizeBytes));
		if(bLoggersActive) //prevent early pints to use delays, if RTOS not yet initialized
			vTaskDelay(200 / portTICK_PERIOD_MS); //every 200ms
		else if(sent == 0)
			break; //just send out logging out once

		xEarlyLogIdxSent += sent;
	}
////////////
#endif

	// Convert according to format
	char buffer[xItemSize];
	//printf("logging_vprintf buffer_len=%d\n",buffer_len);
	//printf("logging_vprintf buffer=[%.*s]\n", buffer_len, buffer);
	int buffer_len = 0;
	buffer_len += snprintf(&buffer[buffer_len], sizeof(buffer) - buffer_len, "[%X] ", (int)xId);
	buffer_len += vsnprintf(&buffer[buffer_len], sizeof(buffer) - buffer_len, fmt, l);
	int sent = 0;

	//write to the early buffer in any case
	int len = min(buffer_len, (sizeof(xEarlyLog) - xEarlyLogIdx));
	if(len > 0)
	{
		unsigned int prev_left2send = xEarlyLogIdx - xEarlyLogIdxSent; //should usually be 0

		memcpy(&xEarlyLog[xEarlyLogIdx], buffer, len);
		xEarlyLogIdx += len;
//		printf("LOG %d, %d", xEarlyLogIdx, len);

//		if(prev_left2send == 0) //if history had been sent off, we can safely send
//		{
			sent = net_logging_out(buffer, buffer_len);

//			//if sending was already possible, also keep the Sent position current
//			if(sent >= len)
//				xEarlyLogIdxSent += len;
//		}
	}
	else
	{
		sent = net_logging_out(buffer, buffer_len);
	}

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

void net_logging_early_init(bool enableStdout)
{
	xPrevious_early_vprintf_like = esp_log_set_early_vprintf(net_logging_early_printf);
	writeToStdout = enableStdout;

	net_logging_init(enableStdout);
}

void net_logging_init(bool enableStdout)
{
	xPrevious_vprintf_like = esp_log_set_vprintf(net_logging_vprintf);
	writeToStdout = enableStdout;
}

void udp_client(void *pvParameters);

esp_err_t udp_logging_init(char *ipaddr, unsigned long port, int16_t enableStdout) {
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
	//printf("ulTaskNotifyTake\n");

	// Set function used to output log entries.
	writeToStdout = enableStdout;
	bLoggersActive = (xMessageBufferTrans_udp != NULL)
							|| (xMessageBufferTrans_tcp != NULL)
							|| (xMessageBufferTrans_mqqt != NULL)
							|| (xMessageBufferTrans_http != NULL)
							;

//	esp_log_set_vprintf(logging_vprintf);
	return ESP_OK;
}

void tcp_client(void *pvParameters);

esp_err_t tcp_logging_init(char *ipaddr, unsigned long port, int16_t enableStdout) {
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
	//printf("ulTaskNotifyTake\n");

	// Set function used to output log entries.
	writeToStdout = enableStdout;
	bLoggersActive = (xMessageBufferTrans_udp != NULL)
							|| (xMessageBufferTrans_tcp != NULL)
							|| (xMessageBufferTrans_mqqt != NULL)
							|| (xMessageBufferTrans_http != NULL)
							;
//	esp_log_set_vprintf(logging_vprintf);
	return ESP_OK;
}

void mqtt_pub(void *pvParameters);

esp_err_t mqtt_logging_init(char *url, char *topic, int16_t enableStdout) {
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
	//printf("ulTaskNotifyTake\n");

	// Set function used to output log entries.
	writeToStdout = enableStdout;
	bLoggersActive = (xMessageBufferTrans_udp != NULL)
							|| (xMessageBufferTrans_tcp != NULL)
							|| (xMessageBufferTrans_mqqt != NULL)
							|| (xMessageBufferTrans_http != NULL)
							;
//	esp_log_set_vprintf(logging_vprintf);
	return ESP_OK;
}

void http_client(void *pvParameters);

esp_err_t http_logging_init(char *url, int16_t enableStdout) {
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
	//printf("ulTaskNotifyTake\n");

	// Set function used to output log entries.
	writeToStdout = enableStdout;
	bLoggersActive = (xMessageBufferTrans_udp != NULL)
							|| (xMessageBufferTrans_tcp != NULL)
							|| (xMessageBufferTrans_mqqt != NULL)
							|| (xMessageBufferTrans_http != NULL)
							;
//	esp_log_set_vprintf(logging_vprintf);
	return ESP_OK;
}

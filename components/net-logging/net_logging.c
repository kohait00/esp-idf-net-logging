#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"

#include "esp_system.h"
#include "esp_log.h"

#include "net_logging.h"

#	define min(x, y) ((x) < (y) ? (x) : (y))

MessageBufferHandle_t xMessageBufferTrans_udp = NULL;
MessageBufferHandle_t xMessageBufferTrans_tcp = NULL;
MessageBufferHandle_t xMessageBufferTrans_mqqt = NULL;
MessageBufferHandle_t xMessageBufferTrans_http = NULL;

bool writeToStdout;

int xEarlyLogIdx = 0;
unsigned char xEarlyLog[2048u] = {0};

int retreive_early_log(void* dest, int size)
{
	int len = min(size, xEarlyLogIdx);
	memcpy(dest, xEarlyLog, len);

	printf("LOG RETREIVE %d, %d", xEarlyLogIdx, len);

	return xEarlyLogIdx;
}

int logging_vprintf( const char *fmt, va_list l ) {

	bool bLoggersActive = (xMessageBufferTrans_udp != NULL)
							|| (xMessageBufferTrans_tcp != NULL)
							|| (xMessageBufferTrans_mqqt != NULL)
							|| (xMessageBufferTrans_http != NULL)
							;
	// Convert according to format
	char buffer[xItemSize];
	int buffer_len = vsprintf(buffer, fmt, l);
	//printf("logging_vprintf buffer_len=%d\n",buffer_len);
	//printf("logging_vprintf buffer=[%.*s]\n", buffer_len, buffer);

	if(bLoggersActive)
	{
		if (buffer_len > 0) {
			// Send MessageBuffer
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;

			if(xMessageBufferTrans_udp != NULL) {
				size_t sended = xMessageBufferSendFromISR(xMessageBufferTrans_udp, &buffer, buffer_len, &xHigherPriorityTaskWoken);
				//printf("logging_vprintf sended=%d\n",sended);
				assert(sended == buffer_len);
			}
			if(xMessageBufferTrans_tcp != NULL) {
				size_t sended = xMessageBufferSendFromISR(xMessageBufferTrans_tcp, &buffer, buffer_len, &xHigherPriorityTaskWoken);
				assert(sended == buffer_len);
			}
			if(xMessageBufferTrans_mqqt != NULL) {
				size_t sended = xMessageBufferSendFromISR(xMessageBufferTrans_mqqt, &buffer, buffer_len, &xHigherPriorityTaskWoken);
				assert(sended == buffer_len);
			}
			if(xMessageBufferTrans_http != NULL) {
				size_t sended = xMessageBufferSendFromISR(xMessageBufferTrans_http, &buffer, buffer_len, &xHigherPriorityTaskWoken);
				assert(sended == buffer_len);
			}
		}
	}
//	else
	{
		int len = min(buffer_len, (sizeof(xEarlyLog) - xEarlyLogIdx));
		if(len > 0)
		{
			//add buffer / buffer_len to an bootup buffer //todo
			memcpy(&xEarlyLog[xEarlyLogIdx], buffer, len);
			xEarlyLogIdx += len;
		}
	}

	// Write to stdout
	if (writeToStdout || (!bLoggersActive)) { //if no logger active ignore the writeToStdout and print anyway
		return vprintf( fmt, l );
	} else {
		return 0;
	}
}

void net_logging_early_init(int16_t enableStdout)
{
	esp_log_set_vprintf(logging_vprintf);
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
//	esp_log_set_vprintf(logging_vprintf);
	return ESP_OK;
}

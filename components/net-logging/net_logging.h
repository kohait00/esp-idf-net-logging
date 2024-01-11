#ifndef NET_LOGGING_H_
#define NET_LOGGING_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_system.h>
#include "net_logging_early.h"

typedef struct {
	uint16_t port;
	char ipv4[20]; // xxx.xxx.xxx.xxx
	char url[64]; // mqtt://iot.eclipse.org
	char topic[64];
	TaskHandle_t taskHandle;
} NET_LOGGING_PARAMETER_T;

typedef enum NET_LOGGING_DEST_e
{
	NET_LOGGING_DEST_UDP = 0,
	NET_LOGGING_DEST_TCP = 1,
	NET_LOGGING_DEST_MQQT = 2,
	NET_LOGGING_DEST_HTTP = 3,
	NET_LOGGING_DEST_COUNT = 4,
} NET_LOGGING_DEST;

bool net_logging_is_enabled(void);
void net_logging_enable_log(void);
void net_logging_disable_log(void);
void net_logging_set_id(unsigned int id);

void net_logging_init(unsigned int id, bool enableStdout);

esp_err_t udp_logging_init(char *ipaddr, unsigned long port);
esp_err_t tcp_logging_init(char *ipaddr, unsigned long port);
esp_err_t mqtt_logging_init(char *url, char *topic);
esp_err_t http_logging_init(char *url);

//

int net_logging_vprintf( const char *fmt, va_list l );
int net_logging_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* NET_LOGGING_H_ */

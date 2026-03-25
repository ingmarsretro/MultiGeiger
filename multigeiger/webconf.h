// Web Configuration related code
// also: OTA updates

#ifndef _WEBCONF_H_
#define _WEBCONF_H_

#include "IotWebConf.h"

extern bool speakerTick;
extern bool playSound;
extern bool ledTick;
extern bool showDisplay;
extern bool sendToCommunity;
extern bool sendToMadavi;
extern bool sendToLora;
extern bool sendToBle;
extern bool sendToMqtt;
extern bool soundLocalAlarm;

extern char appeui[];
extern char deveui[];
extern char appkey[];
extern char mqttBroker[];
extern char mqttPort[];
extern char mqttTopic[];

extern float localAlarmThreshold;
extern int localAlarmFactor;

extern char ssid[];
extern IotWebConf iotWebConf;

void setup_webconf(bool loraHardware);

// When MULTIGEIGER_MINIMAL_WIFI: avoid calling iotWebConf.getState()/delay() which assume init() ran.
int get_effective_wifi_state(void);
void webconf_loop_step(unsigned long ms);

#endif // _WEBCONF_H_

// MQTT publish for Geiger data

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "log.h"
#include "userdefines.h"
#include "webconf.h"

static WiFiClient mqttWifiClient;
static PubSubClient mqttClient(mqttWifiClient);
static bool mqtt_configured = false;

void mqtt_reconnect(void) {
  mqtt_configured = false;
  mqttClient.disconnect();
}

bool mqtt_publish_geiger(float usv_h, unsigned int cpm, float usv_h_since_start, const char *firmware_version) {
  if (!sendToMqtt || mqttBroker[0] == '\0')
    return false;

  int port = atoi(mqttPort);
  if (port <= 0)
    port = 1883;

  if (!mqtt_configured) {
    mqttClient.setServer(mqttBroker, (uint16_t)port);
    mqtt_configured = true;
  }

  if (!mqttClient.connected()) {
    String clientId = "multigeiger-";
    clientId += String(random(0xffff), HEX);
    if (!mqttClient.connect(clientId.c_str()))
      return false;
  }

  // JSON payload: usv_h, cpm, usv_h_since_start, version
  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"usv_h\":%.4f,\"cpm\":%u,\"usv_h_since_start\":%.4f,\"version\":\"%s\"}",
           (double)usv_h, cpm, (double)usv_h_since_start, firmware_version ? firmware_version : "");

  bool ok = mqttClient.publish(mqttTopic, payload);
  if (DEBUG_SERVER_SEND)
    log(DEBUG, "MQTT publish %s: %s", ok ? "ok" : "fail", payload);
  return ok;
}

void mqtt_loop(void) {
  if (sendToMqtt && mqttBroker[0] != '\0')
    mqttClient.loop();
}

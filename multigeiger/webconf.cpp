// Web Configuration related code
// also: OTA updates

#include <Arduino.h>
#include <WiFi.h>

#include "log.h"
#include "speaker.h"

#include "IotWebConf.h"
#include "IotWebConfTParameter.h"
#include <IotWebConfESP32HTTPUpdateServer.h>
#include "userdefines.h"
#include "mqtt.h"

// Checkboxes have 'selected' if checked, so we need 9 byte for this string.
#define CHECKBOX_LEN 9

bool speakerTick = SPEAKER_TICK;
bool playSound = PLAY_SOUND;
bool showDisplay = SHOW_DISPLAY;
bool sendToCommunity = SEND2SENSORCOMMUNITY;
bool sendToMadavi = SEND2MADAVI;
bool sendToLora = SEND2LORA;
bool sendToBle = SEND2BLE;
bool sendToMqtt = false;
bool soundLocalAlarm = LOCAL_ALARM_SOUND;

char speakerTick_c[CHECKBOX_LEN];
char playSound_c[CHECKBOX_LEN];
char showDisplay_c[CHECKBOX_LEN];
char sendToCommunity_c[CHECKBOX_LEN];
char sendToMadavi_c[CHECKBOX_LEN];
char sendToLora_c[CHECKBOX_LEN];
char sendToBle_c[CHECKBOX_LEN];
char soundLocalAlarm_c[CHECKBOX_LEN];
char sendToMqtt_c[CHECKBOX_LEN];

char appeui[17] = "";
char deveui[17] = "";
char appkey[IOTWEBCONF_WORD_LEN] = "";
#define MQTT_STR_LEN 64
char mqttBroker[MQTT_STR_LEN] = "";
char mqttPort[6] = "1883";
char mqttTopic[MQTT_STR_LEN] = "multigeiger/data";
static bool isLoraBoard;

float localAlarmThreshold = LOCAL_ALARM_THRESHOLD;
int localAlarmFactor = (int)LOCAL_ALARM_FACTOR;

iotwebconf::ParameterGroup grpMisc = iotwebconf::ParameterGroup("misc", "Misc. Settings");
iotwebconf::CheckboxParameter startSoundParam = iotwebconf::CheckboxParameter("Start sound", "startSound", playSound_c, CHECKBOX_LEN, playSound);
iotwebconf::CheckboxParameter speakerTickParam = iotwebconf::CheckboxParameter("Speaker tick", "speakerTick", speakerTick_c, CHECKBOX_LEN, speakerTick);
iotwebconf::CheckboxParameter showDisplayParam = iotwebconf::CheckboxParameter("Show display", "showDisplay", showDisplay_c, CHECKBOX_LEN, showDisplay);

iotwebconf::ParameterGroup grpTransmission = iotwebconf::ParameterGroup("transmission", "Transmission Settings");
iotwebconf::CheckboxParameter sendToCommunityParam = iotwebconf::CheckboxParameter("Send to sensor.community", "send2Community", sendToCommunity_c, CHECKBOX_LEN, sendToCommunity);
iotwebconf::CheckboxParameter sendToMadaviParam = iotwebconf::CheckboxParameter("Send to madavi.de", "send2Madavi", sendToMadavi_c, CHECKBOX_LEN, sendToMadavi);
iotwebconf::CheckboxParameter sendToBleParam = iotwebconf::CheckboxParameter("Send to BLE (Reboot required!)", "send2ble", sendToBle_c, CHECKBOX_LEN, sendToBle);
iotwebconf::CheckboxParameter sendToMqttParam = iotwebconf::CheckboxParameter("Send to MQTT", "send2Mqtt", sendToMqtt_c, CHECKBOX_LEN, sendToMqtt);

iotwebconf::ParameterGroup grpMqtt = iotwebconf::ParameterGroup("mqtt", "MQTT Settings");
iotwebconf::TextParameter mqttBrokerParam = iotwebconf::TextParameter("Broker host", "mqttBroker", mqttBroker, MQTT_STR_LEN);
iotwebconf::TextParameter mqttPortParam = iotwebconf::TextParameter("Broker port", "mqttPort", mqttPort, 6);
iotwebconf::TextParameter mqttTopicParam = iotwebconf::TextParameter("Topic", "mqttTopic", mqttTopic, MQTT_STR_LEN);

iotwebconf::ParameterGroup grpLoRa = iotwebconf::ParameterGroup("lora", "LoRa Settings");
iotwebconf::CheckboxParameter sendToLoraParam = iotwebconf::CheckboxParameter("Send to LoRa (=>TTN)", "send2lora", sendToLora_c, CHECKBOX_LEN, sendToLora);
iotwebconf::TextParameter deveuiParam = iotwebconf::TextParameter("DEVEUI", "deveui", deveui, 17);
iotwebconf::TextParameter appeuiParam = iotwebconf::TextParameter("APPEUI", "appeui", appeui, 17);
iotwebconf::TextParameter appkeyParam = iotwebconf::TextParameter("APPKEY", "appkey", appkey, 33);

iotwebconf::ParameterGroup grpAlarm = iotwebconf::ParameterGroup("alarm", "Local Alarm Setting");
iotwebconf::CheckboxParameter soundLocalAlarmParam = iotwebconf::CheckboxParameter("Enable local alarm sound", "soundLocalAlarm", soundLocalAlarm_c, CHECKBOX_LEN, soundLocalAlarm);
iotwebconf::FloatTParameter localAlarmThresholdParam =
  iotwebconf::Builder<iotwebconf::FloatTParameter>("localAlarmThreshold").
  label("Local alarm threshold (µSv/h)").
  defaultValue(localAlarmThreshold).
  step(0.1).placeholder("e.g. 0.5").build();
iotwebconf::IntTParameter<int16_t> localAlarmFactorParam =
  iotwebconf::Builder<iotwebconf::IntTParameter<int16_t>>("localAlarmFactor").
  label("Factor of current dose rate vs. accumulated").
  defaultValue(localAlarmFactor).
  min(2).max(100).
  step(1).placeholder("2..100").build();

// This only needs to be changed if the layout of the configuration is changed.
// Appending new variables does not require a new version number here.
// If this value is changed, ALL configuration variables must be re-entered,
// including the WiFi credentials.
#define CONFIG_VERSION "017"

DNSServer dnsServer;
WebServer server(80);
HTTPUpdateServer httpUpdater;

char *buildSSID(void);

// SSID == thingName
const char *theName = buildSSID();
char ssid[IOTWEBCONF_WORD_LEN];  // LEN == 33 (2020-01-13)

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "12345678";

IotWebConf iotWebConf(theName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);

#if MULTIGEIGER_MINIMAL_WIFI
static int wifiStateOverride = -1;
#endif

int get_effective_wifi_state(void) {
#if MULTIGEIGER_MINIMAL_WIFI
  if (wifiStateOverride >= 0)
    return wifiStateOverride;
#endif
  return (int)iotWebConf.getState();
}

void webconf_loop_step(unsigned long ms) {
#if MULTIGEIGER_MINIMAL_WIFI
  unsigned long end = millis() + ms;
  while (millis() < end) {
    server.handleClient();
    delay(1);
  }
#else
  iotWebConf.delay(ms);
#endif
}

unsigned long getESPchipID() {
  uint64_t espid = ESP.getEfuseMac();
  uint8_t *pespid = (uint8_t *)&espid;
  uint32_t id = 0;
  uint8_t *pid = (uint8_t *)&id;
  pid[0] = (uint8_t)pespid[5];
  pid[1] = (uint8_t)pespid[4];
  pid[2] = (uint8_t)pespid[3];
  log(INFO, "ID: %08X", id);
  log(INFO, "MAC: %04X%08X", (uint16_t)(espid >> 32), (uint32_t)espid);
  return id;
}

char *buildSSID() {
  // build SSID from ESP chip id
  uint32_t id = getESPchipID();
  sprintf(ssid, "ESP32-%d", id);
  return ssid;
}

void handleRoot(void) {  // Handle web requests to "/" path.
#if !MULTIGEIGER_MINIMAL_WIFI
  if (iotWebConf.handleCaptivePortal()) {
    return;
  }
#endif
  const char *index =
    "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1, user-scalable=no' />"
    "<title>MultiGeiger Configuration</title>"
    "</head>"
    "<body>"
    "<h1>Configuration</h1>"
    "<p>"
    "Go to the <a href='config'>config page</a> to change settings or update firmware."
    "</p>"
    "</body>"
    "</html>\n";
  server.send(200, "text/html;charset=UTF-8", index);
  // looks like user wants to do some configuration or maybe flash firmware.
  // while accessing the flash, we need to turn ticking off to avoid exceptions.
  // user needs to save the config (or flash firmware + reboot) to turn it on again.
  // note: it didn't look like there is an easy way to put this call at the right place
  // (start of fw flash / start of config save) - this is why it is here.
  tick_enable(false);
}

static char lastWiFiSSID[IOTWEBCONF_WORD_LEN] = "";

void loadConfigVariables(void) {
  iotwebconf::Parameter *wifiSsidParam = iotWebConf.getWifiSsidParameter();
  if (wifiSsidParam && wifiSsidParam->valueBuffer) {
    // check if WiFi SSID has changed. If so, restart cpu. Otherwise, the program will not use the new SSID
    if ((strcmp(lastWiFiSSID, "") != 0) && (strcmp(lastWiFiSSID, wifiSsidParam->valueBuffer) != 0)) {
      log(INFO, "Doing restart...");
      ESP.restart();
    }
    strcpy(lastWiFiSSID, wifiSsidParam->valueBuffer);
  }

  speakerTick = speakerTickParam.isChecked();
  playSound = startSoundParam.isChecked();
  showDisplay = showDisplayParam.isChecked();
  sendToCommunity = sendToCommunityParam.isChecked();
  sendToMadavi = sendToMadaviParam.isChecked();
  sendToLora = sendToLoraParam.isChecked();
  sendToBle = sendToBleParam.isChecked();
  sendToMqtt = sendToMqttParam.isChecked();
  soundLocalAlarm = soundLocalAlarmParam.isChecked();
  localAlarmThreshold = localAlarmThresholdParam.value();
  localAlarmFactor = localAlarmFactorParam.value();
}

void configSaved(void) {
  log(INFO, "Config saved. ");
  loadConfigVariables();
  mqtt_reconnect();
  tick_enable(true);
}

void setup_webconf(bool loraHardware) {
  isLoraBoard = loraHardware;
  iotWebConf.setConfigSavedCallback(&configSaved);
  // *INDENT-OFF*   <- for 'astyle' to not format the following 3 lines
  iotWebConf.setupUpdateServer(
    [](const char *updatePath) { httpUpdater.setup(&server, updatePath); },
    [](const char *userName, char *password) { httpUpdater.updateCredentials(userName, password); });
  // *INDENT-ON*
  // override the confusing default labels of IotWebConf:
  iotWebConf.getThingNameParameter()->label = "Geiger accesspoint SSID";
  iotWebConf.getApPasswordParameter()->label = "Geiger accesspoint password";
  iotWebConf.getWifiSsidParameter()->label = "WiFi client SSID";
  iotWebConf.getWifiPasswordParameter()->label = "WiFi client password";

  // add the setting parameter
  grpMisc.addItem(&startSoundParam);
  grpMisc.addItem(&speakerTickParam);
  grpMisc.addItem(&showDisplayParam);
  iotWebConf.addParameterGroup(&grpMisc);
  grpTransmission.addItem(&sendToCommunityParam);
  grpTransmission.addItem(&sendToMadaviParam);
  grpTransmission.addItem(&sendToBleParam);
  grpTransmission.addItem(&sendToMqttParam);
  iotWebConf.addParameterGroup(&grpTransmission);
  grpMqtt.addItem(&mqttBrokerParam);
  grpMqtt.addItem(&mqttPortParam);
  grpMqtt.addItem(&mqttTopicParam);
  iotWebConf.addParameterGroup(&grpMqtt);
  if (isLoraBoard) {
    grpLoRa.addItem(&sendToLoraParam);
    grpLoRa.addItem(&deveuiParam);
    grpLoRa.addItem(&appeuiParam);
    grpLoRa.addItem(&appkeyParam);
    iotWebConf.addParameterGroup(&grpLoRa);
  }
  grpAlarm.addItem(&soundLocalAlarmParam);
  grpAlarm.addItem(&localAlarmThresholdParam);
  grpAlarm.addItem(&localAlarmFactorParam);
  iotWebConf.addParameterGroup(&grpAlarm);

  // if we don't have LoRa hardware, do not send to LoRa
  if (!isLoraBoard)
    sendToLora = false;

#if MULTIGEIGER_MINIMAL_WIFI
  // Start AP and web server without IotWebConf.init() to avoid LoadProhibited crash
  // on Wifi_LorA-32_V2 when IotWebConf triggers WiFi state change to AP.
  // Use 8-char password (WPA2 minimum); some devices reject others.
  static const char apPassword[] = "12345678";
  log(INFO, "Minimal WiFi: starting AP without IotWebConf.init()");
  log(INFO, "AP SSID: %s  |  AP password: %s", ssid, apPassword);
  wifiStateOverride = (int)iotwebconf::ApMode;
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, apPassword)) {
    log(ERROR, "Minimal WiFi: softAP failed");
    wifiStateOverride = 0;  // off / not connected (IotWebConf state 0)
  }
  delay(500);
  server.begin();
  server.on("/", handleRoot);
  server.on("/config", []() {
    server.send(200, "text/html; charset=utf-8",
      "<!DOCTYPE html><html><body><h1>Config</h1><p>Minimal WiFi mode: full config requires firmware without MULTIGEIGER_MINIMAL_WIFI.</p></body></html>");
  });
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
#else
  // Run init() in a dedicated task with large stack to avoid stack overflow.
  static SemaphoreHandle_t initDone = xSemaphoreCreateBinary();
  const uint32_t WEBCONF_INIT_STACK = 16 * 1024;
  xTaskCreate(
    [](void *) {
      iotWebConf.init();
      xSemaphoreGive(initDone);
      vTaskDelete(NULL);
    },
    "webconf_init",
    WEBCONF_INIT_STACK,
    NULL,
    1,
    NULL
  );
  if (initDone != NULL) {
    if (xSemaphoreTake(initDone, pdMS_TO_TICKS(15000)) != pdTRUE)
      log(WARNING, "webconf init task timed out");
  } else {
    iotWebConf.init();
  }
  delay(500);
  loadConfigVariables();
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.onNotFound([]() {
    iotWebConf.handleNotFound();
  });
#endif
}

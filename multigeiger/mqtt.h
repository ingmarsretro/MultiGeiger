#ifndef _MQTT_H_
#define _MQTT_H_

// Publish Geiger data to MQTT: uSv/h, CPM, uSv/h since start, firmware version.
// Returns true if published successfully.
bool mqtt_publish_geiger(float usv_h, unsigned int cpm, float usv_h_since_start, const char *firmware_version);

// Call from loop() to maintain connection and process incoming (optional).
void mqtt_loop(void);

// Reconnect if broker/port/topic changed (call after config save).
void mqtt_reconnect(void);

#endif

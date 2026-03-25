# MultiGeiger (ESP32 WiFi LoRa 32 V2 firmware)

This repository contains a **MultiGeiger** firmware build targeting the **Heltec WiFi LoRa 32 V2** (ESP32 + OLED + LoRa) and a matching hardware pinout. MultiGeiger is a **radioactivity measurement device** (Geiger‑Müller tube + HV supply) that can display measurements locally and publish them to community services.

- **Firmware location**: `MultiGeiger-master/` (PlatformIO project)
- **Online documentation (EN/DE, versioned)**: `https://multigeiger.readthedocs.org/`
- **Map / Karte**: `https://multigeiger.citysensor.de/`

---

## English

### What is MultiGeiger?

MultiGeiger measures Geiger‑Müller tube pulses, calculates **CPM** (counts per minute) and derived dose rates (depending on tube type), and can:

- show current / accumulated values on an OLED
- tick on every pulse (speaker + LED)
- send measurements to:
  - **sensor.community** (archive)
  - **madavi.de** (near real-time graphs)
  - **TTN (LoRaWAN)** (for LoRa boards)
  - **MQTT** (local broker / home automation)

Temperature / humidity / pressure sensors **BME280 / BME680** are supported (optional).

### Hardware target and pinout (Heltec WiFi LoRa 32 V2)

This build is configured for the Heltec board environment `wifi_lora_32_v2`. The intended pin mapping is documented in `MultiGeiger-master/platformio.ini`:

- **I²C**: SDA = GPIO4, SCL = GPIO15
- **GM pulse input**: GPIO2 (`GMZ_COUNT`)
- **HV / charging**: GPIO22 (`HV_CAP_FUL`), GPIO23 (`HV_FET_OUT`)
- **Speaker**: GPIO0 + GPIO13
- **DIP switches**: GPIO39/38/37/36 (SW0..SW3)

DIP switches (closed to GND = ON) are read at runtime in the loop:

- **SW0**: speaker tick enable
- **SW1**: display enable
- **SW2**: LED tick enable
- **SW3**: BLE enable

### Build and flash (PlatformIO)

This is a PlatformIO project (`MultiGeiger-master/platformio.ini`) using:

- platform `espressif32`
- framework `arduino`
- board `heltec_wifi_lora_32_V2`

Open `MultiGeiger-master/` in PlatformIO and build/upload the `wifi_lora_32_v2` environment.

### Configuration (WiFi, servers, LoRa, MQTT, OTA)

MultiGeiger provides a web configuration UI and **OTA firmware update**:

- The device advertises an AP with an SSID like `ESP32-<chipid>` (built from the ESP32 MAC).
- In **full config mode**, the config page supports toggles for:
  - display / speaker / LED tick
  - send to sensor.community / madavi / TTN (LoRa) / BLE / MQTT
  - LoRaWAN keys (DEVEUI / APPEUI / APPKEY) when LoRa hardware is present
  - MQTT broker / port / topic
  - optional local alarm threshold + factor

**AP password note**:  (see `MultiGeiger-master/multigeiger/webconf.cpp`, `wifiInitialApPassword`). 

### Data formats (MQTT)

If MQTT is enabled and configured, the firmware publishes a JSON payload like:

```json
{"usv_h":0.0123,"cpm":17,"usv_h_since_start":0.0101,"version":"V2.0.0"}
```

Topic and broker/port are configurable in the web UI.

### “Final version” changes in this repository

Compared to a plain upstream build, this repo includes/uses several stability and deployment changes (visible in source and build flags):

- **Crash-avoidance for WiFi/AP + web config**:
  - Optional **minimal WiFi mode** controlled by `MULTIGEIGER_MINIMAL_WIFI` (build flag). In this mode, the firmware starts a basic AP + web server without full IotWebConf init, to avoid a known crash scenario on this target.
  - In normal mode (`MULTIGEIGER_MINIMAL_WIFI=0`), `IotWebConf.init()` is executed in a dedicated FreeRTOS task with a large stack to reduce stack/overflow issues during AP mode transitions.
  - The main loop task stack is increased (`SET_LOOP_TASK_STACK_SIZE(16 * 1024)`) to avoid “LoadProhibited” failures around AP mode / configuration.
- **HV recharge and audio moved away from ISR flash calls**:
  - Controlled by `MULTIGEIGER_RECHARGE_FROM_TASK=1`. Recharge (and related ticking) is run from a task; timer ISR only wakes the task. This avoids exceptions when ISR paths would otherwise call code located in flash.
- **MQTT uplink support**:
  - Optional MQTT publish via `PubSubClient`, configurable from the web UI (broker/port/topic).



### License

MultiGeiger is licensed under **GNU GPL v3 (or later)**. See `MultiGeiger-master/LICENSE`.

---

## Deutsch

### Was ist MultiGeiger?

MultiGeiger misst die Impulse eines Geiger‑Müller‑Zählrohrs, berechnet **CPM** (Counts per Minute) sowie abgeleitete Dosisleistungen (abhängig vom Röhrentyp) und kann:

- Messwerte lokal auf einem OLED anzeigen
- bei jedem Impuls ticken (Lautsprecher + LED)
- Messdaten senden an:
  - **sensor.community** (Archiv)
  - **madavi.de** (nahezu Echtzeit‑Graphen)
  - **TTN (LoRaWAN)** (bei LoRa‑Boards)
  - **MQTT** (lokaler Broker / Smart‑Home)

Optional werden **BME280 / BME680** (Temperatur/Luftfeuchte/Luftdruck) unterstützt.

### Hardware-Ziel und Pinbelegung (Heltec WiFi LoRa 32 V2)

Dieses Build ist für die PlatformIO‑Umgebung `wifi_lora_32_v2` ausgelegt. Die vorgesehene Pinbelegung steht in `MultiGeiger-master/platformio.ini`:

- **I²C**: SDA = GPIO4, SCL = GPIO15
- **GM‑Impuls Eingang**: GPIO2 (`GMZ_COUNT`)
- **HV / Laden**: GPIO22 (`HV_CAP_FUL`), GPIO23 (`HV_FET_OUT`)
- **Lautsprecher**: GPIO0 + GPIO13
- **DIP‑Schalter**: GPIO39/38/37/36 (SW0..SW3)

DIP‑Schalter (gegen GND geschlossen = EIN) werden im Loop ausgewertet:

- **SW0**: Lautsprecher‑Tick
- **SW1**: Display EIN/AUS
- **SW2**: LED‑Tick
- **SW3**: BLE EIN/AUS

### Build und Flashen (PlatformIO)

PlatformIO‑Projekt: `MultiGeiger-master/platformio.ini` mit:

- Platform `espressif32`
- Framework `arduino`
- Board `heltec_wifi_lora_32_V2`

Öffne `MultiGeiger-master/` in PlatformIO und baue/flash die Umgebung `wifi_lora_32_v2`.

### Konfiguration (WLAN, Server, LoRa, MQTT, OTA)

MultiGeiger bietet eine Web‑Konfiguration sowie **OTA‑Firmware‑Updates**:

- Das Gerät erzeugt einen Access Point mit SSID `ESP32-<chipid>` (aus der ESP32‑MAC abgeleitet).
- Im **vollständigen Konfigurationsmodus** lassen sich u. a. einstellen:
  - Display / Lautsprecher / LED Tick
  - Senden an sensor.community / madavi / TTN (LoRa) / BLE / MQTT
  - LoRaWAN‑Schlüssel (DEVEUI / APPEUI / APPKEY) bei vorhandener LoRa‑Hardware
  - MQTT Broker / Port / Topic
  - optionaler lokaler Alarm (Schwelle + Faktor)

**Hinweis zum AP‑Passwort**: (siehe `MultiGeiger-master/multigeiger/webconf.cpp`, `wifiInitialApPassword`). 

### Datenformat (MQTT)

Wenn MQTT aktiviert und konfiguriert ist, wird ein JSON‑Payload publiziert, z. B.:

```json
{"usv_h":0.0123,"cpm":17,"usv_h_since_start":0.0101,"version":"V2.0.0"}
```

Topic sowie Broker/Port sind über die Web‑UI konfigurierbar.

### Änderungen in der „Final Version“ dieses Repos

Dieses Repo enthält (sichtbar im Quellcode und den Build‑Flags) mehrere Änderungen für Stabilität und Deployment:

- **Stabilität bei WiFi/AP + Web‑Konfiguration**:
  - Optionaler **Minimal‑WiFi‑Modus** via `MULTIGEIGER_MINIMAL_WIFI` (Build‑Flag). In diesem Modus wird ein einfacher AP + Webserver‑Setup ohne vollständige IotWebConf‑Initialisierung gestartet, um einen bekannten Crash‑Pfad zu vermeiden.
  - Im Normalbetrieb (`MULTIGEIGER_MINIMAL_WIFI=0`) läuft `IotWebConf.init()` in einem eigenen FreeRTOS‑Task mit großem Stack, um Stack‑Probleme beim Wechsel in den AP‑Modus zu reduzieren.
  - Erhöhter Stack für den Loop‑Task (`SET_LOOP_TASK_STACK_SIZE(16 * 1024)`), um “LoadProhibited”‑Fehler zu vermeiden.
- **HV‑Nachladen und Audio weg aus ISR‑Flash‑Pfaden**:
  - Gesteuert über `MULTIGEIGER_RECHARGE_FROM_TASK=1`. Das Nachladen läuft in einem Task; die Timer‑ISR weckt nur den Task. Das reduziert Exceptions, wenn ISR‑Code sonst Flash‑Code aufrufen würde.
- **MQTT‑Uplink**:
  - Optionales MQTT‑Publishing via `PubSubClient`, konfigurierbar in der Web‑UI (Broker/Port/Topic).



### Lizenz

MultiGeiger steht unter der **GNU GPL v3 (oder neuer)**. Siehe `MultiGeiger-master/LICENSE`.

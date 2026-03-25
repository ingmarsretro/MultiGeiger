// OLED display related code

#include <Arduino.h>
#include <U8x8lib.h>

#include "hal/heltecv2.h"
#include "version.h"
#include "log.h"
#include "userdefines.h"

#include "display.h"

#define PIN_DISPLAY_ON 21

// Wifi_LorA-32_V2: pin4=SDA, pin15=SCL
#define PIN_OLED_RST 16
#define PIN_OLED_SCL 15
#define PIN_OLED_SDA 4

U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(PIN_OLED_RST, PIN_OLED_SCL, PIN_OLED_SDA);
U8X8 *pu8x8;

bool displayIsClear;
static bool isLoraBoard;

// Wifi_LorA-32_V2 has 128x64 OLED; use full size for all boards.
#define DISPLAY_STATUS_LINE 7

void display_start_screen(void) {
  char line[20];

  pu8x8->clear();

  pu8x8->setFont(u8x8_font_amstrad_cpc_extended_f);
  pu8x8->drawString(0, 0, "  Multi-Geiger");
  pu8x8->setFont(u8x8_font_victoriamedium8_r);
  pu8x8->drawString(0, 1, "________________");
  pu8x8->drawString(0, 3, "Info:boehri.de");
  snprintf(line, 17, "%s", VERSION_STR);
  pu8x8->drawString(0, 5, line);
  displayIsClear = false;
}

void setup_display(bool loraHardware) {
  isLoraBoard = loraHardware;
  pu8x8 = &u8x8;  // always 128x64 (Wifi_LorA-32_V2 has full-size OLED)
  if (isLoraBoard) {
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);
  }
  pu8x8->begin();
  display_start_screen();
  // Show measurement screen (0 nSv/h) so digits are visible; publish() will refresh later.
  display_GMC(0, 0, 0, true);
}

void clear_displayline(int line) {
  if (!pu8x8)
    return;
  pu8x8->drawString(0, line, "                ");  // 16 chars
}

void display_statusline(String txt) {
  if (txt.length() == 0 || !pu8x8)
    return;
  pu8x8->setFont(u8x8_font_victoriamedium8_r);
  clear_displayline(DISPLAY_STATUS_LINE);
  pu8x8->drawString(0, DISPLAY_STATUS_LINE, txt.c_str());
}

static int status[STATUS_MAX] = {ST_NODISPLAY, ST_NODISPLAY, ST_NODISPLAY, ST_NODISPLAY,
                                 ST_NODISPLAY, ST_NODISPLAY, ST_NODISPLAY, ST_NODISPLAY
                                };  // current status of misc. subsystems

static const char *status_chars[STATUS_MAX] = {
  // group WiFi and transmission to internet servers
  ".W0wA",  // ST_WIFI_OFF, ST_WIFI_CONNECTED, ST_WIFI_ERROR, ST_WIFI_CONNECTING, ST_WIFI_AP
  ".s1S?",  // ST_SCOMM_OFF, ST_SCOMM_IDLE, ST_SCOMM_ERROR, ST_SCOMM_SENDING, ST_SCOMM_INIT
  ".m2M?",  // ST_MADAVI_OFF, ST_MADAVI_IDLE, ST_MADAVI_ERROR, ST_MADAVI_SENDING, ST_MADAVI_INIT
  // group TTN (LoRa WAN)
  ".t3T?",  // ST_TTN_OFF, ST_TTN_IDLE, ST_TTN_ERROR, ST_TTN_SENDING, ST_TTN_INIT
  // group BlueTooth
  ".B4b?",  // ST_BLE_OFF, ST_BLE_CONNECTED, ST_BLE_ERROR, ST_BLE_CONNECTABLE, ST_BLE_INIT
  // group other
  ".",      // ST_NODISPLAY
  ".",      // ST_NODISPLAY
  ".H7",    // ST_NODISPLAY, ST_HV_OK, ST_HV_ERROR
};

void set_status(int index, int value) {
  if ((index >= 0) && (index < STATUS_MAX)) {
    if (status[index] != value) {
      status[index] = value;
      display_status();
    }
  } else
    log(ERROR, "invalid parameters: set_status(%d, %d)", index, value);
}

int get_status(int index) {
  return status[index];
}

char get_status_char(int index) {
  if ((index >= 0) && (index < STATUS_MAX)) {
    int idx = status[index];
    if (idx < strlen(status_chars[index]))
      return status_chars[index][idx];
    else
      log(ERROR, "string status_chars[%d] is too short, no char at index %d", index, idx);
  } else
    log(ERROR, "invalid parameters: get_status_char(%d)", index);
  return '?';  // some error happened
}

void display_status(void) {
  if (!pu8x8)
    return;
  char output[17];
  snprintf(output, 17, "%c %c %c %c %c %c %c %c",
           get_status_char(0), get_status_char(1), get_status_char(2), get_status_char(3),
           get_status_char(4), get_status_char(5), get_status_char(6), get_status_char(7));
  display_statusline(output);
}

char *format_time(unsigned int secs) {
  static char result[4];
  unsigned int mins = secs / 60;
  unsigned int hours = secs / (60 * 60);
  unsigned int days = secs / (24 * 60 * 60);
  if (secs < 60) {
    snprintf(result, 4, "%2ds", secs);
  } else if (mins < 60) {
    snprintf(result, 4, "%2dm", mins);
  } else if (hours < 24) {
    snprintf(result, 4, "%2dh", hours);
  } else {
    days = days % 100;  // roll over after 100d
    snprintf(result, 4, "%2dd", days);
  }
  return result;
}

void display_GMC(unsigned int TimeSec, int RadNSvph, int CPM, bool use_display) {
  if (!use_display) {
    // Do not clear: leaving last content avoids "only status line" (A ? . . . . 7) visible after display off.
    return;
  }

  pu8x8->clear();
  char output[40];

  pu8x8->setFont(u8x8_font_7x14_1x2_f);
  sprintf(output, "%3s%7d nSv/h", format_time(TimeSec), RadNSvph);
  pu8x8->drawString(0, 0, output);
  pu8x8->setFont(u8x8_font_inb33_3x6_n);
  sprintf(output, "%5d", CPM);
  pu8x8->drawString(0, 2, output);

  display_status();
  displayIsClear = false;
}

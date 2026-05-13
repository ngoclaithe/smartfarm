#ifndef CONFIG_H
#define CONFIG_H

const char* const WIFI_SSID     = "YOUR_WIFI_SSID";
const char* const WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* const MQTT_SERVER    = "192.168.1.100";
const int         MQTT_PORT      = 1883;
const char* const TOPIC_DATA     = "esp8266/data";
const char* const TOPIC_CONTROL  = "esp8266/control";

#define DHTPIN    4
#define DHTTYPE   DHT11
#define SOIL_PIN  34
#define PUMP_PIN  26
#define FAN_PIN   27
#define LIGHT_PIN 25

#define BTN_PUMP_PIN  32
#define BTN_FAN_PIN   33
#define BTN_LIGHT_PIN 35
#define BTN_MODE_PIN  39

#define OLED_ADDRESS 0x3C
#define DEBOUNCE_MS  250

#endif

#ifndef CONFIG_H
#define CONFIG_H

const char* const WIFI_SSID     = "TP-Link_B30E";
const char* const WIFI_PASSWORD = "78533554";

const char* const MQTT_SERVER    = "192.168.0.102";
const int         MQTT_PORT      = 1883;
const char* const TOPIC_DATA     = "esp8266/data";
const char* const TOPIC_CONTROL  = "esp8266/control";

// Chân cấu hình cho ESP32-S3
#define DHTPIN    4
#define DHTTYPE   DHT11
#define SOIL_PIN  5 // Analog ADC1_CH4
#define PUMP_PIN  6
#define FAN_PIN   7
#define LIGHT_PIN 15

// Các nút nhấn (ESP32-S3 thoải mái chân, dùng pull-up nội)
#define BTN_PUMP_PIN  16
#define BTN_FAN_PIN   1
#define BTN_LIGHT_PIN 2
#define BTN_MODE_PIN  21

// I2C cho OLED 1.3 inch (SH1106)
#define I2C_SDA_PIN   8
#define I2C_SCL_PIN   9
#define OLED_ADDRESS  0x3C

#define DEBOUNCE_MS  250

#endif

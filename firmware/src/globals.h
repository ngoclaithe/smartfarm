#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

enum OperationMode { MODE_MANUAL, MODE_SCHEDULE };

extern SemaphoreHandle_t xMutex;

extern float g_temp;
extern float g_hum;
extern float g_soil;
extern bool  g_pump_state;
extern bool  g_fan_state;
extern bool  g_light_state;
extern OperationMode g_mode;

extern unsigned long pump_off_time;
extern bool pump_timer_active;

extern WiFiClient espClient;
extern PubSubClient client;

#endif

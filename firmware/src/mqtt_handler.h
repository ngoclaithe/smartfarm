#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>

void mqtt_callback(char* topic, byte* payload, unsigned int length);
void mqtt_reconnect();
void publishState();
void TaskMQTT(void* pvParameters);

#endif

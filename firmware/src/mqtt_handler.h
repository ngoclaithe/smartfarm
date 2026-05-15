#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

void initMQTT();
void TaskMQTT(void* pvParameters);
void publishState();

#endif

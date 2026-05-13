#include "mqtt_handler.h"
#include "config.h"
#include "globals.h"
#include "wifi_manager.h"
#include "relay_control.h"
#include <ArduinoJson.h>

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, length)) return;

  const char* reason = doc["reason"] | "manual";

  xSemaphoreTake(xMutex, portMAX_DELAY);
  OperationMode mode = g_mode;
  xSemaphoreGive(xMutex);

  if (mode == MODE_MANUAL &&
      (strcmp(reason, "schedule") == 0 || strcmp(reason, "threshold") == 0)) {
    return;
  }

  const char* action = doc["action"];
  int duration = doc["duration_sec"] | 0;
  handleAction(action, duration);
  publishState();
}

void mqtt_reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      client.subscribe(TOPIC_CONTROL);
      publishState();
    } else {
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

void publishState() {
  if (!client.connected()) return;

  xSemaphoreTake(xMutex, portMAX_DELAY);
  float t = g_temp;
  float h = g_hum;
  float s = g_soil;
  bool pump  = g_pump_state;
  bool fan   = g_fan_state;
  bool light = g_light_state;
  OperationMode mode = g_mode;
  xSemaphoreGive(xMutex);

  StaticJsonDocument<300> doc;
  doc["temperature"]   = t;
  doc["air_humidity"]  = h;
  doc["soil_moisture"] = s;
  doc["pump"]  = pump;
  doc["fan"]   = fan;
  doc["light"] = light;
  doc["mode"]  = (mode == MODE_MANUAL) ? "manual" : "schedule";

  char buffer[300];
  serializeJson(doc, buffer);
  client.publish(TOPIC_DATA, buffer);
}

void TaskMQTT(void* pvParameters) {
  (void)pvParameters;
  setup_wifi();
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqtt_callback);

  for (;;) {
    if (!client.connected()) {
      mqtt_reconnect();
    }
    client.loop();

    xSemaphoreTake(xMutex, portMAX_DELAY);
    if (pump_timer_active && millis() > pump_off_time) {
      g_pump_state = false;
      pump_timer_active = false;
      digitalWrite(PUMP_PIN, LOW);
    }
    xSemaphoreGive(xMutex);

    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

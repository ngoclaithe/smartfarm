#include "sensor_task.h"
#include "config.h"
#include "globals.h"
#include <DHT.h>
#include <ArduinoJson.h>

static DHT dht(DHTPIN, DHTTYPE);

void TaskSensor(void* pvParameters) {
  (void)pvParameters;
  dht.begin();

  for (;;) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int soil_raw = analogRead(SOIL_PIN);
    int soil_mapped = map(soil_raw, 4095, 0, 0, 100);
    soil_mapped = constrain(soil_mapped, 0, 100);

    if (!isnan(t) && !isnan(h)) {
      xSemaphoreTake(xMutex, portMAX_DELAY);
      g_temp = t;
      g_hum = h;
      g_soil = soil_mapped;
      bool pump = g_pump_state;
      bool fan  = g_fan_state;
      bool light = g_light_state;
      OperationMode mode = g_mode;
      xSemaphoreGive(xMutex);

      if (client.connected()) {
        StaticJsonDocument<300> doc;
        doc["temperature"] = t;
        doc["air_humidity"] = h;
        doc["soil_moisture"] = soil_mapped;
        doc["pump"]  = pump;
        doc["fan"]   = fan;
        doc["light"] = light;
        doc["mode"]  = (mode == MODE_MANUAL) ? "manual" : "schedule";

        char buffer[300];
        serializeJson(doc, buffer);
        client.publish(TOPIC_DATA, buffer);
      }
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

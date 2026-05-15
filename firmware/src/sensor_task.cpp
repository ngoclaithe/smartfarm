#include "sensor_task.h"
#include "config.h"
#include "globals.h"
#include "mqtt_handler.h"
#include <DHT.h>
#include <ArduinoJson.h>

static DHT dht(DHTPIN, DHTTYPE);

void initSensors() {
  dht.begin();
}

void TaskSensor(void* pvParameters) {
  (void)pvParameters;
  for(;;) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int soil_raw = analogRead(SOIL_PIN);
    
    // ADC ESP32/ESP32-S3 là 12 bit (0-4095)
    int soil_mapped = map(soil_raw, 4095, 0, 0, 100);
    soil_mapped = constrain(soil_mapped, 0, 100);
    
    Serial.printf("Soil Raw: %d -> Mapped: %d%%\n", soil_raw, soil_mapped);

    xSemaphoreTake(xMutex, portMAX_DELAY);
    // Chỉ cập nhật nhiệt độ/độ ẩm nếu đọc thành công, tránh bị đè NaN
    if (!isnan(t)) g_temp = t;
    if (!isnan(h)) g_hum = h;
    g_soil = soil_mapped;
    
    // Copy ra biến local để publish
    float pub_t = g_temp;
    float pub_h = g_hum;
    float pub_s = g_soil;
    bool pump = g_pump_state;
    bool fan  = g_fan_state;
    bool light = g_light_state;
    OperationMode mode = g_mode;
    xSemaphoreGive(xMutex);

    if (client.connected()) {
      StaticJsonDocument<300> doc;
      doc["temperature"] = pub_t;
      doc["air_humidity"] = pub_h;
      doc["soil_moisture"] = pub_s;
      doc["pump"]  = pump;
      doc["fan"]   = fan;
      doc["light"] = light;
      doc["mode"]  = (mode == MODE_MANUAL) ? "manual" : "schedule";

      char buffer[300];
      serializeJson(doc, buffer);
      client.publish(TOPIC_DATA, buffer);
    }
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "relay_control.h"
#include "display_task.h"
#include "sensor_task.h"
#include "mqtt_handler.h"
#include "button_handler.h"

SemaphoreHandle_t xMutex;
float g_temp = 0.0;
float g_hum = 0.0;
float g_soil = 0.0;
bool  g_pump_state = false;
bool  g_fan_state = false;
bool  g_light_state = false;
OperationMode g_mode = MODE_MANUAL;
unsigned long pump_off_time = 0;
bool pump_timer_active = false;

WiFiClient espClient;
PubSubClient client(espClient);

static TaskHandle_t TaskSensorHandle;
static TaskHandle_t TaskDisplayHandle;
static TaskHandle_t TaskMQTTHandle;
static TaskHandle_t TaskButtonHandle;

void setup() {
  Serial.begin(115200);
  initRelays();
  initButtons();
  initDisplay();

  xMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(TaskSensor,  "TaskSensor",  4096, NULL, 1, &TaskSensorHandle,  1);
  xTaskCreatePinnedToCore(TaskDisplay, "TaskDisplay", 4096, NULL, 1, &TaskDisplayHandle, 1);
  xTaskCreatePinnedToCore(TaskButton,  "TaskButton",  4096, NULL, 1, &TaskButtonHandle,  1);
  xTaskCreatePinnedToCore(TaskMQTT,    "TaskMQTT",    8192, NULL, 2, &TaskMQTTHandle,    0);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}

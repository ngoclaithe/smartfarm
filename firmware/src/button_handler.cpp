#include "button_handler.h"
#include "config.h"
#include "globals.h"
#include "relay_control.h"
#include "mqtt_handler.h"

static unsigned long lastPress[4] = {0, 0, 0, 0};

void initButtons() {
  pinMode(BTN_PUMP_PIN,  INPUT_PULLUP);
  pinMode(BTN_FAN_PIN,   INPUT_PULLUP);
  pinMode(BTN_LIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_MODE_PIN,  INPUT_PULLUP);
}

static bool debounce(int index) {
  unsigned long now = millis();
  if (now - lastPress[index] > DEBOUNCE_MS) {
    lastPress[index] = now;
    return true;
  }
  return false;
}

void TaskButton(void* pvParameters) {
  (void)pvParameters;
  for(;;) {
    xSemaphoreTake(xMutex, portMAX_DELAY);
    OperationMode mode = g_mode;
    xSemaphoreGive(xMutex);

    if (mode == MODE_MANUAL) {
      if (digitalRead(BTN_PUMP_PIN) == LOW && debounce(0)) {
        togglePump();
        publishState();
      }
      if (digitalRead(BTN_FAN_PIN) == LOW && debounce(1)) {
        toggleFan();
        publishState();
      }
      if (digitalRead(BTN_LIGHT_PIN) == LOW && debounce(2)) {
        toggleLight();
        publishState();
      }
    }

    if (digitalRead(BTN_MODE_PIN) == LOW && debounce(3)) {
      xSemaphoreTake(xMutex, portMAX_DELAY);
      g_mode = (g_mode == MODE_MANUAL) ? MODE_SCHEDULE : MODE_MANUAL;
      xSemaphoreGive(xMutex);
      publishState();
    }
    
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

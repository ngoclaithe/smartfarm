#include "relay_control.h"
#include "config.h"
#include "globals.h"

void initRelays() {
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(LIGHT_PIN, LOW);
}

void handleAction(const char* action, int duration) {
  xSemaphoreTake(xMutex, portMAX_DELAY);

  if (strcmp(action, "pump_on") == 0) {
    g_pump_state = true;
    digitalWrite(PUMP_PIN, HIGH);
    if (duration > 0) {
      pump_off_time = millis() + (duration * 1000);
      pump_timer_active = true;
    } else {
      pump_timer_active = false;
    }
  } else if (strcmp(action, "pump_off") == 0) {
    g_pump_state = false;
    pump_timer_active = false;
    digitalWrite(PUMP_PIN, LOW);
  } else if (strcmp(action, "fan_on") == 0) {
    g_fan_state = true;
    digitalWrite(FAN_PIN, HIGH);
  } else if (strcmp(action, "fan_off") == 0) {
    g_fan_state = false;
    digitalWrite(FAN_PIN, LOW);
  } else if (strcmp(action, "light_on") == 0) {
    g_light_state = true;
    digitalWrite(LIGHT_PIN, HIGH);
  } else if (strcmp(action, "light_off") == 0) {
    g_light_state = false;
    digitalWrite(LIGHT_PIN, LOW);
  }

  xSemaphoreGive(xMutex);
}

void togglePump() {
  xSemaphoreTake(xMutex, portMAX_DELAY);
  g_pump_state = !g_pump_state;
  digitalWrite(PUMP_PIN, g_pump_state ? HIGH : LOW);
  if (!g_pump_state) {
    pump_timer_active = false;
  }
  xSemaphoreGive(xMutex);
}

void toggleFan() {
  xSemaphoreTake(xMutex, portMAX_DELAY);
  g_fan_state = !g_fan_state;
  digitalWrite(FAN_PIN, g_fan_state ? HIGH : LOW);
  xSemaphoreGive(xMutex);
}

void toggleLight() {
  xSemaphoreTake(xMutex, portMAX_DELAY);
  g_light_state = !g_light_state;
  digitalWrite(LIGHT_PIN, g_light_state ? HIGH : LOW);
  xSemaphoreGive(xMutex);
}

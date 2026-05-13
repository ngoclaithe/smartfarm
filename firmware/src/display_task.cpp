#include "display_task.h"
#include "config.h"
#include "globals.h"
#include <U8g2lib.h>
#include <Wire.h>

static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void initDisplay() {
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 30, "Dang khoi dong...");
  u8g2.sendBuffer();
}

void TaskDisplay(void* pvParameters) {
  (void)pvParameters;

  for (;;) {
    xSemaphoreTake(xMutex, portMAX_DELAY);
    float t = g_temp;
    float h = g_hum;
    float s = g_soil;
    bool p = g_pump_state;
    bool f = g_fan_state;
    bool l = g_light_state;
    OperationMode mode = g_mode;
    xSemaphoreGive(xMutex);

    char line[32];
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawStr(8, 12, "NONG TRAI T.MINH");
    u8g2.drawHLine(0, 14, 128);

    u8g2.setFont(u8g2_font_6x10_tr);

    snprintf(line, sizeof(line), "Nhiet do: %.1f *C", t);
    u8g2.drawStr(0, 26, line);

    snprintf(line, sizeof(line), "Do am KK: %.1f %%", h);
    u8g2.drawStr(0, 37, line);

    snprintf(line, sizeof(line), "Do am dat: %.0f %%", s);
    u8g2.drawStr(0, 48, line);

    snprintf(line, sizeof(line), "Bom:%s Quat:%s Den:%s",
             p ? "B" : "T", f ? "B" : "T", l ? "B" : "T");
    u8g2.drawStr(0, 59, line);

    const char* modeStr = (mode == MODE_MANUAL) ? "TAY" : "HEN";
    u8g2.drawStr(104, 59, modeStr);

    u8g2.sendBuffer();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

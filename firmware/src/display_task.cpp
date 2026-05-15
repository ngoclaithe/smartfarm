#include "display_task.h"
#include "config.h"
#include "globals.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// OLED 1.3 inch I2C - Chip SH1106 128x64
static Adafruit_SH1106G display(128, 64, &Wire, -1);

static int currentPage = 0;
static unsigned long lastPageSwitch = 0;
#define PAGE_COUNT     4
#define PAGE_INTERVAL  3000  // 3 giây đổi trang

// ===== Vẽ thanh Header =====
static void drawHeader(const char* title) {
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 0);
  display.print(title);
  display.drawLine(0, 17, 127, 17, SH110X_WHITE);
}

// ===== Hiển thị chế độ góc dưới phải =====
static void drawMode(OperationMode mode) {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  const char* modeStr = (mode == MODE_MANUAL) ? "[TAY]" : "[AUTO]";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(modeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(128 - w - 2, 56);
  display.print(modeStr);
}

// ===== Trang hiển thị 1 cảm biến TO =====
static void drawSensorPage(float value, const char* unit) {
  // Giá trị số TO căn giữa màn hình (size 3)
  char val[16];
  snprintf(val, sizeof(val), "%.1f%s", value, unit);
  display.setTextSize(3);
  display.setTextColor(SH110X_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(val, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 28);
  display.print(val);
}

// ===== Trang tổng quan (size 2, viết tắt gọn) =====
static void drawOverviewPage(float t, float h, float s) {
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);

  char line[16];

  snprintf(line, sizeof(line), "T:  %.1f*C", t);
  display.setCursor(4, 20);
  display.print(line);

  snprintf(line, sizeof(line), "KK: %.1f%%", h);
  display.setCursor(4, 37);
  display.print(line);

  snprintf(line, sizeof(line), "Dat:%.0f%%", s);
  display.setCursor(4, 54);
  display.print(line);
}

// ===== Init =====
void initDisplay() {
  delay(500);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  display.begin(0x3C, true);

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(2);
  display.setCursor(10, 15);
  display.print("SMART FARM");

  display.setTextSize(1);
  display.setCursor(18, 45);
  display.print("Dang khoi dong...");

  display.display();
}

// ===== Task hiển thị FreeRTOS =====
void TaskDisplay(void* pvParameters) {
  (void)pvParameters;
  lastPageSwitch = millis();

  for(;;) {
    if (millis() - lastPageSwitch >= PAGE_INTERVAL) {
      currentPage = (currentPage + 1) % PAGE_COUNT;
      lastPageSwitch = millis();
    }

    xSemaphoreTake(xMutex, portMAX_DELAY);
    float t = g_temp;
    float h = g_hum;
    float s = g_soil;
    OperationMode mode = g_mode;
    xSemaphoreGive(xMutex);

    display.clearDisplay();

    switch (currentPage) {
      case 0:
        drawHeader("NHIET DO");
        drawSensorPage(t, "*C");
        break;
      case 1:
        drawHeader("DO AM KK");
        drawSensorPage(h, "%");
        break;
      case 2:
        drawHeader("DO AM DAT");
        drawSensorPage(s, "%");
        break;
      case 3:
        drawHeader("TONG QUAN");
        drawOverviewPage(t, h, s);
        break;
    }

    drawMode(mode);
    display.display();
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

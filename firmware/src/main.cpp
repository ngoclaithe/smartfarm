#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// ==========================================
// THÔNG SỐ CẤU HÌNH (THAY ĐỔI CHO PHÙ HỢP)
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "192.168.1.100"; // Thay bằng IP của máy tính chạy server Flask
const int mqtt_port = 1883;

// Topic để tương thích với Backend Python hiện tại
const char* topic_data = "esp8266/data";
const char* topic_control = "esp8266/control";

// ==========================================
// CẤU HÌNH CHÂN (PINS) CHO ESP32
// ==========================================
#define DHTPIN 4          // Chân Data của DHT11
#define DHTTYPE DHT11     // Loại cảm biến DHT
#define SOIL_PIN 34       // Chân Analog đọc độ ẩm đất (Nên dùng ADC1)
#define PUMP_PIN 26       // Chân điều khiển Relay Bơm
#define FAN_PIN 27        // Chân điều khiển Relay Quạt
#define LIGHT_PIN 25      // Chân điều khiển Relay Đèn

// ==========================================
// CẤU HÌNH OLED
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// Mặc định ESP32: SDA = GPIO 21, SCL = GPIO 22
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==========================================
// KHỞI TẠO ĐỐI TƯỢNG
// ==========================================
WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);

// ==========================================
// BIẾN TOÀN CỤC & MUTEX (ĐỂ CHIA SẺ GIỮA CÁC TASK)
// ==========================================
SemaphoreHandle_t xMutex;
float g_temp = 0.0;
float g_hum = 0.0;
float g_soil = 0.0;
bool g_pump_state = false;
bool g_fan_state = false;
bool g_light_state = false;

// Hỗ trợ tự động tắt bơm dựa theo "duration_sec" từ API
unsigned long pump_off_time = 0;
bool pump_timer_active = false;

// Handles cho FreeRTOS
TaskHandle_t TaskSensorHandle;
TaskHandle_t TaskDisplayHandle;
TaskHandle_t TaskMQTTHandle;

// ==========================================
// HÀM XỬ LÝ WIFI & MQTT
// ==========================================
void setup_wifi() {
  delay(10);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Parse JSON từ MQTT Payload
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  const char* action = doc["action"];
  int duration = doc["duration_sec"] | 0;

  // Lấy Mutex trước khi thay đổi trạng thái phần cứng
  xSemaphoreTake(xMutex, portMAX_DELAY);
  
  if (strcmp(action, "pump_on") == 0) {
    g_pump_state = true;
    digitalWrite(PUMP_PIN, HIGH); // Sửa lại LOW nếu Relay của bạn kích ở mức thấp
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

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(topic_control);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

// ==========================================
// TASKS FREERTOS
// ==========================================

// Task 1: Đọc cảm biến và Gửi dữ liệu MQTT
void TaskSensor(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int soil_raw = analogRead(SOIL_PIN);
    
    // ESP32 ADC là 12-bit (0 - 4095). 
    // Thường 4095 là đất khô cong, 0 là đất ướt sũng. Bạn cần calib lại số này.
    int soil_mapped = map(soil_raw, 4095, 0, 0, 100); 
    if (soil_mapped < 0) soil_mapped = 0;
    if (soil_mapped > 100) soil_mapped = 100;
    
    if (isnan(t) || isnan(h)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      xSemaphoreTake(xMutex, portMAX_DELAY);
      g_temp = t;
      g_hum = h;
      g_soil = soil_mapped;
      xSemaphoreGive(xMutex);
      
      // Publish to MQTT
      if (client.connected()) {
        StaticJsonDocument<200> doc;
        doc["temperature"] = t;
        doc["air_humidity"] = h;
        doc["soil_moisture"] = soil_mapped;
        
        char buffer[200];
        serializeJson(doc, buffer);
        client.publish(topic_data, buffer);
      }
    }
    
    // Đọc cảm biến 5 giây 1 lần
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

// Task 2: Hiển thị OLED
void TaskDisplay(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    // Lấy dữ liệu an toàn
    xSemaphoreTake(xMutex, portMAX_DELAY);
    float t = g_temp;
    float h = g_hum;
    float s = g_soil;
    bool p = g_pump_state;
    bool f = g_fan_state;
    bool l = g_light_state;
    xSemaphoreGive(xMutex);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Header
    display.setCursor(0, 0);
    display.println("SMART FARM ESP32");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    
    // Sensors
    display.setCursor(0, 15);
    display.printf("Temp: %.1f C\n", t);
    display.setCursor(0, 25);
    display.printf("Hum:  %.1f %%\n", h);
    display.setCursor(0, 35);
    display.printf("Soil: %.1f %%\n", s);
    
    // Relays Status
    display.setCursor(0, 50);
    display.printf("BOM:%c QUA:%c DEN:%c", 
      p ? '1' : '0',
      f ? '1' : '0',
      l ? '1' : '0'
    );
    
    // Network status
    display.setCursor(110, 50);
    if(client.connected()) {
      display.print("OK");
    } else {
      display.print("NC");
    }

    display.display();
    
    // Cập nhật màn hình 1 giây 1 lần
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Task 3: Xử lý mạng (WiFi + MQTT)
void TaskMQTT(void *pvParameters) {
  (void) pvParameters;
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  for (;;) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop(); // Lắng nghe control từ server
    
    // Kiểm tra hẹn giờ tắt bơm
    xSemaphoreTake(xMutex, portMAX_DELAY);
    if (pump_timer_active && millis() > pump_off_time) {
      g_pump_state = false;
      pump_timer_active = false;
      digitalWrite(PUMP_PIN, LOW); // Tắt bơm
      Serial.println("Pump Auto OFF by Timer");
    }
    xSemaphoreGive(xMutex);
    
    vTaskDelay(20 / portTICK_PERIOD_MS); // Nghỉ 20ms nhường CPU
  }
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  // Khởi tạo các chân Relay
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(LIGHT_PIN, LOW);
  
  // Khởi tạo Cảm biến
  dht.begin();
  
  // Khởi tạo Màn hình OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,20);
  display.println("Booting System...");
  display.display();
  
  // Khởi tạo Mutex bảo vệ dữ liệu dùng chung
  xMutex = xSemaphoreCreateMutex();
  
  // ----------------------------------------------------
  // TẠO CÁC TASK FREERTOS (ESP32 có 2 Core: Core 0 và Core 1)
  // ----------------------------------------------------
  
  xTaskCreatePinnedToCore(
    TaskSensor,       // Tên hàm
    "TaskSensor",     // Tên task
    4096,             // Kích thước Stack
    NULL,             // Tham số
    1,                // Độ ưu tiên (1 = thấp)
    &TaskSensorHandle,// Handle
    1                 // Chạy trên Core 1
  );
  
  xTaskCreatePinnedToCore(
    TaskDisplay,
    "TaskDisplay",
    4096,
    NULL,
    1,
    &TaskDisplayHandle,
    1                 // Chạy trên Core 1
  );
  
  xTaskCreatePinnedToCore(
    TaskMQTT,
    "TaskMQTT",
    8192,
    NULL,
    2,                // Ưu tiên cao hơn để xử lý mạng realtime
    &TaskMQTTHandle,
    0                 // Chạy trên Core 0 (Chuyên xử lý mạng)
  );
}

void loop() {
  // Bỏ trống, toàn bộ xử lý đã được FreeRTOS quản lý
  vTaskDelay(portMAX_DELAY);
}

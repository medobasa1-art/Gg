#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ======================= NETWORK SETTINGS =======================
const char* ssid = "FallGuard_Sock";
const char* password = "password123"; // Must be at least 8 characters

AsyncWebServer server(80);
AsyncEventSource events("/events");

// ======================= HARDWARE SETTINGS ======================
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif
const int BUZZER_PIN = -1;

// ======================= ALGORITHM SETTINGS =====================
const uint16_t SAMPLE_HZ = 50;
const uint32_t SAMPLE_PERIOD_MS = 1000 / SAMPLE_HZ;
const float TUMBLE_JERK_G = 0.8;
const uint32_t TUMBLE_MIN_MS = 600;
const float STILL_ACCEL_DEV_G = 0.15;
const uint32_t STILL_MIN_MS = 1200;
const uint32_t ALERT_COOLDOWN_MS = 5000;

Adafruit_MPU6050 mpu;

enum State { WAITING_FOR_TUMBLE, TUMBLING, WAITING_FOR_STILL, ALERTED };
State currentState = WAITING_FOR_TUMBLE;

uint32_t lastSampleMs = 0;
uint32_t tumbleStartMs = 0;
uint32_t stillStartMs = 0;
uint32_t lastAlertMs = 0;
float prevAccelMagG = 1.0;
float currentJerk = 0.0;

)rawliteral";

// ======================= HARDWARE SETUP =========================
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // 1. Initialize MPU6050
  Wire.begin(21, 22);
  if (!mpu.begin()) {
    Serial.println("MPU6050 connection failed!");
    while (1) { delay(10); }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);

  // 2. Setup Wi-Fi Access Point
  Serial.println("Setting up Access Point...");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // 3. Setup Web Server & SSE
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    client->send("Connected", NULL, millis(), 1000);
  });
  server.addHandler(&events);
  server.begin();
}

// ======================= MAIN LOOP ==============================
void loop() {
  uint32_t currentMs = millis();

  // Non-blocking sampling loop
  if (currentMs - lastSampleMs >= SAMPLE_PERIOD_MS) {
    lastSampleMs = currentMs;

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // Calculate magnitude of acceleration (1G = 9.8 m/s^2)
    float accelMag = sqrt(sq(a.acceleration.x) + sq(a.acceleration.y) + sq(a.acceleration.z)) / 9.81;
    currentJerk = abs(accelMag - prevAccelMagG);
    prevAccelMagG = accelMag;

    // --- State Machine Logic ---
    switch (currentState) {
      case WAITING_FOR_TUMBLE:
        if (currentJerk > TUMBLE_JERK_G) {
          currentState = TUMBLING;
          tumbleStartMs = currentMs;
        }
        break;

      case TUMBLING:
        if (currentJerk < TUMBLE_JERK_G) {
          // If tumbling stopped, check if it tumbled long enough
          if (currentMs - tumbleStartMs > TUMBLE_MIN_MS) {
            currentState = WAITING_FOR_STILL;
            stillStartMs = currentMs;
          } else {
            currentState = WAITING_FOR_TUMBLE; // False alarm
          }
        }
        break;

      case WAITING_FOR_STILL:
        if (abs(accelMag - 1.0) < STILL_ACCEL_DEV_G) {
          if (currentMs - stillStartMs > STILL_MIN_MS) {
            currentState = ALERTED;
            lastAlertMs = currentMs;
          }
        } else {
          // Moved again, break stillness check
          currentState = WAITING_FOR_TUMBLE; 
        }
        break;

      case ALERTED:
        digitalWrite(LED_BUILTIN, HIGH);
        if (currentMs - lastAlertMs > ALERT_COOLDOWN_MS) {
          currentState = WAITING_FOR_TUMBLE; // Auto-reset after cooldown
          digitalWrite(LED_BUILTIN, LOW);
        }
        break;
    }

    // --- Send Data to Web Dashboard ---
    // Create a simple JSON string manually to avoid heavy JSON libraries
    char jsonStr[100];
    snprintf(jsonStr, sizeof(jsonStr), "{\"jerk\":%.2f, \"state\":%d}", currentJerk, currentState);
    events.send(jsonStr, "sensor_update", millis());
  }
}

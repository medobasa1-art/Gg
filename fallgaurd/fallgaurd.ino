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

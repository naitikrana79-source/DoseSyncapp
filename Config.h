#pragma once

// ── Compartment 1 RGB LED ──────────────────────────────────────────────────
#define PIN_RGB1_R  15
#define PIN_RGB1_G  2
#define PIN_RGB1_B  4

// ── Compartment 2 RGB LED ──────────────────────────────────────────────────
// RX2 = IO16, per pin-map
#define PIN_RGB2_R  32
#define PIN_RGB2_G  33
#define PIN_RGB2_B  14 

// ── Compartment 3 RGB LED ──────────────────────────────────────────────────
// TX2 = IO17, per pin-map
#define PIN_RGB3_R  5  // TX2
#define PIN_RGB3_G  18
#define PIN_RGB3_B  19

// ── Confirm buttons (INPUT_PULLUP, active LOW) ─────────────────────────────
#define PIN_BTN1    13   // compartment 1
#define PIN_BTN2    27 // compartment 2
#define PIN_BTN3     25  // compartment 3
#define PIN_BTN_SNOOZE 26// snooze

// ── Buzzer (NPN transistor base via 1kΩ on IO23) ──────────────────────────
#define PIN_BUZZER  23

// ── I2C ───────────────────────────────────────────────────────────────────
#define PIN_SDA     21
#define PIN_SCL     22
#define I2C_ADDR_RTC  0x68
#define I2C_ADDR_OLED 0x3C

// ── Alarm / timing constants ───────────────────────────────────────────────
#define NUM_COMPARTMENTS   3
#define MAX_SNOOZES        3
#define SNOOZE_MINUTES     5
#define MISSED_MINUTES    30
#define GREEN_HOLD_MS  10000   // 10 s steady green after confirm
#define FLASH_HALF_MS    250   // 2 Hz flash → 250 ms half-period
#define BEEP_ON_MS       500
#define BEEP_OFF_MS      500

// ── NVS namespace / keys ───────────────────────────────────────────────────
#define NVS_NS_ALARMS   "alarms"
#define NVS_NS_LOG      "medlog"
#define NVS_KEY_COUNT   "count"
#define NVS_KEY_LOG_POS "pos"
#define MAX_LOG_ENTRIES  100

// ── WiFi AP ───────────────────────────────────────────────────────────────
#define WIFI_SSID   "MedBox-AP"
#define WIFI_PASS   "medbox123"
#define WIFI_IP     "192.168.4.1"

// ── BLE UUIDs ─────────────────────────────────────────────────────────────
#define BLE_SVC_UUID   "12345678-1234-1234-1234-123456789abc"
#define BLE_CHAR_TIME  "12345678-1234-1234-1234-123456789ab1"
#define BLE_CHAR_ALARM "12345678-1234-1234-1234-123456789ab2"
#define BLE_CHAR_LOG   "12345678-1234-1234-1234-123456789ab3"
#define BLE_CHAR_NOTIF "12345678-1234-1234-1234-123456789ab4"
//Servo motor pin
#define PIN_SERVO 16

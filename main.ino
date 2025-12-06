/**
 * SMART VENTILATION SYSTEM - HIGH STABILITY VERSION
 * * Perbaikan Utama:
 * 1. Stack Size Task Blynk diperbesar (4096 -> 8192) untuk mencegah Crash memori.
 * 2. Proteksi "if (Blynk.connected())" pada SETIAP pengiriman data.
 * (Mencegah sistem hang jika internet putus).
 * 3. Prioritas Task disesuaikan agar WiFi tidak kalah dengan Motor.
 */

// --- 1. KONFIGURASI BLYNK ---
#define BLYNK_TEMPLATE_ID "TMPL63fagcvzD"
#define BLYNK_TEMPLATE_NAME "Blynk IoT Project"
#define BLYNK_AUTH_TOKEN "MK9bBTiOhOeM6moZJ5l0v_VYPM7IFiU-"
#define BLYNK_PRINT Serial

// --- 2. LIBRARY ---
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// --- 3. DEFINISI PIN ---
#define DHTPIN 15
#define DHTTYPE DHT11
#define SERVO_PIN 13
#define BUZZER_PIN 4

// Motor Driver L298N
#define L298N_IN1 27
#define L298N_IN2 25
#define L298N_ENA 14

// Tombol Manual
#define BTN_MANUAL_PIN 0  

// WiFi Credentials
char ssid[] = "Jakmau";
char pass[] = "jakmau220405";

// --- 4. GLOBAL VARIABLES ---
DHT dht(DHTPIN, DHTTYPE);
Servo windowServo;

QueueHandle_t sensorQueue;
SemaphoreHandle_t manualSemaphore;

// Setting Buzzer
const int buzzerFreq = 2000;
const int buzzerRes = 8;

struct SensorData {
  float temperature;
  float humidity;
};

// Status Global
volatile bool isManualMode = false; 
volatile bool manualState = false;  
float tempThreshold = 26.0;

// ==========================================
// 5. FUNGSI PENDUKUNG
// ==========================================

void beepSafe(int freq, int durationMs) {
  // Menggunakan ledcWriteTone (Support ESP32 v3.0+)
  ledcWriteTone(BUZZER_PIN, freq);
  vTaskDelay(durationMs / portTICK_PERIOD_MS);
  ledcWriteTone(BUZZER_PIN, 0); 
}

void IRAM_ATTR onManualButtonPress() {
  xSemaphoreGiveFromISR(manualSemaphore, NULL);
}

// ==========================================
// 6. DEFINISI TASKS
// ==========================================

// --- TASK A: SENSOR (Producer) ---
// Priority: 1 (Rendah, karena hanya baca sensor)
void taskReadSensor(void *pvParameters) {
  SensorData data;
  while (1) {
    data.temperature = dht.readTemperature();
    data.humidity = dht.readHumidity();

    if (!isnan(data.temperature) && !isnan(data.humidity)) {
      xQueueSend(sensorQueue, &data, 0);
    } else {
      Serial.println("[SENSOR] Error: Gagal baca DHT11");
    }
    // Baca setiap 2 detik
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// --- TASK B: PROCESSING & LOGIC (Consumer) ---
// Priority: 2 (Menengah, agar responsif terhadap tombol/sensor)
void taskProcessing(void *pvParameters) {
  SensorData receivedData;
  bool targetWindowOpen = false;
  bool targetFanOn = false;
  int targetFanSpeed = 0;
  bool lastWindowStatus = false; 

  while (1) {
    // 1. CEK TOMBOL FISIK (INTERRUPT)
    if (xSemaphoreTake(manualSemaphore, 0) == pdTRUE) {
      // Debounce: Cegah tombol terpencet 2x dalam waktu sangat singkat
      static unsigned long lastTime = 0;
      if (millis() - lastTime > 300) { 
        isManualMode = !isManualMode;
        if (isManualMode) manualState = true; // Default ON saat manual
        
        Serial.printf("\n[FISIK] Mode Berubah: %s\n", isManualMode ? "MANUAL" : "AUTO");
        beepSafe(2000, 100); 

        // Update Blynk V4 (HANYA JIKA CONNECTED)
        // Ini mencegah sistem 'hang' jika internet mati saat tombol ditekan
        if (Blynk.connected()) {
          Blynk.virtualWrite(V4, isManualMode ? 1 : 0);
        }
        lastTime = millis();
      }
    }

    // 2. LOGIKA UTAMA
    if (isManualMode) {
      // --- Manual Mode ---
      if (manualState) {
        targetWindowOpen = true; targetFanOn = true; targetFanSpeed = 255;
      } else {
        targetWindowOpen = false; targetFanOn = false; targetFanSpeed = 0;
      }
    } 
    else {
      // --- Auto Mode ---
      if (xQueueReceive(sensorQueue, &receivedData, 100 / portTICK_PERIOD_MS) == pdPASS) {
        
        // Aturan Jendela
        if (receivedData.temperature > tempThreshold || receivedData.humidity > 70.0) {
          targetWindowOpen = true;
        } else {
          targetWindowOpen = false;
        }

        // Aturan Kipas
        if (receivedData.temperature > tempThreshold) {
          targetFanOn = true;
          int tMin = (int)tempThreshold;
          int tMax = 35;
          if (tMax <= tMin) tMax = tMin + 1; // Safety Divide by Zero
          
          targetFanSpeed = map((int)receivedData.temperature, tMin, tMax, 150, 255);
          targetFanSpeed = constrain(targetFanSpeed, 0, 255);
        } else {
          targetFanOn = false; targetFanSpeed = 0;
        }

        // Kirim ke Blynk (PROTECTED)
        if (Blynk.connected()) {
          Blynk.virtualWrite(V0, receivedData.temperature);
          Blynk.virtualWrite(V1, receivedData.humidity);
        }
        
        // Serial Debug (Heartbeat agar tahu sistem hidup)
        Serial.printf("[AUTO] T:%.1f H:%.1f | Fan: %d\n", receivedData.temperature, receivedData.humidity, targetFanSpeed);
      }
    }

    // 3. EKSEKUSI HARDWARE
    // Servo & Buzzer
    if (targetWindowOpen != lastWindowStatus) {
      if (targetWindowOpen) {
        windowServo.write(90);
        Serial.println(">> ACTION: Jendela BUKA");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        beepSafe(1000, 500);
      } else {
        windowServo.write(0);
        Serial.println(">> ACTION: Jendela TUTUP");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        beepSafe(500, 200);
      }
      lastWindowStatus = targetWindowOpen;
    }

    // Motor L298N
    if (targetFanOn) {
      digitalWrite(L298N_IN1, HIGH); digitalWrite(L298N_IN2, LOW);
      analogWrite(L298N_ENA, targetFanSpeed);
    } else {
      digitalWrite(L298N_IN1, LOW); digitalWrite(L298N_IN2, LOW);
      analogWrite(L298N_ENA, 0);
    }

    // Update LED Status di Blynk (PROTECTED)
    if (Blynk.connected()) {
      Blynk.virtualWrite(V2, targetWindowOpen ? 1 : 0);
      Blynk.virtualWrite(V3, targetFanOn ? 1 : 0);
    }
    
    // Delay Penting agar task tidak rakus resource
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// --- TASK C: BLYNK CONNECTION MANAGER ---
// Priority: 2 (Setara dengan Processing agar koneksi tetap lancar)
// Stack Size: 8192 (DIPERBESAR AGAR TIDAK CRASH)
void taskBlynk(void *pvParameters) {
  
  // Setup WiFi Awal
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);

  while (1) {
    // 1. Cek WiFi
    if (WiFi.status() == WL_CONNECTED) {
      
      // 2. Cek Blynk
      if (!Blynk.connected()) {
        Serial.println("[BLYNK] Connecting...");
        // Coba connect dengan timeout 3 detik
        if (Blynk.connect(3000)) {
          Serial.println("[BLYNK] --- ONLINE ---");
        } else {
          Serial.println("[BLYNK] Offline. Retrying in 5s...");
          vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
      } else {
        // Jika sudah connect, jalankan
        Blynk.run();
      }
    
    } else {
      // Jika WiFi putus
      Serial.println("[WIFI] Searching...");
      // Tidak perlu WiFi.reconnect() manual terus menerus, 
      // ESP32 otomatis mencoba di background.
      vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
    
    // Delay wajib
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// 7. BLYNK CALLBACKS
// ==========================================
BLYNK_WRITE(V4) {
  isManualMode = (param.asInt() == 1);
  Serial.printf("[APP] Switch Mode: %s\n", isManualMode ? "MANUAL" : "AUTO");
  beepSafe(1500, 100);
}

BLYNK_WRITE(V5) {
  manualState = (param.asInt() == 1);
  Serial.printf("[APP] Manual Control: %s\n", manualState ? "OPEN" : "CLOSE");
}

// ==========================================
// 8. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000); 

  Serial.println("\n=== SYSTEM BOOTING (STABLE V3) ===");

  // 1. Create RTOS Objects
  sensorQueue = xQueueCreate(5, sizeof(SensorData));
  manualSemaphore = xSemaphoreCreateBinary();

  // 2. Init Hardware
  dht.begin();
  
  windowServo.setPeriodHertz(50);
  windowServo.attach(SERVO_PIN, 500, 2400);
  windowServo.write(0);

  ledcAttach(BUZZER_PIN, buzzerFreq, buzzerRes);

  pinMode(L298N_IN1, OUTPUT);
  pinMode(L298N_IN2, OUTPUT);
  pinMode(L298N_ENA, OUTPUT);
  pinMode(BTN_MANUAL_PIN, INPUT_PULLUP);

  // 3. Attach Interrupt
  attachInterrupt(digitalPinToInterrupt(BTN_MANUAL_PIN), onManualButtonPress, FALLING);

  // 4. Create Tasks (Perhatikan Stack Size & Priority)
  
  // Task Sensor (Core 1, Prio 1)
  xTaskCreatePinnedToCore(taskReadSensor, "ReadSensor", 2048, NULL, 1, NULL, 1);
  
  // Task Logic (Core 1, Prio 2)
  xTaskCreatePinnedToCore(taskProcessing, "Processing", 4096, NULL, 2, NULL, 1);
  
  // Task Blynk (Core 0, Prio 2, STACK BESAR 8192)
  xTaskCreatePinnedToCore(taskBlynk, "BlynkRun", 8192, NULL, 2, NULL, 0);

  Serial.println("=== TASKS STARTED ===");
}

void loop() {
  vTaskDelete(NULL);
}
/**
 * SMART FACTORY VENTILATION SYSTEM (ESP32)
 *
 * Deskripsi Singkat:
 * - Sistem membuka/menutup jendela menggunakan Servo dan mengontrol kipas (L298N)
 *   berdasarkan pembacaan suhu/kelembapan dari DHT11.
 * - Mode Otomatis akan bereaksi terhadap ambang suhu dan kelembapan.
 * - Mode Manual dapat diaktifkan lewat tombol fisik (ISR) maupun Blynk.
 * - Arsitektur menggunakan FreeRTOS: pemisahan tugas sensor, logika, dan koneksi Blynk
 *   untuk menjaga sistem responsif dan stabil.
 */

// --- KONFIGURASI BLYNK ---
// BLYNK_PRINT diarahkan ke Serial untuk log/debug koneksi.
#define BLYNK_TEMPLATE_ID "TMPL63fagcvzD"
#define BLYNK_TEMPLATE_NAME "Blynk IoT Project"
#define BLYNK_AUTH_TOKEN "MK9bBTiOhOeM6moZJ5l0v_VYPM7IFiU-"
#define BLYNK_PRINT Serial


#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#define DHTPIN 15
#define DHTTYPE DHT11
#define SERVO_PIN 13
#define BUZZER_PIN 4

// Motor Driver L298N
// IN1/IN2 menentukan arah; ENA (PWM) menentukan kecepatan kipas.
#define L298N_IN1 27
#define L298N_IN2 25
#define L298N_ENA 14

// Tombol Manual
// Tombol fisik untuk toggle mode Manual.
#define BTN_MANUAL_PIN 0  

// WiFi Credentials
// SSID dan password jaringan WiFi untuk koneksi Blynk.
char ssid[] = "SSID"; // GANTI SSID WIFI YANG SESUAI
char pass[] = "PASSWORD";

// --- GLOBAL VARIABLES ---
// Objek sensor, servo, antrian antar-task, dan semaphore untuk sinyal dari ISR.
// Struktur SensorData memuat suhu/kelembapan yang ditukar antar task.
DHT dht(DHTPIN, DHTTYPE);
Servo windowServo;

QueueHandle_t sensorQueue;
SemaphoreHandle_t manualSemaphore;

// Setting Buzzer
// Konfigurasi nada dan resolusi PWM untuk buzzer sebagai indikator aksi.
const int buzzerFreq = 2000;
const int buzzerRes = 8;

struct SensorData {
  float temperature;
  float humidity;
};

// Status Global
// Flag mode dan ambang suhu untuk logika otomatis.
// 26°C untuk testing
// Untuk real deployment, sesuaikan dengan kebutuhan ventilasi.
volatile bool isManualMode = false; 
volatile bool manualState = false;  
float tempThreshold = 26.0; 

void beepSafe(int freq, int durationMs) {
  // Menghasilkan bunyi singkat sebagai feedback status menggunakan ledcWriteTone.
  ledcWriteTone(BUZZER_PIN, freq);
  vTaskDelay(durationMs / portTICK_PERIOD_MS);
  ledcWriteTone(BUZZER_PIN, 0); 
}

void IRAM_ATTR onManualButtonPress() {
  // ISR tombol fisik: tidak mengubah state langsung, hanya memberi sinyal
  // ke task Processing melalui semaphore agar penanganan dilakukan di konteks task.
  xSemaphoreGiveFromISR(manualSemaphore, NULL);
}

// ==========================================
// DEFINISI TASKS
// ==========================================

// --- TASK A: SENSOR (Producer) ---
// Priority: 1 — tugas ringan untuk membaca sensor secara periodik
// Bertanggung jawab mendorong data ke queue tanpa blocking agar konsumen tetap responsif.
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
    // Interval sampling sensor: setiap 2 detik untuk stabilitas pembacaan.
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// --- TASK B: PROCESSING & LOGIC (Consumer) ---
// Priority: 2 — konsumen queue yang menjalankan logika kontrol utama.
// Menerapkan state mesin: menanggapi ISR (manual) dan data sensor (otomatis),
// lalu mengeksekusi aksi perangkat keras secara aman dan teratur.
void taskProcessing(void *pvParameters) {
  SensorData receivedData;
  bool targetWindowOpen = false;
  bool targetFanOn = false;
  int targetFanSpeed = 0;
  bool lastWindowStatus = false; 

  while (1) {
    // 1. CEK TOMBOL FISIK (INTERRUPT)
    // Mengambil semaphore dari ISR. Debounce dilakukan di sini agar penanganan tetap deterministik.
    if (xSemaphoreTake(manualSemaphore, 0) == pdTRUE) {
      // Debounce: Cegah tombol terpencet 2x dalam waktu sangat singkat
      static unsigned long lastTime = 0;
      if (millis() - lastTime > 300) { 
        isManualMode = !isManualMode;
        if (isManualMode) manualState = true; // Default ON saat manual
        
        Serial.printf("\n[FISIK] Mode Berubah: %s\n", isManualMode ? "MANUAL" : "AUTO");
        beepSafe(2000, 100); 

        // Sinkronisasi status ke dashboard Blynk (V4) hanya saat terhubung,
        // sehingga tidak terjadi blocking/hang ketika jaringan tidak tersedia.
        if (Blynk.connected()) {
          Blynk.virtualWrite(V4, isManualMode ? 1 : 0);
        }
        lastTime = millis();
      }
    }

    // 2. LOGIKA UTAMA
    // Mode Manual: memaksa jendela/kipas sesuai perintah pengguna.
    // Mode Otomatis: keputusan berdasarkan ambang suhu dan kelembapan.
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
        // Jendela buka jika suhu melebihi ambang atau kelembapan > 70%.
        if (receivedData.temperature > tempThreshold || receivedData.humidity > 70.0) {
          targetWindowOpen = true;
        } else {
          targetWindowOpen = false;
        }

        // Aturan Kipas
        // Kipas menyala bila suhu > ambang. Kecepatan dipetakan linier
        // dari kisaran suhu (tMin..tMax) ke PWM (150..255).
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

        // Kirim ke Blynk (dengan proteksi koneksi)
        if (Blynk.connected()) {
          Blynk.virtualWrite(V0, receivedData.temperature);
          Blynk.virtualWrite(V1, receivedData.humidity);
        }
        
        // Serial debug sebagai "heartbeat" agar terlihat status sistem saat otomatis.
        Serial.printf("[AUTO] T:%.1f H:%.1f | Fan: %d\n", receivedData.temperature, receivedData.humidity, targetFanSpeed);
      }
    }

    // 3. EKSEKUSI PERANGKAT KERAS
    // Servo & Buzzer: hanya berubah saat status jendela berganti untuk menghindari gerak berulang.
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
    // Mengatur arah dan PWM kecepatan kipas berdasarkan keputusan logika.
    if (targetFanOn) {
      digitalWrite(L298N_IN1, HIGH); digitalWrite(L298N_IN2, LOW);
      analogWrite(L298N_ENA, targetFanSpeed);
    } else {
      digitalWrite(L298N_IN1, LOW); digitalWrite(L298N_IN2, LOW);
      analogWrite(L298N_ENA, 0);
    }

    // Update indikator status di Blynk (dengan proteksi koneksi)
    if (Blynk.connected()) {
      Blynk.virtualWrite(V2, targetWindowOpen ? 1 : 0);
      Blynk.virtualWrite(V3, targetFanOn ? 1 : 0);
    }
    
    // Delay singkat untuk memberi waktu scheduler FreeRTOS dan menjaga responsivitas.
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// --- TASK C: BLYNK CONNECTION MANAGER ---
// Priority: 2 — menjaga siklus koneksi WiFi/Blynk tetap lancar dan non-blocking.
// Menangani kondisi terputus dan menjalankan loop Blynk saat online.
void taskBlynk(void *pvParameters) {
  
  // Inisialisasi WiFi dan konfigurasi Blynk.
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);

  while (1) {
    // 1. Cek status WiFi
    if (WiFi.status() == WL_CONNECTED) {
      
      // 2. Cek status Blynk
      if (!Blynk.connected()) {
        Serial.println("[BLYNK] Connecting...");
        // Mencoba menyambung dengan batas waktu agar tidak memblokir task lain.
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
      // Jika WiFi putus, beri jeda sebelum mencoba lagi (ESP32 mencoba di background).
      Serial.println("[WIFI] Searching...");
      // Tidak perlu WiFi.reconnect() manual terus menerus, 
      // ESP32 otomatis mencoba di background.
      vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
    
    // Jeda siklus koneksi agar task ini tetap ringan dan teratur.
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// 7. BLYNK CALLBACKS
// ==========================================
// Callback untuk menerima perintah dari aplikasi Blynk.
// V4: toggle mode; V5: aksi manual (OPEN/CLOSE).
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
// Inisialisasi perangkat keras, objek RTOS, interrupt tombol,
// dan pembuatan task dengan prioritas yang sesuai.
void setup() {
  Serial.begin(115200);
  delay(1000); 

  Serial.println("\n=== SYSTEM BOOTING (STABLE V3) ===");

  // 1. Membuat objek RTOS (queue & semaphore)
  sensorQueue = xQueueCreate(5, sizeof(SensorData));
  manualSemaphore = xSemaphoreCreateBinary();

  // 2. Inisialisasi perangkat keras: sensor DHT, servo jendela, buzzer, driver motor, tombol.
  dht.begin();
  
  windowServo.setPeriodHertz(50);
  windowServo.attach(SERVO_PIN, 500, 2400);
  windowServo.write(0);

  ledcAttach(BUZZER_PIN, buzzerFreq, buzzerRes);

  pinMode(L298N_IN1, OUTPUT);
  pinMode(L298N_IN2, OUTPUT);
  pinMode(L298N_ENA, OUTPUT);
  pinMode(BTN_MANUAL_PIN, INPUT_PULLUP);

  // 3. Pasang interrupt tombol (FALLING): dipicu saat ditekan (pull-up aktif).
  attachInterrupt(digitalPinToInterrupt(BTN_MANUAL_PIN), onManualButtonPress, FALLING);

  // 4. Membuat tasks (perhatikan ukuran stack & prioritas untuk keseimbangan sistem)
  
  // Task Sensor (Core 1, Prio 1)
  xTaskCreatePinnedToCore(taskReadSensor, "ReadSensor", 2048, NULL, 1, NULL, 1);
  
  // Task Logic (Core 1, Prio 2)
  xTaskCreatePinnedToCore(taskProcessing, "Processing", 4096, NULL, 2, NULL, 1);
  
  // Task Blynk (Core 0, Prio 2, STACK BESAR 8192)
  xTaskCreatePinnedToCore(taskBlynk, "BlynkRun", 8192, NULL, 2, NULL, 0);

  Serial.println("=== TASKS STARTED ===");
}

void loop() {
  // Tidak perlu logika di loop karena semua dikendalikan oleh FreeRTOS tasks.
  vTaskDelete(NULL);
}
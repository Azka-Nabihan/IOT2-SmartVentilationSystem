# Smart Factory Ventilation System

## Introduction
- Tujuan: Membangun sistem ventilasi otomatis untuk lingkungan pabrik menggunakan ESP32 yang membuka/menutup jendela dengan Servo dan mengontrol kipas berdasarkan suhu serta kelembapan.
- Ringkasan: Sistem membaca data sensor DHT11, menentukan status ventilasi, mengoperasikan jendela (Servo) dan kipas (melalui L298N), serta melaporkan status ke dashboard Blynk IoT. Keandalan ditingkatkan dengan FreeRTOS (tasks, queue, semaphore) dan operasi Blynk yang diproteksi.
- Fitur Utama:
  - Mode Otomatis/Manual dengan tombol fisik (ISR)
  - Kontrol jendela: terbuka saat terlalu panas atau terlalu lembab
  - Kecepatan kipas proporsional terhadap suhu di atas ambang batas
  - Manajemen konektivitas yang tangguh (WiFi + Blynk) dengan penjagaan anti-crash
  - Dashboard Blynk real-time untuk monitoring dan kontrol

## Implementation
- Perangkat Keras:
  - Papan: ESP32
  - Sensor: DHT11 (Suhu, Kelembapan)
  - Aktuator: Servo (jendela), kipas DC melalui driver L298N
  - Buzzer untuk feedback status
  - Button untuk ISR
  - Foto Rangkaian : 
  ![picture 2](https://i.imgur.com/tliDbgE.jpeg)  

- Pemetaan Pin (sesuai `main.ino`):
  - `DHTPIN = 15`, `DHTTYPE = DHT11`
  - `SERVO_PIN = 13`, `BUZZER_PIN = 4`
  - `L298N_IN1 = 27`, `L298N_IN2 = 25`, `L298N_ENA = 14`
  - `BTN_MANUAL_PIN = 0`
- Arsitektur Perangkat Lunak:
  - FreeRTOS Tasks
    - `taskReadSensor` (Producer): baca DHT11 tiap 2 detik, kirim ke `sensorQueue`
    - `taskProcessing` (Consumer): putuskan aksi jendela/kipas, update Blynk, eksekusi kontrol hardware
    - `taskBlynk`: kelola koneksi WiFi + Blynk dan jalankan loop Blynk dengan aman
  - Komunikasi Antar-Task
    - `QueueHandle_t sensorQueue`: antrian data sensor
    - `SemaphoreHandle_t manualSemaphore`: ISR → semaphore untuk toggle mode manual
  - ISR
    - `onManualButtonPress()`: toggle mode manual via semaphore, dengan debounce dan buzzer
  - Dashboard Blynk
    - Penulisan diproteksi dengan `if (Blynk.connected())` untuk hindari hang
    - Virtual pin: V0 (Suhu), V1 (Kelembapan), V2 (Status Jendela), V3 (Status Kipas), V4 (Mode Manual), V5 (Aksi Manual)![picture 1](https://i.imgur.com/SHSWLnS.png)  

  - Logika Kontrol
    - Otomatis: jendela terbuka jika `suhu > ambang` atau `kelembapan > 70%`; kipas aktif jika `suhu > ambang` dengan kecepatan 150–255 (dipetakan)
    - Manual: V5 untuk OPEN/CLOSE; memaksa status jendela/kipas sesuai perintah
  - Peningkatan Stabilitas
    - Stack task Blynk diperbesar (8192)
    - Penyetelan prioritas (Processing/Blynk prio 2, Sensor prio 1)
    - Penulisan Blynk terjaga dan retry koneksi

## Testing and Evaluation
- Uji Fungsional:
  - Baca Sensor: Pastikan nilai DHT stabil, terkirim ke queue dan dikonsumsi
  - Mode Otomatis: Verifikasi jendela terbuka saat suhu/kelembapan tinggi; kipas meningkat seiring suhu
  - Mode Manual: ISR men-toggle mode; V5 OPEN/CLOSE langsung tercermin pada hardware
  - Konektivitas: Simulasikan putus WiFi/Blynk; pastikan sistem tidak hang dan pulih
- Performa & Stabilitas:
  - Penjadwalan FreeRTOS menjaga responsivitas (delay loop processing 50 ms)
  - Tidak ada crash dengan stack Blynk besar dan operasi terjaga
- Verifikasi Dashboard:
  - Blynk menampilkan gauge Suhu/Kelembapan secara live
  - Tile status mencerminkan jendela dan kipas
  - Switch mode (V4/V5) berfungsi sesuai harapan
- Video Hasil Demo:
  

## Conclusion
Smart Factory Ventilation System ini secara andal mengotomasi ventilasi dan kontrol kipas berdasarkan kondisi lingkungan, menyediakan mode otomatis dan manual. Desain memanfaatkan FreeRTOS untuk konkurensi, queue/semaphore untuk signaling antar-task yang aman, serta Blynk untuk monitoring/kontrol IoT. Langkah stabilitas memastikan perilaku yang tangguh saat fluktuasi jaringan.

## References
- Dokumentasi ESP32 Arduino Core
- FreeRTOS pada ESP32 (tasks, queue, semaphore)
- Dokumen Blynk IoT (ESP32 + Virtual Pins)
- Library Sensor DHT (DHT11)
- Penggunaan Driver L298N dengan ESP32
  

---
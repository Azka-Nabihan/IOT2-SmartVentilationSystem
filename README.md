# Smart Factory Ventilation System

## Introduction
- Purpose: To build an automatic ventilation system for factory environments using ESP32 that opens/closes windows with a Servo and controls fans based on temperature and humidity.
- Summary: The system reads data from the DHT11 sensor, determines ventilation status, operates windows (Servo) and fans (via L298N), and reports status to the Blynk IoT dashboard. Reliability is enhanced with FreeRTOS (tasks, queue, semaphore) and protected Blynk operations.
- Key Features:
  - Automatic/Manual mode with physical buttons (ISR)
  - Window control: opens when too hot or too humid
  - Fan speed proportional to temperature above threshold
  - Robust connectivity management (WiFi + Blynk) with anti-crash safeguards
  - Real-time Blynk dashboard for monitoring and control

## Implementation
- Hardware:
  - Board: ESP32
  - Sensor: DHT11 (Temperature, Humidity)
  - Actuators: Servo (window), DC fan via L298N driver
  - Buzzer for status feedback
  - Button for ISR
  - Circuit Photo :
  ![picture 2](https://i.imgur.com/tliDbgE.jpeg)  

- Pin Mapping (based on `main.ino`):
  - `DHTPIN = 15`, `DHTTYPE = DHT11`
  - `SERVO_PIN = 13`, `BUZZER_PIN = 4`
  - `L298N_IN1 = 27`, `L298N_IN2 = 25`, `L298N_ENA = 14`
  - `BTN_MANUAL_PIN = 0`
- Software Architecture:
  - FreeRTOS Tasks
    - `taskReadSensor` (Producer): reads DHT11 every 2 seconds, sends to `sensorQueue`
    - `taskProcessing` (Consumer): decides window/fan actions, updates Blynk, executes hardware control
    - `taskBlynk`: manages WiFi + Blynk connection and runs the Blynk loop safely
  - Inter-task Communication
    - `QueueHandle_t sensorQueue`: queue for sensor data
    - `SemaphoreHandle_t manualSemaphore`: ISR → semaphore to toggle manual mode
  - ISR
    - `onManualButtonPress()`: toggles manual mode via semaphore, with debounce and buzzer
  - Blynk Dashboard
    - Written with `if (Blynk.connected())` to avoid hang
    - Virtual pins: V0 (Temperature), V1 (Humidity), V2 (Window Status), V3 (Fan Status), V4 (Manual Mode), V5 (Manual Action)![picture 1](https://i.imgur.com/SHSWLnS.png)  

  - Control Logic
    - Automatic: window opens if `suhu > threshold` or `kelembapan > 70%`; fan active if `suhu > threshold` with speed 150–255 (mapped)
    - Manual: V5 for OPEN/CLOSE; forces window/fan status according to command
  - Stability Enhancements
    - Stack size for Blynk task increased (8192)
    - Priority settings (Processing/Blynk prio 2, Sensor prio 1)
    - Blynk writes are protected and retry on connection

## Testing and Evaluation
- Functional Testing:
  - Serial Monitor Output : ![picture 0](https://i.imgur.com/FBuerv2.png)  

  - Sensor Reading: Ensure DHT values are stable, sent to queue, and consumed
  - Automatic Mode: Verify window opens when temperature/humidity is high; fan increases with temperature
  - Manual Mode: ISR toggles mode; V5 OPEN/CLOSE directly reflected on hardware
  - Connectivity: Simulate WiFi/Blynk drop; ensure system doesn't hang and recovers
- Performance & Stability:
  - FreeRTOS scheduling maintains responsiveness (delay loop processing 50 ms)
  - No crashes with large Blynk stack and safe operations
- Dashboard Verification:
  - Blynk shows live temperature/humidity gauges
  - Tile status reflects window/fan status
  - Switch mode (V4/V5) functions as expected
  - Dashboard Result : ![picture 1](https://i.imgur.com/ZeFqWqN.png)  

- Video Demo: https://www.youtube.com/shorts/bXwB0Q3jQdk
  

## Conclusion
The Smart Factory Ventilation System is reliable and automates ventilation and fan control based on environmental conditions, providing both automatic and manual modes. The design uses FreeRTOS for concurrency, queue/semaphore for safe inter-task signaling, and Blynk for IoT monitoring/control. Stability measures ensure robust behavior during network fluctuations.

## References
- ESP32 Arduino Core Documentation : 
- FreeRTOS on ESP32 (tasks, queue, semaphore) : 
  - “RTOS Fundamentals - FreeRTOSTM,” Freertos.org, 2024. https://www.freertos.org/Documentation/01-FreeRTOS-quick-start/01-Beginners-guide/01-RTOS-fundamentals
  - “Practical Sections | Digilab UI,” Digilabdte.com, 2025. https://learn.digilabdte.com/books/internet-of-things/page/practical-sections
  - “Queue | Digilab UI,” Digilabdte.com, 2025. https://learn.digilabdte.com/books/internet-of-things/page/queue
  - “5.6 Synchronization Me... | Digilab UI,” Digilabdte.com, 2025. https://learn.digilabdte.com/books/internet-of-things/page/56-synchronization-mechanisms-a-comparative-guide
- Blynk IoT Documentation (ESP32 + Virtual Pins) : 
  - “9.2 Blynk | Digilab UI,” Digilabdte.com, 2025. https://learn.digilabdte.com/books/internet-of-things/page/92-blynk
  - “9.3 Blynk Tutorial | Digilab UI,” Digilabdte.com, 2025. https://learn.digilabdte.com/books/internet-of-things/page/93-blynk-tutorial

- Library Sensor DHT (DHT11) : “ESP32 with DHT11/DHT22 Temperature and Humidity Sensor using Arduino IDE | Random Nerd Tutorials,” Random Nerd Tutorials, Apr. 25, 2019. https://randomnerdtutorials.com/esp32-dht11-dht22-temperature-humidity-sensor-arduino-ide/
- Using Driver L298N with ESP32 : Guidebook Technoskill 2.0 2025, “Guidebook Technoskill 2.0 2025,” Google Docs, 2025. https://docs.google.com/document/d/1lyyaZubaStaKYXbVGo3tT9aA75IE6OENZj3-3QUhlEc/edit?tab=t.0 (accessed Dec. 07, 2025).
  
  

## Use Cases

### 1. Automated Factory Environment Control
- **Scenario**: In a manufacturing facility, the Smart Factory Ventilation System automatically opens windows and adjusts fan speeds based on real-time temperature and humidity readings. This ensures optimal working conditions for employees and protects sensitive equipment from overheating.

### 2. Remote Monitoring and Control
- **Scenario**: Supervisors can monitor the factory environment remotely using the Blynk IoT dashboard. They receive alerts on their smartphones if temperature or humidity exceeds predefined thresholds, allowing them to take immediate action if necessary.

### 3. Energy Efficiency Management
- **Scenario**: The system operates in automatic mode, adjusting fan speeds based on real-time data, which reduces energy consumption compared to traditional systems that run at a constant speed regardless of conditions.

### 4. Manual Override for Specific Situations
- **Scenario**: In case of specific operational needs, operators can switch to manual mode using physical buttons or the Blynk dashboard, allowing them to control the ventilation system directly based on immediate requirements.

### 5. Data Logging and Analysis
- **Scenario**: The system logs historical temperature and humidity data, which can be analyzed later to identify trends and make informed decisions about facility management and improvements.
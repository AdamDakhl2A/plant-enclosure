/*
  Device: Arduino Uno R4 WiFi
  Role: BLE Peripheral (Server)
  Hardware: VEML7700 (I2C), DHT11 (Pin 2)
*/

#include <ArduinoBLE.h>
#include <Wire.h>
#include <Adafruit_VEML7700.h>
#include <DHT.h>

// --- Configuration ---
#define DHTPIN 2
#define DHTTYPE DHT11

// UUIDs must match your ESP32 Client code
const char* serviceUUID = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char* sensorCharUUID = "19B10001-E8F2-537E-4F6C-D104768A1214"; // Data: R4 -> ESP32
const char* messageCharUUID = "19B10002-E8F2-537E-4F6C-D104768A1214"; // Commands: ESP32 -> R4

// Initialize Sensors
DHT dht(DHTPIN, DHTTYPE);
Adafruit_VEML7700 veml = Adafruit_VEML7700();

// BLE Service & Characteristics
BLEService customService(serviceUUID);
// BLENotify allows the ESP32 to receive updates without constant polling
BLEStringCharacteristic sensorCharacteristic(sensorCharUUID, BLERead | BLENotify, 50);
BLEStringCharacteristic messageCharacteristic(messageCharUUID, BLEWrite | BLERead, 50);

unsigned long lastUpdate = 0;
const long updateInterval = 5000; // Send data every 5 seconds

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // 1. Initialize Sensors
  dht.begin();
  if (!veml.begin()) {
    Serial.println("Error: VEML7700 not found. Check I2C wiring.");
    while (1); 
  }
  
  // Configure Lux Sensor for indoor lighting
  veml.setGain(VEML7700_GAIN_1);
  veml.setIntegrationTime(VEML7700_IT_100MS);

  // 2. Initialize BLE
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }

  BLE.setLocalName("UnoR4_Sensor_Hub");
  BLE.setAdvertisedService(customService);
  
  customService.addCharacteristic(sensorCharacteristic);
  customService.addCharacteristic(messageCharacteristic);

  BLE.addService(customService);
  BLE.advertise();

  Serial.println("BLE Peripheral active. Awaiting ESP32 connection...");
}

void loop() {
  BLEDevice central = BLE.central();

  if (central) {
    Serial.print("Connected to: ");
    Serial.println(central.address());

    while (central.connected()) {
      unsigned long currentMillis = millis();

      // --- Task 1: Check for incoming messages from ESP32 ---
      if (messageCharacteristic.written()) {
        String received = messageCharacteristic.value();
        Serial.print("ESP32 Command: ");
        Serial.println(received);
      }

      // --- Task 2: Check for manual Serial input to send to ESP32 ---
      if (Serial.available()) {
        String manualMsg = Serial.readStringUntil('\n');
        manualMsg.trim();
        if (manualMsg.length() > 0) {
          sensorCharacteristic.writeValue(manualMsg);
          Serial.println("Manual Msg Sent to ESP32.");
        }
      }

      // --- Task 3: Send Sensor Data Packets ---
      if (currentMillis - lastUpdate >= updateInterval) {
        lastUpdate = currentMillis;
        sendSensorData();
      }
    }
    Serial.println("Disconnected from central.");
  }
}

void sendSensorData() {
  float h = dht.readHumidity();
  float t = dht.readTemperature(); // Celsius
  float lux = veml.readLux();

  // Check if readings are valid before sending
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  // Format: "T:22.5,H:45,L:350.2"
  String dataPacket = "T:" + String(t, 1) + ",H:" + String(h, 0) + ",L:" + String(lux, 1);
  
  Serial.print("Broadcasting Packet: ");
  Serial.println(dataPacket);

  // Push update to ESP32
  sensorCharacteristic.writeValue(dataPacket);
}
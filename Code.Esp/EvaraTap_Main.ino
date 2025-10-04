// EvaraTap ESP32 Flow Control System - CORRECTED VERSION
// MQTT Architecture exactly matching reference system with online/offline detection

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <HTTPClient.h>

// --- WiFi Configuration ---
const char* WIFI_SSID = "1234";
const char* WIFI_PASSWORD = "123456789";

// --- MQTT Configuration - EXACTLY matching reference architecture ---
const char* MQTT_BROKER = "io.adafruit.com";
const int MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "evaratap-esp32-01";
const char* AIO_USERNAME = "ADI08";
const char* AIO_KEY = "YOUR_ADAFRUIT_IO_KEY_HERE";

// MQTT Topics - CORRECTED to match dashboard expectations
const char* TOPIC_STATUS = "ADI08/feeds/esp32-status";
const char* TOPIC_DATA = "ADI08/feeds/esp32-data";
const char* TOPIC_COMMAND = "ADI08/feeds/esp32-commands";

// --- ThingSpeak Configuration ---
String apiKey = "87DIJFPJNZ6BOSS2";
const char* serverTS = "http://api.thingspeak.com/update";
String channelID = "2715858";
unsigned long uploadInterval = 15000;

// --- Pin Definitions ---
const int FLOW_SENSOR_PIN = 32;
const int RELAY_OPEN_PIN = 25;
const int RELAY_CLOSE_PIN = 26;

// --- Flow Sensor Configuration ---
const float PULSES_PER_LITER = 367.9;
const int PULSE_TIME = 100;

// --- EEPROM Configuration ---
#define EEPROM_SIZE 512
#define VOLUME_LIMIT_ADDR 0
#define AUTO_SHUTOFF_ADDR 4
#define TOTAL_VOLUME_ADDR 8
#define SYSTEM_MODE_ADDR 12

// --- Global Variables ---
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Flow sensor variables
volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseTimeISR = 0;
unsigned long previousPulseCount = 0;
float currentFlowRateLPS = 0.0;
float totalVolumeLiters = 0.0;

// Valve control variables
bool valveOpen = true;
bool valveStateChanged = false;
bool limitReached = false;
float volumeLimitLiters = 10.0;
float volumeAtValveOpen = 0.0;
float volumeSinceValveOpen = 0.0;
bool autoShutoffEnabled = true;

// System state variables
String systemMode = "AUTO";
bool manualOverride = false;
unsigned long currentOnTimeS = 0;
unsigned long valveOpenStartTime = 0;

// Non-blocking timers
unsigned long lastSensorRead = 0;
const long sensorReadInterval = 1000;

unsigned long lastDataPublish = 0;
const long dataPublishInterval = 10000;

unsigned long lastMqttReconnectAttempt = 0;
const long mqttReconnectInterval = 5000;

unsigned long lastThingSpeakUpload = 0;

// --- Function Prototypes ---
void setupWifi();
void callback(char* topic, byte* payload, unsigned int length);
boolean reconnectMqtt();
void publishSensorData();
void openValve();
void closeValve();
void resetSystem();
void saveSettings();
void loadSettings();

// -------------------- EEPROM FUNCTIONS --------------------
void saveSettings() {
  EEPROM.put(VOLUME_LIMIT_ADDR, volumeLimitLiters);
  EEPROM.put(AUTO_SHUTOFF_ADDR, autoShutoffEnabled);
  EEPROM.put(TOTAL_VOLUME_ADDR, totalVolumeLiters);
  
  int modeInt = (systemMode == "MANUAL") ? 1 : 0;
  EEPROM.put(SYSTEM_MODE_ADDR, modeInt);
  
  EEPROM.commit();
  Serial.printf("Settings saved to EEPROM (Mode: %s)\n", systemMode.c_str());
}

void loadSettings() {
  EEPROM.get(VOLUME_LIMIT_ADDR, volumeLimitLiters);
  EEPROM.get(AUTO_SHUTOFF_ADDR, autoShutoffEnabled);
  EEPROM.get(TOTAL_VOLUME_ADDR, totalVolumeLiters);
  
  int modeInt;
  EEPROM.get(SYSTEM_MODE_ADDR, modeInt);
  if (modeInt == 1) {
    systemMode = "MANUAL";
    manualOverride = true;
  } else {
    systemMode = "AUTO";
    manualOverride = false;
  }
  
  if (isnan(volumeLimitLiters) || volumeLimitLiters <= 0) {
    volumeLimitLiters = 10.0;
  }
  if (isnan(totalVolumeLiters) || totalVolumeLiters < 0) {
    totalVolumeLiters = 0.0;
    pulseCount = 0;
  } else {
    pulseCount = (unsigned long)(totalVolumeLiters * PULSES_PER_LITER);
  }
  
  Serial.println("Settings loaded from EEPROM");
}

// -------------------- ISR --------------------
void IRAM_ATTR flowSensorISR() {
  pulseCount++;
  lastPulseTimeISR = micros();
}

// -------------------- VALVE FUNCTIONS --------------------
void openValve() {
  if (!valveOpen && !limitReached) {
    digitalWrite(RELAY_OPEN_PIN, LOW);
    delay(PULSE_TIME);
    digitalWrite(RELAY_OPEN_PIN, HIGH);
    
    valveOpen = true;
    valveStateChanged = true;
    valveOpenStartTime = millis();
    volumeAtValveOpen = totalVolumeLiters;
    volumeSinceValveOpen = 0.0;
    
    Serial.println("Valve OPENED - water flowing");
  }
}

void closeValve() {
  if (valveOpen) {
    digitalWrite(RELAY_CLOSE_PIN, LOW);
    delay(PULSE_TIME);
    digitalWrite(RELAY_CLOSE_PIN, HIGH);
    
    valveOpen = false;
    valveStateChanged = true;
    valveOpenStartTime = 0;
    
    Serial.println("Valve CLOSED - water stopped");
  }
}

void resetSystem() {
  limitReached = false;
  totalVolumeLiters = 0;
  pulseCount = 0;
  previousPulseCount = 0;
  volumeAtValveOpen = 0;
  volumeSinceValveOpen = 0;
  
  Serial.println("System RESET - volume counter zeroed");
}

// -------------------- WiFi Setup Function ---
void setupWifi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi. Will retry.");
  }
}

// -------------------- MQTT Message Callback ---
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📩 Message arrived on topic: ");
  Serial.println(topic);

  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';

  Serial.print("📩 Payload: ");
  Serial.println(message);

  if (strcmp(topic, TOPIC_COMMAND) == 0) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.printf("❌ Command parsing failed: %s\n", error.c_str());
      return;
    }
    
    const char* command = doc["command"];
    if (!command) {
      Serial.println("⚠️ Command payload missing 'command' key.");
      return;
    }

    Serial.printf("🎯 Executing command: %s\n", command);

    if (strcmp(command, "on") == 0 || strcmp(command, "open") == 0) {
      systemMode = "MANUAL";
      manualOverride = true;
      limitReached = false;
      openValve();
      saveSettings();
      
    } else if (strcmp(command, "off") == 0 || strcmp(command, "close") == 0) {
      systemMode = "MANUAL";
      manualOverride = true;
      closeValve();
      saveSettings();
      
    } else if (strcmp(command, "reset") == 0) {
      resetSystem();
      systemMode = "AUTO";
      manualOverride = false;
      saveSettings();
      
    } else if (strcmp(command, "auto") == 0) {
      systemMode = "AUTO";
      manualOverride = false;
      saveSettings();
      
    } else if (strcmp(command, "manual") == 0) {
      systemMode = "MANUAL";
      manualOverride = true;
      limitReached = false;
      saveSettings();
    }
    
    // Send immediate status update after command
    delay(500);
    publishSensorData();
  }
}

// -------------------- MQTT Reconnect Function ---
boolean reconnectMqtt() {
  Serial.print("🔄 Attempting MQTT connection...");
  if (mqttClient.connect(MQTT_CLIENT_ID, AIO_USERNAME, AIO_KEY)) {
    Serial.println(" ✅ Connected!");
    
    // Publish status message (retained)
    mqttClient.publish(TOPIC_STATUS, "online", true);
    Serial.printf("📤 Published to: %s\n", TOPIC_STATUS);
    
    // Subscribe to command topic
    mqttClient.subscribe(TOPIC_COMMAND, 1);
    Serial.printf("📡 Subscribed to: %s\n", TOPIC_COMMAND);
    
  } else {
    Serial.print(" ❌ Failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" | Will retry...");
  }
  return mqttClient.connected();
}

// -------------------- Sensor Data Publishing - CORRECTED ---
void publishSensorData() {
  if (!mqttClient.connected()) {
    Serial.println("⚠️ MQTT not connected - skipping publish");
    return;
  }
  
  float flowRateLPM = currentFlowRateLPS * 60.0;
  unsigned long currentTime = millis();
  currentOnTimeS = valveOpen ? (currentTime - valveOpenStartTime) / 1000 : 0;
  
  // Create JSON payload - EXACTLY matching dashboard expectations
  StaticJsonDocument<512> doc;
  doc["device_heartbeat"] = true;                    // ✅ Critical for online detection
  doc["motor_state"] = valveOpen ? "ON" : "OFF";     // ✅ Valve state
  doc["manual_override"] = manualOverride;           // ✅ Mode indicator
  doc["current_on_time_s"] = currentOnTimeS;         // ✅ Session time
  doc["sensor_lockout"] = false;                     // ✅ System health
  
  // Flow-specific data
  doc["flow_rate_lpm"] = round(flowRateLPM * 10) / 10.0;
  doc["total_volume_liters"] = round(totalVolumeLiters * 10) / 10.0;
  doc["volume_since_open"] = round(volumeSinceValveOpen * 10) / 10.0;
  doc["volume_limit_liters"] = round(volumeLimitLiters * 10) / 10.0;
  doc["auto_shutoff_enabled"] = autoShutoffEnabled;
  doc["limit_reached"] = limitReached;
  doc["system_mode"] = systemMode;
  
  // System info
  doc["uptime_seconds"] = millis() / 1000;
  doc["wifi_rssi"] = WiFi.RSSI();

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);

  Serial.printf("📤 Publishing to: %s\n", TOPIC_DATA);
  Serial.printf("📤 Data: %s\n", jsonBuffer);

  bool published = mqttClient.publish(TOPIC_DATA, jsonBuffer, false);
  if (published) {
    Serial.println("✅ Data published successfully!");
  } else {
    Serial.println("❌ Publish failed!");
  }
}

// -------------------- ThingSpeak Upload ---
void uploadToThingSpeak() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(serverTS) + "?api_key=" + apiKey +
                 "&field1=" + String(currentFlowRateLPS * 60.0, 2) +
                 "&field2=" + String(totalVolumeLiters, 3) +
                 "&field3=" + String(valveOpen ? 1 : 0) +
                 "&field4=" + String(volumeLimitLiters, 1);

    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.println("📈 ThingSpeak update OK");
    } else {
      Serial.println("❌ ThingSpeak update failed");
    }
    http.end();
  }
}

// -------------------- SETUP ---
void setup() {
  Serial.begin(115200);
  Serial.println("\n🌊 EvaraTap Flow Control System Starting...");
  Serial.println("📋 MQTT Topics:");
  Serial.printf("   Status: %s\n", TOPIC_STATUS);
  Serial.printf("   Data: %s\n", TOPIC_DATA);
  Serial.printf("   Commands: %s\n", TOPIC_COMMAND);

  EEPROM.begin(EEPROM_SIZE);
  loadSettings();

  pinMode(RELAY_OPEN_PIN, OUTPUT);
  pinMode(RELAY_CLOSE_PIN, OUTPUT);
  digitalWrite(RELAY_OPEN_PIN, HIGH);
  digitalWrite(RELAY_CLOSE_PIN, HIGH);

  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR, RISING);

  setupWifi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(callback);
  mqttClient.setBufferSize(1024);  // Increase buffer for large JSON payloads

  if (!limitReached) {
    openValve();
  }

  lastSensorRead = millis();
  lastDataPublish = millis();
  lastThingSpeakUpload = millis();
  
  Serial.println("🚀 EvaraTap system ready!");
}

// -------------------- MAIN LOOP ---
void loop() {
  unsigned long now = millis();

  // 1. Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi disconnected. Reconnecting...");
    setupWifi();
    return;
  }

  // 2. Maintain MQTT connection (non-blocking)
  if (!mqttClient.connected()) {
    if (now - lastMqttReconnectAttempt > mqttReconnectInterval) {
      lastMqttReconnectAttempt = now;
      if (reconnectMqtt()) {
        lastMqttReconnectAttempt = 0;
      }
    }
  } else {
    mqttClient.loop();
  }

  // 3. Perform periodic sensor reading
  if (now - lastSensorRead >= sensorReadInterval) {
    detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));
    unsigned long currentPulses = pulseCount;
    unsigned long pulsesThisInterval = currentPulses - previousPulseCount;

    currentFlowRateLPS = (float)pulsesThisInterval / PULSES_PER_LITER / (sensorReadInterval / 1000.0);
    previousPulseCount = currentPulses;
    totalVolumeLiters = (float)currentPulses / PULSES_PER_LITER;
    
    if (valveOpen) {
      volumeSinceValveOpen = totalVolumeLiters - volumeAtValveOpen;
      
      if (systemMode == "AUTO" && autoShutoffEnabled && volumeSinceValveOpen >= volumeLimitLiters && !limitReached) {
        limitReached = true;
        Serial.println("⚠️ Volume limit reached - closing valve");
        closeValve();
      }
    }

    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR, RISING);
    lastSensorRead = now;
  }

  // 4. Publish sensor data to MQTT
  if (now - lastDataPublish >= dataPublishInterval) {
    if (mqttClient.connected()) {
      publishSensorData();
    }
    lastDataPublish = now;
  }

  // 5. Upload to ThingSpeak
  if (now - lastThingSpeakUpload >= uploadInterval) {
    uploadToThingSpeak();
    lastThingSpeakUpload = now;
  }
}
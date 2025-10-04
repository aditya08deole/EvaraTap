#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// -------------------- USER SETTINGS --------------------
#define FLOW_SENSOR_PIN   32
const int RELAY_OPEN_PIN  = 25;
const int RELAY_CLOSE_PIN = 26;

// --- REPLACE WITH YOUR CREDENTIALS ---
const char* ssid        = "YOUR_WIFI_SSID";
const char* password    = "YOUR_WIFI_PASSWORD";
const char* AIO_SERVER  = "io.adafruit.com";
const int AIO_SERVERPORT = 1883;
const char* AIO_USERNAME = "YOUR_ADAFRUIT_USERNAME";
const char* AIO_KEY      = "YOUR_ADAFRUIT_KEY";
// ---------------------------------------

String VALVE_CONTROL_TOPIC       = String(AIO_USERNAME) + "/feeds/valve-control";
String ESP_DATA_TOPIC            = String(AIO_USERNAME) + "/feeds/esp-data";
String DASHBOARD_HEARTBEAT_TOPIC = String(AIO_USERNAME) + "/feeds/dashboard-heartbeat";

const float PULSES_PER_LITER     = 367.9;
const int PULSE_TIME             = 100;

#define EEPROM_SIZE           512
#define VOLUME_LIMIT_ADDR     0
#define AUTO_SHUTOFF_ADDR     4
#define TOTAL_VOLUME_ADDR     8

const unsigned long HEARTBEAT_TIMEOUT    = 30000; // 30 seconds
const unsigned long DATA_SEND_INTERVAL = 3000;  // 3 seconds
const unsigned long READ_INTERVAL_MS   = 1000;  // 1 second

// -------------------- GLOBAL VARIABLES --------------------
WiFiClient client;
PubSubClient mqtt(client);

volatile unsigned long pulseCount = 0;
unsigned long previousPulseCount = 0;
float currentFlowRateLPS = 0.0;
float totalVolumeLiters = 0.0;

bool valveOpen = false;
bool limitReached = false;
float volumeLimitLiters = 100.0;
float volumeAtValveOpen = 0.0;
float volumeSinceValveOpen = 0.0;
bool autoShutoffEnabled = true;
bool emergencyShutoff = false;

unsigned long lastReadTime = 0;
unsigned long lastDataSend = 0;
unsigned long lastHeartbeat = 0;
bool dashboardConnected = false;

String systemMode = "AUTO"; // AUTO or MANUAL

// -------------------- EEPROM FUNCTIONS --------------------
void saveSettings() {
  EEPROM.put(VOLUME_LIMIT_ADDR, volumeLimitLiters);
  EEPROM.put(AUTO_SHUTOFF_ADDR, autoShutoffEnabled);
  EEPROM.put(TOTAL_VOLUME_ADDR, totalVolumeLiters);
  EEPROM.commit();
  Serial.println("Settings saved to EEPROM");
}

void loadSettings() {
  EEPROM.get(VOLUME_LIMIT_ADDR, volumeLimitLiters);
  EEPROM.get(AUTO_SHUTOFF_ADDR, autoShutoffEnabled);
  EEPROM.get(TOTAL_VOLUME_ADDR, totalVolumeLiters);
  
  if (isnan(volumeLimitLiters) || volumeLimitLiters <= 0) {
    volumeLimitLiters = 100.0;
  }
  if (isnan(totalVolumeLiters) || totalVolumeLiters < 0) {
    totalVolumeLiters = 0.0;
    pulseCount = 0;
  } else {
    pulseCount = (unsigned long)(totalVolumeLiters * PULSES_PER_LITER);
  }
  
  Serial.println("Settings loaded from EEPROM");
  Serial.printf("Volume limit: %.1f, Auto shutoff: %s, Total volume: %.2f\n", 
                volumeLimitLiters, autoShutoffEnabled ? "ON" : "OFF", totalVolumeLiters);
}

// -------------------- CORE FUNCTIONS --------------------
void IRAM_ATTR flowSensorISR() {
  pulseCount++;
}

void closeValve() {
  if (valveOpen) {
    digitalWrite(RELAY_CLOSE_PIN, LOW);
    delay(PULSE_TIME);
    digitalWrite(RELAY_CLOSE_PIN, HIGH);
    valveOpen = false;
    Serial.println("✓ Valve CLOSED - Water stopped");
  }
}

void openValve() {
  if (!valveOpen && !limitReached && !emergencyShutoff) {
    digitalWrite(RELAY_OPEN_PIN, LOW);
    delay(PULSE_TIME);
    digitalWrite(RELAY_OPEN_PIN, HIGH);
    valveOpen = true;
    volumeAtValveOpen = totalVolumeLiters;
    volumeSinceValveOpen = 0.0;
    Serial.println("✓ Valve OPENED - Water flowing");
  } else {
    Serial.println("⚠ Cannot open valve - System locked or already open");
  }
}

void emergencyCloseValve() {
  closeValve();
  emergencyShutoff = true;
  Serial.println("🚨 EMERGENCY SHUTDOWN - Dashboard disconnected");
}

void resetSystem() {
  limitReached = false;
  emergencyShutoff = false;
  totalVolumeLiters = 0;
  pulseCount = 0;
  previousPulseCount = 0;
  volumeAtValveOpen = 0;
  volumeSinceValveOpen = 0;
  saveSettings(); // Save the reset volume
  Serial.println("🔄 System RESET - All counters zeroed");
}

// -------------------- MQTT FUNCTIONS --------------------
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  Serial.printf("📩 Message on topic %s: %s\n", topic, message.c_str());

  if (String(topic) == VALVE_CONTROL_TOPIC) {
    DynamicJsonDocument doc(200);
    deserializeJson(doc, message);
    String command = doc["command"];

    if (command == "open") { systemMode = "MANUAL"; openValve(); } 
    else if (command == "close") { systemMode = "MANUAL"; closeValve(); } 
    else if (command == "reset") { systemMode = "AUTO"; resetSystem(); } 
    else if (command == "auto") { systemMode = "AUTO"; emergencyShutoff = false; Serial.println("🔄 Switched to AUTO mode"); } 
    else if (command == "set_config") {
      if (doc.containsKey("volume_limit")) { volumeLimitLiters = doc["volume_limit"]; }
      if (doc.containsKey("auto_shutoff")) { autoShutoffEnabled = doc["auto_shutoff"]; }
      saveSettings();
      Serial.println("⚙️ Settings updated from dashboard");
    }
  } else if (String(topic) == DASHBOARD_HEARTBEAT_TOPIC) {
    lastHeartbeat = millis();
    dashboardConnected = true;
    if (emergencyShutoff && systemMode == "AUTO") {
      emergencyShutoff = false;
      Serial.println("💚 Dashboard reconnected - Emergency shutdown cleared");
    }
  }
}

void sendSensorData() {
  if (!mqtt.connected()) return;
  DynamicJsonDocument doc(512);
  doc["device_heartbeat"] = true;
  doc["valve_state"] = valveOpen ? "ON" : "OFF";
  doc["system_mode"] = systemMode;
  doc["dashboard_connected"] = dashboardConnected;
  doc["emergency_shutoff"] = emergencyShutoff;
  doc["flow_rate_lpm"] = currentFlowRateLPS * 60.0;
  doc["total_volume_l"] = totalVolumeLiters;
  doc["volume_since_open"] = volumeSinceValveOpen;
  doc["volume_limit"] = volumeLimitLiters;
  doc["auto_shutoff_enabled"] = autoShutoffEnabled;
  doc["limit_reached"] = limitReached;
  
  String jsonString;
  serializeJson(doc, jsonString);
  mqtt.publish(ESP_DATA_TOPIC.c_str(), jsonString.c_str());
}

void MQTT_connect() {
  if (mqtt.connected()) return;
  Serial.print("🔗 Connecting to MQTT... ");
  while (!mqtt.connected()) {
    if (mqtt.connect("ESP32_EvaraTap", AIO_USERNAME, AIO_KEY)) {
      Serial.println("✓ MQTT Connected!");
      mqtt.subscribe(VALVE_CONTROL_TOPIC.c_str());
      mqtt.subscribe(DASHBOARD_HEARTBEAT_TOPIC.c_str());
      Serial.println("📡 Subscribed to control and heartbeat topics.");
    } else {
      Serial.printf("❌ Failed, rc=%d. Retrying in 5 seconds...\n", mqtt.state());
      delay(5000);
    }
  }
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n🌊 EvaraTap Flow Control System Starting...");

  EEPROM.begin(EEPROM_SIZE);
  loadSettings();

  pinMode(RELAY_OPEN_PIN, OUTPUT);
  pinMode(RELAY_CLOSE_PIN, OUTPUT);
  digitalWrite(RELAY_OPEN_PIN, HIGH);
  digitalWrite(RELAY_CLOSE_PIN, HIGH);

  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR, RISING);

  WiFi.begin(ssid, password);
  Serial.print("📶 Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\n✓ WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

  mqtt.setServer(AIO_SERVER, AIO_SERVERPORT);
  mqtt.setCallback(onMqttMessage);
  
  lastReadTime = millis();
  lastDataSend = millis();
  lastHeartbeat = millis();

  Serial.println("🚀 EvaraTap system ready!");
  Serial.println("=====================================");
}

// -------------------- MAIN LOOP --------------------
void loop() {
  unsigned long now = millis();
  MQTT_connect();
  mqtt.loop();

  if (now - lastReadTime >= READ_INTERVAL_MS) {
    detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));
    unsigned long currentPulses = pulseCount;
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR, RISING);
    
    unsigned long pulsesThisInterval = currentPulses - previousPulseCount;
    currentFlowRateLPS = (float)pulsesThisInterval / PULSES_PER_LITER / (READ_INTERVAL_MS / 1000.0);
    previousPulseCount = currentPulses;
    totalVolumeLiters = (float)currentPulses / PULSES_PER_LITER;

    if (valveOpen) {
      volumeSinceValveOpen = totalVolumeLiters - volumeAtValveOpen;
      if (systemMode == "AUTO" && autoShutoffEnabled && volumeSinceValveOpen >= volumeLimitLiters && !limitReached) {
        limitReached = true;
        closeValve();
        Serial.println("⚠ Volume limit reached - Auto closing valve");
      }
    }
    
    // Save total volume periodically (e.g., every 0.1L)
    if (abs(fmod(totalVolumeLiters, 0.1)) < 0.01) {
      EEPROM.put(TOTAL_VOLUME_ADDR, totalVolumeLiters);
      EEPROM.commit();
    }
    lastReadTime = now;
  }

  if (now - lastDataSend >= DATA_SEND_INTERVAL) {
    sendSensorData();
    lastDataSend = now;
  }

  if (now - lastHeartbeat > HEARTBEAT_TIMEOUT) {
    if (dashboardConnected) {
      dashboardConnected = false;
      Serial.println("💔 Dashboard connection lost");
      if (systemMode == "AUTO" && valveOpen && !emergencyShutoff) {
        emergencyCloseValve();
      }
    }
  }
  delay(10);
}


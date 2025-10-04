#include <WiFi.h>#include <WiFi.h>

#include <PubSubClient.h>#include <PubSubClient.h>

#include <ArduinoJson.h>#include <ArduinoJson.h>

#include <EEPROM.h>#include <EEPROM.h>



// -------------------- USER SETTINGS --------------------// -------------------- USER SETTINGS --------------------

#define FLOW_SENSOR_PIN 32#define FLOW_SENSOR_PIN 32



// Valve control pins// Valve control pins

const int RELAY_OPEN_PIN = 25;   // Controls relay for OPENINGconst int RELAY_OPEN_PIN = 25;   // Controls relay for OPENING

const int RELAY_CLOSE_PIN = 26;  // Controls relay for CLOSINGconst int RELAY_CLOSE_PIN = 26;  // Controls relay for CLOSING



// WiFi credentials - REPLACE WITH YOUR CREDENTIALS// WiFi credentials - REPLACE WITH YOUR CREDENTIALS

const char* ssid = "YOUR_WIFI_SSID";const char* ssid = "YOUR_WIFI_SSID";

const char* password = "YOUR_WIFI_PASSWORD";const char* password = "YOUR_WIFI_PASSWORD";



// Adafruit IO MQTT settings - REPLACE WITH YOUR CREDENTIALS// Adafruit IO MQTT settings - REPLACE WITH YOUR CREDENTIALS

const char* AIO_SERVER = "io.adafruit.com";const char* AIO_SERVER = "io.adafruit.com";

const int AIO_SERVERPORT = 1883;const int AIO_SERVERPORT = 1883;

const char* AIO_USERNAME = "YOUR_ADAFRUIT_USERNAME";const char* AIO_USERNAME = "YOUR_ADAFRUIT_USERNAME";

const char* AIO_KEY = "YOUR_ADAFRUIT_KEY";const char* AIO_KEY = "YOUR_ADAFRUIT_KEY";



// MQTT Topics// MQTT Topics

String VALVE_CONTROL_TOPIC = String(AIO_USERNAME) + "/feeds/valve-control";String VALVE_CONTROL_TOPIC = String(AIO_USERNAME) + "/feeds/valve-control";

String ESP_DATA_TOPIC = String(AIO_USERNAME) + "/feeds/esp-data";String ESP_DATA_TOPIC = String(AIO_USERNAME) + "/feeds/esp-data";

String DASHBOARD_HEARTBEAT_TOPIC = String(AIO_USERNAME) + "/feeds/dashboard-heartbeat";String DASHBOARD_HEARTBEAT_TOPIC = String(AIO_USERNAME) + "/feeds/dashboard-heartbeat";



// Flow sensor calibration// Flow sensor calibration

const float PULSES_PER_LITER = 367.9;const float PULSES_PER_LITER = 367.9;



// Valve pulse duration// Valve pulse duration

const int PULSE_TIME = 100;const int PULSE_TIME = 100;



// EEPROM settings// EEPROM settings

#define EEPROM_SIZE 512#define EEPROM_SIZE 512

#define VOLUME_LIMIT_ADDR 0#define VOLUME_LIMIT_ADDR 0

#define AUTO_SHUTOFF_ADDR 4#define AUTO_SHUTOFF_ADDR 4

#define TOTAL_VOLUME_ADDR 8#define TOTAL_VOLUME_ADDR 8



// Timing constants// Timing constants

const unsigned long HEARTBEAT_TIMEOUT = 30000; // 30 secondsconst unsigned long HEARTBEAT_TIMEOUT = 30000; // 30 seconds

const unsigned long DATA_SEND_INTERVAL = 3000;  // 3 secondsconst unsigned long DATA_SEND_INTERVAL = 3000;  // 3 seconds

const unsigned long READ_INTERVAL_MS = 1000;    // 1 secondconst unsigned long READ_INTERVAL_MS = 1000;    // 1 second



// -------------------- VARIABLES --------------------// -------------------- VARIABLES --------------------

WiFiClient client;WiFiClient client;

PubSubClient mqtt(client);PubSubClient mqtt(client);



// Flow sensor variables// Flow sensor variables

volatile unsigned long pulseCount = 0;volatile unsigned long pulseCount = 0;

volatile unsigned long lastPulseTimeISR = 0;volatile unsigned long lastPulseTimeISR = 0;

unsigned long previousPulseCount = 0;unsigned long previousPulseCount = 0;

float currentFlowRateLPS = 0.0;float currentFlowRateLPS = 0.0;

float totalVolumeLiters = 0.0;float totalVolumeLiters = 0.0;



// Valve state and volume tracking// Valve state and volume tracking

bool valveOpen = false;bool valveOpen = false;

bool valveStateChanged = false;bool valveStateChanged = false;

bool limitReached = false;bool limitReached = false;

float volumeLimitLiters = 10.0;float volumeLimitLiters = 10.0;

float volumeAtValveOpen = 0.0;float volumeAtValveOpen = 0.0;

float volumeSinceValveOpen = 0.0;float volumeSinceValveOpen = 0.0;

bool autoShutoffEnabled = true;bool autoShutoffEnabled = true;

bool emergencyShutoff = false;bool emergencyShutoff = false;



// Timing variables// Timing variables

unsigned long lastReadTime = 0;unsigned long lastReadTime = 0;

unsigned long lastDataSend = 0;unsigned long lastDataSend = 0;

unsigned long lastHeartbeat = 0;unsigned long lastHeartbeat = 0;

bool dashboardConnected = false;bool dashboardConnected = false;



// System state// System state

String systemMode = "AUTO"; // AUTO or MANUALString systemMode = "AUTO"; // AUTO or MANUAL



// -------------------- EEPROM FUNCTIONS --------------------// -------------------- EEPROM FUNCTIONS --------------------

void saveSettings() {void saveSettings() {

  EEPROM.put(VOLUME_LIMIT_ADDR, volumeLimitLiters);  EEPROM.put(VOLUME_LIMIT_ADDR, volumeLimitLiters);

  EEPROM.put(AUTO_SHUTOFF_ADDR, autoShutoffEnabled);  EEPROM.put(AUTO_SHUTOFF_ADDR, autoShutoffEnabled);

  EEPROM.put(TOTAL_VOLUME_ADDR, totalVolumeLiters);  EEPROM.put(TOTAL_VOLUME_ADDR, totalVolumeLiters);

  EEPROM.commit();  EEPROM.commit();

  Serial.println("Settings saved to EEPROM");  Serial.println("Settings saved to EEPROM");

}}



void loadSettings() {void loadSettings() {

  EEPROM.get(VOLUME_LIMIT_ADDR, volumeLimitLiters);  EEPROM.get(VOLUME_LIMIT_ADDR, volumeLimitLiters);

  EEPROM.get(AUTO_SHUTOFF_ADDR, autoShutoffEnabled);  EEPROM.get(AUTO_SHUTOFF_ADDR, autoShutoffEnabled);

  EEPROM.get(TOTAL_VOLUME_ADDR, totalVolumeLiters);  EEPROM.get(TOTAL_VOLUME_ADDR, totalVolumeLiters);

    

  // Default values if EEPROM is empty  // Default values if EEPROM is empty

  if (isnan(volumeLimitLiters) || volumeLimitLiters <= 0) {  if (isnan(volumeLimitLiters) || volumeLimitLiters <= 0) {

    volumeLimitLiters = 10.0;    volumeLimitLiters = 10.0;

  }  }

  if (isnan(totalVolumeLiters) || totalVolumeLiters < 0) {  if (isnan(totalVolumeLiters) || totalVolumeLiters < 0) {

    totalVolumeLiters = 0.0;    totalVolumeLiters = 0.0;

    pulseCount = 0;    pulseCount = 0;

  } else {  } else {

    pulseCount = (unsigned long)(totalVolumeLiters * PULSES_PER_LITER);    pulseCount = (unsigned long)(totalVolumeLiters * PULSES_PER_LITER);

  }  }

    

  Serial.println("Settings loaded from EEPROM");  Serial.println("Settings loaded from EEPROM");

  Serial.print("Volume limit: "); Serial.println(volumeLimitLiters);  Serial.print("Volume limit: "); Serial.println(volumeLimitLiters);

  Serial.print("Auto shutoff: "); Serial.println(autoShutoffEnabled ? "ON" : "OFF");  Serial.print("Auto shutoff: "); Serial.println(autoShutoffEnabled ? "ON" : "OFF");

  Serial.print("Total volume: "); Serial.println(totalVolumeLiters);  Serial.print("Total volume: "); Serial.println(totalVolumeLiters);

}}



// -------------------- FLOW SENSOR ISR --------------------// -------------------- FLOW SENSOR ISR --------------------

void IRAM_ATTR flowSensorISR() {void IRAM_ATTR flowSensorISR() {

  pulseCount++;  pulseCount++;

  lastPulseTimeISR = micros();  lastPulseTimeISR = micros();

}}



// -------------------- VALVE CONTROL FUNCTIONS --------------------// -------------------- VALVE CONTROL FUNCTIONS --------------------

void openValve() {void openValve() {

  if (!valveOpen && !limitReached && !emergencyShutoff) {  if (!valveOpen && !limitReached && !emergencyShutoff) {

    digitalWrite(RELAY_OPEN_PIN, LOW);    digitalWrite(RELAY_OPEN_PIN, LOW);

    delay(PULSE_TIME);    delay(PULSE_TIME);

    digitalWrite(RELAY_OPEN_PIN, HIGH);    digitalWrite(RELAY_OPEN_PIN, HIGH);

        

    valveOpen = true;    valveOpen = true;

    valveStateChanged = true;    valveStateChanged = true;

    volumeAtValveOpen = totalVolumeLiters;    volumeAtValveOpen = totalVolumeLiters;

    volumeSinceValveOpen = 0.0;    volumeSinceValveOpen = 0.0;

        

    Serial.println("✓ Valve OPENED - Water flowing");    Serial.println("✓ Valve OPENED - Water flowing");

  } else {  } else {

    Serial.println("⚠ Cannot open valve - System locked or already open");    Serial.println("⚠ Cannot open valve - System locked or already open");

  }  }

}}



void closeValve() {void closeValve() {

  if (valveOpen) {  if (valveOpen) {

    digitalWrite(RELAY_CLOSE_PIN, LOW);    digitalWrite(RELAY_CLOSE_PIN, LOW);

    delay(PULSE_TIME);    delay(PULSE_TIME);

    digitalWrite(RELAY_CLOSE_PIN, HIGH);    digitalWrite(RELAY_CLOSE_PIN, HIGH);

        

    valveOpen = false;    valveOpen = false;

    valveStateChanged = true;    valveStateChanged = true;

        

    Serial.println("✓ Valve CLOSED - Water stopped");    Serial.println("✓ Valve CLOSED - Water stopped");

  }  }

}}



void emergencyCloseValve() {void emergencyCloseValve() {

  closeValve();  closeValve();

  emergencyShutoff = true;  emergencyShutoff = true;

  Serial.println("🚨 EMERGENCY SHUTDOWN - Dashboard disconnected");  Serial.println("🚨 EMERGENCY SHUTDOWN - Dashboard disconnected");

}}



void resetSystem() {void resetSystem() {

  limitReached = false;  limitReached = false;

  emergencyShutoff = false;  emergencyShutoff = false;

  totalVolumeLiters = 0;  totalVolumeLiters = 0;

  pulseCount = 0;  pulseCount = 0;

  previousPulseCount = 0;  previousPulseCount = 0;

  volumeAtValveOpen = 0;  volumeAtValveOpen = 0;

  volumeSinceValveOpen = 0;  volumeSinceValveOpen = 0;

    

  saveSettings();  saveSettings();

  Serial.println("🔄 System RESET - All counters zeroed");  Serial.println("🔄 System RESET - All counters zeroed");

}}



// -------------------- MQTT FUNCTIONS --------------------// -------------------- MQTT FUNCTIONS --------------------

void MQTT_connect() {void MQTT_connect() {

  int8_t ret;  int8_t ret;

  if (mqtt.connected()) {  if (mqtt.connected()) {

    return;    return;

  }  }



  Serial.print("🔗 Connecting to MQTT... ");  Serial.print("🔗 Connecting to MQTT... ");

    

  while (!mqtt.connected()) {  while (!mqtt.connected()) {

    if (mqtt.connect("ESP32_EvaraTap", AIO_USERNAME, AIO_KEY)) {    if (mqtt.connect("ESP32_EvaraTap", AIO_USERNAME, AIO_KEY)) {

      Serial.println("✓ MQTT Connected!");      Serial.println("✓ MQTT Connected!");

            

      // Subscribe to valve control topic      // Subscribe to valve control topic

      mqtt.subscribe(VALVE_CONTROL_TOPIC.c_str());      mqtt.subscribe(VALVE_CONTROL_TOPIC.c_str());

      Serial.print("📡 Subscribed to: "); Serial.println(VALVE_CONTROL_TOPIC);      Serial.print("📡 Subscribed to: "); Serial.println(VALVE_CONTROL_TOPIC);

            

      // Subscribe to dashboard heartbeat      // Subscribe to dashboard heartbeat

      mqtt.subscribe(DASHBOARD_HEARTBEAT_TOPIC.c_str());      mqtt.subscribe(DASHBOARD_HEARTBEAT_TOPIC.c_str());

      Serial.print("💓 Subscribed to heartbeat: "); Serial.println(DASHBOARD_HEARTBEAT_TOPIC);      Serial.print("💓 Subscribed to heartbeat: "); Serial.println(DASHBOARD_HEARTBEAT_TOPIC);

            

    } else {    } else {

      Serial.print("❌ Failed, rc="); Serial.print(mqtt.state());      Serial.print("❌ Failed, rc="); Serial.print(mqtt.state());

      Serial.println(" Retrying in 5 seconds...");      Serial.println(" Retrying in 5 seconds...");

      delay(5000);      delay(5000);

    }    }

  }  }

}}



void onMqttMessage(char* topic, byte* payload, unsigned int length) {void onMqttMessage(char* topic, byte* payload, unsigned int length) {

  String message;  String message;

  for (int i = 0; i < length; i++) {  for (int i = 0; i < length; i++) {

    message += (char)payload[i];    message += (char)payload[i];

  }  }

    

  Serial.print("📩 Message on topic "); Serial.print(topic);   Serial.print("📩 Message on topic "); Serial.print(topic); 

  Serial.print(": "); Serial.println(message);  Serial.print(": "); Serial.println(message);



  // Handle valve control commands  // Handle valve control commands

  if (String(topic) == VALVE_CONTROL_TOPIC) {  if (String(topic) == VALVE_CONTROL_TOPIC) {

    DynamicJsonDocument doc(200);    DynamicJsonDocument doc(200);

    deserializeJson(doc, message);    deserializeJson(doc, message);

        

    String command = doc["command"];    String command = doc["command"];

        

    if (command == "open") {    if (command == "open") {

      systemMode = "MANUAL";      systemMode = "MANUAL";

      openValve();      openValve();

    } else if (command == "close") {    } else if (command == "close") {

      systemMode = "MANUAL";       systemMode = "MANUAL"; 

      closeValve();      closeValve();

    } else if (command == "reset") {    } else if (command == "reset") {

      systemMode = "AUTO";      systemMode = "AUTO";

      resetSystem();      resetSystem();

    } else if (command == "auto") {    } else if (command == "auto") {

      systemMode = "AUTO";      systemMode = "AUTO";

      emergencyShutoff = false;      emergencyShutoff = false;

      Serial.println("🔄 Switched to AUTO mode");      Serial.println("🔄 Switched to AUTO mode");

    } else if (command.startsWith("set_settings")) {    }

      // Handle settings commands from dashboard  }

      if (doc.containsKey("volume_limit")) {  

        volumeLimitLiters = doc["volume_limit"];  // Handle dashboard heartbeat

      }  if (String(topic) == DASHBOARD_HEARTBEAT_TOPIC) {

      if (doc.containsKey("auto_shutoff")) {    lastHeartbeat = millis();

        autoShutoffEnabled = doc["auto_shutoff"];    dashboardConnected = true;

      }    if (emergencyShutoff && systemMode == "AUTO") {

      saveSettings();      emergencyShutoff = false;

      Serial.println("⚙️ Settings updated from dashboard");      Serial.println("💚 Dashboard reconnected - Emergency shutdown cleared");

    }    }

  }  }

  }

  // Handle dashboard heartbeat

  if (String(topic) == DASHBOARD_HEARTBEAT_TOPIC) {void sendSensorData() {

    lastHeartbeat = millis();  if (!mqtt.connected()) return;

    dashboardConnected = true;  

    if (emergencyShutoff && systemMode == "AUTO") {  DynamicJsonDocument doc(512);

      emergencyShutoff = false;  

      Serial.println("💚 Dashboard reconnected - Emergency shutdown cleared");  // System information

    }  doc["device_heartbeat"] = true;

  }  doc["valve_state"] = valveOpen ? "OPEN" : "CLOSED";

}  doc["system_mode"] = systemMode;

  doc["dashboard_connected"] = dashboardConnected;

void sendSensorData() {  doc["emergency_shutoff"] = emergencyShutoff;

  if (!mqtt.connected()) return;  

    // Flow and volume data

  DynamicJsonDocument doc(512);  doc["flow_rate_lps"] = currentFlowRateLPS;

    doc["flow_rate_lpm"] = currentFlowRateLPS * 60.0;

  // System information  doc["total_volume_liters"] = totalVolumeLiters;

  doc["device_heartbeat"] = true;  doc["volume_since_open"] = volumeSinceValveOpen;

  doc["valve_state"] = valveOpen ? "OPEN" : "CLOSED";  

  doc["system_mode"] = systemMode;  // Settings and limits

  doc["dashboard_connected"] = dashboardConnected;  doc["volume_limit_liters"] = volumeLimitLiters;

  doc["emergency_shutoff"] = emergencyShutoff;  doc["auto_shutoff_enabled"] = autoShutoffEnabled;

    doc["limit_reached"] = limitReached;

  // Flow and volume data  

  doc["flow_rate_lps"] = currentFlowRateLPS;  // Pulse count for debugging

  doc["flow_rate_lpm"] = currentFlowRateLPS * 60.0;  doc["pulse_count"] = (unsigned long)pulseCount;

  doc["total_volume_liters"] = totalVolumeLiters;  

  doc["volume_since_open"] = volumeSinceValveOpen;  String jsonString;

    serializeJson(doc, jsonString);

  // Settings and limits  

  doc["volume_limit_liters"] = volumeLimitLiters;  mqtt.publish(ESP_DATA_TOPIC.c_str(), jsonString.c_str());

  doc["auto_shutoff_enabled"] = autoShutoffEnabled;}

  doc["limit_reached"] = limitReached;

  // -------------------- SETUP --------------------

  // Pulse count for debuggingvoid setup() {

  doc["pulse_count"] = (unsigned long)pulseCount;  Serial.begin(115200);

    Serial.println("\n🌊 EvaraTap Flow Control System Starting...");

  String jsonString;

  serializeJson(doc, jsonString);  // Initialize EEPROM

    EEPROM.begin(EEPROM_SIZE);

  mqtt.publish(ESP_DATA_TOPIC.c_str(), jsonString.c_str());  loadSettings();

}

  // Initialize valve control pins

// -------------------- SETUP --------------------  pinMode(RELAY_OPEN_PIN, OUTPUT);

void setup() {  pinMode(RELAY_CLOSE_PIN, OUTPUT);

  Serial.begin(115200);  

  Serial.println("\n🌊 EvaraTap Flow Control System Starting...");  // Ensure both relays are off initially (HIGH = off for most relay modules)

  digitalWrite(RELAY_OPEN_PIN, HIGH);

  // Initialize EEPROM  digitalWrite(RELAY_CLOSE_PIN, HIGH);

  EEPROM.begin(EEPROM_SIZE);  Serial.println("⚡ Valve control pins initialized");

  loadSettings();

  // Initialize flow sensor

  // Initialize valve control pins  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);

  pinMode(RELAY_OPEN_PIN, OUTPUT);  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR, RISING);

  pinMode(RELAY_CLOSE_PIN, OUTPUT);  Serial.println("🌊 Flow sensor initialized");

  

  // Ensure both relays are off initially (HIGH = off for most relay modules)  // Initialize WiFi

  digitalWrite(RELAY_OPEN_PIN, HIGH);  WiFi.begin(ssid, password);

  digitalWrite(RELAY_CLOSE_PIN, HIGH);  Serial.print("📶 Connecting to WiFi");

  Serial.println("⚡ Valve control pins initialized");  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

  // Initialize flow sensor    Serial.print(".");

  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);  }

  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR, RISING);  Serial.println();

  Serial.println("🌊 Flow sensor initialized");  Serial.print("✓ WiFi connected! IP: "); Serial.println(WiFi.localIP());



  // Initialize WiFi  // Initialize MQTT

  WiFi.begin(ssid, password);  mqtt.setServer(AIO_SERVER, AIO_SERVERPORT);

  Serial.print("📶 Connecting to WiFi");  mqtt.setCallback(onMqttMessage);

  while (WiFi.status() != WL_CONNECTED) {  

    delay(500);  // Initialize timing

    Serial.print(".");  lastReadTime = millis();

  }  lastDataSend = millis();

  Serial.println();  lastHeartbeat = millis();

  Serial.print("✓ WiFi connected! IP: "); Serial.println(WiFi.localIP());

  Serial.println("🚀 EvaraTap system ready!");

  // Initialize MQTT  Serial.println("=====================================");

  mqtt.setServer(AIO_SERVER, AIO_SERVERPORT);}

  mqtt.setCallback(onMqttMessage);

  // -------------------- MAIN LOOP --------------------

  // Initialize timingvoid loop() {

  lastReadTime = millis();  unsigned long now = millis();

  lastDataSend = millis();  

  lastHeartbeat = millis();  // Maintain MQTT connection

  MQTT_connect();

  Serial.println("🚀 EvaraTap system ready!");  mqtt.loop();

  Serial.println("=====================================");

}  // ---- Calculate flow data every 1 second ----

  if (now - lastReadTime >= READ_INTERVAL_MS) {

// -------------------- MAIN LOOP --------------------    // Temporarily disable interrupts for consistent reading

void loop() {    detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));

  unsigned long now = millis();    

      unsigned long currentPulses = pulseCount;

  // Maintain MQTT connection    unsigned long pulsesThisInterval = currentPulses - previousPulseCount;

  MQTT_connect();

  mqtt.loop();    // Calculate flow rate (L/s)

    currentFlowRateLPS = (float)pulsesThisInterval / PULSES_PER_LITER / (READ_INTERVAL_MS / 1000.0);

  // ---- Calculate flow data every 1 second ----    previousPulseCount = currentPulses;

  if (now - lastReadTime >= READ_INTERVAL_MS) {    totalVolumeLiters = (float)currentPulses / PULSES_PER_LITER;

    // Temporarily disable interrupts for consistent reading    

    detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));    // Update volume since valve was opened

        if (valveOpen) {

    unsigned long currentPulses = pulseCount;      volumeSinceValveOpen = totalVolumeLiters - volumeAtValveOpen;

    unsigned long pulsesThisInterval = currentPulses - previousPulseCount;      

      // Check if volume limit has been reached (only in AUTO mode)

    // Calculate flow rate (L/s)      if (systemMode == "AUTO" && autoShutoffEnabled && 

    currentFlowRateLPS = (float)pulsesThisInterval / PULSES_PER_LITER / (READ_INTERVAL_MS / 1000.0);          volumeSinceValveOpen >= volumeLimitLiters && !limitReached) {

    previousPulseCount = currentPulses;        limitReached = true;

    totalVolumeLiters = (float)currentPulses / PULSES_PER_LITER;        closeValve();

            Serial.println("⚠ Volume limit reached - Auto closing valve");

    // Update volume since valve was opened      }

    if (valveOpen) {    }

      volumeSinceValveOpen = totalVolumeLiters - volumeAtValveOpen;    

          // Re-enable interrupts

      // Check if volume limit has been reached (only in AUTO mode)    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR, RISING);

      if (systemMode == "AUTO" && autoShutoffEnabled && 

          volumeSinceValveOpen >= volumeLimitLiters && !limitReached) {    // Debug output

        limitReached = true;    Serial.printf("💧 Flow: %.1f L/min | Volume: %.2f L | Valve: %s | Mode: %s\n", 

        closeValve();                  currentFlowRateLPS * 60.0, totalVolumeLiters, 

        Serial.println("⚠ Volume limit reached - Auto closing valve");                  valveOpen ? "OPEN" : "CLOSED", systemMode.c_str());

      }

    }    // Save total volume periodically

        if ((int)(totalVolumeLiters * 10) % 10 == 0) { // Every 0.1L

    // Re-enable interrupts      EEPROM.put(TOTAL_VOLUME_ADDR, totalVolumeLiters);

    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR, RISING);      EEPROM.commit();

    }

    // Debug output

    Serial.printf("💧 Flow: %.1f L/min | Volume: %.2f L | Valve: %s | Mode: %s\n",     lastReadTime = now;

                  currentFlowRateLPS * 60.0, totalVolumeLiters,   }

                  valveOpen ? "OPEN" : "CLOSED", systemMode.c_str());

  // ---- Send data to dashboard every 3 seconds ----

    // Save total volume periodically  if (now - lastDataSend >= DATA_SEND_INTERVAL) {

    if ((int)(totalVolumeLiters * 10) % 10 == 0) { // Every 0.1L    sendSensorData();

      EEPROM.put(TOTAL_VOLUME_ADDR, totalVolumeLiters);    lastDataSend = now;

      EEPROM.commit();  }

    }

  // ---- Check dashboard heartbeat ----

    lastReadTime = now;  if (now - lastHeartbeat > HEARTBEAT_TIMEOUT) {

  }    if (dashboardConnected) {

      dashboardConnected = false;

  // ---- Send data to dashboard every 3 seconds ----      Serial.println("💔 Dashboard connection lost");

  if (now - lastDataSend >= DATA_SEND_INTERVAL) {      

    sendSensorData();      // Emergency shutdown if in AUTO mode and valve is open

    lastDataSend = now;      if (systemMode == "AUTO" && valveOpen && !emergencyShutoff) {

  }        emergencyCloseValve();

      }

  // ---- Check dashboard heartbeat ----    }

  if (now - lastHeartbeat > HEARTBEAT_TIMEOUT) {  }

    if (dashboardConnected) {

      dashboardConnected = false;  // Small delay to prevent watchdog reset

      Serial.println("💔 Dashboard connection lost");  delay(10);

      }
      // Emergency shutdown if in AUTO mode and valve is open
      if (systemMode == "AUTO" && valveOpen && !emergencyShutoff) {
        emergencyCloseValve();
      }
    }
  }

  // Small delay to prevent watchdog reset
  delay(10);
}
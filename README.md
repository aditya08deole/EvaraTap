# 🌊 EvaraTap Flow Control System

**Complete MQTT-enabled water flow monitoring and control system with real-time dashboard**

## 📋 System Overview

EvaraTap is an ESP32-based water flow control system that provides:
- **Real-time flow monitoring** with YF-S201 flow sensor
- **Automated valve control** with relay-controlled solenoid valve
- **MQTT communication** via Adafruit IO for remote monitoring
- **Web dashboard** with live gauges and controls
- **Auto-shutoff** when volume limits are reached
- **ThingSpeak integration** for data logging

## 🏗️ Architecture

```
ESP32 Device ←→ Adafruit IO ←→ Netlify Serverless ←→ Web Dashboard
     ↓
ThingSpeak (Data Logging)
```

### Hardware Components
- **ESP32 DevKit**: Main controller
- **YF-S201 Flow Sensor**: 367.9 pulses/liter
- **Solenoid Valve**: 12V with relay control
- **2-Channel Relay Module**: For valve open/close control

### Software Components
- **ESP32 Firmware**: Flow control + MQTT communication
- **Web Dashboard**: Real-time monitoring and control interface
- **MQTT Proxy**: Serverless function for secure API communication
- **Adafruit IO**: MQTT broker and feed management

## 🚀 Quick Start

### 1. Hardware Setup
```
ESP32 Connections:
├── Pin 32: Flow sensor signal (YF-S201)
├── Pin 25: Relay 1 (Valve OPEN)
├── Pin 26: Relay 2 (Valve CLOSE)
└── Power: 5V for relays, 3.3V for ESP32
```

### 2. ESP32 Configuration
1. **Upload:** `Code.Esp/EvaraTap_Main.ino`
2. **Configure WiFi:**
   ```cpp
   const char* WIFI_SSID = "YourWiFiName";
   const char* WIFI_PASSWORD = "YourWiFiPassword";
   ```
3. **Set Adafruit IO credentials:**
   ```cpp
   const char* AIO_USERNAME = "YourAdafruitUsername";
   const char* AIO_KEY = "YourAdafruitIOKey";
   ```

### 3. Web Dashboard Deployment
1. **Deploy to Netlify** (connects to this GitHub repo)
2. **Set Environment Variables:**
   ```
   ADAFRUIT_IO_USERNAME = YourAdafruitUsername
   ADAFRUIT_IO_KEY = YourAdafruitIOKey
   MQTT_BROKER_HOST = io.adafruit.com
   MQTT_BROKER_PORT = 1883
   ```
3. **Access dashboard** at your Netlify URL

## 📊 MQTT Topics & Feeds

The system automatically creates these Adafruit IO feeds:

| Feed Name | Purpose | Created By | Content |
|-----------|---------|------------|---------|
| `esp32-status` | Device online/offline | ESP32 | "online"/"offline" |
| `esp32-data` | Sensor data & heartbeat | ESP32 | JSON with all sensor values |
| `esp32-commands` | Control commands | Dashboard | JSON commands {"command": "on/off/reset"} |

### Data Structure (`esp32-data` feed):
```json
{
  "device_heartbeat": true,
  "motor_state": "ON",
  "manual_override": false,
  "current_on_time_s": 120,
  "sensor_lockout": false,
  "flow_rate_lpm": 5.2,
  "total_volume_liters": 12.3,
  "volume_since_open": 2.1,
  "volume_limit_liters": 10.0,
  "auto_shutoff_enabled": true,
  "limit_reached": false,
  "system_mode": "AUTO",
  "uptime_seconds": 3600,
  "wifi_rssi": -45
}
```

## 🎛️ Dashboard Features

### Status Indicators
- **🟢 ONLINE**: Device connected and responding
- **🔴 OFFLINE**: Device disconnected (>15 seconds no data)

### Real-time Gauges
- **Flow Rate**: Current flow in LPM (0-50 scale)
- **Total Volume**: Cumulative volume in liters
- **Session Volume**: Volume since valve opened

### Control Panel
- **Valve ON/OFF**: Manual valve control
- **AUTO Mode**: Automatic shutoff at volume limit
- **RESET**: Clear volume counters
- **Emergency Stop**: Immediate valve closure

## ⚙️ Configuration Options

### Volume Limits
- **Default**: 10.0 liters auto-shutoff
- **Range**: 1-100 liters
- **Stored**: EEPROM (persists after restart)

### Flow Sensor Calibration
- **Sensor**: YF-S201 Hall effect
- **Calibration**: 367.9 pulses per liter
- **Accuracy**: ±2% when calibrated

### MQTT Timing
- **Data Publishing**: Every 10 seconds
- **Heartbeat**: Included in every data message
- **Status Updates**: Immediate on valve state change
- **Reconnect**: 5-second intervals if disconnected

## 🔒 Security Features

- **Environment Variables**: Credentials stored securely on Netlify
- **No Hardcoded Secrets**: Web files contain no sensitive information
- **HTTPS**: All dashboard communication encrypted
- **MQTT Authentication**: Username/key authentication with Adafruit IO

## 📈 Data Logging

### ThingSpeak Integration
- **Channel ID**: Auto-configured
- **Fields**:
  - Field 1: Flow Rate (LPM)
  - Field 2: Total Volume (L)
  - Field 3: Valve State (1/0)
  - Field 4: Volume Limit (L)
- **Update Rate**: Every 15 seconds

### Local Storage (EEPROM)
- Volume limits and settings
- System mode (AUTO/MANUAL)
- Total volume counter
- Auto-shutoff preferences

## 🔧 Troubleshooting

### Dashboard Shows "OFFLINE"
1. **Check ESP32 Serial Monitor**
   ```
   ✅ Expected: "📤 Publishing to: ADI08/feeds/esp32-data"
   ❌ Problem: "⚠️ MQTT not connected - skipping publish"
   ```

2. **Verify Adafruit IO Feeds**
   - Should see: `esp32-data`, `esp32-status`, `esp32-commands`
   - Missing `esp32-data` = ESP32 not publishing successfully

3. **Check Netlify Environment Variables**
   - Verify all MQTT credentials are set
   - Redeploy if variables were recently added

### Flow Sensor Not Reading
- **Check connections**: Pin 32 to sensor signal
- **Verify power**: 5V to sensor VCC
- **Test with water**: Sensor needs actual flow to trigger
- **Serial output**: Should show pulse counts increasing

### Valve Not Responding
- **Relay connections**: Pins 25/26 to relay inputs
- **Power supply**: 12V for solenoid valve
- **Serial commands**: Check for valve open/close messages
- **Manual test**: Use dashboard buttons to test

## 📁 File Structure

```
evaratap/
├── index.html              # Main dashboard interface
├── netlify.toml            # Netlify deployment config  
├── package.json            # Project dependencies
├── .gitignore              # Git ignore rules
├── api/
│   └── mqtt-proxy.js       # Serverless MQTT bridge
├── Code.Esp/
│   └── EvaraTap_Main.ino   # ESP32 firmware (main)
└── README.md               # This documentation
```

## 🔄 System Operation Modes

### AUTO Mode (Default)
- Valve opens automatically on system start
- Monitors volume continuously  
- Auto-closes valve when limit reached
- Prevents overflow and water waste

### MANUAL Mode
- Full user control via dashboard
- Volume limits can be overridden
- Valve state controlled by buttons
- Useful for testing and maintenance

## 📞 Support & Development

### Monitoring Tools
- **ESP32 Serial**: Real-time system status
- **Adafruit IO Dashboard**: Feed activity and data
- **Browser Console**: Web dashboard debugging  
- **ThingSpeak Graphs**: Historical data analysis

### Common Parameters
- **WiFi Timeout**: 30 seconds
- **MQTT Reconnect**: 5 seconds
- **Data Freshness**: 15 seconds (dashboard)
- **Sensor Reading**: 1 second intervals
- **EEPROM Writes**: On setting changes only

---

**🌊 EvaraTap System - Smart Water Management Made Simple**
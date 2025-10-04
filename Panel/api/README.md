# EvaraTap MQTT Proxy API

This API acts as a proxy between the web dashboard and MQTT broker, allowing secure communication with the EvaraTap water flow control system.

## Setup

1. Install dependencies
   ```
   npm install
   ```

2. Update the MQTT credentials in `mqtt-proxy.js`:
   ```javascript
   const ADAFRUIT_IO_USERNAME = "YOUR_ADAFRUIT_IO_USERNAME";
   const ADAFRUIT_IO_KEY = "YOUR_ADAFRUIT_IO_KEY";
   ```

## API Endpoints

The MQTT proxy exposes the following endpoints:

### Send Heartbeat

```
POST /api/heartbeat
```

Sends a heartbeat signal to the ESP32 device to maintain connection status.

#### Response

```json
{
  "success": true,
  "message": "Heartbeat sent"
}
```

### Control Valve

```
POST /api/valve
```

Controls the water valve (open/close).

#### Request Body

```json
{
  "state": "ON" | "OFF"
}
```

#### Response

```json
{
  "success": true,
  "message": "Valve command sent: ON"
}
```

### Get System Status

```
GET /api/status
```

Retrieves the current system status from MQTT.

#### Response

```json
{
  "success": true,
  "status": {
    "systemStatus": "ONLINE",
    "valveStatus": "ON",
    "flowRate": 2.5
  }
}
```

## Integration with Dashboard

The dashboard makes API calls to these endpoints to communicate with the ESP32 device. The proxy handles all MQTT communication, ensuring reliable data exchange and proper connection handling.

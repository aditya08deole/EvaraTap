# EvaraTap MQTT Proxy API

This API acts as a proxy between the web dashboard and MQTT broker, allowing secure communication with the EvaraTap water flow control system.

## Setup

### Environment Variables (Required for Vercel)

Set these environment variables in your Vercel dashboard:

```bash
ADAFRUIT_IO_USERNAME=your_adafruit_username
ADAFRUIT_IO_KEY=your_adafruit_io_key
```

Optional (have defaults):
```bash
MQTT_BROKER_HOST=io.adafruit.com
MQTT_BROKER_PORT=1883
```

## API Endpoint

The API uses a single endpoint for all actions:

**`POST /api/mqtt-proxy`**

### Actions

To perform an action, send a POST request with a JSON body specifying the `action` and optional `payload`.

#### Get System Status

Retrieves the latest ESP32 data from Adafruit IO.

**Request:**
```json
{
  "action": "get_system_status"
}
```

**Response:**
```json
{
  "status": "success",
  "data": {
    "esp_data": {
      "device_heartbeat": true,
      "valve_state": "ON",
      "system_mode": "AUTO",
      "flow_rate_lpm": 2.5,
      "total_volume_liters": 45.2,
      "volume_since_open": 5.3,
      "volume_limit_liters": 100.0,
      "auto_shutoff_enabled": true,
      "limit_reached": false,
      "sensor_lockout": false,
      "manual_override": false,
      "current_open_time_s": 120
    },
    "server_timestamp": "2025-10-04T08:54:32.000Z"
  }
}
```

#### Send Valve Command

Sends control commands to the ESP32 device.

**Request:**
```json
{
  "action": "send_valve_command", 
  "payload": "open"
}
```

**Available Commands:**
- `"open"` - Opens the valve (switches to MANUAL mode)
- `"close"` - Closes the valve (switches to MANUAL mode)  
- `"reset"` - Resets volume counters (switches to AUTO mode)
- `"auto"` - Switches to AUTO mode
- `{"volume_limit": 50, "auto_shutoff": true}` - Updates settings

**Response:**
```json
{
  "status": "success",
  "details": "Valve command sent successfully."
}
```

## MQTT Topics Used

1. **`{username}/feeds/valve-control`** - Dashboard sends commands to ESP32
2. **`{username}/feeds/esp-data`** - ESP32 sends sensor data to dashboard

## Integration with Dashboard

The dashboard communicates with the ESP32 through this single-endpoint API. The ESP32 operates autonomously and does not depend on dashboard connectivity for core functionality.

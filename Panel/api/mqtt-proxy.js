// mqtt-proxy.js
// This serverless function acts as a proxy between the web dashboard and MQTT broker
// It handles MQTT communication for the EvaraTap system

const mqtt = require('mqtt');

// MQTT connection settings for Adafruit IO
const ADAFRUIT_IO_USERNAME = 'ADAFRUIT_USERNAME'; // Replace with your username
const ADAFRUIT_IO_KEY = 'ADAFRUIT_KEY';           // Replace with your AIO key
const ADAFRUIT_IO_URL = 'mqtts://io.adafruit.com';

// MQTT topics
const TOPIC_PREFIX = `${ADAFRUIT_IO_USERNAME}/feeds/`;
const HEARTBEAT_TOPIC = `${TOPIC_PREFIX}evaratap-heartbeat`;
const SYSTEM_STATUS_TOPIC = `${TOPIC_PREFIX}evaratap-status`;
const VALVE_COMMAND_TOPIC = `${TOPIC_PREFIX}evaratap-valve`;
const VALVE_STATUS_TOPIC = `${TOPIC_PREFIX}evaratap-valve-status`;
const FLOW_TOPIC = `${TOPIC_PREFIX}evaratap-flow`;

// Connection options
const options = {
  username: ADAFRUIT_IO_USERNAME,
  password: ADAFRUIT_IO_KEY,
  keepalive: 60,
  reconnectPeriod: 5000
};

// Create MQTT client
let client = null;

/**
 * Connect to MQTT broker if not already connected
 * @returns {Promise} - Resolves with MQTT client when connected
 */
function connectMQTT() {
  return new Promise((resolve, reject) => {
    if (client && client.connected) {
      return resolve(client);
    }
    
    try {
      client = mqtt.connect(ADAFRUIT_IO_URL, options);
      
      client.on('connect', () => {
        console.log('Connected to MQTT broker');
        resolve(client);
      });
      
      client.on('error', (err) => {
        console.error('MQTT connection error:', err);
        reject(err);
      });
    } catch (error) {
      console.error('Failed to connect to MQTT:', error);
      reject(error);
    }
  });
}

/**
 * Send heartbeat to ESP32
 * @returns {Promise} - Resolves when heartbeat is sent
 */
async function sendHeartbeat() {
  try {
    const mqttClient = await connectMQTT();
    
    // Prepare heartbeat message with current timestamp
    const heartbeat = {
      timestamp: Date.now(),
      source: 'dashboard'
    };
    
    // Publish heartbeat
    mqttClient.publish(HEARTBEAT_TOPIC, JSON.stringify(heartbeat));
    return { success: true, message: 'Heartbeat sent' };
  } catch (error) {
    console.error('Failed to send heartbeat:', error);
    return { success: false, error: error.message };
  }
}

/**
 * Control valve state or send other commands
 * @param {string} state - "ON", "OFF", "RESET", "LIMIT:X" or "AUTO:ON/OFF"
 * @returns {Promise} - Resolves when command is sent
 */
async function controlValve(state) {
  try {
    const mqttClient = await connectMQTT();
    
    // Get the command string
    const commandStr = state.toString().toUpperCase();
    
    // Handle different commands
    if (commandStr === "ON" || commandStr === "OFF") {
      // Basic valve control
      mqttClient.publish(VALVE_COMMAND_TOPIC, commandStr);
      return { success: true, message: `Valve command sent: ${commandStr}` };
    } 
    else if (commandStr === "RESET") {
      // System reset command
      mqttClient.publish(VALVE_COMMAND_TOPIC, "RESET");
      return { success: true, message: "Reset command sent" };
    }
    else if (commandStr.startsWith("LIMIT:")) {
      // Volume limit setting
      mqttClient.publish(VALVE_COMMAND_TOPIC, commandStr);
      return { success: true, message: `Volume limit command sent: ${commandStr}` };
    }
    else if (commandStr === "AUTO:ON" || commandStr === "AUTO:OFF") {
      // Auto shutoff setting
      mqttClient.publish(VALVE_COMMAND_TOPIC, commandStr);
      return { success: true, message: `Auto shutoff command sent: ${commandStr}` };
    }
    else {
      throw new Error('Invalid command format');
    }
    
  } catch (error) {
    console.error('Failed to control valve:', error);
    return { success: false, error: error.message };
  }
}

/**
 * Get system status by subscribing to status topics
 * @returns {Promise} - Resolves with system status
 */
async function getSystemStatus() {
  return new Promise(async (resolve, reject) => {
    try {
      const mqttClient = await connectMQTT();
      const timeout = setTimeout(() => {
        mqttClient.unsubscribe([SYSTEM_STATUS_TOPIC, VALVE_STATUS_TOPIC, FLOW_TOPIC]);
        resolve({ 
          success: false, 
          error: 'Timeout waiting for system status' 
        });
      }, 5000);
      
      const status = {
        systemStatus: null,
        valveStatus: null,
        flowRate: null,
        totalVolume: 0,
        volumeLimit: 10.0,
        autoShutoff: true
      };
      
      // Set a flag for message handling
      let messageHandler = null;
      
      // Count received messages to know when we have all data
      let receivedMessages = 0;
      
      mqttClient.subscribe([SYSTEM_STATUS_TOPIC, VALVE_STATUS_TOPIC, FLOW_TOPIC]);
      
      messageHandler = (topic, message) => {
        const messageStr = message.toString();
        
        if (topic === SYSTEM_STATUS_TOPIC) {
          status.systemStatus = messageStr;
          receivedMessages++;
        } else if (topic === VALVE_STATUS_TOPIC) {
          status.valveStatus = messageStr;
          
          // Try to parse additional info if present in the message
          try {
            // Extract total volume and limit if available
            // Format might be "ON:12.5:10.0:true" (status:volume:limit:autoShutoff)
            if (messageStr.includes(':')) {
              const parts = messageStr.split(':');
              if (parts.length >= 2) {
                status.totalVolume = parseFloat(parts[1]) || 0;
              }
              if (parts.length >= 3) {
                status.volumeLimit = parseFloat(parts[2]) || 10.0;
              }
              if (parts.length >= 4) {
                status.autoShutoff = parts[3] === 'true';
              }
              
              // Just keep the ON/OFF part for valve status
              status.valveStatus = parts[0];
            }
          } catch (e) {
            console.error('Error parsing valve status message:', e);
          }
          
          receivedMessages++;
        } else if (topic === FLOW_TOPIC) {
          status.flowRate = parseFloat(messageStr);
          receivedMessages++;
        }
        
        // If we have all three messages, resolve
        if (receivedMessages >= 3) {
          clearTimeout(timeout);
          mqttClient.unsubscribe([SYSTEM_STATUS_TOPIC, VALVE_STATUS_TOPIC, FLOW_TOPIC]);
          mqttClient.removeListener('message', messageHandler);
          resolve({ success: true, status });
        }
      };
      
      mqttClient.on('message', messageHandler);
      
      // Also send a heartbeat to ensure ESP is responsive
      await sendHeartbeat();
      
    } catch (error) {
      console.error('Failed to get system status:', error);
      reject({ success: false, error: error.message });
    }
  });
}

module.exports = {
  sendHeartbeat,
  controlValve,
  getSystemStatus
};
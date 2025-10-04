import mqtt from 'mqtt';

// Environment variables for MQTT connection
const MQTT_BROKER_HOST = process.env.MQTT_BROKER_HOST || 'io.adafruit.com';
const MQTT_BROKER_PORT = process.env.MQTT_BROKER_PORT || '1883';
const ADAFRUIT_IO_USERNAME = process.env.ADAFRUIT_IO_USERNAME;
const ADAFRUIT_IO_KEY = process.env.ADAFRUIT_IO_KEY;

// Define MQTT topics
const TOPIC_VALVE_CONTROL = `${ADAFRUIT_IO_USERNAME}/feeds/valve-control`;
const TOPIC_ESP_DATA = `${ADAFRUIT_IO_USERNAME}/feeds/esp-data`;

/**
 * Main handler for all incoming requests
 */
export default async function handler(request, response) {
    // Set CORS headers
    response.setHeader('Access-Control-Allow-Origin', '*');
    response.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    response.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (request.method === 'OPTIONS') {
        return response.status(200).end();
    }

    if (request.method !== 'POST') {
        return response.status(405).json({ 
            status: 'error', 
            details: 'Method Not Allowed. Use POST.' 
        });
    }

    // Verify MQTT credentials
    if (!ADAFRUIT_IO_USERNAME || !ADAFRUIT_IO_KEY) {
        return response.status(500).json({ 
            status: 'error', 
            details: 'MQTT credentials not configured on server.' 
        });
    }

    const { action, payload } = request.body;

    try {
        switch (action) {
            case 'send_valve_command':
                await handlePublishCommand(TOPIC_VALVE_CONTROL, payload);
                return response.status(200).json({ 
                    status: 'success', 
                    details: 'Valve command sent successfully.' 
                });

            case 'get_system_status':
                // Get system status
                const statusData = await getSystemStatus();
                return response.status(200).json({ 
                    status: 'success', 
                    data: statusData 
                });

            default:
                return response.status(400).json({ 
                    status: 'error', 
                    details: 'Invalid action specified.' 
                });
        }
    } catch (error) {
        console.error('[MQTT_PROXY_ERROR]', error.message);
        return response.status(500).json({ 
            status: 'error', 
            details: error.message 
        });
    }
}

/**
 * Publishes a valve control command to MQTT
 */
function handlePublishCommand(topic, command) {
    return new Promise((resolve, reject) => {
        const client = mqtt.connect(`mqtt://${MQTT_BROKER_HOST}:${MQTT_BROKER_PORT}`, {
            username: ADAFRUIT_IO_USERNAME,
            password: ADAFRUIT_IO_KEY,
            clientId: `evaratap_proxy_${Date.now()}`,
            reconnectPeriod: 0,
            connectTimeout: 10000,
        });

        const timeout = setTimeout(() => {
            client.end(true);
            reject(new Error('MQTT connection timeout'));
        }, 15000);

        client.on('connect', () => {
            clearTimeout(timeout);
            console.log(`✓ MQTT connected for command: ${typeof command === 'object' ? JSON.stringify(command) : command}`);
            
            let messagePayload;
            if (typeof command === 'object') {
                // Handle settings commands
                messagePayload = JSON.stringify({
                    command: 'set_settings',
                    ...command,
                    timestamp: new Date().toISOString()
                });
            } else {
                // Handle simple string commands
                messagePayload = JSON.stringify({ 
                    command: command,
                    timestamp: new Date().toISOString()
                });
            }
            
            client.publish(topic, messagePayload, { retain: false }, (err) => {
                client.end();
                if (err) {
                    console.error('❌ Publish error:', err);
                    reject(new Error('Failed to publish valve command.'));
                } else {
                    console.log(`📡 Command published: ${command}`);
                    resolve();
                }
            });
        });

        client.on('error', (err) => {
            clearTimeout(timeout);
            client.end();
            console.error('❌ MQTT connection error:', err);
            reject(new Error(`MQTT connection failed: ${err.message}`));
        });
    });
}



/**
 * Fetches the latest ESP32 data from Adafruit IO
 */
async function getFeedData(feedKey) {
    const apiUrl = `https://io.adafruit.com/api/v2/${ADAFRUIT_IO_USERNAME}/feeds/${feedKey}/data/last`;
    
    try {
        const apiResponse = await fetch(apiUrl, {
            headers: { 'X-AIO-Key': ADAFRUIT_IO_KEY },
        });

        if (!apiResponse.ok) {
            if (apiResponse.status === 404) {
                console.warn(`⚠️ Feed not found: ${feedKey}`);
                return null;
            }
            throw new Error(`Adafruit API error for '${feedKey}': ${apiResponse.status}`);
        }

        const data = await apiResponse.json();
        if (!data || !data.value) {
            console.warn(`⚠️ No data in feed: ${feedKey}`);
            return null;
        }

        // Parse JSON data
        let parsedValue;
        try {
            parsedValue = JSON.parse(data.value);
        } catch (e) {
            console.error(`❌ Invalid JSON in feed ${feedKey}:`, data.value);
            throw new Error(`Malformed JSON from feed ${feedKey}`);
        }

        return {
            ...parsedValue,
            last_updated: data.created_at
        };

    } catch (error) {
        console.error(`❌ Error fetching feed ${feedKey}:`, error.message);
        throw error;
    }
}

/**
 * Gets the current system status from ESP32
 */
async function getSystemStatus() {
    const espDataFeedKey = TOPIC_ESP_DATA.split('/').pop();

    try {
        const espData = await getFeedData(espDataFeedKey);
        
        return {
            esp_data: espData,
            server_timestamp: new Date().toISOString()
        };

    } catch (error) {
        console.error('❌ Error getting system status:', error.message);
        
        // Return partial data on error
        return {
            esp_data: null,
            server_timestamp: new Date().toISOString(),
            error: error.message
        };
    }
}
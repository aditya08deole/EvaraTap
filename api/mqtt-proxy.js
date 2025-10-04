const mqtt = require('mqtt');

// 🔒 SECURITY: These environment variables MUST be set in your Netlify project settings.
// ❌ NEVER commit actual credentials to GitHub - Adafruit will regenerate keys!
const MQTT_BROKER_HOST = process.env.MQTT_BROKER_HOST || 'io.adafruit.com';
const MQTT_BROKER_PORT = process.env.MQTT_BROKER_PORT || '1883';
const ADAFRUIT_IO_USERNAME = process.env.ADAFRUIT_IO_USERNAME;
const ADAFRUIT_IO_KEY = process.env.ADAFRUIT_IO_KEY;

// Validate required environment variables
if (!ADAFRUIT_IO_USERNAME || !ADAFRUIT_IO_KEY) {
    console.error('🚨 MISSING CREDENTIALS: ADAFRUIT_IO_USERNAME and ADAFRUIT_IO_KEY must be set in Netlify environment variables');
}

// Define topics - EXACTLY matching ESP32 topics
const TOPIC_ESP32_DATA = `${ADAFRUIT_IO_USERNAME}/feeds/esp32-data`;
const TOPIC_ESP32_COMMANDS = `${ADAFRUIT_IO_USERNAME}/feeds/esp32-commands`;

/**
 * Main handler for all incoming requests - EXACTLY like reference
 */
exports.handler = async function(event, context) {
    const request = {
        method: event.httpMethod,
        body: JSON.parse(event.body || '{}')
    };
    
    const response = {
        statusCode: 200,
        headers: {
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Headers': 'Content-Type',
            'Access-Control-Allow-Methods': 'POST, OPTIONS',
            'Content-Type': 'application/json'
        },
        body: ''
    };
    
    const sendResponse = (code, data) => {
        response.statusCode = code;
        response.body = JSON.stringify(data);
        return response;
    };
    // Handle CORS preflight
    if (request.method === 'OPTIONS') {
        return sendResponse(200, {});
    }
    
    if (request.method !== 'POST') {
        return sendResponse(405, { status: 'error', details: 'Method Not Allowed' });
    }
    
    if (!ADAFRUIT_IO_USERNAME || !ADAFRUIT_IO_KEY) {
        return sendResponse(500, { status: 'error', details: 'CRITICAL: MQTT credentials are not configured on the server.' });
    }

    const { action, payload } = request.body;

    try {
        if (action === 'send_esp32_command') {
            await handlePublish(TOPIC_ESP32_COMMANDS, payload);
            return sendResponse(200, { status: 'success', details: 'Command published.' });

        } else if (action === 'get_system_status') {
            const data = await handleGetSystemStatus();
            return sendResponse(200, { status: 'success', data });

        } else {
            return sendResponse(400, { status: 'error', details: 'Invalid action specified.' });
        }
    } catch (error) {
        console.error('[PROXY_ERROR]', error.message);
        return sendResponse(500, { status: 'error', details: error.message });
    }
}

/**
 * Publishes a command message to the specified MQTT topic - EXACTLY like reference
 */
function handlePublish(topic, command) {
    return new Promise((resolve, reject) => {
        const client = mqtt.connect(`mqtt://${MQTT_BROKER_HOST}:${MQTT_BROKER_PORT}`, {
            username: ADAFRUIT_IO_USERNAME,
            password: ADAFRUIT_IO_KEY,
            clientId: `netlify_proxy_pub_${Date.now()}`,
            reconnectPeriod: 0,
        });

        client.on('connect', () => {
            const messagePayload = JSON.stringify({ command: command });
            client.publish(topic, messagePayload, { retain: false }, (err) => {
                client.end();
                if (err) return reject(new Error('Failed to publish message.'));
                resolve();
            });
        });

        client.on('error', (err) => {
            client.end();
            reject(new Error(`MQTT connection failed: ${err.message}`));
        });
    });
}

/**
 * Fetches the last status message from a given Adafruit IO feed - EXACTLY like reference
 */
async function getFeedData(feedKey) {
    const apiUrl = `https://io.adafruit.com/api/v2/${ADAFRUIT_IO_USERNAME}/feeds/${feedKey}/data/last`;
    try {
        const apiResponse = await fetch(apiUrl, {
            headers: { 'X-AIO-Key': ADAFRUIT_IO_KEY },
        });

        if (!apiResponse.ok) {
            if (apiResponse.status === 404) {
                return null;
            }
            throw new Error(`Adafruit API request for '${feedKey}' failed with status ${apiResponse.status}`);
        }

        const data = await apiResponse.json();
        if (!data || !data.value) return null;

        const parsedValue = JSON.parse(data.value);
        return {
            ...parsedValue,
            last_updated: data.created_at
        };
    } catch (e) {
        if (e instanceof SyntaxError) {
            throw new Error(`Malformed JSON from feed ${feedKey}.`);
        }
        throw e;
    }
}

/**
 * Fetches the status from ESP32 data feed - matching reference pattern exactly
 */
async function handleGetSystemStatus() {
    const esp32FeedKey = TOPIC_ESP32_DATA.split('/').pop();

    const [esp32Result] = await Promise.allSettled([
        getFeedData(esp32FeedKey)
    ]);

    const esp32Data = esp32Result.status === 'fulfilled' ? esp32Result.value : null;
    
    return {
        esp32: esp32Data,
    };
}

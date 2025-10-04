// Simple test function to verify Netlify deployment
exports.handler = async (event, context) => {
    console.log('✅ Test function called with method:', event.httpMethod);
    
    return {
        statusCode: 200,
        headers: {
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Headers': 'Content-Type',
            'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            status: 'success',
            message: 'Netlify function is working!',
            method: event.httpMethod,
            timestamp: new Date().toISOString()
        })
    };
};
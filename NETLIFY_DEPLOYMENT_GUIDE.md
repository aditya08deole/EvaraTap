# 🌍 Netlify Deployment Guide for Global Access

## 🎯 Goal: Deploy EvaraTap dashboard globally on Netlify for worldwide remote monitoring

### 📋 Current Status
- ✅ ESP32 working and publishing data to Adafruit IO
- ✅ Local dashboard working with direct connection  
- ❌ Netlify deployment failing due to missing environment variables

## 🔧 Step-by-Step Netlify Setup

### Step 1: Push Code to GitHub
Your code is already in GitHub repository: `aditya08deole/EvaraTap`

### Step 2: Connect Netlify to GitHub
1. Go to [netlify.com](https://netlify.com)
2. Click "Add new site" → "Import an existing project"
3. Choose GitHub and select your `EvaraTap` repository
4. Deploy settings:
   - **Build command**: Leave empty (static site)
   - **Publish directory**: `/` (root folder)
   - Click "Deploy site"

### Step 3: Set Environment Variables (CRITICAL!)
After deployment, in Netlify dashboard:

1. Go to **Site settings** → **Environment variables**
2. Add these variables:

```
ADAFRUIT_IO_USERNAME = ADI08
ADAFRUIT_IO_KEY = YOUR_ADAFRUIT_IO_KEY_HERE
MQTT_BROKER_HOST = io.adafruit.com  
MQTT_BROKER_PORT = 1883
```

### Step 4: Redeploy Site
- Go to **Deploys** → **Trigger deploy** → **Deploy site**
- Wait for deployment to complete

### Step 5: Test Your Global Dashboard
- Access your Netlify URL: `https://your-site-name.netlify.app`
- Should show ONLINE status
- Test controls from any device worldwide! 🌍

## 🚀 Expected Result
- **Your URL**: `https://evaratap-yourname.netlify.app`
- **Global access**: Monitor from phone, laptop, anywhere
- **Real-time control**: Open/close valve remotely
- **Live data**: See flow rates and volume from anywhere

## 🔒 Security Note
Environment variables keep your Adafruit IO keys secure - they're not visible in the website code but work on the server.

---
**Once you set the environment variables, your dashboard will be globally accessible! 🌍**